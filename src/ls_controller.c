#include "ls_controller.h"
#include "liquidsoap_client.h"
#include "smoothoperator.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#define LS_CMD_MAX 2048

struct controller {
  const config_t *cfg;
  ls_socket_t *ls_sock;
  bool healthy;
};

static bool is_valid_identifier(const char *s) {
  if (s == NULL || *s == '\0')
    return false;
  if (strlen(s) > 64)
    return false;
  for (const char *p = s; *p; p++) {
    if (!isalnum((unsigned char)*p) && *p != '_')
      return false;
  }
  return true;
}

static bool payload_get_string(json_t *payload, const char *key,
                                const char **out) {
  if (!json_is_object(payload))
    return false;
  json_t *obj = json_object_get(payload, key);
  if (!json_is_string(obj))
    return false;
  const char *val = json_string_value(obj);
  if (val == NULL || strlen(val) == 0 || strlen(val) > 1024)
    return false;
  *out = val;
  return true;
}

static bool is_safe_ls_arg(const char *arg) {
  if (arg == NULL || *arg == '\0' || strlen(arg) > LS_CMD_MAX)
    return false;
  for (const char *p = arg; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n' || c == '\r' || c == '\0')
      return false;
    if (!isprint(c) && c != '\t')
      return false;
  }
  return true;
}

static result_t send_command(controller_t *ctrl, const char *command) {
  if (command == NULL)
    return RESULT_INVALID;
  if (!is_safe_ls_arg(command))
    return RESULT_INVALID;

  ls_response_t *resp = ls_send_command(ctrl->ls_sock, command);
  if (resp == NULL) {
    ctrl->healthy = false;
    return RESULT_RETRY;
  }

  result_t result = resp->ok ? RESULT_OK : RESULT_ERROR;
  ls_response_free(resp);
  return result;
}

static result_t send_command_fmt(controller_t *ctrl, const char *fmt, ...) {
  char buf[LS_CMD_MAX];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= sizeof(buf)) {
    log_msg(LOG_ERROR, "controller", "command too long or format error",
            NULL, NULL);
    return RESULT_INVALID;
  }
  return send_command(ctrl, buf);
}

static result_t handle_control_skip(controller_t *ctrl, json_t *payload) {
  (void)payload;
  return send_command(ctrl, "next");
}

static result_t handle_control_shutdown(controller_t *ctrl, json_t *payload) {
  (void)payload;
  return send_command(ctrl, "shutdown");
}

static result_t handle_announcement_push(controller_t *ctrl, json_t *payload) {
  const char *filepath = NULL;
  if (!payload_get_string(payload, "filepath", &filepath))
    return RESULT_INVALID;
  if (!is_safe_ls_arg(filepath))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "announcements.push %s", filepath);
}

static result_t handle_source_skip(controller_t *ctrl, json_t *payload) {
  const char *source = NULL;
  if (!payload_get_string(payload, "source", &source))
    return RESULT_INVALID;
  if (!is_valid_identifier(source))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "%s.skip", source);
}

static result_t handle_source_metadata(controller_t *ctrl, json_t *payload) {
  const char *source = NULL;
  if (!payload_get_string(payload, "source", &source))
    return RESULT_INVALID;
  if (!is_valid_identifier(source))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "%s.metadata", source);
}

static result_t handle_source_remaining(controller_t *ctrl, json_t *payload) {
  const char *source = NULL;
  if (!payload_get_string(payload, "source", &source))
    return RESULT_INVALID;
  if (!is_valid_identifier(source))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "%s.remaining", source);
}

static result_t handle_request_push(controller_t *ctrl, json_t *payload) {
  const char *queue = NULL;
  const char *uri = NULL;
  if (!payload_get_string(payload, "queue", &queue))
    return RESULT_INVALID;
  if (!payload_get_string(payload, "uri", &uri))
    return RESULT_INVALID;
  if (!is_valid_identifier(queue))
    return RESULT_INVALID;
  if (!is_safe_ls_arg(uri))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "%s.push %s", queue, uri);
}

static result_t handle_request_queue_list(controller_t *ctrl, json_t *payload) {
  const char *queue = NULL;
  if (!payload_get_string(payload, "queue", &queue))
    return RESULT_INVALID;
  if (!is_valid_identifier(queue))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "%s.queue", queue);
}

static result_t handle_request_on_air(controller_t *ctrl, json_t *payload) {
  (void)payload;
  return send_command(ctrl, "request.on_air");
}

static result_t handle_request_alive(controller_t *ctrl, json_t *payload) {
  (void)payload;
  return send_command(ctrl, "request.alive");
}

static result_t handle_request_metadata(controller_t *ctrl, json_t *payload) {
  if (!json_is_object(payload))
    return RESULT_INVALID;
  json_t *rid_obj = json_object_get(payload, "rid");
  if (!json_is_integer(rid_obj))
    return RESULT_INVALID;
  json_int_t rid = json_integer_value(rid_obj);
  if (rid < 0)
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "request.metadata %lld", (long long)rid);
}

static result_t handle_request_trace(controller_t *ctrl, json_t *payload) {
  if (!json_is_object(payload))
    return RESULT_INVALID;
  json_t *rid_obj = json_object_get(payload, "rid");
  if (!json_is_integer(rid_obj))
    return RESULT_INVALID;
  json_int_t rid = json_integer_value(rid_obj);
  if (rid < 0)
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "request.trace %lld", (long long)rid);
}

