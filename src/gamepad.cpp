#include "gamepad/gamepad.hpp"
#include "backends/backend.hpp"
#include "core/event_engine.hpp"
#include "core/event_queue.hpp"
#include "core/logger.hpp"
#include "platform/waiter.hpp"
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace gamepad {
namespace {
Config validate(Config config) {
    if (config.slot > 3) throw std::invalid_argument("XInput slot must be 0..3");
    if (config.queue_capacity == 0 || config.queue_capacity > 1048576)
        throw std::invalid_argument("queue_capacity must be 1..1048576");
    if (config.poll_interval < std::chrono::microseconds(1000) ||
        config.poll_interval > std::chrono::seconds(1))
        throw std::invalid_argument("poll_interval must be 1ms..1s");
    if (config.reconnect_interval < std::chrono::milliseconds(10) ||
        config.reconnect_interval > std::chrono::minutes(1))
        throw std::invalid_argument("reconnect_interval must be 10ms..1min");
    if (config.long_press_threshold < std::chrono::milliseconds(1) ||
        config.long_press_threshold > std::chrono::hours(1))
        throw std::invalid_argument("long_press_threshold must be 1ms..1h");
    if (config.analog_event_interval < std::chrono::microseconds(0) ||
        config.analog_event_interval > std::chrono::seconds(1))
        throw std::invalid_argument("analog_event_interval must be 0..1s");
    if (!std::isfinite(config.left_deadzone) || !std::isfinite(config.right_deadzone) ||
        config.left_deadzone < 0 || config.left_deadzone >= 1 ||
        config.right_deadzone < 0 || config.right_deadzone >= 1)
        throw std::invalid_argument("stick deadzones must be finite values in [0,1)");
    if (config.trigger_press_threshold == 0 ||
        config.trigger_release_threshold >= config.trigger_press_threshold)
        throw std::invalid_argument("trigger thresholds require 0 <= release < press <= 255");
    if (config.log_max_bytes < 4096 || config.log_backups > 10)
        throw std::invalid_argument("log_max_bytes must be >= 4096 and log_backups <= 10");
    if (config.log_level > LogLevel::debug) throw std::invalid_argument("invalid log level");
    return config;
}
} // namespace

struct Gamepad::Impl {
    Config config;
    std::unique_ptr<detail::Backend> backend;
    detail::EventQueue queue;
    detail::Logger logger;
    detail::EventEngine engine;
    std::atomic<bool> stop_requested{false}, active{true};
    std::atomic<std::uint64_t> polls{0}, errors{0}, missed{0};
    std::mutex join_mutex;
    detail::DeadlineWaiter waiter;
    std::thread worker; // Constructed last, after all members are ready.

    Impl(Config input, std::unique_ptr<detail::Backend> input_backend)
        : config(validate(std::move(input))),
          backend(input_backend ? std::move(input_backend) : detail::make_native_backend(config)),
          queue(config.queue_capacity), logger(config), engine(config) {
        // If thread creation throws, every previously constructed member unwinds.
        worker = std::thread([this] { run(); });
    }
    ~Impl() { stop(); }
    void stop() noexcept {
        stop_requested.store(true, std::memory_order_release);
        waiter.request_stop();
        std::lock_guard<std::mutex> lock(join_mutex);
        if (worker.joinable()) worker.join();
    }
    void run() noexcept {
        using Clock = std::chrono::steady_clock;
        const auto origin = Clock::now();
        auto deadline = origin;
        std::uint64_t last_overflow_log = 0;
        bool overflow_logged = false;
        std::uint32_t last_error = 0;
        const auto elapsed = [&] {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - origin).count());
        };
        auto emit = [&](Event event) {
            const bool overflow = queue.push(event);
            if (event.type == EventType::connected)
                logger.write(LogLevel::info, "controller connected");
            else if (event.type == EventType::disconnected)
                logger.write(LogLevel::info, "controller disconnected", static_cast<unsigned>(event.cancelled));
            if (overflow && (!overflow_logged || event.timestamp_ns - last_overflow_log >= 1000000000ULL)) {
                logger.write(LogLevel::warning, "queue overflow; discarded oldest events; total", queue.statistics().dropped);
                last_overflow_log = event.timestamp_ns;
                overflow_logged = true;
            }
        };
        logger.write(LogLevel::info, "capture worker started; poll_us", static_cast<std::uint64_t>(config.poll_interval.count()));
        if (!waiter.high_resolution())
            logger.write(LogLevel::warning, "high-resolution timer unavailable; polling precision may be reduced");
        try {
            while (!stop_requested.load(std::memory_order_acquire)) {
                const auto sample = backend->read();
                polls.fetch_add(1, std::memory_order_relaxed);
                const auto now = elapsed();
                if (sample.status == detail::ReadStatus::error) {
                    errors.fetch_add(1, std::memory_order_relaxed);
                    if (sample.error != last_error)
                        logger.write(LogLevel::error, "backend read failed; native_error", sample.error);
                    last_error = sample.error;
                } else { last_error = 0; }
                engine.process(sample, now, emit);
                queue.publish(engine.state());
                const auto interval = sample.status == detail::ReadStatus::connected
                    ? config.poll_interval
                    : std::chrono::duration_cast<std::chrono::microseconds>(config.reconnect_interval);
                deadline += interval;
                const auto after = Clock::now();
                if (deadline <= after) {
                    missed.fetch_add(1, std::memory_order_relaxed);
                    deadline = after + interval; // Never spin to catch up missed ticks.
                }
                waiter.wait_until(deadline);
            }
        } catch (...) {
            // Preserve a diagnostic event and always close/wake consumers.
            logger.write(LogLevel::error, "capture worker failed unexpectedly");
            Event event;
            event.type = EventType::backend_error; event.timestamp_ns = elapsed();
            event.native_error = 0xFFFFFFFFU; event.state = engine.state();
            queue.push(event);
        }
        try {
            engine.disconnect(elapsed(), CancelReason::stopped, emit);
            Event event;
            event.type = EventType::stopped; event.timestamp_ns = elapsed();
            event.state = engine.state();
            queue.push(event);
        } catch (...) { logger.write(LogLevel::error, "shutdown event generation failed"); }
        active.store(false, std::memory_order_release);
        queue.close();
        logger.write(LogLevel::info, "capture worker stopped; polls", polls.load(std::memory_order_relaxed));
        logger.write(LogLevel::info, "queue dropped total", queue.statistics().dropped);
    }
};

