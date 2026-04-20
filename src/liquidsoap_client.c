#include "liquidsoap_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
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
  ls_protocol_t proto;
  char *host;           /* used for telnet */
  uint16_t port;        /* used for telnet */
  char *socket_path;    /* used for socket */
  uint32_t timeout_ms;
  bool connected;
};

static bool is_safe_ls_arg(const char *arg) {
  if (arg == NULL)
    return false;
  size_t len = strlen(arg);
  if (len == 0 || len > 1024)
    return false;

  for (const char *p = arg; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n' || c == '\r' || c == '\0')
      return false;
    if (!isprint(c) && c != '\t')
      return false;
  }
  return true;
}


static bool telnet_connect(ls_socket_t *sock) {
  struct addrinfo hints, *result = NULL, *rp = NULL;
  char port_str[8];
  int rc = 0;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  snprintf(port_str, sizeof(port_str), "%u", sock->port);

  rc = getaddrinfo(sock->host, port_str, &hints, &result);
  if (rc != 0) {
    return false;
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sock->fd = socket(rp->ai_family, rp->ai_socktype | SOCK_CLOEXEC,
                      rp->ai_protocol);
    if (sock->fd < 0)
      continue;

    if (connect(sock->fd, rp->ai_addr, rp->ai_addrlen) == 0)
      break;

    close(sock->fd);
    sock->fd = -1;
  }

  freeaddrinfo(result);

  if (rp == NULL) {
    return false;
  }

  return true;
}

static bool unix_socket_connect(ls_socket_t *sock) {
  struct stat st;
  struct sockaddr_un addr;

  if (lstat(sock->socket_path, &st) < 0)
    return false;

  if (S_ISLNK(st.st_mode))
    return false;

  if (stat(sock->socket_path, &st) < 0)
    return false;

  if (!S_ISSOCK(st.st_mode))
    return false;

  if ((st.st_mode & S_IWOTH) != 0)
    return false;

  sock->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock->fd < 0)
    return false;

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock->socket_path, sizeof(addr.sun_path) - 1);

  if (connect(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  return true;
}

ls_socket_t *ls_socket_create(const char *host, uint16_t port,
                               uint32_t timeout_ms) {
  ls_socket_t *sock = calloc(1, sizeof(ls_socket_t));
  if (sock == NULL)
    return NULL;

  sock->proto = LS_PROTO_TELNET;
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

ls_socket_t *ls_socket_create_unix(const char *socket_path,
                                    uint32_t timeout_ms) {
  ls_socket_t *sock = calloc(1, sizeof(ls_socket_t));
  if (sock == NULL)
    return NULL;

  sock->proto = LS_PROTO_SOCKET;
  sock->socket_path = strdup(socket_path);
  if (sock->socket_path == NULL) {
    free(sock);
    return NULL;
  }

  sock->timeout_ms = timeout_ms;
  sock->fd = -1;
  sock->connected = false;

  if (!ls_reconnect(sock)) {
    free(sock->socket_path);
    free(sock);
    return NULL;
  }

  return sock;
}

ls_socket_t *ls_socket_create_from_config(const config_t *cfg) {
  if (cfg == NULL)
    return NULL;

  if (cfg->liquidsoap_protocol == LS_PROTO_TELNET) {
    return ls_socket_create(cfg->liquidsoap_host, cfg->liquidsoap_port,
                            cfg->liquidsoap_timeout_ms);
  } else if (cfg->liquidsoap_protocol == LS_PROTO_SOCKET) {
    return ls_socket_create_unix(cfg->liquidsoap_socket_path,
                                 cfg->liquidsoap_timeout_ms);
  }

  return NULL;
}

bool ls_is_connected(const ls_socket_t *sock) {
  return sock && sock->connected;
}

bool ls_reconnect(ls_socket_t *sock) {
  if (sock == NULL)
    return false;

  if (sock->fd >= 0)
    close(sock->fd);

  sock->fd = -1;
  sock->connected = false;

  struct timeval tv;
  tv.tv_sec = sock->timeout_ms / 1000;
  tv.tv_usec = (sock->timeout_ms % 1000) * 1000;

  bool connected = false;

  if (sock->proto == LS_PROTO_TELNET) {
    if (!telnet_connect(sock))
      return false;
    connected = true;

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
    if (setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) <
        0) {
      close(sock->fd);
      sock->fd = -1;
      return false;
    }

    if (setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) <
        0) {
      close(sock->fd);
      sock->fd = -1;
      return false;
    }
  } else if (sock->proto == LS_PROTO_SOCKET) {
    if (!unix_socket_connect(sock))
      return false;
    connected = true;

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
  }

  if (!connected)
    return false;

  sock->connected = true;
  return true;
}

