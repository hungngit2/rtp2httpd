# Web Basic Auth for /status, /player, /setting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Require HTTP Basic Auth on `/status`, `/player`, `/setting` (and their APIs/SSE) for non-local clients, when `web-auth-user`/`web-auth-password` are configured.

**Architecture:** A new `is_client_local()` check in `connection.c` classifies the request's effective client address (raw socket peer, or the first `X-Forwarded-For` hop when `xff` is enabled) as local/private or not. A new Basic Auth check runs right after the existing r2h-token check, scoped only to the three page routes and their API/SSE sub-routes, and only enforced when both credentials are configured — for non-local clients always, and for local clients too when `web-auth-require-local` is enabled. Credentials live in two new plaintext config fields plus one boolean toggle, following the exact patterns already used for `r2h-token` and `xff`.

**Tech Stack:** C11 (daemon), Python/pytest (e2e), React/TypeScript (settings UI).

## Global Constraints

- Pure C11, no C++ features (project-wide).
- 2-space indent, no tabs, in C files; `_s`/`_t` struct naming.
- Logging via `logger()`, never `printf`/`fprintf`.
- `snprintf`/`strncpy`, never `sprintf`/`strcpy`.
- Cross-platform (Linux/macOS/FreeBSD) — guard any platform-specific API with `#ifdef`.
- Config file format is INI; new keys are `web-auth-user` / `web-auth-password`.
- `e2e/` tests run against a real built binary (no C unit test framework exists in this repo) — the "write failing test, then implement" cycle in this plan means: write the pytest test against the *not-yet-built* feature, confirm it fails against the current binary, implement, rebuild, rerun.
- The dev environment lacks `uv`; use `pip3 install --user pytest pytest-xdist scapy` once, then invoke `python3 -m pytest e2e/... -p no:cacheprovider` directly instead of `scripts/run-e2e.sh` (which hard-requires `uv`).
- Terse code comments — only non-obvious "why", one line, no multi-line blocks.

---

### Task 1: Base64 decode helper in `utils.c`/`utils.h`

**Files:**
- Modify: `src/utils.h` (add declaration near other string helpers, e.g. after `format_host_port_for_url`)
- Modify: `src/utils.c` (add implementation)
- Test: manual verification via a scratch C program (no C unit test framework exists in this repo)

**Interfaces:**
- Produces: `int base64_decode(const char *input, char *output, size_t output_size);` — decodes standard base64 (RFC 4648, `+`/`/` alphabet, `=` padding required, input length must be a multiple of 4). Returns decoded byte count (excludes the extra NUL this function also writes at `output[count]`) on success, `-1` on malformed input or if `output_size` is too small. Used by Task 4.

- [ ] **Step 1: Add the declaration to `src/utils.h`**

Insert after the `format_host_port_for_url` declaration (before the `json_escaped_len` block):

```c
/**
 * Decode a standard base64 string (RFC 4648 alphabet, `=` padding required).
 * @param input NUL-terminated base64 string; length must be a multiple of 4
 * @param output Output buffer (NUL-terminated on success)
 * @param output_size Size of output buffer
 * @return Decoded byte count on success, -1 on malformed input or if output_size is too small
 */
int base64_decode(const char *input, char *output, size_t output_size);
```

- [ ] **Step 2: Add the implementation to `src/utils.c`**

Check the top of `src/utils.c` for its existing `#include` list first (`Read src/utils.c` lines 1-20) and add `<string.h>` if not already present. Then append this near the end of the file:

```c
static int base64_char_value(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

int base64_decode(const char *input, char *output, size_t output_size) {
  if (!input || !output)
    return -1;

  size_t len = strlen(input);
  if (len == 0 || len % 4 != 0)
    return -1;

  size_t pad = 0;
  if (input[len - 1] == '=')
    pad++;
  if (len >= 2 && input[len - 2] == '=')
    pad++;

  size_t decoded_len = (len / 4) * 3 - pad;
  if (decoded_len >= output_size)
    return -1;

  size_t out_i = 0;
  for (size_t i = 0; i < len; i += 4) {
    int v0 = base64_char_value(input[i]);
    int v1 = base64_char_value(input[i + 1]);
    int v2 = (input[i + 2] == '=') ? 0 : base64_char_value(input[i + 2]);
    int v3 = (input[i + 3] == '=') ? 0 : base64_char_value(input[i + 3]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0)
      return -1;

    uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
    output[out_i++] = (char)((triple >> 16) & 0xFF);
    if (input[i + 2] != '=')
      output[out_i++] = (char)((triple >> 8) & 0xFF);
    if (input[i + 3] != '=')
      output[out_i++] = (char)(triple & 0xFF);
  }
  output[out_i] = '\0';
  return (int)out_i;
}
```

- [ ] **Step 3: Verify it compiles and behaves correctly with a scratch program**

```bash
cat > /tmp/test_base64.c <<'EOF'
#include <stdio.h>
#include <string.h>
int base64_decode(const char *input, char *output, size_t output_size);
int main(void) {
  char out[64];
  int n = base64_decode("YWRtaW46c2VjcmV0", out, sizeof(out)); /* admin:secret */
  printf("n=%d out=%s\n", n, out);
  int bad = base64_decode("not-base64!!", out, sizeof(out));
  printf("bad=%d\n", bad);
  return (n == 12 && strcmp(out, "admin:secret") == 0 && bad == -1) ? 0 : 1;
}
EOF
cc -DHTTP_URL_BUFFER_SIZE=2048 /tmp/test_base64.c -I src -x c - -o /tmp/test_base64 <<'EOF'
EOF
```

Since `base64_decode` will live in `utils.c` alongside other functions requiring project headers, instead just compile the two new functions directly for this smoke test:

```bash
awk '/^static int base64_char_value/,/^}$/' src/utils.c > /tmp/b64_impl.c
awk '/^int base64_decode/,0' src/utils.c | awk '/^}$/{print;exit}1' >> /tmp/b64_impl.c
cat > /tmp/test_base64.c <<'EOF'
#include <stdio.h>
#include <string.h>
#include <stdint.h>
EOF
cat /tmp/b64_impl.c >> /tmp/test_base64.c
cat >> /tmp/test_base64.c <<'EOF'
int main(void) {
  char out[64];
  int n = base64_decode("YWRtaW46c2VjcmV0", out, sizeof(out));
  printf("n=%d out=%s\n", n, out);
  int bad = base64_decode("not-base64!!", out, sizeof(out));
  printf("bad=%d\n", bad);
  return (n == 12 && strcmp(out, "admin:secret") == 0 && bad == -1) ? 0 : 1;
}
EOF
cc /tmp/test_base64.c -o /tmp/test_base64 && /tmp/test_base64
```

Expected output: `n=12 out=admin:secret` and `bad=-1`, exit code 0.

- [ ] **Step 4: Full project build to confirm no regressions**

```bash
cmake --build build -j
```

Expected: builds successfully (warnings pre-existing in this file are fine; no new errors).

- [ ] **Step 5: Commit**

```bash
git add src/utils.h src/utils.c
git commit -m "feat(utils): add base64_decode helper"
```

---

### Task 2: Config fields `web-auth-user` / `web-auth-password` / `web-auth-require-local`

**Files:**
- Modify: `src/configuration.h`
- Modify: `src/configuration.c`

**Interfaces:**
- Consumes: nothing new.
- Produces: `config.web_auth_user` (char*, NULL default), `config.web_auth_password` (char*, NULL default), `config.web_auth_require_local` (int, 0 default) — consumed by Task 4 (`connection.c`) and Task 5 (`settings_api.c`).

- [ ] **Step 1: Add the fields to `config_t` in `src/configuration.h`**

Add after the `r2h_token` field declaration (in the "Network and service settings" block, `src/configuration.h` around line 53):

```c
  char *r2h_token; /* Authentication token for HTTP requests (NULL=disabled) */
  char *web_auth_user;     /* HTTP Basic Auth username for /status,/player,/setting
                               from non-local clients (NULL=disabled) */
  char *web_auth_password; /* HTTP Basic Auth password (NULL=disabled) */
  int web_auth_require_local; /* Also require Basic Auth for local/LAN clients
                                  (0=local bypasses auth [default], 1=always required) */
```

- [ ] **Step 2: Add the `cmd_*_set` flags in `src/configuration.c`**

After `int cmd_r2h_token_set = 0;` (around line 34):

```c
int cmd_r2h_token_set = 0;
int cmd_web_auth_user_set = 0;
int cmd_web_auth_password_set = 0;
int cmd_web_auth_require_local_set = 0;
```

- [ ] **Step 3: Free the strings in `free_config_strings()`**

After the `r2h_token` free block (around line 121-122). Note: `web_auth_require_local` is an `int`, not a string, so it needs no free — it's handled by `config_init()`'s default-reset in Step 7 instead.

```c
  if (!cmd_r2h_token_set || force_free)
    safe_free_string(&target->r2h_token);
  if (!cmd_web_auth_user_set || force_free)
    safe_free_string(&target->web_auth_user);
  if (!cmd_web_auth_password_set || force_free)
    safe_free_string(&target->web_auth_password);
```

- [ ] **Step 4: Parse from config file in `parse_global_sec()`**

After the `r2h-token` block (around line 678-684). `web-auth-require-local` follows the existing boolean pattern (compare `use-relative-path-in-m3u` at `src/configuration.c:633-636`), using the already-available `parse_bool()`:

```c
  if (strcasecmp("r2h-token", param) == 0) {
    if (set_if_not_cmd_override(cmd_r2h_token_set, "r2h-token")) {
      safe_free_string(&config.r2h_token);
      config.r2h_token = strdup(value);
    }
    return;
  }

  if (strcasecmp("web-auth-user", param) == 0) {
    if (set_if_not_cmd_override(cmd_web_auth_user_set, "web-auth-user")) {
      safe_free_string(&config.web_auth_user);
      config.web_auth_user = strdup(value);
    }
    return;
  }

  if (strcasecmp("web-auth-password", param) == 0) {
    if (set_if_not_cmd_override(cmd_web_auth_password_set, "web-auth-password")) {
      safe_free_string(&config.web_auth_password);
      config.web_auth_password = strdup(value);
    }
    return;
  }

  if (strcasecmp("web-auth-require-local", param) == 0) {
    if (set_if_not_cmd_override(cmd_web_auth_require_local_set, "web-auth-require-local"))
      config.web_auth_require_local = parse_bool(value);
    return;
  }
```

- [ ] **Step 5: Add to `config_snapshot()`**

In the NULL-out block (around line 1087, after `snapshot->r2h_token = NULL;`) — only the two string fields need NULLing, `web_auth_require_local` is a plain `int` already copied by the earlier `*snapshot = config;` struct copy:

```c
  snapshot->r2h_token = NULL;
  snapshot->web_auth_user = NULL;
  snapshot->web_auth_password = NULL;
```

And in the `SNAPSHOT_STRING` calls (around line 1113, after `SNAPSHOT_STRING(r2h_token, cmd_r2h_token_set);`):

```c
  SNAPSHOT_STRING(r2h_token, cmd_r2h_token_set);
  SNAPSHOT_STRING(web_auth_user, cmd_web_auth_user_set);
  SNAPSHOT_STRING(web_auth_password, cmd_web_auth_password_set);
```