Gamepad::Gamepad(Config config) : Gamepad(std::move(config), nullptr) {}
Gamepad::Gamepad(Config config, std::unique_ptr<detail::Backend> backend)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(backend))) {}
Gamepad::~Gamepad() = default;
void Gamepad::stop() noexcept { impl_->stop(); }
bool Gamepad::running() const noexcept { return impl_->active.load(std::memory_order_acquire); }
bool Gamepad::try_pop(Event& event) { return impl_->queue.try_pop(event); }
WaitResult Gamepad::wait_pop(Event& event, std::chrono::milliseconds timeout) {
    if (timeout < std::chrono::milliseconds(0)) throw std::invalid_argument("timeout cannot be negative");
    return impl_->queue.wait_pop(event, timeout);
}
State Gamepad::snapshot() const { return impl_->queue.snapshot(); }
Statistics Gamepad::statistics() const {
    auto stats = impl_->queue.statistics();
    stats.running = running();
    stats.polls = impl_->polls.load(std::memory_order_relaxed);
    stats.read_errors = impl_->errors.load(std::memory_order_relaxed);
    stats.missed_deadlines = impl_->missed.load(std::memory_order_relaxed);
    return stats;
}

const char* to_string(EventType value) noexcept {
    switch (value) {
#define GAMEPAD_CASE(v) case EventType::v: return #v
    GAMEPAD_CASE(connected); GAMEPAD_CASE(disconnected); GAMEPAD_CASE(state_changed);
    GAMEPAD_CASE(button_down); GAMEPAD_CASE(button_up); GAMEPAD_CASE(short_press);
    GAMEPAD_CASE(long_press); GAMEPAD_CASE(device_info_changed); GAMEPAD_CASE(backend_error);
    GAMEPAD_CASE(stopped);
#undef GAMEPAD_CASE
    }
    return "unknown";
}
const char* to_string(Button value) noexcept {
    switch (value) {
#define GAMEPAD_CASE(v) case Button::v: return #v
    GAMEPAD_CASE(none); GAMEPAD_CASE(dpad_up); GAMEPAD_CASE(dpad_down);
    GAMEPAD_CASE(dpad_left); GAMEPAD_CASE(dpad_right); GAMEPAD_CASE(start);
    GAMEPAD_CASE(back); GAMEPAD_CASE(left_stick); GAMEPAD_CASE(right_stick);
    GAMEPAD_CASE(left_shoulder); GAMEPAD_CASE(right_shoulder); GAMEPAD_CASE(a);
    GAMEPAD_CASE(b); GAMEPAD_CASE(x); GAMEPAD_CASE(y);
    GAMEPAD_CASE(left_trigger); GAMEPAD_CASE(right_trigger);
#undef GAMEPAD_CASE
    }
    return "unknown";
}
const char* to_string(CancelReason value) noexcept {
    switch (value) {
    case CancelReason::none: return "none";
    case CancelReason::disconnected: return "disconnected";
    case CancelReason::stopped: return "stopped";
    case CancelReason::backend_error: return "backend_error";
    case CancelReason::initially_held: return "initially_held";
    }
    return "unknown";
}
} // namespace gamepad
