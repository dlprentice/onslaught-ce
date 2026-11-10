#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused actual-reader/lockstep admission controls, using authored files and stub world/transport.

No game, sockets, devices, campaign/return controls or player files are used. The boot seam forwards
Prepared to real retained compatibility; this does not exercise genuine boot roles or Windows APIs.
The authored voice bytes test the existing length parser, not decoding or runtime desynchronization.
Completed, cancelled and failed retirement are exercised with authored controller/reader fixtures.
They do not establish real-game, Windows or actual-peer lifecycle acceptance.
The Windows D3DX raw texture-reader path is outside this draft and still needs its own review and checks.
"""
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import tempfile

import deps
from network_compatibility_tests import FILES
from network_world_tests import actual_body


EXPECTED_PASSES = {
    "present-voice": (
        "authored present voice keeps its parsed timing",
        "present dialogue permits normal admitted tick completion",
    ),
    "absent-voice": (
        "never-present optional dialogue permits normal admitted tick completion",
    ),
    "after-load": (
        "lost admission refuses actual after-load boundary",
    ),
    "ready-barrier": (
        "already-ready start barrier cannot skip lost admission",
    ),
    "before-update": (
        "lost content stops the next actual update before simulation admission",
        "active content failure destroys its transport before more simulation",
        "active content failure stays sticky without another tick or close",
    ),
    "after-update": (
        "real missing dialogue revokes tick completion before input or hash publication",
        "active content failure destroys its transport before more simulation",
        "active content failure stays sticky without another tick or close",
    ),
    "between-controllers": (
        "content failure between controllers blocks second actual application without a new send",
        "active content failure destroys its transport before more simulation",
        "active content failure stays sticky without another tick or close",
    ),
    "automation": (
        "non-admitted automation permits normal tick completion after content loss",
    ),
    "network-disabled": (
        "actual network-disabled reader retains ordinary failed-open behavior",
    ),
    "retired-finished": (
        "genuine final agreement precedes admission retirement",
        "retired terminal admission does not query its destroyed context",
        "retired terminal admission preserves update and native-control routing",
        "retired terminal admission preserves its existing outcome without another close",
        "retired confirmed completion preserves its agreed outcome",
    ),
    "retired-ended": (
        "genuine final agreement precedes admission retirement",
        "retired terminal admission does not query its destroyed context",
        "retired terminal admission preserves its existing outcome without another close",
        "retired confirmed completion preserves its agreed outcome",
    ),
    "retired-cancelled": (
        "retired terminal admission does not query its destroyed context",
        "retired terminal admission preserves its existing outcome without another close",
        "retired cancellation does not become a confirmed success",
    ),
    "retired-failed": (
        "retired failed admission never resumes an update",
        "retired failed admission preserves its failure without another close",
    ),
    "retired-reconfigure": (
        "retired terminal admission preserves its existing outcome without another close",
        "a new admitted session cannot reuse a retired compatibility context",
    ),
}


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    port = deps.PORT
    owner = deps.WORK / "network-lockstep-admission-tests"
    owner.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=owner))
    receipt = {"limits": __doc__, "compile": [], "cases": [], "mutations": []}
    mc = deps.SRC / "monocypher-4.0.2/src"
    pins = {"monocypher.h": "f78bb31255cfb7beba66afd2137f5194c8a025cf40488b6cc1e295234d43f374",
            "monocypher.c": "afe2b098c8569577a84488e0b98d276d1fba6506adea68bb9241a52111734c59"}
    if {name: hashlib.sha256((mc / name).read_bytes()).hexdigest() for name in pins} != pins:
        raise RuntimeError("pinned Monocypher input changed")

    def execute(command, label, expected=0, env=None, cwd=None):
        proc = subprocess.run(command, capture_output=True, text=True, timeout=60, env=env, cwd=cwd)
        (out / (label + ".log")).write_text(proc.stdout + proc.stderr)
        if proc.returncode != expected:
            raise RuntimeError(label + ": unexpected exit\n" + proc.stdout + proc.stderr)
        return proc

    try:
        crypto = out / "monocypher.o"
        command = [str(deps.DATA / "llvm/bin/clang"), "-m32", "-O0", "-c", str(mc / "monocypher.c"), "-o", str(crypto)]
        execute(command, "crypto-build")
        receipt["compile"].append(command)
        # Reuse the native getter and semantic input validator boundaries, without invoking the
        # terminal driver's aggregate campaign or return entry point.
        world = (port / "game/ce_network_world.cpp").read_text()
        start = world.index("const uint32_t native_actions[]=")
        validator = out / "validator.inc"
        validator.write_text(world[start:world.index(";", start) + 1] + "\n" +
                             actual_body(world, "unsigned ActionRank(") + "\n" + actual_body(world, "bool ValidInput(") +
                             '\nextern "C" ' + actual_body(world, "int CE_WorldLockstepValidInput(") + "\n")
        getter = out / "getter.inc"
        getter.write_text(actual_body((port.parent / "src/game.cpp").read_text(), "BOOL\tCGame::GetSlot(").replace(
            "CGame::GetSlot", "NativeGame::GetSlot", 1) + "\n")
        includes = [port / "tools/tests/lockstep_stubs", port / "platform/linux", port / "include/linux",
                    port / "game", port / "network", deps.DATA / "dxvk-native/usr/include/dxvk", mc]
        common = [str(deps.DATA / "llvm/bin/clang++"), "--target=i686-pc-linux-gnu", "-m32", "-std=c++17", "-O0", "-fwrapv",
                  "-pthread", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-fshort-wchar", "-fms-extensions",
                  "-fno-autolink", "-Wno-ignored-attributes", '-DCE_NETWORK_SIMULATION_ID="fixture-simulation-v1"',
                  '-DLOCKSTEP_VALIDATOR_INC="' + str(validator) + '"', '-DNATIVE_SLOT_GETTER_INC="' + str(getter) + '"']
        common += ["-I" + str(path) for path in includes]
        controller = port / "game/ce_network_lockstep.cpp"
        compatibility = port / "network/compatibility.cpp"
        driver = port / "tools/tests/network_lockstep_admission_driver.cpp"
        terminal = port / "tools/tests/network_lockstep_terminal_driver.cpp"
        dependencies = [port / "network/native_posix_stat.cpp", port / "platform/linux/files.cpp", port / "platform/linux/paths.cpp",
                        port / "tools/tests/mods_none.cpp", port / "game/ce_network_campaign.cpp", port / "game/ce_network_result_exchange.cpp",
                        port / "network/codec.cpp", port / "network/prerun_flags.cpp", crypto]

        def compile_fixture(label, compat=compatibility, terminal_source=terminal):
            binary = out / label
            command = [*common, '-DLOCKSTEP_TERMINAL_DRIVER="' + str(terminal_source) + '"', str(driver), str(compat),
                       *map(str, dependencies), "-o", str(binary)]
            execute(command, label + "-build")
            receipt["compile"].append(command)
            return binary

        voice = bytearray(96)
        voice[0:7] = b"\x01vorbis"
        voice[12:16] = (1000).to_bytes(4, "little")
        voice[64:68] = b"OggS"
        voice[70:78] = (5000).to_bytes(8, "little")

        def scenario(binary, mode, expected_assertion=None):
            with tempfile.TemporaryDirectory(prefix="authored-", dir=out) as directory:
                root = Path(directory)
                game, user = root / "game", root / "user"
                user.mkdir()
                files = {**FILES, "data/resources/856_res_pc.aya": b"authored mission archive", "data/dialogue/fixture.ogg": voice}
                for name, data in files.items():
                    path = game / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                env = {key: value for key, value in os.environ.items() if not key.startswith(("BEA_", "ADMISSION_", "COMPAT_TEST_")) and
                       key not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
                env.update(HOME=str(root), BEA_GAME_DIR=str(game), BEA_USER_DIR=str(user))
                role = "BEA_LOCKSTEP_" if mode == "automation" else "BEA_NET_"
                env.update({role + "HOST": "127.0.0.1:27600", role + "SECRET": "11" * 32})
                proc = subprocess.run([str(binary), mode], env=env, cwd=root, capture_output=True, text=True, timeout=15)
                label = binary.name + "-" + mode
                (out / (label + ".log")).write_text(proc.stdout + proc.stderr)
                lines = proc.stdout.splitlines()
                completion = "COMPLETE lockstep-admission " + mode
                completed = bool(lines) and lines[-1] == completion and lines.count(completion) == 1
                expected_passes = ["PASS " + assertion for assertion in EXPECTED_PASSES[mode]]
                if expected_assertion:
                    caught = completed and proc.returncode == 1 and ("FAIL " + expected_assertion) in lines
                else:
                    caught = completed and proc.returncode == 0 and all(assertion in lines for assertion in expected_passes)
                receipt["cases"].append({"label": label, "exit": proc.returncode, "expected_assertion": expected_assertion,
                                         "expected_pass_lines": expected_passes if not expected_assertion else [],
                                         "completion_marker": completion, "completed": completed, "accepted": caught})
                if not caught:
                    raise RuntimeError(label + ": causal control failed\n" + proc.stdout + proc.stderr)

        binary = compile_fixture("actual")
        for mode in (
            "present-voice",
            "absent-voice",
            "after-load",
            "ready-barrier",
            "before-update",
            "after-update",
            "between-controllers",
            "automation",
            "retired-finished",
            "retired-ended",
            "retired-cancelled",
            "retired-failed",
            "retired-reconfigure",
        ):
            scenario(binary, mode)
        offline = out / "network-disabled"
        command = [*common, "-DCE_NETWORK_OFF=1", str(driver),
                   str(port / "platform/linux/files.cpp"), str(port / "platform/linux/paths.cpp"),
                   str(port / "tools/tests/mods_none.cpp"), "-o", str(offline)]
        execute(command, "network-disabled-build")
        receipt["compile"].append(command)
        scenario(offline, "network-disabled")
        assertion = "real missing dialogue revokes tick completion before input or hash publication"
        changed = out / "compatibility-no-failure.cpp"
        original = compatibility.read_text()
        needle = "if (state.snapshot.tree.find(name) != state.snapshot.tree.end())"
        if original.count(needle) != 1:
            raise RuntimeError("failed-open mutation anchor changed")
        changed.write_text(original.replace(needle, "if (false)", 1))
        scenario(compile_fixture("ignore-reader-failure", compat=changed), "after-update", assertion)
        original = controller.read_text()
        for signature in ("bool AdmissionCurrent(", "bool AdmissionLive("):
            original = original.replace(actual_body(original, signature), signature + ") { return true; }", 1)
        changed = out / "lockstep-no-live-guard.cpp"
        changed.write_text(original)
        terminal_mutant = out / "terminal-no-live-guard.cpp"
        terminal_mutant.write_text(terminal.read_text().replace('#include "../../game/ce_network_lockstep.cpp"', '#include "' + str(changed) + '"', 1))
        scenario(compile_fixture("ignore-session-failure", terminal_source=terminal_mutant), "after-update", assertion)
        original = controller.read_text()
        if original.count("||AdmissionRetired()") != 2:
            raise RuntimeError("retired-admission mutation anchor changed")
        changed = out / "lockstep-recheck-retired-admission.cpp"
        changed.write_text(original.replace("||AdmissionRetired()", ""))
        terminal_mutant = out / "terminal-recheck-retired-admission.cpp"
        terminal_mutant.write_text(terminal.read_text().replace(
            '#include "../../game/ce_network_lockstep.cpp"', '#include "' + str(changed) + '"'))
        scenario(compile_fixture("recheck-retired-admission", terminal_source=terminal_mutant),
                 "retired-finished", "retired terminal admission does not query its destroyed context")
        receipt["mutations"] = ["ignore-reader-failure", "ignore-session-failure", "recheck-retired-admission"]
        receipt["source_sha256"] = {str(path.relative_to(port)): hashlib.sha256(path.read_bytes()).hexdigest()
                                    for path in (controller, compatibility, driver, terminal)}
        print("PASS focused authored-reader/lockstep admission controls")
        print(out / "result.json")
    finally:
        (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
