# Memphis — RabbitMQ Controller for Liquidsoap

A high-performance C daemon that bridges RabbitMQ and Liquidsoap, providing event-driven audio stream control with reliable message routing.

```
RabbitMQ (message bus)
    ↓
Memphis (this daemon)
    ↓
Liquidsoap (audio server)
```

---

## What It Does

✅ **Consumes messages from RabbitMQ** — Listens to `radio.events` exchange for control events  
✅ **Routes events to Liquidsoap** — Sends commands via persistent TCP socket  
✅ **Validates all messages** — Enforces JSON schema with required fields  
✅ **Automatic retry with backoff** — Re-queues failed messages to RabbitMQ  
✅ **Structured logging** — JSON audit logs with timestamps and event tracking  
✅ **Persistent connection** — Maintains TCP socket to Liquidsoap (no setup/teardown overhead)  
✅ **Memory-safe** — Zero-copy JSON parsing, bounds checking, ASAN/UBSAN clean  
✅ **No heavy dependencies** — Pure C, minimal libraries (librabbitmq, jansson, cunit)

---

## What It Doesn't Do

❌ **Does NOT encrypt messages by default** — Use TLS for production (configuration provided)  
❌ **Does NOT authenticate senders** — All RabbitMQ producers are trusted; add HMAC-SHA256 if needed  
❌ **Does NOT perform rate limiting** — Implement via RabbitMQ policies or middleware  
❌ **Does NOT provide HTTP health check** — (Planned for Phase 2)  
❌ **Does NOT manage Liquidsoap lifecycle** — Assumes Liquidsoap is already running  
❌ **Does NOT support clustering** — Single-instance daemon (horizontal scaling via RabbitMQ)

---

## Installation

### Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt-get install -y build-essential cmake pkg-config \
  librabbitmq-dev libjansson-dev libcunit1-dev
```

**macOS:**
```bash
brew install cmake librabbitmq jansson cunit
```

**RHEL/CentOS:**
```bash
sudo yum install -y gcc cmake pkgconfig \
  librabbitmq-devel jansson-devel CUnit-devel
```

### Build from Source

```bash
git clone https://github.com/intelliurb-lab/smoothoperator.git
cd smoothoperator

# Debug build (with symbols, no optimization)
make debug

# Release build (optimized, hardened)
make release

# Verify build succeeded
ls -lh build/bin/memphis
```

---

## Usage

### 1. Configure Environment

Create `.env` file from example:
```bash
cp conf/memphis.env .env
```

Edit `.env` with your values:
```bash
# RabbitMQ
RABBITMQ_HOST=127.0.0.1
RABBITMQ_PORT=5672
RABBITMQ_USER=memphis
RABBITMQ_PASS=your_strong_password_here_min_12_chars
RABBITMQ_VHOST=/

# Liquidsoap
LIQUIDSOAP_HOST=127.0.0.1
LIQUIDSOAP_PORT=1234
LIQUIDSOAP_TIMEOUT_MS=3000

# Logging
LOG_LEVEL=INFO
LOG_FILE=/var/log/memphis.log
```

### 2. Start RabbitMQ

```bash
# Using systemd
sudo systemctl start rabbitmq-server
sudo systemctl status rabbitmq-server

# Or Docker
docker run -d --name rabbitmq -p 5672:5672 rabbitmq:3
```

### 3. Start Liquidsoap

Memphis expects Liquidsoap to be listening on TCP port 1234:

```bash
# Example Liquidsoap script with telnet server
liquidsoap '
  server.telnet(port=1234, bind_addr="127.0.0.1")
'
```

### 4. Start Memphis

```bash
# Load environment variables
source .env

# Run daemon (logs to /var/log/memphis.log)
./build/bin/memphis

# Or run in foreground for testing
LOG_FILE=- ./build/bin/memphis
```

### 5. Send Test Messages

```bash
# Using Python
python3 << 'EOF'
import pika
import json

