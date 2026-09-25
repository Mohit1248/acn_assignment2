#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

// config.json schema, locked in Phase 0. Every field listed here is
// required by the spec; load_config() enforces that.

struct ServerConfig {
    std::string ip;
    uint16_t port = 0;
    int server_threads = 0;  // worker pool size
    int client_threads = 0;  // concurrency used by the `load` experiment driver
};

struct BackendConfig {
    std::string ip;
    uint16_t port = 0;
};

struct LoadBalancerConfig {
    std::string ip;
    uint16_t port = 0;
    int health_interval_ms = 0;
    std::vector<BackendConfig> backends;  // exactly 4 entries
};

struct Config {
    ServerConfig server;
    LoadBalancerConfig load_balancer;
    bool has_load_balancer = false;  // true when load_balancer was parsed/validated
};

// Parses and validates `path`. Part A only needs the `server` section
// (require_load_balancer = false); Part B needs both (true).
//
// On any problem - missing field, wrong-typed field, malformed JSON, or a
// backends[] array that isn't exactly 4 entries - prints one line to stderr
// naming the offending field and calls exit(1). Never returns on failure,
// e.g.:
//   error: missing required field 'server.port'
//   error: wrong type for field 'server.server_threads'
//   error: malformed JSON: <parser detail>
Config load_config(const std::string& path, bool require_load_balancer);

#endif  // COMMON_CONFIG_H
