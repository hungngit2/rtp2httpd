# Web Settings Page (`/setting`)

## Problem

`rtp2httpd` has two built-in web pages baked into the binary: `/status` (live monitoring) and `/player` (built-in player). On OpenWrt, global configuration is edited through the `luci-app-rtp2httpd` LuCI panel. On every other platform (Armbian/Debian, Docker, static binary), there is no GUI at all — configuration means SSH-ing in and hand-editing `/etc/rtp2httpd.conf`.

This adds a third built-in page, `/setting`, that exposes the same global settings LuCI does, as a structured form, usable on any platform.

## Scope

**In scope:** the `[global]` section options that LuCI's `rtp2httpd.js` exposes (see field list below), presented as a tabbed form, backed by new HTTP endpoints to read and persist `rtp2httpd.conf`.

**Out of scope (explicit, by decision):**
- No raw config-file textarea fallback. Only fields listed below are editable through `/setting`.
- No editing of the `[services]` section (inline M3U playlist content). Users who need inline M3U service definitions must continue to edit the config file by hand; `external-m3u` (a URL/file pointer) is covered by the form and is the recommended path.
- No OpenWrt/procd-only fields (`disabled`, `respawn`) — those don't apply outside OpenWrt's init system.
- No multi-instance / per-service configuration — this mirrors LuCI's own scope, which is global settings only.

## Access & Security

`/setting` is dispatched through the same route table as `/status` and `/player`, so the existing global `r2h-token` check (if configured) protects it automatically — no new auth mechanism needed. Because this page can change `ffmpeg-args`/`ffmpeg-path`, `cors-allow-origin`, `hostname`, and `r2h-token` itself, the docs should note that operators who expose `rtp2httpd` publicly must set `r2h-token` (already documented as a general recommendation).

## Backend

### New config field
`config.setting_page_path` / `config.setting_page_route`, following the exact existing pattern of `status_page_path`/`player_page_path` in `configuration.c` (including CLI flag `--setting-page-path` and config key `setting-page-path`). Default: `/setting`.

### New route wiring (`connection.c`)
Add a third branch alongside the existing `status_route`/`player_route` checks that serves the embedded `/setting.html` when the request path matches `setting_page_route`.

### New API endpoints (under the existing `status/api/` prefix, reusing its dispatch table)
- `GET status/api/get-config` — returns the current effective config as JSON (one key per form field below, using the same names as the config-file keys, e.g. `"maxclients": 5`). Values come from the live `config` global, not by re-reading the file, so the form always reflects what's actually running.
- `POST status/api/save-config` — body is a JSON object with the same shape. The handler:
  1. Validates each field (type/range — mirroring the `datatype` constraints already defined in `rtp2httpd.js`, e.g. `maxclients` 1–5000, `workers` 1–64).
  2. Re-renders `rtp2httpd.conf` by rewriting only the managed keys in the `[global]` section (and the `[bind]` section for `listen`), preserving comments, ordering, and the `[services]` section byte-for-byte. This avoids clobbering hand-written M3U content or comments when the form's `save` doesn't touch them.
  3. Writes the file atomically (write to `<path>.tmp`, `fsync`, `rename`).
  4. Sends `SIGHUP` to the supervisor (same mechanism `handle_reload_config` already uses) to apply the change immediately.
  5. Responds `{"success":true}` or `{"success":false,"error":"..."}` (HTTP 400 on validation failure, matching the existing handlers' style).

### Listen addresses
The `[bind]` section's lines (`node port` or a bare `/path` for Unix sockets) are represented in the API as a `"listen"` array of strings, using the same canonical forms LuCI already validates: bare port (`5140`), `host:port`, `[ipv6]:port`, or an absolute Unix socket path. Rewriting `[bind]` on save replaces its entire contents with the submitted list (defaulting to `* 5140` if empty, matching the shipped default config).

## Frontend

### New page: `web-ui/src/pages/setting.tsx`, embedded as `setting.html` (same build pipeline as `status.tsx`/`player.tsx` → `embedded_web_data.h`).

### Tabs (mirroring `rtp2httpd.js` 1:1, so anyone familiar with the LuCI panel feels at home)
1. **Basic** — Listen Addresses (dynamic list), Logging level (select: Fatal/Error/Warn/Info/Debug)
2. **Network & Performance** — Upstream Interface (simple text input; no `DeviceSelect` widget since that's a LuCI/UCI-specific device picker with no equivalent outside OpenWrt) or per-stream-type interface overrides, Max clients, Workers, Buffer Pool Max Size, UDP Receive Buffer Size, Multicast Rejoin Interval, FCC Listen Port Range, Zero-Copy on Send, RTSP STUN Server
3. **Player & M3U** — External M3U, External M3U Update Interval, Player Page Path, a link/button to open `/player` (disabled with a hint if External M3U is empty, same UX as LuCI)
4. **Monitoring & Advanced** — button to open `/status`, Status Page Path, App Path Prefix, Use Relative Paths in M3U, Hostname, R2H Token, CORS Allow Origin, X-Forwarded-For, Access Log Path, Access Log Format, HTTP Proxy User-Agent, RTSP User-Agent, Video Snapshot (+ FFmpeg Path / FFmpeg Args, shown only when Video Snapshot is on)

### Behavior
- On load: `GET status/api/get-config`, populate the form.
- On save: client-side validation (mirroring the ranges/formats above) → `POST status/api/save-config` → on success, show a toast/banner ("Settings saved and reloaded") and re-fetch to confirm; on failure, show the returned error inline.
- Styling/i18n follows the existing `status.tsx`/`player.tsx` conventions (Tailwind, the project's existing i18n hook pattern) rather than introducing a new UI paradigm.

## Testing
- e2e coverage (per the project's `e2e` skill conventions): start `R2HProcess` with a temp config, hit `GET status/api/get-config` and assert it matches the loaded config; `POST status/api/save-config` with a changed value, assert the file on disk reflects it, assert a subsequent `GET` (after reload) reflects the new value, assert unrelated `[services]` content is untouched.
- Manual verification: load `/setting` in a browser against `devlab`, change a couple of fields per tab, confirm they persist across a `save` + page reload, and confirm `/status`/`/player` still work with the new page installed alongside them.

## Known limitations (accepted)
- Editing while `[services]` contains hand-written M3U content is safe (untouched) but that content itself isn't visible/editable from `/setting`.
- Concurrent edits (two browser tabs saving at once) use last-write-wins; no optimistic locking. Acceptable given this is a single-operator admin tool.
