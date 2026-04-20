# SmoothOperator — Liquidsoap Transport & Command Coverage Plan

Detailed plan for:

1. Adding Unix domain socket transport alongside the existing TCP ("telnet") transport, selectable via configuration.
2. Expanding RabbitMQ event coverage so every Liquidsoap server command can be driven from the queue.
3. Fixing two correctness/safety bugs discovered while auditing the current implementation.

---

## Table of Contents

1. [Goals & Non-Goals](#goals--non-goals)
2. [Current State (Evidence)](#current-state-evidence)
3. [Bugs Discovered](#bugs-discovered)
4. [Phase 1 — Transport Abstraction](#phase-1--transport-abstraction)
5. [Phase 2 — Response Protocol Fix](#phase-2--response-protocol-fix)
6. [Phase 3 — Full Command Coverage](#phase-3--full-command-coverage)
7. [Phase 4 — Raw Command Gateway (optional)](#phase-4--raw-command-gateway-optional)
8. [Security Analysis](#security-analysis)
9. [Backward Compatibility & Risk Matrix](#backward-compatibility--risk-matrix)
10. [Test Strategy](#test-strategy)
11. [Rollout Plan](#rollout-plan)
12. [Open Questions](#open-questions)

---

## Goals & Non-Goals

### Goals

- **G1.** Allow operators to choose transport at deploy time (`telnet` TCP vs. Unix domain socket).
- **G2.** Expose the full Liquidsoap server command surface through well-defined RabbitMQ events (structured per-command handlers, not a raw passthrough by default).
- **G3.** Fix the response protocol so responses are consumed completely (critical for Phase 3 commands that return data).
- **G4.** Preserve backward compatibility: existing `control.skip`, `control.shutdown`, `announcement.push` events continue to work with no config change.
- **G5.** Fail closed on invalid or ambiguous configuration; never silently degrade security.

### Non-Goals (explicitly out of scope)

- **NG1.** HTTP / web / SSH interfaces to Liquidsoap (deferred).
- **NG2.** Path-injection hardening for `announcement.push` (separate follow-up, flagged in [Bug 2](#bug-2--path-injection-in-announcementpush)).
- **NG3.** Changes to the RabbitMQ wire schema version (still `version: 1`).
- **NG4.** Authentication on the telnet transport (Liquidsoap does not support it natively; use Unix socket or SSH wrapper instead).

---

## Current State (Evidence)

### Transport — TCP only

`src/liquidsoap_client.c` hardcodes AF_INET with SOCK_STREAM:

- L82: `sock->fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);`
- L115–141: `getaddrinfo` + loop of `connect()` over IPv4/IPv6 results.
- L102–113: `SO_KEEPALIVE`, `TCP_NODELAY` set unconditionally (TCP-only options).

`include/liquidsoap_client.h:17-18`:

```c
ls_socket_t *ls_socket_create(const char *host, uint16_t port,
                               uint32_t timeout_ms);
```

Constructor signature assumes TCP. No Unix-socket path.

### Events routed — 3 total

`src/ls_controller.c:47-72`:

```c
if (strcmp(event, "control.skip") == 0) {
  command = "next";
} else if (strcmp(event, "control.shutdown") == 0) {
  command = "shutdown";
} else if (strcmp(event, "announcement.push") == 0) {
  // ... extracts filepath from payload, builds "announcements.push <filepath>"
} else {
  return RESULT_INVALID;
}
```

`docs/API.md` also references `playlist.change` as a planned event, but it is **not** implemented in the router.

### Configuration — env-based, no protocol selector

`include/config.h:7-28` — struct has `liquidsoap_host` (string), `liquidsoap_port` (uint16), `liquidsoap_timeout_ms`, `liquidsoap_reconnect_max_delay_ms`. No protocol field, no socket path.

`src/config.c:65-68` loads only host/port/timeouts. `config_is_valid` at L97-104 requires both host and port.

### Command sanitisation

`src/liquidsoap_client.c:25-36` — `is_safe_ls_arg` rejects `\n`, `\r`, `\0`, non-printable (except `\t`), empty strings, strings > 1024 chars. Applied to the *final* command string in `ls_send_command` (L186). Good for preventing line-injection into the telnet protocol, but does not enforce semantic restrictions (e.g., filesystem path rules).

---

## Bugs Discovered

### Bug 1 — Response protocol is incomplete

**Location:** `src/liquidsoap_client.c:213` inside `ls_send_command`.

**What the code does:**

```c
char *response = ls_recv_line(sock->fd, 256);   // reads exactly one line
/* ... */
if (strncmp(response, "OK", 2) == 0) {
  resp->ok = true;
  ...
} else {
  resp->ok = false;
  ...
}
```

`ls_recv_line` (L155-180) reads byte-by-byte until the first `\n`, returning a single line.

**Why it is wrong:**

Liquidsoap's telnet / command-server protocol terminates *every* response with a line containing only `END`. A `next` command on a typical source produces on the wire:

```
Done.\r\n
END\r\n
```

The current implementation reads `Done.`, returns, and leaves `END\r\n` in the socket receive buffer. On the **next** command, the first `ls_recv_line` call returns `"END"` (the leftover), which does not start with `"OK"`, so `resp->ok` is set to false and `resp->message` becomes `"END"`. The *real* response of the second command is then consumed by the third, and so on — the protocol state machine is permanently off by one.

**Why it hasn't surfaced badly yet:**

- `next` / `shutdown` / `announcements.push` all return a single status line plus `END`. The "off-by-one" means we're always reading the *previous* command's `END` as the current command's response. Consumers only check `resp->ok`, so functional behavior is degraded but not catastrophic: commands still get *sent*, just acknowledged with stale/wrong responses.
- `shutdown` closes the connection server-side, which accidentally resets the state machine.
- Liquidsoap is tolerant of extra newlines in command input.

**Why we must fix it before Phase 3:**

Any getter command (`<source>.metadata`, `var.get`, `request.queue`) returns multi-line bodies followed by `END`. Without `END`-loop handling we would read only the first line of output and leak the remainder into the next command's response.

**Fix scope:** [Phase 2](#phase-2--response-protocol-fix).

### Bug 2 — Path injection in `announcement.push`

**Location:** `src/ls_controller.c:62-69`.

**What the code does:**

```c
const char *filepath = json_string_value(filepath_obj);
static char cmd_buf[512];
int n = snprintf(cmd_buf, sizeof(cmd_buf), "announcements.push %s", filepath);
```

`filepath` comes verbatim from the JSON payload. The only validation is the generic `is_safe_ls_arg` applied downstream (rejects `\n`, `\r`, `\0`, non-printable).

**Why it is a problem:**

A RabbitMQ producer with publish access to `radio.events` can push *any* file Liquidsoap's process can read (`/etc/passwd`, `/proc/self/environ`, arbitrary mp3/wav outside the announcements directory, etc.). Liquidsoap will attempt to load and play it. If the process runs with elevated privileges (e.g., reads `/etc/shadow` — unlikely but possible), content could be leaked via streaming output.

**Scope note:** Flagged here because it sits in the same file we'll be modifying in Phase 3, and expanding request events (`request.push`) without hardening this would broaden the blast radius. Fix itself is **out of scope for this plan** — tracked separately so this plan stays focused on transport + coverage. Recommended follow-up: allowlisted root directory (`ANNOUNCEMENTS_DIR`), `realpath()` + prefix check, reject symlinks.

---

## Phase 1 — Transport Abstraction

### 1.1 Configuration additions

**`include/config.h`** — add two fields to `config_t`:

```c
typedef enum {
  LS_PROTO_TELNET = 0,   /* default, TCP */
  LS_PROTO_SOCKET = 1    /* Unix domain socket */
} ls_protocol_t;

typedef struct {
  /* ... existing fields ... */
  ls_protocol_t liquidsoap_protocol;
  char         *liquidsoap_socket_path;  /* only used when protocol == SOCKET */
  /* ... */
} config_t;
```

**`src/config.c`** — new env var parsing:

| Env var | Type | Default | Notes |
|---|---|---|---|
| `LIQUIDSOAP_PROTOCOL` | string | `telnet` | Accepts `telnet`, `socket`. Any other value = config error. |
| `LIQUIDSOAP_SOCKET_PATH` | string | *unset* | Absolute filesystem path. Required iff protocol == `socket`. |

Parsing helper:

```c
static ls_protocol_t get_config_proto(const char *key, ls_protocol_t dflt) {
  const char *val = getenv(key);
  if (!val || !*val) return dflt;
  if (strcmp(val, "telnet") == 0) return LS_PROTO_TELNET;
  if (strcmp(val, "socket") == 0) return LS_PROTO_SOCKET;
  fprintf(stderr, "ERROR: LIQUIDSOAP_PROTOCOL must be 'telnet' or 'socket', got '%s'\n", val);
  return (ls_protocol_t)-1;  /* sentinel, caught in config_is_valid */
}
```

**`config_is_valid`** — extend with protocol-conditional checks:

```
if protocol == TELNET:
  require liquidsoap_host, liquidsoap_port
  reject liquidsoap_socket_path set (warn + fail, avoids operator confusion)
if protocol == SOCKET:
  require liquidsoap_socket_path absolute (starts with '/')
  reject liquidsoap_host, liquidsoap_port set (same rationale)
  require length(socket_path) <= 108  (AF_UNIX sun_path limit on Linux)
```

Fail-closed: any conflict returns false; `main.c` aborts startup.

**`memphis.env.example`** — update with commented examples for both modes.

### 1.2 Transport layer changes

**`include/liquidsoap_client.h`** — extend:

```c
typedef struct ls_socket ls_socket_t;

/* Backward-compatible: always creates a TCP connection. */
ls_socket_t *ls_socket_create(const char *host, uint16_t port,
                               uint32_t timeout_ms);

/* New: unix-domain socket connection. */
ls_socket_t *ls_socket_create_unix(const char *socket_path,
                                    uint32_t timeout_ms);

/* New: dispatch based on config. Preferred entry point going forward. */
ls_socket_t *ls_socket_create_from_config(const config_t *cfg);
```

**`src/liquidsoap_client.c`** — refactor `struct ls_socket`:

```c
struct ls_socket {
  int        fd;
  ls_protocol_t proto;
  /* TCP fields (used iff proto == TELNET) */
  char      *host;
  uint16_t   port;
  /* Unix fields (used iff proto == SOCKET) */
  char      *socket_path;
  /* common */
  uint32_t   timeout_ms;
  bool       connected;
};
```

`ls_reconnect` branches on `proto`:

- **TELNET branch**: current code (getaddrinfo loop + connect).
- **SOCKET branch**:
  1. `lstat` the path. If it fails → return false. If `S_ISLNK` → return false (reject symlinks to prevent swap-under-you attacks).
  2. `stat` the path (follow). If `!S_ISSOCK(st.st_mode)` → return false.
  3. Reject world-writable: `if (st.st_mode & S_IWOTH) return false;`
  4. `socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)`.
  5. Apply `SO_RCVTIMEO` / `SO_SNDTIMEO`. **Skip** `TCP_NODELAY` / `SO_KEEPALIVE` (nonsensical on AF_UNIX; setsockopt would error).
  6. Populate `struct sockaddr_un` with `sun_family = AF_UNIX` and `strncpy(sun_path, socket_path, sizeof(sun_path)-1)`.
  7. `connect()`. On failure → close and return false.

`ls_send_command` is untouched — protocol is line-based plain text either way.

`ls_socket_free` — also `free(sock->socket_path)`.

### 1.3 Controller wiring

**`src/ls_controller.c`** — `controller_create` switches to:

```c
ctrl->ls_sock = ls_socket_create_from_config(cfg);
```

No behavior change when `LIQUIDSOAP_PROTOCOL=telnet`.

### 1.4 Documentation updates

- `memphis.env.example`: add both modes with inline comments.
- `docs/API.md` section "Liquidsoap Protocol": document both transports and the selector.
- `ARCHITECTURE.md`: add a sentence in "System Overview" noting the transport is pluggable.
- `README.md`: short "choosing a transport" note with security guidance (prefer socket in prod).

### 1.5 Files touched in Phase 1

| File | Change |
|---|---|
| `include/config.h` | Add `ls_protocol_t` enum + 2 fields |
| `src/config.c` | Parse/validate new env vars; free new fields |
| `include/liquidsoap_client.h` | Add `ls_socket_create_unix` + `ls_socket_create_from_config` |
| `src/liquidsoap_client.c` | Struct refactor; socket branch in `ls_reconnect`; free path |
| `src/ls_controller.c` | Use `ls_socket_create_from_config` |
| `test/test_liquidsoap_client.c` | Add unix-socket tests |
| `test/test_config.c` | Add protocol-selector tests |
| `memphis.env.example` | New vars |
| `docs/API.md` | New transport section |
| `README.md` | Brief note |
| `ARCHITECTURE.md` | Brief note |

---

## Phase 2 — Response Protocol Fix

### 2.1 Liquidsoap protocol reference

Every command response on the telnet/socket interface has the form:

```
<body_line_1>\r\n
<body_line_2>\r\n
...
<body_line_n>\r\n
END\r\n
```

The `END` sentinel is a line containing **exactly** the three bytes `E`, `N`, `D`. Body can be zero lines (e.g., a successful `var.set`). Body can be very large (`var.list` with many variables, metadata dumps).

Status semantics vary per command — there is no universal `OK` prefix. The first body line is often the status for side-effectful commands (`Done.`, `shutdown`) and is the data itself for getters (e.g., the metadata JSON). `ERROR` strings appear in the body with no machine-readable prefix, so we cannot reliably determine success purely from text. We'll retain the current heuristic (first line starts with `OK` or `Done`) for `resp->ok` but also expose the full body so richer events can parse it.

### 2.2 Implementation

Extend `ls_response_t`:

```c
typedef struct {
  bool      ok;
  char     *message;     /* first body line, kept for backward compat */
  char     *body;        /* all body lines joined with '\n', NULL if empty */
  size_t    body_len;
  uint32_t  latency_ms;
} ls_response_t;
```

New helper `ls_recv_response` replaces the single `ls_recv_line` call:

```
const size_t MAX_BODY_BYTES = 64 * 1024;    /* hard cap */
const size_t MAX_LINE_BYTES = 4 * 1024;     /* raised from 256 */
const int    MAX_LINES      = 2048;         /* defensive */

buffer = growable_buffer_new()
for i in 0..MAX_LINES:
  line = ls_recv_line(fd, MAX_LINE_BYTES)
  if line == NULL: return error    /* timeout / disconnect */
  if strcmp(line, "END") == 0:
    free(line); break
  if buffer.len + strlen(line) + 1 > MAX_BODY_BYTES:
    free(line); return error       /* runaway response */
  if buffer.len > 0: append '\n'
  append line
  free(line)
return buffer.as_owned_string()
```

`resp->ok` heuristic:

```
first_line = body up to first '\n' (or whole body)
resp->ok = (strncmp(first_line, "OK", 2) == 0
         || strncmp(first_line, "Done", 4) == 0)
resp->message = strdup(first_line)
resp->body    = body  (transfer ownership)
```

### 2.3 Safety

- Hard caps prevent a hostile or broken Liquidsoap from OOM-ing the daemon.
- On cap exceeded, we disconnect the socket (mark `sock->connected = false`) to resynchronise on next command — the buffer state is unknowable at that point.
- `MAX_LINE_BYTES` raised because Liquidsoap's metadata lines can be >256 bytes.

### 2.4 Files touched in Phase 2

| File | Change |
|---|---|
| `include/liquidsoap_client.h` | Extend `ls_response_t` |
| `src/liquidsoap_client.c` | `ls_recv_response`, replace call in `ls_send_command`, update free |
| `test/test_liquidsoap_client.c` | Multi-line response tests, `END` sentinel tests, oversize tests |

### 2.5 Backward compatibility

Existing callers (`src/ls_controller.c:77-86`) use only `resp->ok` and (indirectly) `resp->message`. `resp->body` is additive. No call-site changes required in Phase 2.

---

## Phase 3 — Full Command Coverage

### 3.1 Approach — structured events over raw passthrough

Each new event gets a dedicated router branch and its own payload validator. This is deliberately more verbose than a generic `ls.command` passthrough, but:

- Per-event schemas are self-documenting.
- Attack surface is narrower: each handler accepts only the fields it needs.
- Future schema v2 migrations are localized.

A raw-command gateway is available as opt-in [Phase 4](#phase-4--raw-command-gateway-optional).

### 3.2 Event catalog

For each: RabbitMQ event name, payload schema, resulting Liquidsoap command, response handling.

#### 3.2.1 Source controls

| Event | Payload | LS command | Notes |
|---|---|---|---|
| `source.skip` | `{ source: string }` | `<source>.skip` | Generalises existing `control.skip` (which maps to `next`, a fixed source). `source` must match `^[a-zA-Z0-9_]+$`. |
| `source.metadata.get` | `{ source: string }` | `<source>.metadata` | Response body is metadata key=value lines; returned to RabbitMQ as `ls.response` with body. |
| `source.remaining` | `{ source: string }` | `<source>.remaining` | Body is a float seconds. |

#### 3.2.2 Request / queue controls

| Event | Payload | LS command | Notes |
|---|---|---|---|
| `request.push` | `{ queue: string, uri: string }` | `<queue>.push <uri>` | Generalises `announcement.push`. `queue` regex-validated; `uri` goes through `is_safe_ls_arg` **plus** (future) allowlist — see Bug 2. |
| `request.queue.list` | `{ queue: string }` | `<queue>.queue` | |
| `request.on_air` | `{}` | `request.on_air` | |
| `request.alive` | `{}` | `request.alive` | |
| `request.metadata` | `{ rid: integer }` | `request.metadata <rid>` | `rid` validated as non-negative int32. |
| `request.trace` | `{ rid: integer }` | `request.trace <rid>` | |

#### 3.2.3 Interactive variables

| Event | Payload | LS command | Notes |
|---|---|---|---|
| `var.list` | `{}` | `var.list` | |
| `var.get` | `{ name: string }` | `var.get <name>` | `name` regex `^[a-zA-Z_][a-zA-Z0-9_]*$`. |
| `var.set` | `{ name: string, value: string }` | `var.set <name> = <value>` | `value` length-capped (e.g., 1024), `is_safe_ls_arg`. |

#### 3.2.4 Output controls

| Event | Payload | LS command | Notes |
|---|---|---|---|
| `output.start` | `{ output: string }` | `<output>.start` | |
| `output.stop` | `{ output: string }` | `<output>.stop` | |

#### 3.2.5 Playlist controls

| Event | Payload | LS command | Notes |
|---|---|---|---|
| `playlist.reload` | `{ playlist: string }` | `<playlist>.reload` | |
| `playlist.uri` | `{ playlist: string, uri?: string }` | `<playlist>.uri` (get) or `<playlist>.uri <uri>` (set) | If `uri` absent → getter; present → setter. |

#### 3.2.6 Server introspection

| Event | Payload | LS command |
|---|---|---|
| `server.uptime` | `{}` | `uptime` |
| `server.version` | `{}` | `version` |
| `server.list` | `{}` | `list` |
| `server.help` | `{ command?: string }` | `help` or `help <command>` |

### 3.3 Shared helpers

Add to `src/ls_controller.c` (or a new `src/ls_events.c`):

```c
/* Validates an identifier against a restrictive regex. */
static bool is_valid_identifier(const char *s, size_t max_len);

/* Extracts a required string field from payload. */
static bool payload_get_string(json_t *payload, const char *key,
                                const char **out, size_t max_len);

/* Extracts a required int64 field. */
static bool payload_get_int(json_t *payload, const char *key, int64_t *out);

/* Formats command into caller-owned buffer; returns false if too long. */
static bool format_cmd(char *buf, size_t buflen, const char *fmt, ...);
```

Each event handler follows the same pattern:

```c
static result_t handle_source_skip(controller_t *ctrl, json_t *payload) {
  const char *source = NULL;
  if (!payload_get_string(payload, "source", &source, 64)) return RESULT_INVALID;
  if (!is_valid_identifier(source, 64))                    return RESULT_INVALID;

  char cmd[256];
  if (!format_cmd(cmd, sizeof(cmd), "%s.skip", source))    return RESULT_INVALID;

  ls_response_t *resp = ls_send_command(ctrl->ls_sock, cmd);
  if (!resp) { ctrl->healthy = false; return RESULT_RETRY; }

  result_t r = resp->ok ? RESULT_OK : RESULT_ERROR;
  ls_response_free(resp);
  return r;
}
```

A dispatch table replaces the current `if/else` chain:

```c
typedef result_t (*event_handler_fn)(controller_t *, json_t *payload);

static const struct {
  const char *event;
  event_handler_fn fn;
} EVENT_TABLE[] = {
  { "control.skip",          handle_control_skip },       /* legacy */
  { "control.shutdown",      handle_control_shutdown },   /* legacy */
  { "announcement.push",     handle_announcement_push },  /* legacy */
  { "source.skip",           handle_source_skip },
  /* ... */
  { NULL, NULL }
};
```

### 3.4 Response emission back to RabbitMQ (optional within Phase 3)

For getter commands (`var.get`, `*.metadata`, `request.queue.list`), the `resp->body` must be published back. `docs/API.md` already documents `ls.response` events — the current code path doesn't publish them yet; this plan treats response publishing as a sub-phase (3.b) that can land after 3.a (handlers exist but do not echo bodies).

### 3.5 Documentation

- Full event catalog in `docs/API.md` with request/response examples for each.
- Update `ARCHITECTURE.md` Event Routing diagram.

### 3.6 Files touched in Phase 3

| File | Change |
|---|---|
| `src/ls_controller.c` | Dispatch table, per-event handlers |
| `src/ls_events.c` (new) | Optional split if controller grows too large |
| `include/ls_controller.h` | No public change |
| `docs/API.md` | Full event catalog |
| `ARCHITECTURE.md` | Updated routing diagram |
| `test/test_ls_controller.c` (new) | One test per event handler |

---

## Phase 4 — Raw Command Gateway (optional)

Off by default. For operators who need to issue ad-hoc commands without waiting for a new structured event to ship.

### 4.1 Gating

- Env `LIQUIDSOAP_ALLOW_RAW_COMMAND` (default `false`).
- Event `ls.command.raw` with payload `{ command: string }`.
- If env is false and the event arrives, handler returns `RESULT_INVALID` and logs at `WARN` with event id.

### 4.2 Validation

- Length ≤ 1024.
- Passes `is_safe_ls_arg`.
- Additional deny-list: reject commands starting with `shutdown`, `exit`, `quit` unless the message also carries `payload.allow_shutdown: true`. (Defence in depth — operators should use `control.shutdown` for intentional shutdowns so it shows up in audit logs under the well-known event name.)

### 4.3 Audit logging

Every invocation logs `module=raw_command level=WARN` with: event id, source, command, response ok/err, latency. This is the only code path that logs the command verbatim.

### 4.4 Files touched in Phase 4

| File | Change |
|---|---|
| `include/config.h` + `src/config.c` | Add `bool liquidsoap_allow_raw_command` |
| `src/ls_controller.c` | Handler for `ls.command.raw` |
| `docs/API.md` | Section with strong "use with care" framing |

---

## Security Analysis

| Concern | Mitigation |
|---|---|
| Command injection via payload fields (`\n`, `\r`, `\0`) | Existing `is_safe_ls_arg` (line 25). Keep and apply to every composed command. |
| Unix socket symlink swap | `lstat` + reject `S_ISLNK`. Open via `O_NOFOLLOW`-equivalent semantics (connect on AF_UNIX doesn't follow symlinks if we `lstat` first and bail). |
| World-writable socket | Reject sockets with `S_IWOTH`. |
| Socket path too long for `sun_path` | Validate length ≤ 108 in config. |
| OOM from runaway LS response | `MAX_BODY_BYTES = 64 KiB`, `MAX_LINES = 2048`. |
| Off-by-one state machine after bad response | On any parse failure disconnect and reconnect to resynchronise. |
| Accidental shutdown via raw gateway | Phase 4 deny-list for `shutdown`/`exit`/`quit` unless explicit flag. |
| Path injection in `announcement.push` / `request.push` | Flagged as Bug 2; not solved here; followup to add allowlisted root with `realpath` prefix check. |
| Audit trail tampering | Logs are append-only JSON with `O_NOFOLLOW` already. Raw-command log adds structured fields. |
| Insecure default | Default remains telnet for backward compat. README recommends socket for production. |

---

## Backward Compatibility & Risk Matrix

| Change | Risk of breaking existing deployment | Why |
|---|---|---|
| Phase 1: new config vars | None | Optional; defaults preserve telnet behavior. |
| Phase 1: transport factory | None | `ls_socket_create` retained as facade. |
| Phase 1: config validation rejects ambiguous combos | Low | Existing deployments only set host+port → still valid. Fails only if operator *also* sets `LIQUIDSOAP_SOCKET_PATH`, which is new. |
| Phase 2: response protocol fix | **Medium** | Changes semantics of `resp->ok` for commands whose first line wasn't `OK`-prefixed. Mitigation: also accept `Done` prefix; integration test against real Liquidsoap before merge. |
| Phase 3: new events | None | Pure additions; unknown events still return `RESULT_INVALID`. |
| Phase 3: dispatch table refactor | Low | Same behaviour for the three legacy events; add unit tests covering each. |
| Phase 4: raw gateway | None while disabled | Off by default. When enabled, auditor-visible log. |

---

## Test Strategy

### Unit (CUnit, existing framework)

- **Phase 1**
  - `test_config`: `LIQUIDSOAP_PROTOCOL=telnet` without path → valid; `=socket` without path → invalid; both path and host set → invalid; unknown protocol → invalid; path > 108 chars → invalid.
  - `test_liquidsoap_client`: unix-socket fixture in `/tmp` (use `socketpair` or a minimal `accept` thread in setUp); assert connect succeeds; assert send/recv roundtrip.
- **Phase 2**
  - Mock server returns: (a) single `OK\nEND\n`, (b) multi-line body + `END`, (c) body without `END` (timeout), (d) oversized body (> 64 KiB) — each exercised.
- **Phase 3**
  - One test per handler: valid payload → correct LS command emitted (use mock client that records the command string); invalid payload (missing field, wrong type, bad identifier) → `RESULT_INVALID`; transport failure → `RESULT_RETRY`.
- **Phase 4**
  - Raw command rejected when env unset; accepted + logged when env set; `shutdown` rejected without allow flag.

### Integration (new, optional CI job)

A `docker-compose.integration.yml` with: RabbitMQ + Liquidsoap (minimal script with `output.dummy()` and an interactive var) + smoothoperator. Publish each event type, assert the observed Liquidsoap state / log output.

### Manual smoke

Documented in `CONTRIBUTING.md` — run Liquidsoap locally with `settings.server.socket := true`, point smoothoperator at it, send a handful of curated test messages.

### Tooling already in place

- `make test` → CUnit suite
- `make coverage` → lcov html
- `make static-analysis` → cppcheck
- `make lint` → clang-format --dry-run
- ASAN/UBSAN in debug build (per README)

Every phase must ship with green `make test` + `make static-analysis`, and be built in debug (ASAN/UBSAN) without warnings.

---

## Rollout Plan

1. **PR 1 — Phase 1** (transport abstraction).
   Review focus: config validation edge cases, unix-socket security checks.
   Merge, deploy to staging with `LIQUIDSOAP_PROTOCOL=telnet` (zero functional change).
   Then staging flip to `socket` — verify behaviour under real load.
2. **PR 2 — Phase 2** (response protocol fix).
   Review focus: the `resp->ok` heuristic change; integration test against real Liquidsoap.
   Canary in staging for at least one week before prod rollout.
3. **PR 3.a — Phase 3, source.* and request.* events**.
4. **PR 3.b — Phase 3, var.* and output.*/playlist.* events**.
5. **PR 3.c — Phase 3, server.* introspection + response publishing back to RabbitMQ**.
6. **PR 4 — Phase 4 raw gateway**, only if operators request it.

Each PR is independently revertible. Config defaults mean any one PR reverted restores prior behaviour.

---

## Open Questions

1. **Should Phase 2's `resp->ok` heuristic also treat explicit `ERROR:` prefix as false?** Liquidsoap doesn't standardise this but many built-in commands use it. Pro: more accurate `ok` flag. Con: one more heuristic to maintain.
2. **Response publishing back to RabbitMQ** — in Phase 3 or deferred? `docs/API.md` documents it but the code doesn't do it today. Not strictly required for expanded command coverage, but without it, getter events are write-only from RQ's perspective. Suggest keeping it as phase 3.c so phase 3.a/b land faster.
3. **Bug 2 priority** — `announcement.push` path validation. Before Phase 3 ships `request.push`, or in parallel?
4. **Config file support** — `config_load` currently takes a `config_file` parameter it ignores. Worth implementing alongside these env vars, or leave alone? (Recommendation: leave alone; env vars + systemd env file are sufficient.)
