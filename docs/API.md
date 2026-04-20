# API — RabbitMQ Message Protocol

## Message Schema (JSON)

All RabbitMQ messages follow this schema:

```json
{
  "version": 1,
  "id": "01HWX5K2M8QPQZ...",
  "timestamp": "2026-04-20T10:30:00Z",
  "source": "radio_cli",
  "event": "control.skip",
  "payload": { }
}
```

### Required Fields
- `version` (integer): Schema version, always `1` for v1.0
- `id` (string): Unique message ID (UUID or similar)
- `timestamp` (string): ISO 8601 timestamp
- `event` (string): Event type (e.g., `control.skip`)

### Optional Fields
- `source` (string): Producer identifier (useful for debugging)
- `payload` (object): Event-specific data

---

## Liquidsoap Control Events

### Legacy Events (backward compatible)

#### control.skip
Skip to next track.

```json
{
  "version": 1,
  "id": "skip_001",
  "timestamp": "2026-04-20T10:30:00Z",
  "event": "control.skip",
  "payload": {}
}
```

#### control.shutdown
Shut down Liquidsoap (use with care!).

```json
{
  "version": 1,
  "id": "shutdown_001",
  "timestamp": "2026-04-20T10:30:00Z",
  "event": "control.shutdown",
  "payload": {}
}
```

#### announcement.push
Push file to announcement queue.

```json
{
  "version": 1,
  "id": "announce_001",
  "timestamp": "2026-04-20T10:30:00Z",
  "event": "announcement.push",
  "payload": {
    "filepath": "/opt/radio/audio/announcements/jingle.mp3"
  }
}
```

---

### Source Controls

#### source.skip
Skip on a specific source (queue).

```json
{
  "version": 1,
  "id": "src_skip_001",
  "event": "source.skip",
  "payload": {
    "source": "main_stream"
  }
}
```

#### source.metadata
Get current playing track metadata.

```json
{
  "version": 1,
  "id": "meta_001",
  "event": "source.metadata",
  "payload": {
    "source": "main_stream"
  }
}
```

#### source.remaining
Get remaining time of current track.

```json
{
  "version": 1,
  "id": "remain_001",
  "event": "source.remaining",
  "payload": {
    "source": "main_stream"
  }
}
```

---

### Request / Queue Operations

#### request.push
Push URI to a queue (generalized `announcement.push`).

```json
{
  "version": 1,
  "id": "req_push_001",
  "event": "request.push",
  "payload": {
    "queue": "announcements",
    "uri": "file:///opt/audio/announce.mp3"
  }
}
```

#### request.queue.list
List all requests in a queue.

```json
{
  "version": 1,
  "id": "queue_list_001",
  "event": "request.queue.list",
  "payload": {
    "queue": "announcements"
  }
}
```

#### request.on_air
List requests currently on air.

```json
{
  "version": 1,
  "id": "on_air_001",
  "event": "request.on_air",
  "payload": {}
}
```

#### request.alive
List "alive" (resolving) requests.

```json
{
  "version": 1,
  "id": "alive_001",
  "event": "request.alive",
  "payload": {}
}
```

#### request.metadata
Get metadata for a specific request ID.

```json
{
  "version": 1,
  "id": "req_meta_001",
  "event": "request.metadata",
  "payload": {
    "rid": 42
  }
}
```

#### request.trace
Trace/debug a specific request ID.

```json
{
  "version": 1,
  "id": "req_trace_001",
  "event": "request.trace",
  "payload": {
    "rid": 42
  }
}
```

---

### Interactive Variables

#### var.list
List all interactive variables.

```json
{
  "version": 1,
  "id": "var_list_001",
  "event": "var.list",
  "payload": {}
}
```

#### var.get
Get value of an interactive variable.

```json
{
  "version": 1,
  "id": "var_get_001",
  "event": "var.get",
  "payload": {
    "name": "crossfade_duration"
  }
}
```

#### var.set
Set value of an interactive variable.

```json
{
  "version": 1,
  "id": "var_set_001",
  "event": "var.set",
  "payload": {
    "name": "crossfade_duration",
    "value": "5.0"
  }
}
```

---

### Output Controls

#### output.start
Start an output.

```json
{
  "version": 1,
  "id": "out_start_001",
  "event": "output.start",
  "payload": {
    "output": "icecast"
  }
}
```

#### output.stop
Stop an output.

```json
{
  "version": 1,
  "id": "out_stop_001",
  "event": "output.stop",
  "payload": {
    "output": "icecast"
  }
}
```

---

### Playlist Controls

#### playlist.reload
Reload a playlist.

```json
{
  "version": 1,
  "id": "pl_reload_001",
  "event": "playlist.reload",
  "payload": {
    "playlist": "morning_show"
  }
}
```

---

### Server Introspection

#### server.uptime
Get Liquidsoap uptime.

