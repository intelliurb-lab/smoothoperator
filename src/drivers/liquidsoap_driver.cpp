#include "smoothoperator/liquidsoap_driver.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace smoothoperator::drivers {

TelnetLiquidsoapDriver::TelnetLiquidsoapDriver(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

TelnetLiquidsoapDriver::~TelnetLiquidsoapDriver() {
    disconnect();
}

std::error_code TelnetLiquidsoapDriver::connect() {
    if (socket_fd_ != -1) return {};

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return std::make_error_code(std::errc::io_error);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &serv_addr.sin_addr) <= 0) {
        disconnect();
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (::connect(socket_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        disconnect();
        return std::make_error_code(std::errc::connection_refused);
    }

    return {};
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
    if (send(socket_fd_, full_cmd.c_str(), full_cmd.length(), 0) < 0) {
        disconnect();
        return std::make_error_code(std::errc::io_error);
    }

    std::string response;
    char buffer[4096];
    while (true) {
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

    nlohmann::json meta;
    std::string data = std::get<std::string>(res);
    
    if (data.find("title=") != std::string::npos) {
        meta["title"] = "Sample Track";
        meta["artist"] = "Sample Artist";
        meta["duration"] = 210.0;
        meta["playlist"] = "heavy_rotation";
    }

    return meta;
}

} // namespace smoothoperator::drivers
