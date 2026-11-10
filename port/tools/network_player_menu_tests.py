#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the actual network menu with in-process providers; no game or sockets.

The fixture covers menu decisions and dispatch only. Native display/input,
transport, gameplay, and Linux/Windows player acceptance require separate work.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = {'multi-address': 6,
 'single-address': 5,
 'no-address': 4,
 'chooser-back': 7,
 'address-appeared': 5,
 'stale-choice': 8,
 'stale-to-none': 8,
 'join-input': 3,
 'join-back': 3,
 'waiting-back': 5,
 'start-not-ready': 5,
 'ready-start': 7,
 'offered-guard': 1,
 'campaign-guest-disabled': 8,
 'campaign-not-ready-disabled': 4,
 'campaign-late-back': 6,
 'campaign-queued': 10,
 'campaign-late-not-ready': 8,
 'campaign-late-not-host': 8,
 'campaign-unavailable': 8,
 'campaign-failed': 8,
 'campaign-stale': 13,
 'campaign-unrendered': 5,
 'campaign-unrendered-reentry': 7,
 'campaign-ordinary-start': 6}
CONTROLS = {'automatic-host': ('multi-address', 5),
 'eager-host': ('chooser-back', 1),
 'back-leave': ('chooser-back', 4),
 'refresh-before-confirm': ('stale-choice', 4),
 'campaign-current-mission': ('campaign-stale', 6),
 'campaign-refusal-fallback': ('campaign-unavailable', 7),
 'campaign-queued-quit': ('campaign-queued', 7),
 'campaign-cache-before-render': ('campaign-stale', 6),
 'campaign-unrendered-admit': ('campaign-unrendered', 4),
 'campaign-reentry-retains-intent': ('campaign-unrendered-reentry', 5),
 'campaign-stale-auto-retry': ('campaign-stale', 6),
 'campaign-refusal-cleanup': ('campaign-unavailable', 7)}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def control(source: str, name: str) -> str:
    if name == "automatic-host":
        old = "CE_LobbyHostAt(mHostAddresses[mHostAddressIndex])"
        new = "CE_LobbyHost()"
    elif name == "eager-host":
        old = "case NA_HOST:\n            RefreshHostAddresses();"
        new = "case NA_HOST:\n            CE_LobbyHost();\n            RefreshHostAddresses();"
    elif name == "back-leave":
        old = "case NP_HOST_CHOOSE:\n            mHostAddressCount = mHostAddressIndex = 0;"
        new = "case NP_HOST_CHOOSE:\n            CE_LobbyLeave();\n            mHostAddressCount = mHostAddressIndex = 0;"
    elif name == "refresh-before-confirm":
        old = "case NA_HOST_BEGIN:\n            if (mPage != NP_HOST_CHOOSE) return;"
        new = old + "\n            RefreshHostAddresses();"
    elif name == "campaign-current-mission":
        old = "CE_LobbyCampaignStart(mCampaignMission)"
        new = "CE_LobbyCampaignStart(([&]() { CE_LobbyStatus current; CE_LobbyStatusGet(&current); return current.level; })())"
    elif name == "campaign-refusal-fallback":
        old = "const CE_LobbyCampaignStartResult result = CE_LobbyCampaignStart(mCampaignMission);"
        new = old + "\n            if (result != CE_LOBBY_CAMPAIGN_START_QUEUED) CE_LobbyStart();"
    elif name == "campaign-queued-quit":
        old = "case CE_LOBBY_CAMPAIGN_START_QUEUED:\n                Notice("
        new = "case CE_LOBBY_CAMPAIGN_START_QUEUED:\n                PostMessage(LT.GetHWnd(), WM_CLOSE, 0, 0);\n                Notice("
    elif name == "campaign-cache-before-render":
        old = "if (mCampaignMissionPendingKnown) mCampaignMissionPending = s.level;"
        new = old + "\n        if (mCampaignMissionPendingKnown) mCampaignMission = mCampaignMissionPending;"
    elif name == "campaign-unrendered-admit":
        old = "if (mPage != NP_LOBBY || !mCampaignMissionKnown) return;"
        new = "if (mPage != NP_LOBBY) return;"
    elif name == "campaign-reentry-retains-intent":
        old = "mPage = page;\n        mCampaignMissionKnown = false;"
        new = "mPage = page;"
    elif name == "campaign-stale-auto-retry":
        old = 'Notice("The selected mission changed. Check it, then confirm again.");'
        new = old + "\n                (void)[]() { CE_LobbyStatus current; CE_LobbyStatusGet(&current); return CE_LobbyCampaignStart(current.level); }();"
    elif name == "campaign-refusal-cleanup":
        old = 'Notice("Campaign co-op is unavailable for this build or mission.");'
        new = old + "\n                CE_LobbyLeave();"
    else:
        raise ValueError("unknown control")
    if source.count(old) != 1:
        raise ValueError("control anchor does not match exactly once")
    return source.replace(old, new, 1)