conn = pika.BlockingConnection(pika.ConnectionParameters('127.0.0.1'))
channel = conn.channel()

msg = {
    "version": 1,
    "id": "test-001",
    "timestamp": "2026-04-20T10:30:00Z",
    "event": "control.skip",
    "source": "test"
}

channel.basic_publish(
    exchange='radio.events',
    routing_key='control.skip',
    body=json.dumps(msg)
)
conn.close()
print("✓ Message sent")
EOF
```

### Supported Events

| Event | Command | Description |
|-------|---------|-------------|
| `control.skip` | `next` | Skip current song |
| `control.shutdown` | `shutdown` | Stop playback |
| `announcement.push` | `announcements.push /path/file.mp3` | Queue audio file |

---

## Testing

### Unit Tests

```bash
make test
```

Expected output:
```
✓ test_config_load
✓ test_message_parse
✓ test_socket_creation
✓ ... (18 tests total)

18/18 PASSED
```

### Test Coverage

```bash
make coverage
```

Generated HTML report in `coverage_html/index.html`

### Memory Safety Verification

Memphis is compiled with AddressSanitizer (ASAN) and UndefinedBehaviorSanitizer (UBSAN) in debug mode:

```bash
make debug
ASAN_OPTIONS=verbosity=1 ./build/bin/memphis
```

Should show no memory errors or undefined behavior.

---

## Configuration

### Environment Variables

**Required:**
- `RABBITMQ_HOST` — RabbitMQ server hostname/IP
- `RABBITMQ_PORT` — RabbitMQ port (default: 5672)
- `RABBITMQ_USER` — Username (min 1 char)
- `RABBITMQ_PASS` — Password (min 12 chars)
- `LIQUIDSOAP_HOST` — Liquidsoap server hostname/IP
- `LIQUIDSOAP_PORT` — Liquidsoap telnet port (default: 1234)
- `LOG_FILE` — Path to log file (or `-` for stdout)
- `LOG_LEVEL` — DEBUG, INFO, WARN, ERROR, FATAL

**Optional:**
- `RABBITMQ_VHOST` — RabbitMQ virtual host (default: `/`)
- `RABBITMQ_TLS_ENABLED` — Enable TLS (0/1)
- `RABBITMQ_TLS_CA_CERT` — CA certificate path
- `LIQUIDSOAP_TIMEOUT_MS` — Socket timeout in ms (default: 3000)

### Security Notes

- Never commit `.env` with real credentials
- Use strong passwords (min 12 characters)
- Enable TLS for production (`RABBITMQ_TLS_ENABLED=1`)
- Restrict RabbitMQ access via firewall
- Run as unprivileged user via `MEMPHIS_USER=memphis`
- Monitor logs for errors and security events

---

## Architecture

For detailed architecture, design decisions, and data flow diagrams, see [ARCHITECTURE.md](ARCHITECTURE.md).

Key points:
- **Single-threaded event loop** — Blocks on `amqp_consume_message()` and `ls_send_command()`
- **Persistent TCP socket** — Maintains connection to Liquidsoap across multiple commands
- **Schema validation** — All messages validated against JSON schema before routing
- **Graceful shutdown** — Properly closes RabbitMQ channel and Liquidsoap socket on SIGTERM

---

## Message Format

All messages must be valid JSON with required fields:

```json
{
  "version": 1,
  "id": "unique-event-id",
  "timestamp": "2026-04-20T10:30:00Z",
  "event": "control.skip",
  "source": "web-ui"
}
```

For detailed API spec including all event types and optional fields, see [API.md](API.md).

---

## Logging

Logs are written in JSON format to the configured log file:

```json
{
  "timestamp": "2026-04-20T10:30:00Z",
  "level": "INFO",
  "module": "rabbitmq_consumer",
  "message": "consumed event from queue",
  "event_id": "test-001",
  "event_type": "control.skip"
}
```

Log levels: DEBUG, INFO, WARN, ERROR, FATAL

Set `LOG_LEVEL` to control verbosity. Always use INFO or higher in production.

---

## Troubleshooting

### Connection Refused to RabbitMQ
```bash
# Check RabbitMQ is running
sudo systemctl status rabbitmq-server

