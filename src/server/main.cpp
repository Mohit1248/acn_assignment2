#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/tcp.h>
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

namespace {

constexpr int kHeaderTimeoutMs = 5000;

constexpr uint64_t kMaxPutBytes = 1ULL << 30;

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
            if (end == v.c_str() || *end != '\0' || q == 0 || v[0] == '-') {
                usage_error("--quantum must be a positive integer (bytes per round)");
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

    struct stat dir_st{};
    if (::stat(args.file_dir.c_str(), &dir_st) != 0 || !S_ISDIR(dir_st.st_mode)) {
        usage_error("--file '" + args.file_dir + "' is not an existing directory");
    }

    return args;
}

std::atomic<bool> g_shutdown{false};
void on_signal(int) { g_shutdown.store(true); }

std::atomic<uint64_t> g_next_id{1};
std::atomic<uint64_t> g_requests_served{0};
std::atomic<uint64_t> g_bytes_served{0};

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
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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
    send_all(fd, err.data(), err.size());
    close(fd);
}

void admit_connection(int fd, IScheduler* sched, const Args& args) {
    AdmissionGuard guard;

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    struct timeval snd_timeout{};
    snd_timeout.tv_sec = 10;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd_timeout, sizeof(snd_timeout));

    HeaderReadResult hdr = read_header_line(fd, kHeaderTimeoutMs);
    if (!hdr.ok) {
        send_err_and_close(fd, hdr.error.empty() ? "header read timeout" : hdr.error);
        return;
    }

    ParsedRequestLine parsed = parse_request_line(hdr.line);
    if (parsed.type == ReqType::MALFORMED) {
        send_err_and_close(fd, parsed.error);
        return;
    }

    if (parsed.type == ReqType::HEALTH) {
        std::string resp = format_ok(sched->queue_depth());
        send_all(fd, resp.data(), resp.size());
        close(fd);
        return;
    }

    if (!is_filename_safe(parsed.filename)) {
        send_err_and_close(fd, "unsafe filename");
        return;
    }
    if (parsed.type == ReqType::PUT && parsed.byte_count > kMaxPutBytes) {
        send_err_and_close(fd, "declared size too large");
        return;
    }
    std::string full_path = args.file_dir + "/" + parsed.filename;

    auto* req = new Request();
    req->id = g_next_id.fetch_add(1);
    req->client_fd = fd;
    req->filename = parsed.filename;
    req->path = full_path;
    req->leftover = std::move(hdr.leftover);
    req->arrival_ns = now_monotonic_ns();

    if (parsed.type == ReqType::GET) {
        req->op = Request::Op::GET;
        int ffd = ::open(full_path.c_str(), O_RDONLY);
        struct stat st{};
        if (ffd < 0 || ::fstat(ffd, &st) != 0 || !S_ISREG(st.st_mode)) {
            if (ffd >= 0) ::close(ffd);
            send_err_and_close(fd, "file not found");
            delete req;
            return;
        }
        req->file_fd = ffd;
        req->bytes = static_cast<uint64_t>(st.st_size);
    } else {
        req->op = Request::Op::PUT;
        req->bytes = parsed.byte_count;
    }

    sched->enqueue(req);
}

void acceptor_loop(int listen_fd, IScheduler* sched, const Args& args) {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
        if (fd < 0) {
            if (g_shutdown.load()) return;
            continue;
        }
        g_active_admissions.fetch_add(1);
        try {
            std::thread(admit_connection, fd, sched, std::cref(args)).detach();
        } catch (const std::system_error&) {
            g_active_admissions.fetch_sub(1);
            send_err_and_close(fd, "server busy");
        }
    }
}

void server_worker_loop(IScheduler* sched, CsvWriter& csv, const SliceParams& sp) {
    while (true) {
        Request* r = sched->next();
        if (!r) return;

        SliceResult res = SliceResult::FAILED;
        try {
            res = serve_slice(r, r->client_fd, sp);
        } catch (const std::exception&) {
            std::string e = format_err("internal error");
            send_all(r->client_fd, e.data(), e.size());
        }

        if (res == SliceResult::PREEMPTED) {
            sched->requeue(r);
            continue;
        }

        r->finish_ns = now_monotonic_ns();
        if (res == SliceResult::DONE) {
            csv.write_row(*r);
            g_requests_served.fetch_add(1);
            g_bytes_served.fetch_add(r->bytes);
        }

        release_request(r);
        close(r->client_fd);
        delete r;
    }
}

}

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);
    Config cfg = load_config(args.config_path, false);

    CsvWriter csv(args.metrics_out);

    IScheduler* sched = make_scheduler(args.sched);

    SliceParams slice_params;
    slice_params.policy = parse_policy(args.sched);
    slice_params.quantum = args.quantum;
    slice_params.p_lines = args.p_lines;

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
        workers.emplace_back(server_worker_loop, sched, std::ref(csv), std::cref(slice_params));
    }

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    shutdown(listen_fd, SHUT_RDWR);
    acceptor.join();
    close(listen_fd);
    while (g_active_admissions.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    sched->shutdown();
    for (auto& t : workers) t.join();

    std::cerr << "server: shutting down. requests_served=" << g_requests_served.load()
              << " bytes_served=" << g_bytes_served.load()
              << " send_calls=" << send_call_count() << "\n";

    csv.close();
    delete sched;
    return 0;
}
