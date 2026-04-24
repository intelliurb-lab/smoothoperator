#include <iostream>
#include <csignal>
#include <ev.h>
#include <amqpcpp.h>
#include <amqpcpp/libev.h>
#include "smoothoperator/config.hpp"
#include "smoothoperator/liquidsoap_driver.hpp"
#include "smoothoperator/rabbitmq_driver.hpp"
#include "smoothoperator/state_manager.hpp"

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

static struct ev_loop *g_loop = nullptr;

static void signal_callback(struct ev_loop *loop, ev_signal *w, int revents) {
    std::cout << "\n[Main] Received signal " << w->signum << ", shutting down..." << std::endl;
    ev_break(loop, EVBREAK_ALL);
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);

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
        auto config = smoothoperator::config::ConfigParser::load(config_path);
        if (debug_mode) config.log_level = "DEBUG";

        std::cout << "SmoothOperator v" << SMOOTHOP_VERSION << " starting..." << std::endl;
        if (debug_mode) {
            std::cout << "[Debug] Config loaded from: " << config_path << std::endl;
            std::cout << "[Debug] RabbitMQ: " << config.rabbitmq.host << ":" << config.rabbitmq.port << std::endl;
            std::cout << "[Debug] Liquidsoap: " << config.liquidsoap.host << ":" << config.liquidsoap.port << std::endl;
        }

        // 1. Infrastructure Setup (Drivers)
        auto ls_driver = std::make_shared<smoothoperator::drivers::TelnetLiquidsoapDriver>(
            config.liquidsoap.host, config.liquidsoap.port);

        // Setup LibEv & AMQP
        struct ev_loop *main_loop = EV_DEFAULT;
        AMQP::LibEvHandler amqp_handler(main_loop);
        AMQP::Address address(config.rabbitmq.host, config.rabbitmq.port,
                             AMQP::Login(config.rabbitmq.user, config.rabbitmq.password),
                             config.rabbitmq.vhost);

        auto connection = std::make_shared<AMQP::TcpConnection>(&amqp_handler, address);
        auto channel = std::make_shared<AMQP::TcpChannel>(connection.get());

        auto event_bus = std::make_shared<smoothoperator::drivers::AmqpEventBus>(channel, config.rabbitmq.exchange_name);

        // 2. Core Setup (Dependency Injection)
        auto state_manager = std::make_shared<smoothoperator::core::StateManager>(ls_driver, event_bus, config.rabbitmq.state_routing_key, config.commands, config.intents);

        // 3. RabbitMQ Wiring (Event Routing)
        // We use passive declaration to verify existence as requested.
        channel->declareExchange(config.rabbitmq.exchange_name, AMQP::topic, AMQP::passive)
            .onError([&](const char* message) {
                std::cerr << "Fatal error: Exchange '" << config.rabbitmq.exchange_name << "' not found or inaccessible: " << message << std::endl;
                ev_break(main_loop, EVBREAK_ALL);
            });

        channel->declareQueue(config.rabbitmq.queue_name, AMQP::passive)
            .onSuccess([&](const std::string &name, uint32_t /*messageCount*/, uint32_t /*consumerCount*/) {
                if (debug_mode) std::cout << "[Debug] Queue verified: " << name << std::endl;

                channel->bindQueue(config.rabbitmq.exchange_name, config.rabbitmq.queue_name, config.rabbitmq.binding_key);

                channel->consume(config.rabbitmq.queue_name)
                    .onReceived([&](const AMQP::Message &message, uint64_t deliveryTag, bool /*redelivered*/) {
                        std::string routing_key = message.routingkey();
                        std::string body(message.body(), message.bodySize());

                        try {
                            auto payload = nlohmann::json::parse(body);
                            state_manager->handle_dj_command(routing_key, payload);
                            channel->ack(deliveryTag);
                        } catch (const nlohmann::json::parse_error& e) {
                            std::cerr << "[Main] JSON parse error in message (discarding): " << e.what() << std::endl;
                            channel->ack(deliveryTag);
                        } catch (const std::exception& e) {
                            std::cerr << "[Main] Logic error processing message: " << e.what() << std::endl;
                            channel->reject(deliveryTag, true);
                        }
                    });
            })
            .onError([&](const char *message) {
                std::cerr << "Fatal error: Queue '" << config.rabbitmq.queue_name << "' not found or inaccessible: " << message << std::endl;
                ev_break(main_loop, EVBREAK_ALL);
            });

        // 4. Polling Timer (Loop Integration)
        // Use heap-allocated context to safely hold shared_ptr and avoid dangling raw pointer
        struct PollContext {
            std::shared_ptr<smoothoperator::core::StateManager> state_manager;
        };
        auto* poll_context = new PollContext{state_manager};

        ev_timer poll_watcher;
        poll_watcher.data = poll_context;
        ev_timer_init(&poll_watcher, [](struct ev_loop * /*loop*/, ev_timer *w, int /*revents*/) {
            auto* ctx = static_cast<PollContext*>(w->data);
            ctx->state_manager->poll();
        }, 0, config.liquidsoap.polling_interval_ms / 1000.0);
        ev_timer_start(main_loop, &poll_watcher);

        // 5. Signal Handlers (Graceful Shutdown)
        g_loop = main_loop;
        ev_signal sig_int, sig_term;
        ev_signal_init(&sig_int, signal_callback, SIGINT);
        ev_signal_init(&sig_term, signal_callback, SIGTERM);
        ev_signal_start(main_loop, &sig_int);
        ev_signal_start(main_loop, &sig_term);

        std::cout << "Event loop running... (Ctrl+C to stop)" << std::endl;
        ev_run(main_loop, 0);

        // Cleanup
        delete poll_context;
        std::cout << "Shutting down gracefully..." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
