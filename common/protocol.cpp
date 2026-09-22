#include "protocol.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>

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
// These are generic, policy-free byte-shuffling helpers - used by both sides
// (client for put/get, C2; server once the accept loop lands, S2/S3). What's
// still Stage 1 "server infrastructure" work (S2/S3/S9) is everything these
// primitives don't decide: the accept loop and thread pool, which timeout
// value to apply when parsing a freshly-accepted connection's header, and
// how HEALTH/shutdown integrate with them.

namespace {
constexpr size_t kMaxHeaderLineLen = 8192;  // guards against an unbounded read if a client never sends '\n'
}  // namespace

HeaderReadResult read_header_line(int fd, int timeout_ms) {
    HeaderReadResult result;

    if (timeout_ms >= 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                result.error = "timed out waiting for header line";
            } else {
                result.error = std::string("recv error: ") + std::strerror(errno);
            }
            return result;
        }
        if (n == 0) {
            result.error = "connection closed before header line completed";
            return result;
        }

        void* nl = memchr(buf, '\n', static_cast<size_t>(n));
        if (nl != nullptr) {
            size_t line_len = static_cast<char*>(nl) - buf;
            result.line.append(buf, line_len);
            size_t consumed = line_len + 1;  // include the '\n' itself
            if (static_cast<size_t>(n) > consumed) {
                result.leftover.assign(buf + consumed, buf + n);
            }
            result.ok = true;
            return result;
        }

        result.line.append(buf, static_cast<size_t>(n));
        if (result.line.size() > kMaxHeaderLineLen) {
            result.error = "header line too long";
            return result;
        }
    }
}

bool read_exact(int fd, std::vector<char>& leftover, char* out, size_t n) {
    size_t filled = 0;

    if (!leftover.empty()) {
        size_t take = std::min(leftover.size(), n);
        if (take > 0) {
            std::memcpy(out, leftover.data(), take);
        }
        leftover.erase(leftover.begin(), leftover.begin() + static_cast<long>(take));
        filled = take;
    }

    while (filled < n) {
        ssize_t r = recv(fd, out + filled, n - filled, 0);
        if (r <= 0) return false;
        filled += static_cast<size_t>(r);
    }
    return true;
}

bool send_all(int fd, const char* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t s = send(fd, data + sent, n - sent, 0);
        if (s < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (s == 0) return false;
        sent += static_cast<size_t>(s);
    }
    return true;
}
