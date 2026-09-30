#ifndef COMMON_REQUEST_H
#define COMMON_REQUEST_H

#include <cstdint>
#include <string>
#include <vector>

struct Request {
    enum class Op { GET, PUT };

    uint64_t id = 0;
    Op op = Op::GET;
    std::string filename;

    uint64_t bytes = 0;

    uint64_t arrival_ns = 0;
    uint64_t start_ns = 0;
    uint64_t finish_ns = 0;

    int rounds = 0;
    uint64_t forfeited_bytes = 0;
    uint64_t deficit = 0;

    uint64_t byte_offset = 0;
    std::vector<char> leftover;

    int client_fd = -1;

    std::string path;
    std::string tmp_path;
    int file_fd = -1;
    bool started = false;
};

inline const char* op_to_string(Request::Op op) {
    return op == Request::Op::GET ? "GET" : "PUT";
}

#endif
