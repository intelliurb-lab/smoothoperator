#include "rabbitmq_consumer.h"
#include "ls_controller.h"
#include "message.h"
#include "smoothoperator.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>

#define AMQP_CONN_FRAME_SIZE 131072
#define AMQP_CONN_HEARTBEAT 30
#define AMQP_QUEUE_NAME "ls.commands"
#define AMQP_EXCHANGE_NAME "radio.events"

/* Routing keys bound to the queue (matches docs/API.md). */
static const char *ROUTING_KEYS[] = {
    "control.*",      "announcement.*", "source.*",  "request.*",
    "var.*",          "output.*",       "playlist.*", "server.*",
    NULL};

struct rabbitmq_consumer {
  amqp_connection_state_t conn;
  amqp_channel_t channel;
  const config_t *cfg;
  bool running;
  bool connected;
  bool channel_open;
  controller_t *controller; /* borrowed; owned by caller */
};

static bool amqp_reply_ok(amqp_rpc_reply_t r, const char *what) {
  if (r.reply_type == AMQP_RESPONSE_NORMAL)
    return true;
  log_msg(LOG_ERROR, "rabbitmq", what, NULL, NULL);
  return false;
}

static bool tls_file_readable(const char *kind, const char *path) {
  if (path == NULL) return true;
  struct stat st;
  if (stat(path, &st) < 0) {
    log_msg(LOG_ERROR, "rabbitmq",
            "TLS file not found or unreadable", kind, path);
    return false;
  }
  if (!S_ISREG(st.st_mode)) {
    log_msg(LOG_ERROR, "rabbitmq", "TLS path is not a regular file",
            kind, path);
    return false;
  }
  if (access(path, R_OK) < 0) {
    log_msg(LOG_ERROR, "rabbitmq", "TLS file not readable (permissions)",
            kind, path);
    return false;
  }
  return true;
}

static bool setup_amqp_socket(rabbitmq_consumer_t *consumer) {
  const config_t *cfg = consumer->cfg;

  if (cfg->rabbitmq_tls_enabled) {
    if (!tls_file_readable("ca_cert", cfg->rabbitmq_tls_ca_cert)) return false;
    if (!tls_file_readable("client_cert", cfg->rabbitmq_tls_client_cert)) return false;
    if (!tls_file_readable("client_key", cfg->rabbitmq_tls_client_key)) return false;

    if ((cfg->rabbitmq_tls_client_cert == NULL) !=
        (cfg->rabbitmq_tls_client_key == NULL)) {
      log_msg(LOG_ERROR, "rabbitmq",
              "TLS: client_cert and client_key must be set together",
              NULL, NULL);
      return false;
    }

    amqp_socket_t *socket = amqp_ssl_socket_new(consumer->conn);
    if (socket == NULL)
      return false;

    amqp_ssl_socket_set_verify_peer(socket, cfg->rabbitmq_tls_verify_peer);
    amqp_ssl_socket_set_verify_hostname(socket, cfg->rabbitmq_tls_verify_peer);

    if (cfg->rabbitmq_tls_ca_cert != NULL) {
      if (amqp_ssl_socket_set_cacert(socket, cfg->rabbitmq_tls_ca_cert) !=
          AMQP_STATUS_OK) {
        log_msg(LOG_ERROR, "rabbitmq", "TLS: set_cacert failed", NULL, NULL);
        return false;
      }
    } else if (cfg->rabbitmq_tls_verify_peer) {
      log_msg(LOG_ERROR, "rabbitmq",
              "TLS: verify_peer=1 requires RABBITMQ_TLS_CA_CERT", NULL, NULL);
      return false;
    }

    if (cfg->rabbitmq_tls_client_cert != NULL &&
        cfg->rabbitmq_tls_client_key != NULL) {
      if (amqp_ssl_socket_set_key(socket, cfg->rabbitmq_tls_client_cert,
                                  cfg->rabbitmq_tls_client_key) !=
          AMQP_STATUS_OK) {
        log_msg(LOG_ERROR, "rabbitmq", "TLS: set_key failed", NULL, NULL);
        return false;
      }
    }

    int status = amqp_socket_open(socket, cfg->rabbitmq_host, cfg->rabbitmq_port);
    if (status != AMQP_STATUS_OK) {
      log_msg(LOG_ERROR, "rabbitmq", "TLS socket open failed", NULL, NULL);
      return false;
    }
    return true;
  }

  amqp_socket_t *socket = amqp_tcp_socket_new(consumer->conn);
  if (socket == NULL)
    return false;

  int status = amqp_socket_open(socket, cfg->rabbitmq_host, cfg->rabbitmq_port);
  if (status != AMQP_STATUS_OK) {
    log_msg(LOG_ERROR, "rabbitmq", "TCP socket open failed", NULL, NULL);
    return false;
  }
  return true;
}

