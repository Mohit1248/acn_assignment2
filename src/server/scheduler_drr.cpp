#include "scheduler.h"
#include "queue.h"

#include "../common/clock.h"

// drr (A15-A16) - identical queue ordering to rr, but
// each request carries a deficit counter (Request::deficit, initialised to
// 0 - see request.h) instead of forfeiting unused allowance each round. The
// deficit/allowance math itself lives in serve_slice (K1/slice.cpp); this
// class only owns queue ordering, which is the same tail-requeue FIFO as
// rr. Deficit is discarded when the request leaves the system (K1/K4
// responsibility, not this file).
class SchedulerDrr : public IScheduler {
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

IScheduler* make_scheduler_drr() { return new SchedulerDrr(); }
