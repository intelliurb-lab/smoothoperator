# Arquitetura — Memphis Controller

## Design Principles

1. **Single Responsibility**: Memphis é APENAS o bridge RabbitMQ ↔ Liquidsoap
2. **Zero Jitter**: Latência previsível, sem surpresas
3. **Explicit > Implicit**: Validação rigorosa, falha rápido em JSON inválido
4. **Minimal Dependencies**: C puro, librabbitmq + jansson apenas
5. **Observability First**: Logging estruturado em cada decisão

## Componentes

### 1. main.c

**Responsabilidade**: Lifecyle, signals, config loading

```
start
  ↓
load config (env vars)
  ↓
init rabbitmq_consumer
init liquidsoap_client
init http_healthcheck
  ↓
signal handlers (SIGTERM, SIGHUP)
  ↓
wait for shutdown signal
  ↓
cleanup (close connections, flush logs)
  ↓
exit
```

**Config** vem de `memphis.env`:
```bash
RABBITMQ_HOST=localhost
RABBITMQ_PORT=5672
RABBITMQ_USER=guest
RABBITMQ_PASS=guest
RABBITMQ_VHOST=/
LIQUIDSOAP_HOST=127.0.0.1
LIQUIDSOAP_PORT=1234
LIQUIDSOAP_RECONNECT_MAX_DELAY=30s
HTTP_HEALTHCHECK_PORT=9000
LOG_LEVEL=INFO
LOG_FILE=/var/log/memphis.log
```

### 2. rabbitmq_consumer.c

**Responsabilidade**: Consumir eventos, ack/nack, retry

```
rabbitmq_consumer_init()
  → connect to RabbitMQ
  → declare queue "ls.commands" (durable, exclusive=false)
  → bind to exchange "radio.events" com routing key pattern
  → QoS prefetch=1 (processa um de cada vez)

while (running) {
  msg = consume_message()
  
  if (msg == NULL) {
    reconnect_with_backoff()
    continue
  }
  
  if (!validate_schema(msg)) {
    nack(msg, requeue=false)  // → DLQ
    log(WARN, "invalid schema")
    continue
  }
  
  result = controller_handle_event(msg)
  
  if (result == OK)
    ack(msg)
  else if (result == RETRY)
    nack(msg, requeue=true)  // volta pra fila
  else
    nack(msg, requeue=false) // → DLQ
}
```

**Reconexão**: Exponential backoff, 50ms → 30s max

### 3. ls_controller.c

**Responsabilidade**: Orquestração, roteamento de eventos

Máquina de estados:
```
IDLE
  ↓ new message
PROCESSING (valida, mapeia evento → comando LS)
  ↓
EXECUTING (envia ao LS via socket persistente)
  ↓
OK / RETRY / ERROR
  ↓
IDLE
```

Eventos suportados (v1.0):

| Evento | Comando LS | Resposta |
|--------|-----------|----------|
| `control.skip` | `next` | ✓ `ls.response` |
| `control.shutdown` | `shutdown` | ✓ Desliga LS |
| `announcement.push` | `announcements.push <file>` | ✓ `ls.response` |
| `playlist.change` | N/A (arquivo em `playlists/`) | ✓ LS auto-reload |

**Resposta** publicada em exchange `radio.events`:
```json
{
  "version": 1,
  "id": "01HWX5K2M8QPQZ...",
  "timestamp": "2026-04-20T10:30:00.123Z",
  "source": "memphis",
  "event": "ls.response",
  "payload": {
    "request_id": "01HWX5K2M8QPQZ...",
    "status": "ok" | "error" | "retry",
    "message": "command executed",
    "latency_ms": 42
  }
}
```

### 4. liquidsoap_client.c

**Responsabilidade**: TCP socket persistente ao Liquidsoap

```c
// Conecta uma vez no init
ls_socket_t *sock = ls_connect(host, port, timeout);
// Reutiliza para N comandos
ls_response_t *resp = ls_send_command(sock, "next");
// Reconecta automaticamente se socket morre
if (ls_is_connected(sock) == false) {
  ls_reconnect(sock);
}
```