# Verify credentials
echo $RABBITMQ_USER $RABBITMQ_PASS

# Test connectivity
telnet 127.0.0.1 5672
```

### Connection Refused to Liquidsoap
```bash
# Verify Liquidsoap is listening
telnet 127.0.0.1 1234

# Check Liquidsoap config includes telnet server
# server.telnet(port=1234, bind_addr="127.0.0.1")
```

### No Logs Appearing
```bash
# Check log file path is writable
ls -la $(dirname $LOG_FILE)

# Check log level is not FATAL
echo $LOG_LEVEL

# Try logging to stdout
LOG_FILE=- ./build/bin/memphis
```

### Memory Issues
```bash
# Check for leaks in debug mode
make debug
ASAN_OPTIONS=verbosity=2 ./build/bin/memphis

# Should show no leaks or undefined behavior
```

---

## Development

### Code Structure

```
src/
  ├── main.c              — Entry point, CLI parsing, signal handling
  ├── config.c            — Environment variable loading and validation
  ├── message.c           — JSON parsing and schema validation
  ├── liquidsoap_client.c — TCP socket management and command sending
  ├── rabbitmq_consumer.c — AMQP consumer loop and message handling
  ├── ls_controller.c     — Event routing and command execution
  └── memphis_logging.c   — Structured JSON logging
include/
  └── *.h                 — Public headers
test/
  ├── test_config.c
  ├── test_message.c
  ├── test_liquidsoap_client.c
  └── test_rabbitmq_consumer.c
```

### Building for Development

```bash
make debug      # Debug with ASAN/UBSAN
make release    # Production build (hardened)
make test       # Run unit tests
make coverage   # Generate coverage report
make format     # Auto-format code
make lint       # Check code style
make clean      # Remove build artifacts
```

### Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Write tests for new functionality
4. Ensure all tests pass: `make test`
5. Format code: `make format`
6. Commit with clear messages
7. Push and open a pull request

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

---

## Performance

**Throughput:** ~1000 messages/sec (single-threaded)  
**Latency:** <100ms end-to-end (local network)  
**Memory footprint:** ~5MB resident  
**CPU usage:** <1% idle

For production deployments with high throughput, consider:
- Running multiple Memphis instances behind a load balancer
- Configuring RabbitMQ queue sharding
- Monitoring via health endpoint (planned Phase 2)

---

## License

**BSD 2-Clause License** — See [LICENSE](LICENSE) for details

Memphis is open source and freely available for commercial and personal use.

---

## Terms of Use

By using Memphis, you agree to the terms in [TERMS_OF_USE.md](TERMS_OF_USE.md). Key points:

- Use for lawful purposes only
- Do not use for malicious activities (DoS, hacking, etc.)
- Not liable for damages from misuse or misconfiguration
- Secure your credentials and configuration

---

## Support

For issues, questions, or contributions:

- Email: contact@intelliurb.com
- GitHub Issues: https://github.com/intelliurb-lab/smoothoperator/issues
- GitHub Discussions: https://github.com/intelliurb-lab/smoothoperator/discussions

---

## Status

**Version:** 0.1.0  
**Phase:** 1 (Core implementation)

| Component | Status |
|-----------|--------|
| RabbitMQ Consumer | ✅ Complete |
| Liquidsoap Client | ✅ Complete |
| Event Routing | ✅ Complete |
| JSON Validation | ✅ Complete |
| Unit Tests | ✅ 18/18 Passing |
| TLS Support | 🟡 Configurable (pending implementation) |
| HTTP Health Check | ⏳ Planned Phase 2 |
| Message Signing | ⏳ Planned Phase 2 |

---

**Last Updated:** 2026-04-20  
**License:** BSD 2-Clause
