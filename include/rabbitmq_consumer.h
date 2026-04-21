#ifndef RABBITMQ_CONSUMER_H
#define RABBITMQ_CONSUMER_H

#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "ls_controller.h"

typedef struct rabbitmq_consumer rabbitmq_consumer_t;

/* Create consumer connection. The controller is borrowed — the caller retains
 * ownership and must free it after the consumer is destroyed. */
rabbitmq_consumer_t *rabbitmq_consumer_create(const config_t *cfg,
                                              controller_t *controller);

/* Start consuming (blocking loop). If shutdown_flag is non-NULL, the loop
 * checks *shutdown_flag between messages — set it from a signal handler to
 * stop the loop without calling any non-async-signal-safe function. */
void rabbitmq_consumer_run(rabbitmq_consumer_t *consumer,
                           volatile sig_atomic_t *shutdown_flag);

/* Check if connected */
bool rabbitmq_consumer_is_connected(const rabbitmq_consumer_t *consumer);

/* Request shutdown */
void rabbitmq_consumer_shutdown(rabbitmq_consumer_t *consumer);

/* Cleanup */
void rabbitmq_consumer_free(rabbitmq_consumer_t *consumer);

#endif /* RABBITMQ_CONSUMER_H */
