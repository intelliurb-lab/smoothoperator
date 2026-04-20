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

  return pSuite;
}
