#include "backend.hpp"
#include <stdexcept>
namespace gamepad::detail {
std::unique_ptr<Backend> make_native_backend(const Config&) {
    throw std::runtime_error("No native gamepad backend for this platform");
}
}
