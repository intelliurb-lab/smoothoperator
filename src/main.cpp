#include <iostream>
#include <ev.h>
#include <amqpcpp.h>
#include <amqpcpp/libev.h>
#include "smoothoperator/config.hpp"
#include "smoothoperator/liquidsoap_driver.hpp"
#include "smoothoperator/rabbitmq_driver.hpp"
#include "smoothoperator/state_manager.hpp"

using namespace smoothoperator;

void print_help() {
    std::cout << "SmoothOperator v" << (std::string(SMOOTHOP_VERSION)) << "\n"
              << "Usage: smoothoperator [options]\n\n"
              << "Options:\n"
              << "  -h, --help           Show this help message and exit\n"
              << "  -v, --version        Show version information and exit\n"
              << "  -d, --debug          Enable debug logging\n"
              << "  -c, --config, --conf <path>  Path to the configuration file (default: smoothoperator.json)\n\n"
              << "GitHub:  https://github.com/intelliurb-lab/smoothoperator\n"
              << "Author:  Carlos A. Quintella / Intelliurb (caq@intelliurb.com)\n"
              << "License: BSD 2-Clause License\n" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string config_path = "smoothoperator.json";
    bool debug_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "SmoothOperator v" << (std::string(SMOOTHOP_VERSION)) << std::endl;
            return 0;
        } else if (arg == "-d" || arg == "--debug" || arg == "--DEBUG") {
            debug_mode = true;
        } else if (arg == "-c" || arg == "--config" || arg == "--conf") {
            if (i + 1 < argc) {
                config_path = argv[++i];
            } else {
                std::cerr << "Error: " << arg << " requires a path argument" << std::endl;
                return 1;
            }
        } else if (arg.starts_with("-")) {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_help();
            return 1;
        } else {
            // Support legacy positional argument if no --config was provided
            if (config_path == "smoothoperator.json") {
                config_path = arg;
            }
        }
    }

    try {
        auto config = config::ConfigParser::load(config_path);
        if (debug_mode) config.log_level = "DEBUG";

        std::cout << "SmoothOperator v1.0 starting..." << std::endl;
        if (debug_mode) {
            std::cout << "[Debug] Config loaded from: " << config_path << std::endl;
            std::cout << "[Debug] RabbitMQ: " << config.rabbitmq.host << ":" << config.rabbitmq.port << std::endl;
            std::cout << "[Debug] Liquidsoap: " << config.liquidsoap.host << ":" << config.liquidsoap.port << std::endl;
        }

        // 1. Infrastructure Setup (Drivers)
        auto ls_driver = std::make_shared<drivers::TelnetLiquidsoapDriver>(
            config.liquidsoap.host, config.liquidsoap.port);

        // Setup LibEv & AMQP
        struct ev_loop *main_loop = EV_DEFAULT;
        AMQP::LibEvHandler amqp_handler(main_loop);
        AMQP::Address address(config.rabbitmq.host, config.rabbitmq.port,
                             AMQP::Login(config.rabbitmq.user, config.rabbitmq.password),
                             config.rabbitmq.vhost);

        AMQP::TcpConnection connection(&amqp_handler, address);
        AMQP::TcpChannel channel(&connection);

        auto event_bus = std::make_shared<drivers::AmqpEventBus>(channel, config.rabbitmq.exchange_name);

        // 2. Core Setup (Dependency Injection)
        auto state_manager = std::make_shared<core::StateManager>(ls_driver, event_bus, config.rabbitmq.state_routing_key, config.commands, config.intents);

        // 3. RabbitMQ Wiring (Event Routing)
        // We use passive declaration to verify existence as requested.
        channel.declareExchange(config.rabbitmq.exchange_name, AMQP::topic, AMQP::passive)
            .onError([&](const char* message) {
                std::cerr << "Fatal error: Exchange '" << config.rabbitmq.exchange_name << "' not found or inaccessible: " << message << std::endl;
                exit(1);
            });

        channel.declareQueue(config.rabbitmq.queue_name, AMQP::passive)
            .onSuccess([&](const std::string &name, uint32_t /*messageCount*/, uint32_t /*consumerCount*/) {
                if (debug_mode) std::cout << "[Debug] Queue verified: " << name << std::endl;
                
                channel.bindQueue(config.rabbitmq.exchange_name, config.rabbitmq.queue_name, config.rabbitmq.binding_key);

                channel.consume(config.rabbitmq.queue_name)
                    .onReceived([&](const AMQP::Message &message, uint64_t deliveryTag, bool /*redelivered*/) {
                        std::string routing_key = message.routingkey();
                        std::string body(message.body(), message.bodySize());
                        
                        try {
                            auto payload = nlohmann::json::parse(body);
                            state_manager->handle_dj_command(routing_key, payload);
                        } catch (const std::exception& e) {
                            std::cerr << "[Main] Error processing message: " << e.what() << std::endl;
                        }

                        channel.ack(deliveryTag);
                    });
            })
            .onError([&](const char *message) {
                std::cerr << "Fatal error: Queue '" << config.rabbitmq.queue_name << "' not found or inaccessible: " << message << std::endl;
                exit(1);
            });

        // 4. Polling Timer (Loop Integration)
        ev_timer poll_watcher;
        poll_watcher.data = state_manager.get();
        ev_timer_init(&poll_watcher, [](struct ev_loop * /*loop*/, ev_timer *w, int /*revents*/) {
            auto* sm = static_cast<core::StateManager*>(w->data);
            sm->poll();
        }, 0, config.liquidsoap.polling_interval_ms / 1000.0);
        ev_timer_start(main_loop, &poll_watcher);

        std::cout << "Event loop running..." << std::endl;
        ev_run(main_loop, 0);

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
