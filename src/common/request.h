#ifndef COMMON_REQUEST_H
#define COMMON_REQUEST_H

#include <cstdint>
#include <string>
#include <vector>

// One admitted-but-unserved request as it moves through the scheduler queue.
// Filled in when the header is parsed (A5), mutated in place across
// preemption/requeue (A17), and read out to the metrics CSV on completion (A23).
struct Request {
    enum class Op { GET, PUT };

    uint64_t id = 0;
    Op op = Op::GET;
    std::string filename;

    // GET: declared reply size once known (== file size on disk).
    // PUT: declared body size from the request line.
    uint64_t bytes = 0;

    // --- timestamps, CLOCK_MONOTONIC nanoseconds (A18) ---
    uint64_t arrival_ns = 0;  // header parsed, request admitted to queue
    uint64_t start_ns = 0;    // a worker first picks the request up (first slice only)
    uint64_t finish_ns = 0;   // last byte of the request has been transferred

    // --- scheduling bookkeeping ---
    int rounds = 0;               // times scheduled: 1 for fcfs/sjf, quantum-slice count for rr/drr (A23)
    uint64_t forfeited_bytes = 0; // rr only: unused per-round allowance discarded on preemption (A13, A23)
    uint64_t deficit = 0;         // drr only: carried allowance, starts at 0, discarded when request leaves (A15)

    // --- preemption state (A17): must survive a requeue ---
    uint64_t byte_offset = 0;             // bytes already transferred for this request
    std::vector<char> leftover;           // bytes read from the socket but not yet consumed
                                           // (PUT: read-ahead body bytes; GET has no socket input to leave over)

    int client_fd = -1;                   // connection this request is being served on
};

inline const char* op_to_string(Request::Op op) {
    return op == Request::Op::GET ? "GET" : "PUT";
}

#endif  // COMMON_REQUEST_H
