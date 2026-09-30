#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

struct ServerConfig {
    std::string ip;
    uint16_t port = 0;
    int server_threads = 0;
    int client_threads = 0;
};

struct BackendConfig {
    std::string ip;
    uint16_t port = 0;
};

struct LoadBalancerConfig {
    std::string ip;
    uint16_t port = 0;
    int health_interval_ms = 0;
    std::vector<BackendConfig> backends;
};

struct Config {
    ServerConfig server;
    LoadBalancerConfig load_balancer;
    bool has_load_balancer = false;
};

Config load_config(const std::string& path, bool require_load_balancer);

#endif
