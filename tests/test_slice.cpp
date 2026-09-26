// Unit tests for the scheduling core: serve_slice (slice.cpp) and the four
// queue policies. Build + run:  make test
//
// serve_slice is driven directly over an AF_UNIX SOCK_SEQPACKET socketpair, so
// every send() the server makes arrives as its own message. That lets the test
// see exactly where round boundaries and write() boundaries fall, which is
// otherwise invisible on a TCP byte stream. The expected numbers below were
// worked out by hand from the spec (A6, A8, A12-A17), not copied from output.
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../src/common/protocol.h"
#include "../src/server/scheduler.h"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        ++g_checks;                                                   \
        if (!(cond)) {                                                \
            ++g_fail;                                                 \
            std::printf("    FAIL line %d: %s\n", __LINE__, msg);     \
        }                                                             \
    } while (0)

static const char* kErrLog = "/tmp/slice_test_stderr.log";

static int count_a14() {
    std::fflush(stderr);
    std::ifstream in(kErrLog);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        if (line.rfind("A14 request_id=", 0) == 0) ++n;
    }
    return n;
}

static std::string make_dir() {
    char t[] = "/tmp/slice_test_XXXXXX";
    return mkdtemp(t);
}

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
}

// n lines of exactly `len` bytes each, '\n' included.
static std::string lines(int n, int len, char base = 'a') {
    std::string s;
    for (int i = 0; i < n; ++i) s += std::string(static_cast<size_t>(len - 1), static_cast<char>(base + i % 26)) + "\n";
    return s;
}

struct GetRun {
    std::vector<std::vector<std::string>> rounds;  // messages received per round (round 0 starts with the OK header)
    std::vector<SliceResult> results;
    Request req;
    std::string payload(size_t round) const {
        std::string s;
        for (size_t i = 0; i < rounds[round].size(); ++i) {
            if (round == 0 && i == 0) continue;  // "OK <n>\n"
            s += rounds[round][i];
        }
        return s;
    }
    std::string all_payload() const {
        std::string s;
        for (size_t r = 0; r < rounds.size(); ++r) s += payload(r);
        return s;
    }
};

static GetRun run_get(const std::string& dir, const std::string& content, const SliceParams& sp,
                      uint64_t id = 1, bool print_rounds = false) {
    GetRun out;
    std::string path = dir + "/f.txt";
    write_file(path, content);
    Request& r = out.req;
    r.op = Request::Op::GET;
    r.id = id;
    r.filename = "f.txt";
    r.path = path;
    r.file_fd = open(path.c_str(), O_RDONLY);
    r.bytes = content.size();

    int sv[2];
    socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv);
    std::vector<std::vector<std::string>> rounds(1);
    std::thread reader([&] {
        std::vector<char> buf(1 << 20);
        for (;;) {
            ssize_t n = recv(sv[1], buf.data(), buf.size(), 0);
            if (n <= 0) break;
            std::string m(buf.data(), static_cast<size_t>(n));
            if (m == "@@END@@") break;
            if (m == "@@ROUND@@") { rounds.emplace_back(); continue; }
            rounds.back().push_back(m);
        }
    });
    for (int guard = 0; guard < 100000; ++guard) {
        SliceResult res = serve_slice(&r, sv[0], sp);
        out.results.push_back(res);
        send(sv[0], "@@ROUND@@", 9, 0);
        if (res != SliceResult::PREEMPTED) break;
    }
    send(sv[0], "@@END@@", 7, 0);
    reader.join();
    rounds.pop_back();
    out.rounds = rounds;
    release_request(&r);
    close(sv[0]);
    close(sv[1]);
    if (print_rounds) {
        for (size_t i = 0; i < out.rounds.size(); ++i) std::printf("      round %zu: %zu bytes\n", i + 1, out.payload(i).size());
    }
    return out;
}

static SliceParams params(Policy p, uint64_t q = 0, int pl = 1) {
    SliceParams sp;
    sp.policy = p;
    sp.quantum = q;
    sp.p_lines = pl;
    return sp;
}

static bool ends_on_line_boundary(const std::string& s) { return s.empty() || s.back() == '\n'; }

// ------------------------------------------------------------------ tests --

