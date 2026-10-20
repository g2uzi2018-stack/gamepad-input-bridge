#pragma once
#include "backends/backend.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace gamepad::detail {
constexpr std::uint32_t physical_mask = 0xF3FF;
constexpr std::array<Button, 16> buttons = {
    Button::dpad_up, Button::dpad_down, Button::dpad_left, Button::dpad_right,
    Button::start, Button::back, Button::left_stick, Button::right_stick,
    Button::left_shoulder, Button::right_shoulder, Button::a, Button::b,
    Button::x, Button::y, Button::left_trigger, Button::right_trigger
};
inline bool analog_equal(const RawState& a, const RawState& b) noexcept {
    return a.left_x == b.left_x && a.left_y == b.left_y &&
           a.right_x == b.right_x && a.right_y == b.right_y &&
           a.left_trigger == b.left_trigger && a.right_trigger == b.right_trigger;
}
inline bool info_equal(const DeviceInfo& a, const DeviceInfo& b) noexcept {
    return a.slot == b.slot && a.subtype == b.subtype &&
           a.capability_flags == b.capability_flags &&
           a.capabilities_known == b.capabilities_known &&
           a.battery_type == b.battery_type && a.battery_level == b.battery_level;
}
inline void normalize_stick(std::int16_t raw_x, std::int16_t raw_y,
                            float deadzone, float& x, float& y) noexcept {
    x = static_cast<float>(raw_x) / (raw_x < 0 ? 32768.0f : 32767.0f);
    y = static_cast<float>(raw_y) / (raw_y < 0 ? 32768.0f : 32767.0f);
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude <= deadzone || magnitude == 0) { x = 0; y = 0; return; }
    const float scale = (std::min(magnitude, 1.0f) - deadzone) /
                        ((1.0f - deadzone) * magnitude);
    x *= scale; y *= scale;
}

// Clock and transport independent. Every tick checks hold timers, even if the
// backend packet number has not changed. No allocations in process/disconnect.
class EventEngine {
public:
    explicit EventEngine(const Config& config) : config_(config) {
        state_.device.slot = config.slot;
    }
    const State& state() const noexcept { return state_; }

    template<class Emit>
    void process(const Sample& sample, std::uint64_t now, Emit&& emit) {
        if (sample.status != ReadStatus::connected) {
            const auto reason = sample.status == ReadStatus::error
                ? CancelReason::backend_error : CancelReason::disconnected;
            disconnect(now, reason, emit);
            if (sample.status == ReadStatus::error && sample.error != last_error_) {
                send(EventType::backend_error, now, emit, Button::none, 0,
                     CancelReason::none, sample.error);
            }
            last_error_ = sample.status == ReadStatus::error ? sample.error : 0;
            return;
        }
        last_error_ = 0;
        const State previous = state_;
        state_.connected = true;
        state_.raw = sample.raw;
        state_.raw.buttons &= physical_mask;
        state_.device = sample.device;
        state_.buttons = state_.raw.buttons;
        synthesize_trigger(Button::left_trigger, sample.raw.left_trigger, previous);
        synthesize_trigger(Button::right_trigger, sample.raw.right_trigger, previous);
        normalize_stick(sample.raw.left_x, sample.raw.left_y, config_.left_deadzone,
                        state_.left_x, state_.left_y);
        normalize_stick(sample.raw.right_x, sample.raw.right_y, config_.right_deadzone,
                        state_.right_x, state_.right_y);
        state_.left_trigger = sample.raw.left_trigger / 255.0f;
        state_.right_trigger = sample.raw.right_trigger / 255.0f;

        if (!previous.connected) {
            presses_ = {};
            // Keys held before connection have unknown start times: no invented
            // down/short/long event; their eventual release is cancelled.
            last_analog_ = state_.raw;
            last_analog_time_ = now;
            send(EventType::connected, now, emit);
            return;
        }
        if (!info_equal(previous.device, state_.device))
            send(EventType::device_info_changed, now, emit);

        const auto threshold = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(config_.long_press_threshold).count());
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const Button button = buttons[i];
            const bool was = previous.pressed(button), is = state_.pressed(button);
            auto& press = presses_[i];
            if (!was && is) {
                press = {now, true, false};
                send(EventType::button_down, now, emit, button);
            } else if (was && !is) {
                const auto held = press.armed ? now - press.began : 0;
                if (press.armed && !press.long_sent && held >= threshold) {
                    send(EventType::long_press, now, emit, button, held);
                    press.long_sent = true;
                }
                send(EventType::button_up, now, emit, button, held,
                     press.armed ? CancelReason::none : CancelReason::initially_held);
                if (press.armed && !press.long_sent)
                    send(EventType::short_press, now, emit, button, held);
                press = {};
            }
            if (is && press.armed && !press.long_sent && now - press.began >= threshold) {
                send(EventType::long_press, now, emit, button, now - press.began);
                press.long_sent = true;
            }
        }
        const auto interval = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(config_.analog_event_interval).count());
        if (!analog_equal(last_analog_, state_.raw) && now - last_analog_time_ >= interval) {
            send(EventType::state_changed, now, emit);
            last_analog_ = state_.raw;
            last_analog_time_ = now;
        }
    }

    template<class Emit>
    void disconnect(std::uint64_t now, CancelReason reason, Emit&& emit) {
        if (!state_.connected) return;
        const State previous = state_;
        state_ = {};
        state_.device = previous.device;
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            if (previous.pressed(buttons[i])) {
                send(EventType::button_up, now, emit, buttons[i],
                     presses_[i].armed ? now - presses_[i].began : 0, reason);
            }
        }
        presses_ = {};
        send(EventType::disconnected, now, emit, Button::none, 0, reason);
    }

private:
    struct Press { std::uint64_t began = 0; bool armed = false, long_sent = false; };
    const Config& config_;
    State state_;
    std::array<Press, buttons.size()> presses_{};
    RawState last_analog_;
    std::uint64_t last_analog_time_ = 0;
    std::uint32_t last_error_ = 0;

    void synthesize_trigger(Button button, std::uint8_t value, const State& old) noexcept {
        if (value >= config_.trigger_press_threshold ||
            (old.pressed(button) && value > config_.trigger_release_threshold))
            state_.buttons |= mask(button);
    }
    template<class Emit>
    void send(EventType type, std::uint64_t now, Emit& emit, Button button = Button::none,
              std::uint64_t held = 0, CancelReason reason = CancelReason::none,
              std::uint32_t error = 0) {
        Event event;
        event.type = type; event.timestamp_ns = now; event.state = state_;
        event.button = button; event.held_ns = held;
        event.cancelled = reason; event.native_error = error;
        emit(event);
    }
};
} // namespace gamepad::detail
