# Memphis — Summary & Status

**Data**: 2026-04-20  
**Phase**: 1-2 (Structure & Skeleton Complete)  
**Status**: 🟢 Ready for P1.1 (Socket TCP Implementation)

---

## What Was Delivered

### 📚 Documentation (Complete)
- ✅ README.md — Overview, features, quick start
- ✅ ARCHITECTURE.md — Design, components, data flow, testing strategy
- ✅ API.md — RabbitMQ schema, message examples, protocol
- ✅ GETTING_STARTED.md — Setup, dev workflow, detailed roadmap
- ✅ This summary (SUMMARY.md)

### 🏗️ Build & Quality Infrastructure
- ✅ CMakeLists.txt — Portable C build system
- ✅ Makefile — Convenience targets (debug, release, test, coverage, lint)
- ✅ .clang-format — Code style enforcement
- ✅ .gitignore — VCS exclusions

### 💻 C Code Foundation
- ✅ Headers (6x): memphis.h, config.h, message.h, liquidsoap_client.h, rabbitmq_consumer.h, ls_controller.h
- ✅ Implementation stubs (7x): main.c, config.c, message.c, liquidsoap_client.c, rabbitmq_consumer.c, ls_controller.c, memphis_logging.c
- ✅ Logging: JSON structured logging to file/stdout

### 🧪 Tests (Critério Framework)
- ✅ test_message.c — JSON parsing, schema validation (8 tests)
- ✅ test_config.c — Config loading, defaults, log levels (5 tests)
- ✅ test_liquidsoap_client.c — Socket interface (2 tests)
- ✅ test_runner.c — Critério discovery

### 🔧 Configuration & Scripts
- ✅ memphis.env.example — Config template with defaults
- ✅ setup-amqp.sh — RabbitMQ installation guide

---

## Where to Work

### Local Development
```bash
cd ~/src/ls-controller/    # Your working copy
# OR
cd /opt/radio/memphis/     # Shared reference
```

Both are identical. Use ~/src/ls-controller/ for development.

---

## Next 4 Phases (Detailed in GETTING_STARTED.md)

### ✅ Phase 1-2: Structure & Skeleton (DONE)
- Duration: ~4 hours
- Delivered: Everything above

### 🟡 Phase 1.1: Socket TCP Persistent (1-2 days)
**Goal**: Liquidsoap client with keep-alive connection

**Acceptance Criteria**:
- [ ] Socket stays open across multiple `ls_send_command()` calls
- [ ] Timeout handling (3s default)
- [ ] Reconnect on disconnection
- [ ] Response parsing (detects "ERR" vs "OK")
- [ ] Tests: all 3 above + latency tracking

**Files to edit**:
- `src/liquidsoap_client.c` — Implement send_command(), keepalive
- `test/test_liquidsoap_client.c` — Add real socket tests

### 🟡 Phase 1.2: RabbitMQ Consumer (2-3 days)
**Goal**: Connect, consume, ACK/NACK

**Acceptance Criteria**:
- [ ] Connect to RabbitMQ (host, port, credentials from config)
- [ ] Declare queue "ls.commands" if missing
- [ ] Consume with QoS=1 (process one at a time)
- [ ] ACK valid messages, NACK (requeue=false) to DLQ on error
- [ ] Exponential backoff reconnect (50ms → 30s)
- [ ] Tests: connection, consumption, ACK/NACK

**Files to edit**:
- `src/rabbitmq_consumer.c` — Implement create(), run(), shutdown()
- `src/config.c` — Add RABBITMQ_* env var loading
- `test/test_rabbitmq_consumer.c` — Integration tests

### 🟡 Phase 1.3: Event Routing (2-3 days)
**Goal**: Map RabbitMQ events to Liquidsoap commands

**Acceptance Criteria**:
- [ ] Parse message.event (string)
- [ ] Route to LS command:
  - `control.skip` → `next`
  - `control.shutdown` → `shutdown`
  - `announcement.push` → `announcements.push <filepath>`
  - `playlist.change` → (file written to /opt/radio/playlists, LS auto-reloads)
- [ ] Format response JSON with request_id, status, latency_ms
- [ ] Publish response to `radio.events` exchange
- [ ] Tests: each event type, happy + error path

**Files to edit**:
- `src/ls_controller.c` — Implement handle_event()
- `src/rabbitmq_consumer.c` — Call controller in loop
- `test/test_ls_controller.c` — Integration tests

### 🟡 Phase 1.4: End-to-End Integration (2-3 days)
**Goal**: Full pipeline running

**Acceptance Criteria**:
- [ ] Main loop runs without crashing
- [ ] Consumer connects on startup
- [ ] Liquidsoap socket stays open
- [ ] Events flow: RabbitMQ → Memphis → Liquidsoap
- [ ] Responses published back to RabbitMQ
- [ ] Graceful shutdown (SIGTERM)
- [ ] Tests: e2e with real RabbitMQ + Liquidsoap

**Files to edit**:
- `src/main.c` — Wire consumer.run() in main loop
- All tests pass

---

## Code Quality Expectations

### Before Each Commit
```bash
make format    # Auto-format with clang-format
make lint      # Check formatting
make test      # All tests pass
make coverage  # Coverage report
```

### Commit Message Style
```
feat: add TCP keepalive to liquidsoap client

- Implement persistent connection in ls_send_command()
- Add SO_KEEPALIVE socket option
- Track latency in response
- Tests: 3 new tests in test_liquidsoap_client.c

AC3 from ARCHITECTURE.md complete.
```

### Testing Requirements
- Unit tests: `make test` must pass 100%
- Acceptance tests: Each AC from next phase checked off
- Manual testing: With real RabbitMQ + Liquidsoap

---

## Troubleshooting

### Build fails
```bash
pkg-config --cflags --libs librabbitmq  # check install
pkg-config --cflags --libs jansson      # check install
```

### Tests fail
- Criterion not installed? `sudo apt-get install criterion-dev`
- Code changed? Run `make format` first
- Clear cache? `make clean && make debug`

### Can't compile
- Check C11 support: `gcc --version` (need >= 4.7)
- Check headers: `ls /usr/include/amqp.h`

---

## Questions / Blockers?

- Coding question? Check `docs/ARCHITECTURE.md` for design
- API question? Check `docs/API.md` for message format
- Setup question? Check `GETTING_STARTED.md`
- Lost? Read `README.md` for overview

---

## Current Files

### Config Files
- `.gitignore` — Git exclusions
- `.clang-format` — Code style
- `CMakeLists.txt` — Build config
- `Makefile` — Convenience targets
- `memphis.env.example` — Config template

### Documentation
- `README.md` — Overview
- `ARCHITECTURE.md` — Design deep-dive
- `API.md` — RabbitMQ protocol
- `GETTING_STARTED.md` — Dev setup & roadmap
- `SUMMARY.md` — This file

### Code (Stubs Ready for Implementation)
- `include/` — 6 header files
- `src/` — 7 implementation files
- `test/` — 4 test files

### Scripts
- `scripts/setup-amqp.sh` — RabbitMQ setup guide

---

## Success Criteria (v1.0 Complete)

✅ Phase 1.1: TCP socket persistent  
✅ Phase 1.2: RabbitMQ consumer  
✅ Phase 1.3: Event routing  
✅ Phase 1.4: End-to-end running  

When all 4 are done, Memphis v1.0 is complete and ready for:
- Production deployment (systemd service)
- Migration of 5 existing Python scripts to Memphis producers
- Observability features (metrics, audit log)

---

**Ready to start coding?** See `GETTING_STARTED.md` section 6 (Development Workflow).

Good luck! 🚀