static void test_rr_forfeit(const std::string& dir) {
    std::printf("rr: 10 lines x 10B, Q=35 -> 3 lines/round, forfeits 5 each preempted round\n");
    std::string content = lines(10, 10);
    GetRun g = run_get(dir, content, params(Policy::RR, 35));
    CHECK(g.all_payload() == content, "bytes received == file");
    CHECK(g.req.rounds == 4, "rounds == 4 (3+3+3+1 lines)");
    CHECK(g.req.forfeited_bytes == 15, "forfeited == 3 preempted rounds x 5");
    CHECK(g.results.back() == SliceResult::DONE && g.results.size() == 4, "3 PREEMPTED then DONE");
    for (size_t i = 0; i < g.rounds.size(); ++i) {
        CHECK(ends_on_line_boundary(g.payload(i)), "round ends on a line boundary (A6)");
        CHECK(g.payload(i).size() <= 35, "round never exceeds Q without A14 (A12)");
    }
    CHECK(g.payload(0).size() == 30 && g.payload(3).size() == 10, "round sizes 30,30,30,10");
}

static void test_drr_deficit(const std::string& dir) {
    std::printf("drr: same file/Q -> unused allowance carries over, nothing forfeited\n");
    std::string content = lines(10, 10);
    GetRun g = run_get(dir, content, params(Policy::DRR, 35));
    CHECK(g.all_payload() == content, "bytes received == file");
    CHECK(g.req.rounds == 3, "rounds == 3 (30, 40, 30 bytes: deficit 5 -> allowance 40)");
    CHECK(g.req.forfeited_bytes == 0, "forfeited_bytes == 0 under drr (A23)");
    CHECK(g.req.deficit == 0, "deficit discarded when the request leaves (A15)");
    CHECK(g.payload(0).size() == 30 && g.payload(1).size() == 40 && g.payload(2).size() == 30,
          "round sizes 30,40,30");
}

static void test_a14_rr(const std::string& dir) {
    std::printf("rr + A14: lines 10,10,20,10 with Q=15\n");
    std::string content = lines(2, 10) + std::string(19, 'x') + "\n" + std::string(9, 'y') + "\n";
    int before = count_a14();
    GetRun g = run_get(dir, content, params(Policy::RR, 15), 42);
    CHECK(g.all_payload() == content, "bytes received == file");
    CHECK(g.req.rounds == 3, "rounds == 3");
    CHECK(g.req.forfeited_bytes == 5, "forfeited == 5 (round 1 only; the A14 overrun forfeits nothing)");
    CHECK(count_a14() - before == 1, "A14 logged exactly once");
    CHECK(g.payload(0).size() == 10 && g.payload(1).size() == 30 && g.payload(2).size() == 10,
          "round sizes 10, 30 (10 + overrunning 20-byte line), 10");
    for (size_t i = 0; i < g.rounds.size(); ++i) CHECK(ends_on_line_boundary(g.payload(i)), "line boundary");
}

static void test_a14_drr(const std::string& dir) {
    std::printf("drr, same file: no A14; deficit grows until the 20-byte line fits (A16)\n");
    std::string content = lines(2, 10) + std::string(19, 'x') + "\n" + std::string(9, 'y') + "\n";
    int before = count_a14();
    GetRun g = run_get(dir, content, params(Policy::DRR, 15));
    CHECK(g.all_payload() == content, "bytes received == file");
    CHECK(count_a14() - before == 0, "A14 never fires under drr");
    CHECK(g.req.rounds == 4, "rounds == 4");
    CHECK(g.req.forfeited_bytes == 0, "forfeited == 0");
    CHECK(g.payload(0).size() == 10 && g.payload(1).size() == 10 && g.payload(2).size() == 20 &&
              g.payload(3).size() == 10,
          "round sizes 10,10,20,10");
    CHECK(g.payload(2).size() <= 10 + 15, "the long line needed deficit: round 3 allowance was 10+15");
}

