#include "client_ops.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <vector>

#include "protocol.h"

// C2 (Mohit, Stage 1): put/get over the framed protocol. One connection per
// call (framing rules: a connection carries exactly one request).

namespace {

constexpr int kResponseTimeoutMs = 10000;

int connect_to(const std::string& ip, uint16_t port, std::string& err) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = "socket() failed";
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        err = "invalid server ip '" + ip + "'";
        return -1;
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        err = "cannot connect to " + ip + ":" + std::to_string(port);
        return -1;
    }
    return fd;
}

std::string basename_of(const std::string& path) {
    size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

}  // namespace

ExchangeResult put_file(const std::string& ip, uint16_t port, const std::string& local_path) {
    std::ifstream in(local_path, std::ios::binary);
    if (!in) {
        return ExchangeResult{false, 0, "cannot open local file '" + local_path + "'"};
    }
    in.seekg(0, std::ios::end);
    uint64_t size = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<char> body(size);
    if (size > 0) in.read(body.data(), static_cast<std::streamsize>(size));

    std::string err;
    int fd = connect_to(ip, port, err);
    if (fd < 0) return ExchangeResult{false, 0, err};

    std::string header = "PUT " + basename_of(local_path) + " " + std::to_string(size) + "\n";
    if (!send_all(fd, header.data(), header.size())) {
        close(fd);
        return ExchangeResult{false, 0, "connection error sending PUT header"};
    }

    HeaderReadResult hdr1 = read_header_line(fd, kResponseTimeoutMs);
    if (!hdr1.ok) {
        close(fd);
        return ExchangeResult{false, 0, "no response to PUT header: " + hdr1.error};
    }
    ParsedResponseLine resp1 = parse_response_line(hdr1.line);
    if (resp1.malformed || !resp1.ok) {
        close(fd);
        return ExchangeResult{false, 0, resp1.malformed ? "malformed response" : resp1.reason};
    }

    if (size > 0 && !send_all(fd, body.data(), body.size())) {
        close(fd);
        return ExchangeResult{false, 0, "connection error sending body"};
    }

    HeaderReadResult hdr2 = read_header_line(fd, kResponseTimeoutMs);
    close(fd);
    if (!hdr2.ok) return ExchangeResult{false, 0, "no final confirmation: " + hdr2.error};
    ParsedResponseLine resp2 = parse_response_line(hdr2.line);
    if (resp2.malformed || !resp2.ok) {
        return ExchangeResult{false, 0, resp2.malformed ? "malformed response" : resp2.reason};
    }
    return ExchangeResult{true, size, ""};
}

ExchangeResult get_file(const std::string& ip, uint16_t port, const std::string& name,
                         const std::string& out_path) {
    std::string err;
    int fd = connect_to(ip, port, err);
    if (fd < 0) return ExchangeResult{false, 0, err};

    std::string header = "GET " + name + "\n";
    if (!send_all(fd, header.data(), header.size())) {
        close(fd);
        return ExchangeResult{false, 0, "connection error sending GET header"};
    }

    HeaderReadResult hdr = read_header_line(fd, kResponseTimeoutMs);
    if (!hdr.ok) {
        close(fd);
        return ExchangeResult{false, 0, "no response to GET: " + hdr.error};
    }
    ParsedResponseLine resp = parse_response_line(hdr.line);
    if (resp.malformed) {
        close(fd);
        return ExchangeResult{false, 0, "malformed response"};
    }
    if (!resp.ok) {
        close(fd);
        return ExchangeResult{false, 0, resp.reason};
    }

    uint64_t size = resp.n;
    std::vector<char> body(size);
    std::vector<char> leftover = std::move(hdr.leftover);
    if (size > 0 && !read_exact(fd, leftover, body.data(), size)) {
        close(fd);
        return ExchangeResult{false, 0, "connection closed before full body received"};
    }
    close(fd);

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) return ExchangeResult{false, 0, "cannot open output file '" + out_path + "'"};
    if (size > 0) out.write(body.data(), static_cast<std::streamsize>(size));
    return ExchangeResult{true, size, ""};
}
