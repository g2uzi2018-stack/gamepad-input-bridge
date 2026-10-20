#pragma once
#include <chrono>
#include <memory>
namespace gamepad::detail {
class DeadlineWaiter {
public:
    DeadlineWaiter();
    ~DeadlineWaiter();
    DeadlineWaiter(const DeadlineWaiter&) = delete;
    DeadlineWaiter& operator=(const DeadlineWaiter&) = delete;
    void request_stop() noexcept;
    void wait_until(std::chrono::steady_clock::time_point deadline);
    bool high_resolution() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