**Features**:
- Keepalive TCP (SO_KEEPALIVE)
- Timeout em send/recv (default 3s)
- Reconexão transparente (sem perder estado)
- Response parsing (simples: busca `ERR` ou sucesso)

### 5. message.c

**Responsabilidade**: Parsing JSON, validação schema, conversão

```c
message_t *msg = message_parse(json_string);

// Valida estrutura obrigatória
if (!message_is_valid(msg)) {
  log(WARN, "schema violation");
  return NULL;
}

// Extrai campos tipados
uint32_t version = message_get_version(msg);  // obrigatório
const char *event_type = message_get_event(msg);
json_t *payload = message_get_payload(msg);
```

**Schema (JSON Schema)**:
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "required": ["version", "id", "event", "timestamp"],
  "properties": {
    "version": { "type": "integer", "const": 1 },
    "id": { "type": "string", "pattern": "^[a-z0-9]+$" },
    "timestamp": { "type": "string", "format": "date-time" },
    "source": { "type": "string" },
    "event": { "type": "string" },
    "payload": { "type": "object" }
  }
}
```

### 6. config.c

**Responsabilidade**: Load env vars, validação, defaults

```c
config_t *cfg = config_load();
// Verifica obrigatórios
if (!config_is_valid(cfg)) {
  log(ERROR, "invalid config");
  exit(1);
}
```

**Precedência**:
1. Environment variables
2. `memphis.env` se existir
3. Defaults sensatos

## Data Flow

```
RabbitMQ
  │
  ├─→ [rabbitmq_consumer] (pré-valida conexão, ack QoS)
  │
  ├─→ [message_parse] (JSON → struct)
  │
  ├─→ [message_validate] (schema)
  │
  ├─→ [ls_controller] (event → LS command)
  │
  ├─→ [liquidsoap_client] (persistent socket)
  │
  ├─→ Liquidsoap [resposta]
  │
  ├─→ [ls_controller] (formata resposta)
  │
  └─→ RabbitMQ exchange "radio.events" [response event]
```

## Error Handling

**Princípio**: Fail fast, log loudly, retry smart

| Erro | Ação |
|------|------|
| JSON inválido | NACK (DLQ), log ERROR |
| Schema inválido | NACK (DLQ), log WARN |
| LS desconectado | Reconnect + retry, log WARN |
| LS timeout | Retry com backoff, log WARN |
| RabbitMQ timeout | Reconnect, log ERROR |
| Config inválida | Exit(1), log ERROR |

## Logging

Estruturado, cada linha é JSON:

```json
{
  "timestamp": "2026-04-20T10:30:00.123456Z",
  "level": "INFO",
  "module": "ls_controller",
  "message": "event processed",
  "event_id": "01HWX5K2M8...",
  "event_type": "control.skip",
  "duration_ms": 42,
  "ls_response": "OK"
}
```

Levels: DEBUG, INFO, WARN, ERROR, FATAL

## Testing Strategy

### Unit Tests

- `test_message_parse.c` — JSON parsing, edge cases
- `test_message_validate.c` — Schema validation
- `test_config.c` — Config loading, defaults
- `test_liquidsoap_client.c` — Socket mock, commands

### Integration Tests

- `test_rabbitmq_consumer.c` — Producer + consumer (RabbitMQ real ou mock)
- `test_ls_controller.c` — End-to-end com fixtures

### Fixtures

- `test/fixtures/messages/` — JSON de teste (válido, inválido, edge cases)
- `test/fixtures/config/` — Config variations

## Performance

**Targets**:
- Latência add-to-stream: < 50ms (incluindo parsing + socket + RabbitMQ ack)
- Throughput: 100+ eventos/seg (comfortável para estações de rádio)
- Memory: < 10MB (steady state)
- CPU: < 5% idle, < 20% em pico

## Future (v1.1+)

- [ ] Observability: Prometheus metrics
- [ ] Auditoria: gravação de eventos em SQLite
- [ ] Delayed messages: scheduler RabbitMQ plugin
- [ ] Webhook: notificações de eventos (Slack, Discord)
- [ ] Multi-stream: router por `stream_id`