def main() -> int:
    parent = ROOT / "local-data/port/network-player-menu-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    out.chmod(0o700)
    source_path = ROOT / "port/game/ce_netpages.cpp"
    driver_path = ROOT / "port/tools/tests/network_player_menu_driver.cpp"
    source = source_path.read_bytes()
    driver = driver_path.read_bytes()
    headers = [ROOT / "port/platform/common" / name for name in ("ce_netlobby.h", "ce_netcode.h")]
    compiler = shutil.which("c++")
    receipt = {
        "schema": "network-player-menu-v1",
        "source_sha256": sha(source), "driver_sha256": sha(driver),
        "headers_sha256": {p.name: sha(p.read_bytes()) for p in headers},
        "cases": [], "controls": [], "compile": [],
        "limits": ["In-process synthetic menu/provider checks only; no game, sockets, devices or VM.",
                   "No public gameplay or native Linux/Windows display, input or player acceptance claim."],
    }
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("BEA_", "SDL_")) and k not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
    isolation = out / "isolation"
    for folder in ("home", "config", "data", "cache", "state", "runtime", "tmp"):
        (isolation / folder).mkdir(parents=True, mode=0o700)
    env.update({"HOME": str(isolation / "home"),
                "XDG_CONFIG_HOME": str(isolation / "config"),
                "XDG_DATA_HOME": str(isolation / "data"),
                "XDG_CACHE_HOME": str(isolation / "cache"),
                "XDG_STATE_HOME": str(isolation / "state"),
                "XDG_RUNTIME_DIR": str(isolation / "runtime"),
                "TMPDIR": str(isolation / "tmp"),
                "SDL_AUDIO_DRIVER": "dummy", "BEA_REPORT_NO_OPEN": "1",
                "BROWSER": "/bin/true"})
    env.pop("DBUS_SESSION_BUS_ADDRESS", None)
    if compiler is None:
        receipt["pass"] = False
        receipt["setup_error"] = "C++ compiler unavailable"
    else:
        for variant in ("baseline", *CONTROLS):
            work = out / variant
            work.mkdir()
            text = source.decode() if variant == "baseline" else control(source.decode(), variant)
            # Keep the entire production implementation; only its project include directives
            # are supplied by the fixture's interface providers and the actual lobby headers.
            text = re.sub(r'^\s*#include\s+"[^"\n]+"\s*$', "", text, flags=re.MULTILINE)
            (work / "menu.inc").write_text(text)
            (work / "driver.cpp").write_bytes(driver)
            binary = work / "driver"
            command = [compiler, "-std=c++17", "-O0", "-DCE_VERSION=\"fixture\"", "-DCE_NETWORK_ENABLED=1",
                       "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                       "-I" + str(ROOT / "port/platform/common"), str(work / "driver.cpp"), "-o", str(binary)]
            with (work / "compile.stdout.private").open("w") as stdout, (work / "compile.stderr.private").open("w") as stderr:
                result = subprocess.run(command, env=env, stdout=stdout, stderr=stderr, timeout=60, check=False)
            receipt["compile"].append({"variant": variant, "exit": result.returncode, "menu_sha256": sha(text.encode())})
            if result.returncode:
                if variant == "baseline":
                    break
                continue
            selected = CASES if variant == "baseline" else [CONTROLS[variant][0]]
            for case in selected:
                with (work / (case + ".stdout.private")).open("w") as stdout, (work / (case + ".stderr.private")).open("w") as stderr:
                    result = subprocess.run([str(binary), case], env=env, stdout=stdout, stderr=stderr, timeout=10, check=False)
                rows = [line.removeprefix("MENU_RESULT ") for line in (work / (case + ".stdout.private")).read_text().splitlines()
                        if line.startswith("MENU_RESULT ")]
                try:
                    counts = json.loads(rows[0]) if len(rows) == 1 else None
                except json.JSONDecodeError:
                    counts = None
                expected = {"checks": CASES[case] if variant == "baseline" else CONTROLS[variant][1],
                            "failures": int(variant != "baseline"), "skipped": 0}
                expected_exit = int(variant != "baseline")
                passed = result.returncode == expected_exit and counts == expected
                row = {"case": case, "variant": variant, "exit": result.returncode,
                       "counts": counts, "expected": expected, "pass": passed}
                receipt["cases" if variant == "baseline" else "controls"].append(row)
        receipt["pass"] = (len(receipt["compile"]) == 1 + len(CONTROLS)
                           and all(c["exit"] == 0 for c in receipt["compile"])
                           and len(receipt["cases"]) == len(CASES)
                           and len(receipt["controls"]) == len(CONTROLS)
                           and all(c["pass"] for c in receipt["cases"] + receipt["controls"]))
    (out / "result.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"pass": receipt["pass"], "cases": len(receipt["cases"]),
                      "controls": len(receipt["controls"]), "receipt": str(out / "result.private.json")}))
    return 0 if receipt["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
