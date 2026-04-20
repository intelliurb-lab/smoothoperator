# Getting Started — Memphis Development

## 1. Dependências

Memphis precisa de:
- `cmake` (build system)
- `gcc` ou `clang` (C compiler)
- `librabbitmq-dev` (RabbitMQ client)
- `libjansson-dev` (JSON parser)
- `criterion-dev` (test framework)

### Instalar (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  librabbitmq-dev \
  libjansson-dev \
  libcunit1-dev \
  pkg-config
```

### Instalar (macOS)

```bash
brew install cmake librabbitmq jansson cunit
```

---

## 2. Setup RabbitMQ

```bash
# Instalar RabbitMQ
sudo apt-get install -y rabbitmq-server
sudo systemctl start rabbitmq-server

# Criar usuário e permissões
sudo rabbitmqctl add_user memphis memphis123
sudo rabbitmqctl set_permissions -p / memphis '.*' '.*' '.*'

# Criar exchange, fila, bindings
sudo rabbitmq-admin declare exchange \
  name=radio.events type=topic durable=true
sudo rabbitmq-admin declare queue \
  name=ls.commands durable=true
sudo rabbitmq-admin declare binding \
  source=radio.events destination=ls.commands \
  routing_key="control.*"
sudo rabbitmq-admin declare binding \
  source=radio.events destination=ls.commands \
  routing_key="announcement.*"
```

---

## 3. Build Memphis

```bash
cd ~/src/ls-controller  # ou /opt/radio/memphis

# Build debug (com símbolos, sem otimização)
make debug

# Ou build release (otimizado)
make release

# Testar compilação
ls build/bin/memphis  # binary pronto
```

---

## 4. Testes

```bash
# Rodar testes (requer Criterion)
make test

# Com coverage
make coverage
# Abrir build/coverage_html/index.html

# Lint (formatting)
make lint
make format  # auto-fix
```

---

## 5. Estrutura do Código

```
src/
  ├── main.c                    Entry point, config loading, signal handling
  ├── config.c                  Config from env vars, defaults, validation
  ├── message.c                 JSON parsing, schema validation
  ├── liquidsoap_client.c       TCP socket (persistente) ao Liquidsoap
  ├── rabbitmq_consumer.c       Consumer loop, ack/nack
  ├── ls_controller.c           Event routing, command dispatch
  └── memphis_logging.c         JSON structured logging

include/
  ├── memphis.h                 Main header, types, logging API
  ├── config.h
  ├── message.h
  ├── liquidsoap_client.h
  ├── rabbitmq_consumer.h
  └── ls_controller.h

test/
  ├── test_runner.c             Test discovery (Critério)
  ├── test_message.c            JSON parsing tests
  ├── test_config.c             Config loading tests
  └── test_liquidsoap_client.c  Socket tests
```

---

## 6. Fluxo de Desenvolvimento

### Desenvolvimento local
```bash
cd ~/src/ls-controller

# Edit código
vim src/my_feature.c

# Compilar e testar
make debug test

# Coverage
make coverage

# Antes de push
make lint format test
```

### Adicionar feature nova

1. **Escrever teste primeiro** (TDD)
   ```c
   // test/test_my_feature.c
   Test(my_feature, should_do_x) {
     cr_assert_eq(result, expected);
   }
   ```

2. **Implementar feature** (fazer teste passar)
   ```c
   // src/my_feature.c
   result_t my_feature(args) {
     // implementação
   }
   ```

3. **Rodar testes**
   ```bash
   make test
   ```

4. **Lint e format**
   ```bash
   make format lint
   ```

5. **Commit**
   ```bash
   git add src/ test/ include/
   git commit -m "feat: add my_feature"
   ```

---

## 7. Checklist de Antes de Compilar em Produção

- [ ] Todos testes passam (`make test`)
- [ ] Coverage aceitável (`make coverage`)
- [ ] Lint passa (`make lint`)
- [ ] Código formatado (`make format`)
- [ ] README atualizado
- [ ] Config exemplo atualizado (memphis.env.example)
- [ ] Integração com Liquidsoap testada manualmente
- [ ] Integração com RabbitMQ testada manualmente

---

## 8. Próximos Passos (Roadmap v1.0)

### AC1: RabbitMQ Connection
- [x] Header files e tipos
- [ ] Implementar `rabbitmq_consumer_create()`
- [ ] Implementar `rabbitmq_consumer_run()` (blocking loop)
- [ ] Tests: conexão, desconexão, reconnect
- **Acceptance Test**: Consumer conecta e consome mensagem válida, publica ack

### AC2: Message Validation
- [x] JSON parsing com jansson
- [x] Schema validation basics
- [ ] Validação completa (todos campos obrigatórios)
- [ ] Tests: válido, inválido, malformado
- **Acceptance Test**: Mensagem inválida vai pra DLQ, válida é processada

### AC3: Liquidsoap Socket
- [x] Socket creation / reconnect
- [ ] Send command completo
- [ ] Parse response
- [ ] Keepalive TCP
- [ ] Tests: socket aberto, comando enviado, resposta recebida
- **Acceptance Test**: Enviar "next", LS responde OK, socket permanece aberto

### AC4: Event Routing
- [ ] Map event type → LS command
- [ ] Handle control.skip, control.shutdown, announcement.push
- [ ] Formatar resposta
- [ ] Tests: cada event type, happy path e error path
- **Acceptance Test**: Consumir skip event, LS recebe "next", resposta publicada

### AC5: Reconnection
- [ ] Exponential backoff para RabbitMQ
- [ ] Exponential backoff para Liquidsoap
- [ ] Tests: simular desconexão, verificar reconnect
- **Acceptance Test**: Derrubar RabbitMQ, controller tenta reconnect, volta quando online

### AC6: Health Check HTTP
- [ ] HTTP listener na porta 9000
- [ ] GET /healthz retorna status
- [ ] Integração com systemd (systemd-notify)
- [ ] Tests: healthz response

### AC7: Logging Estruturado
- [x] Função `log_msg()` em JSON
- [ ] Testar logging em arquivo
- [ ] Testar logging em stdout
- **Acceptance Test**: Evento processado gera log JSON com event_id

---

## 9. Debugging

### Compilar com debug symbols
```bash
make debug  # CFLAGS=-g -O0 --coverage
```

### Rodare com gdb
```bash
gdb build/bin/memphis
(gdb) run
(gdb) bt  # backtrace
```

### Ver logs em tempo real
```bash
tail -f /var/log/memphis.log | jq .
```

### Monitorar RabbitMQ
```bash
sudo rabbitmqctl list_queues
sudo rabbitmqctl purge_queue ls.commands  # limpar fila (CUIDADO!)
```

---

## 10. FAQ

**P: Preciso instalar CUnit para rodar testes?**
R: Sim, `libcunit1-dev` (Ubuntu) ou `cunit` (Homebrew). É leve e sempre disponível.

**P: Devo usar C puro ou devo usar uma biblioteca maior?**
R: C puro + librabbitmq + jansson + cunit é a escolha. Mínimo de dependências.

**P: Como testoManualmente?**
R: Vê `docs/API.md` para exemplos de publicar mensagens no RabbitMQ.

**P: Como debugar a comunicação com Liquidsoap?**
R: `telnet 127.0.0.1 1234` e mande comandos manualmente.

---

## Próxima: [ARCHITECTURE.md](docs/ARCHITECTURE.md)
