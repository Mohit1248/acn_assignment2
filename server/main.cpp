#include <atomic>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>

#include "../common/config.h"
#include "../common/csv_writer.h"
#include "scheduler.h"
#include "stub_scheduler.h"

// CLI parsing + validation (A1) - Stage 1, S1. The accept loop / worker
// pool / signal-driven shutdown below are TODO markers for S2/S3/S6/S9;
// this file compiles and runs today (prints its parsed config and exits)
// so both Stage 1 tracks have something to build against immediately.

namespace {

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
            args.p_lines = std::atoi(next_val("--p").c_str());
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

    // TODO(S2, Stage 1): SO_REUSEADDR listening socket on cfg.server.ip:
    // cfg.server.port; accept() loop feeding a pool of cfg.server.server_threads
    // worker threads. Each worker: read_header_line() [S3, with a receive
    // timeout so a silent client can't pin it or block admission, A5] ->
    // parse_request_line() -> filename validation [S4, A25] -> sched->enqueue()
    // -> sched->next() -> stub_serve_whole() [Stage 1 only] or a serve_slice()
    // loop [Stage 2] -> csv.write_row() on completion [S7]. Errors -> ERR
    // <reason>, never a silent close [S8, A24].
    // HEALTH [S6, A5/B8] must be answered immediately, out of band of the
    // queue, with sched->queue_depth() - and must never reach the CSV.
    // TODO(S9, Stage 1): on g_shutdown (checked here or from the accept
    // loop), stop accepting, sched->shutdown(), join all workers, print an
    // aggregate summary, csv.close().

    std::cerr << "server: CLI parsed OK (sched=" << args.sched
              << ", quantum=" << (args.has_quantum ? std::to_string(args.quantum) : "n/a")
              << ", file=" << args.file_dir
              << ", p=" << args.p_lines
              << ", config=" << args.config_path
              << ", metrics-out=" << args.metrics_out
              << "). Listening on " << cfg.server.ip << ":" << cfg.server.port
              << " with " << cfg.server.server_threads << " worker thread(s)."
              << " Accept loop not yet implemented (Stage 1, S2/S3/S6/S9).\n";

    delete sched;
    return 0;
}
