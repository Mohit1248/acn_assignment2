#include "scheduler.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "../common/logging.h"
#include "../common/protocol.h"

namespace {

constexpr uint64_t kUnbounded = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kChunk = 64 * 1024;
constexpr int kBodyChunkTimeoutMs = 10000;

bool bounded(Policy p) { return p == Policy::RR || p == Policy::DRR; }

void send_error(int fd, const char* reason) {
    std::string e = format_err(reason);
    send_all(fd, e.data(), e.size());
}

uint64_t line_length_at(int fd, uint64_t offset, uint64_t file_size) {
    char buf[16 * 1024];
    uint64_t pos = offset;
    while (pos < file_size) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(sizeof(buf), file_size - pos));
        ssize_t n = pread(fd, buf, want, static_cast<off_t>(pos));
        if (n <= 0) return 0;
        const void* nl = memchr(buf, '\n', static_cast<size_t>(n));
        if (nl != nullptr) {
            return (pos - offset) + static_cast<uint64_t>(static_cast<const char*>(nl) - buf) + 1;
        }
        pos += static_cast<uint64_t>(n);
    }
    return file_size - offset;
}

bool append_range(int fd, uint64_t offset, uint64_t len, std::string& out) {
    size_t old_size = out.size();
    out.resize(old_size + static_cast<size_t>(len));
    uint64_t done = 0;
    while (done < len) {
        ssize_t n = pread(fd, &out[old_size + static_cast<size_t>(done)],
                          static_cast<size_t>(len - done), static_cast<off_t>(offset + done));
        if (n <= 0) {
            out.resize(old_size);
            return false;
        }
        done += static_cast<uint64_t>(n);
    }
    return true;
}

bool stream_range(int file_fd, int sock, uint64_t offset, uint64_t len) {
    std::vector<char> buf(static_cast<size_t>(std::min<uint64_t>(len, kChunk)));
    uint64_t done = 0;
    while (done < len) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(buf.size(), len - done));
        ssize_t n = pread(file_fd, buf.data(), want, static_cast<off_t>(offset + done));
        if (n <= 0) return false;
        if (!send_all(sock, buf.data(), static_cast<size_t>(n))) return false;
        done += static_cast<uint64_t>(n);
    }
    return true;
}

struct Batch {
    std::string data;
    int lines = 0;
    bool flush(int sock) {
        bool ok = data.empty() || send_all(sock, data.data(), data.size());
        data.clear();
        lines = 0;
        return ok;
    }
};

SliceResult serve_get(Request* r, int fd, const SliceParams& sp) {
    r->rounds += 1;

    if (!r->started) {
        std::string ok = format_ok(r->bytes);
        if (!send_all(fd, ok.data(), ok.size())) return SliceResult::FAILED;
        r->started = true;
    }

    const bool bnd = bounded(sp.policy);
    uint64_t left = kUnbounded;
    if (bnd) {
        const uint64_t carried = sp.policy == Policy::DRR ? r->deficit : 0;
        left = carried > kUnbounded - sp.quantum ? kUnbounded : carried + sp.quantum;
    }

    const int p = std::max(1, sp.p_lines);
    Batch batch;
    uint64_t sent_this_round = 0;

    while (r->byte_offset < r->bytes) {
        uint64_t len = line_length_at(r->file_fd, r->byte_offset, r->bytes);
        if (len == 0) return SliceResult::FAILED;

        if (bnd) {
            if (sp.policy == Policy::RR && sent_this_round == 0 && len > sp.quantum) {
                if (!batch.flush(fd)) return SliceResult::FAILED;
                if (!stream_range(r->file_fd, fd, r->byte_offset, len)) return SliceResult::FAILED;
                r->byte_offset += len;
                log_a14_fire(r->id, r->filename, len, sp.quantum);
                return r->byte_offset >= r->bytes ? SliceResult::DONE : SliceResult::PREEMPTED;
            }
            if (len > left) {
                if (!batch.flush(fd)) return SliceResult::FAILED;
                if (sp.policy == Policy::RR) r->forfeited_bytes += left;
                else r->deficit = left;
                return SliceResult::PREEMPTED;
            }
        }

        if (len > kChunk) {
            if (!batch.flush(fd)) return SliceResult::FAILED;
            if (!stream_range(r->file_fd, fd, r->byte_offset, len)) return SliceResult::FAILED;
        } else {
            if (!append_range(r->file_fd, r->byte_offset, len, batch.data)) return SliceResult::FAILED;
            if (++batch.lines >= p && !batch.flush(fd)) return SliceResult::FAILED;
        }
        r->byte_offset += len;
        sent_this_round += len;
        if (bnd) left -= len;
    }

    if (!batch.flush(fd)) return SliceResult::FAILED;
    r->deficit = 0;
    return SliceResult::DONE;
}

bool write_all(int fd, const char* data, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, data + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        done += static_cast<size_t>(w);
    }
    return true;
}

SliceResult serve_put(Request* r, int fd, const SliceParams& sp) {
    r->rounds += 1;

    if (!r->started) {
        r->tmp_path = r->path + ".tmp." + std::to_string(r->id);
        r->file_fd = open(r->tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (r->file_fd < 0) {
            r->tmp_path.clear();
            send_error(fd, "cannot write file");
            return SliceResult::FAILED;
        }
        std::string ok = format_ok(0);
        if (!send_all(fd, ok.data(), ok.size())) return SliceResult::FAILED;
        r->started = true;
    }

    const uint64_t remaining = r->bytes - r->byte_offset;
    const uint64_t take = bounded(sp.policy) ? std::min(sp.quantum, remaining) : remaining;

    std::vector<char> buf(static_cast<size_t>(std::min<uint64_t>(take, kChunk)));
    uint64_t done = 0;
    while (done < take) {
        size_t n = static_cast<size_t>(std::min<uint64_t>(buf.size(), take - done));
        if (!read_exact(fd, r->leftover, buf.data(), n, kBodyChunkTimeoutMs)) {
            send_error(fd, "body shorter than declared");
            return SliceResult::FAILED;
        }
        if (!write_all(r->file_fd, buf.data(), n)) {
            send_error(fd, "cannot write file");
            return SliceResult::FAILED;
        }
        r->byte_offset += n;
        done += n;
    }

    if (r->byte_offset < r->bytes) return SliceResult::PREEMPTED;

    close(r->file_fd);
    r->file_fd = -1;
    if (rename(r->tmp_path.c_str(), r->path.c_str()) != 0) {
        send_error(fd, "cannot write file");
        return SliceResult::FAILED;
    }
    r->tmp_path.clear();

    std::string done_ok = format_ok(0);
    return send_all(fd, done_ok.data(), done_ok.size()) ? SliceResult::DONE : SliceResult::FAILED;
}

}

Policy parse_policy(const std::string& name) {
    if (name == "sjf") return Policy::SJF;
    if (name == "rr") return Policy::RR;
    if (name == "drr") return Policy::DRR;
    return Policy::FCFS;
}

SliceResult serve_slice(Request* req, int fd, const SliceParams& params) {
    return req->op == Request::Op::GET ? serve_get(req, fd, params) : serve_put(req, fd, params);
}

void release_request(Request* req) {
    if (req->file_fd >= 0) {
        close(req->file_fd);
        req->file_fd = -1;
    }
    if (!req->tmp_path.empty()) {
        unlink(req->tmp_path.c_str());
        req->tmp_path.clear();
    }
}
