#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include "backend.hpp"

namespace gamepad::detail {
namespace {
class XInputBackend final : public Backend {
public:
    explicit XInputBackend(std::uint32_t slot) : slot_(slot) { info_.slot = slot; }
    Sample read() noexcept override {
        XINPUT_STATE input{};
        const DWORD result = XInputGetState(slot_, &input);
        Sample sample;
        sample.device = info_;
        if (result != ERROR_SUCCESS) {
            connected_ = false;
            sample.status = result == ERROR_DEVICE_NOT_CONNECTED
                ? ReadStatus::disconnected : ReadStatus::error;
            sample.error = result;
            return sample;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!connected_ || now >= next_metadata_) {
            refresh_info();
            next_metadata_ = now + std::chrono::seconds(5);
        }
        connected_ = true;
        sample.status = ReadStatus::connected;
        sample.device = info_;
        sample.raw = {input.dwPacketNumber, input.Gamepad.wButtons,
            input.Gamepad.sThumbLX, input.Gamepad.sThumbLY,
            input.Gamepad.sThumbRX, input.Gamepad.sThumbRY,
            input.Gamepad.bLeftTrigger, input.Gamepad.bRightTrigger};
        return sample;
    }
private:
    std::uint32_t slot_;
    bool connected_ = false;
    DeviceInfo info_;
    std::chrono::steady_clock::time_point next_metadata_{};
    void refresh_info() noexcept {
        info_ = {};
        info_.slot = slot_;
        XINPUT_CAPABILITIES capabilities{};
        if (XInputGetCapabilities(slot_, 0, &capabilities) == ERROR_SUCCESS) {
            info_.capabilities_known = true;
            info_.capability_flags = capabilities.Flags;
            info_.subtype = capabilities.SubType;
        }
        XINPUT_BATTERY_INFORMATION battery{};
        if (XInputGetBatteryInformation(slot_, BATTERY_DEVTYPE_GAMEPAD, &battery) == ERROR_SUCCESS) {
            switch (battery.BatteryType) {
            case BATTERY_TYPE_WIRED: info_.battery_type = BatteryType::wired; break;
            case BATTERY_TYPE_ALKALINE: info_.battery_type = BatteryType::alkaline; break;
            case BATTERY_TYPE_NIMH: info_.battery_type = BatteryType::nimh; break;
            case BATTERY_TYPE_DISCONNECTED: info_.battery_type = BatteryType::disconnected; break;
            default: info_.battery_type = BatteryType::unknown; break;
            }
            if ((info_.battery_type == BatteryType::alkaline || info_.battery_type == BatteryType::nimh)
                && battery.BatteryLevel <= BATTERY_LEVEL_FULL)
                info_.battery_level = static_cast<std::int8_t>(battery.BatteryLevel);
        }
    }
};
} // namespace
std::unique_ptr<Backend> make_native_backend(const Config& config) {
    return std::make_unique<XInputBackend>(config.slot);
}
} // namespace gamepad::detail
