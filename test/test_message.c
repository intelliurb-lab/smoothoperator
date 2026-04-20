#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdlib.h>
#include <string.h>
#include "message.h"

/* Test: parse valid message */
void test_parse_valid(void) {
  const char *json =
      "{"
      "\"version\": 1,"
      "\"id\": \"msg123\","
      "\"timestamp\": \"2026-04-20T10:30:00Z\","
      "\"event\": \"control.skip\","
      "\"payload\": {\"reason\": \"manual\"}"
      "}";

  message_t *msg = message_parse(json);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_EQUAL(message_get_version(msg), 1);
  CU_ASSERT_STRING_EQUAL(message_get_id(msg), "msg123");
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "control.skip");

  message_free(msg);
}

/* Test: parse invalid JSON */
void test_parse_invalid_json(void) {
  const char *json = "{invalid json";
  message_t *msg = message_parse(json);
  CU_ASSERT_PTR_NULL(msg);
}

/* Test: validate missing version */
void test_validate_missing_version(void) {
  const char *json =
      "{"
      "\"id\": \"msg123\","
      "\"timestamp\": \"2026-04-20T10:30:00Z\","
      "\"event\": \"control.skip\""
      "}";

  message_t *msg = message_parse(json);
  CU_ASSERT_PTR_NULL(msg);
}

/* Test: validate wrong version */
void test_validate_wrong_version(void) {
  const char *json =
      "{"
      "\"version\": 2,"
      "\"id\": \"msg123\","
      "\"timestamp\": \"2026-04-20T10:30:00Z\","
      "\"event\": \"control.skip\""
      "}";

  message_t *msg = message_parse(json);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_FALSE(message_is_valid(msg));

  message_free(msg);
}

/* Test: parse with optional source */
void test_parse_with_optional_fields(void) {
  const char *json =
      "{"
      "\"version\": 1,"
      "\"id\": \"msg123\","
      "\"timestamp\": \"2026-04-20T10:30:00Z\","
      "\"source\": \"radio_cli\","
      "\"event\": \"control.skip\","
      "\"payload\": {}"
      "}";

  message_t *msg = message_parse(json);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_TRUE(message_is_valid(msg));
  CU_ASSERT_STRING_EQUAL(message_get_source(msg), "radio_cli");

  message_free(msg);
}

/* Test: null pointer handling */
void test_parse_null(void) {
  message_t *msg = message_parse(NULL);
  CU_ASSERT_PTR_NULL(msg);
}

/* Test: getters with null message */
void test_getters_null_message(void) {
  CU_ASSERT_EQUAL(message_get_version(NULL), 0);
  CU_ASSERT_PTR_NULL(message_get_id(NULL));
  CU_ASSERT_PTR_NULL(message_get_event(NULL));
}

/* Register test suite */
CU_pSuite suite_message(void) {
  CU_pSuite pSuite = CU_add_suite("message", 0, 0);
  if (pSuite == NULL)
    return NULL;

  CU_add_test(pSuite, "parse valid message", test_parse_valid);
  CU_add_test(pSuite, "parse invalid JSON", test_parse_invalid_json);
  CU_add_test(pSuite, "validate missing version", test_validate_missing_version);
  CU_add_test(pSuite, "validate wrong version", test_validate_wrong_version);
  CU_add_test(pSuite, "parse with optional fields",
              test_parse_with_optional_fields);
  CU_add_test(pSuite, "parse NULL", test_parse_null);
  CU_add_test(pSuite, "getters with NULL message", test_getters_null_message);

  return pSuite;
}
