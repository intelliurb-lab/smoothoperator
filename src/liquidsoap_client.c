#include "liquidsoap_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

struct ls_socket {
  int fd;
  char *host;
  uint16_t port;
  uint32_t timeout_ms;
  bool connected;
};

static bool is_safe_ls_arg(const char *arg) {
  if (arg == NULL) return false;
  size_t len = strlen(arg);
  if (len == 0 || len > 1024) return false;

  for (const char *p = arg; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n' || c == '\r' || c == '\0') return false;
    if (!isprint(c) && c != '\t') return false;
  }
  return true;
}

static uint32_t backoff_ms(int attempt) {
  uint32_t base = 50 * (1U << (attempt > 10 ? 10 : attempt));
  uint32_t jitter = (uint32_t)rand() % (base > 0 ? base : 1);
  uint32_t result = base + jitter;
  return result > 30000 ? 30000 : result;
}

ls_socket_t *ls_socket_create(const char *host, uint16_t port,
                               uint32_t timeout_ms) {
  ls_socket_t *sock = calloc(1, sizeof(ls_socket_t));
  if (sock == NULL)
    return NULL;

  sock->host = strdup(host);
  if (sock->host == NULL) {
    free(sock);
    return NULL;
  }

  sock->port = port;
  sock->timeout_ms = timeout_ms;
  sock->fd = -1;
  sock->connected = false;

  if (!ls_reconnect(sock)) {
    free(sock->host);
    free(sock);
    return NULL;
  }

  return sock;
}

bool ls_is_connected(const ls_socket_t *sock) {
  return sock && sock->connected;
}

bool ls_reconnect(ls_socket_t *sock) {
  if (sock == NULL)
    return false;

  if (sock->fd >= 0)
    close(sock->fd);

  sock->fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock->fd < 0)
    return false;

  struct timeval tv;
  tv.tv_sec = sock->timeout_ms / 1000;
  tv.tv_usec = (sock->timeout_ms % 1000) * 1000;

  if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  int yes = 1;
  if (setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) < 0) {
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  if (setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) {
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  struct addrinfo hints, *result = NULL, *rp = NULL;
  char port_str[8];
  int rc = 0;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  snprintf(port_str, sizeof(port_str), "%u", sock->port);

  rc = getaddrinfo(sock->host, port_str, &hints, &result);
  if (rc != 0) {
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    close(sock->fd);
    sock->fd = socket(rp->ai_family, rp->ai_socktype | SOCK_CLOEXEC,
                      rp->ai_protocol);
    if (sock->fd < 0)
      continue;

    if (connect(sock->fd, rp->ai_addr, rp->ai_addrlen) == 0)
      break;
  }

  freeaddrinfo(result);

  if (rp == NULL) {
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  sock->connected = true;
  return true;
}

static char *ls_recv_line(int fd, size_t max_len) {
  if (fd < 0) return NULL;

  char *line = malloc(max_len + 1);
  if (line == NULL) return NULL;

  size_t pos = 0;
  while (pos < max_len) {
    ssize_t n = read(fd, &line[pos], 1);
    if (n <= 0) {
      free(line);
      return NULL;
    }
    if (line[pos] == '\n') {
      if (pos > 0 && line[pos - 1] == '\r') {
        pos--;
      }
      line[pos] = '\0';
      return line;
    }
    pos++;
  }

  free(line);
  return NULL;
}

ls_response_t *ls_send_command(ls_socket_t *sock, const char *command) {
  if (sock == NULL || command == NULL)
    return NULL;

  if (!is_safe_ls_arg(command))
    return NULL;

  if (!sock->connected) {
    if (!ls_reconnect(sock))
      return NULL;
  }

  ls_response_t *resp = calloc(1, sizeof(ls_response_t));
  if (resp == NULL)
    return NULL;

  time_t start = time(NULL);

  char cmd_buf[1024];
  int n = snprintf(cmd_buf, sizeof(cmd_buf), "%s\n", command);
  if (n < 0 || (size_t)n >= sizeof(cmd_buf)) {
    free(resp);
    return NULL;
  }

  if (write(sock->fd, cmd_buf, (size_t)n) != n) {
    free(resp);
    sock->connected = false;
    return NULL;
  }

  char *response = ls_recv_line(sock->fd, 256);
  if (response == NULL) {
    free(resp);
    sock->connected = false;
    return NULL;
  }

  time_t end = time(NULL);
  resp->latency_ms = (uint32_t)((end - start) * 1000);

  if (strncmp(response, "OK", 2) == 0) {
    resp->ok = true;
    resp->message = strdup("OK");
  } else {
    resp->ok = false;
    resp->message = response;
    response = NULL;
  }

  free(response);

  if (resp->message == NULL) {
    free(resp);
    return NULL;
  }

  return resp;
}

void ls_socket_free(ls_socket_t *sock) {
  if (sock == NULL)
    return;

  if (sock->fd >= 0)
    close(sock->fd);
  free(sock->host);
  free(sock);
}

void ls_response_free(ls_response_t *resp) {
  if (resp == NULL)
    return;

  free(resp->message);
  free(resp);
}
