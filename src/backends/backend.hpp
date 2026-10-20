#pragma once
#include "gamepad/gamepad.hpp"

namespace gamepad::detail {
enum class ReadStatus { connected, disconnected, error };
struct Sample {
    ReadStatus status = ReadStatus::disconnected;
    RawState raw;
    DeviceInfo device;
    std::uint32_t error = 0;
};
class Backend {
public:
    virtual ~Backend() = default;
    // Called only by the owning worker. Must not throw or wait indefinitely.
    virtual Sample read() noexcept = 0;
};
std::unique_ptr<Backend> make_native_backend(const Config& config);
} // namespace gamepad::detail
