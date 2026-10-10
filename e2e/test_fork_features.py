"""
E2E tests for fork-only features (see FORK.md): the /setting page and its API,
HTTP Basic Auth for the web UI, and bare-path access with app-path-prefix.

Upstream never edits this file, so a sync that drops one of the FORK.md hooks
fails here instead of silently losing the feature.
"""

import base64
import json
import time
from urllib.parse import quote

import pytest
from helpers import MockHTTPUpstream, R2HProcess, find_free_port, get_header, http_get, http_request

APP_PREFIX = "/tv"


def _basic_auth(user: str, password: str) -> dict:
    token = base64.b64encode(f"{user}:{password}".encode()).decode()
    return {"Authorization": f"Basic {token}"}


def _port(r2h: R2HProcess) -> int:
    assert r2h.port is not None
    return r2h.port


def _start(r2h_binary, config: str) -> R2HProcess:
    port = find_free_port()
    r2h = R2HProcess(r2h_binary, port, config_content=config.format(port=port))
    r2h.start()
    return r2h


def _post_settings(port: int, body: bytes, path: str = "/setting/api/save-config", headers: dict | None = None):
    return http_request(
        "127.0.0.1",
        port,
        "POST",
        path,
        headers={"Content-Type": "application/x-www-form-urlencoded", **(headers or {})},
        body=body,
    )