static bool declare_topology(rabbitmq_consumer_t *consumer) {
  amqp_exchange_declare(consumer->conn, consumer->channel,
                        amqp_cstring_bytes(AMQP_EXCHANGE_NAME),
                        amqp_cstring_bytes("topic"),
                        0, /* passive */
                        1, /* durable */
                        0, /* auto_delete */
                        0, /* internal */
                        amqp_empty_table);
  if (!amqp_reply_ok(amqp_get_rpc_reply(consumer->conn),
                     "exchange_declare failed"))
    return false;

  amqp_queue_declare(consumer->conn, consumer->channel,
                     amqp_cstring_bytes(AMQP_QUEUE_NAME),
                     0, /* passive */
                     1, /* durable */
                     0, /* exclusive */
                     0, /* auto_delete */
                     amqp_empty_table);
  if (!amqp_reply_ok(amqp_get_rpc_reply(consumer->conn),
                     "queue_declare failed"))
    return false;

  for (int i = 0; ROUTING_KEYS[i] != NULL; i++) {
    amqp_queue_bind(consumer->conn, consumer->channel,
                    amqp_cstring_bytes(AMQP_QUEUE_NAME),
                    amqp_cstring_bytes(AMQP_EXCHANGE_NAME),
                    amqp_cstring_bytes(ROUTING_KEYS[i]), amqp_empty_table);
    if (!amqp_reply_ok(amqp_get_rpc_reply(consumer->conn),
                       "queue_bind failed"))
      return false;
  }

  amqp_basic_consume(consumer->conn, consumer->channel,
                     amqp_cstring_bytes(AMQP_QUEUE_NAME), amqp_empty_bytes,
                     0, /* no_local */
                     0, /* no_ack — we ack manually */
                     0, /* exclusive */
                     amqp_empty_table);
  if (!amqp_reply_ok(amqp_get_rpc_reply(consumer->conn),
                     "basic_consume failed"))
    return false;

  return true;
}

