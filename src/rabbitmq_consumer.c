#include "rabbitmq_consumer.h"
#include "ls_controller.h"
#include "message.h"
#include "smoothoperator.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <amqp.h>
#include <amqp_tcp_socket.h>

#define AMQP_DEFAULT_FRAME_SIZE 131072
#define AMQP_DEFAULT_HEARTBEAT 0
#define AMQP_QUEUE_NAME "ls.commands"
#define AMQP_EXCHANGE_NAME "radio.events"

struct rabbitmq_consumer {
  amqp_connection_state_t conn;
  amqp_channel_t channel;
  const config_t *cfg;
  bool running;
  bool connected;
  controller_t *controller;
};

static bool setup_amqp_connection(rabbitmq_consumer_t *consumer) {
  if (consumer == NULL || consumer->cfg == NULL)
    return false;

  consumer->conn = amqp_new_connection();
  if (consumer->conn == NULL)
    return false;

  amqp_socket_t *socket = amqp_tcp_socket_new(consumer->conn);
  if (socket == NULL) {
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    return false;
  }

  int status = amqp_socket_open(socket, consumer->cfg->rabbitmq_host,
                                consumer->cfg->rabbitmq_port);
  if (status != AMQP_STATUS_OK) {
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    return false;
  }

  amqp_rpc_reply_t reply = amqp_login(
      consumer->conn, consumer->cfg->rabbitmq_vhost, 0,
      AMQP_DEFAULT_FRAME_SIZE, AMQP_DEFAULT_HEARTBEAT,
      AMQP_SASL_METHOD_PLAIN, consumer->cfg->rabbitmq_user,
      consumer->cfg->rabbitmq_pass);

  if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    return false;
  }

  amqp_channel_open(consumer->conn, consumer->channel);
  reply = amqp_get_rpc_reply(consumer->conn);
  if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
    amqp_destroy_connection(consumer->conn);
    consumer->conn = NULL;
    return false;
  }

  amqp_basic_qos(consumer->conn, consumer->channel, 0, 1, 0);

  consumer->connected = true;
  return true;
}

rabbitmq_consumer_t *rabbitmq_consumer_create(const config_t *cfg) {
  if (cfg == NULL)
    return NULL;

  rabbitmq_consumer_t *consumer = calloc(1, sizeof(rabbitmq_consumer_t));
  if (consumer == NULL)
    return NULL;

  consumer->cfg = cfg;
  consumer->channel = 1;
  consumer->running = false;
  consumer->connected = false;

  if (!setup_amqp_connection(consumer)) {
    free(consumer);
    return NULL;
  }

  consumer->controller = controller_create(cfg);
  if (consumer->controller == NULL) {
    amqp_destroy_connection(consumer->conn);
    free(consumer);
    return NULL;
  }

  return consumer;
}

void rabbitmq_consumer_run(rabbitmq_consumer_t *consumer) {
  if (consumer == NULL || consumer->controller == NULL)
    return;

  consumer->running = true;

  while (consumer->running) {
    amqp_envelope_t envelope;
    amqp_rpc_reply_t reply = amqp_consume_message(
        consumer->conn, &envelope, NULL, 0);

    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
      consumer->connected = false;
      consumer->running = false;
      break;
    }

    if (envelope.message.body.len == 0) {
      amqp_destroy_envelope(&envelope);
      continue;
    }

    char *json_str = malloc(envelope.message.body.len + 1);
    if (json_str == NULL) {
      amqp_basic_nack(consumer->conn, consumer->channel,
                      envelope.delivery_tag, 0, 1);
      amqp_destroy_envelope(&envelope);
      continue;
    }

    memcpy(json_str, envelope.message.body.bytes,
           envelope.message.body.len);
    json_str[envelope.message.body.len] = '\0';

    message_t *msg = message_parse(json_str);
    free(json_str);

    if (msg == NULL || !message_is_valid(msg)) {
      amqp_basic_nack(consumer->conn, consumer->channel,
                      envelope.delivery_tag, 0, 0);
      message_free(msg);
      amqp_destroy_envelope(&envelope);
      continue;
    }

    result_t result = controller_handle_event(consumer->controller, msg);

    if (result == RESULT_OK) {
      amqp_basic_ack(consumer->conn, consumer->channel,
                     envelope.delivery_tag, 0);
    } else {
      amqp_basic_nack(consumer->conn, consumer->channel,
                      envelope.delivery_tag, 0, 1);
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

  if (consumer->controller != NULL) {
    controller_free(consumer->controller);
  }

  if (consumer->conn != NULL) {
    amqp_channel_close(consumer->conn, consumer->channel,
                       AMQP_REPLY_SUCCESS);
    amqp_connection_close(consumer->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(consumer->conn);
  }

  free(consumer);
}
