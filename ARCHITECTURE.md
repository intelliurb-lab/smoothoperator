# Architecture

SmoothOperator is a high-performance, single-threaded event-driven daemon written in C++23 that bridges RabbitMQ and Liquidsoap.

## System Overview

```plantuml
@startuml System Overview

package "Message Bus" {
  component RabbitMQ [
    RabbitMQ
    ----
    Exchange: radio.events (Topic)
    Queue: smoothoperator.events
  ]
}

package "SmoothOperator (C++23)" {
  component StateManager [
    State Manager (Core)
    ----
    Business Logic
    State Mirroring
    Intent Translation
  ]
  
  component ConfigParser [
    Config Parser
    ----
    JSON + .env loading
    Validation
  ]
  
  component RabbitMQDriver [
    RabbitMQ Driver (AMQP-CPP)
    ----
    Event Bus implementation
    Pub/Sub handling
  ]
  
  component LiquidsoapDriver [
    Liquidsoap Driver
    ----
    Stream Provider implementation
    Telnet/Socket transport
  ]
}

package "Audio Server" {
  component Liquidsoap [
    Liquidsoap
    ----
    Port: 1234 (telnet)
    Unix Socket (optional)
  ]
}

RabbitMQ --> RabbitMQDriver: message (DJ Intent)
RabbitMQDriver --> StateManager: handle_dj_command
StateManager --> LiquidsoapDriver: execute command
LiquidsoapDriver --> Liquidsoap: TCP/Socket
Liquidsoap --> LiquidsoapDriver: Response
StateManager --> RabbitMQDriver: publish state
RabbitMQDriver --> RabbitMQ: radio.state
@enduml
```

## Key Technologies

- **C++23:** Modern language features for safety and performance.
- **Libev:** High-performance event loop for non-blocking I/O.
- **AMQP-CPP:** Robust C++ library for RabbitMQ communication.
- **nlohmann/json:** Modern JSON for C++ used for configuration and message parsing.
- **Clean Architecture:** Decoupling core logic (StateManager) from infrastructure (Drivers).

## Core Design Principles

1. **Single-threaded Event Loop:** Uses `libev` to handle RabbitMQ events and polling timers without the complexity of multi-threading.
2. **State Mirroring:** Proactively polls Liquidsoap and maintains a local "source of truth" to reduce latency for external clients.
3. **Intent Translation:** Maps high-level DJ "intents" (e.g., `dj.skip`) to technical Liquidsoap commands (e.g., `source.skip`).
4. **Dependency Injection:** Drivers are injected into the Core, making it easy to test and swap transports (e.g., switching from Telnet to Unix Sockets).

## Build & Environment

- **CMake:** Standard build system with `GNUInstallDirs` support.
- **Makefile:** Clean wrapper for developers and automated deployments.
- **Sanitizers:** Integrated ASAN, UBSAN, and TSAN for memory and thread safety.

---
**Last Updated:** 2026-04-23
