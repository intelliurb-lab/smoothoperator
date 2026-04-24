#include "smoothoperator/liquidsoap_driver.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>

namespace smoothoperator::drivers {

TelnetLiquidsoapDriver::TelnetLiquidsoapDriver(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

TelnetLiquidsoapDriver::~TelnetLiquidsoapDriver() {
    disconnect();
}

void TelnetLiquidsoapDriver::apply_backoff_delay() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_connection_attempt_);

    // Only increase backoff if we've already waited minimum time
    if (elapsed.count() >= backoff_ms_) {
        backoff_ms_ = std::min(backoff_ms_ * 2, MAX_BACKOFF_MS);
    }

    last_connection_attempt_ = std::chrono::steady_clock::now();
}

std::error_code TelnetLiquidsoapDriver::connect() {
    if (socket_fd_ != -1) return {};

    apply_backoff_delay();

    struct addrinfo hints{}, *results = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port_);
    int ret = getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &results);
    if (ret != 0) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    for (struct addrinfo *p = results; p != nullptr; p = p->ai_next) {
        socket_fd_ = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (socket_fd_ < 0) continue;

        if (::connect(socket_fd_, p->ai_addr, p->ai_addrlen) == 0) {
            freeaddrinfo(results);
            backoff_ms_ = 100;
            return {};
        }

        close(socket_fd_);
        socket_fd_ = -1;
    }

    freeaddrinfo(results);
    return std::make_error_code(std::errc::connection_refused);
}

void TelnetLiquidsoapDriver::disconnect() {
    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

core::Result<std::string> TelnetLiquidsoapDriver::execute(const std::string& command) {
    auto err = connect();
    if (err) return err;

    std::string full_cmd = command + "\r\n";
    if (send(socket_fd_, full_cmd.c_str(), full_cmd.length(), MSG_NOSIGNAL) < 0) {
        disconnect();
        return std::make_error_code(std::errc::io_error);
    }

    const size_t MAX_RESPONSE_SIZE = 1024 * 1024;
    std::string response;
    char buffer[4096];
    while (true) {
        if (response.size() > MAX_RESPONSE_SIZE) {
            disconnect();
            return std::make_error_code(std::errc::message_size);
        }

        ssize_t n = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            disconnect();
            return std::make_error_code(std::errc::connection_aborted);
        }
        buffer[n] = '\0';
        response += buffer;
        if (response.find("END\r\n") != std::string::npos || response.find("END\n") != std::string::npos) {
            break;
        }
    }

    return response;
}

core::Result<nlohmann::json> TelnetLiquidsoapDriver::get_metadata() {
    auto res = execute("request.on_air");
    if (std::holds_alternative<std::error_code>(res)) return std::get<std::error_code>(res);

    nlohmann::json meta = nlohmann::json::object();
    std::string data = std::get<std::string>(res);

    // Robust line-based parser (handles key="value" format)
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (line == "END" || line == "END\r") break;

        // Look for pattern: key="value"
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);

        // Value starts after =" and ends at last "
        if (eq_pos + 1 >= line.length() || line[eq_pos + 1] != '"') continue;

        size_t val_start = eq_pos + 2;
        size_t val_end = line.rfind('"');

        if (val_end <= val_start) continue;

        std::string val = line.substr(val_start, val_end - val_start);

        // Remove trailing \r if present
        if (!val.empty() && val.back() == '\r') val.pop_back();

        // Convert numeric fields from string to proper types
        if (key == "duration") {
            try {
                meta[key] = std::stod(val);
            } catch (...) {
                meta[key] = 0.0;
            }
        } else {
            meta[key] = val;
        }
    }

    // Ensure mandatory fields for core with correct types
    if (!meta.contains("title")) meta["title"] = "Unknown";
    if (!meta.contains("artist")) meta["artist"] = "Unknown";
    if (!meta.contains("duration") || !meta["duration"].is_number()) meta["duration"] = 0.0;
    if (!meta.contains("playlist")) meta["playlist"] = "default";

    return meta;
}

} // namespace smoothoperator::drivers