static void test_batching(const std::string& dir) {
    std::printf("--p 3: whole lines grouped 3 per write, accounting unchanged (A8)\n");
    std::string content = lines(10, 10);

    GetRun a = run_get(dir, content, params(Policy::FCFS, 0, 3));
    CHECK(a.all_payload() == content, "fcfs bytes identical");
    CHECK(a.req.rounds == 1, "fcfs: one round (A23)");
    std::vector<size_t> sizes;
    for (size_t i = 1; i < a.rounds[0].size(); ++i) sizes.push_back(a.rounds[0][i].size());
    CHECK((sizes == std::vector<size_t>{30, 30, 30, 10}), "writes of 3,3,3,1 lines (short last group)");

    GetRun p1 = run_get(dir, content, params(Policy::FCFS, 0, 1));
    CHECK(p1.rounds[0].size() == 11, "--p 1: header + 10 one-line writes");

    // Same rr accounting with and without batching.
    GetRun r1 = run_get(dir, content, params(Policy::RR, 35, 1));
    GetRun r3 = run_get(dir, content, params(Policy::RR, 35, 3));
    CHECK(r1.req.rounds == r3.req.rounds && r1.req.forfeited_bytes == r3.req.forfeited_bytes,
          "--p changes only write grouping, not rounds/forfeiture");
    for (size_t r = 0; r < r3.rounds.size(); ++r)
        for (size_t i = (r == 0 ? 1 : 0); i < r3.rounds[r].size(); ++i)
            CHECK(r3.rounds[r][i].size() % 10 == 0 && ends_on_line_boundary(r3.rounds[r][i]), "no partial line in any write");
}

static void test_edge_files(const std::string& dir) {
    std::printf("edge files: no trailing newline, empty, one huge line\n");
    GetRun a = run_get(dir, "abc\ndef", params(Policy::FCFS));
    CHECK(a.all_payload() == "abc\ndef", "final line without \\n is still sent");
    GetRun rr = run_get(dir, "abc\ndef", params(Policy::RR, 4));
    CHECK(rr.all_payload() == "abc\ndef" && rr.results.back() == SliceResult::DONE, "same under rr, Q=4");

    GetRun e = run_get(dir, "", params(Policy::RR, 10));
    CHECK(e.rounds[0].size() == 1 && e.rounds[0][0] == "OK 0\n" && e.results.size() == 1, "empty file: OK 0 and done");

    std::string big(300000, 'z');  // one line, 300000 bytes, no newline at all
    int before = count_a14();
    GetRun h = run_get(dir, big, params(Policy::RR, 1000));
    CHECK(h.all_payload() == big && h.req.rounds == 1 && count_a14() - before == 1, "single line >> Q under rr: one round, A14");
    GetRun hd = run_get(dir, big, params(Policy::DRR, 100000));
    CHECK(hd.all_payload() == big && hd.req.rounds == 3, "same under drr, Q=100000: 3 rounds (deficit 200000 -> 300000)");
}

static void test_pinned_file(const std::string& dir) {
    std::printf("GET keeps serving the version it was sized on, even if a PUT renames over it (A7)\n");
    std::string path = dir + "/f.txt";
    std::string content = lines(20, 10);
    write_file(path, content);
    Request r;
    r.op = Request::Op::GET;
    r.id = 5;
    r.filename = "f.txt";
    r.path = path;
    r.file_fd = open(path.c_str(), O_RDONLY);
    r.bytes = content.size();
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    SliceParams sp = params(Policy::RR, 50);
    SliceResult first = serve_slice(&r, sv[0], sp);
    CHECK(first == SliceResult::PREEMPTED, "first round preempted");
    write_file(dir + "/new.tmp", lines(3, 7, 'Q'));           // a PUT completes meanwhile:
    rename((dir + "/new.tmp").c_str(), path.c_str());           //   different content AND size
    SliceResult res = SliceResult::PREEMPTED;
    while (res == SliceResult::PREEMPTED) res = serve_slice(&r, sv[0], sp);
    CHECK(res == SliceResult::DONE, "finishes");
    shutdown(sv[0], SHUT_WR);
    std::string got;
    char buf[4096];
    ssize_t n;
    while ((n = recv(sv[1], buf, sizeof buf, 0)) > 0) got.append(buf, static_cast<size_t>(n));
    CHECK(got == "OK 200\n" + content, "client got the original 200-byte version, not the new file");
    release_request(&r);
    close(sv[0]);
    close(sv[1]);
}

