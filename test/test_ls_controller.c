#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "ls_controller.h"
#include "message.h"

static void test_event_control_skip(void) {
  json_t *payload = json_object();
  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-skip"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("control.skip"));
  json_object_set_new(msg_obj, "payload", payload);

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "control.skip");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

static void test_event_source_skip(void) {
  json_t *payload = json_object();
  json_object_set_new(payload, "source", json_string("main_stream"));

  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-src-skip"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("source.skip"));
  json_object_set_new(msg_obj, "payload", payload);

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "source.skip");

  json_t *parsed_payload = message_get_payload(msg);
  json_t *source_obj = json_object_get(parsed_payload, "source");
  CU_ASSERT_STRING_EQUAL(json_string_value(source_obj), "main_stream");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

static void test_event_announcement_push(void) {
  json_t *payload = json_object();
  json_object_set_new(payload, "filepath",
                      json_string("/opt/audio/announcement.mp3"));

  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-announce"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("announcement.push"));
  json_object_set_new(msg_obj, "payload", payload);

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "announcement.push");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

static void test_event_var_set(void) {
  json_t *payload = json_object();
  json_object_set_new(payload, "name", json_string("my_var"));
  json_object_set_new(payload, "value", json_string("new_value"));

  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-var-set"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("var.set"));
  json_object_set_new(msg_obj, "payload", payload);

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "var.set");

  json_t *parsed_payload = message_get_payload(msg);
  json_t *name_obj = json_object_get(parsed_payload, "name");
  json_t *value_obj = json_object_get(parsed_payload, "value");
  CU_ASSERT_STRING_EQUAL(json_string_value(name_obj), "my_var");
  CU_ASSERT_STRING_EQUAL(json_string_value(value_obj), "new_value");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

static void test_event_request_push(void) {
  json_t *payload = json_object();
  json_object_set_new(payload, "queue", json_string("announcements"));
  json_object_set_new(payload, "uri",
                      json_string("file:///opt/audio/announce.mp3"));

  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-req-push"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("request.push"));
  json_object_set_new(msg_obj, "payload", payload);

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "request.push");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

static void test_event_request_metadata(void) {
  json_t *payload = json_object();
  json_object_set_new(payload, "rid", json_integer(42));

  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-req-meta"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("request.metadata"));
  json_object_set_new(msg_obj, "payload", payload);

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "request.metadata");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

static void test_event_output_start(void) {
  json_t *payload = json_object();
  json_object_set_new(payload, "output", json_string("icecast"));

  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-out-start"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("output.start"));
  json_object_set_new(msg_obj, "payload", payload);

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "output.start");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

static void test_event_server_uptime(void) {
  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-uptime"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("server.uptime"));
  json_object_set_new(msg_obj, "payload", json_object());

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "server.uptime");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

static void test_event_unknown(void) {
  json_t *msg_obj = json_object();
  json_object_set_new(msg_obj, "version", json_integer(1));
  json_object_set_new(msg_obj, "id", json_string("test-unknown"));
  json_object_set_new(msg_obj, "timestamp", json_string("2026-04-20T00:00:00Z"));
  json_object_set_new(msg_obj, "event", json_string("unknown.event"));
  json_object_set_new(msg_obj, "payload", json_object());

  char *json_str = json_dumps(msg_obj, JSON_COMPACT);
  message_t *msg = message_parse(json_str);
  CU_ASSERT_PTR_NOT_NULL(msg);
  CU_ASSERT_STRING_EQUAL(message_get_event(msg), "unknown.event");

  free(json_str);
  json_decref(msg_obj);
  message_free(msg);
}

CU_pSuite suite_ls_controller(void) {
  CU_pSuite pSuite = CU_add_suite("ls_controller", 0, 0);
  if (pSuite == NULL)
    return NULL;

  CU_add_test(pSuite, "event control.skip", test_event_control_skip);
  CU_add_test(pSuite, "event source.skip", test_event_source_skip);
  CU_add_test(pSuite, "event announcement.push", test_event_announcement_push);
  CU_add_test(pSuite, "event var.set", test_event_var_set);
  CU_add_test(pSuite, "event request.push", test_event_request_push);
  CU_add_test(pSuite, "event request.metadata", test_event_request_metadata);
  CU_add_test(pSuite, "event output.start", test_event_output_start);
  CU_add_test(pSuite, "event server.uptime", test_event_server_uptime);
  CU_add_test(pSuite, "event unknown", test_event_unknown);

  return pSuite;
}
