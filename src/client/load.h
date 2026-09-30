#ifndef CLIENT_LOAD_H
#define CLIENT_LOAD_H

#include <cstdint>
#include <string>

bool run_load(const std::string& server_ip, uint16_t server_port, const std::string& workload_dir,
              uint64_t n_requests, int client_threads);

#endif
