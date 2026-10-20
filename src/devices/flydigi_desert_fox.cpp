#include "profile.hpp"
namespace gamepad::example {
Config device_configuration() {
    Config config;
    config.device_label = "flydigi_desert_fox";
    // PC receiver exposes standard XInput. No proprietary remapping required.
    return config;
}
}
