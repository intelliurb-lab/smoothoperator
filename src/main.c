#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <pthread.h>

#include "smoothoperator.h"
#include "config.h"
#include "ls_controller.h"
#include "rabbitmq_consumer.h"
#include "rabbitmq_producer.h"

static volatile sig_atomic_t shutdown_requested = 0;
extern volatile sig_atomic_t log_reopen_requested;

static void handle_signal(int sig) {
  (void)sig;
  shutdown_requested = 1;
}

static void handle_sighup(int sig) {
  (void)sig;
  log_reopen_requested = 1;
}

static void setup_signals(void) {
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;

  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);

  sa.sa_handler = handle_sighup;
  sigaction(SIGHUP, &sa, NULL);

  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);
}

struct producer_thread_args {
  rabbitmq_producer_t *producer;
  volatile sig_atomic_t *shutdown_flag;
};

static void *producer_thread_fn(void *arg) {
  struct producer_thread_args *args = (struct producer_thread_args *)arg;
  if (args == NULL || args->producer == NULL)
    return NULL;

  rabbitmq_producer_run(args->producer, args->shutdown_flag);
  return NULL;
}

static bool drop_privileges(const char *username) {
  if (geteuid() != 0) return true;

  struct passwd *pw = getpwnam(username);
  if (pw == NULL) {
    fprintf(stderr, "ERROR: user '%s' not found\n", username);
    return false;
  }

  if (pw->pw_uid == 0) {
    fprintf(stderr, "ERROR: refusing to drop to uid 0 ('%s')\n", username);
    return false;
  }

  if (initgroups(username, pw->pw_gid) < 0) {
    perror("initgroups");
    return false;
  }

  if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0) {
    perror("setresgid");
    return false;
  }

  if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
    perror("setresuid");
    return false;
  }

  uid_t ruid, euid, suid;
  gid_t rgid, egid, sgid;
  if (getresuid(&ruid, &euid, &suid) < 0 ||
      getresgid(&rgid, &egid, &sgid) < 0) {
    perror("getres{u,g}id");
    return false;
  }
  if (ruid != pw->pw_uid || euid != pw->pw_uid || suid != pw->pw_uid ||
      rgid != pw->pw_gid || egid != pw->pw_gid || sgid != pw->pw_gid) {
    fprintf(stderr, "ERROR: privilege drop verification failed\n");
    return false;
  }

  if (setuid(0) != -1 || errno != EPERM) {
    fprintf(stderr, "ERROR: can still regain root\n");
    return false;
  }

  return true;
}

static void disable_core_dumps(void) {
  struct rlimit rl = {0, 0};
  if (setrlimit(RLIMIT_CORE, &rl) < 0) {
    perror("setrlimit RLIMIT_CORE");
  }

  if (prctl(PR_SET_DUMPABLE, 0) < 0) {
    perror("prctl PR_SET_DUMPABLE");
  }
}

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s [OPTIONS]\n", prog);
  fprintf(stderr, "OPTIONS:\n");
  fprintf(stderr, "  -c, --config FILE    Config file (default: smoothoperator.conf)\n");
  fprintf(stderr, "                       Passwords are loaded from .env,\n");
  fprintf(stderr, "                       smoothoperator.env, or the environment.\n");
  fprintf(stderr, "  -l, --log-level LEVEL Log level (DEBUG, INFO, WARN, ERROR)\n");
  fprintf(stderr, "  -v, --version        Show version\n");
  fprintf(stderr, "  -h, --help           Show this help\n");
}

