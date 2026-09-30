#ifndef COMMON_CLIENT_OPS_H
#define COMMON_CLIENT_OPS_H

#include <cstdint>
#include <string>

struct ExchangeResult {
    bool ok = false;
    uint64_t bytes = 0;
    std::string error;
};

ExchangeResult put_file(const std::string& ip, uint16_t port, const std::string& local_path);

ExchangeResult get_file(const std::string& ip, uint16_t port, const std::string& name,
                         const std::string& out_path);

#endif
