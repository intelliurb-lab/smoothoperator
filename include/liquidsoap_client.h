#ifndef LIQUIDSOAP_CLIENT_H
#define LIQUIDSOAP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct ls_socket ls_socket_t;

/* Response from Liquidsoap */
typedef struct {
  bool ok;
  char *message;
  uint32_t latency_ms;
} ls_response_t;

/* Create and connect to Liquidsoap */
ls_socket_t *ls_socket_create(const char *host, uint16_t port,
                               uint32_t timeout_ms);

/* Send command, get response */
ls_response_t *ls_send_command(ls_socket_t *sock, const char *command);

/* Check if socket is connected */
bool ls_is_connected(const ls_socket_t *sock);

/* Reconnect (auto-called by ls_send_command if needed) */
bool ls_reconnect(ls_socket_t *sock);

/* Cleanup */
void ls_socket_free(ls_socket_t *sock);
void ls_response_free(ls_response_t *resp);

#endif /* LIQUIDSOAP_CLIENT_H */
