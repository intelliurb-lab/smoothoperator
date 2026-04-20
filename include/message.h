#ifndef MESSAGE_H
#define MESSAGE_H

#include <jansson.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint32_t version;
  char *id;
  char *timestamp;
  char *source;
  char *event;
  json_t *payload;
} message_t;

/* Parse JSON string to message */
message_t *message_parse(const char *json_str);

/* Validate message schema */
bool message_is_valid(const message_t *msg);

/* Getters */
uint32_t message_get_version(const message_t *msg);
const char *message_get_id(const message_t *msg);
const char *message_get_timestamp(const message_t *msg);
const char *message_get_source(const message_t *msg);
const char *message_get_event(const message_t *msg);
json_t *message_get_payload(const message_t *msg);

/* Cleanup */
void message_free(message_t *msg);

#endif /* MESSAGE_H */
