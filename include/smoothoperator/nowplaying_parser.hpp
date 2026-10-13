#pragma once

#include <nlohmann/json.hpp>
#include <cmath>
#include <sstream>
#include <string>

namespace smoothoperator::drivers {

// Parses the output of the Liquidsoap "jax.nowplaying" command ("artist|title").
// Uses the first line that is not END (\r stripped). Splits on the first '|';
// without '|', the whole line is the title. Missing fields fall back to defaults.
// Parses a duration in seconds; empty, invalid, negative or non-finite -> 0.0.
inline double parse_duration(const std::string& text) {
    if (text.empty()) return 0.0;
    try {
        size_t used = 0;
        double d = std::stod(text, &used);
        if (used != text.size() || !std::isfinite(d) || d < 0.0) return 0.0;
        return d;
    } catch (...) {
        return 0.0;
    }
}

// Format: "artist|title|duration" (duration optional; title may contain '|'
// when 3 fields are present, the last '|' separates the duration).
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
            std::string rest = line.substr(bar + 1);
            std::string title = rest;
            auto bar2 = rest.rfind('|');
            if (bar2 != std::string::npos) {
                title = rest.substr(0, bar2);
                meta["duration"] = parse_duration(rest.substr(bar2 + 1));
            }
            if (!artist.empty()) meta["artist"] = artist;
            if (!title.empty()) meta["title"] = title;
        }
        break;
    }
    if (!meta.contains("title")) meta["title"] = "Unknown";
    if (!meta.contains("artist")) meta["artist"] = "Unknown";
    if (!meta.contains("duration")) meta["duration"] = 0.0;
    meta["playlist"] = "default";
    return meta;
}

} // namespace smoothoperator::drivers
