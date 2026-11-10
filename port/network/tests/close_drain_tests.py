#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real-ENet loopback regression for destruction after a reliable close.

The proxy drops the first close burst after both peers complete authentication.
It uses ephemeral loopback ports, fresh random fixture authentication material,
and no game data. A bounded drain gives ENet a chance to retransmit; it does not
guarantee delivery when the peer remains unavailable.
"""
from __future__ import annotations

import ast
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[3]


def build(enet_source: Path, output: Path) -> Path:
    """Reuse the accepted dependency verifier and compiler recipe only."""
    source = ROOT / "port/network/tests/terminal_close_tests.py"
    text = source.read_text()
    namespace = {"__file__": str(source), "__name__": "close_drain_build_fixture"}
    exec(compile(text, str(source), "exec"), namespace)
    namespace["verify_sources"](enet_source)
    tree = ast.parse(text)
    recipe = next(node for node in tree.body
                  if isinstance(node, ast.FunctionDef) and node.name == "build")
    replacements = 0
    for node in ast.walk(recipe):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            if "terminal_close_peer.cpp" in node.value:
                node.value = node.value.replace("terminal_close_peer.cpp", "close_drain_peer.cpp")
                replacements += 1
    if replacements != 1:
        raise RuntimeError("accepted compiler recipe driver changed")
    exec(compile(ast.Module(body=[recipe], type_ignores=[]), str(source), "exec"), namespace)
    return namespace["build"](enet_source, output)


def unused_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class CloseProxy:
    def __init__(self, folder: Path, host_port: int, closer: str, loss: bool):
        self.folder, self.host_port, self.closer, self.loss = folder, host_port, closer, loss
        self.front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.front.bind(("127.0.0.1", 0))
        self.back.bind(("127.0.0.1", 0))
        self.port = self.front.getsockname()[1]
        self.guest = None
        self.stop = threading.Event()
        self.error = None
        self.dropped = 0
        self.armed = False
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def loop(self):
        try:
            while not self.stop.is_set():
                ready, _, _ = select.select([self.front, self.back], [], [], 0.01)
                for sock in ready:
                    data, addr = sock.recvfrom(65536)
                    from_closer = (sock is self.back) == (self.closer == "host")
                    if self.armed and self.loss and from_closer and not (self.folder / "drop.release").exists():
                        self.dropped += 1
                        continue
                    if sock is self.front:
                        self.guest = addr
                        self.back.sendto(data, ("127.0.0.1", self.host_port))
                    elif self.guest:
                        self.front.sendto(data, self.guest)
                if not self.armed and (self.folder / "drop.arm").exists():
                    # Complete this iteration before acknowledging the arm, so
                    # pre-close traffic cannot consume the loss control.
                    self.armed = True
                    (self.folder / "drop.ready").touch()
        except BaseException as exc:
            self.error = type(exc).__name__

    def close(self):
        self.stop.set()
        self.thread.join(1)
        self.front.close()
        self.back.close()
        if self.thread.is_alive() or self.error:
            raise RuntimeError("loopback proxy failed")


def owned_stop(child: subprocess.Popen, expected: list[str]):
    if child.poll() is not None:
        return
    raw = Path(f"/proc/{child.pid}/cmdline").read_bytes()
    actual = [item.decode() for item in raw.split(b"\0") if item]
    if actual != expected:
        raise RuntimeError("fixture child identity changed")
    child.kill()
    child.wait()


def pair(binary: Path, output: Path, closer: str, loss: bool, repeat: bool,
         delivery_expected: bool = True, late: bool = False) -> bool:
    folder = Path(tempfile.mkdtemp(prefix="pair-", dir=output))
    folder.chmod(0o700)
    secret = folder / "fixture-auth.private"
    secret.write_bytes(os.urandom(32))
    secret.chmod(0o600)
    host_port = unused_port()
    proxy = CloseProxy(folder, host_port, closer, loss)
    children: list[tuple[subprocess.Popen, list[str]]] = []
    logs = []
    try:
        for role, port in (("host", host_port), ("guest", proxy.port)):
            mode = "repeat" if repeat else "once"
            if late and role != closer:
                mode = "late"
            argv = [str(binary), role, str(port), "closer" if role == closer else "observer",
                    str(folder), str(secret), mode]
            log = (folder / f"{role}.private.log").open("wb")
            logs.append(log)
            child = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT)
            children.append((child, argv))
            # Establish exact ownership immediately, including the command line,
            # before any possible timeout cleanup.
            for _ in range(100):
                try:
                    cmdline = Path(f"/proc/{child.pid}/cmdline").read_bytes()
                    if cmdline:
                        if cmdline.split(b"\0")[0].decode() != str(binary):
                            raise RuntimeError("fixture executable identity mismatch")
                        break
                except FileNotFoundError:
                    if child.poll() is not None:
                        break
                time.sleep(0.001)
        for _ in range(1000):
            if (folder / "drop.ready").exists():
                break
            if any(child.poll() is not None for child, _ in children):
                break
            time.sleep(0.005)
        for _ in range(20):
            time.sleep(0.005)
        if delivery_expected:
            (folder / "drop.release").touch()
        for _ in range(2400):
            if all(child.poll() is not None for child, _ in children):
                break
            time.sleep(0.005)
        exits = [child.poll() for child, _ in children]
        closer_exit = exits[0 if closer == "host" else 1]
        observer_exit = exits[1 if closer == "host" else 0]
        result = closer_exit == 0 and observer_exit == (0 if delivery_expected else 98)
        result = result and (folder / "closer.bounded").exists()
        result = result and (proxy.dropped > 0 if loss else proxy.dropped == 0)
        result = result and ((folder / "observer.late-sent").exists() if late else True)
        (folder / "result.json").write_text(json.dumps({
            "passed": result, "peer_exit_codes": exits, "dropped_datagrams": proxy.dropped,
            "loss_control_active": loss, "closer_bounded": (folder / "closer.bounded").exists(),
            "closer_role": closer, "repeated_close": repeat,
            "delivery_expected": delivery_expected, "late_event_control": late,
        }, indent=2) + "\n")
        return result
    finally:
        for child, argv in children:
            owned_stop(child, argv)
        proxy.close()
        for log in logs:
            log.close()


def single(binary: Path, output: Path, role: str, mode: str) -> bool:
    folder = Path(tempfile.mkdtemp(prefix="single-", dir=output))
    secret = folder / "fixture-auth.private"
    secret.write_bytes(os.urandom(32))
    secret.chmod(0o600)
    argv = [str(binary), role, str(unused_port()), mode, str(folder), str(secret), "once"]
    with (folder / "peer.private.log").open("wb") as log:
        child = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT)
        try:
            for _ in range(1200):
                if child.poll() is not None:
                    break
                time.sleep(0.005)
            result = child.poll() == 0 and (folder / "single.bounded").exists()
            (folder / "result.json").write_text(json.dumps({
                "passed": result, "bounded": (folder / "single.bounded").exists(),
                "mode": mode, "role": role,
            }) + "\n")
            return result
        finally:
            owned_stop(child, argv)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enet-source", type=Path, required=True)
    args = parser.parse_args()
    output = ROOT / "local-data/port/network-close-drain-tests"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    run.chmod(0o700)
    binary = build(args.enet_source, run)
    results = [pair(binary, run, closer, loss, repeat)
               for closer, loss, repeat in (("host", False, False), ("guest", False, False),
                                           ("host", True, False), ("guest", True, False),
                                           ("host", True, True), ("guest", True, True))]
    results += [pair(binary, run, closer, True, False, delivery_expected=False)
                for closer in ("host", "guest")]
    results += [pair(binary, run, closer, True, False, late=True)
                for closer in ("host", "guest")]
    results += [single(binary, run, "host", "partial"),
                single(binary, run, "guest", "partial"),
                single(binary, run, "guest", "failed")]
    hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (
        ROOT / "port/network/transport.cpp", Path(__file__),
        ROOT / "port/network/tests/close_drain_peer.cpp",
        ROOT / "port/network/tests/terminal_close_tests.py", args.enet_source / "protocol.c")}
    (run / "summary.json").write_text(json.dumps({
        "passed": sum(results), "total": len(results), "source_sha256": hashes,
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    }, indent=2) + "\n")
    print(f"close-drain: {sum(results)}/{len(results)}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
