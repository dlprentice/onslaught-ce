#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Whole-lobby host and guest recovery controls with authored boundaries and no sockets.

Observe the session factory to catch an unintended same-context retry. No relay
records, credentials, player files, game, device input or provider are used.
"""
import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import tempfile

import deps
from network_lockstep_resilience_tests import run_command, sha

ROOT = Path(__file__).resolve().parents[2]
LOBBY = "port/platform/common/ce_netlobby.cpp"
HARNESS = "port/tools/tests/network_lobby_notice_driver.cpp"
DRIVER = "port/tools/tests/network_lobby_relay_departure_driver.cpp"
GUARD = "    if (CE_NetGameIceRequested()) {\n        // This exchange keeps"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("native", "linux32"), default="native")
    parser.add_argument("--causal", action="store_true")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / "local-data/port/lobby-relay-departure-tests"
    parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    out = Path(tempfile.mkdtemp(prefix=args.target + "-", dir=parent))
    snapshot = out / "source"
    paths = {LOBBY, HARNESS, DRIVER, "port/platform/common/ce_netcode.cpp", "port/game/ce_network_boot_authority.cpp",
             "port/network/codec.cpp", "port/updater/sha256.c"}
    for directory in ("port/platform/common", "port/network", "port/game", "port/updater"):
        paths.update(str(path.relative_to(ROOT)) for path in (ROOT / directory).glob("*.h"))
    pins = {}
    for relative in sorted(paths):
        source, dest = ROOT / relative, snapshot / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        pins[relative] = sha(source)
        if sha(dest) != pins[relative]:
            raise RuntimeError("source snapshot changed during copy")
    source = snapshot / LOBBY
    production = source.read_text()
    if production.count(GUARD) != 1:
        raise RuntimeError("relay departure guard is not unique")
    start = production.index(GUARD)
    end = production.index("\n\n\tif (connected)", start)
    block = production[start:end]
    cases = [("actual", production, None)]
    if args.causal:
        cases.append(("old-host-retry", production[:start] + production[end:],
                      "relay host retires without recreating its one-use exchange"))
        controls = (
            ("suppress-direct-retry", "if (CE_NetGameIceRequested()) {", "if (true) {",
             "direct host still listens again with the same code after departure"),
            ("call-abort-voluntary", "s.phase == CE_NET_CLOSED && s.error == CE_NET_PEER_LEFT",
             "s.error == CE_NET_PEER_LEFT", "unproven relay disconnect is not called voluntary departure"),
            ("hide-terminal-state", "L.state = CE_LOBBY_FAILED;", "L.state = CE_LOBBY_OFF;",
             "relay host retires without recreating its one-use exchange"),
            ("omit-fresh-attempt-action",
             "Ask the operator to prepare fresh private lobby and game folders. Relaunch both games and repeat the private code and file exchange.",
             "Try again.", "authenticated relay departure retains its cause and fresh-attempt action"),
            # The private failure line (stderr) carries the lobby's own fixed words, never the other build's label.
            ("omit-relay-failure-line", "        RelayLobbyFailed(logged);\n", "",
             "relay host failure prints the lobby's own sentence, then the new state"),
            ("version-label-in-failure-line",
             "logged = \"A player using another Onslaught CE version tried to join.\";", "logged = cause;",
             "relay host failure prints the lobby's own sentence, then the new state"),
        )
        for name, before, after, expected in controls:
            if block.count(before) != 1:
                raise RuntimeError("control anchor is not unique: " + name)
            cases.append((name, production[:start] + block.replace(before, after, 1) + production[end:], expected))
        host_start = production.index("void HostFailure(const CE_NetStatus &s)")
        head = production[host_start:start]
        if head.count("        RelayLobbyFailed(route_problem);\n") != 1:
            raise RuntimeError("host backend failure anchor is not unique")
        cases.append(("omit-host-backend-failure-line", production[:host_start] + head.replace(
            "        RelayLobbyFailed(route_problem);\n", "", 1) + production[start:],
            "backend failure on the relay host prints the state, then its sentence"))
        trace = 'fprintf(stderr, LOBBY_LOG "state %s\\n", StateName(next));'
        if production.count(trace) != 1:
            raise RuntimeError("state trace anchor is not unique")
        cases.append(("state-trace-silent", production.replace(trace, "(void)next;", 1),
                      "relay host failure prints the lobby's own sentence, then the new state"))
        # The state trace prints once per change and names every state as the log's readers expect.
        guard = "if (next != value)"
        if production.count(guard) != 1:
            raise RuntimeError("state trace guard anchor is not unique")
        cases.append(("state-trace-on-every-assignment", production.replace(guard, "if (true)", 1),
                      "setting the state to the one the lobby is in prints nothing"))
        ready, starting = 'case CE_LOBBY_READY: return "ready";', 'case CE_LOBBY_STARTING: return "starting";'
        if production.count(ready) != 1 or production.count(starting) != 1:
            raise RuntimeError("state name anchors are not unique")
        cases.append(("state-names-ready-and-starting-swapped",
                      production.replace(ready, 'case CE_LOBBY_READY: return "starting";', 1).replace(
                          starting, 'case CE_LOBBY_STARTING: return "ready";', 1),
                      "every lobby state prints its own name when the lobby enters it"))
        guest_start = production.index("void GuestFailure(const CE_NetStatus &s)")
        guest_end = production.index("\n#ifdef _WIN32\nint gWsa;", guest_start)
        guest = production[guest_start:guest_end]
        guest_controls = (
            ("guest-direct-advice", "if (relay)\n\t{", "if (false)\n\t{",
             "guest relay admission failure gives fresh setup advice"),
            ("guest-abort-voluntary", "s.phase == CE_NET_CLOSED && s.error == CE_NET_PEER_LEFT",
             "s.error == CE_NET_PEER_LEFT",
             "unproven relay host disconnect is not a voluntary guest leave"),
            ("guest-ready-timeout-left", "L.state = departed ? CE_LOBBY_LEFT : CE_LOBBY_FAILED;",
             "L.state = (departed || was) ? CE_LOBBY_LEFT : CE_LOBBY_FAILED;",
             "ready relay guest timeout is terminal without inventing departure"),
            ("guest-version-recovery-omitted", "if (relay) Text(L.st.notice, sizeof L.st.notice, relay_recovery);",
             "(void)relay_recovery;",
             "relay guest version refusal preserves labels and fresh setup advice"),
            ("guest-backend-precedence-lost", "if (route_failed) {",
             "if (route_failed && !CE_NetGameIceRequested()) {",
             "guest backend diagnostic takes precedence over generic relay recovery"),
            ("guest-direct-becomes-relay", "const bool relay = CE_NetGameIceRequested() != 0;",
             "const bool relay = true;", "direct guest admission retains its address guidance"),
            ("guest-recovery-action-omitted",
             "Relaunch both games with fresh private lobby and game folders; repeat the code and file exchange.",
             "Try again.", "guest relay admission failure gives fresh setup advice"),
            ("guest-omit-backend-failure-line", "        RelayLobbyFailed(route_problem);\n", "",
             "backend failure on the relay guest prints the state, then its sentence"),
            ("guest-omit-relay-failure-line", "\t\tRelayLobbyFailed(cause);\n", "",
             "relay guest failure prints the new state, then the lobby's own sentence"),
        )
        for name, before, after, expected in guest_controls:
            if guest.count(before) != 1:
                raise RuntimeError("guest control anchor is not unique: " + name)
            cases.append((name, production[:guest_start] + guest.replace(before, after, 1) +
                          production[guest_end:], expected))
    if args.target == "linux32":
        cc = str(deps.DATA / "llvm/bin/clang")
        cxx = str(deps.DATA / "llvm/bin/clang++")
        compile_flags = ["--target=i686-pc-linux-gnu", *deps.sysroot_flags(), "-m32"]
        link_flags = [*deps.sysroot_link_flags(), "-m32"]
    else:
        cc, cxx = "clang", "clang++"
        compile_flags, link_flags = [], []
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("BEA_") and key not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
    env.update(TMPDIR=str(out), HOME=str(out), XDG_CONFIG_HOME=str(out / "config"),
               XDG_DATA_HOME=str(out / "data"), XDG_CACHE_HOME=str(out / "cache"))
    sha_obj = out / "sha256.o"
    command = [cc, *compile_flags, "-O1", "-c", str(snapshot / "port/updater/sha256.c"), "-o", str(sha_obj)]
    if run_command(command, out / "sha-build.log", env):
        raise RuntimeError("SHA compile failed; inspect " + str(out))
    records = []
    for name, text, expected in cases:
        source.write_text(text)
        exe = out / (name + "-test")
        command = [cxx, *compile_flags, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Wno-unused-function", "-DCE_NETWORK_ICE=1",
                   '-DCE_NETWORK_SIMULATION_ID="authored-relay-departure"',
                   '-DRELAY_DEPARTURE_HARNESS_INC="' + str(snapshot / HARNESS) + '"',
                   '-DLOBBY_SOURCE="' + str(source) + '"',
                   "-I", str(snapshot / "port/platform/common"), "-I", str(snapshot / "port/network"),
                   "-I", str(deps.PREFIX / "include"), str(snapshot / DRIVER),
                   str(snapshot / "port/platform/common/ce_netcode.cpp"), str(snapshot / "port/game/ce_network_boot_authority.cpp"),
                   str(snapshot / "port/network/codec.cpp"), str(sha_obj), *link_flags, "-o", str(exe)]
        if run_command(command, out / (name + "-build.log"), env):
            raise RuntimeError("fixture compile failed: " + name + "; inspect " + str(out))
        log = out / (name + "-result.log")
        status = run_command([str(exe)], log, env, 10)
        lines = log.read_text().splitlines()
        complete = any(line.startswith("RELAY_DEPARTURE_COMPLETE failures=") for line in lines)
        passed = complete and (status == 0 if expected is None else status == 1 and "FAIL " + expected in lines)
        records.append({"name": name, "passed": passed, "exit_status": status,
                        "completed": complete, "expected_failure": expected,
                        "executable_sha256": sha(exe), "source_sha256": sha(source), "command": command})
        print(("PASS " if passed else "FAIL ") + name, flush=True)
    source.write_text(production)
    unchanged = all(sha(ROOT / path) == digest for path, digest in pins.items())
    passed = unchanged and all(record["passed"] for record in records)
    receipt = {"target": args.target, "passed": passed, "cases": records,
               "source_sha256": pins, "production_inputs_unchanged": unchanged,
               "limits": "Whole production lobby, authored transport status and factory. No socket, actual ICE/exchange file, provider, device input, game, save or native Windows execution."}
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(("PASS " if unchanged else "FAIL ") + "production-inputs-unchanged")
    print("evidence:", out / "result.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
