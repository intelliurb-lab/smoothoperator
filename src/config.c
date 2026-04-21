#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

/* Keys that are only allowed via env vars or dedicated secret files
 * (.env, smoothoperator.env). They must NOT be read from smoothoperator.conf. */
static bool is_secret_key(const char *key) {
  return strcmp(key, "RABBITMQ_PASS") == 0;
}

/* Parse a file of KEY=VALUE lines and populate the environment.
 * - Blank lines and lines starting with '#' are ignored.
 * - Surrounding single/double quotes on the value are stripped.
 * - setenv() is called with overwrite=0, so existing env vars always win.
 * - If skip_secrets is true, sensitive keys (passwords) are ignored,
 *   enforcing that secrets live only in dedicated files or env. */
static void load_kv_file(const char *path, bool skip_secrets) {
  if (path == NULL || *path == '\0')
    return;

  FILE *fp = fopen(path, "re");
  if (fp == NULL)
    return;

  char line[4096];
  int lineno = 0;
  while (fgets(line, sizeof(line), fp) != NULL) {
    lineno++;

    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#')
      continue;

    char *eq = strchr(p, '=');
    if (eq == NULL)
      continue;

    *eq = '\0';
    char *key = p;

    char *k_end = eq;
    while (k_end > key && (k_end[-1] == ' ' || k_end[-1] == '\t'))
      *--k_end = '\0';
    if (*key == '\0')
      continue;
    for (const char *kp = key; *kp; kp++) {
      if (!isalnum((unsigned char)*kp) && *kp != '_') {
        key = NULL;
        break;
      }
    }
    if (key == NULL)
      continue;

    char *value = eq + 1;
    while (*value == ' ' || *value == '\t')
      value++;
    char *v_end = value + strlen(value);
    while (v_end > value &&
           (v_end[-1] == '\n' || v_end[-1] == '\r' ||
            v_end[-1] == ' ' || v_end[-1] == '\t'))
      *--v_end = '\0';
    size_t vlen = (size_t)(v_end - value);
    if (vlen >= 2 &&
        ((value[0] == '"' && value[vlen - 1] == '"') ||
         (value[0] == '\'' && value[vlen - 1] == '\''))) {
      value[vlen - 1] = '\0';
      value++;
    }

    if (skip_secrets && is_secret_key(key))
      continue;

    if (setenv(key, value, 0) != 0) {
      fprintf(stderr, "WARNING: setenv(%s) failed at %s:%d: %s\n", key, path,
              lineno, strerror(errno));
    }
  }

  fclose(fp);
}

static char *get_config_string(const char *key, const char *default_val) {
  const char *val = getenv(key);
  if (val && strlen(val) > 0) {
    return strdup(val);
  }
  return default_val ? strdup(default_val) : NULL;
}

static bool parse_ulong(const char *val, unsigned long *out) {
  if (val == NULL || *val == '\0')
    return false;
  /* Reject leading sign to avoid strtoul's silent negative wrap-around. */
  if (*val == '-' || *val == '+')
    return false;
  errno = 0;
  char *endptr = NULL;
  unsigned long n = strtoul(val, &endptr, 10);
  if (errno != 0 || endptr == val || *endptr != '\0')
    return false;
  *out = n;
  return true;
}

static uint16_t get_config_uint16(const char *key, uint16_t default_val) {
  const char *val = getenv(key);
  if (val == NULL || *val == '\0')
    return default_val;

  unsigned long n = 0;
  if (!parse_ulong(val, &n)) {
    fprintf(stderr, "ERROR: %s='%s' is not a non-negative integer\n", key, val);
    return default_val;
  }
  if (n == 0 || n > 65535) {
    fprintf(stderr, "ERROR: %s='%s' out of range (1-65535)\n", key, val);
    return default_val;
  }
  return (uint16_t)n;
}

static uint32_t get_config_uint32(const char *key, uint32_t default_val) {
  const char *val = getenv(key);
  if (val == NULL || *val == '\0')
    return default_val;

  unsigned long n = 0;
  if (!parse_ulong(val, &n)) {
    fprintf(stderr, "ERROR: %s='%s' is not a non-negative integer\n", key, val);
    return default_val;
  }
  if (n == 0 || n > UINT32_MAX) {
    fprintf(stderr, "ERROR: %s='%s' out of range (1-%u)\n", key, val,
            UINT32_MAX);
    return default_val;
  }
  return (uint32_t)n;
}

