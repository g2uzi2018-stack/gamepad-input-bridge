#include "backend.hpp"
#include <stdexcept>
namespace gamepad::detail {
std::unique_ptr<Backend> make_native_backend(const Config&) {
    throw std::runtime_error("Native backend is currently Windows XInput only; Linux backend is not implemented yet");
}
}
