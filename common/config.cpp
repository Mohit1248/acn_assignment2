#include "config.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "third_party/json.hpp"

using json = nlohmann::json;

namespace {

[[noreturn]] void fail_missing(const std::string& field) {
    std::cerr << "error: missing required field '" << field << "'\n";
    std::exit(1);
}

[[noreturn]] void fail_type(const std::string& field) {
    std::cerr << "error: wrong type for field '" << field << "'\n";
    std::exit(1);
}

[[noreturn]] void fail_custom(const std::string& field, const std::string& detail) {
    std::cerr << "error: " << detail << " for field '" << field << "'\n";
    std::exit(1);
}

[[noreturn]] void fail_json(const std::string& detail) {
    std::cerr << "error: malformed JSON: " << detail << "\n";
    std::exit(1);
}

const json& require_obj(const json& parent, const char* key, const char* path) {
    if (!parent.contains(key)) fail_missing(path);
    const json& v = parent.at(key);
    if (!v.is_object()) fail_type(path);
    return v;
}

std::string require_string(const json& obj, const char* key, const char* path) {
    if (!obj.contains(key)) fail_missing(path);
    const json& v = obj.at(key);
    if (!v.is_string()) fail_type(path);
    return v.get<std::string>();
}

uint16_t require_port(const json& obj, const char* key, const char* path) {
    if (!obj.contains(key)) fail_missing(path);
    const json& v = obj.at(key);
    if (!v.is_number_integer()) fail_type(path);
    long long n = v.get<long long>();
    if (n < 0 || n > 65535) fail_custom(path, "port out of range");
    return static_cast<uint16_t>(n);
}

int require_int(const json& obj, const char* key, const char* path) {
    if (!obj.contains(key)) fail_missing(path);
    const json& v = obj.at(key);
    if (!v.is_number_integer()) fail_type(path);
    return v.get<int>();
}

}  // namespace

Config load_config(const std::string& path, bool require_load_balancer) {
    std::ifstream in(path);
    if (!in) {
        fail_json("cannot open '" + path + "'");
    }
    std::stringstream ss;
    ss << in.rdbuf();

    json root;
    try {
        root = json::parse(ss.str());
    } catch (const json::parse_error& e) {
        fail_json(e.what());
    }
    if (!root.is_object()) {
        fail_type("<root>");
    }

    Config cfg;
    const json& server = require_obj(root, "server", "server");
    cfg.server.ip = require_string(server, "ip", "server.ip");
    cfg.server.port = require_port(server, "port", "server.port");
    cfg.server.server_threads = require_int(server, "server_threads", "server.server_threads");
    cfg.server.client_threads = require_int(server, "client_threads", "server.client_threads");

    if (require_load_balancer) {
        const json& lb = require_obj(root, "load_balancer", "load_balancer");
        cfg.load_balancer.ip = require_string(lb, "ip", "load_balancer.ip");
        cfg.load_balancer.port = require_port(lb, "port", "load_balancer.port");
        cfg.load_balancer.health_interval_ms =
            require_int(lb, "health_interval_ms", "load_balancer.health_interval_ms");

        if (!lb.contains("backends")) fail_missing("load_balancer.backends");
        const json& backends = lb.at("backends");
        if (!backends.is_array()) fail_type("load_balancer.backends");
        if (backends.size() != 4) {
            fail_custom("load_balancer.backends", "must have exactly 4 entries");
        }

        for (size_t i = 0; i < backends.size(); ++i) {
            const json& b = backends[i];
            std::string p = "load_balancer.backends[" + std::to_string(i) + "]";
            if (!b.is_object()) fail_type(p);
            BackendConfig bc;
            bc.ip = require_string(b, "ip", (p + ".ip").c_str());
            bc.port = require_port(b, "port", (p + ".port").c_str());
            cfg.load_balancer.backends.push_back(bc);
        }
        cfg.has_load_balancer = true;
    }

    return cfg;
}
