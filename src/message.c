#include "message.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAX_ID_LEN 64
#define MAX_EVENT_LEN 128
#define MAX_TIMESTAMP_LEN 32
#define MAX_SOURCE_LEN 64
#define MAX_PAYLOAD_SIZE (64 * 1024)

static char *strdup_bounded(const char *s, size_t max) {
  if (s == NULL) return NULL;
  size_t len = strnlen(s, max + 1);
  if (len > max) return NULL;
  return strndup(s, len);
}

message_t *message_parse(const char *json_str) {
  if (json_str == NULL)
    return NULL;

  json_error_t error;
  json_t *root = json_loads(json_str, 0, &error);
  if (root == NULL)
    return NULL;

  message_t *msg = calloc(1, sizeof(message_t));
  if (msg == NULL) {
    json_decref(root);
    return NULL;
  }

  /* Extract and validate fields */
  json_t *version_obj = json_object_get(root, "version");
  if (!json_is_integer(version_obj)) {
    message_free(msg);
    json_decref(root);
    return NULL;
  }
  json_int_t ver = json_integer_value(version_obj);
  if (ver < 0 || ver > UINT32_MAX) {
    message_free(msg);
    json_decref(root);
    return NULL;
  }
  msg->version = (uint32_t)ver;

  json_t *id_obj = json_object_get(root, "id");
  if (json_is_string(id_obj)) {
    msg->id = strdup_bounded(json_string_value(id_obj), MAX_ID_LEN);
    if (msg->id == NULL) {
      message_free(msg);
      json_decref(root);
      return NULL;
    }
  }

  json_t *timestamp_obj = json_object_get(root, "timestamp");
  if (json_is_string(timestamp_obj)) {
    msg->timestamp = strdup_bounded(json_string_value(timestamp_obj), MAX_TIMESTAMP_LEN);
    if (msg->timestamp == NULL) {
      message_free(msg);
      json_decref(root);
      return NULL;
    }
  }

  json_t *source_obj = json_object_get(root, "source");
  if (json_is_string(source_obj)) {
    msg->source = strdup_bounded(json_string_value(source_obj), MAX_SOURCE_LEN);
    if (msg->source == NULL) {
      message_free(msg);
      json_decref(root);
      return NULL;
    }
  }

  json_t *event_obj = json_object_get(root, "event");
  if (json_is_string(event_obj)) {
    msg->event = strdup_bounded(json_string_value(event_obj), MAX_EVENT_LEN);
    if (msg->event == NULL) {
      message_free(msg);
      json_decref(root);
      return NULL;
    }
  }

  json_t *payload_obj = json_object_get(root, "payload");
  if (json_is_object(payload_obj)) {
    msg->payload = json_deep_copy(payload_obj);
    if (msg->payload == NULL) {
      message_free(msg);
      json_decref(root);
      return NULL;
    }
  }

  json_decref(root);
  return msg;
}

bool message_is_valid(const message_t *msg) {
  return msg != NULL && msg->version == 1 && msg->id != NULL &&
         msg->timestamp != NULL && msg->event != NULL;
}

uint32_t message_get_version(const message_t *msg) {
  return msg ? msg->version : 0;
}

const char *message_get_id(const message_t *msg) {
  return msg ? msg->id : NULL;
}

const char *message_get_timestamp(const message_t *msg) {
  return msg ? msg->timestamp : NULL;
}

const char *message_get_source(const message_t *msg) {
  return msg ? msg->source : NULL;
}

const char *message_get_event(const message_t *msg) {
  return msg ? msg->event : NULL;
}

json_t *message_get_payload(const message_t *msg) {
  return msg ? msg->payload : NULL;
}

void message_free(message_t *msg) {
  if (msg == NULL)
    return;

  free(msg->id);
  free(msg->timestamp);
  free(msg->source);
  free(msg->event);
  if (msg->payload)
    json_decref(msg->payload);
  free(msg);
}
