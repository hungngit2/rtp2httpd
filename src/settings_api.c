#include "settings_api.h"
#include "configuration.h"
#include "http.h"
#include "utils.h"
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef enum { FT_STRING, FT_INT, FT_BOOL, FT_IFNAME } field_type_t;

typedef struct {
  const char *config_key; /* key as it appears in rtp2httpd.conf and in the JSON/form payload */
  field_type_t type;
  size_t offset; /* offsetof(config_t, field) */
} setting_field_t;

/* Table-driven list of every global setting exposed by /setting. Adding a
 * field here is the only change needed to expose it via GET/POST; both
 * handlers below are fully generic over this table. */
const setting_field_t SETTING_FIELDS[] = {
    {"verbosity", FT_INT, offsetof(config_t, verbosity)},
    {"maxclients", FT_INT, offsetof(config_t, maxclients)},
    {"hostname", FT_STRING, offsetof(config_t, hostname)},
    {"xff", FT_BOOL, offsetof(config_t, xff)},
    {"r2h-token", FT_STRING, offsetof(config_t, r2h_token)},
    {"workers", FT_INT, offsetof(config_t, workers)},
    {"buffer-pool-max-size", FT_INT, offsetof(config_t, buffer_pool_max_size)},
    {"udp-rcvbuf-size", FT_INT, offsetof(config_t, udp_rcvbuf_size)},
    {"upstream-interface", FT_IFNAME, offsetof(config_t, upstream_interface)},
    {"upstream-interface-fcc", FT_IFNAME, offsetof(config_t, upstream_interface_fcc)},
    {"upstream-interface-rtsp", FT_IFNAME, offsetof(config_t, upstream_interface_rtsp)},
    {"upstream-interface-multicast", FT_IFNAME, offsetof(config_t, upstream_interface_multicast)},
    {"upstream-interface-http", FT_IFNAME, offsetof(config_t, upstream_interface_http)},
    {"mcast-rejoin-interval", FT_INT, offsetof(config_t, mcast_rejoin_interval)},
    {"ffmpeg-path", FT_STRING, offsetof(config_t, ffmpeg_path)},
    {"ffmpeg-args", FT_STRING, offsetof(config_t, ffmpeg_args)},
    {"video-snapshot", FT_BOOL, offsetof(config_t, video_snapshot)},
    {"status-page-path", FT_STRING, offsetof(config_t, status_page_path)},
    {"player-page-path", FT_STRING, offsetof(config_t, player_page_path)},
    {"setting-page-path", FT_STRING, offsetof(config_t, setting_page_path)},
    {"app-path-prefix", FT_STRING, offsetof(config_t, app_path_prefix)},
    {"use-relative-path-in-m3u", FT_BOOL, offsetof(config_t, use_relative_path_in_m3u)},
    {"external-m3u", FT_STRING, offsetof(config_t, external_m3u_url)},
    {"external-m3u-update-interval", FT_INT, offsetof(config_t, external_m3u_update_interval)},
    {"zerocopy-on-send", FT_BOOL, offsetof(config_t, zerocopy_on_send)},
    {"rtsp-stun-server", FT_STRING, offsetof(config_t, rtsp_stun_server)},
    {"http-proxy-user-agent", FT_STRING, offsetof(config_t, http_proxy_user_agent)},
    {"rtsp-user-agent", FT_STRING, offsetof(config_t, rtsp_user_agent)},
    {"cors-allow-origin", FT_STRING, offsetof(config_t, cors_allow_origin)},
    {"access-log", FT_STRING, offsetof(config_t, access_log)},
    {"log-format", FT_STRING, offsetof(config_t, log_format)},
};
const size_t SETTING_FIELDS_COUNT = sizeof(SETTING_FIELDS) / sizeof(SETTING_FIELDS[0]);

/* Appends a JSON-quoted, escaped copy of src to dst (which must already have
 * room). Returns the number of bytes written (not including the terminator). */
