#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise native signaling boot control flow with authored providers, clocks and protocol adapters.

No HTTP, game, real credential, native ICE backend, campaign ledger or platform acceptance.
The exchange context and START cryptography are adapters; their independent bridge checks own crypto evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def block(source: str, pattern: str, *, semicolon: bool = False) -> str:
    match = re.search(pattern, source, re.M)
    if not match:
        raise ValueError("production declaration missing: " + pattern)
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'',
                    lambda value: " " * len(value.group()), source, flags=re.S)
    start = match.start()
    brace = masked.index("{", match.end())
    depth, end = 1, brace + 1
    while depth:
        depth += (masked[end] == "{") - (masked[end] == "}")
        end += 1
    if semicolon:
        end = source.index(";", end) + 1
    return source[start:end]


def once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError("causal control no longer matches")
    return source.replace(old, new)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--profile", choices=("native", "ordinary-ice"), default="native")
    parser.add_argument("--causal-controls", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    destination = repo / "local-data/port/qa"
    destination.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="signal-boot-", dir=destination))
    output.chmod(0o700)
    print("Signaling boot fixture output:", output, flush=True)
    names = ["platform/common/ce_netlobby.cpp", "network/ice_game.cpp",
             "game/ce_network_boot_authority.cpp", "game/ce_network_boot_authority.h",
             "tools/tests/network_signaling_boot_driver.cpp", "network/tests/ice_signaling_consumer_tests.cpp", "game/ce_network_boot.cpp"]
    inputs = {name: (repo / "port" / name).read_bytes() for name in names}
    source = inputs[names[0]].decode()
    transition = block(source, r"^struct CeLobbySignalingTransition\b", semicolon=True)
    state_start = source.index("std::shared_ptr<CeBootSignalAuthority> gSignalAuthority;")
    admission = block(source, r"^bool SignalAdmitLaunch\(")
    state_end = source.index(admission) + len(admission)
    core = transition + "\n" + source[state_start:state_end]
    messages = [item for item in re.findall(r"enum\s+\w*\s*\{[^}]*\};", source, re.S) if "MSG_" in item]
    if not messages:
        raise ValueError("production message enum missing")
    functions = ["Close", "Forget", "ScrubEnvironment", "BeginRestart", "CE_LobbyLeave",
                 "CE_LobbySignalingSelect", "CE_LobbySignalingClear", "CE_LobbySignalingSelected",
                 "CE_LobbySignalingOrigin", "CE_LobbySignalingView", "CE_LobbySignalingBeginPreflight",
                 "CE_LobbySignalingPump", "CE_LobbySignalingAccept", "CE_LobbySignalingRetry",
                 "CE_LobbySignalingGameSecret", "CE_LobbySignalingBootPrepared"]
    declarations = re.findall(r"^(?:static )?(?:constexpr|const)[^;\n]*\bMSG_\w+[^;\n]*;", source, re.M)
    actions = "\n".join(messages + declarations) + "\n" + "\n".join(
        block(source, r"^(?:extern \"C\" )?(?:void|bool|int|CeIceSignalView|const char\*)\s+" + name + r"\(")
        for name in functions)
    actions += "\n" + block(source, r"^struct Boot\b", semicolon=True)
    boot_source = inputs["game/ce_network_boot.cpp"].decode()
    boot_entry = block(boot_source, r'^extern "C" int CE_BootStart\(')
    roles = next(item for item in re.findall(r'(?:const )?bool\s+[^;]+;', boot_entry) if "BEA_NET_HOST" in item)
    intent_variables = re.findall(r'\b(\w+)\s*=', roles)
    intent_kinds = re.findall(r'Present\s*\(\s*"BEA_NET_(HOST|JOIN)"\s*\)', roles)
    if len(intent_variables) != 2 or len(intent_kinds) != 2:
        raise ValueError("actual boot role declaration changed")
    intent_names = dict(zip(intent_kinds, intent_variables))
    intent_pattern = r'if\s*\(\s*!' + intent_names["HOST"] + r'\s*&&\s*!' + intent_names["JOIN"] + r'\s*\)'
    intent_match = re.search(intent_pattern, boot_entry)
    if not intent_match:
        raise ValueError("actual boot intent gate changed")
    if boot_entry[intent_match.end():].lstrip().startswith("{"):
        ordinary = block(boot_entry, r'^\s*' + intent_pattern)
    else:
        ordinary = boot_entry[intent_match.start():boot_entry.index(";", intent_match.end()) + 1]
    boot_gate = block(boot_source, r'^bool Present\(') + "\nint FixtureNativeBootEntry() { unsigned prepared = 0;\n" + roles + "\n" + ordinary + "\nreturn 2; }\n"
    result = {"scope": "production lobby, authority and consumer control flow with authored backend adapters", "profile": args.profile,
              "limits": ["no HTTP", "no real credential", "no game", "no campaign ledger", "no libjuice",
                         "no cryptographic or Windows acceptance"],
              "inputs": {name: hashlib.sha256(data).hexdigest() for name, data in inputs.items()}, "checks": []}
    trials = [("journey", core, actions, False), ("pending-failure", core, actions, False), ("pending-expiry", core, actions, False)]
    if args.profile == "ordinary-ice":
        trials = [("ordinary-ice", core, actions, False)]
    if args.causal_controls and args.profile == "native":
        role_pattern = r'\} else \{\n[ \t]*SetEnv\([^;\n]*"BEA_NET_HOST"[^;\n]*;\n[ \t]*\}'
        role_control, matches = re.subn(role_pattern, "} else {}", actions, count=1)
        if matches != 1:
            raise ValueError("native role export control no longer matches")
        trials += [
            ("role-intent", core, role_control, True),
            ("no-directory", core, actions.replace("CE_NetSelectSignalingDirectory(gSignalAuthority->Directory())", "true"), True),
            ("missing-game-plan", core, once(actions, "CE_NetRestoreSignalingGamePlan(plan)", "true"), True),
            ("secret-environment", core, once(actions, "if (!gSignalSelected) {", "if (true) {"), True),
            ("pair-closed-on-start", core, once(actions, "Close(L.host);", "Close(L.host); SignalTerminal();"), True),
            ("disk-handoff", core, once(actions, "gSignalSelected ? SignalPrepareStart(h)", "false ? SignalPrepareStart(h)"), True),
            ("refreshed-plan", core, once(actions, "bool restored = CE_NetRestoreSignalingGamePlan(plan);",
                "plan.requestedAt = uint64_t(CE_HandoffNow()); plan.setupDeadlineMs = CE_NetNowMs() + 120000;\n    bool restored = CE_NetRestoreSignalingGamePlan(plan);"), True),
        ]
    environment = {"PATH": os.defpath, "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"}
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise ValueError("C++ compiler unavailable")
    for label, trial_core, trial_actions, negative in trials:
        root = output / label
        root.mkdir(mode=0o700)
        (root / "tmp").mkdir(mode=0o700)
        trial_environment = environment | {"TMPDIR": str(root / "tmp")}
        for relative in ["network.h", "ice_game.h", "ice_signaling.h", "ice_session.h", "ice_exchange.h"]:
            data = (repo / "port/network" / relative).read_bytes()
            (root / relative).write_bytes(data)
            result["inputs"]["network/" + relative] = hashlib.sha256(data).hexdigest()
            (root / "network").mkdir(exist_ok=True)
            (root / "network" / relative).write_bytes(data)
        for relative in ["game/ce_network_boot_authority.h", "game/ce_network_boot_authority.cpp",
                         "game/ce_network_boot.h", "game/ce_network_stl.h", "network/signaling_provider.h", "network/signaling_https.h", "network/compatibility.h",
                         "platform/common/ce_netcode.h", "platform/common/ce_netlobby.h"]:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            data = (repo / "port" / relative).read_bytes()
            target.write_bytes(data)
            result["inputs"][relative] = hashlib.sha256(data).hexdigest()
        fixture = inputs["tools/tests/network_signaling_boot_driver.cpp"].decode()
        (root / "fixture.cpp").write_text(fixture)
        (root / "signaling_consumer_fixture.inc").write_bytes(inputs["network/tests/ice_signaling_consumer_tests.cpp"])
        for relative, selector in [("fixture_state.h", "STATE"), ("ice_credentials.h", "CREDENTIALS"), ("juice/juice.h", "JUICE")]:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("#define CE_SIGNAL_FIXTURE_" + selector + "\n" + inputs["network/tests/ice_signaling_consumer_tests.cpp"].decode() + "\n#undef CE_SIGNAL_FIXTURE_" + selector + "\n")
        (root / "ice_game.cpp").write_bytes(inputs["network/ice_game.cpp"])
        (root / "production_lobby_core.inc").write_text(trial_core)
        (root / "production_lobby_actions.inc").write_text(trial_actions)
        (root / "production_boot_gate.inc").write_text(boot_gate)
        selected = root / "selected"
        selected.mkdir(mode=0o700)
        binary = root / "boot-check"
        command = [compiler, "-std=gnu++14", "-O0", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                   "-Wno-unused-variable", "-Wno-error=extra", "-DCE_NETWORK_ICE=1", "-pthread", "-I", str(root),
                   str(root / "fixture.cpp"), str(root / "ice_game.cpp"), str(root / "game/ce_network_boot_authority.cpp"),
                   "-o", str(binary)]
        if args.profile == "native":
            command.insert(1, "-DCE_NETWORK_SIGNALING=1")
        built = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=trial_environment)
        (root / "compile.private.log").write_bytes(built.stdout)
        entry = {"name": label, "negative_control": negative, "compiled": built.returncode == 0, "passed": False}
        result["checks"].append(entry)
        if built.returncode:
            print("Compile failed:", label, "(private diagnostics retained)")
            break
        outcome = "failure" if label == "pending-failure" else "expiry" if label == "pending-expiry" else "ready"
        ran = subprocess.run([str(binary), str(selected), outcome], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             env=trial_environment, timeout=30)
        (root / "run.private.log").write_bytes(ran.stdout)
        entry["exit"] = ran.returncode
        summaries = re.findall(rb"^SIGNAL_BOOT checks=(\d+) failures=(\d+)$", ran.stdout, re.M)
        children = re.findall(rb"^SIGNAL_BOOT_CHILD checks=(\d+) failures=(\d+)$", ran.stdout, re.M)
        if summaries:
            entry["assertions"], entry["failures"] = map(int, summaries[-1])
        expected = {
            "role-intent": "native child crosses actual boot role intent gate before compatibility binding",
            "no-directory": "native request does not require manual environment",
            "missing-game-plan": "child reaches actual game consumer preparation",
            "secret-environment": "native boot has no disk or environment secret",
            "pair-closed-on-start": "normal lobby close cancels local stage while preserving pair",
            "disk-handoff": "native START writes no secret handoff or environment",
            "refreshed-plan": "exec does not refresh original START deadlines",
        }
        completed = bool(summaries and int(summaries[-1][0]) > 0)
        abnormal = any(text in ran.stdout for text in (b"terminate called", b"core dumped", b"Segmentation fault"))
        if negative:
            entry["passed"] = completed and ran.returncode == 1 and int(summaries[-1][1]) > 0 and not abnormal and (
                "FAIL " + expected[label]).encode() in ran.stdout
        else:
            entry["passed"] = completed and ran.returncode == 0 and int(summaries[-1][1]) == 0 and (
                args.profile == "ordinary-ice" or bool(children and int(children[-1][0]) > 0 and int(children[-1][1]) == 0))
        print(label + ": " + ("control rejected" if negative and entry["passed"] else "PASS" if entry["passed"] else "FAIL"), flush=True)
        if not entry["passed"]:
            break
        if args.profile == "ordinary-ice":
            leak = list(command)
            leak.insert(1, "-DCE_SIGNAL_FIXTURE_LEAK_FACTORY=1")
            leak[-1] = str(root / "factory-reference-negative")
            linked = subprocess.run(leak, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=trial_environment)
            (root / "factory-reference-negative.private.log").write_bytes(linked.stdout)
            rejected = linked.returncode != 0 and b"CeCreateSignalingProvider" in linked.stdout and (
                b"undefined reference" in linked.stdout or b"undefined symbol" in linked.stdout)
            result["ordinary_factory_reference_rejected"] = rejected
            print("factory-reference negative link: " + ("rejected" if rejected else "FAIL"), flush=True)
            if not rejected:
                entry["passed"] = False
    result["passed"] = len(result["checks"]) == len(trials) and all(item["passed"] for item in result["checks"])
    (output / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
