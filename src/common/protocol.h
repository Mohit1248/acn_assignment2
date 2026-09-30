#ifndef COMMON_PROTOCOL_H
#define COMMON_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

struct HeaderReadResult {
    bool ok = false;
    std::string line;
    std::vector<char> leftover;
    std::string error;
};

HeaderReadResult read_header_line(int fd, int timeout_ms);

bool read_exact(int fd, std::vector<char>& leftover, char* out, size_t n, int total_timeout_ms = -1);

bool send_all(int fd, const char* data, size_t n);

uint64_t send_call_count();

enum class ReqType { GET, PUT, HEALTH, MALFORMED };

struct ParsedRequestLine {
    ReqType type = ReqType::MALFORMED;
    std::string filename;
    uint64_t byte_count = 0;
    std::string error;
};

ParsedRequestLine parse_request_line(const std::string& line);

std::string format_ok(uint64_t n);
std::string format_err(const std::string& reason);

struct ParsedResponseLine {
    bool ok = false;
    bool malformed = false;
    uint64_t n = 0;
    std::string reason;
};

ParsedResponseLine parse_response_line(const std::string& line);

bool is_filename_safe(const std::string& name);

#endif
