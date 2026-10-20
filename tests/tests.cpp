#include "test_access.hpp"
#include "core/event_engine.hpp"
#include "core/event_queue.hpp"
#include "core/logger.hpp"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace gamepad;
using namespace gamepad::detail;

namespace {
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(\
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)

template<class F> void expect_invalid(F&& f) {
    bool threw = false;
    try { f(); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}
template<class F> void eventually(F&& predicate) {
    const auto end = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        CHECK(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(1ms);
    }
}
struct Harness {
    Config config;
    EventEngine engine;
    std::vector<Event> events;
    explicit Harness(Config c = {}) : config(std::move(c)), engine(config) {}
    void feed(RawState raw, std::uint64_t ms, DeviceInfo info = {}) {
        Sample sample; sample.status = ReadStatus::connected; sample.raw = raw; sample.device = info;
        engine.process(sample, ms * 1000000, [&](Event e) { events.push_back(e); });
    }
    void disconnect(std::uint64_t ms, CancelReason reason = CancelReason::disconnected) {
        engine.disconnect(ms * 1000000, reason, [&](Event e) { events.push_back(e); });
    }
    std::size_t count(EventType type) const {
        return static_cast<std::size_t>(std::count_if(events.begin(), events.end(),
            [&](const Event& e) { return e.type == type; }));
    }
};
RawState raw_buttons(std::uint32_t buttons) { RawState state; state.buttons = buttons; return state; }

struct SharedInput {
    std::mutex mutex;
    Sample sample;
    std::atomic<std::uint64_t> reads{0};
    void set(Sample input) { std::lock_guard<std::mutex> lock(mutex); sample = input; }
};
class ControlledBackend final : public Backend {
public:
    explicit ControlledBackend(std::shared_ptr<SharedInput> input) : input_(std::move(input)) {}
    Sample read() noexcept override {
        std::lock_guard<std::mutex> lock(input_->mutex);
        input_->reads.fetch_add(1, std::memory_order_relaxed);
        return input_->sample;
    }
private:
    std::shared_ptr<SharedInput> input_;
};
Config quiet_config() {
    Config config; config.log_level = LogLevel::off;
    config.poll_interval = 1ms; config.reconnect_interval = 10ms;
    return config;
}
auto make_pad(const Config& config, const std::shared_ptr<SharedInput>& input) {
    return TestAccess::make(config, std::make_unique<ControlledBackend>(input));
}
Sample connected(std::uint32_t buttons = 0) {
    Sample sample; sample.status = ReadStatus::connected; sample.raw.buttons = buttons; return sample;
}

void short_press() {
    Harness h; h.feed({}, 0); h.feed(raw_buttons(mask(Button::a)), 10); h.feed({}, 110);
    CHECK(h.events.size() == 4);
    CHECK(h.events[1].type == EventType::button_down);
    CHECK(h.events[2].type == EventType::button_up);
    CHECK(h.events[3].type == EventType::short_press);
    CHECK(h.events[3].held_ns == 100000000);
    CHECK(!h.events[3].state.pressed(Button::a));
}
void long_press_unchanged_packet() {
    Harness h; h.feed({}, 0); const auto raw = raw_buttons(mask(Button::a));
    h.feed(raw, 10); h.feed(raw, 509); CHECK(h.count(EventType::long_press) == 0);
    h.feed(raw, 510); h.feed(raw, 800); h.feed({}, 900);
    CHECK(h.count(EventType::long_press) == 1);
    CHECK(h.count(EventType::short_press) == 0);
    CHECK(h.events.back().type == EventType::button_up);
    CHECK(h.events.back().held_ns == 890000000);
}
void release_at_threshold() {
    Harness h; h.feed({}, 0); h.feed(raw_buttons(mask(Button::b)), 100); h.feed({}, 600);
    CHECK(h.count(EventType::long_press) == 1);
    CHECK(h.count(EventType::short_press) == 0);
    CHECK(h.events[2].type == EventType::long_press);
    CHECK(h.events[3].type == EventType::button_up);
}
void simultaneous_independent_buttons() {
    Harness h; h.feed({}, 0);
    h.feed(raw_buttons(mask(Button::a) | mask(Button::b)), 1);
    h.feed(raw_buttons(mask(Button::b)), 100);
    h.feed(raw_buttons(mask(Button::b)), 501); h.feed({}, 700);
    CHECK(h.count(EventType::button_down) == 2);
    CHECK(h.count(EventType::short_press) == 1);
    CHECK(h.count(EventType::long_press) == 1);
}
void initial_held_and_reconnect() {
    Harness h; h.feed(raw_buttons(mask(Button::a)), 0);
    h.feed(raw_buttons(mask(Button::a)), 1000); h.feed({}, 1001);
    CHECK(h.count(EventType::button_down) == 0);
    CHECK(h.count(EventType::short_press) == 0);
    CHECK(h.count(EventType::long_press) == 0);
    CHECK(h.events.back().cancelled == CancelReason::initially_held);
    h.feed(raw_buttons(mask(Button::a)), 1010); h.disconnect(1100);
    CHECK(h.events.back().type == EventType::disconnected);
    CHECK(!h.events.back().state.connected);
    CHECK(h.events[h.events.size() - 2].cancelled == CancelReason::disconnected);
    CHECK(h.count(EventType::short_press) == 0);
    h.feed({}, 1200); h.feed(raw_buttons(mask(Button::a)), 1201); h.feed({}, 1300);
    CHECK(h.count(EventType::short_press) == 1);
}
void stop_cancels_all_buttons() {
    Harness h; h.feed({}, 0); h.feed(raw_buttons(mask(Button::a) | mask(Button::y)), 1);
    h.disconnect(600, CancelReason::stopped);
    CHECK(h.count(EventType::button_up) == 2);
    CHECK(h.count(EventType::short_press) == 0);
    CHECK(h.count(EventType::long_press) == 0);
    CHECK(h.events.back().state.buttons == 0);
    CHECK(h.events.back().cancelled == CancelReason::stopped);
}
void trigger_hysteresis() {
    Harness h; h.feed({}, 0);
    RawState raw; raw.left_trigger = 30; raw.right_trigger = 255; h.feed(raw, 1);
    CHECK(h.engine.state().pressed(Button::left_trigger));
    CHECK(h.engine.state().pressed(Button::right_trigger));
    raw.left_trigger = 27; h.feed(raw, 2); CHECK(h.count(EventType::button_up) == 0);
    raw.left_trigger = 25; h.feed(raw, 3); CHECK(h.count(EventType::button_up) == 1);
    CHECK(!h.engine.state().pressed(Button::left_trigger));
    CHECK(h.engine.state().pressed(Button::right_trigger));
}
void normalization_and_coalescing() {
    Harness h; h.feed({}, 0);
    RawState raw; raw.left_x = 200; h.feed(raw, 1);
    CHECK(h.engine.state().left_x == 0);
    CHECK(h.count(EventType::state_changed) == 0);
    h.feed(raw, 8); CHECK(h.count(EventType::state_changed) == 1);
    raw.left_x = -32768; raw.left_y = 0; h.feed(raw, 16);
    CHECK(std::abs(h.engine.state().left_x + 1.0f) < 0.00001f);
    raw.right_x = 32767; raw.right_y = 32767; h.feed(raw, 24);
    const auto& state = h.engine.state();
    CHECK(std::abs(std::sqrt(state.right_x * state.right_x + state.right_y * state.right_y) - 1.0f) < 0.00001f);
    CHECK(state.raw.left_x == -32768);
    h.feed(raw_buttons(mask(Button::a)), 25); // Digital edge is not delayed by analog coalescing.
    CHECK(h.events.back().type == EventType::button_down);
}
void metadata_reserved_bits_and_packet() {
    Harness h; h.feed(raw_buttons(0x0C00), 0);
    CHECK(h.engine.state().buttons == 0);
    RawState raw; raw.packet = 123; h.feed(raw, 1); CHECK(h.events.size() == 1);
    DeviceInfo info; info.battery_type = BatteryType::alkaline; info.battery_level = 2;
    h.feed(raw, 2, info); CHECK(h.events.back().type == EventType::device_info_changed);
    CHECK(h.events.back().state.device.battery_level == 2);
}
void backend_error_deduplication() {
    Harness h; h.feed({}, 0); h.feed(raw_buttons(mask(Button::a)), 1);
    Sample error; error.status = ReadStatus::error; error.error = 5;
    auto emit = [&](Event e) { h.events.push_back(e); };
    h.engine.process(error, 2000000, emit); h.engine.process(error, 3000000, emit);
    CHECK(h.count(EventType::backend_error) == 1);
    CHECK(h.count(EventType::disconnected) == 1);
    CHECK(h.events[h.events.size() - 3].cancelled == CancelReason::backend_error);
    error.error = 6; h.engine.process(error, 4000000, emit);
    CHECK(h.count(EventType::backend_error) == 2);
    h.feed({}, 10); h.engine.process(error, 11000000, emit);
    CHECK(h.count(EventType::backend_error) == 3);
}
void queue_overflow_and_close() {
    EventQueue queue(2); Event event;
    event.state.raw.left_x = 1; CHECK(!queue.push(event));
    event.state.raw.left_x = 2; CHECK(!queue.push(event));
    event.state.raw.left_x = 3; CHECK(queue.push(event));
    CHECK(queue.snapshot().raw.left_x == 3);
    queue.close(); CHECK(!queue.push(event));
    CHECK(queue.wait_pop(event, 0ms) == WaitResult::event);
    CHECK(event.sequence == 2 && event.dropped_total == 1 && event.state.raw.left_x == 2);
    CHECK(queue.try_pop(event) && event.sequence == 3);
    CHECK(queue.wait_pop(event, 0ms) == WaitResult::stopped);
    const auto stats = queue.statistics();
    CHECK(stats.produced == 3 && stats.consumed == 2 && stats.dropped == 1);
    CHECK(stats.high_watermark == 2 && stats.queue_depth == 0);
    EventQueue single(1); single.push({}); single.push({});
    CHECK(single.try_pop(event) && event.sequence == 2 && event.dropped_total == 1);
    expect_invalid([] { EventQueue invalid(0); });
}
void queue_wait_and_concurrent_overwrite() {
    EventQueue queue(32);
    Event event; CHECK(queue.wait_pop(event, 1ms) == WaitResult::timeout);
    std::atomic<bool> ordered{true};
    std::thread consumer([&] {
        std::uint64_t previous = 0;
        Event next;
        while (queue.wait_pop(next, 1s) == WaitResult::event) {
            if (next.sequence <= previous || next.state.raw.packet != next.timestamp_ns)
                ordered = false;
            previous = next.sequence;
        }
    });
    for (std::uint32_t i = 0; i < 100000; ++i) {
        Event next; next.timestamp_ns = i; next.state.raw.packet = i; queue.push(next);
    }
    queue.close(); consumer.join();
    CHECK(ordered.load());
    const auto stats = queue.statistics();
    CHECK(stats.produced == stats.consumed + stats.dropped);
}
void config_validation() {
    auto input = std::make_shared<SharedInput>();
    Config config = quiet_config(); config.slot = 4;
    expect_invalid([&] { auto pad = make_pad(config, input); });
    config = quiet_config(); config.queue_capacity = 0;
    expect_invalid([&] { auto pad = make_pad(config, input); });
    config = quiet_config(); config.poll_interval = 0us;
    expect_invalid([&] { auto pad = make_pad(config, input); });
    config = quiet_config(); config.left_deadzone = std::numeric_limits<float>::quiet_NaN();
    expect_invalid([&] { auto pad = make_pad(config, input); });
    config = quiet_config(); config.right_deadzone = 1;
    expect_invalid([&] { auto pad = make_pad(config, input); });
    config = quiet_config(); config.trigger_release_threshold = config.trigger_press_threshold;
    expect_invalid([&] { auto pad = make_pad(config, input); });
    config = quiet_config(); config.long_press_threshold = 0ms;
    expect_invalid([&] { auto pad = make_pad(config, input); });
    config = quiet_config(); config.analog_event_interval = -1us;
    expect_invalid([&] { auto pad = make_pad(config, input); });
    config = quiet_config(); config.log_max_bytes = 100;
    expect_invalid([&] { auto pad = make_pad(config, input); });
}
void threaded_lifecycle_and_callback_exception() {
    auto input = std::make_shared<SharedInput>(); input->set(connected());
    auto pad = make_pad(quiet_config(), input);
    Event event; CHECK(pad->wait_pop(event, 1s) == WaitResult::event);
    CHECK(event.type == EventType::connected);
    input->set(connected(mask(Button::a)));
    eventually([&] { return pad->statistics().queue_depth != 0; });
    bool threw = false;
    const auto caller = std::this_thread::get_id();
    try {
        pad->dispatch_pending([&](const Event&) {
            CHECK(std::this_thread::get_id() == caller);
            throw std::runtime_error("business handler failed");
        });
    } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw && pad->running());
    const auto reads = input->reads.load();
    eventually([&] { return input->reads.load() > reads; });
    expect_invalid([&] { pad->wait_pop(event, -1ms); });
    std::thread stop_a([&] { pad->stop(); });
    std::thread stop_b([&] { pad->stop(); });
    stop_a.join(); stop_b.join(); pad->stop();
    CHECK(!pad->running());
    bool saw_stopped = false, saw_cancelled = false;
    pad->dispatch_pending([&](const Event& e) {
        if (e.type == EventType::stopped) saw_stopped = true;
        if (e.type == EventType::button_up && e.cancelled == CancelReason::stopped) saw_cancelled = true;
    }, 1024);
    CHECK(saw_stopped && saw_cancelled);
    CHECK(pad->wait_pop(event, 1s) == WaitResult::stopped);
    CHECK(!pad->snapshot().connected && pad->snapshot().buttons == 0);
    CHECK(pad->statistics().produced == pad->statistics().consumed);
}
void stop_interrupts_reconnect_wait_and_waiter() {
    auto input = std::make_shared<SharedInput>();
    auto config = quiet_config(); config.reconnect_interval = 60s;
    auto pad = make_pad(config, input);
    eventually([&] { return input->reads.load() != 0; });
    auto waiter = std::async(std::launch::async, [&] {
        Event event;
        while (pad->wait_pop(event, 60s) == WaitResult::event) {}
        return true;
    });
    const auto start = std::chrono::steady_clock::now();
    pad->stop();
    CHECK(std::chrono::steady_clock::now() - start < 1s);
    CHECK(waiter.wait_for(1s) == std::future_status::ready && waiter.get());
}
void threaded_hotplug_and_dispatch_limit() {
    auto input = std::make_shared<SharedInput>();
    auto pad = make_pad(quiet_config(), input);
    eventually([&] { return input->reads.load() != 0; });
    input->set(connected());
    Event event; CHECK(pad->wait_pop(event, 1s) == WaitResult::event);
    CHECK(event.type == EventType::connected);
    input->set(connected(mask(Button::b)));
    eventually([&] { return pad->snapshot().pressed(Button::b); });
    input->set({});
    eventually([&] { return !pad->snapshot().connected; });
    CHECK(pad->dispatch_pending([](const Event&) {}, 1) == 1);
    CHECK(pad->statistics().queue_depth >= 2);
    pad->dispatch_pending([](const Event&) {}, 100);
    input->set(connected());
    CHECK(pad->wait_pop(event, 1s) == WaitResult::event);
    CHECK(event.type == EventType::connected);
}
void repeated_raii_shutdown() {
    auto input = std::make_shared<SharedInput>(); input->set(connected());
    for (int i = 0; i < 100; ++i) {
        auto pad = make_pad(quiet_config(), input);
        if (i % 2 == 0) pad->stop();
    }
}
void callback_can_stop() {
    auto input = std::make_shared<SharedInput>(); input->set(connected());
    auto pad = make_pad(quiet_config(), input);
    eventually([&] { return pad->statistics().queue_depth != 0; });
    bool saw_stopped = false;
    pad->dispatch_pending([&](const Event& event) {
        if (event.type == EventType::connected) pad->stop();
        if (event.type == EventType::stopped) saw_stopped = true;
    });
    CHECK(saw_stopped && !pad->running());
}
void log_rotation_and_open_failure() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("gamepad-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    try {
        Config config = quiet_config(); config.log_level = LogLevel::info;
        config.log_to_console = false; config.log_max_bytes = 4096;
        config.log_file = (directory / "capture.log").u8string();
        {
            Logger logger(config);
            for (int i = 0; i < 150; ++i) logger.write(LogLevel::info, "bounded log test", static_cast<std::uint64_t>(i));
        }
        CHECK(std::filesystem::exists(directory / "capture.log.1"));
        CHECK(std::filesystem::exists(directory / "capture.log.2"));
        CHECK(!std::filesystem::exists(directory / "capture.log.3"));
        CHECK(std::filesystem::file_size(directory / "capture.log") <= 4096);
        config.log_file = (directory / "missing" / "capture.log").u8string();
        bool threw = false;
        try { Logger logger(config); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    } catch (...) { std::filesystem::remove_all(directory); throw; }
    std::filesystem::remove_all(directory);
}
} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"short press ordering and duration", short_press},
        {"long press with unchanged packets", long_press_unchanged_packet},
        {"release exactly at long threshold", release_at_threshold},
        {"simultaneous independent buttons", simultaneous_independent_buttons},
        {"initial held keys and reconnection", initial_held_and_reconnect},
        {"stop cancels held buttons", stop_cancels_all_buttons},
        {"independent trigger hysteresis", trigger_hysteresis},
        {"radial normalization and analog coalescing", normalization_and_coalescing},
        {"metadata reserved bits packet-only changes", metadata_reserved_bits_and_packet},
        {"backend error deduplication", backend_error_deduplication},
        {"queue overflow recovery and draining", queue_overflow_and_close},
        {"concurrent queue overwrite consistency", queue_wait_and_concurrent_overwrite},
        {"configuration validation", config_validation},
        {"thread lifecycle callback exceptions concurrent stop", threaded_lifecycle_and_callback_exception},
        {"stop wakes reconnect and consumer waits", stop_interrupts_reconnect_wait_and_waiter},
        {"threaded hotplug and bounded dispatch", threaded_hotplug_and_dispatch_limit},
        {"repeated RAII destruction", repeated_raii_shutdown},
        {"consumer callback can stop safely", callback_can_stop},
        {"log rotation and startup failure", log_rotation_and_open_failure}
    };
    int failures = 0;
    for (const auto& test : tests) {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << test.name << ": " << e.what() << '\n'; }
    }
    std::cout << std::size(tests) << " tests, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
