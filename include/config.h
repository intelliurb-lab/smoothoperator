#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  LS_PROTO_TELNET = 0,
  LS_PROTO_SOCKET = 1
} ls_protocol_t;

typedef struct {
  char *rabbitmq_host;
  uint16_t rabbitmq_port;
  char *rabbitmq_user;
  char *rabbitmq_pass;
  char *rabbitmq_vhost;
  bool rabbitmq_tls_enabled;
  char *rabbitmq_tls_ca_cert;
  char *rabbitmq_tls_client_cert;
  char *rabbitmq_tls_client_key;
  bool rabbitmq_tls_verify_peer;

  ls_protocol_t liquidsoap_protocol;
  char *liquidsoap_host;
  uint16_t liquidsoap_port;
  char *liquidsoap_socket_path;
  uint32_t liquidsoap_timeout_ms;
  uint32_t liquidsoap_reconnect_max_delay_ms;

  uint16_t http_healthcheck_port;

  char *log_file;
  char *log_level;
} config_t;

/* Load config from environment + file */
config_t *config_load(const char *config_file);

/* Validate config */
bool config_is_valid(const config_t *cfg);

/* Cleanup */
void config_free(config_t *cfg);

/* Get parsed log level */
int config_get_log_level(const config_t *cfg);

#endif /* CONFIG_H */