- [ ] **Step 6: Add long-only CLI options**

These are config-file/settings-page-oriented fields with no natural short letter free (`-T`/`-U`/etc. are taken); follow the existing long-only pattern used for `--setting-page-path`/`--access-log` (`enum long_option_e`, `src/configuration.c` around line 66-70):

```c
enum long_option_e {
  OPT_APP_PATH_PREFIX = 1000,
  OPT_USE_RELATIVE_PATH_IN_M3U,
  OPT_ACCESS_LOG,
  OPT_LOG_FORMAT,
  OPT_SETTING_PAGE_PATH,
  OPT_WEB_AUTH_USER,
  OPT_WEB_AUTH_PASSWORD,
  OPT_WEB_AUTH_REQUIRE_LOCAL
};
```

Add to the `long_options[]` array (after the `{"setting-page-path", ...}` entry, around line 1438):

```c
                                    {"setting-page-path", required_argument, 0, OPT_SETTING_PAGE_PATH},
                                    {"web-auth-user", required_argument, 0, OPT_WEB_AUTH_USER},
                                    {"web-auth-password", required_argument, 0, OPT_WEB_AUTH_PASSWORD},
                                    {"web-auth-require-local", no_argument, 0, OPT_WEB_AUTH_REQUIRE_LOCAL},
```

Add the `case` handlers in the `getopt_long` switch (after the `case OPT_SETTING_PAGE_PATH:` block, around line 1566-1569). The boolean case follows the `OPT_USE_RELATIVE_PATH_IN_M3U` pattern (`src/configuration.c:1574-1577`) — `no_argument` options set the flag to `1` directly, they don't call `parse_bool`:

```c
    case OPT_SETTING_PAGE_PATH:
      set_setting_page_path_value(optarg);
      cmd_setting_page_path_set = 1;
      break;
    case OPT_WEB_AUTH_USER:
      safe_free_string(&config.web_auth_user);
      config.web_auth_user = strdup(optarg);
      cmd_web_auth_user_set = 1;
      break;
    case OPT_WEB_AUTH_PASSWORD:
      safe_free_string(&config.web_auth_password);
      config.web_auth_password = strdup(optarg);
      cmd_web_auth_password_set = 1;
      break;
    case OPT_WEB_AUTH_REQUIRE_LOCAL:
      config.web_auth_require_local = 1;
      cmd_web_auth_require_local_set = 1;
      break;
```

- [ ] **Step 7: Add the default reset in `config_init()`**

Find the block of `if (!cmd_..._set) config...= 0;` boolean-default resets (around `src/configuration.c:1176-1184`, alongside `cmd_xff_set`/`cmd_use_relative_path_in_m3u_set`) and add:

```c
  if (!cmd_xff_set)
    config.xff = 0;
  if (!cmd_web_auth_require_local_set)
    config.web_auth_require_local = 0;
```

- [ ] **Step 8: Add to `usage()` help text**

After the `--setting-page-path` usage line (`src/configuration.c` around line 1345):

```c
          "\t   --web-auth-user <user>  HTTP Basic Auth username for "
          "/status,/player,/setting from non-local clients (default: disabled)\n"
          "\t   --web-auth-password <password>  HTTP Basic Auth password "
          "(default: disabled)\n"
          "\t   --web-auth-require-local  Also require HTTP Basic Auth for "
          "local/LAN clients (default: off, local bypasses auth)\n"
```

- [ ] **Step 9: Build**

```bash
cmake --build build -j
```

Expected: builds successfully with no new warnings/errors.

- [ ] **Step 10: Commit**

```bash
git add src/configuration.h src/configuration.c
git commit -m "feat(config): add web-auth-user/password/require-local fields"
```

---

### Task 3: Capture the `Authorization` header

**Files:**
- Modify: `src/http.h`
- Modify: `src/http.c`

**Interfaces:**
- Consumes: existing header-parsing loop in `http_parse_request()`.
- Produces: `c->http_req.authorization` (char[512], NUL-terminated raw header value, e.g. `"Basic YWRtaW46c2VjcmV0"`) — consumed by Task 4.

- [ ] **Step 1: Add the field to `http_request_t` in `src/http.h`**

After the `cookie` field (around line 52):

```c
  char cookie[HTTP_COOKIE_BUFFER_SIZE];     /* Cookie header value for r2h-token extraction */
  char authorization[512];                  /* Authorization header value, e.g. "Basic <base64>" */
```

- [ ] **Step 2: Populate it in `http_parse_request()`'s header loop**

In `src/http.c`, after the `Cookie` extraction block (around line 377-379):

```c
        } else if (strcasecmp(inbuf, "Cookie") == 0) {
          strncpy(req->cookie, value, sizeof(req->cookie) - 1);
          req->cookie[sizeof(req->cookie) - 1] = '\0';
        } else if (strcasecmp(inbuf, "Authorization") == 0) {
          strncpy(req->authorization, value, sizeof(req->authorization) - 1);
          req->authorization[sizeof(req->authorization) - 1] = '\0';
        } else if (strcasecmp(inbuf, "Access-Control-Request-Method") == 0) {
```