```json
{
  "version": 1,
  "id": "uptime_001",
  "event": "server.uptime",
  "payload": {}
}
```

#### server.version
Get Liquidsoap version.

```json
{
  "version": 1,
  "id": "version_001",
  "event": "server.version",
  "payload": {}
}
```

#### server.list
List all available server commands.

```json
{
  "version": 1,
  "id": "list_001",
  "event": "server.list",
  "payload": {}
}
```

#### server.help
Get help for a command.

```json
{
  "version": 1,
  "id": "help_001",
  "event": "server.help",
  "payload": {
    "command": "var.set"
  }
}
```

---

## Response Format

All successful commands receive an implicit acknowledge via RabbitMQ ACK. For getter commands (`source.metadata`, `var.get`, `request.queue.list`), the response body is returned via the response latency metric and message field.

On error:
- `RESULT_INVALID`: Message schema validation failed or unknown event → NACK (no requeue)
- `RESULT_ERROR`: Command was valid but Liquidsoap returned an error → NACK (no requeue)
- `RESULT_RETRY`: Connection failure or socket timeout → NACK (requeue=true)

---

## Liquidsoap Protocol (Transport)

SmoothOperator communicates with Liquidsoap via either:

### TCP Telnet (default)
Port 1234 (configurable via `LIQUIDSOAP_PORT`). Set `LIQUIDSOAP_PROTOCOL=telnet`.

### Unix Domain Socket
For local-only deployments. Set `LIQUIDSOAP_PROTOCOL=socket` and `LIQUIDSOAP_SOCKET_PATH=/path/to/socket`.

Both transports use plain-text line protocol:
- Commands: `command_name args\n`
- Responses: Multiple lines terminated by a line containing only `END`

Example:
```
→ var.get my_var
← my_var = 5.0
← END
```

---

## Configuration

### Environment Variables

| Variable | Default | Required | Notes |
|---|---|---|---|
| `LIQUIDSOAP_PROTOCOL` | `telnet` | No | `telnet` or `socket` |
| `LIQUIDSOAP_HOST` | - | telnet mode | Hostname/IP |
| `LIQUIDSOAP_PORT` | - | telnet mode | Port number (1-65535) |
| `LIQUIDSOAP_SOCKET_PATH` | - | socket mode | Absolute path to Unix socket |
| `LIQUIDSOAP_TIMEOUT_MS` | `3000` | No | Socket I/O timeout |

### Example: TCP Telnet (default)
```bash
LIQUIDSOAP_PROTOCOL=telnet
LIQUIDSOAP_HOST=127.0.0.1
LIQUIDSOAP_PORT=1234
```

### Example: Unix Domain Socket
```bash
LIQUIDSOAP_PROTOCOL=socket
LIQUIDSOAP_SOCKET_PATH=/var/run/liquidsoap/ls.sock
```

---

## Exchange & Queues

### Exchange
- **Name**: `radio.events`
- **Type**: `topic`
- **Durable**: true
- **Auto-delete**: false

### Queue
- **Name**: `ls.commands`
- **Durable**: true
- **Exclusive**: false
- **QoS**: prefetch=1 (process one message at a time)

### Bindings

| Routing Key | Queue |
|---|---|
| `control.*` | `ls.commands` |
| `announcement.*` | `ls.commands` |
| `source.*` | `ls.commands` |
| `request.*` | `ls.commands` |
| `var.*` | `ls.commands` |
| `output.*` | `ls.commands` |
| `playlist.*` | `ls.commands` |
| `server.*` | `ls.commands` |

---

## Error Handling

### Invalid Messages
Messages that fail schema validation (missing required fields, wrong types) are:
1. Logged at WARN level
2. NACK'd without requeue
3. Optionally moved to DLQ if configured

### Unknown Events
Unknown event types return `RESULT_INVALID` and are NACK'd without requeue.

### Connection Failures
If Liquidsoap is unreachable or socket times out:
1. Response is NACK'd with requeue=true
2. Controller marks itself unhealthy
3. Automatic exponential backoff on reconnection

---

## Testing

### Manual Test (requires RabbitMQ + Liquidsoap running)

```bash
# Publish a skip event
python3 << 'EOF'
import pika
import json
import uuid
from datetime import datetime

conn = pika.BlockingConnection(pika.ConnectionParameters('localhost'))
channel = conn.channel()

message = {
    "version": 1,
    "id": str(uuid.uuid4()),
    "timestamp": datetime.utcnow().isoformat() + "Z",
    "source": "test_script",
    "event": "control.skip",
    "payload": {}
}

channel.basic_publish(
    exchange='radio.events',
    routing_key='control.skip',
    body=json.dumps(message),
    properties=pika.BasicProperties(delivery_mode=2)
)

print("Message published")
conn.close()
EOF
```

---

**Last Updated**: 2026-04-20
