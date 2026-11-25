#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <system_error>

namespace gamepad {
struct RumblePulse {
    std::uint16_t strong;
    std::uint16_t weak;
    std::chrono::milliseconds duration;
};

// 单次设备定时脉冲，调用方可沿用预设，也可独立设置两路马达。
namespace rumble {
inline constexpr RumblePulse long_confirmation{65535, 65535, std::chrono::milliseconds{700}};
inline constexpr RumblePulse short_confirmation{52428, 39321, std::chrono::milliseconds{130}};
inline constexpr RumblePulse step{42598, 29491, std::chrono::milliseconds{90}};
inline constexpr RumblePulse limit{36044, 18022, std::chrono::milliseconds{160}};
} // namespace rumble

// Linux EV_FF output on the same persistent event path as the input reader.
// Device-timed pulse. Closing the object removes all effects owned by this fd.
class EvdevRumble final {
public:
    explicit EvdevRumble(std::string event_path);
    ~EvdevRumble();
    EvdevRumble(const EvdevRumble&) = delete;
    EvdevRumble& operator=(const EvdevRumble&) = delete;
    std::error_code pulse(std::uint16_t strong, std::uint16_t weak,
                          std::chrono::milliseconds duration);
    std::error_code pulse(RumblePulse feedback) {
        return pulse(feedback.strong, feedback.weak, feedback.duration);
    }
private:
    void close_device();
    std::string path_;
    int fd_ = -1;
    int effect_ = -1;
};
} // namespace gamepad
