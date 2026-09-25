#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "../common/clock.h"
#include "../common/config.h"
#include "../common/csv_writer.h"
#include "../common/protocol.h"
#include "scheduler.h"
#include "stub_scheduler.h"

// CLI parsing + validation (A1) - Stage 1, S1, DONE (Phase 0).
// Accept loop / worker pool / signal-driven shutdown - S2/S3/S4/S6/S7/S8/S9,
// this pass.

namespace {

// TODO(S3): confirm this against the assignment PDF - not a config.json
// field, so it's a local constant for now.
constexpr int kHeaderTimeoutMs = 5000;

// Largest PUT body we accept. The spec sets no limit; without one, a request
// like "PUT x 99999999999999" makes the transfer path try to allocate that
// much memory and abort the whole server. Rejected with ERR at admission.
constexpr uint64_t kMaxPutBytes = 1ULL << 30;  // 1 GiB

struct Args {
    std::string sched;
    bool has_quantum = false;
    uint64_t quantum = 0;
    std::string file_dir;
    int p_lines = 1;
    std::string config_path = "config.json";
    std::string metrics_out = "metrics.csv";
};

[[noreturn]] void usage_error(const std::string& msg) {
    std::cerr << "error: " << msg << "\n";
    std::exit(1);
}

Args parse_args(int argc, char** argv) {
    Args args;
    bool have_sched = false;
    bool have_file = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next_val = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) usage_error(std::string("missing value for ") + flag);
            return argv[++i];
        };

        if (a == "--sched") {
            args.sched = next_val("--sched");
            have_sched = true;
        } else if (a == "--quantum") {
            std::string v = next_val("--quantum");
            char* end = nullptr;
            unsigned long long q = std::strtoull(v.c_str(), &end, 10);
            if (end == v.c_str() || *end != '\0') {
                usage_error("--quantum must be a non-negative integer");
            }
            args.quantum = q;
            args.has_quantum = true;
        } else if (a == "--file") {
            args.file_dir = next_val("--file");
            have_file = true;
        } else if (a == "--p") {
            std::string v = next_val("--p");
            char* end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (end == v.c_str() || *end != '\0' || n < 1) {
                usage_error("--p must be a positive integer (lines per write)");
            }
            args.p_lines = static_cast<int>(n);
        } else if (a == "--config") {
            args.config_path = next_val("--config");
        } else if (a == "--metrics-out") {
            args.metrics_out = next_val("--metrics-out");
        } else {
            usage_error("unknown flag '" + a + "'");
        }
    }

    if (!have_sched) usage_error("missing required flag --sched");
    if (args.sched != "fcfs" && args.sched != "sjf" && args.sched != "rr" && args.sched != "drr") {
        usage_error("--sched must be one of fcfs, sjf, rr, drr");
    }
    if (!have_file) usage_error("missing required flag --file");

    bool needs_quantum = (args.sched == "rr" || args.sched == "drr");
    if (needs_quantum && !args.has_quantum) {
        usage_error("--quantum is required when --sched is rr or drr");
    }
    if (!needs_quantum && args.has_quantum) {
        usage_error("--quantum is not allowed with --sched fcfs or sjf");
    }

    return args;
}

std::atomic<bool> g_shutdown{false};
void on_signal(int) { g_shutdown.store(true); }

std::atomic<uint64_t> g_next_id{1};
std::atomic<uint64_t> g_requests_served{0};
std::atomic<uint64_t> g_bytes_served{0};

// Admission threads are detached, so shutdown has to be able to wait for
// them: a request accepted just before SIGTERM must still be enqueued and
// answered (A26), and the scheduler/args must outlive every such thread.
std::atomic<int> g_active_admissions{0};

struct AdmissionGuard {
    ~AdmissionGuard() { g_active_admissions.fetch_sub(1); }
};

int make_listening_socket(const std::string& ip, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "error: socket() failed: " << std::strerror(errno) << "\n";
        std::exit(1);
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));  // A26

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "error: invalid server.ip in config: " << ip << "\n";
        std::exit(1);
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "error: bind() failed: " << std::strerror(errno) << "\n";
        std::exit(1);
    }
    if (listen(fd, 128) < 0) {
        std::cerr << "error: listen() failed: " << std::strerror(errno) << "\n";
        std::exit(1);
    }
    return fd;
}

void send_err_and_close(int fd, const std::string& reason) {
    std::string err = format_err(reason);
    send_all(fd, err.data(), err.size());  // best-effort; ignore failure here
    close(fd);
}