static char *ls_recv_line(int fd, size_t max_len) {
  if (fd < 0)
    return NULL;

  char *line = malloc(max_len + 1);
  if (line == NULL)
    return NULL;

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

static char *ls_recv_response(int fd, bool *out_ok) {
  const size_t MAX_BODY_BYTES = 64 * 1024;
  const size_t MAX_LINE_BYTES = 4 * 1024;
  const int MAX_LINES = 2048;

  if (fd < 0 || out_ok == NULL)
    return NULL;

  char *body = NULL;
  size_t body_len = 0;
  char *first_line = NULL;

  for (int i = 0; i < MAX_LINES; i++) {
    char *line = ls_recv_line(fd, MAX_LINE_BYTES);
    if (line == NULL) {
      free(body);
      return NULL;
    }

    if (strcmp(line, "END") == 0) {
      free(line);
      break;
    }

    if (i == 0) {
      first_line = strdup(line);
    }

    if (body_len + strlen(line) + 1 > MAX_BODY_BYTES) {
      free(line);
      free(body);
      free(first_line);
      return NULL;
    }

    if (body_len > 0) {
      char *new_body = realloc(body, body_len + strlen(line) + 2);
      if (new_body == NULL) {
        free(line);
        free(body);
        free(first_line);
        return NULL;
      }
      body = new_body;
      body[body_len++] = '\n';
    } else {
      body = malloc(strlen(line) + 1);
      if (body == NULL) {
        free(line);
        free(first_line);
        return NULL;
      }
    }

    strcpy(body + body_len, line);
    body_len += strlen(line);
    free(line);
  }

  if (first_line != NULL) {
    *out_ok = (strncmp(first_line, "OK", 2) == 0 ||
               strncmp(first_line, "Done", 4) == 0);
    free(first_line);
  } else {
    *out_ok = false;
  }

  return body;
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

  bool ok = false;
  char *body = ls_recv_response(sock->fd, &ok);
  if (body == NULL) {
    free(resp);
    sock->connected = false;
    return NULL;
  }

  time_t end = time(NULL);
  resp->latency_ms = (uint32_t)((end - start) * 1000);
  resp->ok = ok;
  resp->body = body;
  resp->body_len = strlen(body);

  resp->message = malloc(resp->body_len + 1);
  if (resp->message == NULL) {
    free(resp->body);
    free(resp);
    return NULL;
  }

  const char *newline = strchr(body, '\n');
  if (newline != NULL) {
    size_t first_line_len = (size_t)(newline - body);
    strncpy(resp->message, body, first_line_len);
    resp->message[first_line_len] = '\0';
  } else {
    strcpy(resp->message, body);
  }

  return resp;
}

void ls_socket_free(ls_socket_t *sock) {
  if (sock == NULL)
    return;

  if (sock->fd >= 0)
    close(sock->fd);
  free(sock->host);
  free(sock->socket_path);
  free(sock);
}

void ls_response_free(ls_response_t *resp) {
  if (resp == NULL)
    return;

  free(resp->message);
  free(resp->body);
  free(resp);
}
