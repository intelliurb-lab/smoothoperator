#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

static void setup_env_vars(void) {
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

  return pSuite;
}
