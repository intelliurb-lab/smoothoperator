#include "rabbitmq_producer.h"
#include "liquidsoap_client.h"
#include "ls_controller.h"
#include "smoothoperator.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>
#include <jansson.h>

#define AMQP_CONN_FRAME_SIZE 131072
#define AMQP_CONN_HEARTBEAT 30
#define POLL_INTERVAL_MS 5000

struct rabbitmq_producer {
  amqp_connection_state_t conn;
  amqp_channel_t channel;
  const config_t *cfg;
  controller_t *controller;
  bool running;
  bool connected;
  bool channel_open;

  char *prev_playlist;
  char *prev_current_song;
};

static bool amqp_reply_ok(amqp_rpc_reply_t r, const char *what) {
  if (r.reply_type == AMQP_RESPONSE_NORMAL)
    return true;
  log_msg(LOG_ERROR, "rabbitmq_producer", what, NULL, NULL);
  return false;
}

static bool tls_file_readable(const char *kind, const char *path) {
  if (path == NULL) return true;
  struct stat st;
  if (stat(path, &st) < 0) {
    log_msg(LOG_ERROR, "rabbitmq_producer",
            "TLS file not found or unreadable", kind, path);
    return false;
  }
  if (!S_ISREG(st.st_mode)) {
    log_msg(LOG_ERROR, "rabbitmq_producer", "TLS path is not a regular file",
            kind, path);
    return false;
  }
  if (access(path, R_OK) < 0) {
    log_msg(LOG_ERROR, "rabbitmq_producer", "TLS file not readable (permissions)",
            kind, path);
    return false;
  }
  return true;
}

static bool setup_amqp_socket(rabbitmq_producer_t *producer) {
  const config_t *cfg = producer->cfg;

  if (cfg->rabbitmq_tls_enabled) {
    if (!tls_file_readable("ca_cert", cfg->rabbitmq_tls_ca_cert)) return false;
    if (!tls_file_readable("client_cert", cfg->rabbitmq_tls_client_cert)) return false;
    if (!tls_file_readable("client_key", cfg->rabbitmq_tls_client_key)) return false;

    if ((cfg->rabbitmq_tls_client_cert == NULL) !=
        (cfg->rabbitmq_tls_client_key == NULL)) {
      log_msg(LOG_ERROR, "rabbitmq_producer",
              "TLS: client_cert and client_key must be set together",
              NULL, NULL);
      return false;
    }

    amqp_socket_t *socket = amqp_ssl_socket_new(producer->conn);
    if (socket == NULL)
      return false;

    amqp_ssl_socket_set_verify_peer(socket, cfg->rabbitmq_tls_verify_peer);
    amqp_ssl_socket_set_verify_hostname(socket, cfg->rabbitmq_tls_verify_peer);

    if (cfg->rabbitmq_tls_ca_cert != NULL) {
      if (amqp_ssl_socket_set_cacert(socket, cfg->rabbitmq_tls_ca_cert) !=
          AMQP_STATUS_OK) {
        log_msg(LOG_ERROR, "rabbitmq_producer", "TLS: set_cacert failed", NULL, NULL);
        return false;
      }
    } else if (cfg->rabbitmq_tls_verify_peer) {
      log_msg(LOG_ERROR, "rabbitmq_producer",
              "TLS: verify_peer=1 requires RABBITMQ_TLS_CA_CERT", NULL, NULL);
      return false;
    }

    if (cfg->rabbitmq_tls_client_cert != NULL &&
        cfg->rabbitmq_tls_client_key != NULL) {
      if (amqp_ssl_socket_set_key(socket, cfg->rabbitmq_tls_client_cert,
                                  cfg->rabbitmq_tls_client_key) !=
          AMQP_STATUS_OK) {
        log_msg(LOG_ERROR, "rabbitmq_producer", "TLS: set_key failed", NULL, NULL);
        return false;
      }
    }

    int status = amqp_socket_open(socket, cfg->rabbitmq_host, cfg->rabbitmq_port);
    if (status != AMQP_STATUS_OK) {
      log_msg(LOG_ERROR, "rabbitmq_producer", "TLS socket open failed", NULL, NULL);
      return false;
    }
    return true;
  }

  amqp_socket_t *socket = amqp_tcp_socket_new(producer->conn);
  if (socket == NULL)
    return false;

  int status = amqp_socket_open(socket, cfg->rabbitmq_host, cfg->rabbitmq_port);
  if (status != AMQP_STATUS_OK) {
    log_msg(LOG_ERROR, "rabbitmq_producer", "TCP socket open failed", NULL, NULL);
    return false;
  }
  return true;
}

