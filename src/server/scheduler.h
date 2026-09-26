#ifndef SERVER_SCHEDULER_H
#define SERVER_SCHEDULER_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "../common/request.h"

// The plug-in point between the server infrastructure (acceptor / admission /
// worker threads, main.cpp) and the scheduling core (this header, slice.cpp,
// scheduler_*.cpp). One IScheduler per server process, chosen by --sched.
//
// Division of labour:
//   * IScheduler decides WHICH request is served next (the queue order).
//   * serve_slice() decides HOW MUCH of that request is transferred this time
//     (one round) and keeps the request's transfer state so it can resume.
//
// Thread-safety: enqueue()/next()/requeue()/queue_depth() are called
// concurrently by admission threads and up to `server_threads` workers and
// are internally synchronized. serve_slice() runs on whichever single worker
// currently holds that Request; nobody else touches it meanwhile.
class IScheduler {
public:
    virtual ~IScheduler() = default;

    // Admits a newly-arrived request (header parsed, size known - A5). The
    // scheduler only holds the pointer while the request sits in the queue.
    virtual void enqueue(Request* req) = 0;

    // Blocks until a request is available, or returns nullptr once shutdown()
    // has been called and the queue has drained. Sets req->start_ns the first
    // time a request is returned (A18: "start" = first pick-up, not each
    // re-pick-up after a requeue).
    virtual Request* next() = 0;

    // Puts a preempted request back per the policy's rule (tail of the queue
    // for rr/drr, A12). fcfs/sjf never preempt, so never call it.
    virtual void requeue(Request* req) = 0;

    // Admitted-but-unserved requests right now, including preempted ones that
    // were requeued (B8). Cheap; does not wait on serving work.
    virtual size_t queue_depth() const = 0;

    // next() returns nullptr once the queue has drained, so workers can exit
    // (A26). Requeues are still accepted afterwards so in-flight rr/drr
    // requests finish.
    virtual void shutdown() = 0;
};

enum class Policy { FCFS, SJF, RR, DRR };

// Parses "fcfs"|"sjf"|"rr"|"drr" (already validated by the CLI, A1).
Policy parse_policy(const std::string& name);

struct SliceParams {
    Policy policy = Policy::FCFS;
    uint64_t quantum = 0;  // --quantum, bytes per round; only used by rr / drr
    int p_lines = 1;       // --p: whole lines grouped into one write (A8)
};

// Outcome of one round:
//   DONE      every byte transferred; final response sent; request finished
//   PREEMPTED round used up; requeue the request, all state kept in `req`
//   FAILED    I/O error or dead peer; nothing more can be done (release_request)
enum class SliceResult { DONE, PREEMPTED, FAILED };

// Serves one round of `req` on `fd` (K1/K2, slice.cpp).
//
//  fcfs / sjf   unbounded allowance: the whole request in a single round.
//  rr           allowance = Q bytes (A12).
//  drr          allowance = deficit + Q; unused allowance is kept in
//               req->deficit instead of being forfeited (A15).
//
//  GET is transferred in whole lines (A6), grouped `p_lines` per write (A8).
//  A round ends as soon as the next line would exceed the remaining allowance
//  (A13); under rr a line longer than Q is sent in full and ends the round
//  (A14, logged); under drr there is no such escape - the deficit grows until
//  the line fits (A16).
//  PUT is byte-exact (A9): a round reads min(allowance, remaining) bytes from
//  the socket and appends them to a temp file; nothing is rounded or forfeited.
//
// Increments req->rounds; maintains req->byte_offset, req->leftover,
// req->deficit, req->forfeited_bytes, req->file_fd (A17).
SliceResult serve_slice(Request* req, int fd, const SliceParams& params);

// Frees per-request transfer resources (file descriptor, unfinished temp
// file). Safe to call once for any finished request, whatever the outcome.
// Does not close the client socket.
void release_request(Request* req);

// Constructs the scheduler for --sched `policy`. Caller owns the result.
IScheduler* make_scheduler(const std::string& policy);

IScheduler* make_scheduler_fcfs();
IScheduler* make_scheduler_sjf();
IScheduler* make_scheduler_rr();
IScheduler* make_scheduler_drr();

#endif  // SERVER_SCHEDULER_H
