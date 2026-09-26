#ifndef COMMON_LOGGING_H
#define COMMON_LOGGING_H

#include <cstdint>
#include <cstdio>
#include <string>

// Locked log line format for the A14 oversized-line escape hatch (rr only -
// drr never fires this, A16). The rr-vs-drr comparison script (A29, C7)
// greps the server's stderr for this exact prefix to count how often A14
// fired per run, so the format must not change without updating that script.
//
// Format (single line, to stderr):
//   A14 request_id=<id> filename=<name> line_bytes=<L> quantum=<Q>
//
// The whole line is built first and emitted with one fputs(), so lines from
// concurrent workers never interleave.
inline void log_a14_fire(uint64_t request_id, const std::string& filename,
                          uint64_t line_bytes, uint64_t quantum) {
    std::string line = "A14 request_id=" + std::to_string(request_id) +
                       " filename=" + filename +
                       " line_bytes=" + std::to_string(line_bytes) +
                       " quantum=" + std::to_string(quantum) + "\n";
    std::fputs(line.c_str(), stderr);
}

#endif  // COMMON_LOGGING_H
