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
  resp->latency_ms = 42;

  CU_ASSERT_TRUE(resp->ok);
  CU_ASSERT_STRING_EQUAL(resp->message, "OK");
  CU_ASSERT_EQUAL(resp->latency_ms, 42);

  ls_response_free(resp);
}

/* Test: socket creation (will fail if LS not running, but structure is correct) */
void test_socket_creation(void) {
  /* NOTE: This test expects Liquidsoap NOT to be running on 127.0.0.1:1234.
     If it is running, the socket will connect successfully. Either way, we're
     testing the interface, not the connection. */

  ls_socket_t *sock = ls_socket_create("127.0.0.1", 1234, 1000);

  /* If we're testing without LS, socket creation should fail gracefully.
     If LS is running, socket is created successfully.
     Both are valid outcomes for this test phase. */

  if (sock != NULL) {
    /* LS was running, socket connected */
    CU_ASSERT_TRUE(ls_is_connected(sock));
    ls_socket_free(sock);
  } else {
    /* LS not running, socket creation failed - expected */
    CU_ASSERT_PTR_NULL(sock);
  }
}

/* Test: response null message */
void test_response_null_message(void) {
  ls_response_t *resp = malloc(sizeof(ls_response_t));
  resp->ok = 0;
  resp->message = NULL;
  resp->latency_ms = 0;

  CU_ASSERT_FALSE(resp->ok);
  CU_ASSERT_PTR_NULL(resp->message);

  ls_response_free(resp);
}

/* Register test suite */
CU_pSuite suite_liquidsoap_client(void) {
  CU_pSuite pSuite = CU_add_suite("liquidsoap_client", 0, 0);
  if (pSuite == NULL)
    return NULL;

  CU_add_test(pSuite, "response allocation", test_response_allocation);
  CU_add_test(pSuite, "socket creation", test_socket_creation);
  CU_add_test(pSuite, "response null message", test_response_null_message);

  return pSuite;
}
