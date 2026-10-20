#include "waiter.hpp"
#include <condition_variable>
#include <mutex>
namespace gamepad::detail {
struct DeadlineWaiter::Impl {
    std::mutex mutex;
    std::condition_variable ready;
    bool stopped = false;
};
DeadlineWaiter::DeadlineWaiter() : impl_(std::make_unique<Impl>()) {}
DeadlineWaiter::~DeadlineWaiter() = default;
bool DeadlineWaiter::high_resolution() const noexcept { return true; }
void DeadlineWaiter::request_stop() noexcept {
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stopped = true; }
    impl_->ready.notify_all();
}
void DeadlineWaiter::wait_until(std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->ready.wait_until(lock, deadline, [&] { return impl_->stopped; });
}
}
