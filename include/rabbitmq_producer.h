#ifndef RABBITMQ_PRODUCER_H
#define RABBITMQ_PRODUCER_H

#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "ls_controller.h"

typedef struct rabbitmq_producer rabbitmq_producer_t;

/* Create producer connection. The controller is borrowed — the caller retains
 * ownership and must free it after the producer is destroyed. */
rabbitmq_producer_t *rabbitmq_producer_create(const config_t *cfg,
                                              controller_t *controller);

/* Start listener (blocking loop). Periodically polls Liquidsoap state and
 * publishes events. If shutdown_flag is non-NULL, the loop checks *shutdown_flag
 * between polls — set it from a signal handler to stop without calling
 * non-async-signal-safe functions. */
void rabbitmq_producer_run(rabbitmq_producer_t *producer,
                           volatile sig_atomic_t *shutdown_flag);

/* Check if connected */
bool rabbitmq_producer_is_connected(const rabbitmq_producer_t *producer);

/* Manually publish an event (used by consumer to forward responses) */
bool rabbitmq_producer_publish(rabbitmq_producer_t *producer,
                               const char *event_type, const char *json_body);

/* Request shutdown */
void rabbitmq_producer_shutdown(rabbitmq_producer_t *producer);

/* Cleanup */
void rabbitmq_producer_free(rabbitmq_producer_t *producer);

#endif /* RABBITMQ_PRODUCER_H */
