#include "scheduler.h"

// TODO(K1 + K2, Stage 2, Mohit): serve_slice.
//   K1 (A6, A8): GET must never emit a partial line as the last thing sent
//   before preempting; a round ends exactly at a line boundary, except the
//   A14 escape hatch under plain rr (log via log_a14_fire(), see
//   common/logging.h - drr never takes this path, A16). --p batches up to
//   p_lines whole lines per write without changing which bytes are sent,
//   their order, or the quantum accounting (A8). PUT is byte-opaque (A9):
//   read exactly quantum_bytes from the socket and write them to disk, no
//   line scanning.
//   K2 (A17): on PREEMPTED, req->byte_offset, req->leftover (any body bytes
//   already read off the socket but not yet written to disk) and
//   req->deficit (drr) must all be left in a state such that a later call
//   with the same req resumes exactly where this call left off.
//
// This stub does nothing and always reports DONE - it must not be linked
// into a real scheduler policy until implemented; scheduler_fcfs/sjf's
// single-round contract still holds even against this stub since they pass
// quantum_bytes = req->bytes.
SliceResult serve_slice(Request* /*req*/, int /*fd*/, uint64_t /*quantum_bytes*/, int /*p_lines*/) {
    return SliceResult::DONE;
}
