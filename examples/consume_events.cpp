#include <gamepad/gamepad.hpp>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    try {
        gamepad::Config config;
        config.slot = 0;
        config.queue_capacity = 256;
        config.poll_interval = 4ms;
        config.long_press_threshold = 500ms;
        gamepad::Gamepad input(config);

        std::uint64_t last_sequence = 0;
        std::uint64_t last_dropped = 0;
        auto handle = [&](const gamepad::Event& event) {
            const bool gap = event.sequence != last_sequence + 1;
            if (gap || event.dropped_total != last_dropped) {
                // 业务此时应根据 event.state 恢复状态，放弃旧的边沿推断。
                std::cerr << "Input gap; current A="
                          << event.state.pressed(gamepad::Button::a) << '\n';
            }
            last_sequence = event.sequence;
            last_dropped = event.dropped_total;
            std::cout << gamepad::to_string(event.type) << ' '
                      << gamepad::to_string(event.button) << '\n';
        };

        const auto end = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < end) {
            gamepad::Event event;
            const auto result = input.wait_pop(event, 100ms);
            if (result == gamepad::WaitResult::stopped) break;
            if (result == gamepad::WaitResult::event) {
                handle(event);
                input.dispatch_pending(handle, 64);
            }
        }
        input.stop();
        while (input.dispatch_pending(handle, 64) != 0) {}
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}