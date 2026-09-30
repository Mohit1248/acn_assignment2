#ifndef SERVER_QUEUE_H
#define SERVER_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

#include "../common/request.h"

class RequestQueue {
public:
    void push(Request* req) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            items_.push_back(req);
        }
        cv_.notify_one();
    }

    Request* pop_pick(const std::function<size_t(const std::vector<Request*>&)>& pick) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this] { return !items_.empty() || shutting_down_; });
        if (items_.empty()) return nullptr;
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

#endif
