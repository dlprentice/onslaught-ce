#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual authenticated loopback transport + Session + native quit/shell return at controlled world/resource boundaries.

No game, assets, SDL, devices, remote contacts, downloads or installs. The real ENet/Monocypher handshake, polling,
queue, terminal controls and native exit methods execute; actor initialization and whole-game acceptance do not.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import deps
import quiet_crash
from network_session_tests import body

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build
ENET_COMMIT = "5a9c537fd464b3c6d3c55e1d3bd47588faf71b42"
CASES = ("app-leaving", "both-leaving", "transport-leaving", "malformed-control", "malformed-then-close",
         "leave-then-malformed", "late-malformed", "unexpected-then-close", "invalid-leave-shape", "bare-disconnect",
         "timeout", "wrong-epoch-leave", "wrong-session-leave", "unauthenticated-leave", "own-quit-first",
         "terminal-input-fifo", "host-wait-ack", "host-resume-ack", "invalid-wait-close", "guest-wait", "guest-resume",
         "birth-close", "invalid-birth-close", "guest-backpressure", "paused-service", "device-unready", "after-flush",
         "before-render", "remote-error", "guest-remote-error", "load-remote-error", "prepare-remote-error", "guest-baseline-remote-error", "guest-syncing-remote-error", "remote-error-pending", "load-close", "prepare-close", "guest-baseline-close", "error-reset-offline", "full-terminal-queue", "queue-overflow", "fail-then-leave", "reset-offline", "reset-network", "reset-invalid")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enet-source", type=Path, default=deps.SRC / ("enet-" + ENET_COMMIT))
    parser.add_argument("--compiler", default="clang++")
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--asan", action="store_true")
    args = parser.parse_args()
    # The controls abort on purpose (abort_on_error=1): their runs go through quiet_crash (no core file, no coredumpctl record).
    parent = ROOT / "local-data/port/network-departure-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    production = out / "production"
    pins = {}
    for name in ("network/transport.cpp", "network/codec.cpp", "network/network.h", "network/ice_game.h", "network/ice_signaling.h", "network/compatibility.h", "game/ce_network_session.cpp",
                 "game/ce_network_session.h", "game/ce_network_world.h", "game/ce_network_lockstep.h", "game/ce_network_boot.h",
                 "game/ce_network_loaded_admission.h", "network/state_delivery.h", "network/state_delivery.cpp", "network/state_delta.h",
                 "network/state_delta.cpp", "game/ce_network_stl.h", "game/ce_network_campaign.h", "platform/common/ce_netcode.h"):
        source = ROOT / "port" / name
        dest = production / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        pins["port/" + name] = sha(source)
    driver = out / "driver.cpp"
    shutil.copyfile(ROOT / "port/tools/tests/network_departure_driver.cpp", driver)
    pins["driver"] = sha(driver)
    pins["runner"] = sha(Path(__file__))
    game_header = (ROOT / "src/game.h").read_text()
    (out / "set-quit.inc").write_text(body(game_header, "\n\tvoid\t\t\tSetQuit("))
    shell_header = (ROOT / "src/ltshell.h").read_text()
    getters = []
    for pattern in (r"BOOL\s+WantsToQuit\(", r"int\s+MsgInfo\("):
        match = re.search(pattern, shell_header)
        getters.append(body(shell_header, match.group()))
    (out / "shell-getters.inc").write_text("\n".join(getters))
    shell = out / "ltshell.cpp"
    # The mirror's text with every patch's section for the two files applied in order (the patches' contexts are written against the
    # mirror's normalised include names), as a build with the network sees it; build.patched_text names a patch that is refused.
    texts = build.patched_text(["ltshell.cpp", "game.cpp"], network=True)
    shell.write_text(texts["ltshell.cpp"], encoding="latin-1")
    (out / "game.cpp").write_text(texts["game.cpp"], encoding="latin-1")
    patch_pins = {}
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        if any(part.startswith("--- a/ltshell.cpp\n") or part.startswith("--- a/game.cpp\n")
               for part in re.split(r"(?=^--- a/)", patch.read_text(), flags=re.M)):
            patch_pins[patch.name] = sha(patch)
    (out / "shell-main-loop.inc").write_text(body(shell.read_text(), "int PCLTShell::MainLoop()"))
    (out / "abort-load.inc").write_text(body((out / "game.cpp").read_text(), "    static int AbortLoad()"))
    for name in ("src/game.cpp", "src/game.h", "src/ltshell.h", "src/ltshell.cpp"):
        pins[name] = sha(ROOT / name)
    for path in out.glob("*.inc"):
        pins[path.name] = sha(path)
    enet, mc = args.enet_source.resolve(), deps.SRC / "monocypher-4.0.2/src"
    if not (enet / "include/enet/enet.h").is_file() or not (mc / "monocypher.c").is_file():
        raise RuntimeError("supply already-available pinned ENet and Monocypher sources; nothing is downloaded")
    env = dict(os.environ, TMPDIR=str(out), ASAN_OPTIONS="detect_leaks=1:abort_on_error=1")
    for key in tuple(env):
        if key.startswith("BEA_"):
            env.pop(key)
    sanitizer = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"] if args.asan else []
    includes = ["-I" + str(enet / "include"), "-I" + str(mc), "-I" + str(out)]
    commands, objects = [], []
    for name in ("callbacks", "compress", "host", "list", "packet", "peer", "protocol", "unix", "monocypher"):
        source = mc / "monocypher.c" if name == "monocypher" else enet / (name + ".c")
        obj = out / (name + ".o")
        pins[str(source)] = sha(source)
        command = [args.cc, "-std=gnu99", "-O1", "-g", *(["-fsanitize=address", "-fno-omit-frame-pointer"] if args.asan else []), *includes,
                   *["-D" + n + "=1" for n in ("HAS_FCNTL", "HAS_POLL", "HAS_GETADDRINFO", "HAS_GETNAMEINFO", "HAS_INET_PTON",
                                               "HAS_INET_NTOP", "HAS_MSGHDR_FLAGS", "HAS_SOCKLEN_T")], "-c", str(source), "-o", str(obj)]
        commands.append(command)
        proc = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
        (out / (name + ".build.log")).write_text(proc.stdout + proc.stderr)
        if proc.returncode:
            raise RuntimeError("dependency fixture compile refused: " + name)
        objects.append(str(obj))
    receipt = {"scope": __doc__, "source_sha256": pins, "patches": patch_pins, "commands": commands, "cases": [], "mutations": [],
               "sanitizer_scope": "Dependencies: ASan only (upstream ENet offsetof macro is not UBSan-clean); port/Session/driver: ASan + nonrecovering UBSan when --asan. No upstream changes.",
               "limits": "One controlled running Session; actual authenticated peer transports and native quit/shell methods. Ordinary monotonic authored 200ms silence deadline; no game clock/harness or actual world/resource changes."}

    def compile_variant(label, session, transport):
        binary = out / (label + "-driver")
        command = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *sanitizer, *includes,
                   '-DTRANSPORT_SOURCE="' + str(transport) + '"', '-DSESSION_SOURCE="' + str(session) + '"', str(driver),
                   str(production / "network/codec.cpp"), str(production / "network/state_delta.cpp"), str(production / "network/state_delivery.cpp"),
                   *objects, "-pthread", "-o", str(binary)]
        commands.append(command)
        proc = subprocess.run(command, env=env, capture_output=True, text=True, timeout=40)
        (out / (label + ".build.log")).write_text(proc.stdout + proc.stderr)
        if proc.returncode:
            raise RuntimeError("variant must compile: " + label)
        return binary

    def exercise(binary, case, reason=""):
        proc = quiet_crash.run([str(binary), case], env=env, capture_output=True, text=True, timeout=5)
        (out / (binary.name + "-" + case + ".log")).write_text(proc.stdout + proc.stderr)
        passed = proc.returncode == (2 if reason else 0) and (reason in proc.stderr if reason else True)
        row = {"variant": binary.name, "case": case, "exit": proc.returncode, "reason": reason,
               "observed": json.loads(proc.stdout) if proc.returncode == 0 else {}, "stderr": proc.stderr, "passed": passed}
        receipt["mutations" if reason else "cases"].append(row)
        print(binary.name, case, "PASS" if passed else "FAIL", flush=True)

    session = production / "game/ce_network_session.cpp"
    transport = production / "network/transport.cpp"
    binary = compile_variant("production", session, transport)
    for case in CASES:
        exercise(binary, case)
    mutants = (
        ("error-emits-graceful-leave", "session", "CE_NetSession *net = s.net; s.net = NULL;\n    CE_NetDestroy(net);", "if (s.net) CE_NetClose(s.net);", "remote-error", "actual sender failure retains local world and mission fences after detach"),
        ("abort-sends-graceful-transport", "transport", '        s->status.phase == CE_NET_ACTIVE || s->status.phase == CE_NET_SYNCING))\n        enet_peer_disconnect_now(s->peer, 0);', '        s->status.phase == CE_NET_ACTIVE || s->status.phase == CE_NET_SYNCING))\n        CloseTransport(s, false);', "remote-error", "malformed loss and transport failures retain error outcome"),
        ("leave-is-failure", "session", "s.peer_leaving = true;", 'return Fail("peer left the engineering session");', "app-leaving", "authenticated intentional departure uses native QUIT"),
        ("erase-completed-on-close", "transport", "CloseTransport(s, true);", "CloseTransport(s, false);", "malformed-then-close", "malformed loss and transport failures retain error outcome"),
        ("raw-disconnect-is-graceful", "session", "bool Status(CE_NetStatus &status) {\n    CE_NetGetStatus(s.net, &status);", "bool Status(CE_NetStatus &status) {\n    CE_NetGetStatus(s.net, &status);\n    if (status.phase == CE_NET_FAILED && status.error == CE_NET_PEER_LEFT) status.phase = CE_NET_CLOSED;", "bare-disconnect", "malformed loss and transport failures retain error outcome"),
        ("short-terminal-drain", "session", "? 64u : 32u", "? 32u : 32u", "late-malformed", "malformed loss and transport failures retain error outcome"),
        ("lost-error-outcome", "session", "exit_outcome = EXIT_ERROR;", "exit_outcome = EXIT_NONE;", "malformed-control", "malformed loss and transport failures retain error outcome"),
        ("wait-resume-callback", "session", "if (terminal) {\n                s.completed_wait", "if (false && terminal) {\n                s.completed_wait", "host-resume-ack", "no world dispatch Apply Capture wait gate or clock rebase follows terminal intent"),
        ("prepare-before-terminal", "session", "    if (TerminalTransport() && !Poll()) return 0;", "    /* negative: native PreRun before terminal proof */", "prepare-close", "no world dispatch Apply Capture wait gate or clock rebase follows terminal intent"),
        ("baseline-before-terminal", "session", "    if (TerminalTransport() && !Poll()) return 0;", "    /* negative: baseline Apply before terminal proof */", "guest-baseline-close", "no world dispatch Apply Capture wait gate or clock rebase follows terminal intent"),
        ("drop-offline-reset", "session", "        exit_outcome = EXIT_NONE; return 1;\n    }\n    s.requested = true; s.host = host;", "        return 1;\n    }\n    s.requested = true; s.host = host;", "error-reset-offline", "successful ordinary configuration clears prior terminal outcome"),
        ("normalize-native-quit", "session", "if (native_quit_seen || exit_outcome == EXIT_NONE)", "if ((native_quit_seen < 0) || exit_outcome == EXIT_NONE)", "both-leaving", "genuine native WM_QUIT code always retains precedence"),
    )
    for label, component, old, new, case, reason in mutants:
        original = session if component == "session" else transport
        text = original.read_text()
        if text.count(old) != 1:
            raise RuntimeError("mutation must name one production boundary: " + label)
        variant = original.with_name(label + ".cpp")
        variant.write_text(text.replace(old, new))
        binary = compile_variant(label, variant if component == "session" else session, variant if component == "transport" else transport)
        exercise(binary, case, reason)
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(out / "result.json")
    return 0 if all(row["passed"] for row in receipt["cases"] + receipt["mutations"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
