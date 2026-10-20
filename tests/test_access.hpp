#pragma once
#include "gamepad/gamepad.hpp"
#include "backends/backend.hpp"
namespace gamepad::detail {
struct TestAccess {
    static std::unique_ptr<Gamepad> make(Config config, std::unique_ptr<Backend> backend) {
        return std::unique_ptr<Gamepad>(new Gamepad(std::move(config), std::move(backend)));
    }
};
}
