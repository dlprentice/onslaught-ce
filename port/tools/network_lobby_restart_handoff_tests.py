#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-bound owned restart, boot and return checks using authored data only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LOBBY = ROOT / "port/platform/common/ce_netlobby.cpp"
CODE = ROOT / "port/platform/common/ce_netcode.cpp"
PAGES = ROOT / "port/game/ce_netpages.cpp"
DRIVER = ROOT / "port/tools/tests/network_lobby_restart_driver.cpp"


def function(text: str, name: str) -> tuple[int, int]:
    match = re.search(r"(?m)^[^\n;{}]*\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", text)
    if not match:
        raise RuntimeError(f"missing source function: {name}")
    depth, end = 1, match.end()
    while depth and end < len(text):
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    if depth:
        raise RuntimeError(f"unbalanced source function: {name}")
    return match.start(), end


def replace_once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise RuntimeError("causal-control anchor changed")
    return text.replace(before, after, 1)


def without_launch_admission(source: str) -> str:
    """Remove the same guard in Boot's ICE, direct and compiled-out alternatives.

    The authored driver compiles the direct alternative. Mutating the first
    textual occurrence would now change only an inactive ICE branch.
    """
    start, end = function(source, "Boot")
    boot = source[start:end]
    before = 'h.kind == CE_HANDOFF_LAUNCH && CE_BootCampaignAdmit(&h)'
    if boot.count(before) != 3:
        raise RuntimeError("Boot launch-admission alternatives changed")
    boot = boot.replace(before, 'h.kind == CE_HANDOFF_LAUNCH')
    return source[:start] + boot + source[end:]


def command(args: list[str], log: Path) -> int:
    with log.open("w") as output:
        return subprocess.run(args, cwd=ROOT, stdout=output,
                              stderr=subprocess.STDOUT, check=False).returncode