static bool declare_exchange(rabbitmq_producer_t *producer) {
  const config_t *cfg = producer->cfg;

  amqp_exchange_declare(producer->conn, producer->channel,
                        amqp_cstring_bytes(cfg->rabbitmq_exchange_name),
                        amqp_cstring_bytes("topic"),
                        0,
                        1,
                        0,
                        0,
                        amqp_empty_table);
  if (!amqp_reply_ok(amqp_get_rpc_reply(producer->conn),
                     "exchange_declare failed"))
    return false;

  return true;
}

static bool setup_amqp_connection(rabbitmq_producer_t *producer) {
  if (producer == NULL || producer->cfg == NULL)
    return false;

  producer->conn = amqp_new_connection();
  if (producer->conn == NULL)
    return false;

  if (!setup_amqp_socket(producer)) {
    amqp_destroy_connection(producer->conn);
    producer->conn = NULL;
    return false;
  }

  amqp_rpc_reply_t reply = amqp_login(
      producer->conn, producer->cfg->rabbitmq_vhost, 0,
      AMQP_CONN_FRAME_SIZE, AMQP_CONN_HEARTBEAT, AMQP_SASL_METHOD_PLAIN,
      producer->cfg->rabbitmq_user, producer->cfg->rabbitmq_pass);

  if (!amqp_reply_ok(reply, "amqp_login failed")) {
    amqp_destroy_connection(producer->conn);
    producer->conn = NULL;
    return false;
  }

  amqp_channel_open(producer->conn, producer->channel);
  reply = amqp_get_rpc_reply(producer->conn);
  if (!amqp_reply_ok(reply, "channel_open failed")) {
    amqp_connection_close(producer->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(producer->conn);
    producer->conn = NULL;
    return false;
  }
  producer->channel_open = true;

  if (!declare_exchange(producer)) {
    amqp_channel_close(producer->conn, producer->channel, AMQP_REPLY_SUCCESS);
    amqp_connection_close(producer->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(producer->conn);
    producer->conn = NULL;
    producer->channel_open = false;
    return false;
  }

  producer->connected = true;
  return true;
}

rabbitmq_producer_t *rabbitmq_producer_create(const config_t *cfg,
                                              controller_t *controller) {
  if (cfg == NULL || controller == NULL)
    return NULL;

  rabbitmq_producer_t *producer = calloc(1, sizeof(rabbitmq_producer_t));
  if (producer == NULL)
    return NULL;

  producer->cfg = cfg;
  producer->controller = controller;
  producer->channel = 1;
  producer->running = false;
  producer->connected = false;
  producer->channel_open = false;
  producer->prev_playlist = NULL;
  producer->prev_current_song = NULL;

  if (!setup_amqp_connection(producer)) {
    free(producer);
    return NULL;
  }

  return producer;
}

bool rabbitmq_producer_publish(rabbitmq_producer_t *producer,
                               const char *event_type, const char *json_body) {
  if (producer == NULL || !producer->connected || event_type == NULL ||
      json_body == NULL)
    return false;

  amqp_basic_properties_t props;
  memset(&props, 0, sizeof(props));
  props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG |
                 AMQP_BASIC_TIMESTAMP_FLAG;
  props.content_type = amqp_cstring_bytes("application/json");
  props.delivery_mode = 2;

  time_t now = time(NULL);
  props.timestamp = (uint64_t)now * 1000;

  int result = amqp_basic_publish(
      producer->conn, producer->channel,
      amqp_cstring_bytes(producer->cfg->rabbitmq_exchange_name),
      amqp_cstring_bytes(event_type), 0, 0, &props,
      amqp_bytes_malloc_dup(amqp_cstring_bytes(json_body)));

  if (result != 0) {
    log_msg(LOG_ERROR, "rabbitmq_producer", "publish failed", event_type, NULL);
    producer->connected = false;
    return false;
  }

  log_msg(LOG_DEBUG, "rabbitmq_producer", "published event", event_type, json_body);
  return true;
}

static char *safe_strdup(const char *str) {
  if (str == NULL)
    return NULL;
  return strdup(str);
}

static bool strings_differ(const char *a, const char *b) {
  if (a == NULL && b == NULL)
    return false;
  if (a == NULL || b == NULL)
    return true;
  return strcmp(a, b) != 0;
}

static void poll_playlist_state(rabbitmq_producer_t *producer) {
  if (producer == NULL || producer->controller == NULL)
    return;

  ls_socket_t *sock = controller_get_socket(producer->controller);
  if (sock == NULL)
    return;

  ls_response_t *resp =
      ls_send_command(sock, "var.get current_playlist");
  if (resp == NULL)
    return;

  if (resp->ok && resp->body != NULL) {
    char *new_playlist = safe_strdup(resp->body);
    if (strings_differ(producer->prev_playlist, new_playlist)) {
      json_t *payload = json_object();
      json_object_set_new(payload, "playlist", json_string(new_playlist ? new_playlist : ""));
      char *json_str = json_dumps(payload, JSON_COMPACT);
      if (json_str != NULL) {
        rabbitmq_producer_publish(producer, "state.playlist_changed", json_str);
        free(json_str);
      }
      json_decref(payload);

      free(producer->prev_playlist);
      producer->prev_playlist = new_playlist;
    } else {
      free(new_playlist);
    }
  }
  ls_response_free(resp);
}

static void poll_current_song(rabbitmq_producer_t *producer) {
  if (producer == NULL || producer->controller == NULL)
    return;

  ls_socket_t *sock = controller_get_socket(producer->controller);
  if (sock == NULL)
    return;

  ls_response_t *resp = ls_send_command(sock, "request.on_air");
  if (resp == NULL)
    return;

  if (resp->ok && resp->body != NULL) {
    char *new_song = safe_strdup(resp->body);
    if (strings_differ(producer->prev_current_song, new_song)) {
      json_t *payload = json_object();
      json_object_set_new(payload, "song", json_string(new_song ? new_song : ""));
      char *json_str = json_dumps(payload, JSON_COMPACT);
      if (json_str != NULL) {
        rabbitmq_producer_publish(producer, "state.current_song", json_str);
        free(json_str);
      }
      json_decref(payload);

      free(producer->prev_current_song);
      producer->prev_current_song = new_song;
    } else {
      free(new_song);
    }
  }
  ls_response_free(resp);
}

void rabbitmq_producer_run(rabbitmq_producer_t *producer,
                           volatile sig_atomic_t *shutdown_flag) {
  if (producer == NULL)
    return;

  producer->running = true;

  while (producer->running) {
    if (shutdown_flag != NULL && *shutdown_flag) {
      producer->running = false;
      break;
    }

    poll_playlist_state(producer);
    poll_current_song(producer);

    usleep(POLL_INTERVAL_MS * 1000);
  }
}

bool rabbitmq_producer_is_connected(const rabbitmq_producer_t *producer) {
  return producer && producer->connected;
}

void rabbitmq_producer_shutdown(rabbitmq_producer_t *producer) {
  if (producer == NULL)
    return;

  producer->running = false;
}

void rabbitmq_producer_free(rabbitmq_producer_t *producer) {
  if (producer == NULL)
    return;

  if (producer->conn != NULL) {
    if (producer->channel_open) {
      amqp_channel_close(producer->conn, producer->channel,
                         AMQP_REPLY_SUCCESS);
    }
    amqp_connection_close(producer->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(producer->conn);
  }

  free(producer->prev_playlist);
  free(producer->prev_current_song);
  free(producer);
}
