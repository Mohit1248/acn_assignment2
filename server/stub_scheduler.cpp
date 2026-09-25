#include "stub_scheduler.h"

#include <cstdio>
#include <fstream>
#include <vector>

#include "../common/clock.h"
#include "../common/protocol.h"
#include "queue.h"

namespace {

class StubScheduler : public IScheduler {
public:
    void enqueue(Request* req) override { queue_.push(req); }

    Request* next() override {
        Request* req = queue_.pop_pick([](const std::vector<Request*>&) { return size_t(0); });
        if (req && req->start_ns == 0) {
            req->start_ns = now_monotonic_ns();
        }
        return req;
    }

    void requeue(Request* req) override { queue_.push(req); }  // stub never preempts

    size_t queue_depth() const override { return queue_.size(); }

    void shutdown() override { queue_.shutdown(); }

private:
    RequestQueue queue_;
};

}  // namespace

IScheduler* make_stub_scheduler() { return new StubScheduler(); }

bool stub_serve_whole(Request* req, int fd, const std::string& full_path) {
    if (req->op == Request::Op::GET) {
        std::ifstream in(full_path, std::ios::binary);
        if (!in) {
            std::string err = format_err("file not found");
            send_all(fd, err.data(), err.size());
            return false;
        }
        in.seekg(0, std::ios::end);
        uint64_t size = static_cast<uint64_t>(in.tellg());
        in.seekg(0, std::ios::beg);
        req->bytes = size;

        std::string ok = format_ok(size);
        if (!send_all(fd, ok.data(), ok.size())) return false;

        std::vector<char> buf(size);
        if (size > 0) in.read(buf.data(), static_cast<std::streamsize>(size));
        req->byte_offset = size;
        return send_all(fd, buf.data(), buf.size());
    }

    // PUT
    std::string ok = format_ok(0);
    if (!send_all(fd, ok.data(), ok.size())) return false;

    std::vector<char> buf(req->bytes);
    if (!read_exact(fd, req->leftover, req->bytes > 0 ? buf.data() : nullptr, req->bytes)) {
        return false;
    }

    // Write to a temp file, then rename() it over the destination. rename is
    // atomic on POSIX, so a concurrent GET of the same name always sees a
    // complete old or complete new file - never the truncated/half-written one
    // that an in-place ofstream(trunc) exposes. (The load driver PUTs and GETs
    // the same few files from many threads, so this race is hit constantly.)
    std::string tmp_path = full_path + ".tmp." + std::to_string(req->id);
    bool wrote = false;
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (out) {
            if (req->bytes > 0) out.write(buf.data(), static_cast<std::streamsize>(req->bytes));
            out.flush();
            wrote = static_cast<bool>(out);
        }
    }
    if (!wrote || std::rename(tmp_path.c_str(), full_path.c_str()) != 0) {
        std::remove(tmp_path.c_str());
        std::string err = format_err("cannot write file");  // A24: never a silent close
        send_all(fd, err.data(), err.size());
        return false;
    }
    req->byte_offset = req->bytes;

    std::string done = format_ok(0);
    return send_all(fd, done.data(), done.size());
}