(This inserts the new `else if` branch between the existing `Cookie` and `Access-Control-Request-Method` branches — merge carefully, don't duplicate the `Access-Control-Request-Method` line.)

- [ ] **Step 3: No changes needed to `http_request_init()`/`http_request_cleanup()`**

`http_request_init()` (`src/http.c:183-192`) already does `memset(req, 0, sizeof(*req))`, which zero-initializes the new `authorization` array — no additional code needed here.

- [ ] **Step 4: Build**

```bash
cmake --build build -j
```

- [ ] **Step 5: Commit**

```bash
git add src/http.h src/http.c
git commit -m "feat(http): capture Authorization header"
```

---

### Task 4: `is_client_local()` + Basic Auth enforcement in `connection.c`

**Files:**
- Modify: `src/connection.c`
- Modify: `src/http.h` (new send-401 helper declaration)
- Modify: `src/http.c` (new send-401 helper implementation)
- Test: `e2e/test_pages.py` (new test class)

**Interfaces:**
- Consumes: `config.web_auth_user`/`config.web_auth_password`/`config.web_auth_require_local` (Task 2), `c->http_req.authorization` (Task 3), `base64_decode()` (Task 1).
- Produces: nothing consumed by later tasks — this is the last backend task.

- [ ] **Step 1: Write the failing e2e test**

First install test deps once (the sandbox lacks `uv`):

```bash
pip3 install --user pytest pytest-xdist scapy
```

Add this test class to `e2e/test_pages.py`, after the `TestAppPathPrefix` class (before `test_token_cookie_path_uses_app_prefix`'s enclosing class ends — i.e. append as a new top-level class at the end of the file):

```python
import base64


class TestWebBasicAuth:
    """web-auth-user/web-auth-password gate /status, /player, /setting (and
    their APIs/SSE) for non-local clients only."""

    def _config(self, port: int) -> str:
        return f"""\
[global]
verbosity = 4
xff = 1
web-auth-user = admin
web-auth-password = secret

[bind]
* {port}
"""

    def _basic_auth_header(self, user: str, password: str) -> dict:
        token = base64.b64encode(f"{user}:{password}".encode()).decode()
        return {"Authorization": f"Basic {token}"}

    def test_local_client_bypasses_auth(self, r2h_binary):
        port = find_free_port()
        r2h = R2HProcess(r2h_binary, port, config_content=self._config(port))
        try:
            r2h.start()
            status, _, _ = http_get("127.0.0.1", port, "/status")
            assert status == 200
        finally:
            r2h.stop()

    def test_non_local_client_requires_auth(self, r2h_binary):
        port = find_free_port()
        r2h = R2HProcess(r2h_binary, port, config_content=self._config(port))
        try:
            r2h.start()
            status, hdrs, _ = http_get(
                "127.0.0.1", port, "/status", headers={"X-Forwarded-For": "8.8.8.8"}
            )
            assert status == 401
            assert "Basic" in get_header(hdrs, "WWW-Authenticate")

            status, _, _ = http_get(
                "127.0.0.1",
                port,
                "/status",
                headers={"X-Forwarded-For": "8.8.8.8", **self._basic_auth_header("admin", "secret")},
            )
            assert status == 200

            status, _, _ = http_get(
                "127.0.0.1",
                port,
                "/status",
                headers={"X-Forwarded-For": "8.8.8.8", **self._basic_auth_header("admin", "wrong")},
            )
            assert status == 401
        finally:
            r2h.stop()

    def test_non_local_client_setting_and_player_require_auth(self, r2h_binary):
        port = find_free_port()
        r2h = R2HProcess(r2h_binary, port, config_content=self._config(port))
        try:
            r2h.start()
            for path in ("/player", "/setting", "/setting/api/get-config"):
                status, _, _ = http_get(
                    "127.0.0.1", port, path, headers={"X-Forwarded-For": "8.8.8.8"}
                )
                assert status == 401, f"{path} should require auth"

                status, _, _ = http_get(
                    "127.0.0.1",
                    port,
                    path,
                    headers={"X-Forwarded-For": "8.8.8.8", **self._basic_auth_header("admin", "secret")},
                )
                assert status == 200, f"{path} should succeed with correct credentials"
        finally:
            r2h.stop()

    def test_non_local_stream_route_unaffected(self, r2h_binary):
        port = find_free_port()
        config = self._config(port) + '\n[services]\nrtp://239.0.0.1:1234\n'
        r2h = R2HProcess(r2h_binary, port, config_content=config)
        try:
            r2h.start()
            status, _, _ = http_request(
                "127.0.0.1",
                port,
                "HEAD",
                "/rtp/239.0.0.1:1234",
                headers={"X-Forwarded-For": "8.8.8.8"},
                timeout=3.0,
            )
            assert status == 200
        finally:
            r2h.stop()

    def test_require_local_forces_auth_for_loopback(self, r2h_binary):
        port = find_free_port()
        config = f"""\
[global]
verbosity = 4
web-auth-user = admin
web-auth-password = secret
web-auth-require-local = 1

[bind]
* {port}
"""
        r2h = R2HProcess(r2h_binary, port, config_content=config)
        try:
            r2h.start()
            status, _, _ = http_get("127.0.0.1", port, "/status")
            assert status == 401

            status, _, _ = http_get(
                "127.0.0.1", port, "/status", headers=self._basic_auth_header("admin", "secret")
            )
            assert status == 200
        finally:
            r2h.stop()
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
python3 -m pytest e2e/test_pages.py::TestWebBasicAuth -v 2>&1 | tail -40
```

Expected: `web-auth-user`/`web-auth-password` are unrecognized config keys (the daemon logs a warning and ignores them, per `parse_global_sec`'s pattern for unknown keys — verify this by reading the end of `parse_global_sec` for its "unknown key" fallback behavior) and no 401 is ever returned — `test_non_local_client_requires_auth` and the setting/player test FAIL because `/status` returns 200 instead of 401 without credentials.

- [ ] **Step 3: Add `http_send_401_basic()` to `src/http.h`/`src/http.c`**

The existing `http_send_401()` hardcodes `WWW-Authenticate: Bearer` for the r2h-token flow — add a sibling function rather than parameterizing it (keeps both call sites simple and avoids changing the existing signature/callers).

`src/http.h`, after the `http_send_401` declaration:

```c
/**
 * Send HTTP 401 Unauthorized response with a Basic auth challenge
 * @param conn Connection object
 */
void http_send_401_basic(connection_t *conn);
```

`src/http.c`, after `http_send_401()`:

```c
void http_send_401_basic(connection_t *conn) {
  static const char body[] = "<!doctype html><title>401</title>Unauthorized";

  send_http_headers(conn, STATUS_401, "text/html; charset=utf-8", "WWW-Authenticate: Basic realm=\"rtp2httpd\"\r\n");
  connection_queue_output_and_flush(conn, (const uint8_t *)body, sizeof(body) - 1);
}
```

- [ ] **Step 4: Reorder route-prefix computation in `connection_route_and_start()`**

Read `src/connection.c` lines 899-1049 first to confirm line numbers match (they may have shifted slightly from earlier tasks' edits). The goal: `player_route`/`player_route_len`, `setting_route`/`setting_route_len`, `setting_api_prefix`/`setting_api_prefix_len`, `status_sse_route_len`, `status_api_prefix_len` must all be computed *before* the r2h-token check (so the new Basic Auth check, inserted right after it, can use them), instead of being computed inline at each dispatch site further down.

Replace the block from the `status_route`/`status_sse_route`/`status_api_prefix` declarations (currently ending around line 924) through the assets check and r2h-token check (currently ending around line 948) with:

```c
  /* status_route/status_sse_route/status_api_prefix/player_route/setting_route
   * /setting_api_prefix are all computed here (not lazily at each dispatch
   * site) so the web-auth scope check below can test against them too. */
  const char *status_route = config.status_page_route ? config.status_page_route : "status";
  size_t status_route_len = strlen(status_route);
  char status_sse_route[HTTP_URL_BUFFER_SIZE];
  char status_api_prefix[HTTP_URL_BUFFER_SIZE];

  if (status_route_len > 0) {
    snprintf(status_sse_route, sizeof(status_sse_route), "%s/sse", status_route);
    snprintf(status_api_prefix, sizeof(status_api_prefix), "%s/api/", status_route);
  } else {
    strncpy(status_sse_route, "sse", sizeof(status_sse_route) - 1);
    status_sse_route[sizeof(status_sse_route) - 1] = '\0';
    strncpy(status_api_prefix, "api/", sizeof(status_api_prefix) - 1);
    status_api_prefix[sizeof(status_api_prefix) - 1] = '\0';
  }
  size_t status_sse_route_len = strlen(status_sse_route);
  size_t status_api_prefix_len = strlen(status_api_prefix);

  const char *player_route = config.player_page_route ? config.player_page_route : "player";
  size_t player_route_len = strlen(player_route);

  const char *setting_route = config.setting_page_route ? config.setting_page_route : "setting";
  size_t setting_route_len = strlen(setting_route);

  char setting_api_prefix[HTTP_URL_BUFFER_SIZE];
  if (setting_route_len > 0) {
    snprintf(setting_api_prefix, sizeof(setting_api_prefix), "%s/api/", setting_route);
  } else {
    strncpy(setting_api_prefix, "api/", sizeof(setting_api_prefix) - 1);
    setting_api_prefix[sizeof(setting_api_prefix) - 1] = '\0';
  }
  size_t setting_api_prefix_len = strlen(setting_api_prefix);

  /* Handle static assets first (bypass r2h-token validation for /assets/) */
  const char *assets_prefix = "assets/";
  size_t assets_prefix_len = strlen(assets_prefix);
  if (path_len >= assets_prefix_len && strncmp(service_path, assets_prefix, assets_prefix_len) == 0) {
    /* Reconstruct full path with leading slash */
    char asset_path[HTTP_URL_BUFFER_SIZE];
    snprintf(asset_path, sizeof(asset_path), "/%.*s", (int)path_len, service_path);
    handle_embedded_file(c, asset_path);
    return 0;
  }

  /* Check r2h-token if configured (supports URL query, Cookie, User-Agent).
   * Applies to all remaining routes -- pages AND streams/services alike. */
  if (config.r2h_token != NULL && config.r2h_token[0] != '\0') {
    const char *raw_query_start = strchr(c->http_req.url, '?');
    token_source_t source = validate_r2h_token(c, query_start, raw_query_start);
    if (source == TOKEN_SOURCE_NONE) {
      http_send_401(c);
      return 0;
    }
    /* Set cookie only when token was provided via URL query (first visit) */
    c->should_set_r2h_cookie = (source == TOKEN_SOURCE_QUERY);
  }

  /* HTTP Basic Auth for /status, /player, /setting (and their APIs/SSE),
   * enforced only for non-local clients. Independent of r2h-token -- both
   * may be configured and both must then pass. */
  if (config.web_auth_user != NULL && config.web_auth_user[0] != '\0' && config.web_auth_password != NULL &&
      config.web_auth_password[0] != '\0') {
    int in_web_auth_scope =
        (status_route_len == path_len && strncmp(service_path, status_route, path_len) == 0) ||
        (status_sse_route_len == path_len && strncmp(service_path, status_sse_route, path_len) == 0) ||
        (path_len >= status_api_prefix_len && strncmp(service_path, status_api_prefix, status_api_prefix_len) == 0) ||
        (player_route_len == path_len && strncmp(service_path, player_route, path_len) == 0) ||
        (setting_route_len == path_len && strncmp(service_path, setting_route, path_len) == 0) ||
        (path_len >= setting_api_prefix_len && strncmp(service_path, setting_api_prefix, setting_api_prefix_len) == 0);

    int web_auth_local_exempt = !config.web_auth_require_local && is_client_local(c);
    if (in_web_auth_scope && !web_auth_local_exempt && !is_web_auth_valid(c)) {
      http_send_401_basic(c);
      return 0;
    }
  }
```

Then remove the now-duplicate declarations further down at each dispatch site (they'd otherwise shadow/redeclare the same names and fail to compile):
- At the player page dispatch (originally around line 956-957): delete the `const char *player_route = ...;` and `size_t player_route_len = ...;` lines, keep only the `if (player_route_len == path_len && ...)` check.
- At the setting page dispatch (originally around line 964-965): delete the `const char *setting_route = ...;` / `size_t setting_route_len = ...;` lines, keep only the `if` check.
- At the setting API dispatch (originally around line 971-978): delete the `char setting_api_prefix[...]` declaration and its `snprintf`/`strncpy` fill block and the `size_t setting_api_prefix_len = ...;` line, keep only the `if (path_len >= setting_api_prefix_len && ...)` check onward.
- At the status SSE dispatch (originally around line 1016): delete `size_t status_sse_len = strlen(status_sse_route);` and change the `if` check to use `status_sse_route_len` instead of `status_sse_len`.
- At the status API dispatch (originally around line 1021): delete `size_t status_api_prefix_len = strlen(status_api_prefix);` (now a duplicate declaration — the one computed earlier is reused as-is, same name).

- [ ] **Step 5: Add `is_client_local()` and `is_web_auth_valid()` helpers**

Add these as static functions in `src/connection.c`, near the top alongside `connection_client_is_tcp()` (after the `token_source_t` enum, before `connection_client_is_tcp`):

```c
static int ipv4_addr_in_cidr(uint32_t addr_host_order, uint32_t base, int prefix_len) {
  uint32_t mask = (prefix_len == 0) ? 0 : (~0U << (32 - prefix_len));
  return (addr_host_order & mask) == (base & mask);
}

/* RFC1918 + loopback + link-local -- private ranges we treat as "local". */
static int ipv4_is_local(uint32_t addr_host_order) {
  return ipv4_addr_in_cidr(addr_host_order, 0x7F000000, 8) ||  /* 127.0.0.0/8 */
         ipv4_addr_in_cidr(addr_host_order, 0x0A000000, 8) ||  /* 10.0.0.0/8 */
         ipv4_addr_in_cidr(addr_host_order, 0xAC100000, 12) || /* 172.16.0.0/12 */
         ipv4_addr_in_cidr(addr_host_order, 0xC0A80000, 16) || /* 192.168.0.0/16 */
         ipv4_addr_in_cidr(addr_host_order, 0xA9FE0000, 16);   /* 169.254.0.0/16 */
}

static int ipv6_is_local(const struct in6_addr *addr) {
  if (IN6_IS_ADDR_LOOPBACK(addr) || IN6_IS_ADDR_LINKLOCAL(addr))
    return 1;
  if ((addr->s6_addr[0] & 0xFE) == 0xFC) /* fc00::/7 unique-local */
    return 1;
  if (IN6_IS_ADDR_V4MAPPED(addr)) {
    uint32_t v4 = ntohl(*(const uint32_t *)&addr->s6_addr[12]);
    return ipv4_is_local(v4);
  }
  return 0;
}

/* Parses an IP literal (no port) and classifies it as local/private.
 * Any parse failure is treated as NOT local (fail closed). */
static int address_string_is_local(const char *ip_str) {
  struct in_addr v4;
  struct in6_addr v6;

  if (inet_pton(AF_INET, ip_str, &v4) == 1)
    return ipv4_is_local(ntohl(v4.s_addr));
  if (inet_pton(AF_INET6, ip_str, &v6) == 1)
    return ipv6_is_local(&v6);
  return 0;
}

/* Determines whether the request's effective client address is local/private.
 * When xff is enabled and X-Forwarded-For is present, trusts that address
 * (same trust boundary already applied to XFF elsewhere in this file);
 * otherwise uses the raw socket peer address. Unix-domain-socket clients are
 * always local. */
static int is_client_local(connection_t *c) {
  if (!c)
    return 1;
  if (c->client_addr_len > 0 && c->client_addr.ss_family == AF_UNIX)
    return 1;

  if (config.xff && c->http_req.x_forwarded_for[0] != '\0') {
    return address_string_is_local(c->http_req.x_forwarded_for);
  }

  if (c->client_addr.ss_family == AF_INET) {
    const struct sockaddr_in *sin = (const struct sockaddr_in *)&c->client_addr;
    return ipv4_is_local(ntohl(sin->sin_addr.s_addr));
  }
  if (c->client_addr.ss_family == AF_INET6) {
    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)&c->client_addr;
    return ipv6_is_local(&sin6->sin6_addr);
  }
  return 0; /* unknown family: fail closed (not local) */
}

/* Validates the Authorization header against config.web_auth_user/password. */
static int is_web_auth_valid(connection_t *c) {
  static const char prefix[] = "Basic ";
  const char *auth = c->http_req.authorization;

  if (strncmp(auth, prefix, sizeof(prefix) - 1) != 0)
    return 0;

  char decoded[256];
  if (base64_decode(auth + sizeof(prefix) - 1, decoded, sizeof(decoded)) < 0)
    return 0;

  char *colon = strchr(decoded, ':');
  if (!colon)
    return 0;
  *colon = '\0';

  return strcmp(decoded, config.web_auth_user) == 0 && strcmp(colon + 1, config.web_auth_password) == 0;
}
```

Add the required includes at the top of `src/connection.c` (alongside the existing `#include <sys/socket.h>`):

```c
#include <arpa/inet.h>
#include <netinet/in.h>
```

- [ ] **Step 6: Build**

```bash
cmake --build build -j
```

Expected: builds successfully. Fix any duplicate-declaration compile errors by re-checking Step 4's removals were applied at every dispatch site.

- [ ] **Step 7: Run the e2e test to confirm it passes**

```bash
python3 -m pytest e2e/test_pages.py::TestWebBasicAuth -v
```

Expected: all 5 tests in `TestWebBasicAuth` PASS.

- [ ] **Step 8: Run the full `test_pages.py` file to confirm no regressions**

```bash
python3 -m pytest e2e/test_pages.py -v
```

Expected: all tests PASS (including the pre-existing `TestAppPathPrefix` class).

- [ ] **Step 9: Commit**

```bash
git add src/connection.c src/http.h src/http.c e2e/test_pages.py
git commit -m "feat(connection): add HTTP Basic Auth for non-local status/player/setting access"
```

---

### Task 5: Expose fields on the `/setting` page (backend)

**Files:**
- Modify: `src/settings_api.c`

**Interfaces:**
- Consumes: `config.web_auth_user`/`config.web_auth_password`/`config.web_auth_require_local` (Task 2).
- Produces: nothing new — `SETTING_FIELDS` is consumed generically by the existing `handle_get_config`/`handle_save_config`.

- [ ] **Step 1: Add all three fields to `SETTING_FIELDS`**

In `src/settings_api.c`, after the `{"r2h-token", ...}` entry:

```c
    {"r2h-token", FT_STRING, offsetof(config_t, r2h_token)},
    {"web-auth-user", FT_STRING, offsetof(config_t, web_auth_user)},
    {"web-auth-password", FT_STRING, offsetof(config_t, web_auth_password)},
    {"web-auth-require-local", FT_BOOL, offsetof(config_t, web_auth_require_local)},
```

- [ ] **Step 2: Build**

```bash
cmake --build build -j
```

- [ ] **Step 3: Write and run an e2e test for get-config/save-config round-trip**

Add to `e2e/test_config.py` (check the file's existing imports/fixtures first — follow the pattern of other `get-config`/`save-config` round-trip tests already in that file):

```python
def test_web_auth_fields_round_trip(r2h_binary):
    port = find_free_port()
    r2h = R2HProcess(r2h_binary, port, extra_args=["-v", "4"])
    try:
        r2h.start()
        status, _, body = http_request(
            "127.0.0.1",
            port,
            "POST",
            "/setting/api/save-config",
            body=b"web-auth-user=admin&web-auth-password=secret&web-auth-require-local=1",
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        assert status == 200

        status, _, body = http_get("127.0.0.1", port, "/setting/api/get-config")
        assert status == 200
        data = json.loads(body)
        assert data["web-auth-user"] == "admin"
        assert data["web-auth-password"] == "secret"
        assert data["web-auth-require-local"] is True
    finally:
        r2h.stop()
```

Run it:

```bash
python3 -m pytest e2e/test_config.py::test_web_auth_fields_round_trip -v
```

Expected: PASS (the table-driven `handle_get_config`/`handle_save_config` need no code changes beyond the `SETTING_FIELDS` entries, per the existing `r2h-token`/`xff` precedents — check how `handle_get_config` serializes `FT_BOOL` fields to confirm whether it's a JSON boolean or `0`/`1`, and adjust the assertion to match the existing convention rather than assuming).

- [ ] **Step 4: Commit**

```bash
git add src/settings_api.c e2e/test_config.py
git commit -m "feat(settings-api): expose web-auth-user/password/require-local"
```

---

### Task 6: Expose fields on the `/setting` page (frontend)

**Files:**
- Modify: `web-ui/src/lib/setting-fields.ts`
- Modify: `web-ui/src/i18n/setting.ts`

**Interfaces:**
- Consumes: the `web-auth-user`/`web-auth-password`/`web-auth-require-local` keys now returned by `get-config` (Task 5).
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Add all three fields to `SETTING_FIELDS` in `web-ui/src/lib/setting-fields.ts`**

After the `{ key: "r2h-token", ... }` entry:

```ts
  { key: "r2h-token", tab: "advanced", type: "text", labelKey: "r2hToken" },
  { key: "web-auth-user", tab: "advanced", type: "text", labelKey: "webAuthUser" },
  { key: "web-auth-password", tab: "advanced", type: "text", labelKey: "webAuthPassword" },
  { key: "web-auth-require-local", tab: "advanced", type: "checkbox", labelKey: "webAuthRequireLocal" },
```

- [ ] **Step 2: Add i18n labels to `web-ui/src/i18n/setting.ts`**

In the `base` (English) dict, after `r2hToken: "Access Token",`:

```ts
  r2hToken: "Access Token",
  webAuthUser: "Web Auth Username",
  webAuthPassword: "Web Auth Password",
  webAuthRequireLocal: "Require Auth for Local Network",
```

In the Simplified Chinese dict, after `r2hToken: "访问令牌",`:

```ts
  r2hToken: "访问令牌",
  webAuthUser: "网页认证用户名",
  webAuthPassword: "网页认证密码",
  webAuthRequireLocal: "本地网络也需要认证",
```

In the Traditional Chinese dict, after `r2hToken: "存取權杖",`:

```ts
  r2hToken: "存取權杖",
  webAuthUser: "網頁認證使用者名稱",
  webAuthPassword: "網頁認證密碼",
  webAuthRequireLocal: "本機網路也需要認證",
```

- [ ] **Step 3: Rebuild the web UI and verify the fields render**

Follow the `build-run` skill's documented command for rebuilding the embedded web UI (do not hand-edit `src/embedded_web_data.h`):

```bash
pnpm run web-ui:build
```

Then start the daemon and check the Advanced tab in a browser:

```bash
cmake --build build -j
./build/rtp2httpd -v 4 -l 15140 &
sleep 1
curl -s http://127.0.0.1:15140/setting | grep -o '<title>[^<]*</title>'
kill %1
```

Expected: the daemon starts and serves the setting page (full visual verification of the new fields can be done manually in a browser at `http://127.0.0.1:15140/setting`, Advanced tab).

- [ ] **Step 4: Do NOT commit `src/embedded_web_data.h` unless explicitly requested**

Per `CLAUDE.md`: "If `src/embedded_web_data.h` changes from a Web UI rebuild, do not commit it unless explicitly requested." Check `git status` — if `src/embedded_web_data.h` shows as modified, leave it unstaged/unless the user asks to include it (it will be regenerated in CI on release anyway).

- [ ] **Step 5: Commit the source changes only**

```bash
git add web-ui/src/lib/setting-fields.ts web-ui/src/i18n/setting.ts
git commit -m "feat(web-ui): add web-auth-user/password fields to settings page"
```

---

### Task 7: Docs update

**Files:**
- Modify: `docs/guide/installation.md`
- Modify: `docs/en/guide/installation.md`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing.

- [ ] **Step 1: Find the existing `/setting`-exposure security note**

```bash
grep -n "r2h-token\|ffmpeg-args\|RCE" docs/guide/installation.md docs/en/guide/installation.md
```

- [ ] **Step 2: Extend the Chinese doc**

Read the surrounding paragraph in `docs/guide/installation.md` first, then add a sentence recommending `web-auth-user`/`web-auth-password` as a second, browser-native option alongside `r2h-token` for internet-facing deployments — matching the existing tone/format of that section. Mention that local/LAN clients bypass this prompt by default, and that `web-auth-require-local` can be enabled (from the same `/setting` page) to require it everywhere, including the LAN, for deployments that want it always-on.

- [ ] **Step 3: Translate the change to English**

Per `CLAUDE.md`, English docs are translations of the Chinese source — use the `translate-docs-zh-en` skill to propagate the Step 2 change into `docs/en/guide/installation.md` rather than hand-translating.

- [ ] **Step 4: Commit**

```bash
git add docs/guide/installation.md docs/en/guide/installation.md
git commit -m "docs: recommend web-auth-user/password for internet-facing /setting"
```

---

### Task 8: Full verification, branch, PR, merge, release v1.0.1

**Files:** none (process task).

- [ ] **Step 1: Full e2e run**

```bash
python3 -m pytest e2e/ -v 2>&1 | tail -60
```

Expected: all tests pass (or match the pre-existing pass/fail baseline from before this feature — no new failures).

- [ ] **Step 2: Full build from clean**

```bash
rm -rf build && cmake -B build && cmake --build build -j
```

Expected: clean build, no errors.

- [ ] **Step 3: Sync the fork with upstream before pushing**

Per this project's established release workflow, sync the fork with `stackia/rtp2httpd` before opening the PR (avoids a stale-fork surprise at merge time):

```bash
gh repo set-default hungngit2/rtp2httpd
gh repo sync hungngit2/rtp2httpd
```

- [ ] **Step 4: Push the branch and open a PR**

This work should already be on a feature branch (created before Task 1 per `superpowers:using-git-worktrees`/standard git flow — if not, create one now: `git checkout -b feat/web-basic-auth main`).

```bash
git push -u origin feat/web-basic-auth
gh pr create --title "feat: HTTP Basic Auth for status/player/setting" --body "$(cat <<'EOF'
## Summary
- Adds optional `web-auth-user`/`web-auth-password` config to require HTTP Basic Auth on `/status`, `/player`, `/setting` (and their APIs/SSE) for non-local clients only
- LAN/private-network clients are unaffected by default; `web-auth-require-local` can force auth everywhere, including LAN
- Independent of the existing `r2h-token` mechanism

## Test plan
- [x] e2e: local client bypasses auth
- [x] e2e: non-local client challenged, correct/incorrect credentials
- [x] e2e: stream routes unaffected
- [x] e2e: web-auth-require-local forces auth for loopback
- [x] Full e2e suite passes
EOF
)"
```

- [ ] **Step 5: Wait for CI, fix any failures**

```bash
gh pr checks <PR_NUMBER> --watch
```

If any check fails, diagnose via `gh run view <RUN_ID> --log-failed` (as done for PR #8's ruff-format and clang-format failures) and push fixes as new commits.

- [ ] **Step 6: Merge to main**

```bash
gh pr merge <PR_NUMBER> --squash --delete-branch
```

- [ ] **Step 7: Sync local main**

```bash
git checkout main
git pull origin main
```

- [ ] **Step 8: Re-cut and publish the v1.0.1 release**

Follow the same release flow used for v1.0.0 (tag at the merge commit, write release notes covering this feature, `gh release create`):

```bash
git tag -a v1.0.1 -m "v1.0.1" $(git rev-parse HEAD)
git push origin v1.0.1
gh release create v1.0.1 --title "v1.0.1" --notes "$(cat <<'EOF'
## New Features

- Added optional HTTP Basic Auth (`web-auth-user`/`web-auth-password`, configurable from the `/setting` page) protecting `/status`, `/player`, and `/setting` when accessed from outside your local network — LAN access remains password-free by default, or can be required everywhere via `web-auth-require-local`
EOF
)"
```

- [ ] **Step 9: Confirm the release**

```bash
gh release view v1.0.1
```

Expected: release exists, points at the correct commit, and CI (if any release-triggered workflow exists) is green.