static size_t json_append_escaped_string(char *dst, size_t dst_size, const char *src) {
  size_t off = 0;

  if (dst_size < 3) {
    if (dst_size > 0)
      dst[0] = '\0';
    return 0;
  }

  dst[off++] = '"';
  for (; *src && off + 2 < dst_size; src++) {
    unsigned char ch = (unsigned char)*src;
    if (ch == '"' || ch == '\\') {
      if (off + 3 >= dst_size)
        break;
      dst[off++] = '\\';
      dst[off++] = (char)ch;
    } else if (ch == '\n') {
      if (off + 3 >= dst_size)
        break;
      dst[off++] = '\\';
      dst[off++] = 'n';
    } else if (ch < 0x20) {
      continue; /* drop other control characters */
    } else {
      dst[off++] = (char)ch;
    }
  }
  dst[off++] = '"';
  dst[off] = '\0';
  return off;
}

/* Renders a single bind_addresses entry as the canonical string form used by
 * both the GET response and the "listen" form field (bare port, host:port,
 * [ipv6]:port, or an absolute Unix socket path). */
static void format_bind_address(bindaddr_t *ba, char *out, size_t out_size) {
  if (ba->type == BIND_ADDR_UNIX) {
    snprintf(out, out_size, "%s", ba->path);
  } else if (ba->node == NULL || strcmp(ba->node, "*") == 0) {
    snprintf(out, out_size, "%s", ba->service);
  } else if (strchr(ba->node, ':')) {
    snprintf(out, out_size, "[%s]:%s", ba->node, ba->service);
  } else {
    snprintf(out, out_size, "%s:%s", ba->node, ba->service);
  }
}

void handle_get_config(connection_t *c) {
  static char buf[16384];
  size_t off = 0;

  off += snprintf(buf + off, sizeof(buf) - off, "{");

  for (size_t i = 0; i < SETTING_FIELDS_COUNT; i++) {
    const setting_field_t *f = &SETTING_FIELDS[i];
    const char *base = (const char *)&config;

    if (i > 0)
      buf[off++] = ',';

    off += snprintf(buf + off, sizeof(buf) - off, "\"%s\":", f->config_key);

    switch (f->type) {
    case FT_INT: {
      int v = *(const int *)(base + f->offset);
      off += snprintf(buf + off, sizeof(buf) - off, "%d", v);
      break;
    }
    case FT_BOOL: {
      int v = *(const int *)(base + f->offset);
      off += snprintf(buf + off, sizeof(buf) - off, "%s", v ? "true" : "false");
      break;
    }
    case FT_STRING: {
      const char *v = *(const char *const *)(base + f->offset);
      off += json_append_escaped_string(buf + off, sizeof(buf) - off, v ? v : "");
      break;
    }
    case FT_IFNAME: {
      const char *v = base + f->offset;
      off += json_append_escaped_string(buf + off, sizeof(buf) - off, v);
      break;
    }
    }
  }

  /* fcc-listen-port-range: combine the two int fields into "start-end" */
  {
    char range[32] = "";
    if (config.fcc_listen_port_min > 0 && config.fcc_listen_port_max > 0) {
      snprintf(range, sizeof(range), "%d-%d", config.fcc_listen_port_min, config.fcc_listen_port_max);
    }
    off += snprintf(buf + off, sizeof(buf) - off, ",\"fcc-listen-port-range\":");
    off += json_append_escaped_string(buf + off, sizeof(buf) - off, range);
  }

  /* listen: bind_addresses linked list rendered as canonical strings */
  off += snprintf(buf + off, sizeof(buf) - off, ",\"listen\":[");
  {
    bindaddr_t *ba = bind_addresses;
    int first = 1;
    char line[256];

    while (ba) {
      if (!first)
        buf[off++] = ',';
      first = 0;
      format_bind_address(ba, line, sizeof(line));
      off += json_append_escaped_string(buf + off, sizeof(buf) - off, line);
      ba = ba->next;
    }
  }
  off += snprintf(buf + off, sizeof(buf) - off, "]}");

  send_http_headers(c, STATUS_200, "application/json", NULL);
  connection_queue_output_and_flush(c, (const uint8_t *)buf, off);
}

/* Rejects values containing raw newlines, which would corrupt the line-based
 * INI format or let a form field inject extra config lines. */
