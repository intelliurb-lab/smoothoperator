#include "memphis.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static FILE *logfile = NULL;
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

void log_init(log_level_t level, const char *logfile_path) {
  log_level = level;

  if (!logfile_path || strcmp(logfile_path, "stdout") == 0) {
    logfile = stdout;
    return;
  }

  int fd = open(logfile_path,
                O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC,
                0600);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open log file safely: %s\n",
            strerror(errno));
    logfile = stderr;
    return;
  }
  logfile = fdopen(fd, "a");
  if (logfile == NULL) {
    close(fd);
    logfile = stderr;
  }
}

void log_msg(log_level_t level, const char *module, const char *message,
             const char *event_id, const char *event_type) {
  if (level < log_level)
    return;

  if (logfile == NULL)
    logfile = stdout;

  time_t now = time(NULL);
  struct tm tm_info;
  gmtime_r(&now, &tm_info);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

  fputs("{\"timestamp\":", logfile);
  json_escape_write(logfile, timestamp);
  fputs(",\"level\":", logfile);
  json_escape_write(logfile, level_names[level]);
  fputs(",\"module\":", logfile);
  json_escape_write(logfile, module ? module : "unknown");

  if (message) {
    fputs(",\"message\":", logfile);
    json_escape_write(logfile, message);
  }
  if (event_id) {
    fputs(",\"event_id\":", logfile);
    json_escape_write(logfile, event_id);
  }
  if (event_type) {
    fputs(",\"event_type\":", logfile);
    json_escape_write(logfile, event_type);
  }

  fputs("}\n", logfile);
  fflush(logfile);
}

void log_cleanup(void) {
  if (logfile && logfile != stdout && logfile != stderr) {
    fclose(logfile);
  }
  logfile = NULL;
}
