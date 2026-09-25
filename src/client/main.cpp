#include <cstdlib>
#include <iostream>
#include <string>

#include "../common/client_ops.h"
#include "../common/config.h"
#include "load.h"

// CLI dispatch (C1, A2/A3) - Stage 1, Mohit. Argument parsing/validation is
// filled in here now; the actual exchanges are TODO in client_ops.cpp (C2)
// and load.cpp (C3).

namespace {

[[noreturn]] void usage_error(const std::string& msg) {
    std::cerr << "error: " << msg << "\n";
    std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage_error("usage: client <put <local-path>|get <name>|load <workload-dir> --requests N> [--config <path>]");
    }

    std::string op = argv[1];
    std::string target = argv[2];
    std::string config_path = "config.json";
    bool has_requests = false;
    uint64_t requests = 0;

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next_val = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) usage_error(std::string("missing value for ") + flag);
            return argv[++i];
        };
        if (a == "--config") {
            config_path = next_val("--config");
        } else if (a == "--requests") {
            std::string v = next_val("--requests");
            char* end = nullptr;
            requests = std::strtoull(v.c_str(), &end, 10);
            if (end == v.c_str() || *end != '\0') usage_error("--requests must be a non-negative integer");
            has_requests = true;
        } else {
            usage_error("unknown flag '" + a + "'");
        }
    }

    if (op == "load" && !has_requests) usage_error("--requests is required for load");
    if (op != "load" && has_requests) usage_error("--requests is only valid with load");

    Config cfg = load_config(config_path, /*require_load_balancer=*/false);

    if (op == "put") {
        ExchangeResult r = put_file(cfg.server.ip, cfg.server.port, target);
        if (!r.ok) {
            std::cerr << "put failed: " << r.error << "\n";
            return 1;
        }
        return 0;
    }
    if (op == "get") {
        ExchangeResult r = get_file(cfg.server.ip, cfg.server.port, target, target);
        if (!r.ok) {
            std::cerr << "get failed: " << r.error << "\n";
            return 1;
        }
        return 0;
    }
    if (op == "load") {
        run_load(cfg.server.ip, cfg.server.port, target, requests, cfg.server.client_threads);
        return 0;
    }

    usage_error("unknown operation '" + op + "' (expected put, get, or load)");
}
