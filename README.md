# Memphis — Controlador Inteligente Intelliurb FM

**Memphis** é o novo daemon de controle para Intelliurb FM, responsável por orquestrar o Liquidsoap via fila de mensagens RabbitMQ. Substitui a comunicação frágil telnet direto com um bus de eventos robusto, permitindo auditoria, retry automático e múltiplos consumidores.

## Visão Geral

```
RabbitMQ (bus de eventos)
    ↓ consome
Memphis (este daemon, em C)
    ↓ comando TCP persistente
Liquidsoap (motor de áudio)
```

Memphis é **a ponte única entre RabbitMQ e Liquidsoap**, garantindo:
- ✅ Conexão TCP persistente (sem overhead de setup/teardown)
- ✅ Validação rigorosa de mensagens (schema JSON)
- ✅ Retry automático com exponential backoff
- ✅ Health check HTTP (integração systemd)
- ✅ Logging estruturado para auditoria
- ✅ Sem dependências pesadas (C puro, libaries mínimas)

## Quick Start

### Pré-requisitos

```bash
# Ubuntu/Debian
sudo apt-get install -y build-essential cmake librabbitmq-dev libjansson-dev libcunit1-dev

# macOS (Homebrew)
brew install cmake librabbitmq jansson cunit
```

### Build

```bash
cd /opt/radio/memphis

# Modo debug (com symbols, sem otimização)
make debug

# Modo release (otimizado)
make release

# Rodar testes
make test

# Coverage report
make coverage
```

### Executar

```bash
# Modo standalone (logs em stdout)
./build/memphis --config config.env

# Systemd
sudo systemctl start memphis
sudo systemctl status memphis
```

## Arquitetura

Ver [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) para design detalhado, diagramas de fluxo e decisões técnicas.

## API de Mensagens

Ver [`docs/API.md`](docs/API.md) para schema JSON, exemplos e protocolo Liquidsoap.

## Testes

Memphis usa **Critério** para testes em C.

```bash
# Rodar todos os testes
make test

# Rodar com verbose
./build/tests --verbose

# Coverage (requer gcov)
make coverage
```

Tests cobrem:
- ✅ Unit: parsing JSON, validação schema
- ✅ Integration: consumer RabbitMQ, client Liquidsoap
- ✅ Stability: reconnection, health check

## Logging

Logs vão para `/var/log/memphis.log` (ou stdout em debug).

Estrutura:
```json
{
  "timestamp": "2026-04-20T10:30:00Z",
  "level": "INFO",
  "module": "rabbitmq_consumer",
  "message": "consumed event from queue",
  "event_id": "01HWX5K2M8QPQZ",
  "event_type": "control.skip"
}
```

## Código-fonte

- `src/main.c` — Entry point, CLI args, signal handlers
- `src/ls_controller.c` — Core logic, event loop
- `src/rabbitmq_consumer.c` — Consumer loop, ack/nack
- `src/liquidsoap_client.c` — Socket TCP persistente ao LS
- `src/config.c` — Load environment, config validation
- `src/message.c` — JSON parsing, schema validation
- `test/` — Testes (Critério)

## Contribuindo

- **Code style**: `clang-format` (`.clang-format` no root)
- **Before push**: `make lint test coverage`
- **Commits**: atômicos, mensagens descritivas
- **PRs**: uma feature/fix por PR, com testes

## Status

| Feature | Status |
|---------|--------|
| Consumer RabbitMQ | 🟡 In Progress |
| Client Liquidsoap (persistente) | 🟡 In Progress |
| Validação schema JSON | 🟡 In Progress |
| Health check HTTP | ⚪ Backlog |
| Logging estruturado | ⚪ Backlog |
| Systemd integration | ⚪ Backlog |

## License

**BSD 2-Clause License** — See [LICENSE](LICENSE) file

Memphis is open source and freely available for commercial and personal use.

## Contato

`intelliurb@gmail.com`
