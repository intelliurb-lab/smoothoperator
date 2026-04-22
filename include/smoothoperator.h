#ifndef SMOOTHOP_H
#define SMOOTHOP_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Version */
#define SMOOTHOP_VERSION "0.2.0"
#define SMOOTHOP_NAME "smoothoperator"

/* Log levels */
typedef enum {
  LOG_DEBUG = 0,
  LOG_INFO = 1,
  LOG_WARN = 2,
  LOG_ERROR = 3,
  LOG_FATAL = 4
} log_level_t;

/* Result codes */
typedef enum {
  RESULT_OK = 0,
  RESULT_ERROR = 1,
  RESULT_RETRY = 2,
  RESULT_INVALID = 3
} result_t;

/* Forward declarations (structs defined in specific headers) */
struct config;
struct message;
struct ls_socket;
struct rabbitmq_consumer;
struct controller;

/* Logging */
void log_init(log_level_t level, const char *logfile);
void log_msg(log_level_t level, const char *module, const char *message,
             const char *event_id, const char *event_type);
void log_cleanup(void);

#endif /* SMOOTHOP_H */
