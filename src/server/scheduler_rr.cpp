#include "scheduler.h"
#include "queue.h"

#include "../common/clock.h"

// rr (A12-A14) - --quantum Q bytes per round, FIFO among
// requests waiting for their next round. A scheduled request is served for
// at most Q bytes (the whole-line/A14-escape-hatch/forfeiture accounting
// itself lives in serve_slice, K1/slice.cpp - this class only owns queue
// ordering) then requeued at the tail if not finished. A request that has
// transferred its final byte leaves the system instead of being requeued.
class SchedulerRr : public IScheduler {
public:
    void enqueue(Request* req) override { queue_.push(req); }

    Request* next() override {
        Request* req = queue_.pop_pick([](const std::vector<Request*>&) { return size_t(0); });
        if (req && req->start_ns == 0) {
            req->start_ns = now_monotonic_ns();
        }
        return req;
    }

    // Requeue at the tail (A12) - plain push is already tail-insertion here.
    void requeue(Request* req) override { queue_.push(req); }

    size_t queue_depth() const override { return queue_.size(); }

    void shutdown() override { queue_.shutdown(); }

private:
    RequestQueue queue_;
};

IScheduler* make_scheduler_rr() { return new SchedulerRr(); }
