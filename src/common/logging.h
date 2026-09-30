#ifndef COMMON_LOGGING_H
#define COMMON_LOGGING_H

#include <cstdint>
#include <cstdio>
#include <string>

inline void log_a14_fire(uint64_t request_id, const std::string& filename,
                          uint64_t line_bytes, uint64_t quantum) {
    std::string line = "A14 request_id=" + std::to_string(request_id) +
                       " filename=" + filename +
                       " line_bytes=" + std::to_string(line_bytes) +
                       " quantum=" + std::to_string(quantum) + "\n";
    std::fputs(line.c_str(), stderr);
}

#endif
