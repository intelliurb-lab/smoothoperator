#include "smoothoperator/state_manager.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace smoothoperator::core {

StateManager::StateManager(std::shared_ptr<StreamProvider> stream_provider, 
                           std::shared_ptr<EventBus> event_bus,
                           std::string state_routing_key,
                           config::CommandsConfig commands,
                           config::IntentsConfig intents)
    : stream_provider_(std::move(stream_provider)), 
      event_bus_(std::move(event_bus)),
      state_routing_key_(std::move(state_routing_key)),
      commands_(std::move(commands)),
      intents_(std::move(intents)) {}

void StateManager::poll() {
    update_status();
    update_metadata();
}

void StateManager::update_status() {
    auto res = stream_provider_->execute(commands_.uptime);
    std::string new_status = std::holds_alternative<std::string>(res) ? "playing" : "error";
    
    if (new_status != state_.status) {
        state_.status = new_status;
        broadcast_state();
    }
}

void StateManager::update_metadata() {
    auto meta_res = stream_provider_->get_metadata();
    if (std::holds_alternative<std::error_code>(meta_res)) return;

    auto& meta = std::get<nlohmann::json>(meta_res);
    
    // Safety check: if meta is not an object, .value() will throw
    if (!meta.is_object()) {
        return;
    }

    bool changed = false;

    // Check for track changes and enrich with temporal data
    std::string title = meta.value("title", "Unknown");
    if (title != state_.track.title) {
        std::cout << "[Debug] New track detected: " << title << std::endl;
        state_.track.title = title;
        state_.track.artist = meta.value("artist", "Unknown");
        state_.track.playlist = meta.value("playlist", "default");
        state_.track.duration = meta.value("duration", 0.0);
        
        // Enrichment: start_time, end_time
        state_.track.start_time = get_current_utc_time();

        auto now = std::chrono::system_clock::now();
        auto end_time = now + std::chrono::milliseconds(static_cast<long long>(state_.track.duration * 1000.0));
        auto tt = std::chrono::system_clock::to_time_t(end_time);
        struct tm tm_buf{};
        gmtime_r(&tt, &tm_buf);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
        state_.track.end_time = ss.str();
        
        changed = true;
    }

    if (changed) {
        broadcast_state();
    }
}

void StateManager::handle_dj_command(const std::string& intent, const nlohmann::json& payload) {
    std::cout << "[Core] Handling intent: " << intent << std::endl;

    // Support both direct payload and wrapped in {"payload": {...}}
    const nlohmann::json& data = payload.contains("payload") ? payload.at("payload") : payload;

    if (intent == intents_.set_playlist) {
        std::string uri = data.at("uri");
        auto res1 = stream_provider_->execute(commands_.playlist_set_uri + " " + uri);
        if (std::holds_alternative<std::error_code>(res1)) {
            std::cerr << "[Core] Failed to set playlist URI: " << std::get<std::error_code>(res1).message() << std::endl;
            return;
        }
        auto res2 = stream_provider_->execute(commands_.playlist_reload);
        if (std::holds_alternative<std::error_code>(res2)) {
            std::cerr << "[Core] Failed to reload playlist: " << std::get<std::error_code>(res2).message() << std::endl;
        }
    } else if (intent == intents_.push_audio) {
        std::string path = data.at("path");
        auto res = stream_provider_->execute(commands_.push_audio + " " + path);
        if (std::holds_alternative<std::error_code>(res)) {
            std::cerr << "[Core] Failed to push audio: " << std::get<std::error_code>(res).message() << std::endl;
        }
    } else if (intent == intents_.skip) {
        auto res = stream_provider_->execute(commands_.skip);
        if (std::holds_alternative<std::error_code>(res)) {
            std::cerr << "[Core] Failed to skip: " << std::get<std::error_code>(res).message() << std::endl;
        }
    } else if (intent == intents_.status_request) {
        broadcast_state();
    }
}

void StateManager::broadcast_state() {
    event_bus_->publish(state_routing_key_, get_state_json());
}

nlohmann::json StateManager::get_state_json() const {
    nlohmann::json j;
    j["status"] = state_.status;
    j["track"]["artist"] = state_.track.artist;
    j["track"]["title"] = state_.track.title;
    j["track"]["playlist"] = state_.track.playlist;
    j["track"]["duration"] = state_.track.duration;
    j["track"]["start_time"] = state_.track.start_time;
    j["track"]["end_time"] = state_.track.end_time;
    j["volume"] = state_.volume;
    return j;
}

std::string StateManager::get_current_utc_time() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf{};
    gmtime_r(&in_time_t, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

} // namespace smoothoperator::core
