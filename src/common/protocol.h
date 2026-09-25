#ifndef COMMON_PROTOCOL_H
#define COMMON_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

// The wire protocol, locked per the assignment spec ("The wire protocol" /
// "Framing rules"). Every message is '\n'-delimited; bodies are opaque byte
// counts carried in the header line itself. A connection carries exactly one
// request.
//
// Implementation owner (Phase 0 -> Stage 1 "server infra" track): fills in
// protocol.cpp. This header is the locked contract both tracks build against.

// ---- reading a framed header line off a raw socket ------------------------

struct HeaderReadResult {
    bool ok = false;             // false on timeout, socket error, or peer closed before a full line arrived
    std::string line;            // header line WITHOUT the trailing '\n'
    std::vector<char> leftover;  // bytes already read past the '\n' in the same recv() call -
                                  // MUST be handed to the body reader, never discarded (framing rules)
    std::string error;           // human-readable reason when ok == false
};

// Reads one '\n'-delimited line from fd. `timeout_ms` bounds the whole read
// (A5: a client that connects and sends nothing must not block admission or
// pin a worker indefinitely) - implement with SO_RCVTIMEO or poll()/select().
HeaderReadResult read_header_line(int fd, int timeout_ms);

// Reads exactly `n` bytes from fd into `out`. Drains `leftover` first (bytes
// already read off the socket but not yet consumed), then recv()s the rest.
// `leftover` is left empty (or with its unused tail) after the call.
// Returns false on socket error or peer closing before `n` bytes arrive.
bool read_exact(int fd, std::vector<char>& leftover, char* out, size_t n);

// Sends exactly `n` bytes, looping over short send()s. Returns false only on
// a real send error. This is the raw primitive with no line-boundary or
// --p batching behaviour - see scheduler.h / serve_slice for A6/A8.
bool send_all(int fd, const char* data, size_t n);

// ---- request line parsing ---------------------------------------------------

enum class ReqType { GET, PUT, HEALTH, MALFORMED };

struct ParsedRequestLine {
    ReqType type = ReqType::MALFORMED;
    std::string filename;     // GET, PUT
    uint64_t byte_count = 0;  // PUT only
    std::string error;        // set when type == MALFORMED; use as the ERR <reason> text (A24)
};

// Parses one header line already stripped of its trailing '\n':
//   "GET <name>"        "PUT <name> <bytes>"        "HEALTH"
// Any other shape - unknown verb, missing name, missing/non-numeric byte
// count - produces MALFORMED with `error` set (A24). Does NOT do filename
// sandboxing (A25); that is a separate check against --file.
ParsedRequestLine parse_request_line(const std::string& line);

// ---- response formatting / parsing -----------------------------------------

// "OK <n>\n"
std::string format_ok(uint64_t n);
// "ERR <reason>\n" - reason must not itself contain '\n'.
std::string format_err(const std::string& reason);

struct ParsedResponseLine {
    bool ok = false;       // true for "OK <n>", false for "ERR <reason>" or malformed
    bool malformed = false;
    uint64_t n = 0;        // valid when ok == true
    std::string reason;    // valid when ok == false && !malformed
};

// Used by the client (GET/PUT confirmations) and by the load balancer
// (HEALTH replies, B7/B8) to parse a response line already stripped of '\n'.
ParsedResponseLine parse_response_line(const std::string& line);

// ---- filename validation (A25) ---------------------------------------------

// Rejects names containing '/', or equal to "." or "..". Does not check
// existence - callers combine this with the --file base directory.
bool is_filename_safe(const std::string& name);

#endif  // COMMON_PROTOCOL_H
