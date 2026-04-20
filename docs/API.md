# API — RabbitMQ Message Protocol

## Message Schema (JSON)

Todas as mensagens RabbitMQ seguem este schema:

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

### Campos Obrigatórios
- `version` (integer): Schema version, sempre 1 pra v1.0
- `id` (string): ID único da mensagem (UUID ou similar)
- `timestamp` (string): ISO 8601 timestamp
- `event` (string): Tipo de evento (ex: `control.skip`)

### Campos Opcionais
- `source` (string): Qual producer enviou (útil pra debug)
- `payload` (object): Dados específicos do evento

---

## Eventos Suportados (v1.0)

### control.skip
**Descrição**: Pular para próxima música

```json
{
  "version": 1,
  "id": "skip_001",
  "timestamp": "2026-04-20T10:30:00Z",
  "source": "radio_cli",
  "event": "control.skip",
  "payload": {
    "reason": "manual_override"
  }
}
```

**Resposta**:
```json
{
  "version": 1,
  "id": "resp_skip_001",
  "timestamp": "2026-04-20T10:30:00.050Z",
  "source": "smoothoperator",
  "event": "ls.response",
  "payload": {
    "request_id": "skip_001",
    "status": "ok",
    "message": "skipped to next track",
    "latency_ms": 45
  }
}
```

---

### control.shutdown
**Descrição**: Desligar Liquidsoap (use com cuidado!)

```json
{
  "version": 1,
  "id": "shutdown_001",
  "timestamp": "2026-04-20T10:30:00Z",
  "source": "radio_cli",
  "event": "control.shutdown",
  "payload": {
    "reason": "maintenance"
  }
}
```

---

### announcement.push
**Descrição**: Empurrar arquivo de anúncio pra fila

```json
{
  "version": 1,
  "id": "announce_001",
  "timestamp": "2026-04-20T10:30:00Z",
  "source": "jax_silver",
  "event": "announcement.push",
  "payload": {
    "filepath": "/opt/radio/audio/announcements/jax_20260420_1030.mp3",
    "priority": "high"
  }
}
```

---

### playlist.change
**Descrição**: Trocar playlist ativa

```json
{
  "version": 1,
  "id": "playlist_001",
  "timestamp": "2026-04-20T10:30:00Z",
  "source": "produtor",
  "event": "playlist.change",
  "payload": {
    "playlist_name": "manha_desconfortavel",
    "start_time": "2026-04-20T06:00:00Z"
  }
}
```

---

## Exchange e Queues

### Exchange
- **Name**: `radio.events`
- **Type**: `topic`
- **Durable**: true
- **Auto-delete**: false

### Queues
- **Name**: `ls.commands`
- **Durable**: true
- **Exclusive**: false
- **QoS**: prefetch=1 (processa um de cada vez)

### Bindings

| Queue | Exchange | Routing Key |
|-------|----------|------------|
| `ls.commands` | `radio.events` | `control.*` |
| `ls.commands` | `radio.events` | `announcement.*` |
| `ls.commands` | `radio.events` | `playlist.*` |

---

## Liquidsoap Protocol

Memphis comunica com Liquidsoap via TCP socket (porta 1234) com protocolo de texto simples.

### Comandos Suportados

```
next                          # Pular pra próxima música
shutdown                      # Desligar Liquidsoap
announcements.push <filepath> # Empurrar anúncio
jax.nowplaying                # Get current track (artist|title)
```

### Respostas

```
OK                       # Sucesso
ERR <message>           # Erro
```

---

## Fluxo Exemplo: Skip de Música

```
1. radio_cli publica mensagem em "radio.events"
   routing_key: "control.skip"
   message_id: "skip_001"

2. RabbitMQ roteia para fila "ls.commands"

3. Memphis consumer recebe mensagem

4. Valida schema (version=1, id, event obrigatórios)

5. Parseia event type "control.skip"

6. Envia "next" ao socket TCP do Liquidsoap

7. Liquidsoap responde "OK"

8. Memphis formata resposta JSON

9. Publica resposta em "radio.events" 
   routing_key: "ls.response"
   request_id: "skip_001"
   status: "ok"
   latency_ms: 45

10. ACK da mensagem para RabbitMQ
```

---

## Versionamento

**Schema versioning**: Se precisar quebrar compatibilidade, crie nova versão:
- Versão 1: campo "source" opcional
- Versão 2 (futuro): requerer "source", adicionar novos campos

Consumidores devem ignorar versões desconhecidas gracefully.

---

## Dead Letter Queue (DLQ)

Mensagens que:
- Falham schema validation
- Event type desconhecido
- Erro ao processar (retry esgotado)

...são movidas para fila `ls.commands.dlq` para análise.

---

## Teste Manual (para dev)

```bash
# Conectar ao RabbitMQ
sudo rabbitmqctl list_exchanges
sudo rabbitmqctl list_queues

# Publicar mensagem de teste
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
    "payload": {"reason": "manual_test"}
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
