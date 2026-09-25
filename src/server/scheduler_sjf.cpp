#include "scheduler.h"
#include "queue.h"

#include "../common/clock.h"

// TODO(K3, Stage 2): sjf (A11) - the request with the smallest declared
// byte count (req->bytes: file size for GET, declared count for PUT) is
// served first. Both verbs share one ordering key. No aging - starvation of
// large requests is expected and discussed in the report, not prevented.
// Never preempts (single round per request, like fcfs).
class SchedulerSjf : public IScheduler {
public:
    void enqueue(Request* req) override { queue_.push(req); }

    Request* next() override {
        // TODO: pick the index of the minimum req->bytes instead of index 0.
        Request* req = queue_.pop_pick([](const std::vector<Request*>&) { return size_t(0); });
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
