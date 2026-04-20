#ifndef RABBITMQ_CONSUMER_H
#define RABBITMQ_CONSUMER_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

typedef struct rabbitmq_consumer rabbitmq_consumer_t;

/* Create consumer connection */
rabbitmq_consumer_t *rabbitmq_consumer_create(const config_t *cfg);

/* Start consuming (blocking loop) */
void rabbitmq_consumer_run(rabbitmq_consumer_t *consumer);

/* Check if connected */
bool rabbitmq_consumer_is_connected(const rabbitmq_consumer_t *consumer);

/* Request shutdown */
void rabbitmq_consumer_shutdown(rabbitmq_consumer_t *consumer);

/* Cleanup */
void rabbitmq_consumer_free(rabbitmq_consumer_t *consumer);

#endif /* RABBITMQ_CONSUMER_H */
