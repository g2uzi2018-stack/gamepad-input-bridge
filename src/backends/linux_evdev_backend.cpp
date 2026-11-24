#include "backend.hpp"
#include "linux_evdev_slot.hpp"

#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>

namespace gamepad::detail {
namespace {
struct KeyBinding { unsigned code; Button button; };
constexpr KeyBinding keys[] = {
    {BTN_A, Button::a}, {BTN_B, Button::b}, {BTN_X, Button::x}, {BTN_Y, Button::y},
    {BTN_TL, Button::left_shoulder}, {BTN_TR, Button::right_shoulder},
    {BTN_START, Button::start}, {BTN_SELECT, Button::back},
    {BTN_THUMBL, Button::left_stick}, {BTN_THUMBR, Button::right_stick}
};
constexpr std::array<unsigned, 8> axes{ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};

template <std::size_t N>
bool has_bit(const std::array<unsigned long, N>& bits, unsigned code) noexcept {
    constexpr auto width = sizeof(unsigned long) * CHAR_BIT;
    return (bits[code / width] & (1UL << (code % width))) != 0;
}
using KeyBits = std::array<unsigned long, KEY_MAX / (sizeof(unsigned long) * CHAR_BIT) + 1>;

int query(int fd, unsigned long request, void* data) noexcept {
    int result;
    do { result = ::ioctl(fd, request, data); } while (result < 0 && errno == EINTR);
    return result < 0 ? errno : 0;
}

struct JoystickPaths {
    glob_t entries{};
    ~JoystickPaths() { ::globfree(&entries); }
};

} // namespace

// joydev owns system-wide jsN assignments. Removing js0 does not renumber js1.
int open_linux_slot(const char* input_class, const char* input_nodes, unsigned slot, int& fd) noexcept {
    char pattern[PATH_MAX];
    if (std::snprintf(pattern, sizeof(pattern), "%s/js%u/device/event[0-9]*", input_class, slot)
        >= static_cast<int>(sizeof(pattern))) return ENAMETOOLONG;
    JoystickPaths paths;
    const int result = ::glob(pattern, GLOB_NOSORT | GLOB_ERR, nullptr, &paths.entries);
    if (result == GLOB_NOMATCH) return ENODEV;
    if (result == GLOB_NOSPACE) return ENOMEM;
    if (result != 0) return errno ? errno : EIO;
    if (paths.entries.gl_pathc != 1) return ENOTSUP;
    const char* sys_path = paths.entries.gl_pathv[0];
    char node[PATH_MAX];
    if (std::snprintf(node, sizeof(node), "%s/%s", input_nodes, std::strrchr(sys_path, '/') + 1)
        >= static_cast<int>(sizeof(node))) return ENAMETOOLONG;
    fd = ::open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return errno;
    // If unplug raced with open, eventN may already belong to a different slot.
    if (::access(sys_path, F_OK) < 0) {
        const int error = errno;
        ::close(fd);
        fd = -1;
        return error;
    }
    return 0;
}

namespace {
int check_layout(int fd) noexcept {
    KeyBits key_bits{};
    int error = query(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data());
    if (error) return error;
    for (const auto& key : keys) if (!has_bit(key_bits, key.code)) return ENOTSUP;
    std::array<unsigned long, ABS_MAX / (sizeof(unsigned long) * CHAR_BIT) + 1> abs_bits{};
    error = query(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits.data());
    if (error) return error;
    for (unsigned axis : axes) if (!has_bit(abs_bits, axis)) return ENOTSUP;
    return 0;
}

// Query the kernel's current state, including controls held before open. Like
// XInput polling, this cannot report a complete tap between successive polls.
int snapshot(int fd, RawState& raw) noexcept {
    KeyBits pressed{};
    int error = query(fd, EVIOCGKEY(sizeof(pressed)), pressed.data());
    if (error) return error;
    for (const auto& key : keys) if (has_bit(pressed, key.code)) raw.buttons |= mask(key.button);

    std::array<input_absinfo, axes.size()> values{};
    for (std::size_t i = 0; i < axes.size(); ++i) {
        error = query(fd, EVIOCGABS(axes[i]), &values[i]);
        if (error) return error;
        if (values[i].minimum >= values[i].maximum) return ENOTSUP;
    }
    // Integer interpolation maps both endpoints exactly and avoids overflow for 32-bit ranges.
    auto scaled = [&](std::size_t i, int full) {
        const auto& axis = values[i];
        const std::int64_t span = static_cast<std::int64_t>(axis.maximum) - axis.minimum;
        const std::int64_t offset = std::clamp(axis.value, axis.minimum, axis.maximum)
                                    - static_cast<std::int64_t>(axis.minimum);
        return static_cast<int>((offset * full + span / 2) / span);
    };
    raw.left_x = static_cast<std::int16_t>(scaled(0, 65535) - 32768);
    raw.left_y = static_cast<std::int16_t>(32767 - scaled(1, 65535));
    raw.right_x = static_cast<std::int16_t>(scaled(2, 65535) - 32768);
    raw.right_y = static_cast<std::int16_t>(32767 - scaled(3, 65535));
    raw.left_trigger = static_cast<std::uint8_t>(scaled(4, 255));
    raw.right_trigger = static_cast<std::uint8_t>(scaled(5, 255));
    if (values[6].value < 0) raw.buttons |= mask(Button::dpad_left);
    if (values[6].value > 0) raw.buttons |= mask(Button::dpad_right);
    if (values[7].value < 0) raw.buttons |= mask(Button::dpad_up);
    if (values[7].value > 0) raw.buttons |= mask(Button::dpad_down);
    return 0;
}

class EvdevBackend final : public Backend {
public:
    explicit EvdevBackend(const Config& config) : event_path_(config.linux_event_path) { info_.slot = config.slot; }
    ~EvdevBackend() override { if (fd_ >= 0) ::close(fd_); }

    Sample read() noexcept override {
        Sample sample;
        sample.device = info_;
        const bool opening = fd_ < 0;
        int error = 0;
        if (opening) {
            if (event_path_.empty()) {
                error = open_linux_slot("/sys/class/input", "/dev/input", info_.slot, fd_);
            } else {
                fd_ = ::open(event_path_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd_ < 0) error = errno;
            }
            if (!error) error = check_layout(fd_);
        }
        if (!error) error = snapshot(fd_, sample.raw);
        if (error) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = -1;
            if (error != ENODEV && error != ENOENT && error != ENXIO) {
                sample.status = ReadStatus::error;
                sample.error = static_cast<std::uint32_t>(error);
            }
            return sample;
        }
        const auto& current = sample.raw;
        if (opening || current.buttons != last_.buttons || current.left_x != last_.left_x ||
            current.left_y != last_.left_y || current.right_x != last_.right_x ||
            current.right_y != last_.right_y || current.left_trigger != last_.left_trigger ||
            current.right_trigger != last_.right_trigger) ++packet_;
        sample.raw.packet = packet_;
        last_ = sample.raw;
        sample.status = ReadStatus::connected;
        return sample;
    }
private:
    const std::string event_path_;
    DeviceInfo info_;
    RawState last_;
    int fd_ = -1;
    std::uint32_t packet_ = 0;
};
} // namespace

std::unique_ptr<Backend> make_native_backend(const Config& config) {
    return std::make_unique<EvdevBackend>(config);
}
} // namespace gamepad::detail
