#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Comprehensive local workerd integration test suite for Onslaught CE Online Lobby.

Runs the real `worker.mjs` and `issuer.mjs` inside the local `workerd` binary on loopback
with SQLite Durable Objects and a loopback mock TURN provider (zero external network traffic,
zero live Cloudflare deployments).
"""
from __future__ import annotations

import base64
import hashlib
import hmac
import http.client
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import tomllib
import unittest
from urllib.parse import urlencode


REPO_ROOT = Path(__file__).resolve().parents[2]
SERVICE_DIR = REPO_ROOT / "port/services/online_lobby"
OPS_DIR = SERVICE_DIR / "ops"
sys.path.insert(0, str(OPS_DIR))
import common as ops_common  # noqa: E402
import smoke_test as ops_smoke  # noqa: E402


SESSION_SECRET = "11" * 32
PERMIT_KEY = "22" * 32
TURN_KEY_ID = "test-turn-key-01"
TURN_API_TOKEN = "canary-master-turn-token-do-not-leak"
MOCK_TURN_USER = "mock-ephemeral-turn-user"
MOCK_TURN_CRED = "mock-ephemeral-turn-cred"

STRICT_HEADERS = {
    "content-type": "application/json",
    "cache-control": "no-store",
    "pragma": "no-cache",
    "x-content-type-options": "nosniff",
    "content-encoding": "identity",
}

MOCK_PROVIDER_JS = r'''
import { DurableObject } from "cloudflare:workers";

export class MockTurnProvider extends DurableObject {
  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === "/stats") {
      const calls = (await this.ctx.storage.get("calls")) || 0;
      return Response.json({ calls });
    }
    if (url.pathname === "/set-mode" && request.method === "POST") {
      const body = await request.json();
      await this.ctx.storage.put("mode", body.mode || "ok");
      return Response.json({ ok: true });
    }
    const calls = ((await this.ctx.storage.get("calls")) || 0) + 1;
    await this.ctx.storage.put("calls", calls);
    const mode = (await this.ctx.storage.get("mode")) || this.env.DEFAULT_MODE || "ok";

    const expectedUrl = "https://rtc.live.cloudflare.com/v1/turn/keys/" +
      this.env.TURN_KEY_ID + "/credentials/generate-ice-servers";
    const body = await request.json().catch(() => null);
    if (url.href !== expectedUrl ||
        request.headers.get("Authorization") !== ("Bearer " + this.env.TURN_API_TOKEN) ||
        !body || Object.keys(body).join(",") !== "ttl" ||
        !Number.isInteger(body.ttl) || body.ttl < 60 || body.ttl > 14400) {
      return new Response(JSON.stringify({ error: "bad_request" }), {
        status: 400, headers: { "Content-Type": "application/json" },
      });
    }

    if (mode === "fail") {
      return new Response(JSON.stringify({ error: "upstream_down" }), {
        status: 503, headers: { "Content-Type": "application/json" },
      });
    }
    if (mode === "redirect") {
      return Response.redirect("https://elsewhere.invalid/turn", 302);
    }
    if (mode === "bad_url") {
      return Response.json({
        iceServers: [
          { urls: ["turn:evil.invalid:3478?transport=udp"], username: "u", credential: "p" },
        ],
      }, { status: 201 });
    }
    if (mode === "combined_obj") {
      return Response.json({
        iceServers: {
          urls: [
            "stun:stun.cloudflare.com:3478",
            "turn:turn.cloudflare.com:3478?transport=udp",
            "turn:turn.cloudflare.com:80?transport=tcp",
            "turns:turn.cloudflare.com:443?transport=tcp",
          ],
          username: "mock-ephemeral-turn-user",
          credential: "mock-ephemeral-turn-cred",
        },
      }, { status: 201 });
    }
    const cred = mode === "echo" ? this.env.TURN_API_TOKEN : "mock-ephemeral-turn-cred";
    return Response.json({
      iceServers: [
        { urls: ["stun:stun.cloudflare.com:3478"] },
        {
          urls: [
            "turn:turn.cloudflare.com:3478?transport=udp",
            "turn:turn.cloudflare.com:443?transport=udp",
            "turn:turn.cloudflare.com:3478?transport=tcp",
            "turns:turn.cloudflare.com:5349?transport=tcp",
          ],
          username: "mock-ephemeral-turn-user",
          credential: cred,
        },
      ],
    }, { status: 201 });
  }
}

export default {
  async fetch(request, env) {
    return env.PROVIDER.get(env.PROVIDER.idFromName("mock-turn-v1")).fetch(request);
  },
};
'''


def find_workerd() -> Path | None:
    """Discover local workerd binary across BEA_WORKERD, repo root, git common dir, and PATH."""
    candidates: list[Path] = []
    env_bin = os.environ.get("BEA_WORKERD")
    if env_bin:
        candidates.append(Path(env_bin))
    rel_paths = [
        Path("local-data/cloudflare-operator/install/node_modules/@cloudflare/workerd-linux-64/bin/workerd"),
        Path("local-data/cloudflare-operator/install/node_modules/@cloudflare/workerd-linux-arm64/bin/workerd"),
        Path("local-data/cloudflare-operator/install/node_modules/.bin/workerd"),
    ]
    common_parent = ops_common.find_git_common_parent(REPO_ROOT)
    for rel in rel_paths:
        candidates.append(REPO_ROOT / rel)
        if common_parent and common_parent != REPO_ROOT:
            candidates.append(common_parent / rel)
    which_bin = shutil.which("workerd")
    if which_bin:
        candidates.append(Path(which_bin))
    for path in candidates:
        if path.is_file() and os.access(path, os.X_OK):
            return path.resolve()
    return None


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def canonical_json(obj) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"))


class WorkerdHarness:
    def __init__(
        self,
        workerd_bin: Path,
        workdir: Path,
        *,
        enabled: bool = True,
        provider_mode: str = "ok",
        daily_turn_limit: int = 200,
        monthly_turn_limit: int = 2000,
        heartbeat_timeout_ms: int = 30000,
        auth_ip_limit: int = 200,
        auth_pubkey_limit: int = 100,
        room_create_limit: int = 100,
    ):
        self.workerd_bin = workerd_bin
        self.workdir = workdir
        self.workdir.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.state_dir = self.workdir / "state"
        self.state_dir.mkdir(mode=0o700, exist_ok=True)
        self.home_dir = self.workdir / "home"
        self.home_dir.mkdir(mode=0o700, exist_ok=True)
        self.tmp_dir = self.workdir / "tmp"
        self.tmp_dir.mkdir(mode=0o700, exist_ok=True)

        self.http_port = free_port()
        self.provider_port = free_port()
        self.issuer_port = free_port()
        self.enabled = enabled
        self.provider_mode = provider_mode
        self.daily_turn_limit = daily_turn_limit
        self.monthly_turn_limit = monthly_turn_limit
        self.heartbeat_timeout_ms = heartbeat_timeout_ms
        self.auth_ip_limit = auth_ip_limit
        self.auth_pubkey_limit = auth_pubkey_limit
        self.room_create_limit = room_create_limit
        self.proc: subprocess.Popen | None = None
        self.log_path = self.workdir / "workerd.log"

        shutil.copyfile(SERVICE_DIR / "worker.mjs", self.workdir / "worker.mjs")
        shutil.copyfile(SERVICE_DIR / "issuer.mjs", self.workdir / "issuer.mjs")
        (self.workdir / "provider.mjs").write_text(MOCK_PROVIDER_JS, encoding="utf-8")

    def _capnp(self) -> str:
        def bindings(mapping: dict[str, str]) -> str:
            return ",\n".join(
                f"(name = {json.dumps(k)}, text = {json.dumps(v)})" for k, v in mapping.items()
            )

        enabled_str = "true" if self.enabled else "false"
        public_vars = {
            "CE_ENABLED": enabled_str,
            "CE_LOCAL_HTTP": "true",
            "CE_SESSION_SECRET": SESSION_SECRET,
            "CE_ISSUER_PERMIT_KEY": PERMIT_KEY,
            "CE_SESSION_SECONDS": "14400",
            "CE_TURN_TTL_SECONDS": "3600",
            "CE_DAILY_TURN_LIMIT": str(self.daily_turn_limit),
            "CE_MONTHLY_TURN_LIMIT": str(self.monthly_turn_limit),
            "CE_HEARTBEAT_TIMEOUT_MS": str(self.heartbeat_timeout_ms),
            "CE_MAX_ACTIVE_ROOMS": "128",
            "CE_AUTH_IP_LIMIT": str(self.auth_ip_limit),
            "CE_AUTH_PUBKEY_LIMIT": str(self.auth_pubkey_limit),
            "CE_ROOM_CREATE_LIMIT": str(self.room_create_limit),
        }
        issuer_vars = {
            "CE_ENABLED": enabled_str,
            "CE_ISSUER_PERMIT_KEY": PERMIT_KEY,
            "CE_TURN_TTL_SECONDS": "3600",
            "TURN_KEY_ID": TURN_KEY_ID,
            "TURN_API_TOKEN": TURN_API_TOKEN,
        }
        provider_vars = {
            "DEFAULT_MODE": self.provider_mode,
            "TURN_KEY_ID": TURN_KEY_ID,
            "TURN_API_TOKEN": TURN_API_TOKEN,
        }
        return f'''using Workerd = import "/workerd/workerd.capnp";
const config :Workerd.Config = (
  services = [
    (name = "public", worker = (
      modules = [(name = "worker.mjs", esModule = embed "worker.mjs")],
      compatibilityDate = "2026-11-02",
      globalOutbound = "denied",
      bindings = [
        {bindings(public_vars)},
        (name = "LOBBY_DIRECTORY", durableObjectNamespace = "LobbyDirectory"),
        (name = "GAME_ROOM", durableObjectNamespace = "GameRoom"),
        (name = "TURN_ISSUER", service = "issuer")
      ],
      durableObjectNamespaces = [
        (className = "LobbyDirectory", uniqueKey = "ce-online-lobby-dir-test-v1", enableSql = true),
        (className = "GameRoom", uniqueKey = "ce-online-lobby-room-test-v1", enableSql = true)
      ],
      durableObjectStorage = (localDisk = "state")
    )),
    (name = "issuer", worker = (
      modules = [
        (name = "issuer.mjs", esModule = embed "issuer.mjs"),
        (name = "worker.mjs", esModule = embed "worker.mjs")
      ],
      compatibilityDate = "2026-11-02",
      globalOutbound = "provider",
      bindings = [
        {bindings(issuer_vars)},
        (name = "PERMIT_AUTHORITY", service = (name = "public", entrypoint = "PermitAuthority"))
      ]
    )),
    (name = "provider", worker = (
      modules = [(name = "provider.mjs", esModule = embed "provider.mjs")],
      compatibilityDate = "2026-11-02",
      globalOutbound = "denied",
      bindings = [
        {bindings(provider_vars)},
        (name = "PROVIDER", durableObjectNamespace = "MockTurnProvider")
      ],
      durableObjectNamespaces = [
        (className = "MockTurnProvider", uniqueKey = "ce-online-lobby-provider-test-v1", enableSql = true)
      ],
      durableObjectStorage = (localDisk = "state")
    )),
    (name = "state", disk = (path = {json.dumps(str(self.state_dir))}, writable = true)),
    (name = "denied", network = (allow = []))
  ],
  sockets = [
    (name = "http", address = "127.0.0.1:{self.http_port}", http = (), service = "public"),
    (name = "provider", address = "127.0.0.1:{self.provider_port}", http = (), service = "provider"),
    (name = "issuer", address = "127.0.0.1:{self.issuer_port}", http = (), service = "issuer")
  ]
);
'''

    def start(self) -> "WorkerdHarness":
        assert self.proc is None
        cfg_path = self.workdir / "config.capnp"
        cfg_path.write_text(self._capnp(), encoding="utf-8")
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": str(self.home_dir),
            "TMPDIR": str(self.tmp_dir),
            "XDG_CACHE_HOME": str(self.home_dir / "cache"),
        }

        def _no_core():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

        log_fh = self.log_path.open("ab")
        try:
            self.proc = subprocess.Popen(
                [str(self.workerd_bin), "serve", str(cfg_path)],
                cwd=str(self.workdir),
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=log_fh,
                stderr=subprocess.STDOUT,
                start_new_session=True,
                preexec_fn=_no_core,
            )
        finally:
            log_fh.close()

        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                log_text = self.log_path.read_text(encoding="utf-8", errors="replace")[:2000]
                raise AssertionError(f"workerd exited early (code {self.proc.returncode}):\n{log_text}")
            try:
                with socket.create_connection(("127.0.0.1", self.http_port), timeout=0.1):
                    return self
            except OSError:
                time.sleep(0.03)
        raise AssertionError("workerd listener did not become ready within 10s.")

    def stop(self) -> None:
        if self.proc is not None:
            if self.proc.poll() is None:
                try:
                    os.killpg(self.proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(self.proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    self.proc.wait(timeout=5)
            self.proc = None

    def raw_request(
        self,
        method: str,
        path: str,
        *,
        body: bytes | None = None,
        headers: dict[str, str] | None = None,
        port: int | None = None,
    ) -> tuple[int, dict[str, str], bytes]:
        conn = http.client.HTTPConnection("127.0.0.1", port or self.http_port, timeout=15)
        try:
            conn.request(method, path, body=body, headers=headers or {})
            resp = conn.getresponse()
            data = resp.read(70000)
            hdrs = {k.lower(): v for k, v in resp.getheaders()}
            return resp.status, hdrs, data
        finally:
            conn.close()

    def api(
        self,
        method: str,
        path: str,
        *,
        body: dict | None = None,
        token: str | None = None,
        extra_headers: dict[str, str] | None = None,
        raw_body: bytes | None = None,
    ) -> tuple[int, dict[str, str], dict]:
        headers: dict[str, str] = {"Accept": "application/json", "Accept-Encoding": "identity"}
        if token is not None:
            headers["Authorization"] = f"Bearer {token}"
        payload = raw_body
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = canonical_json(body).encode("utf-8")
        elif raw_body is not None and (not extra_headers or "Content-Type" not in extra_headers):
            headers["Content-Type"] = "application/json"
        if extra_headers:
            headers.update(extra_headers)
        code, hdrs, raw = self.raw_request(method, path, body=payload, headers=headers)
        for req_k, req_v in STRICT_HEADERS.items():
            actual = hdrs.get(req_k, "")
            if actual.lower() != req_v:
                raise AssertionError(
                    f"Strict header {req_k!r} mismatch on {method} {path} (HTTP {code}): "
                    f"expected {req_v!r}, got {actual!r}"
                )
        if "location" in hdrs or "set-cookie" in hdrs:
            raise AssertionError(f"Forbidden header present on {method} {path}: {hdrs}")
        return code, hdrs, json.loads(raw.decode("utf-8"))

    def provider_calls(self) -> int:
        code, _, raw = self.raw_request("GET", "/stats", port=self.provider_port)
        assert code == 200
        return json.loads(raw.decode("utf-8"))["calls"]

    def set_provider_mode(self, mode: str) -> None:
        code, _, _ = self.raw_request(
            "POST",
            "/set-mode",
            body=json.dumps({"mode": mode}).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            port=self.provider_port,
        )
        assert code == 200

    def auth_player(
        self,
        callsign: str,
        *,
        build_id: str = "0.1.14",
        content_id: str = "",
        ip: str | None = None,
        signer: ops_smoke.EphemeralSigner | None = None,
    ) -> tuple[ops_smoke.EphemeralSigner, dict]:
        signer = signer or ops_smoke.EphemeralSigner()
        extra = {"CF-Connecting-IP": ip} if ip else None
        code, _, ch = self.api(
            "POST",
            "/ce-lobby/v1/auth/challenge",
            body={"pubkey": signer.pubkey_hex, "callsign": callsign, "build_id": build_id, "content_id": content_id},
            extra_headers=extra,
        )
        assert code == 200, f"challenge failed: {code} {ch}"
        msg = f"onslaught-ce-lobby-auth-v1\n{ch['nonce']}\n{ch['issued_at']}\n{signer.pubkey_hex}\n{callsign}".encode("utf-8")
        sig = signer.sign(msg).hex()
        code, _, sess = self.api(
            "POST",
            "/ce-lobby/v1/auth/session",
            body={
                "pubkey": signer.pubkey_hex,
                "callsign": callsign,
                "nonce": ch["nonce"],
                "issued_at": ch["issued_at"],
                "signature": sig,
                "build_id": build_id,
                "content_id": content_id,
            },
            extra_headers=extra,
        )
        assert code == 200, f"session failed: {code} {sess}"
        return signer, sess

    def read_room_sqlite(self) -> list[dict]:
        rooms = []
        room_dir = self.state_dir / "ce-online-lobby-room-test-v1"
        if not room_dir.is_dir():
            return rooms
        for db_path in sorted(room_dir.glob("*.sqlite")):
            if db_path.name == "metadata.sqlite":
                continue
            with sqlite3.connect(str(db_path)) as conn:
                rows = conn.execute("SELECT value FROM room_kv WHERE name = 'room'").fetchall()
                for (val,) in rows:
                    rooms.append(json.loads(val))
        return rooms


WORKERD_BIN = find_workerd()


class StaticAndOpsTests(unittest.TestCase):
    def test_wrangler_templates_and_hygiene(self):
        lobby = tomllib.loads((SERVICE_DIR / "wrangler.toml").read_text(encoding="utf-8"))
        issuer = tomllib.loads((SERVICE_DIR / "issuer.wrangler.toml").read_text(encoding="utf-8"))

        self.assertEqual("onslaught-ce-lobby", lobby["name"])
        self.assertIs(True, lobby["workers_dev"])
        self.assertIs(False, lobby["preview_urls"])
        self.assertIs(False, lobby["observability"]["enabled"])
        self.assertEqual("false", lobby["vars"]["CE_ENABLED"])
        self.assertEqual(["LobbyDirectory", "GameRoom"], lobby["migrations"][0]["new_sqlite_classes"])
        self.assertEqual("TURN_ISSUER", lobby["services"][0]["binding"])
        self.assertEqual("onslaught-ce-turn-issuer", lobby["services"][0]["service"])

        self.assertEqual("onslaught-ce-turn-issuer", issuer["name"])
        self.assertIs(False, issuer["workers_dev"])
        self.assertIs(False, issuer["preview_urls"])
        self.assertIs(False, issuer["observability"]["enabled"])
        self.assertEqual("false", issuer["vars"]["CE_ENABLED"])
        self.assertEqual("PERMIT_AUTHORITY", issuer["services"][0]["binding"])
        self.assertEqual("PermitAuthority", issuer["services"][0]["entrypoint"])
        self.assertEqual([], issuer["env"]["bootstrap"]["services"])
        self.assertIs(False, issuer["env"]["bootstrap"]["workers_dev"])
        self.assertIs(False, issuer["env"]["bootstrap"]["preview_urls"])

        ops_common.validate_wrangler_templates()

        for path in sorted(SERVICE_DIR.rglob("*")):
            if path.is_file() and path.suffix in (".mjs", ".toml", ".capnp", ".py", ".md"):
                text = path.read_text(encoding="utf-8")
                self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:240], f"Missing SPDX in {path.name}")
                if path.suffix in (".toml", ".capnp", ".mjs", ".md"):
                    self.assertNotIn("account_id", text, f"Unexpected account_id in {path.name}")

    def test_ops_toolkit_dry_run_and_stdin_secret_safety(self):
        with tempfile.TemporaryDirectory(prefix="ce-ops-test-") as tmp:
            tmp_path = Path(tmp)
            # Fake local Wrangler wrapper verifying secret arrives ONLY on stdin, never argv.
            capture_file = tmp_path / "captured.json"
            fake_wrapper = tmp_path / "fake_wrangler.py"
            fake_wrapper.write_text(
                "import json, sys\n"
                "data = {'argv': sys.argv[1:], 'stdin': sys.stdin.read()}\n"
                f"open({str(capture_file)!r}, 'w').write(json.dumps(data))\n"
                "print('echo_canary ' + data['stdin'].strip())\n",
                encoding="utf-8",
            )
            secret_val = "ab" * 32
            ops_common.put_secret(ops_common.LOBBY_TOML, "CE_SESSION_SECRET", secret_val, wrapper=fake_wrapper)
            captured = json.loads(capture_file.read_text(encoding="utf-8"))
            self.assertEqual(secret_val, captured["stdin"].strip())
            for arg in captured["argv"]:
                self.assertNotIn(secret_val, arg)

            # Verify redaction helper scrubs secret values from output.
            self.assertEqual("val=[REDACTED]", ops_common.redact(f"val={secret_val}", [secret_val]))

            # Verify private secret file permission check refuses world-readable files.
            bad_file = tmp_path / "bad_secret"
            bad_file.write_text(secret_val + "\n", encoding="utf-8")
            os.chmod(bad_file, 0o644)
            with self.assertRaises(PermissionError):
                ops_common.read_private_secret_file(bad_file)
            good_file = ops_common.write_private_secret_file(tmp_path / "priv", "CE_SESSION_SECRET", secret_val)
            self.assertEqual(secret_val, ops_common.read_private_secret_file(good_file))

            # Verify dry-run paths execute cleanly with exit code 0 and zero secret output.
            for cmd in (
                [sys.executable, "-B", str(OPS_DIR / "deploy.py"), "--dry-run"],
                [sys.executable, "-B", str(OPS_DIR / "killswitch.py"), "--disable", "--dry-run"],
                [sys.executable, "-B", str(OPS_DIR / "killswitch.py"), "--enable", "--dry-run"],
                [sys.executable, "-B", str(OPS_DIR / "status.py"), "--wrangler", "--dry-run"],
            ):
                proc = subprocess.run(cmd, capture_output=True, text=True, check=False, timeout=15)
                self.assertEqual(0, proc.returncode, proc.stderr or proc.stdout)
                self.assertNotIn("a" * 64, proc.stdout)


@unittest.skipIf(WORKERD_BIN is None, "workerd binary not found locally")
class WorkerdIntegrationTests(unittest.TestCase):
    def make_server(self, **kwargs) -> WorkerdHarness:
        tmp = tempfile.TemporaryDirectory(prefix="ce-lobby-workerd-")
        self.addCleanup(tmp.cleanup)
        harness = WorkerdHarness(WORKERD_BIN, Path(tmp.name), **kwargs)
        self.addCleanup(harness.stop)
        return harness.start()

    def test_killswitch_disabled_returns_503_with_strict_headers(self):
        srv = self.make_server(enabled=False)
        code, _, data = srv.api("GET", "/ce-lobby/v1/health")
        self.assertEqual(503, code)
        self.assertIs(False, data["ok"])
        self.assertIs(False, data["enabled"])

        signer = ops_smoke.EphemeralSigner()
        code, _, data = srv.api(
            "POST",
            "/ce-lobby/v1/auth/challenge",
            body={"pubkey": signer.pubkey_hex, "callsign": "PilotOne", "build_id": "0.1.14", "content_id": ""},
        )
        self.assertEqual(503, code)
        self.assertEqual("service_unavailable", data["error"])

        code, _, _ = srv.api("GET", "/ce-lobby/v1/lobbies")
        self.assertEqual(503, code)

        # Direct issuer call also fails closed when CE_ENABLED=false.
        code, _, _ = srv.raw_request(
            "POST", "/issue",
            body=b'{"permit":{},"signature":"x"}',
            headers={"Content-Type": "application/json"},
            port=srv.issuer_port,
        )
        self.assertEqual(503, code)
        self.assertEqual(0, srv.provider_calls())

        # ops/status.py exits 2 on disabled 503.
        proc = subprocess.run(
            [
                sys.executable, "-B", str(OPS_DIR / "status.py"),
                "--url", f"http://127.0.0.1:{srv.http_port}",
                "--allow-loopback-http",
            ],
            capture_output=True, text=True, check=False, timeout=15,
        )
        self.assertEqual(2, proc.returncode, proc.stderr)

    def test_http_strictness_and_malformed_inputs(self):
        srv = self.make_server()
        code, _, data = srv.api("GET", "/ce-lobby/v1/health")
        self.assertEqual(200, code)
        self.assertTrue(data["ok"])
        self.assertEqual(1, data["proto"])
        self.assertEqual(4, data["max_match_slots"])
        self.assertEqual(64, data["wire_max_slots"])

        # Path traversal & unknown route.
        code, _, _ = srv.api("GET", "/ce-lobby/v1/../health")
        self.assertEqual(400, code)
        code, _, _ = srv.api("GET", "/ce-lobby/v1/nonexistent", token="bad")
        self.assertEqual(401, code)

        # Duplicate keys, invalid JSON, wrong Content-Type, oversized body.
        code, _, data = srv.api("POST", "/ce-lobby/v1/auth/challenge", raw_body=b'{"pubkey":"a","pubkey":"b"}')
        self.assertEqual(400, code)
        self.assertEqual("duplicate_key", data["error"])
        code, _, _ = srv.api("POST", "/ce-lobby/v1/auth/challenge", raw_body=b"{not json")
        self.assertEqual(400, code)
        code, _, _ = srv.api(
            "POST", "/ce-lobby/v1/auth/challenge",
            raw_body=b"{}", extra_headers={"Content-Type": "text/plain"},
        )
        self.assertEqual(400, code)
        code, _, _ = srv.api("POST", "/ce-lobby/v1/auth/challenge", raw_body=b" " * 65537)
        self.assertEqual(400, code)

    def test_ed25519_auth_antireplay_expiry_signature_and_ratelimits(self):
        srv = self.make_server(auth_ip_limit=5, auth_pubkey_limit=3)
        signer = ops_smoke.EphemeralSigner()
        raw_callsign = "  Sky   Hawk  "
        code, _, ch = srv.api(
            "POST",
            "/ce-lobby/v1/auth/challenge",
            body={"pubkey": signer.pubkey_hex, "callsign": raw_callsign, "build_id": "0.1.14", "content_id": ""},
            extra_headers={"CF-Connecting-IP": "198.51.100.1"},
        )
        self.assertEqual(200, code)
        self.assertEqual(32, len(ch["nonce"]))
        self.assertEqual(ch["issued_at"] + 120, ch["expires_at"])

        # 1. Bad signature (flipped last bit) -> 401.
        msg = f"onslaught-ce-lobby-auth-v1\n{ch['nonce']}\n{ch['issued_at']}\n{signer.pubkey_hex}\n{raw_callsign}".encode("utf-8")
        good_sig = bytearray(signer.sign(msg))
        bad_sig = bytearray(good_sig)
        bad_sig[-1] ^= 0x01
        code, _, err = srv.api(
            "POST",
            "/ce-lobby/v1/auth/session",
            body={
                "pubkey": signer.pubkey_hex, "callsign": raw_callsign,
                "nonce": ch["nonce"], "issued_at": ch["issued_at"],
                "signature": bad_sig.hex(), "build_id": "0.1.14", "content_id": "",
            },
            extra_headers={"CF-Connecting-IP": "198.51.100.2"},
        )
        self.assertEqual(401, code)
        self.assertEqual("invalid_signature", err["error"])

        # 2. Expired timestamp outside +-120s window -> 401.
        stale_ts = int(time.time()) - 300
        stale_msg = f"onslaught-ce-lobby-auth-v1\n{ch['nonce']}\n{stale_ts}\n{signer.pubkey_hex}\n{raw_callsign}".encode("utf-8")
        code, _, err = srv.api(
            "POST",
            "/ce-lobby/v1/auth/session",
            body={
                "pubkey": signer.pubkey_hex, "callsign": raw_callsign,
                "nonce": ch["nonce"], "issued_at": stale_ts,
                "signature": signer.sign(stale_msg).hex(), "build_id": "0.1.14", "content_id": "",
            },
            extra_headers={"CF-Connecting-IP": "198.51.100.3"},
        )
        self.assertEqual(401, code)
        self.assertEqual("timestamp_expired", err["error"])

        # 3. Valid Ed25519 signature succeeds and derives exact Crockford base32 player_id + 4-HEX tag.
        code, _, sess = srv.api(
            "POST",
            "/ce-lobby/v1/auth/session",
            body={
                "pubkey": signer.pubkey_hex, "callsign": raw_callsign,
                "nonce": ch["nonce"], "issued_at": ch["issued_at"],
                "signature": bytes(good_sig).hex(), "build_id": "0.1.14", "content_id": "",
            },
            extra_headers={"CF-Connecting-IP": "198.51.100.4"},
        )
        self.assertEqual(200, code)
        self.assertEqual(signer.player_id, sess["player_id"])
        self.assertEqual(26, len(sess["player_id"]))
        self.assertEqual(signer.tag, sess["tag"])
        self.assertEqual("Sky Hawk", sess["callsign"])
        self.assertTrue(sess["session_token"].startswith("CES1."))

        # 4. Single-use nonce replay -> 401.
        code, _, err = srv.api(
            "POST",
            "/ce-lobby/v1/auth/session",
            body={
                "pubkey": signer.pubkey_hex, "callsign": raw_callsign,
                "nonce": ch["nonce"], "issued_at": ch["issued_at"],
                "signature": bytes(good_sig).hex(), "build_id": "0.1.14", "content_id": "",
            },
            extra_headers={"CF-Connecting-IP": "198.51.100.5"},
        )
        self.assertEqual(401, code)
        self.assertEqual("nonce_replayed", err["error"])

        # 5. Tampered bearer token -> 401.
        tampered = sess["session_token"][:-1] + ("A" if sess["session_token"][-1] != "A" else "B")
        code, _, _ = srv.api("GET", "/ce-lobby/v1/lobbies", token=tampered)
        self.assertEqual(401, code)

        # 5b. Pure-Python RFC 8032 Ed25519 fallback matches cryptography byte-for-byte.
        seed = bytes(range(32))
        s_crypto = ops_smoke.EphemeralSigner(seed)
        h_digest = hashlib.sha512(seed).digest()
        a_int = int.from_bytes(h_digest[:32], "little")
        a_int = (a_int & ((1 << 254) - 8)) | (1 << 254)
        pub_pure = ops_smoke._ed_encode(ops_smoke._ed_scalarmult(ops_smoke._ED_B, a_int))
        self.assertEqual(s_crypto.pubkey_bytes, pub_pure)
        test_msg = b"onslaught-ce-lobby-auth-v1\ntest-vector"
        r_val = int.from_bytes(hashlib.sha512(h_digest[32:] + test_msg).digest(), "little") % ops_smoke._ED_L
        r_enc = ops_smoke._ed_encode(ops_smoke._ed_scalarmult(ops_smoke._ED_B, r_val))
        k_val = int.from_bytes(hashlib.sha512(r_enc + pub_pure + test_msg).digest(), "little") % ops_smoke._ED_L
        sig_pure = r_enc + ((r_val + k_val * a_int) % ops_smoke._ED_L).to_bytes(32, "little")
        self.assertEqual(s_crypto.sign(test_msg), sig_pure)

        # 6. Per-pubkey rate limit (auth_pubkey_limit=3 across distinct IPs).
        pk_signer = ops_smoke.EphemeralSigner()
        for i in range(3):
            code, _, _ = srv.api(
                "POST", "/ce-lobby/v1/auth/challenge",
                body={"pubkey": pk_signer.pubkey_hex, "callsign": "RatePub", "build_id": "0.1.14", "content_id": ""},
                extra_headers={"CF-Connecting-IP": f"198.51.100.{20 + i}"},
            )
            self.assertEqual(200, code)
        code, _, err = srv.api(
            "POST", "/ce-lobby/v1/auth/challenge",
            body={"pubkey": pk_signer.pubkey_hex, "callsign": "RatePub", "build_id": "0.1.14", "content_id": ""},
            extra_headers={"CF-Connecting-IP": "198.51.100.29"},
        )
        self.assertEqual(429, code)
        self.assertEqual("rate_limited", err["error"])

        # 7. Per-IP rate limit (auth_ip_limit=5 across distinct pubkeys).
        for _ in range(5):
            s = ops_smoke.EphemeralSigner()
            code, _, _ = srv.api(
                "POST", "/ce-lobby/v1/auth/challenge",
                body={"pubkey": s.pubkey_hex, "callsign": "RateIp", "build_id": "0.1.14", "content_id": ""},
                extra_headers={"CF-Connecting-IP": "203.0.113.99"},
            )
            self.assertEqual(200, code)
        s_extra = ops_smoke.EphemeralSigner()
        code, _, err = srv.api(
            "POST", "/ce-lobby/v1/auth/challenge",
            body={"pubkey": s_extra.pubkey_hex, "callsign": "RateIp", "build_id": "0.1.14", "content_id": ""},
            extra_headers={"CF-Connecting-IP": "203.0.113.99"},
        )
        self.assertEqual(429, code)
        self.assertEqual("rate_limited", err["error"])

    def test_directory_browse_paging_compatibility_and_unlisted_resolve(self):
        srv = self.make_server()
        _, host = srv.auth_player("DirHost", build_id="0.1.14", content_id="")
        tok = host["session_token"]

        # Reject invalid map, mode, and >4 max_seats for star_lockstep.
        for bad_payload in (
            {"name": "BadMap", "mode": "versus_2v2", "map": 999, "max_seats": 4},
            {"name": "BadMode", "mode": "free_for_all", "map": 850, "max_seats": 4},
            {"name": "TooBig", "mode": "versus_2v2", "map": 850, "max_seats": 5, "topology": "star_lockstep"},
            {"name": "TooSmall", "mode": "versus_2v2", "map": 850, "max_seats": 1},
            {"name": "BadTopo", "mode": "versus_2v2", "map": 850, "max_seats": 8, "topology": "authoritative"},
        ):
            code, _, _ = srv.api("POST", "/ce-lobby/v1/lobbies", token=tok, body=bad_payload)
            self.assertEqual(400, code, f"Expected 400 for {bad_payload}")

        # Create 5 compatible public rooms, 1 unlisted room, and 2 incompatible public rooms.
        pub_ids = []
        valid_maps = [100, 850, 856, 860, 866]
        valid_modes = ["tutorial", "versus_2v2", "coop_defence", "coop_skirmish", "versus_2v2"]
        for idx, (m, mode) in enumerate(zip(valid_maps, valid_modes)):
            code, _, r = srv.api(
                "POST", "/ce-lobby/v1/lobbies", token=tok,
                body={"name": f"Public Room {idx}", "mode": mode, "map": m, "max_seats": 4, "visibility": "public"},
            )
            self.assertEqual(200, code)
            pub_ids.append(r["room_id"])

        code, _, unlisted = srv.api(
            "POST", "/ce-lobby/v1/lobbies", token=tok,
            body={"name": "Secret Room", "mode": "versus_2v2", "map": 851, "max_seats": 4,
                  "visibility": "unlisted", "password": "pw"},
        )
        self.assertEqual(200, code)

        _, old_host = srv.auth_player("OldHost", build_id="0.1.13", content_id="")
        code, _, old_room = srv.api(
            "POST", "/ce-lobby/v1/lobbies", token=old_host["session_token"],
            body={"name": "Old Build", "mode": "versus_2v2", "map": 852, "max_seats": 4, "visibility": "public"},
        )
        self.assertEqual(200, code)

        _, mod_host = srv.auth_player("ModHost", build_id="0.1.14", content_id="f" * 64)
        code, _, mod_room = srv.api(
            "POST", "/ce-lobby/v1/lobbies", token=mod_host["session_token"],
            body={"name": "Modded Room", "mode": "versus_2v2", "map": 853, "max_seats": 4, "visibility": "public"},
        )
        self.assertEqual(200, code)

        # Page through compatible public rooms (limit=2): must return exact 5 rooms, never unlisted or incompatible.
        seen_ids = []
        cursor = 0
        pages = 0
        while True:
            qs = urlencode({"cursor": cursor, "limit": 2, "build_id": "0.1.14", "content_id": "", "include_incompatible": 0})
            code, _, page = srv.api("GET", f"/ce-lobby/v1/lobbies?{qs}", token=tok)
            self.assertEqual(200, code)
            pages += 1
            for item in page["rooms"]:
                self.assertTrue(item["compatible"])
                seen_ids.append(item["room_id"])
            cursor = page["next_cursor"]
            if cursor == 0:
                break
            self.assertLess(pages, 10)
        self.assertEqual(3, pages)
        self.assertEqual(set(pub_ids), set(seen_ids))
        self.assertNotIn(unlisted["room_id"], seen_ids)

        # include_incompatible=1 returns the 2 incompatible rooms marked compatible=False.
        qs = urlencode({"cursor": 0, "limit": 32, "build_id": "0.1.14", "content_id": "", "include_incompatible": 1})
        code, _, full = srv.api("GET", f"/ce-lobby/v1/lobbies?{qs}", token=tok)
        self.assertEqual(200, code)
        by_id = {r["room_id"]: r for r in full["rooms"]}
        self.assertEqual(7, len(by_id))
        self.assertIs(False, by_id[old_room["room_id"]]["compatible"])
        self.assertIs(False, by_id[mod_room["room_id"]]["compatible"])
        self.assertNotIn(unlisted["room_id"], by_id)

        # Resolve unlisted room code (case-insensitive) and 404 for unknown code.
        code, _, res = srv.api(
            "POST", "/ce-lobby/v1/lobbies/resolve", token=tok,
            body={"code": unlisted["code"].lower()},
        )
        self.assertEqual(200, code)
        self.assertEqual(unlisted["room_id"], res["room_id"])
        self.assertIs(True, res["has_password"])
        self.assertEqual("lobby", res["state"])

        code, _, _ = srv.api("POST", "/ce-lobby/v1/lobbies/resolve", token=tok, body={"code": "ZZZZ99"})
        self.assertEqual(404, code)

    def test_room_lifecycle_join_updates_ai_chat_and_kick(self):
        srv = self.make_server()
        _, host = srv.auth_player("RoomHost", build_id="0.1.14")
        _, g1 = srv.auth_player("GuestOne", build_id="0.1.14")
        _, g2 = srv.auth_player("GuestTwo", build_id="0.1.14")
        _, bad_ver = srv.auth_player("OldVer", build_id="0.1.13")

        room_password = "canary-room-password-123"
        code, _, room = srv.api(
            "POST", "/ce-lobby/v1/lobbies", token=host["session_token"],
            body={
                "name": "Forseti Skirmish", "mode": "versus_2v2", "map": 850,
                "max_seats": 2, "visibility": "public", "password": room_password,
                "require_relay": False, "ai_fill": False, "be_cfg": "Aquila",
            },
        )
        self.assertEqual(200, code)
        rid = room["room_id"]

        # Incompatible build -> 409; wrong password -> 403; right password -> 200.
        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/join", token=bad_ver["session_token"],
            body={"password": room_password, "build_id": "0.1.13", "content_id": "", "be_cfg": ""},
        )
        self.assertEqual(409, code)

        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/join", token=g1["session_token"],
            body={"password": "wrong", "build_id": "0.1.14", "content_id": "", "be_cfg": ""},
        )
        self.assertEqual(403, code)

        code, _, joined = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/join", token=g1["session_token"],
            body={"password": room_password, "build_id": "0.1.14", "content_id": "", "be_cfg": "Aquila"},
        )
        self.assertEqual(200, code)
        self.assertEqual("human", joined["seats"][1]["kind"])
        self.assertEqual(g1["player_id"], joined["seats"][1]["player_id"])

        # Room full (2/2) -> 409.
        code, _, err = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/join", token=g2["session_token"],
            body={"password": room_password, "build_id": "0.1.14", "content_id": "", "be_cfg": ""},
        )
        self.assertEqual(409, code)
        self.assertEqual("room_full", err["error"])

        # Expand to 4 seats, toggle AI slot on seat 2, closed on seat 3; refuse altering human seat 1.
        code, _, expanded = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/update", token=host["session_token"],
            body={"max_seats": 4, "set_seat_kind": {"seat": 2, "kind": "ai"}},
        )
        self.assertEqual(200, code)
        self.assertEqual(4, len(expanded["seats"]))
        self.assertEqual("ai", expanded["seats"][2]["kind"])

        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/update", token=host["session_token"],
            body={"set_seat_kind": {"seat": 1, "kind": "ai"}},
        )
        self.assertEqual(409, code)

        # Guest updates own seat (side, be_cfg, ready, rtt_ms, route); non-host room edit refused 403.
        code, _, updated = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/update", token=g1["session_token"],
            body={"team": 0, "be_cfg": "MuspellBE", "ready": True, "rtt_ms": 41, "route": "direct"},
        )
        self.assertEqual(200, code)
        self.assertEqual(0, updated["seats"][1]["team"])
        self.assertEqual("MuspellBE", updated["seats"][1]["be_cfg"])
        self.assertIs(True, updated["seats"][1]["ready"])
        self.assertEqual(41, updated["seats"][1]["rtt_ms"])
        self.assertEqual("direct", updated["seats"][1]["route"])

        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/update", token=g1["session_token"],
            body={"map": 855},
        )
        self.assertEqual(403, code)

        # Invalid be_cfg / room name return 400 (never 503 across DO RPC).
        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/update", token=g1["session_token"],
            body={"be_cfg": "bad\x00cfg"},
        )
        self.assertEqual(400, code)
        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/update", token=host["session_token"],
            body={"name": "   "},
        )
        self.assertEqual(400, code)

        # Poll unchanged vs rtt_ms update.
        rev = updated["rev"]
        code, _, poll_same = srv.api("GET", f"/ce-lobby/v1/rooms/{rid}?since={rev}", token=g1["session_token"])
        self.assertEqual(200, code)
        self.assertIs(True, poll_same["unchanged"])
        code, _, poll_rtt = srv.api("GET", f"/ce-lobby/v1/rooms/{rid}?since={rev}&rtt_ms=29", token=g1["session_token"])
        self.assertEqual(200, code)
        self.assertEqual(29, poll_rtt["seats"][1]["rtt_ms"])
        code, _, poll_same2 = srv.api(
            "GET", f"/ce-lobby/v1/rooms/{rid}?since={poll_rtt['rev']}&rtt_ms=29", token=g1["session_token"],
        )
        self.assertEqual(200, code)
        self.assertIs(True, poll_same2["unchanged"])

        # Map change resets non-host ready = False.
        code, _, map_changed = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/update", token=host["session_token"],
            body={"map": 855},
        )
        self.assertEqual(200, code)
        self.assertEqual(855, map_changed["map"])
        self.assertIs(False, map_changed["seats"][1]["ready"])

        # Chat sanitization, length limit, rate limit (5 / 10s), and 32-message ring.
        code, _, c1 = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/chat", token=host["session_token"],
            body={"text": "  gl\x00\nhf\u2603  "},
        )
        self.assertEqual(200, code)
        self.assertEqual("glhf", c1["chat"][-1]["text"])

        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/chat", token=host["session_token"],
            body={"text": "\x00\n\t\u2603"},
        )
        self.assertEqual(400, code)

        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/chat", token=host["session_token"],
            body={"text": "A" * 161},
        )
        self.assertEqual(400, code)

        for i in range(4):
            code, _, _ = srv.api(
                "POST", f"/ce-lobby/v1/rooms/{rid}/chat", token=host["session_token"],
                body={"text": f"host msg {i}"},
            )
            self.assertEqual(200, code)
        # 6th message within 10s -> 429.
        code, _, err = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/chat", token=host["session_token"],
            body={"text": "too fast"},
        )
        self.assertEqual(429, code)
        self.assertEqual("rate_limited", err["error"])

        # Host kicks g1 -> g1 blocked on rejoin, poll, chat, update.
        code, _, kicked = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/update", token=host["session_token"],
            body={"kick_seat": 1},
        )
        self.assertEqual(200, code)
        self.assertEqual("open", kicked["seats"][1]["kind"])

        for method, path, body in (
            ("POST", f"/ce-lobby/v1/rooms/{rid}/join", {"password": room_password, "build_id": "0.1.14", "content_id": "", "be_cfg": ""}),
            ("GET", f"/ce-lobby/v1/rooms/{rid}", None),
            ("POST", f"/ce-lobby/v1/rooms/{rid}/chat", {"text": "back?"}),
            ("POST", f"/ce-lobby/v1/rooms/{rid}/update", {"ready": True}),
        ):
            code, _, _ = srv.api(method, path, token=g1["session_token"], body=body)
            self.assertEqual(403, code, f"Kicked player should get 403 on {method} {path}")

        # Verify bounded 32-msg ring by having 7 temporary players join, send 5 msgs each, and leave.
        last_chat_state = None
        for p_idx in range(7):
            _, tmp_p = srv.auth_player(f"Chatter{p_idx}", build_id="0.1.14")
            code, _, _ = srv.api(
                "POST", f"/ce-lobby/v1/rooms/{rid}/join", token=tmp_p["session_token"],
                body={"password": room_password, "build_id": "0.1.14", "content_id": "", "be_cfg": ""},
            )
            self.assertEqual(200, code)
            for m_idx in range(5):
                code, _, last_chat_state = srv.api(
                    "POST", f"/ce-lobby/v1/rooms/{rid}/chat", token=tmp_p["session_token"],
                    body={"text": f"p{p_idx}-m{m_idx}"},
                )
                self.assertEqual(200, code)
            code, _, _ = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/leave", token=tmp_p["session_token"], body={})
            self.assertEqual(200, code)
        self.assertEqual(32, len(last_chat_state["chat"]))
        self.assertEqual(40, last_chat_state["chat"][-1]["seq"])
        self.assertEqual(9, last_chat_state["chat"][0]["seq"])

        # Host leave closes room and removes it from directory.
        code, _, closed = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/leave", token=host["session_token"], body={})
        self.assertEqual(200, code)
        self.assertEqual("closed", closed["state"])
        code, _, _ = srv.api("POST", "/ce-lobby/v1/lobbies/resolve", token=host["session_token"], body={"code": room["code"]})
        self.assertEqual(404, code)

        # Confirm raw room password never appears in SQLite or logs.
        srv.stop()
        for path in [srv.log_path, *srv.state_dir.rglob("*")]:
            if path.is_file():
                self.assertNotIn(room_password.encode("utf-8"), path.read_bytes())

    def test_four_player_and_mixed_ai_match_start_and_finish(self):
        srv = self.make_server()
        _, h = srv.auth_player("HostZero", build_id="0.1.14")
        guests = [srv.auth_player(f"Wingman{i}", build_id="0.1.14")[1] for i in range(1, 4)]
        _, outsider = srv.auth_player("Spectator", build_id="0.1.14")

        code, _, room = srv.api(
            "POST", "/ce-lobby/v1/lobbies", token=h["session_token"],
            body={"name": "Four Player Brawl", "mode": "versus_2v2", "map": 850, "max_seats": 4, "ai_fill": False},
        )
        self.assertEqual(200, code)
        rid = room["room_id"]

        # Cannot start with < 2 humans.
        code, _, err = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/start", token=h["session_token"], body={})
        self.assertEqual(409, code)
        self.assertEqual("not_enough_players", err["error"])

        # Join 3 guests -> 4 humans total.
        for g in guests:
            code, _, _ = srv.api(
                "POST", f"/ce-lobby/v1/rooms/{rid}/join", token=g["session_token"],
                body={"password": "", "build_id": "0.1.14", "content_id": "", "be_cfg": "Aquila"},
            )
            self.assertEqual(200, code)

        # Refuse start while any guest is unready, and refuse non-host calling start.
        code, _, err = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/start", token=h["session_token"], body={})
        self.assertEqual(409, code)
        self.assertEqual("players_not_ready", err["error"])
        code, _, _ = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/start", token=guests[0]["session_token"], body={})
        self.assertEqual(403, code)

        for g in guests:
            code, _, _ = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/update", token=g["session_token"], body={"ready": True})
            self.assertEqual(200, code)

        # Host starts (host counts ready on Start).
        code, _, started = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/start", token=h["session_token"], body={})
        self.assertEqual(200, code)
        self.assertEqual("starting", started["state"])
        ticket = started["start_ticket"]
        self.assertTrue(ticket["match_id"].startswith("mt_"))
        self.assertGreater(ticket["seed"], 0)
        self.assertEqual(850, ticket["level"])
        self.assertEqual(64, len(ticket["roster_hash"]))
        self.assertEqual(64, len(ticket["match_secret"]))
        self.assertEqual(4, len(ticket["slots"]))
        self.assertEqual([0, 1, 2, 3], [s["seat"] for s in ticket["slots"]])
        self.assertEqual([0, 1, 2, 3], [s["sim_slot"] for s in ticket["slots"]])

        # match_secret must NEVER leak to non-member or directory listing.
        code, _, out_poll = srv.api("GET", f"/ce-lobby/v1/rooms/{rid}", token=outsider["session_token"])
        self.assertEqual(200, code)
        self.assertEqual("", out_poll["start_ticket"]["match_secret"])
        code, _, dir_list = srv.api("GET", "/ce-lobby/v1/lobbies", token=outsider["session_token"])
        self.assertEqual(200, code)
        self.assertNotIn(ticket["match_secret"], json.dumps(dir_list))

        # Finish returns to lobby, clears ticket and ready flags.
        code, _, finished = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/finish", token=h["session_token"], body={})
        self.assertEqual(200, code)
        self.assertEqual("lobby", finished["state"])
        self.assertIsNone(finished["start_ticket"])
        self.assertTrue(all(not s["ready"] for s in finished["seats"]))

        # Test 2 humans + 2 AI start.
        for g in guests[1:]:
            code, _, _ = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/leave", token=g["session_token"], body={})
            self.assertEqual(200, code)
        code, _, _ = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/update", token=h["session_token"], body={"ai_fill": True})
        self.assertEqual(200, code)
        code, _, _ = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/update", token=guests[0]["session_token"], body={"ready": True})
        self.assertEqual(200, code)
        code, _, started_ai = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/start", token=h["session_token"], body={})
        self.assertEqual(200, code)
        slots = started_ai["start_ticket"]["slots"]
        self.assertEqual(["human", "human", "ai", "ai"], [s["kind"] for s in slots])
        self.assertEqual([0, 1, 2, 3], [s["sim_slot"] for s in slots])

    def test_multi_guest_star_signaling_and_turn_modes_and_caps(self):
        # Part A: 4-player star ICE signaling + STUN+TURN vs relay-only + replay + fault handling.
        srv = self.make_server(daily_turn_limit=50, monthly_turn_limit=500)
        _, h = srv.auth_player("StarHost", build_id="0.1.14")
        guests = [srv.auth_player(f"StarGuest{i}", build_id="0.1.14")[1] for i in range(1, 4)]

        code, _, room = srv.api(
            "POST", "/ce-lobby/v1/lobbies", token=h["session_token"],
            body={"name": "Star Relay", "mode": "versus_2v2", "map": 850, "max_seats": 4, "require_relay": False},
        )
        self.assertEqual(200, code)
        rid = room["room_id"]
        for g in guests:
            self.assertEqual(200, srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/join", token=g["session_token"], body={})[0])
            self.assertEqual(200, srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/update", token=g["session_token"], body={"ready": True})[0])

        # TURN refused before match starts.
        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/turn", token=h["session_token"],
            body={"match_id": "mt_" + "0" * 16, "guest_seat": 1},
        )
        self.assertEqual(409, code)

        code, _, started = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/start", token=h["session_token"], body={})
        self.assertEqual(200, code)
        mid = started["start_ticket"]["match_id"]

        # Directed Host -> Guests 1,2,3 offers & Guests -> Host answers.
        for seat_idx in (1, 2, 3):
            code, _, sig = srv.api(
                "POST", f"/ce-lobby/v1/rooms/{rid}/signal", token=h["session_token"],
                body={"match_id": mid, "from_seat": 0, "to_seat": seat_idx, "kind": "offer",
                      "sdp": f"v=0\r\na=ice-ufrag:offer-{seat_idx}"},
            )
            self.assertEqual(200, code)
            self.assertGreater(sig["seq"], 0)

        # Negative controls: spoofed from_seat (403), guest->guest (400), oversized SDP (400), wrong match_id (409).
        self.assertEqual(403, srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/signal", token=guests[0]["session_token"],
            body={"match_id": mid, "from_seat": 0, "to_seat": 1, "kind": "offer", "sdp": "v=0"},
        )[0])
        self.assertEqual(400, srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/signal", token=guests[0]["session_token"],
            body={"match_id": mid, "from_seat": 1, "to_seat": 2, "kind": "offer", "sdp": "v=0"},
        )[0])
        self.assertEqual(400, srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/signal", token=h["session_token"],
            body={"match_id": mid, "from_seat": 0, "to_seat": 1, "kind": "offer", "sdp": "X" * 4097},
        )[0])
        self.assertEqual(409, srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/signal", token=h["session_token"],
            body={"match_id": "mt_" + "a" * 16, "from_seat": 0, "to_seat": 1, "kind": "offer", "sdp": "v=0"},
        )[0])

        # Each guest receives ONLY its own offer, then sends an answer back to host.
        for seat_idx, g in enumerate(guests, start=1):
            code, _, inbox = srv.api(
                "GET", f"/ce-lobby/v1/rooms/{rid}/signal?match_id={mid}&since=0", token=g["session_token"],
            )
            self.assertEqual(200, code)
            self.assertEqual(1, len(inbox["messages"]))
            self.assertEqual(seat_idx, inbox["messages"][0]["to_seat"])
            self.assertIn(f"offer-{seat_idx}", inbox["messages"][0]["sdp"])
            self.assertEqual(200, srv.api(
                "POST", f"/ce-lobby/v1/rooms/{rid}/signal", token=g["session_token"],
                body={"match_id": mid, "from_seat": seat_idx, "to_seat": 0, "kind": "answer",
                      "sdp": f"v=0\r\na=ice-ufrag:answer-{seat_idx}"},
            )[0])

        code, _, host_inbox = srv.api(
            "GET", f"/ce-lobby/v1/rooms/{rid}/signal?match_id={mid}&since=0", token=h["session_token"],
        )
        self.assertEqual(200, code)
        self.assertEqual([1, 2, 3], [m["from_seat"] for m in host_inbox["messages"]])
        code, _, host_inbox2 = srv.api(
            "GET", f"/ce-lobby/v1/rooms/{rid}/signal?match_id={mid}&since={host_inbox['seq']}", token=h["session_token"],
        )
        self.assertEqual(200, code)
        self.assertEqual([], host_inbox2["messages"])

        # Per-guest TURN (require_relay=False -> STUN + UDP TURN, no TCP TURN).
        code, _, host_turn = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/turn", token=h["session_token"],
            body={"match_id": mid, "guest_seat": 1},
        )
        self.assertEqual(200, code)
        self.assertEqual(3600, host_turn["ttl"])
        urls = [u for s in host_turn["iceServers"] for u in s["urls"]]
        self.assertIn("stun:stun.cloudflare.com:3478", urls)
        self.assertIn("turn:turn.cloudflare.com:3478?transport=udp", urls)
        self.assertIn("turn:turn.cloudflare.com:443?transport=udp", urls)
        self.assertFalse(any("transport=tcp" in u for u in urls))
        for entry in host_turn["iceServers"]:
            if entry["urls"][0].startswith("stun:"):
                self.assertNotIn("credential", entry)
            else:
                self.assertEqual(MOCK_TURN_USER, entry["username"])
                self.assertEqual(MOCK_TURN_CRED, entry["credential"])

        # Guest 1 can fetch for seat 1, CANNOT fetch for seat 2 (403).
        srv.set_provider_mode("combined_obj")
        code, _, combined_turn = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/turn", token=guests[0]["session_token"],
            body={"match_id": mid, "guest_seat": 1},
        )
        self.assertEqual(200, code)
        combined_urls = [u for s in combined_turn["iceServers"] for u in s["urls"]]
        self.assertEqual(["stun:stun.cloudflare.com:3478", "turn:turn.cloudflare.com:3478?transport=udp"], combined_urls)
        self.assertNotIn("credential", combined_turn["iceServers"][0])
        self.assertEqual(MOCK_TURN_CRED, combined_turn["iceServers"][1]["credential"])
        srv.set_provider_mode("ok")

        self.assertEqual(403, srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/turn", token=guests[0]["session_token"],
            body={"match_id": mid, "guest_seat": 2},
        )[0])
        self.assertEqual(2, srv.provider_calls())

        # Relay-only room (`require_relay=True`): returns ONLY UDP TURN, ZERO STUN!
        self.assertEqual(200, srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/finish", token=h["session_token"], body={})[0])
        self.assertEqual(200, srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/update", token=h["session_token"], body={"require_relay": True})[0])
        for g in guests:
            self.assertEqual(200, srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/update", token=g["session_token"], body={"ready": True})[0])
        code, _, started_relay = srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/start", token=h["session_token"], body={})
        self.assertEqual(200, code)
        mid2 = started_relay["start_ticket"]["match_id"]
        code, _, relay_turn = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/turn", token=guests[1]["session_token"],
            body={"match_id": mid2, "guest_seat": 2},
        )
        self.assertEqual(200, code)
        relay_urls = [u for s in relay_turn["iceServers"] for u in s["urls"]]
        self.assertFalse(any(u.startswith("stun:") for u in relay_urls))
        self.assertIn("turn:turn.cloudflare.com:3478?transport=udp", relay_urls)
        self.assertEqual(3, srv.provider_calls())

        # Upstream fault & master-token echo checks (zero token leak, fails closed 503).
        for bad_mode in ("echo", "redirect", "fail", "bad_url"):
            srv.set_provider_mode(bad_mode)
            code, _, err = srv.api(
                "POST", f"/ce-lobby/v1/rooms/{rid}/turn", token=guests[2]["session_token"],
                body={"match_id": mid2, "guest_seat": 3},
            )
            self.assertEqual(503, code, f"Expected 503 for provider mode {bad_mode}")
            self.assertNotIn(TURN_API_TOKEN, json.dumps(err))
        srv.set_provider_mode("ok")
        calls_before_replay = srv.provider_calls()

        # Atomic permit replay check: read spent permit from SQLite, replay directly to issuer -> 503, 0 new calls!
        srv.stop()
        rooms_in_db = srv.read_room_sqlite()
        self.assertEqual(1, len(rooms_in_db))
        spent_permits = [p["permit"] for p in rooms_in_db[0]["permits"].values() if p.get("spent")]
        self.assertGreaterEqual(len(spent_permits), 1)
        spent = spent_permits[0]
        raw_permit = canonical_json(spent).encode("utf-8")
        sig = base64.urlsafe_b64encode(
            hmac.digest(bytes.fromhex(PERMIT_KEY), b"Onslaught CE online lobby TURN permit v1\0" + raw_permit, "sha256")
        ).rstrip(b"=").decode("ascii")
        srv.start()
        code, _, _ = srv.raw_request(
            "POST", "/issue",
            body=json.dumps({"permit": spent, "signature": sig}).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            port=srv.issuer_port,
        )
        self.assertEqual(503, code)
        self.assertEqual(calls_before_replay, srv.provider_calls())

        # Fail closed when < 2 humans remain in an active match.
        for g in guests:
            self.assertEqual(200, srv.api("POST", f"/ce-lobby/v1/rooms/{rid}/leave", token=g["session_token"], body={})[0])
        code, _, _ = srv.api(
            "POST", f"/ce-lobby/v1/rooms/{rid}/turn", token=h["session_token"],
            body={"match_id": mid2, "guest_seat": 1},
        )
        self.assertEqual(409, code)
        self.assertEqual(calls_before_replay, srv.provider_calls())

        # Secret scan across workerd logs and SQLite files.
        srv.stop()
        for secret_bytes in (
            SESSION_SECRET.encode("utf-8"),
            PERMIT_KEY.encode("utf-8"),
            TURN_API_TOKEN.encode("utf-8"),
        ):
            for path in [srv.log_path, *srv.state_dir.rglob("*")]:
                if path.is_file():
                    self.assertNotIn(secret_bytes, path.read_bytes(), f"Secret leaked into {path.name}")

        # Part B: Daily TURN cap enforcement (CE_DAILY_TURN_LIMIT=2).
        srv_daily = self.make_server(daily_turn_limit=2, monthly_turn_limit=100)
        _, dh = srv_daily.auth_player("DailyHost")
        _, dg = srv_daily.auth_player("DailyGuest")
        drid = srv_daily.api("POST", "/ce-lobby/v1/lobbies", token=dh["session_token"],
                             body={"name": "DailyCap", "mode": "versus_2v2", "map": 850, "max_seats": 2})[2]["room_id"]
        srv_daily.api("POST", f"/ce-lobby/v1/rooms/{drid}/join", token=dg["session_token"], body={})
        srv_daily.api("POST", f"/ce-lobby/v1/rooms/{drid}/update", token=dg["session_token"], body={"ready": True})
        dmid = srv_daily.api("POST", f"/ce-lobby/v1/rooms/{drid}/start", token=dh["session_token"], body={})[2]["start_ticket"]["match_id"]
        self.assertEqual(200, srv_daily.api("POST", f"/ce-lobby/v1/rooms/{drid}/turn", token=dh["session_token"],
                                            body={"match_id": dmid, "guest_seat": 1})[0])
        self.assertEqual(200, srv_daily.api("POST", f"/ce-lobby/v1/rooms/{drid}/turn", token=dg["session_token"],
                                            body={"match_id": dmid, "guest_seat": 1})[0])
        self.assertEqual(2, srv_daily.provider_calls())
        # 3rd call hits daily cap -> 503, upstream NOT called!
        self.assertEqual(503, srv_daily.api("POST", f"/ce-lobby/v1/rooms/{drid}/turn", token=dh["session_token"],
                                            body={"match_id": dmid, "guest_seat": 1})[0])
        self.assertEqual(2, srv_daily.provider_calls())

        # Part C: Monthly TURN cap enforcement (CE_MONTHLY_TURN_LIMIT=2).
        srv_month = self.make_server(daily_turn_limit=100, monthly_turn_limit=2)
        _, mh = srv_month.auth_player("MonthHost")
        _, mg = srv_month.auth_player("MonthGuest")
        mrid = srv_month.api("POST", "/ce-lobby/v1/lobbies", token=mh["session_token"],
                             body={"name": "MonthCap", "mode": "versus_2v2", "map": 850, "max_seats": 2})[2]["room_id"]
        srv_month.api("POST", f"/ce-lobby/v1/rooms/{mrid}/join", token=mg["session_token"], body={})
        srv_month.api("POST", f"/ce-lobby/v1/rooms/{mrid}/update", token=mg["session_token"], body={"ready": True})
        mmid = srv_month.api("POST", f"/ce-lobby/v1/rooms/{mrid}/start", token=mh["session_token"], body={})[2]["start_ticket"]["match_id"]
        self.assertEqual(200, srv_month.api("POST", f"/ce-lobby/v1/rooms/{mrid}/turn", token=mh["session_token"],
                                            body={"match_id": mmid, "guest_seat": 1})[0])
        self.assertEqual(200, srv_month.api("POST", f"/ce-lobby/v1/rooms/{mrid}/turn", token=mg["session_token"],
                                            body={"match_id": mmid, "guest_seat": 1})[0])
        self.assertEqual(2, srv_month.provider_calls())
        self.assertEqual(503, srv_month.api("POST", f"/ce-lobby/v1/rooms/{mrid}/turn", token=mh["session_token"],
                                            body={"match_id": mmid, "guest_seat": 1})[0])
        self.assertEqual(2, srv_month.provider_calls())

    def test_heartbeat_pruning_and_ops_smoke_and_status(self):
        srv = self.make_server(heartbeat_timeout_ms=1200)
        _, h_alive = srv.auth_player("AliveHost")
        _, h_stale = srv.auth_player("StaleHost")

        r_alive = srv.api("POST", "/ce-lobby/v1/lobbies", token=h_alive["session_token"],
                          body={"name": "Alive Room", "mode": "versus_2v2", "map": 850, "max_seats": 4})[2]
        r_stale = srv.api("POST", "/ce-lobby/v1/lobbies", token=h_stale["session_token"],
                          body={"name": "Stale Room", "mode": "versus_2v2", "map": 851, "max_seats": 4})[2]

        # Keep r_alive heartbeating while r_stale goes silent past 1.2s.
        for _ in range(4):
            time.sleep(0.4)
            code, _, _ = srv.api("GET", f"/ce-lobby/v1/rooms/{r_alive['room_id']}?since=0", token=h_alive["session_token"])
            self.assertEqual(200, code)

        code, _, listing = srv.api("GET", "/ce-lobby/v1/lobbies", token=h_alive["session_token"])
        self.assertEqual(200, code)
        ids = [r["room_id"] for r in listing["rooms"]]
        self.assertIn(r_alive["room_id"], ids)
        self.assertNotIn(r_stale["room_id"], ids)

        code, _, _ = srv.api("POST", "/ce-lobby/v1/lobbies/resolve", token=h_alive["session_token"],
                             body={"code": r_stale["code"]})
        self.assertEqual(404, code)
        code, _, health = srv.api("GET", "/ce-lobby/v1/health")
        self.assertEqual(200, code)
        self.assertEqual(1, health["active_rooms"])

        # Run ops/status.py and ops/smoke_test.py end-to-end against real workerd!
        url = f"http://127.0.0.1:{srv.http_port}"
        status_proc = subprocess.run(
            [sys.executable, "-B", str(OPS_DIR / "status.py"), "--url", url, "--allow-loopback-http"],
            capture_output=True, text=True, check=False, timeout=15,
        )
        self.assertEqual(0, status_proc.returncode, status_proc.stderr)
        self.assertTrue(json.loads(status_proc.stdout)["ok"])

        smoke_proc = subprocess.run(
            [sys.executable, "-B", str(OPS_DIR / "smoke_test.py"), "--url", url,
             "--allow-loopback-http", "--check-turn"],
            capture_output=True, text=True, check=False, timeout=20,
        )
        self.assertEqual(0, smoke_proc.returncode, smoke_proc.stderr or smoke_proc.stdout)
        smoke_out = json.loads(smoke_proc.stdout)
        self.assertTrue(smoke_out["ok"])
        self.assertTrue(smoke_out["checked_turn"])
        self.assertGreaterEqual(smoke_out["turn_servers"], 2)


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
