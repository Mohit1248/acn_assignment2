#ifndef CLIENT_LOAD_H
#define CLIENT_LOAD_H

#include <cstdint>
#include <string>

// TODO(C3, Stage 1, Mohit): the experiment driver (A4).
//   1. Seed the server once: PUT every file in `workload_dir` (uncounted in
//      any reported metric).
//   2. Issue `n_requests` from a shared atomic counter across
//      `client_threads` concurrent threads. Each request picks a workload
//      file uniformly at random and, with equal probability, GETs its base
//      name or PUTs it again. Closed-loop: each thread waits for the full
//      response before issuing its next request.
//   3. Report nothing itself - all metrics come from the server's CSV
//      (A23); this function's job is purely to generate the load. Returns
//      false if the workload directory is unusable or any request failed, so
//      the client can exit non-zero.
bool run_load(const std::string& server_ip, uint16_t server_port, const std::string& workload_dir,
              uint64_t n_requests, int client_threads);

#endif  // CLIENT_LOAD_H