static void test_put(const std::string& dir) {
    std::printf("PUT: byte-exact rounds, leftover header bytes consumed first, atomic install (A9, A13, A17)\n");
    std::string body;
    for (int i = 0; i < 10; ++i) body += "0123456789";
    Request r;
    r.op = Request::Op::PUT;
    r.id = 7;
    r.filename = "up.txt";
    r.path = dir + "/up.txt";
    r.bytes = body.size();
    r.leftover.assign(body.begin(), body.begin() + 25);  // bytes over-read together with the header

    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    send(sv[1], body.data() + 25, body.size() - 25, 0);

    SliceParams sp = params(Policy::RR, 40);
    CHECK(serve_slice(&r, sv[0], sp) == SliceResult::PREEMPTED && r.byte_offset == 40, "round 1: exactly Q=40 bytes");
    CHECK(access((dir + "/up.txt").c_str(), F_OK) != 0, "destination not visible while incomplete");
    CHECK(r.leftover.empty(), "leftover fully consumed first");
    CHECK(serve_slice(&r, sv[0], sp) == SliceResult::PREEMPTED && r.byte_offset == 80, "round 2: another 40");
    CHECK(serve_slice(&r, sv[0], sp) == SliceResult::DONE && r.byte_offset == 100, "round 3: remaining 20 (no rounding)");
    CHECK(r.rounds == 3 && r.forfeited_bytes == 0 && r.deficit == 0, "3 rounds, nothing forfeited");

    std::ifstream in(dir + "/up.txt", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    CHECK(ss.str() == body, "installed file is byte-identical");
    CHECK(access((dir + "/up.txt.tmp.7").c_str(), F_OK) != 0, "temp file removed");

    char buf[64];
    ssize_t n = recv(sv[1], buf, sizeof buf, MSG_DONTWAIT);
    CHECK(n == 10 && std::string(buf, 10) == "OK 0\nOK 0\n", "client saw exactly the two OK 0 lines");
    release_request(&r);

    // A body shorter than declared must fail cleanly and leave nothing behind.
    Request s;
    s.op = Request::Op::PUT;
    s.id = 8;
    s.filename = "short.txt";
    s.path = dir + "/short.txt";
    s.bytes = 100;
    int sv2[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv2);
    send(sv2[1], "only ten b", 10, 0);
    shutdown(sv2[1], SHUT_WR);
    CHECK(serve_slice(&s, sv2[0], params(Policy::FCFS)) == SliceResult::FAILED, "short body -> FAILED");
    release_request(&s);
    CHECK(access((dir + "/short.txt").c_str(), F_OK) != 0 && access((dir + "/short.txt.tmp.8").c_str(), F_OK) != 0,
          "no partial file left behind");
    close(sv[0]); close(sv[1]); close(sv2[0]); close(sv2[1]);
}

static void test_policies() {
    std::printf("queue policies\n");
    Request a, b, c, d;
    a.id = 1; a.bytes = 300;
    b.id = 2; b.bytes = 100;
    c.id = 3; c.bytes = 200;
    d.id = 4; d.bytes = 100;

    IScheduler* sjf = make_scheduler("sjf");
    for (Request* r : {&a, &b, &c, &d}) sjf->enqueue(r);
    CHECK(sjf->queue_depth() == 4, "depth 4");
    CHECK(sjf->next() == &b && sjf->next() == &d && sjf->next() == &c && sjf->next() == &a,
          "sjf: 100(id2), 100(id4, later tie), 200, 300");
    delete sjf;

    IScheduler* fcfs = make_scheduler("fcfs");
    for (Request* r : {&a, &b, &c}) fcfs->enqueue(r);
    CHECK(fcfs->next() == &a && fcfs->next() == &b && fcfs->next() == &c, "fcfs: arrival order");
    delete fcfs;

    for (const char* name : {"rr", "drr"}) {
        Request x, y, z;
        x.id = 1; y.id = 2; z.id = 3;
        IScheduler* s = make_scheduler(name);
        s->enqueue(&x); s->enqueue(&y); s->enqueue(&z);
        Request* first = s->next();
        CHECK(first == &x && x.start_ns != 0, "first pick sets start_ns");
        uint64_t started = x.start_ns;
        s->requeue(&x);  // preempted: goes to the tail
        CHECK(s->queue_depth() == 3, "requeued request counts in queue depth (B8)");
        CHECK(s->next() == &y && s->next() == &z && s->next() == &x, "round robin: tail requeue");
        CHECK(x.start_ns == started, "start_ns not overwritten on later rounds (A18)");
        delete s;
    }
}

int main() {
    if (!std::freopen(kErrLog, "w", stderr)) return 2;  // capture the A14 log lines
    std::string dir = make_dir();
    test_rr_forfeit(dir);
    test_drr_deficit(dir);
    test_a14_rr(dir);
    test_a14_drr(dir);
    test_batching(dir);
    test_edge_files(dir);
    test_pinned_file(dir);
    test_put(dir);
    test_policies();
    std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
    std::string rm = "rm -rf " + dir;
    if (std::system(rm.c_str()) != 0) {}
    return g_fail == 0 ? 0 : 1;
}
