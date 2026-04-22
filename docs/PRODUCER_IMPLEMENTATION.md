# RabbitMQ Producer Implementation

## Overview

SmoothOperator now has **bi-directional messaging**:

```
COMMANDS (Incoming)          STATE (Outgoing)
├─ ls.commands queue    ┌─→  ├─ state.playlist_changed
└─ radio.events ex.  ──→ SO ──→─ state.current_song
                         │
                    Liquidsoap
                    (source of truth)
```

## Architecture

### Producer Module (`rabbitmq_producer.c/h`)

**Responsibilities:**
1. Connect to RabbitMQ (separate connection from consumer)
2. Periodically poll Liquidsoap state (~5s interval)
3. Detect changes and publish events
4. Handle TLS, authentication, reconnection

**Key functions:**
- `rabbitmq_producer_create()` - Initialize connection
- `rabbitmq_producer_run()` - Start polling loop (blocking)
- `rabbitmq_producer_publish()` - Publish custom events
- `rabbitmq_producer_free()` - Cleanup

### Threading Model

```
Main Thread                Background Thread
├─ Load config           ├─ Producer init
├─ Start consumer        ├─ Poll loop (5s)
│  ├─ Receive commands   │  ├─ var.get current_playlist
│  ├─ Route to LS        │  ├─ request.on_air
│  └─ Block here         │  └─ Publish if changed
└─ Cleanup               └─ Detached (runs until shutdown)
```

Consumer runs in main thread (blocking AMQP consume), Producer runs detached in background.

## State Events

### `state.playlist_changed`
**When:** `var.get current_playlist` value changes

**Routing key:** `state.playlist_changed`

**Payload:**
```json
{
  "playlist": "default"
}
```

**Example:** When a DJ switches from "news_hour" to "music_rotation"

---

### `state.current_song`
**When:** `request.on_air` output changes

**Routing key:** `state.current_song`

**Payload:**
```json
{
  "song": "artist - title (metadata)"
}
```

**Example:** When a new track starts playing

---

## Configuration

**No additional config needed.** Producer inherits all RabbitMQ settings:

```bash
RABBITMQ_HOST=127.0.0.1
RABBITMQ_PORT=5672
RABBITMQ_USER=memphis
RABBITMQ_PASS=...         # ← Same auth as consumer
RABBITMQ_EXCHANGE_NAME=radio.events
```

## Usage

### Consume State Events

```bash
# Declare queue for state events
rabbitmqctl declare_queue smoothoperator.state --durable

# Bind to state.* routing keys
rabbitmqctl bind_queue smoothoperator.state radio.events 'state.*'
```

### Python Consumer Example

```python
import pika
import json

conn = pika.BlockingConnection(
    pika.ConnectionParameters('localhost')
)
channel = conn.channel()

def callback(ch, method, props, body):
    event = json.loads(body)
    print(f"{method.routing_key}: {event}")
    if method.routing_key == 'state.playlist_changed':
        print(f"→ Now playing: {event['playlist']}")
    ch.basic_ack(delivery_tag=method.delivery_tag)

channel.basic_consume(
    queue='smoothoperator.state',
    on_message_callback=callback
)
channel.start_consuming()
```

### JavaScript/Node.js Example

```javascript
const amqp = require('amqplib');

async function consumeStateEvents() {
  const conn = await amqp.connect('amqp://localhost');
  const ch = await conn.createChannel();
  
  await ch.assertQueue('smoothoperator.state');
  await ch.bindQueue('smoothoperator.state', 'radio.events', 'state.*');
  
  ch.consume('smoothoperator.state', (msg) => {
    const event = JSON.parse(msg.content.toString());
    console.log(`State update: ${msg.fields.routingKey}`, event);
    ch.ack(msg);
  });
}

consumeStateEvents().catch(console.error);
```

## Monitoring

### Check if producer is connected
```bash
# Watch logs
tail -f /var/log/smoothoperator.log | grep producer
```

### Sample log output
```
{"timestamp":"2026-04-22T15:30:45Z","level":"INFO","module":"rabbitmq_producer","message":"published event","event_type":"state.playlist_changed","data":"{\"playlist\":\"music\"}"}
```

### Verify messages arriving
```bash
# Listen on state queue
rabbitmqctl get smoothoperator.state auto
```

## Performance

- **Polling interval:** 5000ms (configurable in `rabbitmq_producer.c` - `POLL_INTERVAL_MS`)
- **Memory overhead:** ~2KB per tracked variable
- **Thread safety:** Signal-safe design (no locks needed)
- **Throughput:** Non-blocking, ~1 publish per 5s (low latency)

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Liquidsoap offline | Skip poll, retry next interval |
| RabbitMQ unavailable | Log error, mark disconnected |
| State unchanged | No publish (efficient) |
| Producer crashes | Main thread (consumer) continues |

## Implementation Details

### Change Detection

Uses string comparison to detect state changes:

```c
if (strings_differ(producer->prev_playlist, new_playlist)) {
    // Publish event
    producer->prev_playlist = strdup(new_playlist);
}
```

### LS Commands Used

- `var.get current_playlist` — Read current playlist variable
- `request.on_air` — Get currently playing track metadata

These are safe, read-only operations (no state mutation).

### Thread Lifecycle

```
main()
├─ pthread_create(producer_thread_fn)
│  └─ rabbitmq_producer_run() [blocking loop]
│     ├─ poll_playlist_state()
│     ├─ poll_current_song()
│     └─ usleep(5000ms)
├─ rabbitmq_consumer_run() [blocking loop, main thread]
│  └─ [waits for SIGTERM/SIGINT]
└─ [signal received]
   ├─ shutdown_requested = 1
   ├─ producer_shutdown() [non-blocking]
   └─ producer_free() [cleanup]
```

## Future Extensions

Easy to add more state events:

```c
static void poll_queue_length(rabbitmq_producer_t *producer) {
  ls_response_t *resp = ls_send_command(sock, "request.queue_length");
  // Publish state.queue_changed
}
```

Then call from `rabbitmq_producer_run()`.

---

**Last Updated:** 2026-04-22