static bool get_config_bool(const char *key, bool default_val) {
  const char *val = getenv(key);
  if (val == NULL || *val == '\0')
    return default_val;

  if (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
      strcasecmp(val, "yes") == 0 || strcasecmp(val, "on") == 0)
    return true;
  if (strcmp(val, "0") == 0 || strcasecmp(val, "false") == 0 ||
      strcasecmp(val, "no") == 0 || strcasecmp(val, "off") == 0)
    return false;

  fprintf(stderr,
          "ERROR: %s='%s' is not a boolean "
          "(expected 0/1, true/false, yes/no, on/off), using default=%s\n",
          key, val, default_val ? "true" : "false");
  return default_val;
}

static ls_protocol_t get_config_proto(const char *key,
                                      ls_protocol_t default_val) {
  const char *val = getenv(key);
  if (!val || !*val)
    return default_val;
  if (strcmp(val, "telnet") == 0)
    return LS_PROTO_TELNET;
  if (strcmp(val, "socket") == 0)
    return LS_PROTO_SOCKET;
  fprintf(stderr,
          "ERROR: LIQUIDSOAP_PROTOCOL must be 'telnet' or 'socket', got '%s'\n",
          val);
  return (ls_protocol_t)-1;
}

config_t *config_load(const char *config_file) {
  /* Secrets-only files: may set RABBITMQ_PASS and other env vars,
   * but existing env vars always win (setenv with overwrite=0). */
  load_kv_file(".env", false);
  load_kv_file("smoothoperator.env", false);

  /* Main config file: non-secret settings only.
   * Password keys are filtered out — they must come from a secret file
   * or the environment. */
  if (config_file != NULL && *config_file != '\0')
    load_kv_file(config_file, true);

  config_t *cfg = calloc(1, sizeof(config_t));
  if (cfg == NULL)
    return NULL;

  /* Load from environment variables (all required, no defaults) */
  cfg->rabbitmq_host = get_config_string("RABBITMQ_HOST", NULL);
  cfg->rabbitmq_port = get_config_uint16("RABBITMQ_PORT", 0);
  cfg->rabbitmq_user = get_config_string("RABBITMQ_USER", NULL);
  cfg->rabbitmq_pass = get_config_string("RABBITMQ_PASS", NULL);
  cfg->rabbitmq_vhost = get_config_string("RABBITMQ_VHOST", "/");

  /* TLS configuration (optional, but recommended for production) */
  cfg->rabbitmq_tls_enabled = get_config_bool("RABBITMQ_TLS_ENABLED", false);
  cfg->rabbitmq_tls_ca_cert = get_config_string("RABBITMQ_TLS_CA_CERT", NULL);
  cfg->rabbitmq_tls_client_cert = get_config_string("RABBITMQ_TLS_CLIENT_CERT", NULL);
  cfg->rabbitmq_tls_client_key = get_config_string("RABBITMQ_TLS_CLIENT_KEY", NULL);
  cfg->rabbitmq_tls_verify_peer = get_config_bool("RABBITMQ_TLS_VERIFY_PEER", true);

  cfg->liquidsoap_protocol =
      get_config_proto("LIQUIDSOAP_PROTOCOL", LS_PROTO_TELNET);
  cfg->liquidsoap_host = get_config_string("LIQUIDSOAP_HOST", NULL);
  cfg->liquidsoap_port = get_config_uint16("LIQUIDSOAP_PORT", 0);
  cfg->liquidsoap_socket_path = get_config_string("LIQUIDSOAP_SOCKET_PATH", NULL);
  cfg->liquidsoap_timeout_ms = get_config_uint32("LIQUIDSOAP_TIMEOUT_MS", 3000);
  cfg->liquidsoap_reconnect_max_delay_ms = get_config_uint32("LIQUIDSOAP_RECONNECT_MAX_DELAY_MS", 30000);

  cfg->http_healthcheck_port = get_config_uint16("HTTP_HEALTHCHECK_PORT", 0);

  cfg->log_file = get_config_string("LOG_FILE", NULL);
  cfg->log_level = get_config_string("LOG_LEVEL", "INFO");

  return cfg;
}