def trial(root: Path, label: str, text: str, m32: bool,
          expected: str | None = None, pages: str | None = None,
          write_failure_only: bool = False) -> dict:
    directory = root / label
    directory.mkdir()
    cases = directory / "cases"
    cases.mkdir()
    definitions = []
    names = {"userdir": "UserDir", "handoffpath": "HandoffPath",
             "dotted": "Dotted", "beginrestart": "BeginRestart",
             "setenv": "SetEnv", "hex": "Hex",
             "scrubenvironment": "ScrubEnvironment",
             "returntofrontend": "ReturnToFrontEnd", "boot": "Boot",
             "leave": "CE_LobbyLeave", "shutdown": "CE_LobbyShutdown", "afterexit": "CE_LobbyAfterExit",
             "lobbystart": "LobbyStart", "forget": "Forget"}
    fragments = {}
    for key, name in names.items():
        start, end = function(text, name)
        part = text[start:end]
        if name == "Boot":
            part = re.sub(r"\bBoot\s*\(\)", "void TestBoot()", part, count=1)
        fragments[key] = part
    start = text.index("CE_CampaignLaunch gContinuation = {};")
    _, end = function(text, "AttachCampaignReturn")
    fragments["transfer"] = text[start:end]
    pages = PAGES.read_text() if pages is None else pages
    for key, name in (("menushutdown", "PortCeNetShutdown"), ("menuclose", "Close"),
                      ("menuclearsignal", "ClearSignalUi")):
        start, end = function(pages, name)
        fragments[key] = pages[start:end]
    for key, part in fragments.items():
        path = directory / f"{key}.inc"
        path.write_text(part + "\n")
        definitions.append(f'-DACTUAL_{key.upper()}="{path}"')
    flags = ["-m32"] if m32 else []
    sha = ROOT / "port/updater/sha256.c"
    obj = directory / "sha256.o"
    result = {"label": label, "m32": m32,
              "lobby_sha256": hashlib.sha256(text.encode()).hexdigest(),
              "driver_sha256": hashlib.sha256(DRIVER.read_bytes()).hexdigest(),
              "menu_sha256": hashlib.sha256(pages.encode()).hexdigest(),
              "netcode_sha256": hashlib.sha256(CODE.read_bytes()).hexdigest(),
              "expected_negative": expected is not None}
    cc = os.environ.get("CC", "gcc")
    cxx = os.environ.get("CXX", "g++")
    result["sha_build_exit"] = command([cc, *flags, "-O1", "-c", str(sha), "-o", str(obj)],
                                       directory / "sha-build.private.log")
    if result["sha_build_exit"]:
        return {**result, "passed": False}
    exe = directory / "test"
    build = [cxx, *flags, "-std=c++17", "-Wall", "-Wextra", "-O1",
             "-I", str(ROOT / "port/platform/common"), *definitions,
             str(DRIVER), str(CODE), str(obj), "-o", str(exe)]
    result["build_exit"] = command(build, directory / "build.private.log")
    if result["build_exit"]:
        return {**result, "passed": False}
    mode_results = []
    outputs = []
    failure_modes = ("write-failure-relay-host", "write-failure-relay-guest",
                     "write-failure-direct-host", "write-failure-direct-guest",
                     "write-failure-direct-host-listener-failed")
    modes = (failure_modes if write_failure_only else
             (None, "shutdown-host", "shutdown-guest", "cancel-host", "cancel-guest", "shutdown-idle"))
    if not write_failure_only and expected is None:
        modes += failure_modes
    for mode in modes:
        case_dir = cases if mode is None else cases / mode
        if mode is not None:
            case_dir.mkdir()
        log = directory / ("run.private.log" if mode is None else mode + ".private.log")
        args = [str(exe), str(case_dir)] + ([] if mode is None else [mode])
        status = command(args, log)
        body = log.read_text()
        outputs.append(body)
        mode_results.append({"mode": mode or "handoff", "exit_code": status,
                             "checks": sum(line.startswith(("PASS ", "FAIL ")) for line in body.splitlines()),
                             "failures": sum(line.startswith("FAIL ") for line in body.splitlines())})
    result["exit_code"] = next((row["exit_code"] for row in mode_results if row["exit_code"]), 0)
    result["callers"] = mode_results
    output = "\n".join(outputs)
    result["checks"] = sum(line.startswith(("PASS ", "FAIL ")) for line in output.splitlines())
    result["failures"] = sum(line.startswith("FAIL ") for line in output.splitlines())
    result["fixture_files"] = sum(p.is_file() for p in cases.rglob("*"))
    if expected:
        result["intended_assertion_observed"] = f"FAIL {expected}" in output
        result["passed"] = result["exit_code"] != 0 and result["intended_assertion_observed"]
    else:
        result["passed"] = result["exit_code"] == 0 and result["checks"] >= 30 and result["failures"] == 0
    return result


