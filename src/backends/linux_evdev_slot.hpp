#pragma once

namespace gamepad::detail {
// On success the caller owns fd.
int open_linux_slot(const char* input_class, const char* input_nodes, unsigned slot, int& fd) noexcept;
}
