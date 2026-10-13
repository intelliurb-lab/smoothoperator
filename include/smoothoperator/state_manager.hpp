#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <mutex>
#include "smoothoperator/core/interfaces.hpp"
#include "smoothoperator/config.hpp"
#include <nlohmann/json.hpp>

namespace smoothoperator::core {

// True if a Liquidsoap status response means the stream is playing:
// trimmed response equals "on" or contains "playing".
inline bool is_playing_response(const std::string& response) {
    std::string r = response;
    auto end_pos = r.find("\nEND");
    if (end_pos != std::string::npos) r = r.substr(0, end_pos);
    if (r.size() >= 3 && r.compare(r.size() - 3, 3, "END") == 0 && r.find_first_not_of(" \t\r\n") == r.size() - 3) r.clear();
    const char* ws = " \t\r\n";
    auto b = r.find_first_not_of(ws);
    if (b == std::string::npos) return false;
    auto e = r.find_last_not_of(ws);
    r = r.substr(b, e - b + 1);
    return r == "on" || r.find("playing") != std::string::npos;
}

struct TrackInfo {
    std::string artist;
    std::string title;
    std::string playlist;
    double duration = 0.0;
    std::string start_time;
    std::string end_time;
};

struct GlobalState {
    std::string status = "stopped";
    TrackInfo track;
    int volume = 100;
};

class StateManager {
public:
    StateManager(std::shared_ptr<StreamProvider> stream_provider, 
                 std::shared_ptr<EventBus> event_bus,
                 std::string state_routing_key,
                 config::CommandsConfig commands,
                 config::IntentsConfig intents);

    void poll();
    
    // Commands (Intents)
    void handle_dj_command(const std::string& intent, const nlohmann::json& payload);
    
    nlohmann::json get_state_json() const;

private:
    std::shared_ptr<StreamProvider> stream_provider_;
    std::shared_ptr<EventBus> event_bus_;
    std::string state_routing_key_;
    config::CommandsConfig commands_;
    config::IntentsConfig intents_;
    mutable std::mutex state_mutex_;
    GlobalState state_;

    void update_metadata();
    void update_status();
    void broadcast_state();
    nlohmann::json get_state_json_unlocked() const;
    std::string get_current_utc_time();
};

} // namespace smoothoperator::core