int main(int argc, char *argv[]) {
  const char *config_file = "smoothoperator.conf";
  const char *log_level = NULL;

  int opt;
  static struct option long_options[] = {
    {"config", required_argument, 0, 'c'},
    {"log-level", required_argument, 0, 'l'},
    {"version", no_argument, 0, 'v'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };

  while ((opt = getopt_long(argc, argv, "c:l:vh", long_options, NULL)) != -1) {
    switch (opt) {
      case 'c':
        config_file = optarg;
        break;
      case 'l':
        log_level = optarg;
        break;
      case 'v':
        printf("%s %s\n", SMOOTHOP_NAME, SMOOTHOP_VERSION);
        return 0;
      case 'h':
        usage(argv[0]);
        return 0;
      default:
        usage(argv[0]);
        return 1;
    }
  }

  /* Load config */
  config_t *cfg = config_load(config_file);
  if (cfg == NULL) {
    fprintf(stderr, "Failed to load config\n");
    return 1;
  }

  if (!config_is_valid(cfg)) {
    fprintf(stderr, "Invalid config\n");
    config_free(cfg);
    return 1;
  }

  /* Disable core dumps to protect credentials in memory */
  disable_core_dumps();

  /* Override log level if provided */
  if (log_level != NULL) {
    if (cfg->log_level) free(cfg->log_level);
    cfg->log_level = strdup(log_level);
    if (cfg->log_level == NULL) {
      fprintf(stderr, "OOM: cannot allocate log_level\n");
      config_free(cfg);
      return 1;
    }
  }

  /* Initialize logging */
  log_init(config_get_log_level(cfg), cfg->log_file);
  log_msg(LOG_INFO, "main", "SmoothOperator starting", NULL, NULL);

  /* Drop privileges if running as root and SMOOTHOP_USER is set */
  const char *smoothoperator_user = getenv("SMOOTHOP_USER");
  if (geteuid() == 0 && smoothoperator_user) {
    if (!drop_privileges(smoothoperator_user)) {
      log_msg(LOG_ERROR, "main", "Failed to drop privileges", NULL, NULL);
      config_free(cfg);
      log_cleanup();
      return 1;
    }
    log_msg(LOG_INFO, "main", "Dropped privileges", NULL, NULL);
  }

  /* Setup signal handlers */
  setup_signals();

  /* Create controller */
  controller_t *ctrl = controller_create(cfg);
  if (ctrl == NULL) {
    log_msg(LOG_ERROR, "main", "Failed to create controller", NULL, NULL);
    config_free(cfg);
    log_cleanup();
    return 1;
  }

  /* Create consumer and start consuming */
  rabbitmq_consumer_t *consumer = rabbitmq_consumer_create(cfg, ctrl);
  if (consumer == NULL) {
    log_msg(LOG_ERROR, "main", "Failed to create RabbitMQ consumer", NULL,
            NULL);
    controller_free(ctrl);
    config_free(cfg);
    log_cleanup();
    return 1;
  }

  /* Create producer for state events */
  rabbitmq_producer_t *producer = rabbitmq_producer_create(cfg, ctrl);
  if (producer == NULL) {
    log_msg(LOG_WARN, "main", "Failed to create RabbitMQ producer (state polling disabled)", NULL,
            NULL);
  }

  log_msg(LOG_INFO, "main", "SmoothOperator started successfully", NULL, NULL);

  /* Start producer in a background thread */
  pthread_t producer_tid = 0;
  int producer_created = 0;
  if (producer != NULL) {
    struct producer_thread_args *args = malloc(sizeof(struct producer_thread_args));
    if (args == NULL) {
      log_msg(LOG_WARN, "main", "Failed to allocate producer thread args", NULL, NULL);
      rabbitmq_producer_free(producer);
      producer = NULL;
    } else {
      args->producer = producer;
      args->shutdown_flag = &shutdown_requested;
      if (pthread_create(&producer_tid, NULL, producer_thread_fn, args) != 0) {
        log_msg(LOG_WARN, "main", "Failed to create producer thread", NULL, NULL);
        free(args);
        rabbitmq_producer_free(producer);
        producer = NULL;
      } else {
        producer_created = 1;
      }
    }
  }

  /* Main loop: consumer runs until shutdown (signal sets the flag) */
  rabbitmq_consumer_run(consumer, &shutdown_requested);

  /* Cleanup */
  if (producer != NULL) {
    rabbitmq_producer_shutdown(producer);
  }

  if (producer_created) {
    pthread_join(producer_tid, NULL);
  }

  if (producer != NULL) {
    rabbitmq_producer_free(producer);
  }
  rabbitmq_consumer_free(consumer);
  controller_free(ctrl);
  config_free(cfg);
  log_cleanup();

  log_msg(LOG_INFO, "main", "SmoothOperator shutdown complete", NULL, NULL);
  return 0;
}
