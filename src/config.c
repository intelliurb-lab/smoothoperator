#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *get_config_string(const char *key, const char *default_val) {
  const char *val = getenv(key);
  if (val && strlen(val) > 0) {
    return strdup(val);
  }
  return default_val ? strdup(default_val) : NULL;
}

static uint16_t get_config_uint16(const char *key, uint16_t default_val) {
  const char *val = getenv(key);
  if (val && strlen(val) > 0) {
    long port = strtol(val, NULL, 10);
    if (port > 0 && port <= 65535) return (uint16_t)port;
  }
  return default_val;
}

static uint32_t get_config_uint32(const char *key, uint32_t default_val) {
  const char *val = getenv(key);
  if (val && strlen(val) > 0) {
    long val_long = strtol(val, NULL, 10);
    if (val_long > 0) return (uint32_t)val_long;
  }
  return default_val;
}

static bool get_config_bool(const char *key, bool default_val) {
  const char *val = getenv(key);
  if (val && strlen(val) > 0) {
    if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0 ||
        strcmp(val, "yes") == 0 || strcmp(val, "on") == 0) {
      return true;
    }
    return false;
  }
  return default_val;
}

config_t *config_load(const char *config_file) {
  (void)config_file;

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

  cfg->liquidsoap_host = get_config_string("LIQUIDSOAP_HOST", NULL);
  cfg->liquidsoap_port = get_config_uint16("LIQUIDSOAP_PORT", 0);
  cfg->liquidsoap_timeout_ms = get_config_uint32("LIQUIDSOAP_TIMEOUT_MS", 3000);
  cfg->liquidsoap_reconnect_max_delay_ms = get_config_uint32("LIQUIDSOAP_RECONNECT_MAX_DELAY_MS", 30000);

  cfg->http_healthcheck_port = get_config_uint16("HTTP_HEALTHCHECK_PORT", 0);

  cfg->log_file = get_config_string("LOG_FILE", NULL);
  cfg->log_level = get_config_string("LOG_LEVEL", "INFO");

  return cfg;
}

bool config_is_valid(const config_t *cfg) {
  if (cfg == NULL) return false;

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
  if (cfg->rabbitmq_pass == NULL || strlen(cfg->rabbitmq_pass) < 12) {
    fprintf(stderr, "ERROR: RABBITMQ_PASS must be set and >= 12 chars\n");
    return false;
  }
  if (cfg->liquidsoap_host == NULL || strlen(cfg->liquidsoap_host) == 0) {
    fprintf(stderr, "ERROR: LIQUIDSOAP_HOST must be set\n");
    return false;
  }
  if (cfg->liquidsoap_port == 0) {
    fprintf(stderr, "ERROR: LIQUIDSOAP_PORT must be set (1-65535)\n");
    return false;
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
