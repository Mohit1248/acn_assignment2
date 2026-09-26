#include "scheduler.h"
#include "queue.h"

#include "../common/clock.h"

// sjf (A11): the queued request with the smallest declared byte count is
// served next - req->bytes, which is the file size for a GET and the count in
// the request line for a PUT, so both verbs are ordered by the same key. Ties
// go to the earlier arrival (the queue keeps arrival order). No aging: the
// starvation of large requests is an expected property of SJF that the report
// discusses, not something to prevent. Never preempts (one round per request).
class SchedulerSjf : public IScheduler {
public:
    void enqueue(Request* req) override { queue_.push(req); }

    Request* next() override {
        Request* req = queue_.pop_pick([](const std::vector<Request*>& items) {
            size_t best = 0;
            for (size_t i = 1; i < items.size(); ++i) {
                if (items[i]->bytes < items[best]->bytes) best = i;  // strict < keeps the earliest tie
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
