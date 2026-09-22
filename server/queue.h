#ifndef SERVER_QUEUE_H
#define SERVER_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

#include "../common/request.h"

// Thread-safe container of admitted-but-unserved Request pointers, meant to
// be reused by all four scheduler policies (scheduler_*.cpp, Stage 2 K3) so
// the locking/shutdown/drain behaviour is written once instead of four
// times. Each policy differs only in *which* element `pop_pick` selects.
//
// This is a shared building block, not itself a full IScheduler - each
// policy wraps one of these and implements IScheduler::next()/requeue() by
// calling pop_pick()/push() with its own selection rule (FIFO index 0 for
// fcfs, min-bytes index for sjf, etc).
class RequestQueue {
public:
    void push(Request* req) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            items_.push_back(req);
        }
        cv_.notify_one();
    }

    // Blocks until `items_` is non-empty or shutdown() has been called.
    // `pick` is invoked with the lock held and must return the index (into
    // its argument) of the element to remove; returns nullptr after
    // shutdown once the queue is empty.
    Request* pop_pick(const std::function<size_t(const std::vector<Request*>&)>& pick) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this] { return !items_.empty() || shutting_down_; });
        if (items_.empty()) return nullptr;  // shutting down and drained
        size_t idx = pick(items_);
        Request* req = items_[idx];
        items_.erase(items_.begin() + static_cast<long>(idx));
        return req;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return items_.size();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            shutting_down_ = true;
        }
        cv_.notify_all();
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::vector<Request*> items_;
    bool shutting_down_ = false;
};

#endif  // SERVER_QUEUE_H
