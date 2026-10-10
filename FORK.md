# Fork features (hungngit2/rtp2httpd)

This fork tracks [stackia/rtp2httpd](https://github.com/stackia/rtp2httpd) and adds three web features. Their code lives in fork-only files; upstream files carry only the hook lines below, so an upstream sync should touch nothing else.

## Features

| Feature | Config (`[global]`) | Behaviour |
|---|---|---|
| Settings page | `setting-page-path` (default `/setting`) | Web UI to edit `[global]` and `[bind]`; saving rewrites the config file and reloads (SIGHUP). API: `<setting-path>/api/get-config`, `<setting-path>/api/save-config`. |
| Web UI Basic Auth | `web-auth-user`, `web-auth-password`, `web-auth-require-local` (default off) | Status, player and settings pages (and their APIs/SSE) require HTTP Basic Auth from non-local clients. Streams, playlists, assets and manifests are not gated. With `xff` on, only the rightmost `X-Forwarded-For` hop (appended by your proxy) decides "local", so a forged leftmost entry cannot bypass auth. |
| Bare paths with `app-path-prefix` | — | Every route works both with and without the prefix, so IPTV apps with hardcoded bare stream URLs keep working behind a reverse proxy. |

These keys are config-file only (no CLI flags).

The settings page can change `ffmpeg-path`, `ffmpeg-args` and listen addresses. Anyone who can open it can run commands as the rtp2httpd user, so set `web-auth-user`/`web-auth-password`, and set `web-auth-require-local = true` if your LAN is not fully trusted.

## Fork-only files

- `src/fork_web.c`, `src/fork_web.h`: fork config keys, header capture, local-client check, Basic Auth, `/setting` routing
- `src/settings_api.c`, `src/settings_api.h`: get/save config API and the config-file rewriter
- `web-ui/setting.html`, `web-ui/src/pages/setting.tsx`, `web-ui/src/components/setting/`, `web-ui/src/hooks/use-setting-*.ts`, `web-ui/src/lib/setting-fields.ts`, `web-ui/src/i18n/setting.ts`
- `e2e/test_fork_features.py`
- `scripts/install-armbian.sh`, `scripts/uninstall-armbian.sh`

## Hook lines in upstream files

Keep these when resolving sync conflicts:

| File | Hook |
|---|---|
| `src/http.h` | `#include "fork_web.h"` and `fork_http_req_t fork;` in `http_request_t` |
| `src/http.c` | `fork_http_consume_header(&req->fork, header);` at the top of `http_request_consume_header` |
| `src/configuration.c` | `#include "fork_web.h"`; `fork_config_init();` first in `config_init`; `if (fork_config_parse_global(param, value)) return;` in `parse_global_sec`; `fork_config_snapshot();` first in `config_snapshot`; `fork_config_restore_snapshot();` in `config_restore_snapshot` |
| `src/connection.c` | `#include "fork_web.h"`; `if (strip_app_path_prefix(...) == 0) url = internal_url_buf;` instead of the 404; `service_create_from_udpxy_url(url)`; `if (fork_web_route(c, service_path, path_len)) return 0;` before the status-page dispatch |
| `CMakeLists.txt` | `src/fork_web.c` and `src/settings_api.c` in `COMMON_SOURCES` |
| `web-ui/vite.config.ts` | `setting` entry in `rollupOptions.input` |
| `e2e/test_config.py`, `e2e/test_pages.py` | bare-path assertions expect 200, not 404 (marked `# Fork:`) |

## Syncing with upstream

```bash
git fetch upstream
git merge upstream/main
# Conflicts in the hook files: keep upstream's version plus the hook lines above.
# Makefile.versioned: keep ours. embedded_web_data.h: take either, then rebuild.
pnpm run web-ui:build
cmake --build build -j
./scripts/run-e2e.sh test_fork_features.py
./scripts/run-e2e.sh test_pages.py
./scripts/run-e2e.sh test_config.py
```

`test_fork_features.py` fails if a sync drops a hook, and the fork's E2E workflow runs it on every push to `main`.
