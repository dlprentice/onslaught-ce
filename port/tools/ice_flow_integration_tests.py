#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Owned loopback-only CE processes with actual courier and sealed-store records.

This is a synthetic session-API integration, not a game or hosted service.
The caller must isolate the network and provide the private synthetic hosts
mapping. No provider credential loader or external issuer is called.
"""
from __future__ import annotations

import argparse
import base64
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import time
from unittest.mock import patch

import ice_pilot_courier as wire
import signaling_session_store as service

ROOT = Path(__file__).resolve().parents[2]
USER, PASSWORD = "invented-ice-flow-user", "invented-ice-flow-password"
HOST, GUEST = b"invented-flow-host-token", b"invented-flow-guest-token"
ALLOWED = frozenset(("RELAY_READY", "RELAY_STOPPED", "DIRECT_READY", "PEER_READY",
                     "AGENT_CREATED", "ACTIVE_DIRECT", "ACTIVE_RELAY", "TRAFFIC_DIRECT",
                     "TRAFFIC_RELAY", "TERMINAL_PEER_LEFT", "TERMINAL_TIMEOUT",
                     "TERMINAL_REFUSED", "PEER_STOPPED"))


def check(condition: bool, label: str) -> None:
    if not condition:
        raise ValueError("offline CE flow assertion: " + label)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def private_directory(path: Path) -> Path:
    path.mkdir(mode=0o700, parents=True, exist_ok=False)
    return path


def wait(predicate, label: str, seconds: float = 8) -> None:
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(0.01)
    raise ValueError("offline CE flow deadline: " + label)


def offline_guard() -> None:
    check([name for _, name in socket.if_nameindex()] == ["lo"], "only loopback interface is enabled")
    endpoints = {row[4][0] for row in socket.getaddrinfo("turn.cloudflare.com", 3478, socket.AF_INET, socket.SOCK_DGRAM)}
    check(endpoints == {"127.0.0.1"}, "fixed hostname resolves only to owned loopback")
    check(not any(os.environ.get(name) for name in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")),
          "desktop and session bus absent")


class Child:
    def __init__(self, binary: Path, directory: Path, *arguments: str):
        check(hasattr(os, "pidfd_open") and hasattr(signal, "pidfd_send_signal"), "Linux pidfd ownership support")
        self.directory = private_directory(directory)
        self.stderr = (directory / "stderr.private.log").open("wb")
        self.lines: list[str] = []
        self.raw = b""
        self.tail = b""
        self.child = None
        self.pidfd = None
        self.identity = None
        self.closed = False
        command = [str(binary), *map(str, arguments)]
        self.expected_cmdline = b"\0".join(os.fsencode(value) for value in command) + b"\0"
        try:
            self.child = subprocess.Popen(command, cwd=directory, stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, stderr=self.stderr)
            self.pidfd = os.pidfd_open(self.child.pid)
            self.identity = self._identity()
            check(self.identity[1] == self.expected_cmdline, "exact spawned child command line")
        except BaseException:
            # A failure after Popen still owns a child and both pipe ends.
            self.close()
            raise

    def _identity(self):
        directory = Path("/proc") / str(self.child.pid)
        fields = (directory / "stat").read_bytes().rsplit(b") ", 1)[1].split()
        return fields[19], (directory / "cmdline").read_bytes()

    def _signal_owned(self, number: int) -> bool:
        if self.child.poll() is not None:
            return False
        check(self.pidfd is not None and self.identity is not None,
              "signal requires previously pinned child identity")
        current = self._identity()
        check(current == self.identity and current[1] == self.expected_cmdline,
              "exact owned command line and process start time before signal")
        # The pidfd targets the original process even if its numeric PID is reused.
        signal.pidfd_send_signal(self.pidfd, number)
        return True

    def pump(self) -> None:
        while select.select([self.child.stdout], [], [], 0)[0]:
            data = os.read(self.child.stdout.fileno(), 4096)
            if not data:
                break
            self.raw += data
            check(len(self.raw) <= 65536, "bounded fixture status stream")
            self.tail += data
            while b"\n" in self.tail:
                line, self.tail = self.tail.split(b"\n", 1)
                text = line.decode("ascii")
                check(text in ALLOWED, "only fixed fixture milestones are printed")
                self.lines.append(text)
            (self.directory / "stdout.private.log").write_bytes(self.raw)

    def has(self, marker: str) -> bool:
        self.pump()
        check(self.child.poll() is None or self.child.returncode == 0, "owned child remains healthy")
        return marker in self.lines

    def expect(self, marker: str) -> None:
        wait(lambda: self.has(marker), marker)

    def send(self, command: str) -> None:
        check(command in ("stop", "close", "expire"), "fixed fixture command")
        check(self.child.poll() is None, "command targets live owned process")
        self.child.stdin.write((command + "\n").encode("ascii"))
        self.child.stdin.flush()

    def close(self) -> None:
        if self.closed:
            return
        failures, forced, collected = [], False, self.child is None
        try:
            if self.child is not None:
                if self.child.poll() is None:
                    try:
                        self.child.stdin.write(b"stop\n")
                        self.child.stdin.flush()
                    except (BrokenPipeError, OSError):
                        pass
                try:
                    self.child.wait(timeout=5)
                    collected = True
                except subprocess.TimeoutExpired:
                    forced = True
                    for number in (signal.SIGTERM, signal.SIGKILL):
                        try:
                            self._signal_owned(number)
                            self.child.wait(timeout=2)
                            collected = True
                            break
                        except subprocess.TimeoutExpired:
                            continue
                        except (OSError, ValueError, IndexError):
                            failures.append("owned_identity_or_signal_refused")
                            break
                if collected:
                    try:
                        self.pump()
                    except (OSError, ValueError):
                        failures.append("status_stream_refused")
                    if self.child.returncode != 0:
                        failures.append("child_exit_nonzero")
                else:
                    failures.append("child_not_collected")
        finally:
            # Close every owned handle even when status validation or wait fails.
            try:
                self.stderr.close()
            except OSError:
                failures.append("stderr_close_failed")
            if self.child is not None:
                for stream in (self.child.stdin, self.child.stdout):
                    if stream is not None:
                        try:
                            stream.close()
                        except OSError:
                            failures.append("pipe_close_failed")
            if self.pidfd is not None:
                try:
                    os.close(self.pidfd)
                except OSError:
                    failures.append("pidfd_close_failed")
                self.pidfd = None
            self.closed = True
            (self.directory / "cleanup.private.json").write_text(json.dumps(
                {"collected": collected, "forced": forced, "failures": failures}, indent=2) + "\n")
        check(not forced and collected and not failures, "all owned children exit cleanly without forced cleanup")


class Pair:
    def __init__(self, binary: Path, root: Path):
        self.binary, self.root = binary, private_directory(root)
        self.paths = {}
        for role in ("host", "guest"):
            private_directory(root / role)
            for stage in ("lobby", "game"):
                self.paths[role, stage] = private_directory(root / role / stage)
        self.peers: dict[str, Child] = {}
        self.stage = ""

    def start(self, stage: str, mismatch: bool = False) -> None:
        check(not self.peers, "each phase uses fresh processes")
        self.stage = stage
        for role in ("host", "guest"):
            mode = "context-mismatch" if mismatch and role == "guest" else "ice"
            self.peers[role] = Child(self.binary, self.root / (stage + "-" + role + "-process"),
                                     role, stage, str(self.paths[role, "lobby"]),
                                     str(self.paths[role, "game"]), mode, "authored-only")
        check(self.peers["host"].child.pid != self.peers["guest"].child.pid, "two independent CE processes")
        a, b = (self.paths[role, stage].stat() for role in ("host", "guest"))
        check((a.st_dev, a.st_ino) != (b.st_dev, b.st_ino), "physically distinct host and guest directories")
        for peer in self.peers.values():
            peer.expect("PEER_READY")
        for role in self.peers:
            self.request(role)

    def directory(self, role: str) -> Path:
        return self.paths[role, self.stage]

    def request(self, role: str) -> dict:
        path = self.directory(role) / ("relay-" + role + ".request.json")
        wait(path.is_file, "actual preparation request")
        return wire._request(path.read_bytes(), role, int(time.time()))

    def record(self, role: str) -> Path:
        context = self.request(role)["context"]
        return self.directory(role) / wire._record_name(context, "offer" if role == "host" else "answer")

    def traffic(self) -> None:
        for peer in self.peers.values():
            peer.expect("TRAFFIC_RELAY")
            check(peer.lines.count("AGENT_CREATED") == 1, "one actual ICE agent per peer")
            check("ACTIVE_DIRECT" not in peer.lines, "selected pairs remain relayed")

    def expire(self) -> None:
        for peer in self.peers.values():
            peer.pump()
            if not any(line.startswith("TERMINAL_") for line in peer.lines):
                peer.send("expire")
                peer.expect("TERMINAL_TIMEOUT")
            check("ACTIVE_RELAY" not in peer.lines and "ACTIVE_DIRECT" not in peer.lines,
                  "refused preparation never admits application traffic")

    def close(self) -> None:
        peers, self.peers = self.peers, {}
        failures = 0
        for peer in peers.values():
            try:
                peer.close()
            except BaseException:
                failures += 1
        check(failures == 0, "every recorded peer cleanup attempted without failure")


def publish(directory: Path, name: str, data: bytes) -> None:
    fd = wire.private._open_directory(directory)
    try:
        wire._publish(fd, name, data)
    finally:
        os.close(fd)


def grant(pair: Pair, role: str) -> dict:
    request = pair.request(role)
    now = int(time.time())
    result = {"schema": 1, "role": role, "context": request["context"],
              "issuedAt": now, "expiresAt": min(now + 300, request["grantExpiresAt"]),
              "iceServers": [{"urls": ["turn:turn.cloudflare.com:3478?transport=udp"],
                              "username": USER, "credential": PASSWORD}]}
    wire._grant(wire._json(result), request, now)
    return result


def courier_move(pair: Pair, kind: str, root: Path, tamper: bool = False) -> None:
    directory = private_directory(root)
    source, destination = ("guest", "host") if kind in ("request", "answer") else ("host", "guest")
    wire.transfer("export", kind, pair.directory(source), directory)
    if tamper:
        path = directory / wire.PARCEL
        parcel = json.loads(path.read_bytes())
        name = wire._record_name(parcel["context"], "offer")
        record = bytearray(base64.b64decode(parcel["records"][name]))
        record[-1] ^= 1
        parcel["records"][name] = base64.b64encode(record).decode("ascii")
        path.write_bytes(wire._json(parcel))
    wire.transfer("import", kind, pair.directory(destination), directory)


def courier_prepare(pair: Pair) -> None:
    courier_move(pair, "request", pair.root / (pair.stage + "-request-parcel"))
    for role in ("host", "guest"):
        publish(pair.directory("host"), "relay-" + role + ".json", wire._json(grant(pair, role)))
    wait(pair.record("host").is_file, "actual encrypted host offer")


class Verifier:
    def verify(self, token: bytes, now: int):
        subject = {HOST: "invented_flow_host", GUEST: "invented_flow_guest",
                   b"other": "invented_flow_other", b"wrong-audience": "invented_flow_host",
                   b"expired": "invented_flow_host"}.get(token)
        if subject is None:
            return None
        return service.Principal(subject, now - 10, now - 1 if token == b"expired" else now + 3600,
                                 audience="wrong" if token == b"wrong-audience" else service.AUDIENCE)


class Issuer:
    def __init__(self):
        self.calls = []

    def issue(self, request):
        self.calls.append((request.stage, request.role))
        return wire._json({"iceServers": [{"urls": ["turn:turn.cloudflare.com:3478?transport=udp"],
                                          "username": USER, "credential": PASSWORD}]})


class StoreFlow:
    def __init__(self, primitive, root: Path):
        self.directory = private_directory(root)
        self.issuer = Issuer()
        binding = hashlib.sha256(b"invented flow identity binding").digest()
        storage = hashlib.sha256(b"invented separate flow storage key").digest()
        self.store = service.SessionStore(self.directory, Verifier(), self.issuer, lambda: int(time.time()),
                                          binding, service.parcels.ParcelCipher(primitive, storage))
        self.approval = self.store.approve_pair("invented_flow_host", "invented_flow_guest", int(time.time()) + 3600)
        check(self.call("pair/accept", HOST).status == "awaiting_pair_consent", "host consents to approved pair")
        check(self.call("pair/accept", GUEST).status == "admitted", "guest consents to approved pair")

    def call(self, route: str, token: bytes, body: dict | None = None):
        return self.store.dispatch(route, token, wire._json({"approval": self.approval, **(body or {})}))

    def request_body(self, pair: Pair, role: str) -> dict:
        return {"stage": pair.stage, "request": pair.request(role), "afterAuthenticatedStart": pair.stage == "game"}

    def ingress_controls(self, pair: Pair) -> None:
        body = self.request_body(pair, "host")
        for token, result in ((b"unknown", "authentication_required"), (b"wrong-audience", "authentication_required"),
                              (b"expired", "authentication_required"), (b"other", "not_admitted")):
            check(self.call("stage/request", token, body).status == result, "fabricated ingress authentication refusal")
        check(self.call("stage/request", HOST, self.request_body(pair, "guest")).status == "invalid_request",
              "trusted identity cannot submit the other role's actual request")
        check(self.call("stage/request", HOST, body | {"subject": "invented_flow_guest"}).status == "invalid_request",
              "body cannot replace trusted identity")
        check(not self.issuer.calls, "rejected authentication makes zero issuer calls")

    def prepare(self, pair: Pair) -> None:
        for token, role in ((HOST, "host"), (GUEST, "guest")):
            check(self.call("stage/request", token, self.request_body(pair, role)).status == "awaiting_peer_request",
                  "store consumes actual process request")
        for token, role in ((HOST, "host"), (GUEST, "guest")):
            reply = self.call("stage/poll", token, {"stage": pair.stage})
            check(reply.status == "exchanging" and reply.grant is not None, "one role-bound fabricated issuance")
            wire._grant(wire._json(reply.grant), pair.request(role), int(time.time()))
            publish(pair.directory(role), "relay-" + role + ".json", wire._json(reply.grant))

    def poll_record(self, token: bytes, stage: str):
        result = None
        last = -1
        def ready():
            nonlocal result, last
            now = int(time.time())
            if now == last:
                return False
            last = now
            result = self.call("stage/poll", token, {"stage": stage})
            return result.record is not None
        wait(ready, "bounded actual stored-record delivery")
        return result

    def exchange(self, pair: Pair) -> None:
        wait(pair.record("host").is_file, "actual store-bound offer")
        offer = pair.record("host").read_bytes()
        check(self.call("stage/record", HOST, {"stage": pair.stage, "record": base64.b64encode(offer).decode("ascii")}).status == "exchanging",
              "store accepts real encrypted host offer")
        reply = self.poll_record(GUEST, pair.stage)
        check(reply.record == offer, "store returns identical offer bytes")
        publish(pair.directory("guest"), pair.record("host").name, reply.record)
        wait(pair.record("guest").is_file, "CE guest authenticates offer and creates answer")
        check(self.call("stage/ack", GUEST, {"stage": pair.stage, "digest": reply.record_digest}).status == "exchanging",
              "offer acknowledged after actual consumer response")
        answer = pair.record("guest").read_bytes()
        check(self.call("stage/record", GUEST, {"stage": pair.stage, "record": base64.b64encode(answer).decode("ascii")}).status == "exchanging",
              "store accepts real encrypted guest answer")
        reply = self.poll_record(HOST, pair.stage)
        check(reply.record == answer, "store returns identical answer bytes")
        publish(pair.directory("host"), pair.record("guest").name, reply.record)
        pair.traffic()
        check(self.call("stage/ack", HOST, {"stage": pair.stage, "digest": reply.record_digest}).status == "service_delivery_complete",
              "store completes after actual CE admission and traffic")


def run(build: Path, output: Path) -> None:
    offline_guard()
    receipt = json.loads((build / "build-receipt.json").read_bytes())
    binary, library = build / "ice-flow-peer", build / "parcel-crypto.so"
    check(digest(binary) == receipt["binary_sha256"] and digest(library) == receipt["library_sha256"],
          "frozen binary and crypto library identities")
    for name, expected in receipt["inputs"].items():
        check(digest(ROOT / name) == expected, "frozen source identities")
    output = output.resolve()
    check(not output.exists() and output.is_relative_to((ROOT / "local-data").resolve()), "fresh ignored output")
    private_directory(output)
    primitive = service.parcels.Monocypher(library, receipt["library_sha256"])
    results = {"schema": 1, "target": receipt["target"], "game_process": False,
               "external_network": False, "cases": [], "real_ce_peer_processes": 0}
    contexts = []

    def record(name):
        results["cases"].append({"name": name, "passed": True})
        (output / "result.private.json").write_text(json.dumps(results, indent=2) + "\n")
        print("PASS", name)

    # Fail closed if any fixture accidentally reaches a provider/key seam.
    with ExitStack() as guard:
        for name in ("_load_key", "issue_local", "issue_from_request"):
            guard.enter_context(patch.object(wire.private, name, side_effect=ValueError("external operation refused")))
        guard.enter_context(patch.object(wire.private.FixedCloudflareTransport, "generate",
                                         side_effect=ValueError("external operation refused")))
        # A real direct CE host is a causal trap: first prove it can admit the
        # authored direct guest, then use a new one during the no-relay case.
        with ExitStack() as scope:
            trap = Child(binary, output / "direct-control-host", "host", "lobby", "unused", "unused", "trap", "authored-only")
            scope.callback(trap.close); trap.expect("DIRECT_READY")
            guest = Child(binary, output / "direct-control-guest", "guest", "lobby", "unused", "unused", "direct", "authored-only")
            scope.callback(guest.close)
            trap.expect("ACTIVE_DIRECT"); guest.expect("ACTIVE_DIRECT")
            results["real_ce_peer_processes"] += 2
        record("live-direct-trap-control")

        with ExitStack() as scope:
            trap = Child(binary, output / "no-relay-trap", "host", "lobby", "unused", "unused", "trap", "authored-only")
            scope.callback(trap.close); trap.expect("DIRECT_READY")
            pair = Pair(binary, output / "no-relay")
            scope.callback(pair.close); pair.start("lobby")
            for role in ("host", "guest"):
                publish(pair.directory(role), "relay-" + role + ".json", wire._json(grant(pair, role)))
                pair.peers[role].expect("AGENT_CREATED")
            time.sleep(0.5)
            pair.expire()
            time.sleep(0.5)
            trap.pump()
            check("ACTIVE_DIRECT" not in trap.lines, "missing relay never falls back to live direct host")
            results["real_ce_peer_processes"] += 3
        record("unavailable-relay-no-direct-fallback")

        with ExitStack() as scope:
            pair = Pair(binary, output / "context-mismatch")
            scope.callback(pair.close); pair.start("lobby", mismatch=True)
            check(pair.request("host")["context"] != pair.request("guest")["context"], "actual mismatched CE contexts")
            try:
                courier_move(pair, "request", pair.root / "request-parcel")
            except wire.Refused:
                pass
            else:
                raise ValueError("courier accepted mismatched actual context")
            check(not (pair.directory("host") / "relay-guest.request.json").exists(), "mismatched request not published")
            pair.expire()
            check(all("AGENT_CREATED" not in peer.lines for peer in pair.peers.values()), "context refusal before ICE creation")
            results["real_ce_peer_processes"] += 2
        record("actual-context-mismatch-refused")

        with ExitStack() as relay_scope:
            relay = Child(binary, output / "loopback-relay", "relay")
            relay_scope.callback(relay.close); relay.expect("RELAY_READY")
            with ExitStack() as scope:
                pair = Pair(binary, output / "courier")
                scope.callback(pair.close)
                for stage in ("lobby", "game"):
                    pair.start(stage); contexts.append(pair.request("host")["context"])
                    courier_prepare(pair)
                    courier_move(pair, "offer", pair.root / (stage + "-offer-parcel"))
                    wait(pair.record("guest").is_file, "actual courier-bound answer")
                    courier_move(pair, "answer", pair.root / (stage + "-answer-parcel"))
                    pair.traffic()
                    pair.peers["guest"].send("close")
                    pair.peers["host"].expect("TERMINAL_PEER_LEFT")
                    pair.close()
                    results["real_ce_peer_processes"] += 2
                    record("courier-" + stage + "-real-session-traffic-and-leave")
                check(contexts[0] != contexts[1], "new factory stage uses a fresh CE context")

            with ExitStack() as scope:
                pair = Pair(binary, output / "ciphertext-mismatch")
                scope.callback(pair.close); pair.start("lobby")
                courier_prepare(pair)
                courier_move(pair, "offer", pair.root / "altered-offer-parcel", tamper=True)
                pair.peers["guest"].expect("TERMINAL_REFUSED")
                check(not pair.record("guest").exists(), "actual exchange rejects altered authentication tag")
                pair.expire()
                results["real_ce_peer_processes"] += 2
            record("altered-courier-ciphertext-refused-by-ce")

            with ExitStack() as scope:
                flow = StoreFlow(primitive, output / "sealed-store")
                scope.callback(flow.store.close)
                pair = Pair(binary, output / "store-pair")
                scope.callback(pair.close)
                for stage in ("lobby", "game"):
                    pair.start(stage)
                    if stage == "lobby":
                        flow.ingress_controls(pair)
                        record("actual-request-authentication-and-role-controls")
                        # Freeze a real host request, then prove a changed guest
                        # context is refused before the second permit is issued.
                        check(flow.call("stage/request", HOST, flow.request_body(pair, "host")).status == "awaiting_peer_request",
                              "actual host request stages context")
                        body = flow.request_body(pair, "guest")
                        body["request"] = body["request"] | {"context": "0" * 64}
                        check(flow.call("stage/request", GUEST, body).status == "invalid_request" and not flow.issuer.calls,
                              "store refuses changed actual context before issuance")
                        record("actual-request-store-context-refusal")
                    flow.prepare(pair); flow.exchange(pair)
                    pair.close()
                    results["real_ce_peer_processes"] += 2
                    record("sealed-store-" + stage + "-real-session-traffic")
                check(sorted(flow.issuer.calls) == sorted((stage, role) for stage in ("lobby", "game") for role in ("host", "guest")),
                      "exactly four fabricated grants for two actual stages")
                flow.store.close()
                for path in flow.directory.glob("signaling.sqlite*"):
                    raw = path.read_bytes()
                    check(USER.encode() not in raw and PASSWORD.encode() not in raw, "no plaintext grant canary in SQLite files")

    canaries = [USER.encode(), PASSWORD.encode(), b"CEICE002", b"a=ice-pwd:", b"a=ice-ufrag:"]
    for path in output.rglob("*.log"):
        data = path.read_bytes()
        check(not any(value in data for value in canaries), "runtime logs omit credential and exchange canaries")
    cleanup = [json.loads(path.read_bytes()) for path in output.rglob("cleanup.private.json")]
    check(bool(cleanup) and all(value == {"collected": True, "forced": False, "failures": []} for value in cleanup),
          "all recorded owned processes collected without signal escalation")
    results["owned_processes_collected_without_force"] = len(cleanup)
    results["runtime_log_canaries"] = "absent"
    results["separate_processes_and_directories"] = True
    results["remaining"] = ["real game and authenticated START handoff", "remote authenticated confidential delivery",
                            "real identity and provider", "Windows private-file and native acceptance", "TURN TCP/TLS and renewal"]
    (output / "result.private.json").write_text(json.dumps(results, indent=2) + "\n")
    record("runtime-privacy-and-clean-shutdown")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        run(args.build.resolve(), args.output)
    except (ValueError, OSError, subprocess.SubprocessError, wire.Refused, service.Refused):
        raise SystemExit("offline CE flow check failed; private diagnostics retained") from None


if __name__ == "__main__":
    main()
