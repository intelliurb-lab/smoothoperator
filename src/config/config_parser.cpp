#include "smoothoperator/config.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sstream>

namespace smoothoperator::config {

void load_env(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        setenv(key.c_str(), value.c_str(), 1);
    }
}

std::string get_env_or_throw(const char* key) {
    const char* val = std::getenv(key);
    if (!val) throw std::runtime_error(std::string("Missing environment variable: ") + key);
    return std::string(val);
}

std::optional<std::string> get_env_optional(const char* key) {
    const char* val = std::getenv(key);
    if (!val) return std::nullopt;
    return std::string(val);
}

AppConfig ConfigParser::load(const std::string& json_path, const std::string& env_path) {
    load_env(env_path);

    std::ifstream file(json_path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open config file: " + json_path);
    }

    nlohmann::json j;
    file >> j;

    AppConfig config;
    
    // RabbitMQ
    auto& r = j.at("rabbitmq");
    config.rabbitmq.host = r.at("host").get<std::string>();
    config.rabbitmq.port = r.at("port").get<uint16_t>();
    config.rabbitmq.user = r.at("user").get<std::string>();
    config.rabbitmq.password = get_env_or_throw("RABBITMQ_PASS");
    if (r.contains("vhost")) config.rabbitmq.vhost = r.at("vhost").get<std::string>();
    if (r.contains("queue_name")) config.rabbitmq.queue_name = r.at("queue_name").get<std::string>();
    if (r.contains("exchange_name")) config.rabbitmq.exchange_name = r.at("exchange_name").get<std::string>();
    if (r.contains("binding_key")) config.rabbitmq.binding_key = r.at("binding_key").get<std::string>();
    if (r.contains("state_routing_key")) config.rabbitmq.state_routing_key = r.at("state_routing_key").get<std::string>();

    // Liquidsoap
    auto& l = j.at("liquidsoap");
    config.liquidsoap.protocol = l.at("protocol").get<std::string>();
    if (config.liquidsoap.protocol == "telnet") {
        config.liquidsoap.host = l.at("host").get<std::string>();
        config.liquidsoap.port = l.at("port").get<uint16_t>();
    } else {
        config.liquidsoap.socket_path = l.at("socket_path").get<std::string>();
    }
    if (l.contains("polling_interval_ms")) {
        config.liquidsoap.polling_interval_ms = l.at("polling_interval_ms").get<uint32_t>();
    }

    // Commands
    if (j.contains("commands")) {
        auto& c = j.at("commands");
        if (c.contains("uptime")) config.commands.uptime = c.at("uptime").get<std::string>();
        if (c.contains("skip")) config.commands.skip = c.at("skip").get<std::string>();
        if (c.contains("playlist_reload")) config.commands.playlist_reload = c.at("playlist_reload").get<std::string>();
        if (c.contains("playlist_set_uri")) config.commands.playlist_set_uri = c.at("playlist_set_uri").get<std::string>();
        if (c.contains("push_audio")) config.commands.push_audio = c.at("push_audio").get<std::string>();
    }

    // Intents
    if (j.contains("intents")) {
        auto& i = j.at("intents");
        if (i.contains("set_playlist")) config.intents.set_playlist = i.at("set_playlist").get<std::string>();
        if (i.contains("push_audio")) config.intents.push_audio = i.at("push_audio").get<std::string>();
        if (i.contains("skip")) config.intents.skip = i.at("skip").get<std::string>();
        if (i.contains("status_request")) config.intents.status_request = i.at("status_request").get<std::string>();
    }

    // App
    if (j.contains("log_level")) config.log_level = j.at("log_level").get<std::string>();
    config.gemini_api_key = get_env_optional("GEMINI_API_KEY");

    return config;
}

} // namespace smoothoperator::config
