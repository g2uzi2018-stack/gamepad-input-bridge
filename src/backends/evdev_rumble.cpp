#include "gamepad/evdev_rumble.hpp"

#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace gamepad {
EvdevRumble::EvdevRumble(std::string path) : path_(std::move(path)) {}
EvdevRumble::~EvdevRumble() { close_device(); }

void EvdevRumble::close_device() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = effect_ = -1;
}

std::error_code EvdevRumble::pulse(std::uint16_t strong, std::uint16_t weak,
                                   std::chrono::milliseconds duration) {
    if (duration.count() <= 0 || duration.count() > 1000)
        return std::make_error_code(std::errc::invalid_argument);
    auto fail = [this](int error) {
        close_device();
        return std::error_code(error, std::generic_category());
    };
    struct stat current{}, opened{};
    if (::stat(path_.c_str(), &current) < 0) return fail(errno);
    if (fd_ >= 0 && (::fstat(fd_, &opened) < 0 || current.st_ino != opened.st_ino ||
                     current.st_dev != opened.st_dev || current.st_rdev != opened.st_rdev))
        close_device();
    if (fd_ < 0) {
        fd_ = ::open(path_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) return fail(errno);
        constexpr auto bits = sizeof(unsigned long) * CHAR_BIT;
        unsigned long supported[FF_MAX / bits + 1]{};
        if (::ioctl(fd_, EVIOCGBIT(EV_FF, sizeof(supported)), supported) < 0) return fail(errno);
        if (!(supported[FF_RUMBLE / bits] & (1UL << (FF_RUMBLE % bits)))) return fail(ENOTSUP);
    }
    ff_effect effect{};
    effect.type = FF_RUMBLE;
    effect.id = static_cast<__s16>(effect_);
    effect.u.rumble.strong_magnitude = strong;
    effect.u.rumble.weak_magnitude = weak;
    effect.replay.length = static_cast<__u16>(duration.count());
    if (::ioctl(fd_, EVIOCSFF, &effect) < 0) return fail(errno);
    effect_ = effect.id;
    input_event play{};
    play.type = EV_FF;
    play.code = static_cast<__u16>(effect_);
    play.value = 1;
    const auto written = ::write(fd_, &play, sizeof(play));
    if (written != sizeof(play)) return fail(written < 0 ? errno : EIO);
    return {};
}
} // namespace gamepad
