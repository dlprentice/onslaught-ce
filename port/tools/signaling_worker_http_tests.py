#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real loopback workerd HTTP/SQLite controls using authored capabilities and a fake provider.

No dependency download, operator key lookup, Internet request or Cloudflare deployment is performed.
"""
from __future__ import annotations

import argparse
import base64
from contextlib import ExitStack
import hashlib
import hmac
import http.client
import json
import os
from pathlib import Path
import resource
import shutil
import socket
import sqlite3
import struct
import subprocess
import threading
import time
import tomllib
import unittest
import importlib.util

import ice_pilot_courier as wire
import signaling_api_contract as api
import signaling_pair_bindings as binding


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/services/closed_signaling"
KEYS = {"CE_ADMISSION_KEY": bytes([11]) * 32, "CE_IDENTITY_KEY": bytes([12]) * 32,
        "CE_PARCEL_KEY": bytes([13]) * 32, "CE_ISSUER_PERMIT_KEY": bytes([14]) * 32}
MASTER = "authored-provider-master-canary"
USER = "authored-turn-user-canary"
PASSWORD = "authored-turn-password-canary"
BUNDLES: Path | None = None

PROVIDER = r'''
import { DurableObject } from "cloudflare:workers";
export class ProviderStats extends DurableObject {
  async fetch(request) {
    if (new URL(request.url).pathname === "/fixture-stats") {
      return Response.json({ calls: await this.ctx.storage.get("calls") || 0 });
    }
    const count = (await this.ctx.storage.get("calls") || 0) + 1;
    await this.ctx.storage.put("calls", count);
    const url = new URL(request.url), value = await request.json();
    if (url.href !== "https://rtc.live.cloudflare.com/v1/turn/keys/authored-key/credentials/generate-ice-servers" ||
        request.headers.get("Authorization") !== "Bearer authored-provider-master-canary" ||
        Object.keys(value).join() !== "ttl" || !Number.isInteger(value.ttl) || value.ttl < 1 || value.ttl > 180) {
      return new Response(null, { status: 400 });
    }
    if (this.env.MODE === "fail") return new Response(null, { status: 503 });
    if (this.env.MODE === "slow") await new Promise(resolve => setTimeout(resolve, 2600));
    if (this.env.MODE === "hold") await new Promise(resolve => setTimeout(resolve, 20000));
    if (this.env.MODE === "redirect") return Response.redirect("https://elsewhere.invalid/", 302);
    const credential = this.env.MODE === "echo" ? "authored-provider-master-canary" : "authored-turn-password-canary";
    return Response.json({ iceServers: [
      { urls: ["stun:stun.cloudflare.com:3478"] },
      { urls: ["turn:turn.cloudflare.com:3478?transport=udp", "turn:turn.cloudflare.com:443?transport=udp",
               "turn:turn.cloudflare.com:3478?transport=tcp"], username: "authored-turn-user-canary", credential }
    ] }, { status: 201 });
  }
}
export default {
  async fetch(request, env) {
    return env.STATS.get(env.STATS.idFromName("authored-provider")).fetch(request);
  }
};
'''


def utc() -> int:
    return int(time.time())


def port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def make_record(role: str, start: int, attempt: bytes | None = None, end: int | None = None) -> bytes:
    """Structurally valid authored envelope; deliberately not authenticated by a game secret."""
    payload = ("authored-unverified-" + role).encode()
    return (b"CEICE002" + bytes([1 if role == "host" else 2]) + struct.pack("<QQ", start, end or start + 90)
            + (attempt or bytes(range(32))) + bytes(range(24)) + struct.pack("<H", len(payload))
            + payload + bytes(range(16)))


def request(role: str, context: str, *, end: int | None = None) -> dict:
    start = utc()
    return dict(schema=1, role=role, context=context, requestedAt=start,
                requestExpiresAt=end or start + 120, grantExpiresAt=start + 180)


class LoopbackTransport:
    def __init__(self, server: "Server"):
        self.server = server

    def send(self, value: api.Request) -> api.Response:
        connection = http.client.HTTPConnection("127.0.0.1", self.server.http_port, timeout=15)
        try:
            connection.request(value.method, value.path, value.body, dict(value.headers))
            reply = connection.getresponse()
            contract = {name.lower() for name, _ in api.HEADERS}
            headers = tuple((name, value) for name, value in reply.getheaders() if name.lower() in contract)
            return api.Response(reply.status, headers, reply.read(65537))
        finally:
            connection.close()


class Server:
    def __init__(self, runtime: Path, folder: Path, *, mode="ok", encryption_limit=10000, lifetime=900):
        self.runtime, self.folder = runtime, folder
        folder.mkdir(mode=0o700)
        self.http_port, self.stats_port, self.issuer_port = port(), port(), port()
        self.mode, self.encryption_limit = mode, encryption_limit
        self.keys = dict(KEYS)
        self.approval = "1" * 32
        self.host, self.guest = "p_" + "2" * 32 + "_host", "p_" + "3" * 32 + "_guest"
        self.start = utc()
        self.admission = dict(schema=1, approval=self.approval, host=self.host, guest=self.guest,
                              issuedAt=self.start, expiresAt=self.start + lifetime)
        self.process = None
        self.logs = []
        for source in ("worker.mjs", "issuer.mjs", "parcel_crypto.mjs"):
            selected = BUNDLES / source if BUNDLES and source != "parcel_crypto.mjs" else SOURCE / source
            shutil.copyfile(selected, folder / source)
        (folder / "provider.mjs").write_text(PROVIDER)
        (folder / "state").mkdir(mode=0o700)
        (folder / "home").mkdir(mode=0o700)
        (folder / "tmp").mkdir(mode=0o700)

    def token(self, role="host", *, subject=None, end=None) -> bytes:
        verifier = binding.PairCapabilityVerifier(self.keys["CE_ADMISSION_KEY"], maximum_seconds=900)
        return verifier.mint(subject or (self.host if role == "host" else self.guest), role,
                             self.start, end or self.start + 900)

    def variables(self) -> dict:
        return {**{k: v.hex() for k, v in self.keys.items()}, "CE_ENABLED": "true", "CE_LOCAL_HTTP": "true",
                "CE_CAPABILITY_SECONDS": "900", "CE_GRANT_SECONDS": "180",
                "CE_PARCEL_KEY_ID": hashlib.sha256(self.keys["CE_PARCEL_KEY"]).hexdigest(),
                "CE_ENCRYPTION_LIMIT": str(self.encryption_limit),
                "CE_APPROVED_PAIR": json.dumps(self.admission, sort_keys=True, separators=(",", ":"))}

    def configuration(self) -> str:
        def texts(values):
            return ",\n".join(f"(name = {json.dumps(k)}, text = {json.dumps(v)})" for k, v in values.items())

        variables = self.variables()
        issuer_values = {k: variables[k] for k in ("CE_ENABLED", "CE_GRANT_SECONDS", "CE_ISSUER_PERMIT_KEY")}
        issuer_values.update(TURN_KEY_ID="authored-key", TURN_API_TOKEN=MASTER)
        return f'''using Workerd = import "/workerd/workerd.capnp";
const config :Workerd.Config = (
 services = [
  (name = "public", worker = (
   modules = [(name = "worker.mjs", esModule = embed "worker.mjs"),
              (name = "parcel_crypto.mjs", esModule = embed "parcel_crypto.mjs")],
   compatibilityDate = "2026-11-02", globalOutbound = "denied",
   bindings = [{texts(variables)},
    (name = "CLOSED_PAIRS", durableObjectNamespace = "ClosedPairs"),
    (name = "TURN_ISSUER", service = "issuer")],
   durableObjectNamespaces = [(className = "ClosedPairs", uniqueKey = "ce-closed-pilot-test-v1", enableSql = true)],
   durableObjectStorage = (localDisk = "state")
  )),
  (name = "issuer", worker = (
   modules = [(name = "issuer.mjs", esModule = embed "issuer.mjs"),
              (name = "worker.mjs", esModule = embed "worker.mjs"),
              (name = "parcel_crypto.mjs", esModule = embed "parcel_crypto.mjs")],
   compatibilityDate = "2026-11-02", globalOutbound = "provider",
   bindings = [{texts(issuer_values)},
    (name = "PERMIT_AUTHORITY", service = (name = "public", entrypoint = "PermitAuthority"))]
  )),
  (name = "provider", worker = (
   modules = [(name = "provider.mjs", esModule = embed "provider.mjs")],
   compatibilityDate = "2026-11-02", globalOutbound = "denied",
   bindings = [(name = "MODE", text = {json.dumps(self.mode)}),
    (name = "STATS", durableObjectNamespace = "ProviderStats")],
   durableObjectNamespaces = [(className = "ProviderStats", uniqueKey = "ce-authored-provider-test-v1", enableSql = true)],
   durableObjectStorage = (localDisk = "state")
  )),
  (name = "state", disk = (path = {json.dumps(str(self.folder / 'state'))}, writable = true)),
  (name = "denied", network = (allow = []))
 ],
 sockets = [(name = "http", address = "127.0.0.1:{self.http_port}", http = (), service = "public"),
            (name = "stats", address = "127.0.0.1:{self.stats_port}", http = (), service = "provider"),
            (name = "private-fixture-issuer", address = "127.0.0.1:{self.issuer_port}", http = (), service = "issuer")]
);'''

    def start_server(self):
        assert self.process is None
        (self.folder / "config.capnp").write_text(self.configuration())
        log = self.folder / f"workerd-{len(self.logs)}.private.log"
        self.logs.append(log)
        handle = log.open("wb")
        env = {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": str(self.folder / "home"),
               "TMPDIR": str(self.folder / "tmp"), "XDG_CACHE_HOME": str(self.folder / "home/cache")}
        def no_core():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        self.process = subprocess.Popen([str(self.runtime), "serve", str(self.folder / "config.capnp")],
                                        cwd=self.folder, env=env, stdin=subprocess.DEVNULL,
                                        stdout=handle, stderr=subprocess.STDOUT, preexec_fn=no_core)
        handle.close()
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError("workerd refused the authored configuration; private log retained")
            try:
                with socket.create_connection(("127.0.0.1", self.http_port), timeout=0.1):
                    return self
            except OSError:
                time.sleep(0.03)
        raise AssertionError("workerd loopback listener did not become ready")

    def stop(self):
        if self.process is not None:
            if self.process.poll() is None:
                command = Path(f"/proc/{self.process.pid}/cmdline").read_bytes().split(b"\0")
                assert command[0] == os.fsencode(self.runtime)
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
            self.process = None

    def stats(self):
        connection = http.client.HTTPConnection("127.0.0.1", self.stats_port, timeout=2)
        try:
            connection.request("GET", "/fixture-stats")
            reply = connection.getresponse()
            assert reply.status == 200
            return json.loads(reply.read(1024))["calls"]
        finally:
            connection.close()

    def replay_private_permit(self, permit, signature):
        connection = http.client.HTTPConnection("127.0.0.1", self.issuer_port, timeout=2)
        try:
            connection.request("POST", "/issue", json.dumps(dict(permit=permit, signature=signature)),
                               {"Content-Type": "application/json"})
            reply = connection.getresponse(); reply.read(32769)
            return reply.status
        finally:
            connection.close()

    def call(self, route, body, *, role="host", token=None, method="POST", raw=None, extra_headers=None):
        token = self.token(role) if token is None else token
        headers = {"Authorization": "Bearer " + token.decode(), "Content-Type": "application/json"}
        headers.update(extra_headers or {})
        value = api.Request(method, api.PREFIX + route, tuple(headers.items()),
                            raw if raw is not None else json.dumps(body, sort_keys=True, separators=(",", ":")).encode())
        reply = LoopbackTransport(self).send(value)
        assert dict((k.lower(), v) for k, v in reply.headers).get("cache-control") == "no-store"
        return reply.code, json.loads(reply.body)

    def consent(self):
        for role in ("host", "guest"):
            assert self.call("pair/current", {}, role=role)[0] == 200
            assert self.call("pair/accept", {"approval": self.approval}, role=role)[0] == 200

    def admit(self, number):
        """Restart with a fresh approved pair, whose first Current activates it; close it once admitted."""
        self.approval = f"{number:032x}"
        self.host, self.guest = f"p_{2 * number:032x}_host", f"p_{2 * number + 1:032x}_guest"
        self.admission.update(approval=self.approval, host=self.host, guest=self.guest)
        self.start_server()
        try:
            code, _ = self.call("pair/current", {})
            if code == 200:
                assert self.call("pair/close", {"approval": self.approval})[1]["status"] == "closed"
        finally:
            self.stop()
        return code

    def stage_body(self, role, *, stage="lobby", context=None, end=None):
        return dict(approval=self.approval, stage=stage, afterAuthenticatedStart=stage == "game",
                    request=request(role, context or hashlib.sha256(b"authored-lobby").hexdigest(), end=end))

    def poll(self, role="host", stage="lobby"):
        return self.call("stage/poll", dict(approval=self.approval, stage=stage), role=role)

    def journal(self, *, change=None):
        assert self.process is None
        files = [p for p in (self.folder / "state/ce-closed-pilot-test-v1").glob("*.sqlite") if p.name != "metadata.sqlite"]
        assert len(files) == 1
        with sqlite3.connect(files[0]) as database:
            state = json.loads(database.execute("SELECT value FROM journal WHERE name='state'").fetchone()[0])
            if change:
                change(state)
                database.execute("UPDATE journal SET value=? WHERE name='state'", (json.dumps(state),))
        return state


class WorkerHttpTests(unittest.TestCase):
    runtime: Path
    output: Path

    def server(self, **options):
        server = Server(self.runtime, self.output / self._testMethodName, **options)
        self.addCleanup(server.stop)
        return server.start_server()

    def ready_pair(self, server):
        server.consent()
        host, guest = server.stage_body("host"), server.stage_body("guest")
        self.assertEqual("awaiting_peer_request", server.call("stage/request", host)[1]["status"])
        self.assertEqual("awaiting_peer_request", server.call("stage/request", guest, role="guest")[1]["status"])
        self.assertEqual(0, server.stats())
        self.assertEqual("exchanging", server.poll("guest")[1]["status"])
        return host, guest

    def test_deployment_templates_keep_the_issuer_private_and_sqlite_enabled(self):
        public = tomllib.loads((SOURCE / "wrangler.toml").read_text())
        issuer = tomllib.loads((SOURCE / "issuer.wrangler.toml").read_text())
        for config in (public, issuer, issuer["env"]["bootstrap"]):
            self.assertIs(config["workers_dev"], False)
            self.assertIs(config["preview_urls"], False)
            self.assertEqual("false", config["vars"]["CE_ENABLED"])
            self.assertFalse(config.get("routes"))
        self.assertEqual([], issuer["env"]["bootstrap"]["services"])
        self.assertEqual(issuer["name"], issuer["env"]["bootstrap"]["name"])
        self.assertEqual(["ClosedPairs"], public["migrations"][0]["new_sqlite_classes"])
        self.assertEqual("PermitAuthority", issuer["services"][0]["entrypoint"])

    def test_current_accept_two_stages_use_the_existing_private_client(self):
        server = self.server()
        clients = {role: api.Client(LoopbackTransport(server), server.token(role), utc) for role in ("host", "guest")}
        self.assertEqual("awaiting_pair_consent", clients["host"].current().status)
        clients["guest"].current()
        clients["host"].accept()
        self.assertEqual("admitted", clients["guest"].accept().status)
        records = []
        for name in ("lobby", "game"):
            context = hashlib.sha256(("authored-" + name).encode()).hexdigest()
            with ExitStack() as stack:
                stages, folders = {}, {}
                for role in ("host", "guest"):
                    folder = server.folder / f"{name}-{role}"
                    folder.mkdir(mode=0o700)
                    path = folder / f"relay-{role}.request.json"
                    path.write_text(json.dumps(request(role, context)))
                    path.chmod(0o600)
                    folders[role] = folder
                    stages[role] = stack.enter_context(clients[role].stage(name, folder, after_authenticated_start=name == "game"))
                stages["host"].request()
                stages["guest"].request()
                time.sleep(1.05)
                self.assertEqual("exchanging", stages["host"].poll().status)
                offer = make_record("host", utc())
                records.append(offer)
                path = folders["host"] / wire._record_name(context, "offer")
                path.write_bytes(offer); path.chmod(0o600)
                stages["host"].publish()
                time.sleep(1.05)
                stages["guest"].poll()
                answer = make_record("guest", utc(), end=struct.unpack_from("<Q", offer, 17)[0])
                records.append(answer)
                path = folders["guest"] / wire._record_name(context, "answer")
                path.write_bytes(answer); path.chmod(0o600)
                stages["guest"].publish()
                time.sleep(1.05)
                stages["host"].poll()
                time.sleep(1.05)
                self.assertEqual("service_delivery_complete", stages["guest"].poll().status)
        self.assertEqual(4, server.stats())
        server.stop()
        protected = [server.token("host"), server.token("guest"), server.host.encode(), server.guest.encode(),
                     MASTER.encode(), USER.encode(), PASSWORD.encode(), *records]
        for path in [*server.logs, *(server.folder / "state").rglob("*")]:
            if path.is_file():
                data = path.read_bytes()
                self.assertFalse(any(marker in data for marker in protected))

    def test_restart_redelivers_same_grant_without_issuing_again(self):
        server = self.server()
        _, guest = self.ready_pair(server)
        first = server.call("stage/request", guest, role="guest")[1]["grant"]
        self.assertEqual(1, server.stats())
        server.stop(); server.start_server()
        second = server.call("stage/request", guest, role="guest")[1]["grant"]
        self.assertEqual(first, second)
        self.assertEqual(1, server.stats())
        reordered = json.dumps(guest, separators=(",", ":")).encode()
        self.assertEqual(400, server.call("stage/request", guest, role="guest", raw=reordered)[0])
        guest["request"]["grantExpiresAt"] -= 1
        self.assertEqual(400, server.call("stage/request", guest, role="guest")[0])
        self.assertEqual(1, server.stats())

    def test_authentication_precedes_body_and_object_creation(self):
        server = self.server()
        self.assertEqual(401, server.call("pair/current", {}, token=b"invalid", raw=b"{")[0])
        self.assertFalse([p for p in (server.folder / "state/ce-closed-pilot-test-v1").glob("*.sqlite") if p.name != "metadata.sqlite"])
        unknown = server.token(subject="p_" + "9" * 32 + "_host")
        self.assertEqual(403, server.call("pair/current", {}, token=unknown, raw=b"{")[0])
        self.assertEqual(400, server.call("pair/current", {}, raw=b'{"x":1,"x":2}')[0])
        self.assertEqual(400, server.call("pair/current?x=1", {})[0])
        self.assertEqual(400, server.call("pair/current", {}, method="GET")[0])
        self.assertEqual(400, server.call("pair/current", {}, extra_headers={"Content-Type": "text/plain"})[0])
        self.assertEqual(0, server.stats())

    def test_two_roles_consent_context_and_stage_attestation(self):
        server = self.server()
        body = server.stage_body("host")
        self.assertEqual("awaiting_pair_consent", server.call("stage/request", body)[1]["status"])
        server.consent()
        self.assertEqual(200, server.call("stage/request", body)[0])
        wrong = server.stage_body("guest", context="e" * 64)
        self.assertEqual(400, server.call("stage/request", wrong, role="guest")[0])
        wrong = server.stage_body("host")
        self.assertEqual(400, server.call("stage/request", wrong, role="guest")[0])
        premature = server.stage_body("host", stage="game")
        self.assertEqual(400, server.call("stage/request", premature)[0])
        body["request"]["context"] = "f" * 64
        self.assertEqual(400, server.call("stage/request", body)[0])
        self.assertEqual(0, server.stats())

    def test_body_limit_and_duplicate_admission_fields_are_refused(self):
        server = self.server()
        self.assertEqual(400, server.call("pair/current", {}, raw=b"")[0])
        self.assertEqual(400, server.call("pair/current", {}, raw=b" " * 65537)[0])
        duplicate = ('{"approval":"' + server.approval + '","approval":"' + server.approval + '"}').encode()
        self.assertEqual(400, server.call("pair/accept", {}, raw=duplicate)[0])
        self.assertEqual("awaiting_pair_consent", server.call("pair/current", {})[1]["status"])
        self.assertEqual(0, server.stats())

    def test_poll_before_stage_or_own_request_does_not_spend_poll_slot(self):
        server = self.server()
        server.consent()
        for _ in range(2):
            self.assertEqual((200, dict(schema=1, status="awaiting_peer_request", role="host")), server.poll())
        host = server.stage_body("host")
        server.call("stage/request", host)
        for _ in range(2):
            self.assertEqual((200, dict(schema=1, status="awaiting_peer_request", role="guest",
                                       setupExpiresAt=host["request"]["requestExpiresAt"])), server.poll("guest"))
        self.assertEqual(0, server.stats())
        server.stop()
        state = server.journal()
        self.assertEqual({}, state["pairs"][server.approval]["stages"]["lobby"]["polls"])
        server.journal(change=lambda state: state["pairs"][server.approval]["stages"]["lobby"].update(deadline=utc() - 1))
        server.start_server()
        self.assertEqual("setup_expired", server.poll("guest")[1]["status"])
        self.assertEqual(0, server.stats())

    def test_record_role_attempt_and_acknowledgement_gates(self):
        server = self.server()
        self.ready_pair(server)
        self.assertEqual("exchanging", server.poll("host")[1]["status"])
        offer = make_record("host", utc(), end=utc() + 2)
        def upload(role, data):
            return server.call("stage/record", dict(approval=server.approval, stage="lobby",
                               record=base64.b64encode(data).decode()), role=role)
        self.assertEqual(200, upload("host", offer)[0])
        answer = make_record("guest", utc(), end=struct.unpack_from("<Q", offer, 17)[0])
        self.assertEqual(400, upload("guest", answer)[0])
        digest = hashlib.sha256(offer).hexdigest()
        self.assertEqual(200, server.call("stage/ack", dict(approval=server.approval, stage="lobby", digest=digest), role="guest")[0])
        changed = make_record("guest", utc(), attempt=bytes([19]) * 32, end=struct.unpack_from("<Q", offer, 17)[0])
        self.assertEqual(400, upload("guest", changed)[0])
        self.assertEqual(400, upload("guest", bytes(range(128, 136)) + answer[8:])[0])
        self.assertEqual(400, server.call("stage/ack", dict(approval=server.approval, stage="lobby", digest="0" * 64), role="guest")[0])
        time.sleep(2.1)
        self.assertNotIn("record", server.poll("guest")[1])
        self.assertEqual(2, server.stats())

    def test_provider_error_echo_and_redirect_burn_the_permit(self):
        for mode in ("fail", "echo", "redirect"):
            with self.subTest(mode=mode):
                server = Server(self.runtime, self.output / (self._testMethodName + "-" + mode), mode=mode)
                self.addCleanup(server.stop); server.start_server(); server.consent()
                server.call("stage/request", server.stage_body("host"))
                guest = server.stage_body("guest")
                self.assertEqual("awaiting_peer_request", server.call("stage/request", guest, role="guest")[1]["status"])
                self.assertEqual("issuance_unknown", server.poll("guest")[1]["status"])
                server.stop(); server.mode = "ok"; server.start_server()
                self.assertEqual("issuance_unknown", server.call("stage/request", guest, role="guest")[1]["status"])
                self.assertEqual(1, server.stats()); server.stop()

    def test_late_issuer_does_not_extend_the_original_setup_window(self):
        server = self.server(mode="slow")
        server.consent()
        deadline = utc() + 2
        server.call("stage/request", server.stage_body("host", end=deadline))
        server.call("stage/request", server.stage_body("guest", end=deadline), role="guest")
        self.assertEqual("setup_expired", server.poll("guest")[1]["status"])
        self.assertEqual(1, server.stats())
        server.stop()
        state = server.journal()
        stage = state["pairs"][server.approval]["stages"]["lobby"]
        self.assertEqual(deadline, stage["deadline"])
        self.assertFalse(any(p.get("grant") for p in stage["permits"].values()))

    def test_reserved_restart_preserves_lease_then_becomes_unknown(self):
        server = self.server(mode="hold")
        server.consent()
        server.call("stage/request", server.stage_body("host"))
        guest = server.stage_body("guest")
        server.call("stage/request", guest, role="guest")
        def pending():
            try:
                server.poll("guest")
            except (OSError, http.client.HTTPException):
                pass
        thread = threading.Thread(target=pending, daemon=True)
        thread.start()
        limit = time.monotonic() + 5
        while server.stats() == 0 and time.monotonic() < limit:
            time.sleep(0.03)
        self.assertEqual(1, server.stats())
        server.stop(); thread.join(timeout=2)
        server.mode = "ok"; server.start_server()
        time.sleep(1.05)
        self.assertEqual("issuing", server.poll("guest")[1]["status"])
        time.sleep(10.1)
        self.assertEqual("issuance_unknown", server.poll("guest")[1]["status"])
        self.assertEqual(1, server.stats())

    def test_key_budget_and_changed_key_fail_closed(self):
        server = self.server(encryption_limit=1)
        server.consent()
        self.assertEqual(200, server.call("stage/request", server.stage_body("host"))[0])
        self.assertEqual(503, server.call("stage/request", server.stage_body("guest"), role="guest")[0])
        self.assertEqual(0, server.stats())
        server.stop(); server.keys["CE_PARCEL_KEY"] = bytes([15]) * 32; server.start_server()
        self.assertEqual(503, server.call("pair/current", {})[0])
        self.assertEqual(0, server.stats())

    def test_stored_request_ciphertext_and_metadata_are_verified_before_issuance(self):
        for kind in ("ciphertext", "metadata"):
            with self.subTest(kind=kind):
                server = Server(self.runtime, self.output / (self._testMethodName + "-" + kind))
                self.addCleanup(server.stop); server.start_server(); server.consent()
                server.call("stage/request", server.stage_body("host"))
                server.call("stage/request", server.stage_body("guest"), role="guest")
                self.assertEqual(0, server.stats()); server.stop()
                def corrupt(state):
                    stored = state["pairs"][server.approval]["stages"]["lobby"]["requests"]["host"]
                    if kind == "ciphertext":
                        data = bytearray(base64.b64decode(stored["payload"]))
                        data[-1] ^= 1
                        stored["payload"] = base64.b64encode(data).decode()
                    else:
                        stored["grantEnd"] += 1
                server.journal(change=corrupt)
                server.start_server()
                self.assertEqual(503, server.poll("host")[0])
                self.assertEqual(0, server.stats()); server.stop()
    def test_close_remains_available_after_the_durable_call_budget(self):
        server = self.server()
        server.consent()
        server.stop()
        server.journal(change=lambda state: state["pairs"][server.approval].update(calls=1024))
        server.start_server()
        self.assertEqual(429, server.call("pair/current", {})[0])
        self.assertEqual((200, dict(schema=1, status="closed", role="host")),
                         server.call("pair/close", {"approval": server.approval}))
        self.assertEqual((200, dict(schema=1, status="closed", role="host")), server.call("pair/current", {}, raw=b""))
        self.assertEqual((200, dict(schema=1, status="closed", role="guest")),
                         server.call("stage/poll", {}, role="guest", raw=b"{"))
        self.assertEqual(0, server.stats())
        server.stop()
        server.journal(change=lambda state: state["pairs"][server.approval].update(expiresAt=utc() - 1))
        server.start_server()
        self.assertEqual((200, dict(schema=1, status="closed", role="host")), server.call("pair/current", {}, raw=b"{"))
        server.stop()
        self.assertEqual(1024, server.journal()["pairs"][server.approval]["calls"])

    def test_expired_pair_precedes_budget_and_body_and_preserves_role(self):
        server = self.server(lifetime=3)
        server.consent(); server.stop()
        server.journal(change=lambda state: state["pairs"][server.approval].update(calls=1024))
        server.start_server()
        time.sleep(max(0, server.admission["expiresAt"] - time.time() + 0.1))
        for role, raw in (("host", b"{"), ("guest", b"")):
            self.assertEqual((200, dict(schema=1, status="expired", role=role)),
                             server.call("pair/current", {}, role=role, raw=raw))
        self.assertEqual(0, server.stats())
        server.stop()
        self.assertEqual(1024, server.journal()["pairs"][server.approval]["calls"])

    def test_private_issuer_rejects_replayed_or_changed_spending_permits(self):
        server = self.server()
        self.ready_pair(server)
        self.assertEqual(1, server.stats())
        server.stop()
        permit = server.journal()["pairs"][server.approval]["stages"]["lobby"]["permits"]["guest"]["permit"]
        raw = json.dumps(permit, sort_keys=True, separators=(",", ":")).encode()
        signature = base64.urlsafe_b64encode(hmac.digest(KEYS["CE_ISSUER_PERMIT_KEY"],
                    b"Onslaught CE issuer permit v1\0" + raw, "sha256")).rstrip(b"=").decode()
        server.start_server()
        self.assertEqual(503, server.replay_private_permit(permit, signature))
        permit["role"] = "host"
        self.assertEqual(503, server.replay_private_permit(permit, signature))
        self.assertEqual(1, server.stats())

    def test_global_reservations_survive_failed_activation_and_daily_ceiling(self):
        server = self.server()
        server.consent(); server.stop()
        def interrupted_activation(state):
            state["admissions"][server.approval]["activated"] = False
            del state["pairs"][server.approval]
        server.journal(change=interrupted_activation)
        server.start_server()
        self.assertEqual(503, server.call("pair/current", {})[0])
        server.stop()
        self.assertEqual(4, next(iter(server.journal()["budgets"].values()))["permits"])
        # The burned reservation remains the day's first approval: 23 more are admitted and the 25th is refused.
        for number in range(2, 26):
            self.assertEqual(200 if number <= 24 else 503, server.admit(number))
        self.assertEqual(96, next(iter(server.journal()["budgets"].values()))["permits"])

    def test_each_approval_reserves_four_permits_up_to_the_24th_and_the_25th_reserves_nothing(self):
        server = Server(self.runtime, self.output / self._testMethodName)
        self.addCleanup(server.stop)
        for number in range(1, 25):
            self.assertEqual(200, server.admit(number))
            state = server.journal()
            self.assertEqual([dict(approvals=number, permits=4 * number)], list(state["budgets"].values()))
            self.assertTrue(state["admissions"][server.approval]["activated"])
        before = server.journal()
        self.assertEqual(503, server.admit(25))
        after = server.journal()
        self.assertEqual(before, after)
        self.assertEqual([dict(approvals=24, permits=96)], list(after["budgets"].values()))
        self.assertNotIn(server.approval, after["admissions"])
        self.assertNotIn(server.approval, after["pairs"])

    def test_a_new_utc_day_starts_with_an_empty_budget(self):
        server = Server(self.runtime, self.output / self._testMethodName)
        self.addCleanup(server.stop)
        self.assertEqual(200, server.admit(1))
        [today] = server.journal()["budgets"]
        self.assertEqual(str(utc() // 86400), today)
        full = dict(approvals=24, permits=96)
        server.journal(change=lambda state: state["budgets"].update({today: full}))
        self.assertEqual(503, server.admit(2))
        def next_day(state):
            # The full budget becomes yesterday's, beside an older day that the next entry prunes.
            day = int(today)
            state["budgets"] = {str(day - 2): dict(full), str(day - 1): state["budgets"][today]}
        server.journal(change=next_day)
        self.assertEqual(200, server.admit(2))
        self.assertEqual({str(int(today) - 1): full, today: dict(approvals=1, permits=4)}, server.journal()["budgets"])

    def test_each_daily_ceiling_refuses_on_its_own(self):
        # Budgets this code writes hold four permits per approval, so both ceilings coincide; seeded budgets separate them.
        server = Server(self.runtime, self.output / self._testMethodName)
        self.addCleanup(server.stop)
        self.assertEqual(200, server.admit(1))
        [today] = server.journal()["budgets"]
        for number, seeded, code, result in ((2, dict(approvals=24, permits=0), 503, dict(approvals=24, permits=0)),
                                             (3, dict(approvals=0, permits=93), 503, dict(approvals=0, permits=93)),
                                             (4, dict(approvals=0, permits=92), 200, dict(approvals=1, permits=96))):
            with self.subTest(budget=seeded):
                server.journal(change=lambda state, seeded=seeded: state["budgets"].update({today: seeded}))
                self.assertEqual(code, server.admit(number))
                self.assertEqual({today: result}, server.journal()["budgets"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()
    pins = json.loads((SOURCE / "runtime.pin.json").read_text())
    runtime = options.inputs.resolve() / "runtime/package/bin/workerd"
    if runtime.stat().st_size != pins["binary_bytes"] or hashlib.file_digest(runtime.open("rb"), "sha256").hexdigest() != pins["binary_sha256"]:
        raise SystemExit("workerd input differs from the reviewed runtime pin")
    license_path = options.inputs.resolve() / "LICENSE.workerd.txt"
    if hashlib.sha256(license_path.read_bytes()).hexdigest() != pins["license_sha256"]:
        raise SystemExit("workerd license input differs from its pin")
    output = options.output.absolute()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location("signaling_bundle", SOURCE / "bundle.py")
    bundler = importlib.util.module_from_spec(spec); spec.loader.exec_module(bundler)
    global BUNDLES
    BUNDLES = output / "bundles"
    bundler.bundle(BUNDLES)
    WorkerHttpTests.runtime, WorkerHttpTests.output = runtime, output
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(WorkerHttpTests))
    receipt = dict(runtime_sha256=pins["binary_sha256"], tests=result.testsRun,
                   failures=len(result.failures), errors=len(result.errors), skipped=len(result.skipped),
                   public_provider_calls=0, deployment=False, native_game_authentication=False,
                   bundles=json.loads((BUNDLES / "bundles.json").read_text()),
                   source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCE.glob("*.mjs")})
    (output / "receipt.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return 0 if result.wasSuccessful() and not result.skipped else 1


if __name__ == "__main__":
    raise SystemExit(main())