static bool setup_amqp_connection(rabbitmq_consumer_t *consumer) {
  if (consumer == NULL || consumer->cfg == NULL)
    return false;

  consumer->conn = amqp_new_connection();
  if (consumer->conn == NULL)
    return false;

  if (!setup_amqp_socket(consumer)) {
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    return false;
  }

  amqp_rpc_reply_t reply = amqp_login(
      consumer->conn, consumer->cfg->rabbitmq_vhost, 0,
      AMQP_CONN_FRAME_SIZE, AMQP_CONN_HEARTBEAT, AMQP_SASL_METHOD_PLAIN,
      consumer->cfg->rabbitmq_user, consumer->cfg->rabbitmq_pass);

  if (!amqp_reply_ok(reply, "amqp_login failed")) {
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    return false;
  }

  amqp_channel_open(consumer->conn, consumer->channel);
  reply = amqp_get_rpc_reply(consumer->conn);
  if (!amqp_reply_ok(reply, "channel_open failed")) {
    amqp_connection_close(consumer->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    return false;
  }
  consumer->channel_open = true;

  amqp_basic_qos(consumer->conn, consumer->channel, 0, 1, 0);
  if (!amqp_reply_ok(amqp_get_rpc_reply(consumer->conn), "basic_qos failed")) {
    amqp_channel_close(consumer->conn, consumer->channel, AMQP_REPLY_SUCCESS);
    amqp_connection_close(consumer->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    consumer->channel_open = false;
    return false;
  }

  if (!declare_topology(consumer)) {
    amqp_channel_close(consumer->conn, consumer->channel, AMQP_REPLY_SUCCESS);
    amqp_connection_close(consumer->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    consumer->channel_open = false;
    return false;
  }

  consumer->connected = true;
  return true;
}

rabbitmq_consumer_t *rabbitmq_consumer_create(const config_t *cfg,
                                              controller_t *controller) {
  if (cfg == NULL || controller == NULL)
    return NULL;

  rabbitmq_consumer_t *consumer = calloc(1, sizeof(rabbitmq_consumer_t));
  if (consumer == NULL)
    return NULL;

  consumer->cfg = cfg;
  consumer->controller = controller;
  consumer->channel = 1;
  consumer->running = false;
  consumer->connected = false;
  consumer->channel_open = false;

  if (!setup_amqp_connection(consumer)) {
    free(consumer);
    return NULL;
  }

  return consumer;
}

void rabbitmq_consumer_run(rabbitmq_consumer_t *consumer,
                           volatile sig_atomic_t *shutdown_flag) {
  if (consumer == NULL || consumer->controller == NULL)
    return;

  consumer->running = true;

  while (consumer->running) {
    if (shutdown_flag != NULL && *shutdown_flag) {
      consumer->running = false;
      break;
    }

    amqp_envelope_t envelope;
    amqp_maybe_release_buffers(consumer->conn);

    struct timeval timeout = {1, 0}; /* 1s — allows shutdown check */
    amqp_rpc_reply_t reply =
        amqp_consume_message(consumer->conn, &envelope, &timeout, 0);

    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
        reply.library_error == AMQP_STATUS_TIMEOUT) {
      continue;
    }

    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
      log_msg(LOG_ERROR, "rabbitmq", "consume_message failed", NULL, NULL);
      consumer->connected = false;
      consumer->running = false;
      break;
    }

    if (envelope.message.body.len == 0) {
      amqp_basic_nack(consumer->conn, consumer->channel, envelope.delivery_tag,
                      0, 0);
      amqp_destroy_envelope(&envelope);
      continue;
    }

    char *json_str = malloc(envelope.message.body.len + 1);
    if (json_str == NULL) {
      amqp_basic_nack(consumer->conn, consumer->channel, envelope.delivery_tag,
                      0, 1);
      amqp_destroy_envelope(&envelope);
      continue;
    }

    memcpy(json_str, envelope.message.body.bytes, envelope.message.body.len);
    json_str[envelope.message.body.len] = '\0';

    message_t *msg = message_parse(json_str);
    free(json_str);

    if (msg == NULL || !message_is_valid(msg)) {
      amqp_basic_nack(consumer->conn, consumer->channel, envelope.delivery_tag,
                      0, 0);
      message_free(msg);
      amqp_destroy_envelope(&envelope);
      continue;
    }

    result_t result = controller_handle_event(consumer->controller, msg);

    if (result == RESULT_OK) {
      amqp_basic_ack(consumer->conn, consumer->channel, envelope.delivery_tag,
                     0);
    } else {
      /* Requeue only on transient errors. INVALID/ERROR are terminal. */
      int requeue = (result == RESULT_RETRY) ? 1 : 0;
      amqp_basic_nack(consumer->conn, consumer->channel, envelope.delivery_tag,
                      0, requeue);
    }

    message_free(msg);
    amqp_destroy_envelope(&envelope);
  }
}

bool rabbitmq_consumer_is_connected(const rabbitmq_consumer_t *consumer) {
  return consumer && consumer->connected;
}

void rabbitmq_consumer_shutdown(rabbitmq_consumer_t *consumer) {
  if (consumer == NULL)
    return;

  consumer->running = false;
}

void rabbitmq_consumer_free(rabbitmq_consumer_t *consumer) {
  if (consumer == NULL)
    return;

  if (consumer->conn != NULL) {
    if (consumer->channel_open) {
      amqp_channel_close(consumer->conn, consumer->channel,
                         AMQP_REPLY_SUCCESS);
    }
    amqp_connection_close(consumer->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(consumer->conn);
  }

  free(consumer);
}
