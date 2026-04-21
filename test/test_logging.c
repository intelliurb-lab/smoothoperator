#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <jansson.h>
#include "smoothoperator.h"

extern volatile sig_atomic_t log_reopen_requested;

static char *slurp(const char *path, size_t *out_len) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) return NULL;
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (sz < 0) { fclose(fp); return NULL; }
  char *buf = malloc((size_t)sz + 1);
  if (buf == NULL) { fclose(fp); return NULL; }
  size_t r = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  buf[r] = '\0';
  if (out_len) *out_len = r;
  return buf;
}

/* Test: log line is valid JSON even when message contains special chars */
void test_log_json_escapes(void) {
  const char *path = "/tmp/smoothop_log_escape_test.log";
  unlink(path);

  log_init(LOG_DEBUG, path);
  log_msg(LOG_INFO, "test",
          "hello \"world\" with\nnewlines\tand\\backslash",
          "id-1", "type-1");
  log_cleanup();

  char *content = slurp(path, NULL);
  CU_ASSERT_PTR_NOT_NULL_FATAL(content);

  /* Line must end with \n */
  size_t len = strlen(content);
  CU_ASSERT(len > 0 && content[len - 1] == '\n');

  /* Strip trailing newline and parse as JSON */
  content[len - 1] = '\0';
  json_error_t err;
  json_t *obj = json_loads(content, 0, &err);
  CU_ASSERT_PTR_NOT_NULL_FATAL(obj);

  /* Required fields present */
  CU_ASSERT_PTR_NOT_NULL(json_object_get(obj, "timestamp"));
  CU_ASSERT_STRING_EQUAL(
      json_string_value(json_object_get(obj, "level")), "INFO");
  CU_ASSERT_STRING_EQUAL(
      json_string_value(json_object_get(obj, "module")), "test");
  CU_ASSERT_STRING_EQUAL(
      json_string_value(json_object_get(obj, "event_id")), "id-1");

  /* Message must round-trip exactly */
  CU_ASSERT_STRING_EQUAL(
      json_string_value(json_object_get(obj, "message")),
      "hello \"world\" with\nnewlines\tand\\backslash");

  json_decref(obj);
  free(content);
  unlink(path);
}

/* Test: log level filtering drops messages below threshold */
void test_log_level_filter(void) {
  const char *path = "/tmp/smoothop_log_filter_test.log";
  unlink(path);

  log_init(LOG_WARN, path);
  log_msg(LOG_DEBUG, "m", "drop_debug", NULL, NULL);
  log_msg(LOG_INFO, "m", "drop_info", NULL, NULL);
  log_msg(LOG_WARN, "m", "keep_warn", NULL, NULL);
  log_msg(LOG_ERROR, "m", "keep_error", NULL, NULL);
  log_cleanup();

  char *content = slurp(path, NULL);
  CU_ASSERT_PTR_NOT_NULL_FATAL(content);
  CU_ASSERT_PTR_NULL(strstr(content, "drop_debug"));
  CU_ASSERT_PTR_NULL(strstr(content, "drop_info"));
  CU_ASSERT_PTR_NOT_NULL(strstr(content, "keep_warn"));
  CU_ASSERT_PTR_NOT_NULL(strstr(content, "keep_error"));
  free(content);
  unlink(path);
}

/* Test: SIGHUP-triggered reopen preserves the rotated file and re-creates the
 * original path. This simulates what logrotate does. */
void test_log_sighup_reopen(void) {
  const char *orig = "/tmp/smoothop_log_rotate_test.log";
  const char *rotated = "/tmp/smoothop_log_rotate_test.log.1";
  unlink(orig);
  unlink(rotated);

  log_init(LOG_DEBUG, orig);
  log_msg(LOG_INFO, "m", "before", NULL, NULL);

  /* Sanity: original was created */
  struct stat st_before;
  CU_ASSERT_EQUAL_FATAL(stat(orig, &st_before), 0);
  CU_ASSERT(st_before.st_size > 0);

  /* Rotate */
  CU_ASSERT_EQUAL_FATAL(rename(orig, rotated), 0);
  log_reopen_requested = 1;
  log_msg(LOG_INFO, "m", "after", NULL, NULL);

  /* The new file at orig path should contain only the "after" line */
  char *new_content = slurp(orig, NULL);
  CU_ASSERT_PTR_NOT_NULL_FATAL(new_content);
  CU_ASSERT_PTR_NOT_NULL(strstr(new_content, "after"));
  CU_ASSERT_PTR_NULL(strstr(new_content, "before"));
  free(new_content);

  /* The rotated file should only contain "before" */
  char *rot_content = slurp(rotated, NULL);
  CU_ASSERT_PTR_NOT_NULL_FATAL(rot_content);
  CU_ASSERT_PTR_NOT_NULL(strstr(rot_content, "before"));
  CU_ASSERT_PTR_NULL(strstr(rot_content, "after"));
  free(rot_content);

  log_cleanup();
  unlink(orig);
  unlink(rotated);
}

/* Test: NULL module defaults to "unknown" (never crashes) */
void test_log_null_module_safe(void) {
  const char *path = "/tmp/smoothop_log_null_test.log";
  unlink(path);
  log_init(LOG_DEBUG, path);
  log_msg(LOG_INFO, NULL, "msg", NULL, NULL);
  log_cleanup();

  char *content = slurp(path, NULL);
  CU_ASSERT_PTR_NOT_NULL_FATAL(content);
  CU_ASSERT_PTR_NOT_NULL(strstr(content, "\"module\":\"unknown\""));
  free(content);
  unlink(path);
}

CU_pSuite suite_logging(void) {
  CU_pSuite pSuite = CU_add_suite("logging", 0, 0);
  if (pSuite == NULL)
    return NULL;

  CU_add_test(pSuite, "JSON escapes special chars", test_log_json_escapes);
  CU_add_test(pSuite, "level filter drops below threshold",
              test_log_level_filter);
  CU_add_test(pSuite, "SIGHUP triggers log rotation",
              test_log_sighup_reopen);
  CU_add_test(pSuite, "NULL module is safe", test_log_null_module_safe);

  return pSuite;
}
