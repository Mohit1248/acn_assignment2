#include "protocol.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>

// ---- pure parsing / formatting (locked in Phase 0 - no socket I/O) --------
//
// Reasonable choices made here (stated per Ground Rules; mirror in README):
//   - Tokens are split on single/multiple ASCII spaces; extra/missing tokens
//     make a line MALFORMED rather than being tolerated.
//   - PUT's byte count must be a non-negative base-10 integer with no sign
//     and no leading/trailing junk; anything else is "non-numeric".
//   - An empty filename, or one containing '/', or exactly "." or "..", is
//     unsafe (A25). Filenames are otherwise not restricted (e.g. spaces are
//     allowed - GET/PUT lines are still exactly 2/3 whitespace-separated
//     tokens, so filenames containing spaces cannot be represented on the
///    wire; this is a known, accepted limitation of the line format).

namespace {

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool parse_u64(const std::string& tok, uint64_t& out) {
    if (tok.empty()) return false;
    for (char c : tok) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    errno = 0;
    char* end = nullptr;
    unsigned long long v = std::strtoull(tok.c_str(), &end, 10);
    if (errno != 0 || end == tok.c_str() || *end != '\0') return false;
    out = static_cast<uint64_t>(v);
    return true;
}

}  // namespace

ParsedRequestLine parse_request_line(const std::string& line) {
    ParsedRequestLine result;
    std::vector<std::string> tok = split_ws(line);

    if (tok.empty()) {
        result.error = "malformed request line";
        return result;
    }

    if (tok[0] == "GET") {
        if (tok.size() != 2) {
            result.error = "GET requires exactly one filename";
            return result;
        }
        result.type = ReqType::GET;
        result.filename = tok[1];
        return result;
    }

    if (tok[0] == "PUT") {
        if (tok.size() != 3) {
            result.error = "PUT requires a filename and a byte count";
            return result;
        }
        uint64_t bytes = 0;
        if (!parse_u64(tok[2], bytes)) {
            result.error = "missing or non-numeric byte count";
            return result;
        }
        result.type = ReqType::PUT;
        result.filename = tok[1];
        result.byte_count = bytes;
        return result;
    }

    if (tok[0] == "HEALTH") {
        if (tok.size() != 1) {
            result.error = "malformed request line";
            return result;
        }
        result.type = ReqType::HEALTH;
        return result;
    }

    result.error = "unknown request line";
    return result;
}

std::string format_ok(uint64_t n) {
    return "OK " + std::to_string(n) + "\n";
}

std::string format_err(const std::string& reason) {
    return "ERR " + reason + "\n";
}

ParsedResponseLine parse_response_line(const std::string& line) {
    ParsedResponseLine result;
    std::vector<std::string> tok = split_ws(line);

    if (!tok.empty() && tok[0] == "OK") {
        uint64_t n = 0;
        if (tok.size() == 2 && parse_u64(tok[1], n)) {
            result.ok = true;
            result.n = n;
            return result;
        }
        result.malformed = true;
        return result;
    }

    if (!tok.empty() && tok[0] == "ERR") {
        // reason is everything after "ERR ", not just the first token
        size_t pos = line.find(' ');
        result.ok = false;
        result.reason = (pos == std::string::npos) ? std::string() : line.substr(pos + 1);
        return result;
    }

    result.malformed = true;
    return result;
}

bool is_filename_safe(const std::string& name) {
    if (name.empty()) return false;
    if (name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos) return false;
    return true;
}

// ---- socket I/O primitives -------------------------------------------------
// Implementation owner: Stage 1 "server infrastructure" track (S2/S3/S9).
// TODO(server-infra): implement with recv()/send() over POSIX sockets.
//   - read_header_line: apply `timeout_ms` via SO_RCVTIMEO (or poll()); on a
///    partial recv() that reads past the '\n', copy the extra bytes into
//     `leftover` before returning - they belong to the body, not the header.
//   - read_exact: drain `leftover` first, then recv() the remainder; loop on
//     short reads; false on error/EOF before `n` bytes are collected.
//   - send_all: loop send() on short writes; false only on a real error.

HeaderReadResult read_header_line(int /*fd*/, int /*timeout_ms*/) {
    HeaderReadResult r;
    r.ok = false;
    r.error = "not implemented";
    return r;
}

bool read_exact(int /*fd*/, std::vector<char>& /*leftover*/, char* /*out*/, size_t /*n*/) {
    return false;
}

bool send_all(int /*fd*/, const char* /*data*/, size_t /*n*/) {
    return false;
}
