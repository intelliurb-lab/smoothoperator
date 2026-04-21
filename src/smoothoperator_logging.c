#include "smoothoperator.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

/* Log line is rendered into a buffer first, then written in a single
 * write() call so two concurrent loggers cannot interleave, and so that
 * SIGHUP-triggered fd swaps are safe (no stdio state to flush).
 * Set by a signal handler; the next log_msg observes it and reopens. */
volatile sig_atomic_t log_reopen_requested = 0;

static int log_fd = -1;
static char *log_path = NULL; /* NULL when logging to an inherited stream */
static log_level_t log_level = LOG_INFO;

static const char *level_names[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static void json_escape_write(FILE *fp, const char *s) {
  if (!s) {
    fputs("null", fp);
    return;
  }
  fputc('"', fp);
  for (const char *p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    switch (c) {
      case '"':  fputs("\\\"", fp); break;
      case '\\': fputs("\\\\", fp); break;
      case '\n': fputs("\\n", fp); break;
      case '\r': fputs("\\r", fp); break;
      case '\t': fputs("\\t", fp); break;
      case '\b': fputs("\\b", fp); break;
      case '\f': fputs("\\f", fp); break;
      default:
        if (c < 0x20) fprintf(fp, "\\u%04x", c);
        else fputc(c, fp);
    }
  }
  fputc('"', fp);
}

static bool open_log_path(const char *path, int *out_fd) {
  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC,
                0600);
  if (fd < 0)
    return false;
  *out_fd = fd;
  return true;
}

static void close_if_owned(int fd) {
  if (fd >= 0 && fd != STDOUT_FILENO && fd != STDERR_FILENO)
    close(fd);
}

void log_init(log_level_t level, const char *logfile_path) {
  log_level = level;

  close_if_owned(log_fd);
  log_fd = -1;
  free(log_path);
  log_path = NULL;

  if (!logfile_path || strcmp(logfile_path, "stdout") == 0) {
    log_fd = STDOUT_FILENO;
    return;
  }

  int fd;
  if (!open_log_path(logfile_path, &fd)) {
    fprintf(stderr, "ERROR: cannot open log file safely: %s\n",
            strerror(errno));
    log_fd = STDERR_FILENO;
    return;
  }

  log_path = strdup(logfile_path);
  if (log_path == NULL) {
    /* Without a remembered path SIGHUP reopen is a no-op; still usable. */
    fprintf(stderr, "WARNING: cannot remember log path (OOM), reopen disabled\n");
  }
  log_fd = fd;
}

static void reopen_if_requested(void) {
  if (!log_reopen_requested)
    return;
  log_reopen_requested = 0;

  if (log_path == NULL)
    return; /* logging to stdout/stderr or path was not remembered */

  int new_fd;
  if (!open_log_path(log_path, &new_fd))
    return; /* keep old fd if reopen fails */

  int old_fd = log_fd;
  log_fd = new_fd;
  close_if_owned(old_fd);
}

void log_msg(log_level_t level, const char *module, const char *message,
             const char *event_id, const char *event_type) {
  if (level < log_level)
    return;
  if ((int)level < 0 || (size_t)level >= sizeof(level_names) / sizeof(level_names[0]))
    return;

  reopen_if_requested();

  if (log_fd < 0)
    log_fd = STDOUT_FILENO;

  time_t now = time(NULL);
  struct tm tm_info;
  gmtime_r(&now, &tm_info);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

  char *buf = NULL;
  size_t len = 0;
  FILE *mem = open_memstream(&buf, &len);
  if (mem == NULL)
    return;

  fputs("{\"timestamp\":", mem);
  json_escape_write(mem, timestamp);
  fputs(",\"level\":", mem);
  json_escape_write(mem, level_names[level]);
  fputs(",\"module\":", mem);
  json_escape_write(mem, module ? module : "unknown");

  if (message) {
    fputs(",\"message\":", mem);
    json_escape_write(mem, message);
  }
  if (event_id) {
    fputs(",\"event_id\":", mem);
    json_escape_write(mem, event_id);
  }
  if (event_type) {
    fputs(",\"event_type\":", mem);
    json_escape_write(mem, event_type);
  }

  fputs("}\n", mem);
  fclose(mem); /* flushes and finalizes buf/len */

  if (buf != NULL && len > 0) {
    const char *p = buf;
    size_t remaining = len;
    while (remaining > 0) {
      ssize_t written = write(log_fd, p, remaining);
      if (written < 0) {
        if (errno == EINTR)
          continue;
        break; /* nothing we can do from here */
      }
      if (written == 0)
        break;
      p += written;
      remaining -= (size_t)written;
    }
  }
  free(buf);
}

void log_cleanup(void) {
  close_if_owned(log_fd);
  log_fd = -1;
  free(log_path);
  log_path = NULL;
}
