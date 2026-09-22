#include "scheduler.h"
#include "queue.h"

#include "../common/clock.h"

// fcfs (A10): requests are served in arrival order. Never preempts, so
// requeue() is only reachable if a caller misuses the interface - kept
// correct (re-admits at the tail) rather than asserting, since K4's wiring
// glue is joint work and shouldn't be able to deadlock on a logic error
// here.
class SchedulerFcfs : public IScheduler {
public:
    void enqueue(Request* req) override { queue_.push(req); }

    Request* next() override {
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

IScheduler* make_scheduler_fcfs() { return new SchedulerFcfs(); }
