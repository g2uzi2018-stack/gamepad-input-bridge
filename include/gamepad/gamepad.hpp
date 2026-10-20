#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace gamepad {

enum class Button : std::uint32_t {
    none = 0, dpad_up = 0x0001, dpad_down = 0x0002,
    dpad_left = 0x0004, dpad_right = 0x0008,
    start = 0x0010, back = 0x0020, left_stick = 0x0040,
    right_stick = 0x0080, left_shoulder = 0x0100,
    right_shoulder = 0x0200, a = 0x1000, b = 0x2000,
    x = 0x4000, y = 0x8000, left_trigger = 0x10000,
    right_trigger = 0x20000
};

constexpr std::uint32_t mask(Button b) noexcept {
    return static_cast<std::uint32_t>(b);
}

enum class EventType : std::uint8_t {
    connected, disconnected, state_changed, button_down, button_up,
    short_press, long_press, device_info_changed, backend_error, stopped
};
enum class LogLevel : std::uint8_t { off, error, warning, info, debug };
enum class WaitResult : std::uint8_t { event, timeout, stopped };
enum class CancelReason : std::uint8_t { none, disconnected, stopped, backend_error, initially_held };
enum class BatteryType : std::uint8_t { unknown, wired, alkaline, nimh, disconnected };

struct RawState {
    std::uint32_t packet = 0; // Backend packet counter, not an event count.
    std::uint32_t buttons = 0;
    std::int16_t left_x = 0, left_y = 0, right_x = 0, right_y = 0;
    std::uint8_t left_trigger = 0, right_trigger = 0;
};

struct DeviceInfo {
    std::uint32_t slot = 0;
    std::uint16_t capability_flags = 0;
    std::uint8_t subtype = 0;
    bool capabilities_known = false;
    BatteryType battery_type = BatteryType::unknown;
    // -1 means unavailable; 0..3 is the coarse XInput battery level.
    std::int8_t battery_level = -1;
};

struct State {
    bool connected = false;
    RawState raw;
    // Includes synthesized LT/RT buttons with configurable hysteresis.
    std::uint32_t buttons = 0;
    float left_x = 0, left_y = 0, right_x = 0, right_y = 0;
    float left_trigger = 0, right_trigger = 0;
    DeviceInfo device;
    bool pressed(Button b) const noexcept { return (buttons & mask(b)) != 0; }
};

struct Event {
    std::uint64_t sequence = 0;
    // Nanoseconds since this object's worker started; steady clock, not UTC.
    std::uint64_t timestamp_ns = 0;
    std::uint64_t held_ns = 0;
    // Cumulative number of discarded events, sampled when this event is popped.
    std::uint64_t dropped_total = 0;
    EventType type = EventType::state_changed;
    Button button = Button::none;
    CancelReason cancelled = CancelReason::none;
    std::uint32_t native_error = 0;
    State state; // Complete state at event generation, even after queue overflow.
};

struct Config {
    std::string device_label = "generic_xinput"; // Descriptive, not identity verification.
    std::uint32_t slot = 0;
    std::size_t queue_capacity = 256;
    std::chrono::microseconds poll_interval{4000};
    std::chrono::milliseconds reconnect_interval{1000};
    std::chrono::milliseconds long_press_threshold{500};
    // Analog changes may be coalesced; digital edges are always immediate.
    // Set to zero to emit every observed analog change.
    std::chrono::microseconds analog_event_interval{8000};
    float left_deadzone = 0.12f, right_deadzone = 0.12f;
    std::uint8_t trigger_press_threshold = 30;
    std::uint8_t trigger_release_threshold = 25;
    LogLevel log_level = LogLevel::warning;
    bool log_to_console = true;
    std::string log_file; // Empty disables file logging. Parent must already exist.
    std::size_t log_max_bytes = 1024 * 1024;
    std::size_t log_backups = 2;
};

struct Statistics {
    std::uint64_t produced = 0, consumed = 0, dropped = 0;
    std::uint64_t polls = 0, read_errors = 0, missed_deadlines = 0;
    std::size_t queue_depth = 0, high_watermark = 0;
    bool running = false;
};

namespace detail { class Backend; struct TestAccess; }

// One object owns one slot, one worker, and one bounded FIFO.
// Consumption APIs may be called from any thread but share ONE stream:
// concurrent consumers split events rather than broadcast them.
class Gamepad final {
public:
    explicit Gamepad(Config config = {});
    ~Gamepad();
    Gamepad(const Gamepad&) = delete;
    Gamepad& operator=(const Gamepad&) = delete;
    Gamepad(Gamepad&&) = delete;
    Gamepad& operator=(Gamepad&&) = delete;

    bool try_pop(Event& event);
    WaitResult wait_pop(Event& event, std::chrono::milliseconds timeout);

    // Callback runs synchronously on the CALLING thread. An exception propagates
    // to that thread; the popped event is consumed, and the worker keeps running.
    template<class Handler>
    std::size_t dispatch_pending(Handler&& handler, std::size_t max_count = 64) {
        Event event;
        std::size_t count = 0;
        while (count < max_count && try_pop(event)) {
            handler(event);
            ++count;
        }
        return count;
    }

    State snapshot() const;
    Statistics statistics() const;
    bool running() const noexcept;
    // Idempotent, safe to call concurrently. Joins the worker before returning.
    // Queued events remain drainable; wait_pop returns stopped once drained.
    void stop() noexcept;

private:
    Gamepad(Config config, std::unique_ptr<detail::Backend> backend);
    friend struct detail::TestAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* to_string(EventType value) noexcept;
const char* to_string(Button value) noexcept;
const char* to_string(CancelReason value) noexcept;

} // namespace gamepad
