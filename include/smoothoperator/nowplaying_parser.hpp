#pragma once

#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace smoothoperator::drivers {

// Parses the output of the Liquidsoap "jax.nowplaying" command ("artist|title").
// Uses the first line that is not END (\r stripped). Splits on the first '|';
// without '|', the whole line is the title. Missing fields fall back to defaults.
inline nlohmann::json parse_nowplaying(const std::string& data) {
    nlohmann::json meta = nlohmann::json::object();
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "END") break;
        if (line.empty()) continue;
        auto bar = line.find('|');
        if (bar == std::string::npos) {
            meta["title"] = line;
        } else {
            std::string artist = line.substr(0, bar);
            std::string title = line.substr(bar + 1);
            if (!artist.empty()) meta["artist"] = artist;
            if (!title.empty()) meta["title"] = title;
        }
        break;
    }
    if (!meta.contains("title")) meta["title"] = "Unknown";
    if (!meta.contains("artist")) meta["artist"] = "Unknown";
    meta["duration"] = 0.0;
    meta["playlist"] = "default";
    return meta;
}

} // namespace smoothoperator::drivers
