#include "ls_controller.h"
#include "liquidsoap_client.h"
#include "smoothoperator.h"
#include <stdlib.h>
#include <string.h>

struct controller {
  const config_t *cfg;
  ls_socket_t *ls_sock;
  bool healthy;
};

controller_t *controller_create(const config_t *cfg) {
  if (cfg == NULL)
    return NULL;

  controller_t *ctrl = calloc(1, sizeof(controller_t));
  if (ctrl == NULL)
    return NULL;

  ctrl->cfg = cfg;
  ctrl->healthy = true;

  /* Connect to Liquidsoap */
  ctrl->ls_sock = ls_socket_create(cfg->liquidsoap_host,
                                    cfg->liquidsoap_port,
                                    cfg->liquidsoap_timeout_ms);

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

  const char *command = NULL;

  if (strcmp(event, "control.skip") == 0) {
    command = "next";
  } else if (strcmp(event, "control.shutdown") == 0) {
    command = "shutdown";
  } else if (strcmp(event, "announcement.push") == 0) {
    json_t *payload = message_get_payload(msg);
    if (!json_is_object(payload)) {
      return RESULT_INVALID;
    }

    json_t *filepath_obj = json_object_get(payload, "filepath");
    if (!json_is_string(filepath_obj)) {
      return RESULT_INVALID;
    }

    const char *filepath = json_string_value(filepath_obj);
    static char cmd_buf[512];
    int n = snprintf(cmd_buf, sizeof(cmd_buf), "announcements.push %s",
                     filepath);
    if (n < 0 || (size_t)n >= sizeof(cmd_buf)) {
      return RESULT_INVALID;
    }
    command = cmd_buf;
  } else {
    return RESULT_INVALID;
  }

  if (command == NULL)
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
