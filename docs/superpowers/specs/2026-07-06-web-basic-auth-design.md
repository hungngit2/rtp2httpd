# HTTP Basic Auth for /status, /player, /setting

## Problem

`/status`, `/player`, and `/setting` are reachable by anyone who can reach the
daemon's HTTP port. `r2h-token` already protects all routes (including
streams) when configured, but it's a URL/cookie/UA token, not something a
browser prompts for — awkward for these three admin/browser-facing pages,
especially `/setting`, which can change `ffmpeg-args` (RCE risk) and other
sensitive config.

## Goal

Add an independent, optional HTTP Basic Auth layer for these three pages and
their supporting APIs/SSE endpoints, enforced only for clients outside the
local network. LAN clients (including reverse-proxy setups where the real
client is identified via a trusted `X-Forwarded-For`) never see a password
prompt; only clients determined to be non-local are challenged.

## Non-goals

- Multi-user accounts, password hashing/storage beyond plaintext in the
  config file (matches the existing `r2h-token` model).
- Protecting stream/service routes (`/rtp/`, `/rtsp/`, `/http/`, `/udp/`,
  `/playlist.m3u`, `/epg.xml`) — out of scope; `r2h-token` already covers
  those if needed.
- Precise same-subnet-as-server detection — private/loopback IP range
  checking is sufficient.
- Rate limiting / brute-force protection on the Basic Auth prompt.

## Design

### Config

Two new optional string fields in `[global]`, following the existing
`r2h-token` pattern:

- `web-auth-user`
- `web-auth-password`

Enforcement is active only when **both** are non-empty. Both are added to
`SETTING_FIELDS` in `src/settings_api.c` so they're editable from the
`/setting` page (advanced tab, next to `r2h-token`), and to
`web-ui/src/lib/setting-fields.ts` / `web-ui/src/i18n/setting.ts` (en, zh,
zh-TW) on the frontend.

### Local-network detection

New `is_client_local(connection_t *c)` helper in `src/connection.c`:

1. Determine the "effective" client address:
   - If `config.xff` is enabled and `c->http_req.x_forwarded_for` is
     non-empty, parse and use the first address in that header (same trust
     boundary the codebase already applies to XFF elsewhere).
   - Otherwise use the raw socket peer address (`c->client_addr`).
   - A Unix-domain-socket client (`c->client_addr.ss_family == AF_UNIX`)
     always counts as local.
2. Return true if that address falls in:
   - IPv4 loopback (127.0.0.0/8) or IPv6 loopback (::1)
   - RFC1918 (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16)
   - IPv6 unique-local (fc00::/7)
   - Link-local (169.254.0.0/16, fe80::/10)
3. Any parse failure (malformed XFF value, etc.) is treated as **not
   local** (fail closed — a request that can't be proven local should
   still be challenged when Basic Auth is configured).

### Enforcement point

In `connection_route_and_start()` (`src/connection.c`), after the existing
r2h-token check block, add a Basic Auth check gated to the routes in scope:

- Page routes: `status_route`, `player_route`, `setting_route` (the same
  route-matching already computed for the existing page dispatch)
- Their APIs/SSE: `status_sse_route`, `status_api_prefix`,
  `setting_api_prefix` + `get-config`/`save-config`

This check runs independently of the r2h-token check — both may be
configured simultaneously, and a request must satisfy both when both are
set. It does not affect stream/service routes at all.

Logic, when `config.web_auth_user`/`config.web_auth_password` are both set:

1. If `is_client_local(c)` → skip, no auth required.
2. Else, look for `c->http_req.authorization` matching
   `Basic <base64(user:pass)>` with the configured credentials.
3. Missing header, malformed header, or mismatched credentials → send 401
   with `WWW-Authenticate: Basic realm="rtp2httpd"` and stop routing.

### New pieces

- `src/http.h`: add `char authorization[512];` to `http_request_t`.
- `src/http.c`: populate `authorization` in the existing header-parsing
  loop (mirrors how `cookie`/`user_agent` are captured today). Not
  included in the r2h-token UA/cookie stripping logic since it's unrelated.
- `src/http.c`: add `void http_send_401_basic(connection_t *conn)` (or a
  parameterized `www_authenticate` value passed to the existing
  `http_send_401`) since the current `http_send_401` hardcodes
  `WWW-Authenticate: Bearer` for the r2h-token flow.
- `src/utils.c`/`src/utils.h`: add a small `base64_decode()` helper
  (the existing `rtsp_base64_encode` in `src/rtsp.c` is encode-only and
  file-static, so this is new, shared code since both `connection.c` needs
  it for parsing incoming Basic Auth headers).
- `src/connection.c`: `is_client_local()` + the Basic Auth enforcement
  block described above.
- `src/configuration.h`/`.c`: `web_auth_user`, `web_auth_password` fields
  (default `NULL`), CLI/config-file parsing following the existing
  string-field pattern (e.g. `r2h_token`).
- `src/settings_api.c`: add both fields to `SETTING_FIELDS`.
- Frontend: `web-ui/src/lib/setting-fields.ts` (`advanced` tab, password
  field masked like a password input) and `web-ui/src/i18n/setting.ts`
  (en/zh/zh-TW labels).
- Docs: extend the existing `/setting`-exposure security note in
  `docs/guide/installation.md` / `docs/en/guide/installation.md` to
  recommend `web-auth-user`/`web-auth-password` for internet-facing
  deployments, alongside `r2h-token`.

### Testing

`e2e/test_pages.py`:

- Local (loopback) client reaches `/status`, `/player`, `/setting`,
  `status/sse`, `setting/api/get-config` without any `Authorization`
  header even when `web-auth-user`/`web-auth-password` are configured.
- With `xff = 1` and a non-local `X-Forwarded-For` (e.g. `8.8.8.8`):
  - No `Authorization` header → 401 with `WWW-Authenticate: Basic`.
  - Correct `Authorization: Basic <base64>` → 200.
  - Wrong credentials → 401.
- Stream routes remain reachable from a simulated non-local client without
  Basic Auth (confirms scope is pages/APIs only).
- `r2h-token` and Basic Auth configured together: a non-local request needs
  both to succeed.

## Open questions

None — all resolved during brainstorming.
