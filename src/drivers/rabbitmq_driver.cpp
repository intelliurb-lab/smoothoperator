#include "smoothoperator/rabbitmq_driver.hpp"
#include <iostream>

namespace smoothoperator::drivers {

AmqpEventBus::AmqpEventBus(std::shared_ptr<AMQP::Channel> channel, const std::string& exchange)
    : channel_(std::move(channel)), exchange_(exchange) {}

void AmqpEventBus::publish(const std::string& topic, const nlohmann::json& payload) {
    std::string msg = payload.dump();
    channel_->publish(exchange_, topic, msg);
}

void AmqpEventBus::subscribe([[maybe_unused]] const std::string& pattern, [[maybe_unused]] std::function<void(const std::string&, const nlohmann::json&)> handler) {
    // This is handled in main.cpp for the prototype to avoid complex state in the driver,
    // but in a full implementation, the driver would manage its own queue and bindings.
}

} // namespace smoothoperator::drivers
