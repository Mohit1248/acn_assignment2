#include "protocol.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

}

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
    if (name.find('\0') != std::string::npos) return false;
    return true;
}

namespace {

using Clock = std::chrono::steady_clock;

bool arm_remaining(int fd, Clock::time_point deadline) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (left <= 0) return false;
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(left / 1000);
    tv.tv_usec = static_cast<suseconds_t>((left % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return true;
}

void set_recv_timeout(int fd, int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

}

HeaderReadResult read_header_line(int fd, int timeout_ms) {
    HeaderReadResult r;

    const bool bounded = timeout_ms >= 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(bounded ? timeout_ms : 0);

    std::string line;
    char buf[512];
    while (true) {
        if (bounded && !arm_remaining(fd, deadline)) {
            r.ok = false;
            r.error = "header read timeout";
            return r;
        }
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            r.ok = false;
            r.error = (errno == EAGAIN || errno == EWOULDBLOCK)
                          ? "header read timeout"
                          : "recv error";
            return r;
        }
        if (n == 0) {
            r.ok = false;
            r.error = "peer closed before header line completed";
            return r;
        }

        char* nl = static_cast<char*>(memchr(buf, '\n', static_cast<size_t>(n)));
        if (nl) {
            size_t line_part = static_cast<size_t>(nl - buf);
            line.append(buf, line_part);
            size_t consumed = line_part + 1;
            size_t remaining = static_cast<size_t>(n) - consumed;
            if (remaining > 0) {
                r.leftover.assign(buf + consumed, buf + consumed + remaining);
            }
            r.ok = true;
            r.line = line;
            if (bounded) set_recv_timeout(fd, timeout_ms);
            return r;
        }
        line.append(buf, static_cast<size_t>(n));
        if (line.size() > 8192) {
            r.ok = false;
            r.error = "header line too long";
            return r;
        }
    }
}

bool read_exact(int fd, std::vector<char>& leftover, char* out, size_t n, int total_timeout_ms) {
    size_t filled = 0;
    const bool bounded = total_timeout_ms >= 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(bounded ? total_timeout_ms : 0);

    if (!leftover.empty()) {
        size_t take = std::min(leftover.size(), n);
        if (take > 0 && out != nullptr) {
            std::memcpy(out, leftover.data(), take);
        }
        filled += take;
        leftover.erase(leftover.begin(), leftover.begin() + static_cast<long>(take));
    }

    while (filled < n) {
        if (bounded && !arm_remaining(fd, deadline)) return false;
        ssize_t r = recv(fd, out + filled, n - filled, 0);
        if (r <= 0) return false;
        filled += static_cast<size_t>(r);
    }
    return true;
}

static std::atomic<uint64_t> g_send_calls{0};

uint64_t send_call_count() { return g_send_calls.load(); }

bool send_all(int fd, const char* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        g_send_calls.fetch_add(1, std::memory_order_relaxed);
        ssize_t s = send(fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (s < 0 && errno == EINTR) continue;
        if (s <= 0) return false;
        sent += static_cast<size_t>(s);
    }
    return true;
}
