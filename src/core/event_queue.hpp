#pragma once
#include "gamepad/gamepad.hpp"
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace gamepad::detail {
// Preallocated, bounded ring. A mutex makes producer-side drop-oldest safe even
// while a consumer is copying an event; no fragile lock-free overwrite protocol.
class EventQueue {
public:
    explicit EventQueue(std::size_t capacity) : storage_(checked_capacity(capacity)) {}
    bool push(Event event) {
        bool dropped = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return false;
            event.sequence = ++stats_.produced;
            latest_ = event.state;
            if (size_ == storage_.size()) {
                head_ = (head_ + 1) % storage_.size();
                --size_; ++stats_.dropped; dropped = true;
            }
            storage_[(head_ + size_) % storage_.size()] = event;
            ++size_;
            if (size_ > stats_.high_watermark) stats_.high_watermark = size_;
        }
        ready_.notify_one();
        return dropped;
    }
    bool try_pop(Event& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        return pop_locked(event);
    }
    WaitResult wait_pop(Event& event, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!ready_.wait_for(lock, timeout, [&] { return size_ != 0 || closed_; }))
            return WaitResult::timeout;
        return pop_locked(event) ? WaitResult::event : WaitResult::stopped;
    }
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        ready_.notify_all();
    }
    void publish(const State& state) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = state;
    }
    State snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_;
    }
    Statistics statistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto stats = stats_;
        stats.queue_depth = size_;
        return stats;
    }
private:
    static std::size_t checked_capacity(std::size_t capacity) {
        if (capacity == 0 || capacity > 1048576)
            throw std::invalid_argument("queue capacity must be 1..1048576");
        return capacity;
    }
    bool pop_locked(Event& event) {
        if (size_ == 0) return false;
        event = storage_[head_];
        event.dropped_total = stats_.dropped;
        head_ = (head_ + 1) % storage_.size();
        --size_; ++stats_.consumed;
        return true;
    }
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::vector<Event> storage_;
    std::size_t head_ = 0, size_ = 0;
    bool closed_ = false;
    State latest_;
    Statistics stats_;
};
} // namespace gamepad::detail