def _wait_for_status(port: int, path: str, expected: int, headers: dict | None = None, timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    status = None
    while time.monotonic() < deadline:
        status, _, _ = http_get("127.0.0.1", port, path, headers=headers, timeout=1.0)
        if status == expected:
            return
        time.sleep(0.1)
    raise AssertionError(f"{path} did not return {expected}; last status was {status}")


@pytest.fixture(scope="module")
def prefixed_r2h(r2h_binary):
    r2h = _start(
        r2h_binary,
        f"""\
[global]
verbosity = 4
app-path-prefix = {APP_PREFIX}

[bind]
* {{port}}

[services]
#EXTM3U
#EXTINF:-1,Prefixed Ch
rtp://239.0.0.1:1234
""",
    )
    yield r2h
    r2h.stop()


class TestSettingPage:
    def test_serves_html(self, r2h_binary):
        r2h = _start(r2h_binary, "[global]\nverbosity = 4\n\n[bind]\n* {port}\n")
        try:
            status, headers, body = http_get("127.0.0.1", _port(r2h), "/setting")
            assert status == 200
            assert "text/html" in get_header(headers, "Content-Type")
            assert b"<title>rtp2httpd Settings</title>" in body
        finally:
            r2h.stop()

    def test_path_is_configurable(self, r2h_binary):
        r2h = _start(r2h_binary, "[global]\nverbosity = 4\nsetting-page-path = /admin\n\n[bind]\n* {port}\n")
        try:
            status, _, _ = http_get("127.0.0.1", _port(r2h), "/admin")
            assert status == 200
            status, _, _ = http_get("127.0.0.1", _port(r2h), "/setting")
            assert status == 404
        finally:
            r2h.stop()

    def test_get_config_reflects_running_config(self, r2h_binary):
        r2h = _start(
            r2h_binary,
            "[global]\nverbosity = 3\nmaxclients = 7\nhostname = example.test\n\n[bind]\n* {port}\n",
        )
        try:
            status, _, body = http_get(
                "127.0.0.1", _port(r2h), "/setting/api/get-config", headers={"Host": "example.test"}
            )
            assert status == 200
            data = json.loads(body)
            assert data["maxclients"] == 7
            assert data["hostname"] == "example.test"
            assert data["verbosity"] == 3
            assert data["listen"] == [str(_port(r2h))]
            assert data["setting-page-path"] == "/setting"
            assert "zerocopy-on-send" not in data
        finally:
            r2h.stop()

    def test_save_updates_file_and_reloads(self, r2h_binary):
        r2h = _start(
            r2h_binary,
            """\
[global]
verbosity = 2
maxclients = 5

[bind]
* {port}

[services]
#EXTM3U
#EXTINF:-1,Test
rtp://239.0.0.1:1234
""",
        )
        try:
            status, _, body = _post_settings(_port(r2h), b"maxclients=42&web-auth-user=admin")
            assert status == 200
            assert json.loads(body)["success"] is True

            assert r2h._config_path is not None
            with open(r2h._config_path) as f:
                saved = f.read()
            assert "maxclients = 42" in saved
            assert "web-auth-user = admin" in saved
            assert "#EXTINF:-1,Test" in saved
            assert "rtp://239.0.0.1:1234" in saved

            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                _, _, body = http_get("127.0.0.1", _port(r2h), "/setting/api/get-config")
                if json.loads(body)["maxclients"] == 42:
                    break
                time.sleep(0.1)
            assert json.loads(body)["maxclients"] == 42
        finally:
            r2h.stop()

    def test_save_rejects_newline_injection(self, r2h_binary):
        r2h = _start(r2h_binary, "[global]\nverbosity = 2\n\n[bind]\n* {port}\n")
        try:
            status, _, body = _post_settings(_port(r2h), b"hostname=evil%0D%0Ar2h-token=hacked")
            assert status == 400
            assert json.loads(body)["success"] is False
        finally:
            r2h.stop()

    @pytest.mark.parametrize(
        ("listen", "bind_line"),
        [("{port}", "* {port}"), ("127.0.0.1:{port}", "127.0.0.1 {port}"), ("* {port}", "* {port}")],
    )
    def test_save_writes_listen_in_bind_format(self, r2h_binary, listen, bind_line):
        r2h = _start(r2h_binary, "[global]\nverbosity = 2\n\n[bind]\n* {port}\n")
        try:
            port = _port(r2h)
            body = f"verbosity=3&listen={quote(listen.format(port=port))}".encode()
            status, _, _ = _post_settings(port, body)
            assert status == 200

            assert r2h._config_path is not None
            with open(r2h._config_path) as f:
                saved = f.read()
            assert f"\n{bind_line.format(port=port)}\n" in saved

            deadline = time.monotonic() + 5.0
            data = {}
            while time.monotonic() < deadline:
                status, _, raw = http_get("127.0.0.1", port, "/setting/api/get-config", timeout=1.0)
                if status == 200:
                    data = json.loads(raw)
                    if data["verbosity"] == 3:
                        break
                time.sleep(0.1)
            assert data.get("verbosity") == 3, "server must still listen on its port after saving"
        finally:
            r2h.stop()

    @pytest.mark.parametrize("listen", ["a:b:c", "[::1]", "host:"])
    def test_save_rejects_malformed_listen(self, r2h_binary, listen):
        r2h = _start(r2h_binary, "[global]\nverbosity = 2\n\n[bind]\n* {port}\n")
        try:
            status, _, body = _post_settings(_port(r2h), f"listen={quote(listen)}".encode())
            assert status == 400
            assert json.loads(body)["success"] is False
        finally:
            r2h.stop()

    def test_web_auth_fields_round_trip(self, r2h_binary):
        r2h = _start(r2h_binary, "[global]\nverbosity = 4\n\n[bind]\n* {port}\n")
        try:
            status, _, _ = _post_settings(
                _port(r2h), b"web-auth-user=admin&web-auth-password=secret&web-auth-require-local=1"
            )
            assert status == 200

            auth = _basic_auth("admin", "secret")
            _wait_for_status(_port(r2h), "/setting/api/get-config", 401)
            status, _, body = http_get("127.0.0.1", _port(r2h), "/setting/api/get-config", headers=auth)
            assert status == 200
            data = json.loads(body)
            assert data["web-auth-user"] == "admin"
            assert data["web-auth-password"] == "secret"
            assert data["web-auth-require-local"] is True
        finally:
            r2h.stop()


class TestBarePathsWithAppPathPrefix:
    @pytest.mark.parametrize("path", ["/status", "/player", "/setting", "/playlist.m3u"])
    def test_reachable_prefixed_and_bare(self, prefixed_r2h, path):
        status, _, _ = http_get("127.0.0.1", _port(prefixed_r2h), f"{APP_PREFIX}{path}")
        assert status == 200
        status, _, _ = http_get("127.0.0.1", _port(prefixed_r2h), path)
        assert status == 200

    def test_setting_api_reachable_prefixed_and_bare(self, prefixed_r2h):
        for path in (f"{APP_PREFIX}/setting/api/get-config", "/setting/api/get-config"):
            status, _, body = http_get("127.0.0.1", _port(prefixed_r2h), path)
            assert status == 200, path
            assert json.loads(body)["app-path-prefix"] == APP_PREFIX

    def test_stream_reachable_prefixed_and_bare(self, prefixed_r2h):
        for path in ("/rtp/239.0.0.1:1234", f"{APP_PREFIX}/rtp/239.0.0.1:1234"):
            status, _, _ = http_request("127.0.0.1", _port(prefixed_r2h), "HEAD", path, timeout=3.0)
            assert status == 200, path

    def test_unknown_path_still_404(self, prefixed_r2h):
        status, _, _ = http_get("127.0.0.1", _port(prefixed_r2h), f"{APP_PREFIX}2/status")
        assert status == 404


WEB_AUTH_CONFIG = """\
[global]
verbosity = 4
xff = 1
web-auth-user = admin
web-auth-password = secret

[bind]
* {port}
"""


@pytest.fixture(scope="module")
def auth_r2h(r2h_binary):
    r2h = _start(r2h_binary, WEB_AUTH_CONFIG + "\n[services]\nrtp://239.0.0.1:1234\n")
    yield r2h
    r2h.stop()


REMOTE = {"X-Forwarded-For": "8.8.8.8"}


class TestWebBasicAuth:
    CONFIG = WEB_AUTH_CONFIG

    def test_local_client_bypasses_auth(self, auth_r2h):
        status, _, _ = http_get("127.0.0.1", _port(auth_r2h), "/status")
        assert status == 200

    @pytest.mark.parametrize(
        "path", ["/status", "/status/api/status", "/player", "/setting", "/setting/api/get-config"]
    )
    def test_remote_client_requires_auth(self, auth_r2h, path):
        status, hdrs, _ = http_get("127.0.0.1", _port(auth_r2h), path, headers=REMOTE)
        assert status == 401
        assert "Basic" in get_header(hdrs, "WWW-Authenticate")

        status, _, _ = http_get("127.0.0.1", _port(auth_r2h), path, headers={**REMOTE, **_basic_auth("admin", "wrong")})
        assert status == 401

    @pytest.mark.parametrize("path", ["/status", "/player", "/setting", "/setting/api/get-config"])
    def test_remote_client_with_credentials(self, auth_r2h, path):
        status, _, _ = http_get(
            "127.0.0.1", _port(auth_r2h), path, headers={**REMOTE, **_basic_auth("admin", "secret")}
        )
        assert status == 200

    def test_forged_leftmost_forwarded_for_does_not_bypass(self, auth_r2h):
        status, _, _ = http_get(
            "127.0.0.1", _port(auth_r2h), "/status", headers={"X-Forwarded-For": "10.0.0.1, 8.8.8.8"}
        )
        assert status == 401

    def test_proxy_appended_local_hop_is_local(self, auth_r2h):
        status, _, _ = http_get(
            "127.0.0.1", _port(auth_r2h), "/status", headers={"X-Forwarded-For": "8.8.8.8, 192.168.1.20"}
        )
        assert status == 200

    def test_stream_route_unaffected(self, auth_r2h):
        status, _, _ = http_request(
            "127.0.0.1", _port(auth_r2h), "HEAD", "/rtp/239.0.0.1:1234", headers=REMOTE, timeout=3.0
        )
        assert status == 200

    def test_assets_and_manifests_unaffected(self, auth_r2h):
        status, _, _ = http_get("127.0.0.1", _port(auth_r2h), "/status.webmanifest", headers=REMOTE)
        assert status == 200

    def test_r2h_token_and_basic_auth_both_required(self, r2h_binary):
        r2h = _start(r2h_binary, self.CONFIG.replace("xff = 1\n", "xff = 1\nr2h-token = sometoken\n"))
        try:
            port = _port(r2h)
            good = _basic_auth("admin", "secret")
            assert http_get("127.0.0.1", port, "/status", headers=REMOTE)[0] == 401
            assert http_get("127.0.0.1", port, "/status", headers={**REMOTE, **good})[0] == 401
            assert http_get("127.0.0.1", port, "/status?r2h-token=sometoken", headers=REMOTE)[0] == 401
            assert http_get("127.0.0.1", port, "/status?r2h-token=sometoken", headers={**REMOTE, **good})[0] == 200
        finally:
            r2h.stop()

    def test_require_local_forces_auth_for_loopback(self, r2h_binary):
        r2h = _start(r2h_binary, self.CONFIG.replace("xff = 1\n", "web-auth-require-local = 1\n"))
        try:
            assert http_get("127.0.0.1", _port(r2h), "/status")[0] == 401
            assert http_get("127.0.0.1", _port(r2h), "/status", headers=_basic_auth("admin", "secret"))[0] == 200
        finally:
            r2h.stop()

    def test_web_auth_credentials_not_forwarded_to_http_upstream(self, auth_r2h):
        upstream = MockHTTPUpstream(routes={"/s": {"status": 200, "body": b"ok"}})
        upstream.start()
        try:
            path = f"/http/127.0.0.1:{upstream.port}/s"
            status, _, _ = http_get("127.0.0.1", _port(auth_r2h), path, headers=_basic_auth("admin", "secret"))
            assert status == 200
            status, _, _ = http_get("127.0.0.1", _port(auth_r2h), path, headers=_basic_auth("other", "creds"))
            assert status == 200
        finally:
            upstream.stop()
        forwarded = [
            {k.lower(): v for k, v in r["headers"].items()}.get("authorization") for r in upstream.requests_log
        ]
        assert forwarded == [None, _basic_auth("other", "creds")["Authorization"]]

    def test_head_401_has_no_body(self, auth_r2h):
        status, _, body = http_request("127.0.0.1", _port(auth_r2h), "HEAD", "/status", headers=REMOTE)
        assert status == 401
        assert body == b""
