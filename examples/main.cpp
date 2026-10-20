#include "gamepad/gamepad.hpp"
#include "devices/profile.hpp"
#include <algorithm>
#include <charconv>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void on_signal(int) { interrupted = 1; }
std::uint32_t number(std::string_view text) {
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid non-negative integer: " + std::string(text));
    return value;
}
void print_event(const gamepad::Event& event, bool json) {
    const auto& state = event.state;
    const auto& raw = state.raw;
    if (json) {
        std::cout << "{\"sequence\":" << event.sequence
            << ",\"timestamp_ns\":" << event.timestamp_ns
            << ",\"type\":\"" << gamepad::to_string(event.type)
            << "\",\"button\":\"" << gamepad::to_string(event.button)
            << "\",\"held_ns\":" << event.held_ns
            << ",\"cancelled\":\"" << gamepad::to_string(event.cancelled)
            << "\",\"dropped_total\":" << event.dropped_total
            << ",\"native_error\":" << event.native_error
            << ",\"connected\":" << (state.connected ? "true" : "false")
            << ",\"slot\":" << state.device.slot
            << ",\"packet\":" << raw.packet << ",\"buttons\":" << state.buttons
            << ",\"raw\":{\"buttons\":" << raw.buttons
            << ",\"lx\":" << raw.left_x << ",\"ly\":" << raw.left_y
            << ",\"rx\":" << raw.right_x << ",\"ry\":" << raw.right_y
            << ",\"lt\":" << static_cast<unsigned>(raw.left_trigger)
            << ",\"rt\":" << static_cast<unsigned>(raw.right_trigger) << '}'
            << ",\"normalized\":{\"lx\":" << state.left_x << ",\"ly\":" << state.left_y
            << ",\"rx\":" << state.right_x << ",\"ry\":" << state.right_y
            << ",\"lt\":" << state.left_trigger << ",\"rt\":" << state.right_trigger << '}'
            << ",\"device\":{\"capabilities_known\":" << (state.device.capabilities_known ? "true" : "false")
            << ",\"capability_flags\":" << state.device.capability_flags
            << ",\"subtype\":" << static_cast<unsigned>(state.device.subtype)
            << ",\"battery_type\":" << static_cast<unsigned>(state.device.battery_type)
            << ",\"battery_level\":" << static_cast<int>(state.device.battery_level) << "}}\n";
    } else {
        std::cout << '#' << event.sequence << " t=" << std::fixed << std::setprecision(3)
            << static_cast<double>(event.timestamp_ns) / 1000000.0 << "ms "
            << gamepad::to_string(event.type) << " button=" << gamepad::to_string(event.button)
            << " held=" << static_cast<double>(event.held_ns) / 1000000.0 << "ms"
            << " connected=" << state.connected << " buttons=0x" << std::hex << state.buttons << std::dec
            << " L=(" << raw.left_x << ',' << raw.left_y << ") R=(" << raw.right_x << ',' << raw.right_y << ')'
            << " LT=" << static_cast<unsigned>(raw.left_trigger)
            << " RT=" << static_cast<unsigned>(raw.right_trigger)
            << " cancelled=" << gamepad::to_string(event.cancelled)
            << " dropped=" << event.dropped_total << " error=" << event.native_error << '\n';
    }
}
void help() {
    std::cout << "Gamepad input bridge (C++17 / Windows XInput)\n"
        "Usage: gamepad_<device> [options]\n"
        "  --slot 0..3             XInput slot (default 0; not a permanent device ID)\n"
        "  --duration SECONDS      Stop automatically (default 0: until Ctrl+C)\n"
        "  --queue-capacity N      Bounded FIFO capacity (default 256)\n"
        "  --poll-ms N             Connected polling interval (default 4)\n"
        "  --long-press-ms N       Long press threshold (default 500)\n"
        "  --analog-ms N           Analog event coalescing (default 8; 0: every change)\n"
        "  --json                  Print JSON Lines to stdout\n"
        "  --stats-only            Consume events without per-event printing\n"
        "  --log-file PATH         Rotating log file (parent directory must exist)\n"
        "  --log-level LEVEL       off/error/warning/info/debug (default info)\n"
        "  --help                  Show this help\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        auto config = gamepad::example::device_configuration();
        config.log_level = gamepad::LogLevel::info;
        std::uint32_t duration = 0;
        bool json = false, stats_only = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view option(argv[i]);
            if (option == "--help") { help(); return 0; }
            if (option == "--json") { json = true; continue; }
            if (option == "--stats-only") { stats_only = true; continue; }
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + std::string(option));
            const std::string_view value(argv[++i]);
            if (option == "--slot") config.slot = number(value);
            else if (option == "--duration") {
                duration = number(value);
                if (duration > 86400) throw std::invalid_argument("duration must be <= 86400 seconds");
            } else if (option == "--queue-capacity") config.queue_capacity = number(value);
            else if (option == "--poll-ms") config.poll_interval = std::chrono::milliseconds(number(value));
            else if (option == "--long-press-ms") config.long_press_threshold = std::chrono::milliseconds(number(value));
            else if (option == "--analog-ms") config.analog_event_interval = std::chrono::milliseconds(number(value));
            else if (option == "--log-file") config.log_file = std::string(value);
            else if (option == "--log-level") {
                if (value == "off") config.log_level = gamepad::LogLevel::off;
                else if (value == "error") config.log_level = gamepad::LogLevel::error;
                else if (value == "warning") config.log_level = gamepad::LogLevel::warning;
                else if (value == "info") config.log_level = gamepad::LogLevel::info;
                else if (value == "debug") config.log_level = gamepad::LogLevel::debug;
                else throw std::invalid_argument("Unknown log level");
            } else throw std::invalid_argument("Unknown option: " + std::string(option));
        }
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        std::cout << std::setprecision(6);
        gamepad::Gamepad pad(config); // Starts its own worker; destructor joins it.
        const auto end = duration == 0 ? std::chrono::steady_clock::time_point::max()
            : std::chrono::steady_clock::now() + std::chrono::seconds(duration);
        // This lambda runs here, on main's thread, never on the capture thread.
        auto handler = [&](const gamepad::Event& event) {
            if (!stats_only) print_event(event, json);
        };
        while (!interrupted && std::chrono::steady_clock::now() < end) {
            gamepad::Event event;
            auto timeout = std::chrono::milliseconds(100);
            if (duration != 0) {
                timeout = std::min(timeout, std::max(std::chrono::milliseconds(0),
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now())));
            }
            const auto result = pad.wait_pop(event, timeout);
            if (result == gamepad::WaitResult::stopped) break;
            if (result == gamepad::WaitResult::event) {
                handler(event);
                pad.dispatch_pending(handler);
                if (!stats_only) std::cout.flush();
            }
        }
        pad.stop(); // Optional explicit stop so we can print/drain shutdown events.
        while (pad.dispatch_pending(handler) != 0) {}
        const auto stats = pad.statistics();
        std::cerr << "summary polls=" << stats.polls << " events=" << stats.produced
            << " consumed=" << stats.consumed << " dropped=" << stats.dropped
            << " high_watermark=" << stats.high_watermark
            << " read_errors=" << stats.read_errors
            << " missed_deadlines=" << stats.missed_deadlines << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
