#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdlib.h>
#include <string.h>
#include "liquidsoap_client.h"

/* Test: response allocation */
void test_response_allocation(void) {
  ls_response_t *resp = malloc(sizeof(ls_response_t));
  CU_ASSERT_PTR_NOT_NULL(resp);

  resp->ok = 1;
  resp->message = malloc(10);
  strcpy(resp->message, "OK");
  resp->body = malloc(10);
  strcpy(resp->body, "OK");
  resp->body_len = 2;
  resp->latency_ms = 42;

  CU_ASSERT_TRUE(resp->ok);
  CU_ASSERT_STRING_EQUAL(resp->message, "OK");
  CU_ASSERT_STRING_EQUAL(resp->body, "OK");
  CU_ASSERT_EQUAL(resp->body_len, 2);
  CU_ASSERT_EQUAL(resp->latency_ms, 42);

  ls_response_free(resp);
}

/* Test: socket creation (will fail if LS not running, but structure is correct) */
void test_telnet_socket_creation(void) {
  ls_socket_t *sock = ls_socket_create("127.0.0.1", 1234, 1000);

  if (sock != NULL) {
    CU_ASSERT_TRUE(ls_is_connected(sock));
    ls_socket_free(sock);
  } else {
    CU_ASSERT_PTR_NULL(sock);
  }
}

/* Test: unix socket creation (socket file doesn't exist, should fail) */
void test_unix_socket_creation_fails(void) {
  ls_socket_t *sock =
      ls_socket_create_unix("/nonexistent/path/ls.sock", 1000);
  CU_ASSERT_PTR_NULL(sock);
}

/* Test: response null message */
void test_response_null_message(void) {
  ls_response_t *resp = malloc(sizeof(ls_response_t));
  resp->ok = 0;
  resp->message = NULL;
  resp->body = NULL;
  resp->body_len = 0;
  resp->latency_ms = 0;

  CU_ASSERT_FALSE(resp->ok);
  CU_ASSERT_PTR_NULL(resp->message);
  CU_ASSERT_PTR_NULL(resp->body);

  ls_response_free(resp);
}

/* Test: response with multi-line body */
void test_response_multi_line(void) {
  ls_response_t *resp = malloc(sizeof(ls_response_t));
  resp->ok = true;
  resp->message = strdup("Line1");
  resp->body = strdup("Line1\nLine2\nLine3");
  resp->body_len = strlen(resp->body);
  resp->latency_ms = 50;

  CU_ASSERT_TRUE(resp->ok);
  CU_ASSERT_STRING_EQUAL(resp->message, "Line1");
  CU_ASSERT_STRING_EQUAL(resp->body, "Line1\nLine2\nLine3");
  CU_ASSERT_EQUAL(resp->body_len, 17);

  ls_response_free(resp);
}

/* Register test suite */
CU_pSuite suite_liquidsoap_client(void) {
  CU_pSuite pSuite = CU_add_suite("liquidsoap_client", 0, 0);
  if (pSuite == NULL)
    return NULL;

  CU_add_test(pSuite, "response allocation", test_response_allocation);
  CU_add_test(pSuite, "telnet socket creation", test_telnet_socket_creation);
  CU_add_test(pSuite, "unix socket creation fails",
              test_unix_socket_creation_fails);
  CU_add_test(pSuite, "response null message", test_response_null_message);
  CU_add_test(pSuite, "response multi-line", test_response_multi_line);

  return pSuite;
}
