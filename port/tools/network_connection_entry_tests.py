#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drive the real connection menu against authored game/backend boundaries.

The production page body and join-code codec are compiled unchanged. Only game
includes are replaced by authored menu stubs. No game data, SDL, display, real
clipboard, sockets, accounts or player files are used. The optional
--terminal-state-only fixture performs only authored status updates. Output must be fresh and
below this checkout's ignored local-data.
When testing separate candidates, --api-include supplies the frozen ICE game
adapter header; the integrated tree uses its own port/network header.
"""

import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (40, 40))
    resource.setrlimit(resource.RLIMIT_AS, (768 * 1024 * 1024, 768 * 1024 * 1024))
    if os.getpriority(os.PRIO_PROCESS, 0) < 10:
        os.setpriority(os.PRIO_PROCESS, 0, 10)


def fixture_page(text):
    game_headers = (
        "common.h", "frontend.h", "PauseMenu.h", "MenuItem.h", "MenuOptionsList.h",
        "FEMessBox.h", "CLIParams.h", "ltshell.h", "renderinfo.h", "text.h",
        "TransitionHelpers.h", "keytrap.h", "data/MissionScripts/text/text.stf",
        "ce_network_boot.h",
    )
    for header in game_headers:
        directive = '#include "' + header + '"'
        if text.count(directive) != 1:
            raise ValueError("unexpected game include boundary")
        text = text.replace(directive, "// authored fixture supplies this game boundary", 1)
    for header in ("ce_netcode.h", "ce_netlobby.h"):
        original = '#include "../platform/common/' + header + '"'
        if text.count(original) != 1:
            raise ValueError("unexpected lobby include boundary")
        text = text.replace(original, '#include "' + header + '"', 1)
    text = text.replace('#include "../network/ice_game.h"', '#include "ice_game.h"')
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--m32", action="store_true")
    parser.add_argument("--ice-enabled", action="store_true")
    parser.add_argument("--api-include", type=Path)
    parser.add_argument("--game-api-include", type=Path)
    parser.add_argument("--lobby-api-include", type=Path)
    parser.add_argument("--terminal-state-only", action="store_true",
                        help="run authored status updates without menu input actions")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.output_dir.resolve()
    if (repo / "local-data").resolve() not in output.parents or output.exists():
        parser.error("output-dir must be fresh and below repo local-data")
    cxx, cc = shutil.which(args.compiler), shutil.which(args.cc)
    if not cxx or not cc:
        parser.error("requested compiler is unavailable")
    api_include = (args.api_include or repo / "port/network").resolve()
    if not (api_include / "ice_game.h").is_file():
        parser.error("the ICE game adapter header is required; supply --api-include for a separate frozen candidate")
    os.umask(0o077)
    output.mkdir(parents=True, mode=0o700)
    env = {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"}
    for key in ("HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_RUNTIME_DIR", "TMPDIR"):
        directory = output / key.lower()
        directory.mkdir(mode=0o700)
        env[key] = str(directory)
    try:
        page = fixture_page((repo / "port/game/ce_netpages.cpp").read_text())
    except ValueError:
        parser.error("production include boundary changed; inspect the source before updating the fixture")
    staged = output / "ce_netpages_fixture.cpp"
    staged.write_text(page)
    mode = ["-m32"] if args.m32 else []
    sha = output / "sha256.o"
    binary = output / "player_connection_entry_tests"
    commands = [
        ("compile-sha", [cc, "-O1", *mode, "-c", str(repo / "port/updater/sha256.c"), "-o", str(sha)], 30),
        ("compile-menu", [cxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                          "-Wno-unused-parameter", *mode, '-DCE_VERSION="authored-fixture"',
                          '-DCE_NETPAGES_SOURCE="' + str(staged) + '"',
                          *(["-DCE_NETWORK_ICE=1"] if args.ice_enabled else []),
                          "-I", str(api_include),
                          "-I", str(repo / "port/network/tests"),
                          "-I", str(repo / "port/platform/common"),
                          "-I", str(repo / "port/network"),
                          str(repo / ("port/network/tests/player_connection_entry_failure_driver.cpp"
                     if args.terminal_state_only else "port/network/tests/player_connection_entry_driver.cpp")),
                          str(repo / "port/platform/common/ce_netcode.cpp"), str(sha),
                          "-o", str(binary)], 60),
        ("test", [str(binary)], 15),
    ]
    extra_api = [args.game_api_include or repo / "port/game",
                 args.lobby_api_include or repo / "port/platform/common"]
    include_flags = [flag for directory in extra_api for flag in ("-I", str(directory))]
    for phase, command, time_limit in commands:
        if command[0] in (cxx, cc):
            command[1:1] = include_flags
    result = {"ok": False, "checks": 0, "m32": args.m32, "ice_enabled": args.ice_enabled}
    for phase, command, timeout in commands:
        result["phase"] = phase
        try:
            with (output / (phase + ".stdout")).open("wb") as stdout, \
                 (output / (phase + ".stderr")).open("wb") as stderr:
                completed = subprocess.run(command, cwd=output, env=env, stdin=subprocess.DEVNULL,
                                           stdout=stdout, stderr=stderr, timeout=timeout,
                                           preexec_fn=limits, check=False)
        except (OSError, subprocess.TimeoutExpired):
            result["execution_error"] = True
            break
        result["exit_code"] = completed.returncode
        if phase == "test":
            try:
                summary = json.loads((output / "test.stdout").read_text())
                checks, failures = summary["checks"], summary["failures"]
                if type(checks) is not int or type(failures) is not int or checks <= 0 or failures < 0:
                    raise ValueError("invalid fixture summary")
                result.update(checks=checks, failures=failures)
                result["ok"] = completed.returncode == 0 and failures == 0
            except (ValueError, TypeError, KeyError):
                result["invalid_summary"] = True
        if completed.returncode:
            break
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
