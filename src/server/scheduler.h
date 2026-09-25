#ifndef SERVER_SCHEDULER_H
#define SERVER_SCHEDULER_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "../common/request.h"

// The plug-in point between server infrastructure (accept loop / worker
// threads, Stage 1) and the scheduler core (Stage 2, K1-K4). One instance
// per running server process, selected by --sched at startup. Locked in
// Phase 0; see assignment2-team-plan-v2.md.
//
// Thread-safety: enqueue()/next()/requeue()/queue_depth() are called
// concurrently by the accept thread and up to `server_threads` workers, and
// must be internally synchronized by each implementation. serve_slice()
// (below) runs on whichever single worker currently holds that Request.
class IScheduler {
public:
    virtual ~IScheduler() = default;

    // Admits a newly-arrived request (header already parsed, byte count
    // known - A5). The scheduler holds a pointer only while the request is
    // in the queue; Request lifetime is owned by the caller.
    virtual void enqueue(Request* req) = 0;

    // Blocks until a request is available, or returns nullptr once
    // shutdown() has been called and the queue has drained. Implementations
    // set req->start_ns the first time a given request is returned (A18:
    // "start" = first pick-up by a worker, not each re-pick-up after a
    // requeue).
    virtual Request* next() = 0;

    // Returns a preempted request to the queue per the policy's ordering
    // rule. Only called under rr/drr (A12/A15) - fcfs/sjf never preempt
    // (A10/A11), so a conforming implementation of either may leave this
    // unreachable in practice, but must still implement it correctly since
    // K4 wires all four through the same interface.
    virtual void requeue(Request* req) = 0;

    // Number of admitted-but-unserved requests right now, including ones
    // currently preempted and requeued. Answers HEALTH (B8). Must be cheap
    // and must not block behind the scheduler's own serving work.
    virtual size_t queue_depth() const = 0;

    // Stops the queue: next() returns nullptr once drained, so blocked
    // workers can exit for graceful shutdown (A26).
    virtual void shutdown() = 0;
};

enum class SliceResult { DONE, PREEMPTED };

// Serves up to one quantum of `req` on `fd`. Implementation owner: Stage 2,
// K1 (see slice.cpp).
//   - fcfs/sjf pass quantum_bytes = req's entire remaining size, so this
//     always returns DONE - there is no preemption under these policies
//     (A10/A11).
//   - rr/drr bound the round to quantum_bytes. GET obeys the whole-line rule
//     (A6: a round must never end mid-line) and --p batching (A8, groups up
//     to `p_lines` whole lines per write without changing which bytes are
//     sent or their order). PUT is byte-opaque (A9): it takes exactly
//     quantum_bytes with no rounding and no line scanning.
// Mutates `req` in place - byte_offset, leftover socket bytes, rounds,
// forfeited_bytes - so a requeue preserves all preemption state (A17).
// Returns DONE once req->bytes have been fully transferred, PREEMPTED if the
// quantum ran out first.
SliceResult serve_slice(Request* req, int fd, uint64_t quantum_bytes, int p_lines);

// Constructs the IScheduler for --sched `policy` ("fcfs"|"sjf"|"rr"|"drr" -
// already validated by main's CLI parsing, A1). Caller owns the result.
// TODO(K4, Stage 2, joint): currently always returns the fcfs implementation
// so the server links and runs end-to-end during Stage 1 against the stub
// transfer path; wire in sjf/rr/drr here as scheduler_*.cpp land.
IScheduler* make_scheduler(const std::string& policy);

// Per-policy constructors, one per scheduler_*.cpp (Stage 2, K3). Prefer
// make_scheduler() above from calling code; these exist so the factory can
// be a one-line switch as each policy lands.
IScheduler* make_scheduler_fcfs();
IScheduler* make_scheduler_sjf();
IScheduler* make_scheduler_rr();
IScheduler* make_scheduler_drr();

#endif  // SERVER_SCHEDULER_H
