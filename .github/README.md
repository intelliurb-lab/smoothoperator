# SmoothOperator — RabbitMQ Controller for Liquidsoap

A high-performance C daemon that bridges RabbitMQ and Liquidsoap, providing event-driven audio stream control with reliable message routing.

RabbitMQ (message bus)
    ↓
SmoothOperator (this daemon)
    ↓
Liquidsoap (audio server)

What It Does

✅ Consumes messages from RabbitMQ — Listens to radio.events exchange for control events
✅ Routes events to Liquidsoap — Sends commands via persistent TCP socket
✅ Validates all messages — Enforces JSON schema with required fields
✅ Automatic retry with backoff — Re-queues failed messages to RabbitMQ
✅ Structured logging — JSON audit logs with timestamps and event tracking
✅ Persistent connection — Maintains TCP socket to Liquidsoap (no setup/teardown overhead)
✅ Memory-safe — Zero-copy JSON parsing, bounds checking, ASAN/UBSAN clean
✅ No heavy dependencies — Pure C, minimal libraries (librabbitmq, jansson, cunit)
✅ Production-ready — Security hardened, fully tested, BSD 2-Clause licensed
What It Doesn't Do

❌ Does NOT encrypt messages by default — Use TLS for production (configuration provided)
❌ Does NOT authenticate senders — All RabbitMQ producers are trusted; add HMAC-SHA256 if needed
❌ Does NOT perform rate limiting — Implement via RabbitMQ policies or middleware
❌ Does NOT provide HTTP health check — (Planned for v0.2)
❌ Does NOT manage Liquidsoap lifecycle — Assumes Liquidsoap is already running
❌ Does NOT support clustering — Single-instance daemon (use RabbitMQ for horizontal scaling)

---

# GitHub Templates & Workflows

This directory contains GitHub-specific configuration files:

- `ISSUE_TEMPLATE/` — Templates for bug reports and feature requests
- `pull_request_template.md` — Template for pull requests

These files help contributors follow a consistent format and ensure quality.
