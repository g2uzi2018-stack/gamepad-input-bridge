#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <system_error>

namespace gamepad {
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
private:
    void close_device();
    std::string path_;
    int fd_ = -1;
    int effect_ = -1;
};
} // namespace gamepad
