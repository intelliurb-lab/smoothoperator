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

std::error_code TelnetLiquidsoapDriver::connect() {
    if (socket_fd_ != -1) return {};

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

    // Simple line-based parser
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (line == "END" || line == "END\r") break;

        size_t pos = line.find("=\"");
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 2);
            if (!val.empty() && val.back() == '"') val.pop_back();
            if (!val.empty() && val.back() == '\r') val.pop_back();
            if (!val.empty() && val.back() == '"') val.pop_back();

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
    }

    // Ensure mandatory fields for core with correct types
    if (!meta.contains("title")) meta["title"] = "Unknown";
    if (!meta.contains("artist")) meta["artist"] = "Unknown";
    if (!meta.contains("duration") || !meta["duration"].is_number()) meta["duration"] = 0.0;
    if (!meta.contains("playlist")) meta["playlist"] = "default";

    return meta;
}

} // namespace smoothoperator::drivers
