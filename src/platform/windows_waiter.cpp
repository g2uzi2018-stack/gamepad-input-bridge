#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "waiter.hpp"
#include <algorithm>
#include <cstdint>
#include <system_error>

namespace gamepad::detail {
struct DeadlineWaiter::Impl {
    HANDLE stop = nullptr, timer = nullptr;
    bool high_resolution = false;
    Impl() {
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateEvent");
        // Available on Windows 10 1803+. Unsupported flags fail cleanly on older Windows.
        timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002,
                                      TIMER_MODIFY_STATE | SYNCHRONIZE);
        high_resolution = timer != nullptr;
        if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        if (!timer) {
            const auto error = GetLastError();
            CloseHandle(stop); stop = nullptr;
            throw std::system_error(static_cast<int>(error), std::system_category(), "CreateWaitableTimer");
        }
    }
    ~Impl() { if (timer) CloseHandle(timer); if (stop) CloseHandle(stop); }
};
DeadlineWaiter::DeadlineWaiter() : impl_(std::make_unique<Impl>()) {}
DeadlineWaiter::~DeadlineWaiter() = default;
bool DeadlineWaiter::high_resolution() const noexcept { return impl_->high_resolution; }
void DeadlineWaiter::request_stop() noexcept { SetEvent(impl_->stop); }
void DeadlineWaiter::wait_until(std::chrono::steady_clock::time_point deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) return;
    LARGE_INTEGER due;
    due.QuadPart = -std::max<std::int64_t>(1, (remaining + 99) / 100); // Relative 100 ns units.
    if (!SetWaitableTimer(impl_->timer, &due, 0, nullptr, nullptr, FALSE))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "SetWaitableTimer");
    const HANDLE handles[] = {impl_->stop, impl_->timer};
    if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_FAILED)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WaitForMultipleObjects");
}
}
