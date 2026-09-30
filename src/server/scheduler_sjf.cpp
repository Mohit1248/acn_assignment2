#include "scheduler.h"
#include "queue.h"

#include "../common/clock.h"

class SchedulerSjf : public IScheduler {
public:
    void enqueue(Request* req) override { queue_.push(req); }

    Request* next() override {
        Request* req = queue_.pop_pick([](const std::vector<Request*>& items) {
            size_t best = 0;
            for (size_t i = 1; i < items.size(); ++i) {
                if (items[i]->bytes < items[best]->bytes) best = i;
            }
            return best;
        });
        if (req && req->start_ns == 0) {
            req->start_ns = now_monotonic_ns();
        }
        return req;
    }

    void requeue(Request* req) override { queue_.push(req); }

    size_t queue_depth() const override { return queue_.size(); }

    void shutdown() override { queue_.shutdown(); }

private:
    RequestQueue queue_;
};

IScheduler* make_scheduler_sjf() { return new SchedulerSjf(); }
