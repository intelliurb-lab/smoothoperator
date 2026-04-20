#ifndef LS_CONTROLLER_H
#define LS_CONTROLLER_H

#include "config.h"
#include "message.h"
#include "memphis.h"

typedef struct controller controller_t;

/* Create controller */
controller_t *controller_create(const config_t *cfg);

/* Handle event from RabbitMQ */
result_t controller_handle_event(controller_t *ctrl, const message_t *msg);

/* Health check */
bool controller_is_healthy(const controller_t *ctrl);

/* Cleanup */
void controller_free(controller_t *ctrl);

#endif /* LS_CONTROLLER_H */
