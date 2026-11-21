#!/usr/bin/env python3
"""Test host-address selection without opening a network socket."""

import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "port" / "tools"
DRIVER = TOOLS / "tests" / "network_lobby_addresses_driver.cpp"


def load_notice_harness():
    spec = importlib.util.spec_from_file_location(
        "lobby_notice_fixture", TOOLS / "network_lobby_notice_tests.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.DRIVER = DRIVER
    return module


def main():
    harness = load_notice_harness()
    output = Path(tempfile.mkdtemp(prefix="ce-lobby-address-"))
    fixture = (TOOLS / "tests" / "network_lobby_notice_driver.cpp").read_text()
    # CE_NetCreate also records the address the lobby asks the transport to listen on (its config's ipv4),
    # which the page never shows: the transport rejects an empty one, so the lobby never opens.
    for name, counter in (("CE_NetCreate",
                           "test_create_count; snprintf(test_create_ipv4, sizeof test_create_ipv4, \"%s\", "
                           "c->ipv4 ? c->ipv4 : \"(null)\");"),
                          ("CE_NetDestroy", "test_destroy_count;")):
        pattern = r"(\b" + name + r"\s*\([^;{}]*\)\s*\{)"
        fixture, changed = re.subn(pattern, lambda m: m.group(1) + " ++" + counter, fixture)
        if changed != 1:
            print("address selection fixture: FAIL; transport stub changed")
            return 1
    fixture = "static int test_create_count, test_destroy_count;\nstatic char test_create_ipv4[32];\n" + fixture
    (output / "network_lobby_notice_driver.cpp").write_text(fixture)
    driver = output / DRIVER.name
    driver.write_text(DRIVER.read_text())
    harness.DRIVER = driver
    game_dir = output / "game"
    user_dir = output / "user"
    game_dir.mkdir()
    user_dir.mkdir()
    env = os.environ.copy()
    for name in ("BEA_LOCKSTEP_HOST", "BEA_LOCKSTEP_JOIN", "BEA_NET_LOBBY_BIND"):
        env.pop(name, None)
    env["BEA_GAME_DIR"] = str(game_dir)
    env["BEA_USER_DIR"] = str(user_dir)
    lobby = harness.LOBBY.read_text()
    netcode = harness.NETCODE.read_text()

    try:
        binary = harness.compile_driver(output, "actual", lobby, netcode, False)
    except RuntimeError:
        log = (output / "actual" / "build.log").read_text(errors="replace")
        errors = [line for line in log.splitlines() if "error:" in line]
        missing_api = bool(errors) and all("CE_LobbyHostAt" in line for line in errors)
        print("address selection compile: FAIL; missing selection API:", missing_api)
        print("private build evidence:", output)
        return 1

    actual = subprocess.run([str(binary)], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, timeout=30)
    (output / "actual-run.log").write_bytes(actual.stdout)
    if actual.returncode:
        print("address selection controls: FAIL; case:", actual.returncode)
        print("private test evidence:", output)
        return 1

    original = "return LobbyHost(selected_address);"
    unbound = "ip = selected_ip;\n        Dotted(ip, L.bind, sizeof L.bind);"
    if lobby.count(unbound) != 1:
        print("address selection mutant: FAIL; expected bind seam absent")
        return 1
    try:
        binary = harness.compile_driver(output, "mutant-unbound",
                                        lobby.replace(unbound, "ip = selected_ip;"), netcode, False)
    except RuntimeError:
        print("address selection mutant: FAIL; could not build")
        print("private test evidence:", output)
        return 1
    changed = subprocess.run([str(binary)], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, env=env, timeout=30)
    (output / "mutant-unbound-run.log").write_bytes(changed.stdout)
    if changed.returncode not in (27, 28):
        print("address selection mutant: FAIL; chosen address left unbound, case:", changed.returncode)
        return 1
    mutant = "return LobbyHost(NULL);"
    if lobby.count(original) != 1:
        print("address selection mutant: FAIL; expected seam absent")
        return 1
    try:
        binary = harness.compile_driver(output, "mutant", lobby.replace(original, mutant),
                                        netcode, False)
    except RuntimeError:
        print("address selection mutant: FAIL; could not build")
        print("private test evidence:", output)
        return 1
    changed = subprocess.run([str(binary)], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, env=env, timeout=30)
    (output / "mutant-run.log").write_bytes(changed.stdout)
    if changed.returncode == 0:
        print("address selection mutant: FAIL; controls accepted ignored choice")
        return 1

    print("address selection controls: PASS; ignored-choice and unbound-address mutants rejected")
    print("private test evidence:", output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
