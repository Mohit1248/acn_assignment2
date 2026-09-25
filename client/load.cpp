#include "load.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "../common/client_ops.h"

// C3 (Mohit, Stage 1): experiment driver (A4). Seeds the server once, then
// generates N closed-loop requests from client_threads concurrent threads.

namespace fs = std::filesystem;

namespace {

std::vector<std::string> list_workload_files(const std::string& dir) {
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());  // deterministic seeding order
    return files;
}

void worker(const std::string& ip, uint16_t port, const std::vector<std::string>& files,
            std::atomic<uint64_t>* counter, uint64_t n_requests, int thread_index) {
    std::mt19937_64 rng(std::random_device{}() ^ (static_cast<uint64_t>(thread_index) << 32));
    std::uniform_int_distribution<size_t> file_pick(0, files.size() - 1);
    std::uniform_int_distribution<int> coin(0, 1);

    while (true) {
        uint64_t idx = counter->fetch_add(1);
        if (idx >= n_requests) break;

        const std::string& path = files[file_pick(rng)];
        std::string name = fs::path(path).filename().string();

        // Closed-loop: get_file/put_file block for the full exchange before
        // this thread claims its next request (A4).
        ExchangeResult r;
        if (coin(rng) == 0) {
            // Discard the body during load generation - `load` only needs
            // to generate traffic and let the server measure it (A4: "the
            // client reports nothing"); `client get` (not `load`) is what
            // actually saves a file for a user.
            r = get_file(ip, port, name, "/dev/null");
        } else {
            r = put_file(ip, port, path);
        }
        if (!r.ok) {
            std::cerr << "load: request failed: " << r.error << "\n";
        }
    }
}

}  // namespace

void run_load(const std::string& server_ip, uint16_t server_port, const std::string& workload_dir,
              uint64_t n_requests, int client_threads) {
    std::vector<std::string> files = list_workload_files(workload_dir);
    if (files.empty()) {
        std::cerr << "load: workload directory '" << workload_dir << "' has no files\n";
        return;
    }

    // Seed: PUT every workload file once, sequentially, before any
    // concurrent load requests begin (A4: "seeding requests are not
    // counted in any reported metric"). The server's CSV logs every
    // completed request unconditionally (A23), so these rows DO appear in
    // it; because seeding fully completes (closed-loop, single connection
    // at a time) before the first load-generated request is even admitted,
    // analysis scripts can reliably exclude seeding by sorting the CSV by
    // arrival_ns/request_id and dropping the first files.size() rows.
    for (const auto& path : files) {
        ExchangeResult r = put_file(server_ip, server_port, path);
        if (!r.ok) {
            std::cerr << "load: seeding failed for '" << path << "': " << r.error << "\n";
        }
    }

    std::atomic<uint64_t> counter{0};
    int n_threads = client_threads > 0 ? client_threads : 1;
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(n_threads));
    for (int t = 0; t < n_threads; ++t) {
        threads.emplace_back(worker, server_ip, server_port, std::cref(files), &counter, n_requests, t);
    }
    for (auto& th : threads) th.join();
}