def write_failure_trials(output: Path, source: str, m32: bool) -> int:
    """Only the authored handoff-write failure callers and their causal controls."""
    start, end = function(source, "BeginRestart")
    restart = source[start:end]
    guard = restart.index("\t\tif (CE_NetGameIceRequested())")
    tail = restart.index('\n\t\tText(L.st.notice, sizeof L.st.notice, "The game could not be started: its private start file could not be written.");', guard)
    recovery = restart[guard:tail]
    replacements = [
        ("relay-recovery-omitted", "", "relay handoff write failure is terminal"),
        ("relay-waiting-state", replace_once(recovery, "L.state = CE_LOBBY_FAILED;",
         "L.state = CE_LOBBY_HOST_WAITING;"), "relay handoff write failure is terminal"),
        ("relay-recreates-exchange", replace_once(recovery, "\t\t\treturn;",
         "\t\t\t(void)Create();\n\t\t\treturn;"), "relay handoff write failure never recreates its exchange"),
        ("direct-retry-disabled", replace_once(recovery, "if (CE_NetGameIceRequested())",
         "if (true)"), "direct host handoff write failure retries listening"),
    ]
    changes = [(label, restart[:guard] + replacement + restart[tail:], expected)
               for label, replacement, expected in replacements]
    changes.append(("failed-direct-retry-waits", replace_once(restart,
        "\t\tif (L.host && !Create())\n\t\t{\n\t\t\tL.state = CE_LOBBY_FAILED;",
        "\t\tif (L.host && !Create())\n\t\t{\n\t\t\tL.state = CE_LOBBY_HOST_WAITING;"),
        "failed direct retry is terminal"))
    results = [trial(output, "actual", source, m32, write_failure_only=True)]
    if results[0]["passed"]:
        for label, changed_restart, expected in changes:
            changed = source[:start] + changed_restart + source[end:]
            results.append(trial(output, label, changed, m32, expected, write_failure_only=True))
    passed = len(results) == 1 + len(changes) and all(result["passed"] for result in results)
    receipt = {"passed": passed, "results": results,
               "fixture_policy": "Authored missing parent only; no game data, ICE directory access or sockets.",
               "limits": ["Actual handoff writer and source-bound BeginRestart; authored transport and UI state.",
                          "No game, process restart, relay, VM, Windows runtime or real-player acceptance."]}
    (output / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": passed, "receipt": str(output / "result.json")}))
    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m32", action="store_true")
    parser.add_argument("--write-failure-only", action="store_true",
                        help="only authored handoff-write failure callers and causal controls")
    args = parser.parse_args()
    os.umask(0o077)
    evidence = ROOT / "local-data/port/network-lobby-restart-handoff-tests"
    evidence.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=evidence))
    source = LOBBY.read_text()
    if args.write_failure_only:
        return write_failure_trials(output, source, args.m32)
    results = [trial(output, "actual", source, args.m32)]
    if not results[0]["passed"]:
        # Keep the concrete baseline error instead of hiding it behind a later
        # stale mutation anchor. A compile failure is never a causal rejection.
        (output / "result.json").write_text(json.dumps({
            "passed": False, "results": results,
            "reason": "Baseline control failed; causal variants were not run.",
        }, indent=2) + "\n")
        print(json.dumps({"passed": False, "receipt": str(output / "result.json")}))
        return 1
    controls = [
        ("owner-publication-removed", replace_once(source,
         'SetEnv("BEA_NET_HANDOFF_OWNER", gHandoffReceipt[0] ? gHandoffReceipt : NULL);',
         'SetEnv("BEA_NET_HANDOFF_OWNER", NULL);'), "boot consumes owned launch"),
        ("pending-clear-removed", replace_once(source,
         'CE_HandoffOwnedClear(gHandoffPath, &gHandoffOwner, NULL);',
         '(void)gHandoffOwner;'), "cancellation clears only its owned entry"),
        ("return-cleanup-removed", replace_once(source,
         'Exec(args);\n    ClearPendingHandoff();\n}',
         'Exec(args);\n}'), "failed execution clears its owned outcome"),
        ("launch-admission-removed", without_launch_admission(source),
         "launch admits its typed campaign before lockstep"),
        ("fresh-name-removed", replace_once(source,
         'hex[2 * i] = digits[random[i] >> 4];', "hex[2 * i] = '0';").replace(
         'hex[2 * i + 1] = digits[random[i] & 15];', "hex[2 * i + 1] = '0';", 1),
         "retry allocates a fresh owned entry"),
        ("return-context-omitted", replace_once(source,
         'AttachCampaignReturn(&h, good);', '(void)good;'),
         "confirmed return keeps the campaign identity"),
        ("session-reuse-omitted", replace_once(source,
         'campaign && !ContinueCampaignNonce(L.nonce)', 'false'),
         "host continuation reuses the completed session"),
        ("attempt-reused", replace_once(source,
         'uint64_t attempt = ++gContinuationAttempt;',
         'uint64_t attempt = gContinuationAttempt;'),
         "host continuation allocates the next attempt"),
        ("selection-resume-omitted", replace_once(source,
         'if (gContinuation.version) L.level = gContinuation.mission;', '(void)gContinuation;'),
         "return restores mission selection for retry"),
    ]
    leave_start, leave_end = function(source, "CE_LobbyLeave")
    leave = source[leave_start:leave_end]
    clear = "    memset(&gContinuation, 0, sizeof gContinuation);\n    gContinuationAttempt = 0;\n"
    omitted = replace_once(leave, clear, "")
    guard = omitted.index("if (L.state == CE_LOBBY_STARTING)")
    premature = omitted[:guard] + clear + omitted[guard:]
    controls.extend([
        ("abandoned-context-retained", source[:leave_start] + omitted + source[leave_end:],
         "explicit leave releases the abandoned campaign nonce context"),
        ("starting-context-cleared", source[:leave_start] + premature + source[leave_end:],
         "starting leave preserves campaign retry context"),
    ])
    if results[0]["passed"]:
        results.extend(trial(output, label, changed, args.m32, expected)
                       for label, changed, expected in controls)
    if results[0]["passed"]:
        start, end = function(source, "CE_LobbyShutdown")
        automatic = source[start:end]
        no_guard = replace_once(automatic, "    if (L.state == CE_LOBBY_STARTING) return;\n", "")
        results.append(trial(output, "automatic-cancels-start", source[:start] + no_guard + source[end:], args.m32,
                             "automatic shutdown preserves the queued launch"))
        pages = PAGES.read_text()
        start, end = function(pages, "PortCeNetShutdown")
        old_caller = replace_once(pages[start:end], "CE_LobbyShutdown();", "CE_LobbyLeave();")
        results.append(trial(output, "automatic-caller-cancels", source, args.m32,
                             "automatic shutdown preserves the queued launch", pages[:start] + old_caller + pages[end:]))
        start, end = function(pages, "Close")
        wrong_cancel = replace_once(pages[start:end], "CE_LobbyLeave();", "CE_LobbyShutdown();")
        results.append(trial(output, "explicit-caller-preserves", source, args.m32,
                             "explicit menu close cancels its queued launch", pages[:start] + wrong_cancel + pages[end:]))
        for label, before, expected in (
            ("explicit-signal-ui-retained", "ClearSignalUi();", "explicit menu close clears signaling approval"),
            ("explicit-code-retained", "mCode->Clear();", "explicit menu close clears the code field"),
            ("explicit-address-retained", "mAddress->Clear();", "explicit menu close clears the address field"),
        ):
            omitted = replace_once(pages[start:end], before, "(void)0;")
            results.append(trial(output, label, source, args.m32, expected,
                                 pages[:start] + omitted + pages[end:]))
        start, end = function(source, "CE_LobbyLeave")
        leave = source[start:end]
        guard_match = re.search(r"(?m)^[ \t]*if \(L.state == CE_LOBBY_STARTING\)\s*return;\n", leave)
        if not guard_match:
            raise RuntimeError("explicit cancellation guard changed")
        guard = guard_match.group()
        early_guard = replace_once(leave, guard, "")
        pos = early_guard.index("ClearPendingHandoff();")
        early_guard = early_guard[:pos] + guard.strip() + "\n    " + early_guard[pos:]
        results.append(trial(output, "explicit-guard-before-cancel", source[:start] + early_guard + source[end:], args.m32,
                             "explicit menu close cancels its queued launch"))
    passed = len(results) == 19 and all(result["passed"] for result in results)
    receipt = {"passed": passed, "results": results,
               "fixture_policy": "Authored records and every trial are retained; no unlink or rename.",
               "limits": ["Source-bound callers with authored dependencies, no actual process restart.",
                          "The driver compiles the direct branch; ICE and network-off alternatives are not runtime coverage.",
                          "No game, GPU, network, VM, Windows runtime or real-player acceptance."]}
    (output / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": passed, "receipt": str(output / "result.json")}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
