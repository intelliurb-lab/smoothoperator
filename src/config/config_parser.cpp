#include "smoothoperator/config.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sstream>

namespace smoothoperator::config {

void load_env(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        // Only warn if default .env is missing, but error if a specific path was expected?
        // For now, let's keep it optional but report if it's there and malformed.
        return;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            throw std::runtime_error("Malformed line in " + path + ":" + std::to_string(line_num) + " - Missing '='");
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        // Basic trim
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        
        setenv(key.c_str(), value.c_str(), 1);
    }
}

std::string get_env_or_throw(const char* key, const std::string& env_path) {
    const char* val = std::getenv(key);
    if (!val) {
        throw std::runtime_error("Missing mandatory environment variable: " + std::string(key) + 
                                 " (Check your " + env_path + " file)");
    }
    return std::string(val);
}

std::optional<std::string> get_env_optional(const char* key) {
    const char* val = std::getenv(key);
    if (!val) return std::nullopt;
    return std::string(val);
}

AppConfig ConfigParser::load(const std::string& json_path, const std::string& env_path) {
    try {
        load_env(env_path);
    } catch (const std::exception& e) {
        throw std::runtime_error("Environment error: " + std::string(e.what()));
    }

    std::ifstream file(json_path);
    if (!file.is_open()) {
        throw std::runtime_error("Configuration error: Could not open file '" + json_path + "'");
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Configuration error: Invalid JSON in '" + json_path + "' at byte " + std::to_string(e.byte) + ": " + e.what());
    }

    AppConfig config;
    
    try {
        // RabbitMQ
        if (!j.contains("rabbitmq") || !j.at("rabbitmq").is_object()) {
            throw std::runtime_error("Missing or invalid 'rabbitmq' section");
        }
        auto& r = j.at("rabbitmq");
        
        if (!r.contains("host")) throw std::runtime_error("Missing 'rabbitmq.host'");
        config.rabbitmq.host = r.at("host").get<std::string>();
        
        if (!r.contains("port")) throw std::runtime_error("Missing 'rabbitmq.port'");
        config.rabbitmq.port = r.at("port").get<uint16_t>();
        
        if (!r.contains("user")) throw std::runtime_error("Missing 'rabbitmq.user'");
        config.rabbitmq.user = r.at("user").get<std::string>();
        
        // Priority: JSON password > JSON pass > .env RABBITMQ_PASS
        if (r.contains("password") && !r.at("password").is_null()) {
            config.rabbitmq.password = r.at("password").get<std::string>();
        } else if (r.contains("pass") && !r.at("pass").is_null()) {
            config.rabbitmq.password = r.at("pass").get<std::string>();
        } else {
            config.rabbitmq.password = get_env_or_throw("RABBITMQ_PASS", env_path);
        }
        
        if (r.contains("vhost") && !r.at("vhost").is_null()) config.rabbitmq.vhost = r.at("vhost").get<std::string>();
        if (r.contains("queue_name") && !r.at("queue_name").is_null()) config.rabbitmq.queue_name = r.at("queue_name").get<std::string>();
        if (r.contains("exchange_name") && !r.at("exchange_name").is_null()) config.rabbitmq.exchange_name = r.at("exchange_name").get<std::string>();
        if (r.contains("binding_key") && !r.at("binding_key").is_null()) config.rabbitmq.binding_key = r.at("binding_key").get<std::string>();
        if (r.contains("state_routing_key") && !r.at("state_routing_key").is_null()) config.rabbitmq.state_routing_key = r.at("state_routing_key").get<std::string>();

        // Liquidsoap
        if (!j.contains("liquidsoap") || !j.at("liquidsoap").is_object()) {
            throw std::runtime_error("Missing or invalid 'liquidsoap' section");
        }
        auto& l = j.at("liquidsoap");
        
        if (!l.contains("protocol")) throw std::runtime_error("Missing 'liquidsoap.protocol'");
        config.liquidsoap.protocol = l.at("protocol").get<std::string>();
        
        if (config.liquidsoap.protocol == "telnet") {
            if (!l.contains("host")) throw std::runtime_error("Missing 'liquidsoap.host' for telnet protocol");
            if (!l.contains("port")) throw std::runtime_error("Missing 'liquidsoap.port' for telnet protocol");
            config.liquidsoap.host = l.at("host").get<std::string>();
            config.liquidsoap.port = l.at("port").get<uint16_t>();
        } else {
            throw std::runtime_error("Unsupported liquidsoap.protocol: '" + config.liquidsoap.protocol + "'. Only 'telnet' is currently implemented.");
        }
        
        if (l.contains("polling_interval_ms") && !l.at("polling_interval_ms").is_null()) {
            config.liquidsoap.polling_interval_ms = l.at("polling_interval_ms").get<uint32_t>();
        }

        // Commands
        if (j.contains("commands") && j.at("commands").is_object()) {
            auto& c = j.at("commands");
            if (c.contains("uptime") && !c.at("uptime").is_null()) config.commands.uptime = c.at("uptime").get<std::string>();
            if (c.contains("skip") && !c.at("skip").is_null()) config.commands.skip = c.at("skip").get<std::string>();
            if (c.contains("playlist_reload") && !c.at("playlist_reload").is_null()) config.commands.playlist_reload = c.at("playlist_reload").get<std::string>();
            if (c.contains("playlist_set_uri") && !c.at("playlist_set_uri").is_null()) config.commands.playlist_set_uri = c.at("playlist_set_uri").get<std::string>();
            if (c.contains("push_audio") && !c.at("push_audio").is_null()) config.commands.push_audio = c.at("push_audio").get<std::string>();
        }

        // Intents
        if (j.contains("intents") && j.at("intents").is_object()) {
            auto& i = j.at("intents");
            if (i.contains("set_playlist") && !i.at("set_playlist").is_null()) config.intents.set_playlist = i.at("set_playlist").get<std::string>();
            if (i.contains("push_audio") && !i.at("push_audio").is_null()) config.intents.push_audio = i.at("push_audio").get<std::string>();
            if (i.contains("skip") && !i.at("skip").is_null()) config.intents.skip = i.at("skip").get<std::string>();
            if (i.contains("status_request") && !i.at("status_request").is_null()) config.intents.status_request = i.at("status_request").get<std::string>();
        }

        // App
        if (j.contains("log_level") && !j.at("log_level").is_null()) config.log_level = j.at("log_level").get<std::string>();
        config.gemini_api_key = get_env_optional("GEMINI_API_KEY");

    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Configuration error: Type mismatch or JSON error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw std::runtime_error("Configuration error: " + std::string(e.what()));
    }

    return config;
}

} // namespace smoothoperator::config
