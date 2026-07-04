# Web Settings Page (`/setting`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a built-in `/setting` web page (like `/status` and `/player`) that lets an operator view and edit `rtp2httpd`'s global configuration through a structured form, on any platform (not just OpenWrt/LuCI).

**Architecture:** Backend gains a `setting_page_path`/`setting_page_route` config option (mirroring `status_page_path`/`player_page_path`), a `GET .../api/get-config` endpoint that serializes the live `config` struct to JSON via a small field-descriptor table, and a `POST .../api/save-config` endpoint that rewrites `rtp2httpd.conf` in place (preserving comments/`[services]` content) using the same table, then signals the supervisor to reload — reusing the existing `SIGHUP` reload path. Frontend gains a new Vite entry (`setting.html` → `src/pages/setting.tsx`) built from a single shared field-metadata array so the form and its validation stay in lockstep with the backend table by construction.

**Tech Stack:** C11 (backend), React + TypeScript + Tailwind (frontend, matching `status.tsx`/`player.tsx` conventions), pytest E2E tests (matching `e2e/test_pages.py` conventions).

## Global Constraints

- Pure C11 backend — no C++.
- 2-space indentation, no tabs, in C. Tabs (not spaces) in TS/JS per Biome config.
- Structs: `_s` suffix for the tag, `_t` for the typedef.
- Logging via `logger()` from `utils.h` — never `printf`/`fprintf`.
- `snprintf`/`strncpy` only — never `sprintf`/`strcpy`.
- No new dependencies without discussion — this plan introduces none.
- `src/embedded_web_data.h` is generated; never hand-edit it, and don't commit a stale rebuild unless the user asks.
- Out of scope (per the approved design spec, `docs/superpowers/specs/2026-07-04-web-settings-page-design.md`): no raw config-file textarea, no `[services]`/inline-M3U editing, no OpenWrt-only fields.

---

## Task 1: `setting_page_path` config option + route dispatch

**Files:**
- Modify: `src/configuration.h`
- Modify: `src/configuration.c`
- Modify: `src/connection.c`
- Test: `e2e/test_pages.py`

