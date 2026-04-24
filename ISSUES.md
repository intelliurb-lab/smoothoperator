# 🐛 SmoothOperator - Bug Report

Relatório de bugs encontrados durante auditoria de código.

---

## 🚨 BUGS CRÍTICOS

### 1. ⚠️ Thread-Safety Issue com `std::gmtime()`
**Severidade:** CRÍTICO  
**Arquivo:** `src/core/state_manager.cpp` (linhas 63, 115)  
**Tipo:** Thread Safety

**Problema:**
```cpp
ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
```

O `std::gmtime()` retorna um ponteiro para uma struct estática que pode ser sobrescrita por chamadas concorrentes. Se o polling timer chamar `get_current_utc_time()` enquanto `handle_dj_command()` está processando, há **race condition**.

**Como Corrigir:**
```cpp
// Use std::gmtime_r (POSIX) ou melhor ainda, use chrono:
std::string StateManager::get_current_utc_time() {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);  // C++20
    // ou
    return fmt::format("{:%Y-%m-%dT%H:%M:%SZ}", now);  // com {fmt}
}
```

---

### 2. 🚨 Command Injection Vulnerability
**Severidade:** CRÍTICO  
**Arquivo:** `src/core/state_manager.cpp` (linhas 82, 86)  
**Tipo:** Security - Command Injection

**Problema:**
```cpp
std::string uri = data.at("uri");
stream_provider_->execute(commands_.playlist_set_uri + " " + uri);  // ❌ Injection!
```

Se o `uri` contiver caracteres especiais, quebras de linha ou comandos Telnet, é possível injetar comandos maliciosos.

**Exemplo de Ataque:**
```json
{"uri": "playlist.m3u\nserver.shutdown"}
```

**Como Corrigir:**
```cpp
// Escape special characters for Telnet protocol
std::string escape_telnet_arg(const std::string& arg) {
    std::string result;
    for (char c : arg) {
        if (c == '\r' || c == '\n' || c == '"') {
            continue;  // ou escape
        }
        result += c;
    }
    return result;
}

// Depois usar:
std::string uri = escape_telnet_arg(data.at("uri"));
std::string path = escape_telnet_arg(path);
stream_provider_->execute(commands_.playlist_set_uri + " " + uri);
```

---

### 3. 🔴 Dangling Reference em AmqpEventBus
**Severidade:** CRÍTICO  
**Arquivo:** `src/drivers/rabbitmq_driver.cpp` (linha 6)  
**Tipo:** Memory Safety

**Problema:**
```cpp
AmqpEventBus::AmqpEventBus(AMQP::Channel& channel, const std::string& exchange)
    : channel_(channel), exchange_(exchange) {}  // ❌ Armazena referência
```

E em `main.cpp` (linha 81):
```cpp
AMQP::TcpChannel channel(&connection);  // Stack object
auto event_bus = std::make_shared<drivers::AmqpEventBus>(channel, ...);
```

Se o `channel` for destruído enquanto `event_bus` tentar usá-lo, **undefined behavior**.

**Como Corrigir:**
```cpp
// Opção 1: Armazenar shared_ptr (melhor)
// Em rabbitmq_driver.hpp
class AmqpEventBus {
private:
    std::shared_ptr<AMQP::Channel> channel_;  // ✅
};

// Em rabbitmq_driver.cpp
AmqpEventBus::AmqpEventBus(std::shared_ptr<AMQP::Channel> channel, const std::string& exchange)
    : channel_(std::move(channel)), exchange_(exchange) {}

// Opção 2: Em main.cpp, usar make_shared
auto channel = std::make_shared<AMQP::TcpChannel>(&connection);
auto event_bus = std::make_shared<drivers::AmqpEventBus>(channel, config.rabbitmq.exchange_name);
```

---

### 4. 🔴 Missing Error Handling After Command Execution
**Severidade:** CRÍTICO  
**Arquivo:** `src/core/state_manager.cpp` (linhas 82-83)  
**Tipo:** Reliability

**Problema:**
```cpp
stream_provider_->execute(commands_.playlist_set_uri + " " + uri);  // Ignora erro
stream_provider_->execute(commands_.playlist_reload);  // Pode falhar se anterior falhou
```

Se `playlist_set_uri` falhar, `playlist_reload` tenta recarregar uma playlist que não foi alterada. Sem tratamento de erro, o estado fica inconsistente.

**Como Corrigir:**
```cpp
auto res1 = stream_provider_->execute(commands_.playlist_set_uri + " " + uri);
if (std::holds_alternative<std::error_code>(res1)) {
    std::cerr << "Failed to set playlist URI: " << std::get<std::error_code>(res1).message() << std::endl;
    return;
}

auto res2 = stream_provider_->execute(commands_.playlist_reload);
if (std::holds_alternative<std::error_code>(res2)) {
    std::cerr << "Failed to reload playlist: " << std::get<std::error_code>(res2).message() << std::endl;
}
```

---

