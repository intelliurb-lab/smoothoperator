#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "config.h"

static void setup_env_vars(void) {
  /* Clear state that previous tests may have set, so each test starts
   * from a known baseline. */
  unsetenv("LIQUIDSOAP_PROTOCOL");
  unsetenv("LIQUIDSOAP_SOCKET_PATH");
  unsetenv("RABBITMQ_TLS_ENABLED");
  unsetenv("RABBITMQ_TLS_CA_CERT");
  unsetenv("RABBITMQ_TLS_CLIENT_CERT");
  unsetenv("RABBITMQ_TLS_CLIENT_KEY");

  setenv("RABBITMQ_HOST", "localhost", 1);
  setenv("RABBITMQ_PORT", "5672", 1);
  setenv("RABBITMQ_USER", "testuser", 1);
  setenv("RABBITMQ_PASS", "testpass123456789", 1);
  setenv("RABBITMQ_VHOST", "/", 1);
  setenv("LIQUIDSOAP_HOST", "127.0.0.1", 1);
  setenv("LIQUIDSOAP_PORT", "1234", 1);
  setenv("LOG_FILE", "/tmp/smoothoperator.log", 1);
  setenv("LOG_LEVEL", "INFO", 1);
}

/* Test: load from environment variables */
void test_load_with_defaults(void) {
  setup_env_vars();
  config_t *cfg = config_load(NULL);

  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_STRING_EQUAL(cfg->rabbitmq_host, "localhost");
  CU_ASSERT_EQUAL(cfg->rabbitmq_port, 5672);
  CU_ASSERT_STRING_EQUAL(cfg->rabbitmq_user, "testuser");
  CU_ASSERT_STRING_EQUAL(cfg->rabbitmq_pass, "testpass123456789");
  CU_ASSERT_STRING_EQUAL(cfg->liquidsoap_host, "127.0.0.1");
  CU_ASSERT_EQUAL(cfg->liquidsoap_port, 1234);

  config_free(cfg);
}

/* Test: config validation */
void test_is_valid(void) {
  setup_env_vars();
  config_t *cfg = config_load(NULL);

  /* With all env vars set, config should be valid */
  CU_ASSERT_TRUE(config_is_valid(cfg));

  config_free(cfg);
}