**Interfaces:**
- Produces: `config.setting_page_path` (`char *`, e.g. `"/setting"`), `config.setting_page_route` (`char *`, e.g. `"setting"`, no leading slash). Both follow the exact null/empty conventions of `status_page_path`/`status_page_route`.
- Produces: request path `<setting_page_route>` now serves the embedded `/setting.html` file (which does not exist as an asset yet — this task will 404 until Task 7 adds it; that's expected and fine to commit).

- [ ] **Step 1: Add the struct fields**

In `src/configuration.h`, right after the player page fields (after line 101, `player_page_route`), add:

```c
  /* Setting page settings */
  char *setting_page_path;  /* Absolute HTTP path for setting page (leading slash) */
  char *setting_page_route; /* Setting page path without leading slash (may be
                               empty) */
```

- [ ] **Step 2: Add the command-line-override flag**

In `src/configuration.c`, find `int cmd_player_page_path_set = 0;` (line 46) and add directly below it:

```c
int cmd_setting_page_path_set = 0;
```

- [ ] **Step 3: Free it in `config_cleanup`**

Find the block starting `if (!cmd_player_page_path_set || force_free) {` (line 122) and add a matching block right after its closing `}`:

```c
  if (!cmd_setting_page_path_set || force_free) {
    safe_free_string(&target->setting_page_path);
    safe_free_string(&target->setting_page_route);
  }
```

- [ ] **Step 4: Add the setter**

Find `set_player_page_path_value` (line 307-309) and add directly below it:

```c
static void set_setting_page_path_value(const char *value) {
  set_page_path_value(value, "setting", &config.setting_page_path, &config.setting_page_route);
}
```

- [ ] **Step 5: Parse the config-file key**

Find the `"player-page-path"` block in the `[global]` key parser (line 642-644):

```c
  if (strcasecmp("player-page-path", param) == 0) {
    if (set_if_not_cmd_override(cmd_player_page_path_set, "player-page-path"))
      set_player_page_path_value(value);
```

Add directly after its closing brace:

```c
  if (strcasecmp("setting-page-path", param) == 0) {
    if (set_if_not_cmd_override(cmd_setting_page_path_set, "setting-page-path"))
      set_setting_page_path_value(value);
    return;
  }
```

- [ ] **Step 6: Snapshot support**

Find `snapshot->player_page_path = NULL;` / `snapshot->player_page_route = NULL;` (lines 1068-1069) and add:

```c
  snapshot->setting_page_path = NULL;
  snapshot->setting_page_route = NULL;
```

Find the `SNAPSHOT_STRING(player_page_path, ...)` / `SNAPSHOT_STRING(player_page_route, ...)` pair (lines 1092-1093) and add:

```c
  SNAPSHOT_STRING(setting_page_path, cmd_setting_page_path_set);
  SNAPSHOT_STRING(setting_page_route, cmd_setting_page_path_set);
```

- [ ] **Step 7: Default value**

Find (line 1172-1173):

```c
  if (!cmd_player_page_path_set)
    set_player_page_path_value("/player");
```

Add directly after:

```c
  if (!cmd_setting_page_path_set)
    set_setting_page_path_value("/setting");
```

- [ ] **Step 8: `--help` text and long option**

Find (lines 1311-1314):

```c
          "\t-s --status-page-path <path>  HTTP path for status UI (default: "
          "/status)\n"
          "\t-p --player-page-path <path>  HTTP path for player UI (default: "
          "/player)\n"
```

Add directly after (still inside the same `fprintf`/string-literal chain):

```c
          "\t   --setting-page-path <path>  HTTP path for settings UI (default: "
          "/setting)\n"
```

Find `enum long_option_e { OPT_APP_PATH_PREFIX = 1000, OPT_USE_RELATIVE_PATH_IN_M3U, OPT_ACCESS_LOG, OPT_LOG_FORMAT };` (line 62) and append a new value:

```c
enum long_option_e { OPT_APP_PATH_PREFIX = 1000, OPT_USE_RELATIVE_PATH_IN_M3U, OPT_ACCESS_LOG, OPT_LOG_FORMAT, OPT_SETTING_PAGE_PATH };
```

Find `{"player-page-path", required_argument, 0, 'p'},` (line 1405) and add directly after:

```c
                                    {"setting-page-path", required_argument, 0, OPT_SETTING_PAGE_PATH},
```

Find the `case OPT_APP_PATH_PREFIX:` handler (line 1533) and add a new case near the other `OPT_*` cases:

```c
    case OPT_SETTING_PAGE_PATH:
      set_setting_page_path_value(optarg);
      cmd_setting_page_path_set = 1;
      break;
```

- [ ] **Step 9: Route dispatch in `connection.c`**

Find the player route block (lines 929-935):

```c
  /* Handle player page */
  const char *player_route = config.player_page_route ? config.player_page_route : "player";
  size_t player_route_len = strlen(player_route);
  if (player_route_len == path_len && strncmp(service_path, player_route, path_len) == 0) {
    handle_embedded_file(c, "/player.html");
    return 0;
  }
```

Add directly after it:

```c
  /* Handle setting page */
  const char *setting_route = config.setting_page_route ? config.setting_page_route : "setting";
  size_t setting_route_len = strlen(setting_route);
  if (setting_route_len == path_len && strncmp(service_path, setting_route, path_len) == 0) {
    handle_embedded_file(c, "/setting.html");
    return 0;
  }
```

- [ ] **Step 10: Build and smoke-test**

Follow the `build-run` skill to build the project (CMake build in `build/`). Run:

```bash
./build/rtp2httpd --help | grep setting-page-path
```

Expected: prints the new `--setting-page-path` usage line.

- [ ] **Step 11: Write the E2E route test**

In `e2e/test_pages.py`, add (using the module's existing `basic_r2h` fixture and `_wait_for_http_status`/`http_get` helpers already imported at the top of the file):

```python
def test_setting_page_default_route_404s_until_asset_exists(basic_r2h):
    """/setting is dispatched, but returns 404 until setting.html is embedded (Task 7)."""
    status, _, _ = http_get("127.0.0.1", basic_r2h.port, "/setting")
    assert status == 404


def test_setting_page_path_is_configurable(r2h_binary):
    port = find_free_port()
    config = f"""\
[global]
verbosity = 4
setting-page-path = /admin

[bind]
* {port}
"""
    r2h = R2HProcess(r2h_binary, port, config_content=config)
    r2h.start()
    try:
        _wait_for_http_status(port, "/admin", expected=404)  # dispatched, asset missing until Task 7
        status, _, _ = http_get("127.0.0.1", port, "/setting")
        assert status == 404  # default route no longer active once overridden
    finally:
        r2h.stop()
```

`R2HProcess`'s constructor (`e2e/helpers/r2h_process.py`) is `R2HProcess(binary, port, extra_args=None, config_content=None, ...)` — `binary` is required and positional (use the `r2h_binary` fixture, not `None`); there is no `config_path=` keyword, only `config_content` (a string — `R2HProcess` writes it to a temp file itself and tracks the path as `r2h._config_path`).

- [ ] **Step 12: Run the new tests**

Follow the `e2e` skill to run: `uv run pytest e2e/test_pages.py -k setting_page -v`
Expected: both tests PASS (404s are the correct, expected result at this stage).

- [ ] **Step 13: Commit**

```bash
git add src/configuration.h src/configuration.c src/connection.c e2e/test_pages.py
git commit -m "feat(setting): add setting-page-path config option and route dispatch"
```

---

## Task 2: `GET .../api/get-config` endpoint

**Files:**
- Create: `src/settings_api.h`
- Create: `src/settings_api.c`
- Modify: `src/connection.c`
- Modify: `CMakeLists.txt` (add `src/settings_api.c` to the source list, next to `src/status.c`)
- Test: `e2e/test_config.py` (new tests appended)

**Interfaces:**
- Consumes: `config_t` from `configuration.h`, `bindaddr_t`/`bind_addresses` from `configuration.h`, `connection_t` from `connection.h`, `send_http_headers`/`connection_queue_output_and_flush` from `http.h`.
- Produces: `void handle_get_config(connection_t *c);` and the field table `SETTING_FIELDS`/`SETTING_FIELDS_COUNT` (used again by Task 4).

- [ ] **Step 1: Create `src/settings_api.h`**

```c
#ifndef __SETTINGS_API_H__
#define __SETTINGS_API_H__

#include "connection.h"

/* GET: returns the live configuration as JSON. */
void handle_get_config(connection_t *c);

/* POST: rewrites rtp2httpd.conf from a form-urlencoded body and reloads. */
void handle_save_config(connection_t *c);

#endif /* __SETTINGS_API_H__ */
```

- [ ] **Step 2: Create `src/settings_api.c` with the field table and JSON serializer**

```c
#include "settings_api.h"
#include "configuration.h"
#include "http.h"
#include "utils.h"
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
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
  } else if (strcmp(ba->node, "*") == 0) {
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
```

- [ ] **Step 3: Wire the route in `connection.c`**

Add `#include "settings_api.h"` near the other includes at the top of `src/connection.c` (next to the existing `#include "status.h"`).

Find the setting-page route block added in Task 1, and add the API prefix dispatch directly after it (mirroring the `status_api_prefix` block at lines 909-928/963-987):

```c
  char setting_api_prefix[HTTP_URL_BUFFER_SIZE];
  if (setting_route_len > 0) {
    snprintf(setting_api_prefix, sizeof(setting_api_prefix), "%s/api/", setting_route);
  } else {
    strncpy(setting_api_prefix, "api/", sizeof(setting_api_prefix) - 1);
    setting_api_prefix[sizeof(setting_api_prefix) - 1] = '\0';
  }
  size_t setting_api_prefix_len = strlen(setting_api_prefix);
  if (path_len >= setting_api_prefix_len && strncmp(service_path, setting_api_prefix, setting_api_prefix_len) == 0) {
    const char *api_name = service_path + setting_api_prefix_len;
    size_t api_name_len = path_len - setting_api_prefix_len;

    if (api_name_len == strlen("get-config") && strncmp(api_name, "get-config", api_name_len) == 0) {
      handle_get_config(c);
      return 0;
    }
    if (api_name_len == strlen("save-config") && strncmp(api_name, "save-config", api_name_len) == 0) {
      handle_save_config(c);
      return 0;
    }
    http_send_404(c);
    return 0;
  }
```

(`handle_save_config` is implemented in Task 4; declare it now via the header included above so this compiles once Task 4 lands — for this task, temporarily stub it in `settings_api.c` with a body that calls `http_send_404(c);` so the project builds; Task 4 replaces the stub.)

- [ ] **Step 4: Add the stub for `handle_save_config`**

In `src/settings_api.c`, add at the end (removed/replaced in Task 4):

```c
void handle_save_config(connection_t *c) {
  http_send_404(c);
}
```

- [ ] **Step 5: Register the new source file in the build**

In `CMakeLists.txt`, find the line listing `src/status.c` in the main source list and add `src/settings_api.c` directly after it, following the file's existing formatting for that list.

- [ ] **Step 6: Build**

Follow the `build-run` skill to rebuild. Confirm no compiler warnings/errors.

- [ ] **Step 7: Write the E2E test**

In `e2e/test_config.py`, check the top of the file for existing imports/fixtures (`R2HProcess`, `http_get`, `find_free_port`) and add:

```python
import json


def test_get_config_endpoint_reflects_running_config(r2h_binary):
    port = find_free_port()
    config = f"""\
[global]
verbosity = 3
maxclients = 7
hostname = example.test

[bind]
* {port}
"""
    r2h = R2HProcess(r2h_binary, port, config_content=config)
    r2h.start()
    try:
        status, _, body = http_get("127.0.0.1", port, "/setting/api/get-config")
        assert status == 200
        data = json.loads(body)
        assert data["maxclients"] == 7
        assert data["hostname"] == "example.test"
        assert data["verbosity"] == 3
        assert data["listen"] == [str(port)]
    finally:
        r2h.stop()
```

- [ ] **Step 8: Run the test**

Follow the `e2e` skill: `uv run pytest e2e/test_config.py -k get_config -v`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add src/settings_api.h src/settings_api.c src/connection.c CMakeLists.txt e2e/test_config.py
git commit -m "feat(setting): add GET .../api/get-config endpoint"
```

---

## Task 3: Config file rewrite engine

**Files:**
- Modify: `src/configuration.h`
- Modify: `src/configuration.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct {
    const char *key;   /* config file key, e.g. "maxclients" */
    const char *value; /* NULL or "" = remove/unset this key */
  } setting_kv_t;

  int config_apply_global_settings(const char *path, const setting_kv_t *kvs, size_t n_kvs,
                                    const char **listen_lines, size_t n_listen);
  ```
  Returns `0` on success, `-1` on failure (I/O error). Rewrites `[global]` in place (only touching active, non-commented lines for keys present in `kvs`; everything else — comments, other keys, ordering — is preserved), and replaces the entire `[bind]` section with `listen_lines` (or a single `* 5140` line if `n_listen == 0`). Creates either section if missing. `[services]` and any other section is untouched, byte-for-byte.

- [ ] **Step 1: Declare the type and function in `src/configuration.h`**

Add near the bottom, just above the `#endif`:

```c
/**
 * A single key/value pair to apply to the [global] section of a config file.
 * A NULL or empty value removes/unsets that key instead of writing it.
 */
typedef struct {
  const char *key;
  const char *value;
} setting_kv_t;

/**
 * Rewrite `path`'s [global] section to apply `kvs`, and replace its [bind]
 * section with `listen_lines` (each already in raw "[bind]"-section-line
 * form, e.g. "* 5140" or "/var/run/rtp2httpd.sock"). Preserves comments,
 * ordering, and all other sections (including [services]) untouched.
 * Creates missing sections. Writes atomically (temp file + rename).
 *
 * @return 0 on success, -1 on I/O error
 */
int config_apply_global_settings(const char *path, const setting_kv_t *kvs, size_t n_kvs,
                                  const char **listen_lines, size_t n_listen);
```

- [ ] **Step 2: Implement it in `src/configuration.c`**

Add near the end of the file, before the final closing of the file (after `bind_addresses_has_unix` or wherever the last function is defined):

```c
/* True if `line` is an ACTIVE (non-commented) assignment for `key`. Commented
 * example/alternative lines (starting with ';' or '#') are never matched, so
 * they're preserved untouched as documentation. */
static int line_is_active_key(const char *line, const char *key) {
  const char *p = line;
  size_t key_len = strlen(key);

  while (*p == ' ' || *p == '\t')
    p++;
  if (*p == ';' || *p == '#')
    return 0;
  if (strncasecmp(p, key, key_len) != 0)
    return 0;
  p += key_len;
  while (*p == ' ' || *p == '\t')
    p++;
  return *p == '=' || *p == '\0';
}

int config_apply_global_settings(const char *path, const setting_kv_t *kvs, size_t n_kvs,
                                  const char **listen_lines, size_t n_listen) {
  char *content;
  long size = 0;
  FILE *f = fopen(path, "r");

  if (f) {
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    content = malloc((size_t)size + 1);
    if (!content) {
      fclose(f);
      return -1;
    }
    if (fread(content, 1, (size_t)size, f) != (size_t)size) {
      fclose(f);
      free(content);
      return -1;
    }
    content[size] = '\0';
    fclose(f);
  } else {
    content = strdup("[global]\n");
    if (!content)
      return -1;
  }

  int *applied = calloc(n_kvs > 0 ? n_kvs : 1, sizeof(int));
  if (!applied) {
    free(content);
    return -1;
  }

  size_t out_cap = (size_t)size + 65536;
  char *out = malloc(out_cap);
  if (!out) {
    free(content);
    free(applied);
    return -1;
  }
  size_t out_len = 0;

  int in_global = 0, in_bind = 0, has_global = 0, has_bind = 0;
  char *saveptr = NULL;
  char *line = strtok_r(content, "\n", &saveptr);

  while (line) {
    const char *t = line;
    while (*t == ' ' || *t == '\t')
      t++;

    if (t[0] == '[') {
      if (in_global) {
        for (size_t i = 0; i < n_kvs; i++) {
          if (!applied[i] && kvs[i].value && kvs[i].value[0]) {
            out_len += snprintf(out + out_len, out_cap - out_len, "%s = %s\n", kvs[i].key, kvs[i].value);
            applied[i] = 1;
          }
        }
      }
      if (in_bind) {
        for (size_t i = 0; i < n_listen; i++)
          out_len += snprintf(out + out_len, out_cap - out_len, "%s\n", listen_lines[i]);
        if (n_listen == 0)
          out_len += snprintf(out + out_len, out_cap - out_len, "* 5140\n");
      }

      in_global = strncasecmp(t, "[global]", 8) == 0;
      in_bind = strncasecmp(t, "[bind]", 6) == 0;
      if (in_global)
        has_global = 1;
      if (in_bind)
        has_bind = 1;

      out_len += snprintf(out + out_len, out_cap - out_len, "%s\n", line);
      line = strtok_r(NULL, "\n", &saveptr);
      continue;
    }

    if (in_bind) {
      line = strtok_r(NULL, "\n", &saveptr);
      continue;
    }

    if (in_global) {
      int matched = 0;
      for (size_t i = 0; i < n_kvs; i++) {
        if (line_is_active_key(t, kvs[i].key)) {
          matched = 1;
          if (!applied[i] && kvs[i].value && kvs[i].value[0]) {
            out_len += snprintf(out + out_len, out_cap - out_len, "%s = %s\n", kvs[i].key, kvs[i].value);
          }
          applied[i] = 1;
          break;
        }
      }
      if (matched) {
        line = strtok_r(NULL, "\n", &saveptr);
        continue;
      }
    }

    out_len += snprintf(out + out_len, out_cap - out_len, "%s\n", line);
    line = strtok_r(NULL, "\n", &saveptr);
  }

  if (in_global) {
    for (size_t i = 0; i < n_kvs; i++) {
      if (!applied[i] && kvs[i].value && kvs[i].value[0]) {
        out_len += snprintf(out + out_len, out_cap - out_len, "%s = %s\n", kvs[i].key, kvs[i].value);
        applied[i] = 1;
      }
    }
  }
  if (in_bind) {
    for (size_t i = 0; i < n_listen; i++)
      out_len += snprintf(out + out_len, out_cap - out_len, "%s\n", listen_lines[i]);
    if (n_listen == 0)
      out_len += snprintf(out + out_len, out_cap - out_len, "* 5140\n");
  }

  if (!has_global) {
    out_len += snprintf(out + out_len, out_cap - out_len, "[global]\n");
    for (size_t i = 0; i < n_kvs; i++) {
      if (!applied[i] && kvs[i].value && kvs[i].value[0]) {
        out_len += snprintf(out + out_len, out_cap - out_len, "%s = %s\n", kvs[i].key, kvs[i].value);
        applied[i] = 1;
      }
    }
  }

  if (!has_bind) {
    out_len += snprintf(out + out_len, out_cap - out_len, "[bind]\n");
    for (size_t i = 0; i < n_listen; i++)
      out_len += snprintf(out + out_len, out_cap - out_len, "%s\n", listen_lines[i]);
    if (n_listen == 0)
      out_len += snprintf(out + out_len, out_cap - out_len, "* 5140\n");
  }

  free(content);
  free(applied);

  char tmp_path[1024];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  FILE *out_f = fopen(tmp_path, "w");
  if (!out_f) {
    free(out);
    return -1;
  }
  size_t written = fwrite(out, 1, out_len, out_f);
  fflush(out_f);
  fsync(fileno(out_f));
  fclose(out_f);
  free(out);

  if (written != out_len) {
    unlink(tmp_path);
    return -1;
  }
  if (rename(tmp_path, path) != 0) {
    unlink(tmp_path);
    return -1;
  }

  return 0;
}
```

Note: `configuration.c` already includes `<stdio.h>`, `<stdlib.h>`, `<string.h>`; confirm `<unistd.h>` (for `fsync`/`unlink`) is included at the top — if not, add `#include <unistd.h>`.

- [ ] **Step 3: Build**

Follow the `build-run` skill to rebuild and confirm it compiles cleanly (no unused-function warnings — `config_apply_global_settings` and `setting_kv_t` are unused until Task 4 wires them in, so expect a possible "defined but not used" warning only if the compiler is set to treat static functions that way; `config_apply_global_settings` itself is non-static/exported so this is not an issue. `line_is_active_key` is `static` and IS used within this same file, so no warning is expected).

- [ ] **Step 4: Commit**

```bash
git add src/configuration.h src/configuration.c
git commit -m "feat(setting): add config_apply_global_settings file-rewrite engine"
```

---

## Task 4: `POST .../api/save-config` endpoint

**Files:**
- Modify: `src/settings_api.c` (replace the Task 2 stub)
- Test: `e2e/test_config.py` (new tests appended)

**Interfaces:**
- Consumes: `SETTING_FIELDS`/`SETTING_FIELDS_COUNT` (Task 2), `config_apply_global_settings`/`setting_kv_t` (Task 3), `http_parse_query_param` (existing, `http.h`), `get_config_file_path` (existing, `configuration.h`).
- Produces: `void handle_save_config(connection_t *c);` (replaces the Task 2 stub of the same signature).

- [ ] **Step 1: Replace the stub with the real handler**

In `src/settings_api.c`, remove the Task 2 stub body and replace `handle_save_config` with:

```c
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
```

- [ ] **Step 2: Build**

Follow the `build-run` skill to rebuild. Fix any compiler warnings (e.g. missing `<errno.h>`/`<signal.h>` includes — `signal.h` for `kill`/`SIGHUP` should already be included from Task 2's `#include <signal.h>`).

- [ ] **Step 3: Write E2E tests**

Append to `e2e/test_config.py` (reusing the `import json`, `R2HProcess`, `http_get`, `find_free_port` already available from Task 2's test):

`R2HProcess` has no `config_path=` keyword — pass the config text via `config_content=`, which writes it to a temp file and records the path on `r2h._config_path` after `start()`. Read that path back to assert on-disk changes:

```python
from helpers import http_request


def test_save_config_updates_file_and_reloads(r2h_binary):
    port = find_free_port()
    config = f"""\
[global]
verbosity = 2
maxclients = 5

[bind]
* {port}

[services]
#EXTM3U
#EXTINF:-1,Test
rtp://239.0.0.1:1234
"""
    r2h = R2HProcess(r2h_binary, port, config_content=config)
    r2h.start()
    try:
        status, _, body = http_request(
            "127.0.0.1",
            port,
            "POST",
            "/setting/api/save-config",
            headers={"Content-Type": "application/x-www-form-urlencoded"},
            body=b"maxclients=42&hostname=example.test",
        )
        assert status == 200
        data = json.loads(body)
        assert data["success"] is True

        with open(r2h._config_path) as f:
            saved = f.read()
        assert "maxclients = 42" in saved
        assert "hostname = example.test" in saved
        assert "#EXTINF:-1,Test" in saved  # [services] untouched
        assert "rtp://239.0.0.1:1234" in saved
    finally:
        r2h.stop()


def test_save_config_rejects_newline_injection(r2h_binary):
    port = find_free_port()
    config = f"[global]\nverbosity = 2\n\n[bind]\n* {port}\n"
    r2h = R2HProcess(r2h_binary, port, config_content=config)
    r2h.start()
    try:
        status, _, body = http_request(
            "127.0.0.1",
            port,
            "POST",
            "/setting/api/save-config",
            headers={"Content-Type": "application/x-www-form-urlencoded"},
            body=b"hostname=evil%0D%0Ar2h-token=hacked",
        )
        assert status == 400
        data = json.loads(body)
        assert data["success"] is False
    finally:
        r2h.stop()
```

- [ ] **Step 4: Run the tests**

Follow the `e2e` skill: `uv run pytest e2e/test_config.py -k save_config -v`
Expected: both PASS.

- [ ] **Step 5: Commit**

```bash
git add src/settings_api.c e2e/test_config.py
git commit -m "feat(setting): add POST .../api/save-config endpoint"
```

---

## Task 5: Frontend field metadata + API hook

**Files:**
- Create: `web-ui/src/lib/setting-fields.ts`
- Create: `web-ui/src/hooks/use-setting-api.ts`

**Interfaces:**
- Produces: `SettingField` type and `SETTING_FIELDS: SettingField[]` (mirrors the backend `SETTING_FIELDS` table 1:1 by `key`, so Task 6's generic renderer covers every field without per-field JSX).
- Produces: `useSettingApi()` returning `{ getConfig(): Promise<Record<string, unknown>>, saveConfig(values: Record<string, string>): Promise<void> }`.

- [ ] **Step 1: Create `web-ui/src/lib/setting-fields.ts`**

```typescript
export type SettingFieldType = "text" | "number" | "select" | "checkbox" | "textarea";

export interface SettingFieldOption {
	value: string;
	label: string;
}

export interface SettingField {
	key: string;
	tab: "basic" | "network" | "player" | "advanced";
	type: SettingFieldType;
	labelKey: string;
	helpKey?: string;
	placeholder?: string;
	options?: SettingFieldOption[];
	min?: number;
	max?: number;
	dependsOn?: { key: string; equals: string };
}

export const SETTING_FIELDS: SettingField[] = [
	{ key: "listen", tab: "basic", type: "textarea", labelKey: "listenAddresses", helpKey: "listenAddressesHelp" },
	{
		key: "verbosity",
		tab: "basic",
		type: "select",
		labelKey: "loggingLevel",
		options: [
			{ value: "0", label: "Fatal" },
			{ value: "1", label: "Error" },
			{ value: "2", label: "Warn" },
			{ value: "3", label: "Info" },
			{ value: "4", label: "Debug" },
		],
	},
	{ key: "upstream-interface", tab: "network", type: "text", labelKey: "upstreamInterface", placeholder: "iptv" },
	{
		key: "upstream-interface-multicast",
		tab: "network",
		type: "text",
		labelKey: "upstreamInterfaceMulticast",
		placeholder: "iptv",
	},
	{
		key: "upstream-interface-fcc",
		tab: "network",
		type: "text",
		labelKey: "upstreamInterfaceFcc",
		placeholder: "iptv",
	},
	{
		key: "upstream-interface-rtsp",
		tab: "network",
		type: "text",
		labelKey: "upstreamInterfaceRtsp",
		placeholder: "iptv",
	},
	{
		key: "upstream-interface-http",
		tab: "network",
		type: "text",
		labelKey: "upstreamInterfaceHttp",
		placeholder: "iptv",
	},
	{ key: "maxclients", tab: "network", type: "number", labelKey: "maxClients", min: 1, max: 5000, placeholder: "5" },
	{ key: "workers", tab: "network", type: "number", labelKey: "workers", min: 1, max: 64, placeholder: "1" },
	{
		key: "buffer-pool-max-size",
		tab: "network",
		type: "number",
		labelKey: "bufferPoolMaxSize",
		min: 1024,
		max: 1048576,
		placeholder: "16384",
	},
	{
		key: "udp-rcvbuf-size",
		tab: "network",
		type: "number",
		labelKey: "udpRcvbufSize",
		min: 65536,
		max: 16777216,
		placeholder: "524288",
	},
	{
		key: "mcast-rejoin-interval",
		tab: "network",
		type: "number",
		labelKey: "mcastRejoinInterval",
		min: 0,
		max: 86400,
		placeholder: "0",
	},
	{
		key: "fcc-listen-port-range",
		tab: "network",
		type: "text",
		labelKey: "fccListenPortRange",
		placeholder: "40000-40100",
	},
	{ key: "zerocopy-on-send", tab: "network", type: "checkbox", labelKey: "zerocopyOnSend" },
	{ key: "rtsp-stun-server", tab: "network", type: "text", labelKey: "rtspStunServer", placeholder: "stun.miwifi.com" },
	{
		key: "external-m3u",
		tab: "player",
		type: "text",
		labelKey: "externalM3u",
		placeholder: "https://example.com/playlist.m3u",
	},
	{
		key: "external-m3u-update-interval",
		tab: "player",
		type: "number",
		labelKey: "externalM3uUpdateInterval",
		min: 0,
		placeholder: "7200",
	},
	{ key: "player-page-path", tab: "player", type: "text", labelKey: "playerPagePath", placeholder: "/player" },
	{ key: "status-page-path", tab: "advanced", type: "text", labelKey: "statusPagePath", placeholder: "/status" },
	{
		key: "setting-page-path",
		tab: "advanced",
		type: "text",
		labelKey: "settingPagePath",
		placeholder: "/setting",
	},
	{
		key: "app-path-prefix",
		tab: "advanced",
		type: "text",
		labelKey: "appPathPrefix",
		placeholder: "/app/rtp2httpd",
	},
	{ key: "use-relative-path-in-m3u", tab: "advanced", type: "checkbox", labelKey: "useRelativePathInM3u" },
	{ key: "hostname", tab: "advanced", type: "text", labelKey: "hostname" },
	{ key: "r2h-token", tab: "advanced", type: "text", labelKey: "r2hToken" },
	{ key: "cors-allow-origin", tab: "advanced", type: "text", labelKey: "corsAllowOrigin", placeholder: "*" },
	{ key: "xff", tab: "advanced", type: "checkbox", labelKey: "xff" },
	{
		key: "access-log",
		tab: "advanced",
		type: "text",
		labelKey: "accessLog",
		placeholder: "/tmp/rtp2httpd-access.log",
	},
	{ key: "log-format", tab: "advanced", type: "text", labelKey: "logFormat" },
	{ key: "http-proxy-user-agent", tab: "advanced", type: "text", labelKey: "httpProxyUserAgent" },
	{ key: "rtsp-user-agent", tab: "advanced", type: "text", labelKey: "rtspUserAgent" },
	{ key: "video-snapshot", tab: "advanced", type: "checkbox", labelKey: "videoSnapshot" },
	{
		key: "ffmpeg-path",
		tab: "advanced",
		type: "text",
		labelKey: "ffmpegPath",
		placeholder: "ffmpeg",
		dependsOn: { key: "video-snapshot", equals: "true" },
	},
	{
		key: "ffmpeg-args",
		tab: "advanced",
		type: "text",
		labelKey: "ffmpegArgs",
		placeholder: "-hwaccel none",
		dependsOn: { key: "video-snapshot", equals: "true" },
	},
];
```

- [ ] **Step 2: Create `web-ui/src/hooks/use-setting-api.ts`**

```typescript
import { useCallback } from "react";
import { buildStatusPath } from "../lib/url";

/**
 * buildStatusPath is page-relative (derived from window.location.pathname),
 * not status-specific despite its name — it works unchanged from /setting.
 */
export function useSettingApi() {
	const getConfig = useCallback(async (): Promise<Record<string, unknown>> => {
		const response = await fetch(buildStatusPath("/api/get-config"));
		if (!response.ok) {
			throw new Error(`Request failed with status ${response.status}`);
		}
		return response.json();
	}, []);

	const saveConfig = useCallback(async (values: Record<string, string>): Promise<void> => {
		const response = await fetch(buildStatusPath("/api/save-config"), {
			method: "POST",
			headers: { "Content-Type": "application/x-www-form-urlencoded" },
			body: new URLSearchParams(values).toString(),
		});
		const data = await response.json().catch(() => undefined);
		if (!response.ok || data?.success === false) {
			throw new Error(data?.error ?? `Request failed with status ${response.status}`);
		}
	}, []);

	return { getConfig, saveConfig };
}
```

- [ ] **Step 3: Typecheck**

Follow the `build-run` skill's web-ui typecheck command (check `package.json` for a `typecheck`/`tsc` script scoped to `web-ui`) and run it. Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add web-ui/src/lib/setting-fields.ts web-ui/src/hooks/use-setting-api.ts
git commit -m "feat(setting): add frontend field metadata and API hook"
```

---

## Task 6: `/setting` page

**Files:**
- Create: `web-ui/setting.html`
- Create: `web-ui/src/pages/setting.tsx`
- Create: `web-ui/src/components/setting/setting-field-input.tsx`
- Create: `web-ui/src/components/setting/setting-tabs.tsx`
- Create: `web-ui/src/i18n/setting.ts`

**Interfaces:**
- Consumes: `SETTING_FIELDS`/`SettingField` (Task 5), `useSettingApi` (Task 5), `useLocale`/`useTheme` hooks and `StatusHeader`-style layout conventions (existing, from `status.tsx`).
- Produces: a mounted React app at `#root` in `setting.html`, matching `status.html`'s HTML shell (theme-flash-prevention script, `index.css`, `#root` div) but with `src="/src/pages/setting.tsx"` and title `rtp2httpd Settings`.

- [ ] **Step 1: Create `web-ui/setting.html`**

Copy `web-ui/status.html` verbatim, then change:
- `"status-theme"` → `"setting-theme"` (the localStorage key in the inline theme script)
- `<title>rtp2httpd Status</title>` → `<title>rtp2httpd Settings</title>`
- `<script type="module" src="/src/pages/status.tsx"></script>` → `<script type="module" src="/src/pages/setting.tsx"></script>`

- [ ] **Step 2: Create `web-ui/src/i18n/setting.ts`**

`status.ts` uses this shape: a `base: Record<string, string>` dict (English), per-locale overlay objects (`zhHans`, `zhHant`) spread over `base`, exported as `translations: Record<Locale, TranslationDict>`, plus `export type TranslationKey = keyof typeof base;` and `export function translate(locale: Locale, key: TranslationKey): string`. Mirror this exact structure in `setting.ts`. Define `base` with these keys (add a `zhHans`/`zhHant` overlay object with the same keys translated, following `status.ts`'s translations for shared concepts like "Save"/"保存" where they overlap):

```typescript
export type TranslationKey =
	| "title"
	| "tabBasic"
	| "tabNetwork"
	| "tabPlayer"
	| "tabAdvanced"
	| "save"
	| "saving"
	| "saveSuccess"
	| "saveError"
	| "openStatusPage"
	| "openPlayerPage"
	| "playerRequiresM3u"
	| "listenAddresses"
	| "listenAddressesHelp"
	| "loggingLevel"
	| "upstreamInterface"
	| "upstreamInterfaceMulticast"
	| "upstreamInterfaceFcc"
	| "upstreamInterfaceRtsp"
	| "upstreamInterfaceHttp"
	| "maxClients"
	| "workers"
	| "bufferPoolMaxSize"
	| "udpRcvbufSize"
	| "mcastRejoinInterval"
	| "fccListenPortRange"
	| "zerocopyOnSend"
	| "rtspStunServer"
	| "externalM3u"
	| "externalM3uUpdateInterval"
	| "playerPagePath"
	| "statusPagePath"
	| "settingPagePath"
	| "appPathPrefix"
	| "useRelativePathInM3u"
	| "hostname"
	| "r2hToken"
	| "corsAllowOrigin"
	| "xff"
	| "accessLog"
	| "logFormat"
	| "httpProxyUserAgent"
	| "rtspUserAgent"
	| "videoSnapshot"
	| "ffmpegPath"
	| "ffmpegArgs";
```

Follow `status.ts`'s exact pattern (dictionary structure, `useLocale`-compatible export names) for the translation values themselves — copy its file structure and only change the keys/English strings above (and their translations for each other locale `status.ts` supports, translating each label to match that locale, consistent with how the rest of the project's UI strings are translated).

- [ ] **Step 3: Create `web-ui/src/components/setting/setting-field-input.tsx`**

```tsx
import type { SettingField } from "../../lib/setting-fields";

interface SettingFieldInputProps {
	field: SettingField;
	label: string;
	value: string;
	onChange: (key: string, value: string) => void;
	disabled?: boolean;
}

export function SettingFieldInput({ field, label, value, onChange, disabled }: SettingFieldInputProps) {
	const inputId = `setting-field-${field.key}`;

	if (field.type === "checkbox") {
		return (
			<label htmlFor={inputId} className="flex items-center gap-2 text-sm">
				<input
					id={inputId}
					type="checkbox"
					checked={value === "true"}
					disabled={disabled}
					onChange={(e) => onChange(field.key, e.target.checked ? "true" : "false")}
					className="h-4 w-4"
				/>
				{label}
			</label>
		);
	}

	if (field.type === "select") {
		return (
			<label htmlFor={inputId} className="flex flex-col gap-1 text-sm">
				<span>{label}</span>
				<select
					id={inputId}
					value={value}
					disabled={disabled}
					onChange={(e) => onChange(field.key, e.target.value)}
					className="rounded border bg-background px-2 py-1"
				>
					{field.options?.map((opt) => (
						<option key={opt.value} value={opt.value}>
							{opt.label}
						</option>
					))}
				</select>
			</label>
		);
	}

	if (field.type === "textarea") {
		return (
			<label htmlFor={inputId} className="flex flex-col gap-1 text-sm">
				<span>{label}</span>
				<textarea
					id={inputId}
					value={value}
					disabled={disabled}
					onChange={(e) => onChange(field.key, e.target.value)}
					rows={4}
					className="rounded border bg-background px-2 py-1 font-mono text-xs"
				/>
			</label>
		);
	}

	return (
		<label htmlFor={inputId} className="flex flex-col gap-1 text-sm">
			<span>{label}</span>
			<input
				id={inputId}
				type={field.type === "number" ? "number" : "text"}
				value={value}
				disabled={disabled}
				placeholder={field.placeholder}
				min={field.min}
				max={field.max}
				onChange={(e) => onChange(field.key, e.target.value)}
				className="rounded border bg-background px-2 py-1"
			/>
		</label>
	);
}
```

- [ ] **Step 4: Create `web-ui/src/components/setting/setting-tabs.tsx`**

```tsx
import { SETTING_FIELDS, type SettingField } from "../../lib/setting-fields";
import { SettingFieldInput } from "./setting-field-input";

const TABS: Array<{ id: SettingField["tab"]; labelKey: string }> = [
	{ id: "basic", labelKey: "tabBasic" },
	{ id: "network", labelKey: "tabNetwork" },
	{ id: "player", labelKey: "tabPlayer" },
	{ id: "advanced", labelKey: "tabAdvanced" },
];

interface SettingTabsProps {
	activeTab: SettingField["tab"];
	onTabChange: (tab: SettingField["tab"]) => void;
	values: Record<string, string>;
	onFieldChange: (key: string, value: string) => void;
	translate: (key: string) => string;
	disabled?: boolean;
}

export function SettingTabs({ activeTab, onTabChange, values, onFieldChange, translate, disabled }: SettingTabsProps) {
	const fieldsForTab = SETTING_FIELDS.filter((f) => f.tab === activeTab);

	return (
		<div className="flex flex-col gap-4">
			<div className="flex gap-2 border-b">
				{TABS.map((tab) => (
					<button
						key={tab.id}
						type="button"
						onClick={() => onTabChange(tab.id)}
						className={`px-3 py-2 text-sm ${activeTab === tab.id ? "border-b-2 border-primary font-medium" : "text-muted-foreground"}`}
					>
						{translate(tab.labelKey)}
					</button>
				))}
			</div>
			<div className="grid grid-cols-1 gap-4 sm:grid-cols-2">
				{fieldsForTab.map((field) => {
					const isVisible =
						!field.dependsOn || (values[field.dependsOn.key] ?? "") === field.dependsOn.equals;
					if (!isVisible)
						return null;
					return (
						<SettingFieldInput
							key={field.key}
							field={field}
							label={translate(field.labelKey)}
							value={values[field.key] ?? ""}
							onChange={onFieldChange}
							disabled={disabled}
						/>
					);
				})}
			</div>
		</div>
	);
}
```

- [ ] **Step 5: Create `web-ui/src/pages/setting.tsx`**

```tsx
import { StrictMode, useCallback, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import { SettingTabs } from "../components/setting/setting-tabs";
import { useLocale } from "../hooks/use-locale";
import { useSettingApi } from "../hooks/use-setting-api";
import { useTheme } from "../hooks/use-theme";
import type { SettingField } from "../lib/setting-fields";
import { buildStatusPath } from "../lib/url";

/* Mirrors status.ts's useLocale-based translation hook shape; adapt the
 * import path/name below to whatever status.tsx actually imports if the
 * project's convention differs (e.g. useStatusTranslation vs a shared
 * useTranslation(locale, dictionary) helper). */
import { useSettingTranslation } from "../hooks/use-setting-translation";

function valuesToStrings(raw: Record<string, unknown>): Record<string, string> {
	const out: Record<string, string> = {};
	for (const [key, value] of Object.entries(raw)) {
		if (Array.isArray(value)) {
			out[key] = value.join("\n");
		} else if (typeof value === "boolean") {
			out[key] = value ? "true" : "false";
		} else {
			out[key] = String(value ?? "");
		}
	}
	return out;
}

function SettingPage() {
	const { locale } = useLocale("setting-locale");
	const t = useSettingTranslation(locale);
	useTheme("setting-theme");
	const { getConfig, saveConfig } = useSettingApi();

	const [activeTab, setActiveTab] = useState<SettingField["tab"]>("basic");
	const [values, setValues] = useState<Record<string, string>>({});
	const [loading, setLoading] = useState(true);
	const [saving, setSaving] = useState(false);
	const [message, setMessage] = useState<{ kind: "success" | "error"; text: string } | null>(null);

	const load = useCallback(async () => {
		setLoading(true);
		try {
			const raw = await getConfig();
			setValues(valuesToStrings(raw));
		} catch (err) {
			setMessage({ kind: "error", text: err instanceof Error ? err.message : String(err) });
		} finally {
			setLoading(false);
		}
	}, [getConfig]);

	useEffect(() => {
		load();
	}, [load]);

	const handleFieldChange = useCallback((key: string, value: string) => {
		setValues((prev) => ({ ...prev, [key]: value }));
	}, []);

	const handleSave = useCallback(async () => {
		setSaving(true);
		setMessage(null);
		try {
			await saveConfig(values);
			setMessage({ kind: "success", text: t("saveSuccess") });
			await load();
		} catch (err) {
			setMessage({ kind: "error", text: err instanceof Error ? err.message : t("saveError") });
		} finally {
			setSaving(false);
		}
	}, [saveConfig, values, t, load]);

	if (loading) {
		return <div className="p-6 text-sm text-muted-foreground">…</div>;
	}

	return (
		<div className="mx-auto flex max-w-3xl flex-col gap-6 p-6">
			<h1 className="text-xl font-semibold">{t("title")}</h1>

			<SettingTabs
				activeTab={activeTab}
				onTabChange={setActiveTab}
				values={values}
				onFieldChange={handleFieldChange}
				translate={t}
				disabled={saving}
			/>

			<div className="flex items-center gap-3">
				<button
					type="button"
					onClick={handleSave}
					disabled={saving}
					className="rounded bg-primary px-4 py-2 text-sm text-primary-foreground disabled:opacity-50"
				>
					{saving ? t("saving") : t("save")}
				</button>
				<a href={buildStatusPath("").replace(/\/setting$/, "/status")} className="text-sm underline">
					{t("openStatusPage")}
				</a>
				<a href={buildStatusPath("").replace(/\/setting$/, "/player")} className="text-sm underline">
					{t("openPlayerPage")}
				</a>
				{message && (
					<span className={message.kind === "success" ? "text-sm text-green-600" : "text-sm text-red-600"}>
						{message.text}
					</span>
				)}
			</div>
		</div>
	);
}

createRoot(document.getElementById("root") as HTMLElement).render(
	<StrictMode>
		<SettingPage />
	</StrictMode>,
);
```

Note the status/player links use a naive `.replace(/\/setting$/, "/status")` against the *current* page's own path — this only produces a correct link when `/setting` is at the default route. Since `setting-page-path`/`status-page-path`/`player-page-path` are independently configurable, this is a known simplification: if the operator has customized any of the three page paths away from their defaults, these two links may be wrong (a broken/missing link, not a crash). Document this as an accepted limitation; do not attempt to fetch the other pages' configured paths just to link to them, since `get-config`'s response already contains `status-page-path` and `player-page-path` — instead, build the two links directly from `values["status-page-path"]` and `values["player-page-path"]` (both already loaded) rather than string-replacing the current URL. Replace the two `<a>` tags above with:

```tsx
				<a href={values["status-page-path"] || "/status"} className="text-sm underline">
					{t("openStatusPage")}
				</a>
				<a href={values["player-page-path"] || "/player"} className="text-sm underline">
					{t("openPlayerPage")}
				</a>
```

- [ ] **Step 6: Create `web-ui/src/hooks/use-setting-translation.ts`**

`use-status-translation.ts` is:

```typescript
import { useCallback } from "react";
import type { TranslationKey } from "../i18n/status";
import { translate } from "../i18n/status";
import type { Locale } from "../lib/locale";

export function useStatusTranslation(locale: Locale) {
  return useCallback((key: TranslationKey) => translate(locale, key), [locale]);
}
```

Create the same file, pointing at `../i18n/setting` (Step 2) instead, and renamed:

```typescript
import { useCallback } from "react";
import type { TranslationKey } from "../i18n/setting";
import { translate } from "../i18n/setting";
import type { Locale } from "../lib/locale";

export function useSettingTranslation(locale: Locale) {
  return useCallback((key: TranslationKey) => translate(locale, key), [locale]);
}
```

- [ ] **Step 7: Add the Vite build entry**

In `web-ui/vite.config.ts`, update the `rolldownOptions.input` object:

```typescript
        input: {
          status: resolve(__dirname, "status.html"),
          player: resolve(__dirname, "player.html"),
          setting: resolve(__dirname, "setting.html"),
        },
```

- [ ] **Step 8: Build the web UI and rebuild the embedded asset header**

Follow the `build-run` skill's documented commands (from `package.json`, using `nvm`/`pnpm` per this project's toolchain):

```bash
pnpm run web-ui:build
```

Expected: completes without errors, and `src/embedded_web_data.h` is regenerated (now containing `setting.html` and its assets).

- [ ] **Step 9: Rebuild the C binary and manually verify**

Follow the `build-run` skill to rebuild `rtp2httpd`, then start it against a local config (or use `tools/devlab`) and open `http://127.0.0.1:<port>/setting` in a browser. Confirm:
- The page loads without console errors.
- All four tabs render fields.
- Changing a field and clicking Save shows a success message.
- Reloading the page shows the saved value persisted.
- `/status` and `/player` still work unaffected.

- [ ] **Step 10: Commit**

```bash
git add web-ui/setting.html web-ui/src/pages/setting.tsx web-ui/src/components/setting web-ui/src/i18n/setting.ts web-ui/src/hooks/use-setting-translation.ts web-ui/vite.config.ts src/embedded_web_data.h
git commit -m "feat(setting): add /setting web UI page"
```

(Per `CLAUDE.md`: only include `src/embedded_web_data.h` in this commit since it's a genuine, intentional rebuild driven by this feature — this is the one case where committing it is expected.)

---

## Task 7: E2E coverage for the full page + docs updates

**Files:**
- Modify: `e2e/test_pages.py` (replace the Task 1 placeholder 404 tests now that the asset exists)
- Modify: `docs/guide/installation.md`
- Modify: `docs/en/guide/installation.md`
- Modify: `docs/reference/configuration.md`
- Modify: `docs/en/reference/configuration.md`
- Modify: `README.md`
- Modify: `README.en.md`

**Interfaces:** none (test + docs only).

- [ ] **Step 1: Update the Task 1 placeholder tests**

In `e2e/test_pages.py`, replace `test_setting_page_default_route_404s_until_asset_exists` and `test_setting_page_path_is_configurable` (from Task 1) with:

```python
def test_setting_page_serves_html(basic_r2h):
    status, headers, body = http_get("127.0.0.1", basic_r2h.port, "/setting")
    assert status == 200
    assert "text/html" in get_header(headers, "Content-Type")
    assert b"<title>rtp2httpd Settings</title>" in body


def test_setting_page_path_is_configurable(r2h_binary):
    port = find_free_port()
    config = f"""\
[global]
verbosity = 4
setting-page-path = /admin

[bind]
* {port}
"""
    r2h = R2HProcess(r2h_binary, port, config_content=config)
    r2h.start()
    try:
        _wait_for_http_status(port, "/admin", expected=200)
        status, _, _ = http_get("127.0.0.1", port, "/setting")
        assert status == 404
    finally:
        r2h.stop()
```

- [ ] **Step 2: Run the full page + config E2E suites**

Follow the `e2e` skill: `uv run pytest e2e/test_pages.py e2e/test_config.py -v`
Expected: all PASS.

- [ ] **Step 3: Update `docs/guide/installation.md`**

In the `## Armbian / Debian 设备部署` section, after the line describing what the installer sets up (the line ending `...并创建 /etc/systemd/system/rtp2httpd.service。`), add a new paragraph:

```markdown
安装完成后，可以通过浏览器访问 `http://<设备IP>:5140/setting` 打开设置页面，无需 SSH 登录即可修改配置（保存后自动生效）。
```

- [ ] **Step 4: Update `docs/en/guide/installation.md`** — use the `translate-docs-zh-en` skill for this file (per `CLAUDE.md`, English docs are always produced via that skill, not by hand-translating) to bring it back in sync with Step 3's change, rather than manually editing the English text here.

- [ ] **Step 5: Update `docs/reference/configuration.md`**

Find the existing description of `status-page-path`/`player-page-path` and add a matching entry for `setting-page-path`, following that file's existing table/list format exactly (same column structure, default value `/setting`, one-line description: "设置页面的 HTTP 路径").

- [ ] **Step 6: Update `docs/en/reference/configuration.md`** — again via the `translate-docs-zh-en` skill, driven by Step 5's change.

- [ ] **Step 7: Update `README.md` and `README.en.md`**

In `README.md`'s `### 📊 实时状态监控` section (or add a new bullet near it), add:

```markdown
- **设置页面**：通过浏览器访问 `http://<server:port>/setting` 修改全局配置，无需编辑配置文件或重启服务
```

Make the equivalent addition to `README.en.md`'s `### 📊 Real-Time Status Monitoring` section:

```markdown
- **Settings page**: Change global configuration from your browser at `http://<server:port>/setting`, no config-file editing or manual restart required
```

- [ ] **Step 8: Commit**

```bash
git add e2e/test_pages.py docs/guide/installation.md docs/en/guide/installation.md docs/reference/configuration.md docs/en/reference/configuration.md README.md README.en.md
git commit -m "docs: document the /setting page; update E2E coverage for the real page"
```

---

## Task 8: Fix the Armbian installer's default config comment (bonus, discovered during planning)

**Files:**
- Modify: `scripts/install-armbian.sh`

**Interfaces:** none — this is a standalone one-line documentation fix inside a heredoc, unrelated to any other task's interfaces.

While mapping the config file format for Task 3, the default config generated by `create_default_config()` in `scripts/install-armbian.sh` was found to contain a misleading example: `#listen = 0.0.0.0:5140` is not valid syntax (bind addresses go in a `[bind]` section as `node port` lines, not a `listen =` key under `[global]`). Fix it while this file's format is fresh context.

- [ ] **Step 1: Fix the heredoc**

In `scripts/install-armbian.sh`, find:

```bash
    cat > "$CONFIG_PATH" <<'EOF'
# rtp2httpd default config
[global]
verbosity = 2
#listen = 0.0.0.0:5140
#maxclients = 20
EOF
```

Replace with:

```bash
    cat > "$CONFIG_PATH" <<'EOF'
# rtp2httpd default config
[global]
verbosity = 2
#maxclients = 20

[bind]
* 5140
EOF
```

- [ ] **Step 2: Verify**

```bash
sh -n scripts/install-armbian.sh && echo OK
```

Expected: `OK`.

- [ ] **Step 3: Commit**

```bash
git add scripts/install-armbian.sh
git commit -m "fix(scripts): correct misleading listen example in armbian default config"
```