## 🟡 BUGS DE MÉDIA SEVERIDADE

### 5. Unbounded Response Buffer (DoS Risk)
**Severidade:** MÉDIO/ALTO  
**Arquivo:** `src/drivers/liquidsoap_driver.cpp` (linhas 64-75)  
**Tipo:** DoS - Memory Exhaustion

**Problema:**
```cpp
std::string response;
while (true) {
    ssize_t n = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
    // ...
    response += buffer;  // ❌ Sem limite de tamanho!
    if (response.find("END\r\n") != std::string::npos) break;
}
```

Um Liquidsoap corrompido ou atacante pode enviar dados ilimitados, exaurindo memória e causando denial-of-service.

**Como Corrigir:**
```cpp
const size_t MAX_RESPONSE_SIZE = 1024 * 1024;  // 1MB limit
std::string response;

while (true) {
    if (response.size() > MAX_RESPONSE_SIZE) {
        disconnect();
        return std::make_error_code(std::errc::message_size);
    }
    
    ssize_t n = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        disconnect();
        return std::make_error_code(std::errc::connection_aborted);
    }
    
    buffer[n] = '\0';
    response += buffer;
    if (response.find("END\r\n") != std::string::npos || response.find("END\n") != std::string::npos) {
        break;
    }
}
```

---

### 6. Lost Messages on JSON Parse Error
**Severidade:** MÉDIO  
**Arquivo:** `src/main.cpp` (linhas 107-114)  
**Tipo:** Data Loss

**Problema:**
```cpp
try {
    auto payload = nlohmann::json::parse(body);
    state_manager->handle_dj_command(routing_key, payload);
} catch (const std::exception& e) {
    std::cerr << "[Main] Error processing message: " << e.what() << std::endl;
}
channel.ack(deliveryTag);  // ❌ Confirma mesmo se falhou!
```

Se JSON parse falhar, a mensagem é **acusada e perdida silenciosamente**. Deveria ir para Dead Letter Queue ou ser requeued.

**Como Corrigir:**
```cpp
try {
    auto payload = nlohmann::json::parse(body);
    state_manager->handle_dj_command(routing_key, payload);
    channel.ack(deliveryTag);  // ✅ Só ack se sucesso
} catch (const nlohmann::json::parse_error& e) {
    std::cerr << "[Main] JSON parse error on message: " << e.what() << std::endl;
    channel.nack(deliveryTag, true);  // ✅ Requeue ou enviar para DLQ
} catch (const std::exception& e) {
    std::cerr << "[Main] Error processing message: " << e.what() << std::endl;
    channel.nack(deliveryTag, false);  // ✅ Não requeue (erro de lógica)
}
```

---

### 7. Potential Race Condition on State Access
**Severidade:** MÉDIO  
**Arquivo:** `src/main.cpp` (linhas 103-115, 124-128) e `src/core/state_manager.cpp`  
**Tipo:** Thread Safety - Data Race

**Problema:**
O timer de polling (linha 125-128) chama `sm->poll()` que modifica `state_`, enquanto simultaneamente uma mensagem RabbitMQ pode chamar `handle_dj_command()` que também acessa `state_`. Sem lock, há **data race**.

O libevent é single-threaded, mas as callbacks podem ser chamadas em sequências inesperadas, causando acesso concorrente a `state_`.

**Como Corrigir:**
```cpp
// Em include/smoothoperator/state_manager.hpp
#include <mutex>

class StateManager {
private:
    mutable std::mutex state_mutex_;
    GlobalState state_;
    
public:
    void poll() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        update_status();
        update_metadata();
    }
    
    void handle_dj_command(const std::string& intent, const nlohmann::json& payload) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        // ... resto do código
    }
    
    nlohmann::json get_state_json() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        // ... resto do código
    }
};
```

---

## 📊 Resumo por Severidade

| # | Bug | Severidade | Tipo |
|---|-----|-----------|------|
| 1 | gmtime() race condition | 🚨 CRÍTICO | Thread Safety |
| 2 | Command Injection | 🚨 CRÍTICO | Security |
| 3 | Dangling Reference | 🔴 CRÍTICO | Memory Safety |
| 4 | Missing Error Handling | 🔴 CRÍTICO | Reliability |
| 5 | Unbounded Buffer | 🟡 MÉDIO/ALTO | DoS/Memory |
| 6 | Lost Messages | 🟡 MÉDIO | Data Loss |
| 7 | Race on State | 🟡 MÉDIO | Thread Safety |

---

## ✅ Prioridade de Correção

1. **IMEDIATO:** Bugs 2, 3, 4 (Segurança e Memory Safety)
2. **URGENTE:** Bugs 1, 5, 7 (Thread safety e DoS)
3. **IMPORTANTE:** Bug 6 (Data Loss)

**Recomendação:** Não deployar em produção até corrigir os bugs críticos.

---

**Relatório gerado:** 2026-04-24  
**Versão auditada:** SmoothOperator v1.0.1
