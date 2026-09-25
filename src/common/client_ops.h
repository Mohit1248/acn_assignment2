#ifndef COMMON_CLIENT_OPS_H
#define COMMON_CLIENT_OPS_H

#include <cstdint>
#include <string>

// Shared by src/client/main.cpp (C1) and src/client/load.cpp (C3), so the load
// driver reuses the exact same exchange code a plain `put`/`get` uses.
// Implementation owner: Mohit, Stage 1 (C2).

struct ExchangeResult {
    bool ok = false;
    uint64_t bytes = 0;  // bytes transferred on success
    std::string error;   // set when !ok
};

// Connects to ip:port, sends "PUT <basename(local_path)> <bytes>\n" then
// the file's exact bytes, and waits for both OK confirmations (declare ->
// OK -> body -> OK, per the wire protocol section). Sends only the file's
// base name on the wire (A3) regardless of local_path's directory.
ExchangeResult put_file(const std::string& ip, uint16_t port, const std::string& local_path);

// Connects to ip:port, sends "GET <name>\n", reads "OK <n>\n" then exactly
// n bytes, and writes them to out_path under the current directory (A3).
ExchangeResult get_file(const std::string& ip, uint16_t port, const std::string& name,
                         const std::string& out_path);

#endif  // COMMON_CLIENT_OPS_H