bool config_is_valid(const config_t *cfg) {
  if (cfg == NULL)
    return false;

  if (cfg->rabbitmq_host == NULL || strlen(cfg->rabbitmq_host) == 0) {
    fprintf(stderr, "ERROR: RABBITMQ_HOST must be set\n");
    return false;
  }
  if (cfg->rabbitmq_port == 0) {
    fprintf(stderr, "ERROR: RABBITMQ_PORT must be set (1-65535)\n");
    return false;
  }
  if (cfg->rabbitmq_user == NULL || strlen(cfg->rabbitmq_user) == 0) {
    fprintf(stderr, "ERROR: RABBITMQ_USER must be set\n");
    return false;
  }
  if (cfg->rabbitmq_pass == NULL || strlen(cfg->rabbitmq_pass) < 16) {
    fprintf(stderr, "ERROR: RABBITMQ_PASS must be set and >= 16 chars\n");
    return false;
  }

  if ((int)cfg->liquidsoap_protocol < 0) {
    fprintf(stderr, "ERROR: LIQUIDSOAP_PROTOCOL parse failed\n");
    return false;
  }

  if (cfg->liquidsoap_protocol == LS_PROTO_TELNET) {
    if (cfg->liquidsoap_host == NULL || strlen(cfg->liquidsoap_host) == 0) {
      fprintf(stderr, "ERROR: LIQUIDSOAP_HOST required for telnet protocol\n");
      return false;
    }
    if (cfg->liquidsoap_port == 0) {
      fprintf(stderr, "ERROR: LIQUIDSOAP_PORT required for telnet protocol\n");
      return false;
    }
    if (cfg->liquidsoap_socket_path != NULL &&
        strlen(cfg->liquidsoap_socket_path) > 0) {
      fprintf(stderr,
              "ERROR: cannot set LIQUIDSOAP_SOCKET_PATH with telnet protocol\n");
      return false;
    }
  } else if (cfg->liquidsoap_protocol == LS_PROTO_SOCKET) {
    if (cfg->liquidsoap_socket_path == NULL ||
        strlen(cfg->liquidsoap_socket_path) == 0) {
      fprintf(stderr,
              "ERROR: LIQUIDSOAP_SOCKET_PATH required for socket protocol\n");
      return false;
    }
    if (cfg->liquidsoap_socket_path[0] != '/') {
      fprintf(stderr, "ERROR: LIQUIDSOAP_SOCKET_PATH must be absolute\n");
      return false;
    }
    if (strlen(cfg->liquidsoap_socket_path) > 108) {
      fprintf(stderr, "ERROR: LIQUIDSOAP_SOCKET_PATH exceeds max length 108\n");
      return false;
    }
    if (cfg->liquidsoap_host != NULL && strlen(cfg->liquidsoap_host) > 0) {
      fprintf(stderr,
              "ERROR: cannot set LIQUIDSOAP_HOST with socket protocol\n");
      return false;
    }
  }

  if (cfg->log_file == NULL || strlen(cfg->log_file) == 0) {
    fprintf(stderr, "ERROR: LOG_FILE must be set\n");
    return false;
  }

  return true;
}

void config_free(config_t *cfg) {
  if (cfg == NULL)
    return;

  if (cfg->rabbitmq_host)
    free(cfg->rabbitmq_host);
  if (cfg->rabbitmq_user)
    free(cfg->rabbitmq_user);
  if (cfg->rabbitmq_pass) {
    volatile char *p = cfg->rabbitmq_pass;
    while (*p) *p++ = 0;
    free(cfg->rabbitmq_pass);
  }
  if (cfg->rabbitmq_vhost)
    free(cfg->rabbitmq_vhost);
  if (cfg->rabbitmq_tls_ca_cert)
    free(cfg->rabbitmq_tls_ca_cert);
  if (cfg->rabbitmq_tls_client_cert)
    free(cfg->rabbitmq_tls_client_cert);
  if (cfg->rabbitmq_tls_client_key) {
    volatile char *p = cfg->rabbitmq_tls_client_key;
    while (*p) *p++ = 0;
    free(cfg->rabbitmq_tls_client_key);
  }
  if (cfg->liquidsoap_host)
    free(cfg->liquidsoap_host);
  if (cfg->liquidsoap_socket_path)
    free(cfg->liquidsoap_socket_path);
  if (cfg->log_file)
    free(cfg->log_file);
  if (cfg->log_level)
    free(cfg->log_level);
  free(cfg);
}

int config_get_log_level(const config_t *cfg) {
  if (cfg == NULL || cfg->log_level == NULL)
    return 1; /* LOG_INFO */

  static const struct {
    const char *name;
    int level;
  } levels[] = {
    {"DEBUG", 0},
    {"INFO", 1},
    {"WARN", 2},
    {"ERROR", 3},
    {"FATAL", 4},
    {NULL, -1}
  };

  for (int i = 0; levels[i].name != NULL; i++) {
    if (strcmp(cfg->log_level, levels[i].name) == 0)
      return levels[i].level;
  }

  fprintf(stderr, "WARNING: unknown LOG_LEVEL '%s', using INFO\n",
          cfg->log_level);
  return 1; /* LOG_INFO */
}
