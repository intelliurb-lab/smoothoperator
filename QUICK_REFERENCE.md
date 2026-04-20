# Quick Reference — Memphis Development

## Build Commands

```bash
make debug              # Build with debug symbols
make release            # Build optimized
make test               # Run all tests
make coverage           # Generate coverage report
make format             # Auto-format code
make lint               # Check formatting
make clean              # Remove build/
make install            # Install to /usr/local/bin
```

## Testing

```bash
make test                         # Run all
cd build && ctest --verbose       # Run with output
./build/bin/memphis_tests         # Run test binary directly
```

## Code Quality Checklist

```bash
make format     # 1. Auto-format
make lint       # 2. Check formatting
make test       # 3. Run tests
make coverage   # 4. Check coverage
```

## Directory Structure

```
~/src/ls-controller/
├── src/                    C source code
├── include/                C headers
├── test/                   Critério tests
├── docs/
│   ├── ARCHITECTURE.md     Design details
│   └── API.md              RabbitMQ protocol
├── CMakeLists.txt          CMake config
├── Makefile                Build targets
├── README.md               Overview
├── GETTING_STARTED.md      Setup & roadmap
└── SUMMARY.md              Current status
```

## Key Files (By Responsibility)

| Responsibility | File | Status |
|---|---|---|
| Entry point, signals | src/main.c | Stub ✅ |
| Config loading | src/config.c | Stub ✅ |
| JSON parsing | src/message.c | Stub ✅ |
| LS socket TCP | src/liquidsoap_client.c | **TODO** |
| RabbitMQ consumer | src/rabbitmq_consumer.c | **TODO** |
| Event routing | src/ls_controller.c | **TODO** |
| Logging | src/memphis_logging.c | Stub ✅ |

## Commit Workflow

```bash
# Edit code
vim src/feature.c

# Format & test
make format test

# Stage
git add src/ test/ include/
git commit -m "feat: description"
```

## Testing New Feature (TDD)

```bash
# 1. Write test
vim test/test_feature.c

# 2. Implement
vim src/feature.c

# 3. Make it pass
make test

# 4. Lint
make format lint

# 5. Commit
git commit -m "feat: ..."
```

## Important Acceptance Criteria

### AC3: TCP Persistent Socket
- Socket stays open across multiple commands
- Timeout handling (3s)
- Keepalive (SO_KEEPALIVE)
- Tests pass: test_liquidsoap_client.c

### AC4: RabbitMQ Consumer
- Connects to RabbitMQ
- Consumes from "ls.commands" queue
- ACK on success, NACK (DLQ) on error
- Exponential backoff reconnect
- Tests pass: test_rabbitmq_consumer.c

### AC5: Event Routing
- Parse event type from message
- control.skip → "next"
- control.shutdown → "shutdown"
- announcement.push → "announcements.push <file>"
- Publish response
- Tests pass: test_ls_controller.c

## Debugging

```bash
# Compile with debug symbols
make debug

# GDB
gdb build/bin/memphis
(gdb) run
(gdb) bt

# View logs JSON
tail -f /var/log/memphis.log | jq .

# Check RabbitMQ
sudo rabbitmqctl list_queues
sudo rabbitmqctl list_channels

# Test LS socket directly
telnet 127.0.0.1 1234
> next
OK
> jax.nowplaying
Artist|Title
```

## Dependencies Check

```bash
pkg-config --cflags --libs librabbitmq    # AMQP
pkg-config --cflags --libs jansson        # JSON
pkg-config --cflags --libs cunit          # Tests
gcc --version                              # Compiler (need >= 4.7)
```

## Common Errors

| Error | Fix |
|---|---|
| `librabbitmq not found` | `sudo apt-get install librabbitmq-dev` |
| `jansson not found` | `sudo apt-get install libjansson-dev` |
| `cunit not found` | `sudo apt-get install libcunit1-dev` |
| `clang-format: not found` | `sudo apt-get install clang-format` |
| Tests fail | `make clean && make debug test` |

## Message Format (Quick)

```json
{
  "version": 1,
  "id": "msg123",
  "timestamp": "2026-04-20T10:30:00Z",
  "source": "radio_cli",
  "event": "control.skip",
  "payload": { "reason": "manual" }
}
```

## Liquidsoap Commands (Quick)

```
next                          # Skip
shutdown                      # Stop
announcements.push <file>     # Queue announcement
jax.nowplaying                # Get current track
```

## Config (memphis.env)

```
RABBITMQ_HOST=localhost
RABBITMQ_PORT=5672
LIQUIDSOAP_HOST=127.0.0.1
LIQUIDSOAP_PORT=1234
LIQUIDSOAP_TIMEOUT_MS=3000
LOG_LEVEL=INFO
LOG_FILE=/var/log/memphis.log
```

## Next Step?

Read `GETTING_STARTED.md` Section 6 (Development Workflow) for detailed instructions.