/* Test: log level DEBUG */
void test_log_level_debug(void) {
  setup_env_vars();
  setenv("LOG_LEVEL", "DEBUG", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_EQUAL(config_get_log_level(cfg), 0);
  config_free(cfg);
}

/* Test: log level INFO */
void test_log_level_info(void) {
  setup_env_vars();
  setenv("LOG_LEVEL", "INFO", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_EQUAL(config_get_log_level(cfg), 1);
  config_free(cfg);
}

/* Test: log level WARN */
void test_log_level_warn(void) {
  setup_env_vars();
  setenv("LOG_LEVEL", "WARN", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_EQUAL(config_get_log_level(cfg), 2);
  config_free(cfg);
}

/* Test: log level ERROR */
void test_log_level_error(void) {
  setup_env_vars();
  setenv("LOG_LEVEL", "ERROR", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_EQUAL(config_get_log_level(cfg), 3);
  config_free(cfg);
}

/* Test: log level FATAL */
void test_log_level_fatal(void) {
  setup_env_vars();
  setenv("LOG_LEVEL", "FATAL", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_EQUAL(config_get_log_level(cfg), 4);
  config_free(cfg);
}

/* Test: invalid config (null) */
void test_is_valid_null(void) {
  CU_ASSERT_FALSE(config_is_valid(NULL));
}

/* Test: protocol telnet (default) */
void test_protocol_telnet_default(void) {
  setup_env_vars();
  unsetenv("LIQUIDSOAP_PROTOCOL");
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_EQUAL(cfg->liquidsoap_protocol, LS_PROTO_TELNET);
  config_free(cfg);
}

/* Test: protocol telnet explicit */
void test_protocol_telnet_explicit(void) {
  setup_env_vars();
  setenv("LIQUIDSOAP_PROTOCOL", "telnet", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_EQUAL(cfg->liquidsoap_protocol, LS_PROTO_TELNET);
  CU_ASSERT_TRUE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: protocol socket */
void test_protocol_socket(void) {
  setup_env_vars();
  setenv("LIQUIDSOAP_PROTOCOL", "socket", 1);
  setenv("LIQUIDSOAP_SOCKET_PATH", "/var/run/liquidsoap/ls.sock", 1);
  unsetenv("LIQUIDSOAP_HOST");
  unsetenv("LIQUIDSOAP_PORT");
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_EQUAL(cfg->liquidsoap_protocol, LS_PROTO_SOCKET);
  CU_ASSERT_STRING_EQUAL(cfg->liquidsoap_socket_path,
                         "/var/run/liquidsoap/ls.sock");
  CU_ASSERT_TRUE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: socket protocol without path (invalid) */
void test_protocol_socket_no_path(void) {
  setup_env_vars();
  setenv("LIQUIDSOAP_PROTOCOL", "socket", 1);
  unsetenv("LIQUIDSOAP_SOCKET_PATH");
  unsetenv("LIQUIDSOAP_HOST");
  unsetenv("LIQUIDSOAP_PORT");
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: socket path not absolute (invalid) */
void test_protocol_socket_relative_path(void) {
  setup_env_vars();
  setenv("LIQUIDSOAP_PROTOCOL", "socket", 1);
  setenv("LIQUIDSOAP_SOCKET_PATH", "relative/path.sock", 1);
  unsetenv("LIQUIDSOAP_HOST");
  unsetenv("LIQUIDSOAP_PORT");
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: socket path too long (invalid) */
void test_protocol_socket_path_too_long(void) {
  setup_env_vars();
  setenv("LIQUIDSOAP_PROTOCOL", "socket", 1);
  char long_path[256];
  memset(long_path, 'a', 200);
  long_path[200] = '\0';
  long_path[0] = '/';
  setenv("LIQUIDSOAP_SOCKET_PATH", long_path, 1);
  unsetenv("LIQUIDSOAP_HOST");
  unsetenv("LIQUIDSOAP_PORT");
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: password shorter than 16 chars is rejected */
void test_pass_too_short(void) {
  setup_env_vars();
  setenv("RABBITMQ_PASS", "short123", 1); /* 8 chars */
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: password exactly 16 chars is accepted */
void test_pass_min_length(void) {
  setup_env_vars();
  setenv("RABBITMQ_PASS", "0123456789abcdef", 1); /* 16 chars */
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_TRUE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: non-numeric port rejected */
void test_invalid_port_non_numeric(void) {
  setup_env_vars();
  setenv("RABBITMQ_PORT", "abc", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  /* parse failure falls back to default=0 → config invalid */
  CU_ASSERT_EQUAL(cfg->rabbitmq_port, 0);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: port overflow (>65535) rejected */
void test_invalid_port_overflow(void) {
  setup_env_vars();
  setenv("RABBITMQ_PORT", "70000", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_EQUAL(cfg->rabbitmq_port, 0);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: negative port rejected (strtoul silent wrap) */
void test_invalid_port_negative(void) {
  setup_env_vars();
  setenv("RABBITMQ_PORT", "-1", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_EQUAL(cfg->rabbitmq_port, 0);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
}

/* Test: invalid bool falls back to default (does not crash) */
void test_invalid_bool_tls(void) {
  setup_env_vars();
  setenv("RABBITMQ_TLS_ENABLED", "maybe", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_FALSE(cfg->rabbitmq_tls_enabled); /* defaults to false */
  config_free(cfg);
  unsetenv("RABBITMQ_TLS_ENABLED");
}

/* Helper: write a temporary config file. Returns path (caller must unlink). */
static char *write_tmp_conf(const char *content) {
  char *path = strdup("/tmp/smoothop_test_XXXXXX.conf");
  int fd = mkstemps(path, 5);
  if (fd < 0) { free(path); return NULL; }
  ssize_t wrote = write(fd, content, strlen(content));
  close(fd);
  if (wrote < 0 || (size_t)wrote != strlen(content)) {
    unlink(path);
    free(path);
    return NULL;
  }
  return path;
}

/* Test: .conf file provides values when env is unset */
void test_conf_file_loaded(void) {
  /* Start from a clean slate — unset everything the .conf will set. */
  unsetenv("RABBITMQ_HOST");
  unsetenv("RABBITMQ_PORT");
  unsetenv("RABBITMQ_USER");
  setenv("RABBITMQ_PASS", "file_pass_16chars", 1); /* pass must come from env/env-file */
  setenv("LIQUIDSOAP_HOST", "127.0.0.1", 1);
  setenv("LIQUIDSOAP_PORT", "1234", 1);
  setenv("LOG_FILE", "/tmp/smoothoperator.log", 1);

  char *conf = write_tmp_conf(
      "RABBITMQ_HOST=fromfile.example.com\n"
      "RABBITMQ_PORT=5671\n"
      "RABBITMQ_USER=fromfile_user\n");
  CU_ASSERT_PTR_NOT_NULL_FATAL(conf);

  config_t *cfg = config_load(conf);
  CU_ASSERT_PTR_NOT_NULL_FATAL(cfg);
  CU_ASSERT_STRING_EQUAL(cfg->rabbitmq_host, "fromfile.example.com");
  CU_ASSERT_EQUAL(cfg->rabbitmq_port, 5671);
  CU_ASSERT_STRING_EQUAL(cfg->rabbitmq_user, "fromfile_user");
  config_free(cfg);
  unlink(conf);
  free(conf);
}

/* Test: env wins over .conf file */
void test_env_wins_over_conf(void) {
  setenv("RABBITMQ_HOST", "from_env", 1);
  setenv("RABBITMQ_PORT", "5672", 1);
  setenv("RABBITMQ_USER", "env_user", 1);
  setenv("RABBITMQ_PASS", "env_pass_16chars!", 1);
  setenv("LIQUIDSOAP_HOST", "127.0.0.1", 1);
  setenv("LIQUIDSOAP_PORT", "1234", 1);
  setenv("LOG_FILE", "/tmp/smoothoperator.log", 1);

  char *conf = write_tmp_conf("RABBITMQ_HOST=from_file\n");
  CU_ASSERT_PTR_NOT_NULL_FATAL(conf);

  config_t *cfg = config_load(conf);
  CU_ASSERT_PTR_NOT_NULL_FATAL(cfg);
  CU_ASSERT_STRING_EQUAL(cfg->rabbitmq_host, "from_env");
  config_free(cfg);
  unlink(conf);
  free(conf);
}

/* Test: RABBITMQ_PASS in .conf is filtered out (secrets not allowed in main conf) */
void test_conf_filters_secrets(void) {
  unsetenv("RABBITMQ_PASS");
  setup_env_vars();
  unsetenv("RABBITMQ_PASS"); /* setup_env sets it; undo */

  char *conf = write_tmp_conf(
      "RABBITMQ_PASS=leaked_from_conf_file_xx\n"
      "RABBITMQ_HOST=conf_host\n");
  CU_ASSERT_PTR_NOT_NULL_FATAL(conf);

  config_t *cfg = config_load(conf);
  CU_ASSERT_PTR_NOT_NULL_FATAL(cfg);
  /* Password must NOT have been loaded from the .conf */
  CU_ASSERT_PTR_NULL(cfg->rabbitmq_pass);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
  unlink(conf);
  free(conf);
}

/* Test: telnet with socket path set (invalid) */
void test_protocol_telnet_with_socket_path(void) {
  setup_env_vars();
  setenv("LIQUIDSOAP_PROTOCOL", "telnet", 1);
  setenv("LIQUIDSOAP_SOCKET_PATH", "/tmp/ls.sock", 1);
  config_t *cfg = config_load(NULL);
  CU_ASSERT_PTR_NOT_NULL(cfg);
  CU_ASSERT_FALSE(config_is_valid(cfg));
  config_free(cfg);
}

/* Register test suite */
CU_pSuite suite_config(void) {
  CU_pSuite pSuite = CU_add_suite("config", 0, 0);
  if (pSuite == NULL)
    return NULL;

  CU_add_test(pSuite, "load with defaults", test_load_with_defaults);
  CU_add_test(pSuite, "is valid", test_is_valid);
  CU_add_test(pSuite, "log level DEBUG", test_log_level_debug);
  CU_add_test(pSuite, "log level INFO", test_log_level_info);
  CU_add_test(pSuite, "log level WARN", test_log_level_warn);
  CU_add_test(pSuite, "log level ERROR", test_log_level_error);
  CU_add_test(pSuite, "log level FATAL", test_log_level_fatal);
  CU_add_test(pSuite, "is valid (NULL)", test_is_valid_null);
  CU_add_test(pSuite, "protocol telnet default", test_protocol_telnet_default);
  CU_add_test(pSuite, "protocol telnet explicit", test_protocol_telnet_explicit);
  CU_add_test(pSuite, "protocol socket", test_protocol_socket);
  CU_add_test(pSuite, "protocol socket no path", test_protocol_socket_no_path);
  CU_add_test(pSuite, "protocol socket relative path",
              test_protocol_socket_relative_path);
  CU_add_test(pSuite, "protocol socket path too long",
              test_protocol_socket_path_too_long);
  CU_add_test(pSuite, "protocol telnet with socket path",
              test_protocol_telnet_with_socket_path);
  CU_add_test(pSuite, "pass too short", test_pass_too_short);
  CU_add_test(pSuite, "pass min length (16)", test_pass_min_length);
  CU_add_test(pSuite, "invalid port (non-numeric)",
              test_invalid_port_non_numeric);
  CU_add_test(pSuite, "invalid port (overflow)", test_invalid_port_overflow);
  CU_add_test(pSuite, "invalid port (negative)", test_invalid_port_negative);
  CU_add_test(pSuite, "invalid bool TLS", test_invalid_bool_tls);
  CU_add_test(pSuite, "conf file loaded", test_conf_file_loaded);
  CU_add_test(pSuite, "env wins over conf", test_env_wins_over_conf);
  CU_add_test(pSuite, "conf filters secrets", test_conf_filters_secrets);

  return pSuite;
}