// Runs once per accepted connection, then exits. Decoupled from serving, so
// a silent/slow client only ties up its own short-lived thread - never a
// server worker, and never blocks HEALTH behind it.
void admit_connection(int fd, IScheduler* sched, const Args& args) {
    AdmissionGuard guard;  // counted in g_active_admissions by the acceptor

    // S3: bounded header read so a silent client can't pin a worker.
    HeaderReadResult hdr = read_header_line(fd, kHeaderTimeoutMs);
    if (!hdr.ok) {
        send_err_and_close(fd, hdr.error.empty() ? "header read timeout" : hdr.error);
        return;
    }

    ParsedRequestLine parsed = parse_request_line(hdr.line);
    if (parsed.type == ReqType::MALFORMED) {
        send_err_and_close(fd, parsed.error);  // S8/A24
        return;
    }

    if (parsed.type == ReqType::HEALTH) {
        // S6: immediate, out-of-band, never enqueued, never in the CSV.
        // Now genuinely immediate - no longer stuck behind a worker or a
        // stuck peer, since admission is decoupled from serving.
        std::string resp = format_ok(sched->queue_depth());
        send_all(fd, resp.data(), resp.size());
        close(fd);
        return;
    }

    // S4: filename sandboxing before any file I/O.
    if (!is_filename_safe(parsed.filename)) {
        send_err_and_close(fd, "unsafe filename");
        return;
    }
    if (parsed.type == ReqType::PUT && parsed.byte_count > kMaxPutBytes) {
        send_err_and_close(fd, "declared size too large");  // A24
        return;
    }
    std::string full_path = args.file_dir + "/" + parsed.filename;

    auto* req = new Request();
    req->id = g_next_id.fetch_add(1);
    req->client_fd = fd;
    req->filename = parsed.filename;
    req->leftover = std::move(hdr.leftover);
    req->arrival_ns = now_monotonic_ns();

    if (parsed.type == ReqType::GET) {
        req->op = Request::Op::GET;
        struct stat st{};
        if (::stat(full_path.c_str(), &st) != 0) {
            send_err_and_close(fd, "file not found");
            delete req;
            return;
        }
        req->bytes = static_cast<uint64_t>(st.st_size);  // declared size, per request.h
    } else {
        req->op = Request::Op::PUT;
        req->bytes = parsed.byte_count;
    }

    sched->enqueue(req);  // admitted (A5) - queue can now genuinely build up
}

// Single thread: only ever calls accept(). Spins off a detached admission
// thread per connection so no client - however slow or silent - can block
// the ability to accept the *next* connection.
void acceptor_loop(int listen_fd, IScheduler* sched, const Args& args) {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
        if (fd < 0) {
            if (g_shutdown.load()) return;  // listen_fd closed for shutdown (S9)
            continue;                        // transient accept error - retry
        }
        g_active_admissions.fetch_add(1);  // before the thread starts, so shutdown can't miss it
        try {
            std::thread(admit_connection, fd, sched, std::cref(args)).detach();
        } catch (const std::system_error&) {
            // Out of threads: shed this connection with an ERR rather than
            // letting the exception abort the whole server.
            g_active_admissions.fetch_sub(1);
            send_err_and_close(fd, "server busy");
        }
    }
}

// server_threads workers, each purely a consumer of the shared queue - never
// touches accept() or header parsing. This is what actually satisfies A5:
// serving order is decided by whichever policy is active, not by who
// happened to accept a connection.
void server_worker_loop(IScheduler* sched, CsvWriter& csv, const Args& args) {
    while (true) {
        Request* to_serve = sched->next();  // blocks until available or shutdown
        if (!to_serve) return;              // shutdown drained the queue

        to_serve->rounds = 1;  // A23 (Stage-1 stub only, see TODO in Stage 2)

        std::string served_path = args.file_dir + "/" + to_serve->filename;
        bool ok = false;
        try {
            ok = stub_serve_whole(to_serve, to_serve->client_fd, served_path);
        } catch (const std::exception&) {
            // One bad request (e.g. bad_alloc) must not take down the worker
            // or the server; tell the client instead of closing silently (A24).
            send_all(to_serve->client_fd, format_err("internal error").data(),
                     format_err("internal error").size());
        }
        to_serve->finish_ns = now_monotonic_ns();

        if (ok) {
            csv.write_row(*to_serve);  // S7 - never called for HEALTH
            g_requests_served.fetch_add(1);
            g_bytes_served.fetch_add(to_serve->bytes);
        }
        // On failure stub_serve_whole has already replied ERR where it can
        // (missing file, unwritable destination); a dead connection needs no reply.

        close(to_serve->client_fd);
        delete to_serve;
    }
}

}  // namespace

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);
    Config cfg = load_config(args.config_path, /*require_load_balancer=*/false);

    CsvWriter csv(args.metrics_out);

    // TODO(K4, Stage 2): swap for make_scheduler(args.sched) once sjf/rr/drr
    // are real; the stub is FIFO/whole-file only and does not satisfy A5/A6.
    IScheduler* sched = make_stub_scheduler();

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    int listen_fd = make_listening_socket(cfg.server.ip, cfg.server.port);

    std::cerr << "server: CLI parsed OK (sched=" << args.sched
              << ", quantum=" << (args.has_quantum ? std::to_string(args.quantum) : "n/a")
              << ", file=" << args.file_dir
              << ", p=" << args.p_lines
              << ", config=" << args.config_path
              << ", metrics-out=" << args.metrics_out
              << "). Listening on " << cfg.server.ip << ":" << cfg.server.port
              << " with " << cfg.server.server_threads << " worker thread(s).\n";

    std::thread acceptor(acceptor_loop, listen_fd, sched, std::cref(args));

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(cfg.server.server_threads));
    for (int i = 0; i < cfg.server.server_threads; ++i) {
        workers.emplace_back(server_worker_loop, sched, std::ref(csv), std::cref(args));
    }

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // S9/A26: graceful shutdown. Order matters:
    //   1. stop accepting (wakes the acceptor out of accept()) and join it;
    //   2. wait for every in-flight admission thread, so a request accepted
    //      just before the signal is still enqueued rather than dropped;
    //   3. only then tell the scheduler to drain, and join the workers.
    shutdown(listen_fd, SHUT_RDWR);
    acceptor.join();
    close(listen_fd);
    while (g_active_admissions.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    sched->shutdown();
    for (auto& t : workers) t.join();

    std::cerr << "server: shutting down. requests_served=" << g_requests_served.load()
              << " bytes_served=" << g_bytes_served.load() << "\n";

    csv.close();
    delete sched;
    return 0;
}