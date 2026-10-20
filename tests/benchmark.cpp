#include "core/event_engine.hpp"
#include "core/event_queue.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    using namespace gamepad;
    using namespace gamepad::detail;
    using Clock = std::chrono::steady_clock;
    constexpr std::uint32_t iterations = 250000;
    EventQueue queue(256);
    Config config; config.analog_event_interval = std::chrono::microseconds(0);
    EventEngine engine(config);
    Sample sample; sample.status = ReadStatus::connected;
    auto emit = [&](Event event) { queue.push(event); };
    engine.process(sample, 0, emit);
    Event event; queue.try_pop(event);
    const auto start = Clock::now();
    for (std::uint32_t i = 0; i < iterations; ++i) {
        sample.raw.left_x = static_cast<std::int16_t>(i % 32768);
        sample.raw.packet = i;
        engine.process(sample, static_cast<std::uint64_t>(i + 1) * 4000000, emit);
        while (queue.try_pop(event)) {}
    }
    const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::cout << "engine+queue+consume ticks=" << iterations << " seconds=" << seconds
        << " ticks_per_second=" << static_cast<double>(iterations) / seconds
        << " event_size_bytes=" << sizeof(Event) << '\n';

    EventQueue concurrent(256);
    std::atomic<bool> consistent{true};
    std::thread consumer([&] {
        std::uint64_t previous = 0;
        Event next;
        while (concurrent.wait_pop(next, std::chrono::seconds(10)) == WaitResult::event) {
            if (next.sequence <= previous || next.timestamp_ns != next.state.raw.packet) consistent = false;
            previous = next.sequence;
        }
    });
    const auto concurrent_start = Clock::now();
    for (std::uint32_t i = 0; i < iterations; ++i) {
        Event next; next.timestamp_ns = i; next.state.raw.packet = i; concurrent.push(next);
    }
    concurrent.close(); consumer.join();
    const auto concurrent_seconds = std::chrono::duration<double>(Clock::now() - concurrent_start).count();
    const auto stats = concurrent.statistics();
    std::cout << "concurrent produced=" << stats.produced << " consumed=" << stats.consumed
        << " dropped=" << stats.dropped << " seconds=" << concurrent_seconds
        << " produced_per_second=" << static_cast<double>(iterations) / concurrent_seconds << '\n';
    return consistent && stats.produced == stats.consumed + stats.dropped ? 0 : 1;
}
