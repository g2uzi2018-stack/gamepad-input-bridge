#include <gamepad/gamepad.hpp>
#include <chrono>
int main() {
    gamepad::Config config;
    config.log_level = gamepad::LogLevel::off;
    gamepad::Gamepad input(config);
    gamepad::Event event;
    input.wait_pop(event, std::chrono::milliseconds(100));
    input.dispatch_pending([](const gamepad::Event&) {});
    input.stop();
    return input.running() ? 1 : 0;
}