static result_t handle_var_list(controller_t *ctrl, json_t *payload) {
  (void)payload;
  return send_command(ctrl, "var.list");
}

static result_t handle_var_get(controller_t *ctrl, json_t *payload) {
  const char *name = NULL;
  if (!payload_get_string(payload, "name", &name))
    return RESULT_INVALID;
  if (!is_valid_identifier(name))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "var.get %s", name);
}

static result_t handle_var_set(controller_t *ctrl, json_t *payload) {
  const char *name = NULL;
  const char *value = NULL;
  if (!payload_get_string(payload, "name", &name))
    return RESULT_INVALID;
  if (!payload_get_string(payload, "value", &value))
    return RESULT_INVALID;
  if (!is_valid_identifier(name))
    return RESULT_INVALID;
  if (!is_safe_ls_arg(value))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "var.set %s = %s", name, value);
}

static result_t handle_output_start(controller_t *ctrl, json_t *payload) {
  const char *output = NULL;
  if (!payload_get_string(payload, "output", &output))
    return RESULT_INVALID;
  if (!is_valid_identifier(output))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "%s.start", output);
}

static result_t handle_output_stop(controller_t *ctrl, json_t *payload) {
  const char *output = NULL;
  if (!payload_get_string(payload, "output", &output))
    return RESULT_INVALID;
  if (!is_valid_identifier(output))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "%s.stop", output);
}

static result_t handle_playlist_reload(controller_t *ctrl, json_t *payload) {
  const char *playlist = NULL;
  if (!payload_get_string(payload, "playlist", &playlist))
    return RESULT_INVALID;
  if (!is_valid_identifier(playlist))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "%s.reload", playlist);
}

static result_t handle_server_uptime(controller_t *ctrl, json_t *payload) {
  (void)payload;
  return send_command(ctrl, "uptime");
}

static result_t handle_server_version(controller_t *ctrl, json_t *payload) {
  (void)payload;
  return send_command(ctrl, "version");
}

static result_t handle_server_list(controller_t *ctrl, json_t *payload) {
  (void)payload;
  return send_command(ctrl, "list");
}

static result_t handle_server_help(controller_t *ctrl, json_t *payload) {
  if (payload == NULL || !json_is_object(payload)) {
    return send_command(ctrl, "help");
  }

  json_t *cmd_obj = json_object_get(payload, "command");
  if (cmd_obj == NULL || !json_is_string(cmd_obj)) {
    return send_command(ctrl, "help");
  }

  const char *cmd = json_string_value(cmd_obj);
  if (cmd == NULL || strlen(cmd) == 0 || !is_valid_identifier(cmd))
    return RESULT_INVALID;

  return send_command_fmt(ctrl, "help %s", cmd);
}

typedef result_t (*event_handler_fn)(controller_t *, json_t *payload);

typedef struct {
  const char *event;
  event_handler_fn fn;
} event_handler_t;

static const event_handler_t EVENT_HANDLERS[] = {
    {"control.skip", handle_control_skip},
    {"control.shutdown", handle_control_shutdown},
    {"announcement.push", handle_announcement_push},
    {"source.skip", handle_source_skip},
    {"source.metadata", handle_source_metadata},
    {"source.remaining", handle_source_remaining},
    {"request.push", handle_request_push},
    {"request.queue.list", handle_request_queue_list},
    {"request.on_air", handle_request_on_air},
    {"request.alive", handle_request_alive},
    {"request.metadata", handle_request_metadata},
    {"request.trace", handle_request_trace},
    {"var.list", handle_var_list},
    {"var.get", handle_var_get},
    {"var.set", handle_var_set},
    {"output.start", handle_output_start},
    {"output.stop", handle_output_stop},
    {"playlist.reload", handle_playlist_reload},
    {"server.uptime", handle_server_uptime},
    {"server.version", handle_server_version},
    {"server.list", handle_server_list},
    {"server.help", handle_server_help},
    {NULL, NULL}};

controller_t *controller_create(const config_t *cfg) {
  if (cfg == NULL)
    return NULL;

  controller_t *ctrl = calloc(1, sizeof(controller_t));
  if (ctrl == NULL)
    return NULL;

  ctrl->cfg = cfg;
  ctrl->healthy = true;

  ctrl->ls_sock = ls_socket_create_from_config(cfg);

  if (ctrl->ls_sock == NULL) {
    free(ctrl);
    return NULL;
  }

  return ctrl;
}

result_t controller_handle_event(controller_t *ctrl, const message_t *msg) {
  if (ctrl == NULL || msg == NULL)
    return RESULT_INVALID;

  const char *event = message_get_event(msg);
  if (event == NULL)
    return RESULT_INVALID;

  json_t *payload = message_get_payload(msg);

  for (int i = 0; EVENT_HANDLERS[i].event != NULL; i++) {
    if (strcmp(event, EVENT_HANDLERS[i].event) == 0) {
      return EVENT_HANDLERS[i].fn(ctrl, payload);
    }
  }

  return RESULT_INVALID;
}

bool controller_is_healthy(const controller_t *ctrl) {
  return ctrl && ctrl->healthy && ls_is_connected(ctrl->ls_sock);
}

void controller_free(controller_t *ctrl) {
  if (ctrl == NULL)
    return;

  if (ctrl->ls_sock)
    ls_socket_free(ctrl->ls_sock);

  free(ctrl);
}