static int value_has_newline(const char *value) {
  return value && strpbrk(value, "\r\n") != NULL;
}

static void send_json_error(connection_t *c, int status, const char *message) {
  char response[256];
  send_http_headers(c, status, "application/json", NULL);
  snprintf(response, sizeof(response), "{\"success\":false,\"error\":\"%s\"}", message);
  connection_queue_output_and_flush(c, (const uint8_t *)response, strlen(response));
}

void handle_save_config(connection_t *c) {
  if (strcasecmp(c->http_req.method, "POST") != 0) {
    send_json_error(c, STATUS_400, "Method not allowed. Use POST");
    return;
  }
  if (c->http_req.body_len == 0) {
    send_json_error(c, STATUS_400, "Missing request body");
    return;
  }

  const char *cfg_path = get_config_file_path();
  if (!cfg_path) {
    send_json_error(c, STATUS_400, "rtp2httpd was not started with a config file, cannot save settings");
    return;
  }

  /* +1 slot for fcc-listen-port-range, which isn't in SETTING_FIELDS because
   * it maps to two separate config_t ints (fcc_listen_port_min/max) rather
   * than one. */
  setting_kv_t kvs[SETTING_FIELDS_COUNT + 1];
  char value_bufs[SETTING_FIELDS_COUNT + 1][512];
  size_t n_kvs = 0;

  for (size_t i = 0; i < SETTING_FIELDS_COUNT; i++) {
    const setting_field_t *f = &SETTING_FIELDS[i];
    if (http_parse_query_param(c->http_req.body, f->config_key, value_bufs[n_kvs], sizeof(value_bufs[n_kvs])) == 0) {
      if (value_has_newline(value_bufs[n_kvs])) {
        send_json_error(c, STATUS_400, "Field value must not contain newlines");
        return;
      }
      kvs[n_kvs].key = f->config_key;
      kvs[n_kvs].value = value_bufs[n_kvs];
      n_kvs++;
    }
  }
  if (http_parse_query_param(c->http_req.body, "fcc-listen-port-range", value_bufs[n_kvs],
                              sizeof(value_bufs[n_kvs])) == 0) {
    if (value_has_newline(value_bufs[n_kvs])) {
      send_json_error(c, STATUS_400, "Field value must not contain newlines");
      return;
    }
    kvs[n_kvs].key = "fcc-listen-port-range";
    kvs[n_kvs].value = value_bufs[n_kvs];
    n_kvs++;
  }

  /* "listen" is submitted as one address per line (a textarea, not a
   * repeated form field, since this codebase's query-param parser only
   * returns the first match for a given key). */
  char listen_raw[4096];
  const char *listen_lines[64];
  size_t n_listen = 0;

  if (http_parse_query_param(c->http_req.body, "listen", listen_raw, sizeof(listen_raw)) == 0) {
    char *saveptr = NULL;
    char *line = strtok_r(listen_raw, "\n", &saveptr);
    while (line && n_listen < 64) {
      while (*line == ' ' || *line == '\t' || *line == '\r')
        line++;
      char *end = line + strlen(line);
      while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = '\0';
      if (line[0] != '\0') {
        if (value_has_newline(line)) {
          send_json_error(c, STATUS_400, "Field value must not contain newlines");
          return;
        }
        listen_lines[n_listen++] = line;
      }
      line = strtok_r(NULL, "\n", &saveptr);
    }
  }

  if (config_apply_global_settings(cfg_path, kvs, n_kvs, listen_lines, n_listen) != 0) {
    send_json_error(c, STATUS_500, "Failed to write config file");
    return;
  }

  pid_t supervisor_pid = getppid();
  if (kill(supervisor_pid, SIGHUP) != 0) {
    send_json_error(c, STATUS_500, "Config saved, but failed to trigger reload");
    return;
  }

  send_http_headers(c, STATUS_200, "application/json", NULL);
  const char *ok = "{\"success\":true,\"message\":\"Settings saved and reload triggered\"}";
  connection_queue_output_and_flush(c, (const uint8_t *)ok, strlen(ok));
}
