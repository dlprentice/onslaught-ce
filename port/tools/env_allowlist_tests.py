#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every BEA_* environment variable in a shipped public program must be allowlisted and non-test-only.

    python3 port/tools/env_allowlist_tests.py [--exe PATH ...] [--require-binary] [-v]

Checks the exact built bytes of the public Linux and Windows programs (not just source files) so that a
test-only switch (test kit, stack marker, commander test, play-unit test, network automation) or any
undocumented BEA_* environment variable cannot silently ship inside a release binary.

Without --exe, checks local-data/port/build/linux/bea and local-data/port/build/windows/bea.exe when
present; missing binaries are SKIPPED, NOT PASSED unless --require-binary is given. The pure judge and
causal mutant checks always run with no external tools or game files required.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
sys.path.insert(0, str(PORT))
import build  # noqa: E402

BEA_TOKEN = re.compile(rb"(?<![A-Za-z0-9_])BEA_[A-Z0-9_]+(?![A-Za-z0-9_])")

# Explicit, reviewed inventory of BEA_* strings allowed in a shipped --public player binary.
# Any new BEA_* variable added to the shipped binary must be classified here with its purpose.
ALLOWED_PUBLIC_BEA: dict[str, str] = {
    # Player / launcher paths, build identity, and user overrides.
    "BEA_GAME_DIR": "game data root override",
    "BEA_USER_DIR": "user data root override",
    "BEA_CE_BUILD": "build commit passed from launcher for problem reports",
    "BEA_CE_CUSTOM_USER_DIR": "launcher marker that user set custom user folder",
    "BEA_CE_GAME_FOLDER_LOGGED": "launcher handshake avoiding duplicate folder log line",
    "BEA_CE_PROJECT": "authored .ceproject launch path",
    "BEA_PORT_CE_LAYER": "explicit CE layer opt-in/off override",
    "BEA_PORT_WIDESCREEN": "widescreen toggle override",
    "BEA_PORT_HUD_SCALE": "HUD scaling override",
    "BEA_PORT_EXCLUSIVE": "Linux exclusive fullscreen opt-in",
    "BEA_PORT_VERBOSE": "opt-in verbose console logging",
    "BEA_PORT_MINIMIZED_CLOCK": "minimized clock diagnostic override",
    "BEA_REPORT_NO_OPEN": "suppress browser open on problem report (headless tests)",
    "BEA_SLEEP_VIRTUAL": "virtual clock Sleep() forwarding for shim replay",
    # Replay, recording, probe, and deterministic clock harness.
    "BEA_PORT_CLOCK": "deterministic replay/test clock selector (disables CE layer)",
    "BEA_PORT_CLOCK_DT": "fixed-step clock delta",
    "BEA_PORT_CLOCK_LOG": "clock diagnostic logging",
    "BEA_PORT_TAPE": "replay input tape path (disables CE layer)",
    "BEA_PORT_PROBES": "replay probe output path (disables CE layer)",
    "BEA_PORT_PROBE_EVERY": "replay probe cadence",
    "BEA_PORT_PROBE_OBJECTS": "extended replay object probes",
    "BEA_PORT_RECORD": "record input tape path (disables CE layer)",
    "BEA_PORT_RECORD_FPS": "record presentation pace",
    "BEA_PORT_REPLAY_CHEAT": "replay-only retail harness cheat for cheat-recorded tapes (refused in network sessions; writes no saves)",
    "BEA_PORT_REPLAY_MAX": "stop replay after frame N",
    "BEA_PORT_REPLAY_SLEEP_US": "replay frame pacing throttle",
    "BEA_PORT_PILOT": "pilot mode fence variable (disables CE layer)",
    "BEA_PORT_AUTOPILOT": "record-mode autopilot script (disables CE layer)",
    "BEA_PORT_AUTOPILOT_START": "record-mode autopilot start frame",
    "BEA_RECORD": "legacy record fence variable (disables CE layer)",
    # Offscreen / silent test guards and headless capture.
    "BEA_PORT_REQUIRE_DUMMY_AUDIO": "fail closed if audio driver is not dummy/silent",
    "BEA_PORT_REQUIRE_OFFSCREEN": "fail closed if SDL video driver is not offscreen",
    "BEA_PORT_SHOT": "capture single frame PPM",
    "BEA_PORT_SHOT_EVERY": "periodic frame PPM capture",
    "BEA_PORT_QUIT": "request clean WM_CLOSE at frame N",
    "BEA_PORT_FAKE_CLICK": "headless UI click injection",
    "BEA_PORT_FAKE_KEYS": "headless keyboard injection",
    "BEA_PORT_FAKE_PAD": "headless virtual gamepad 1",
    "BEA_PORT_FAKE_PAD2": "headless virtual gamepad 2",
    "BEA_PORT_FAKE_PAD_KIND": "headless virtual gamepad kind",
    "BEA_PORT_FAKE_PAD_LATE": "headless virtual gamepad delayed attach",
    # Audio, Bink, D3D, input, CRT, feel, terrain, and update diagnostics.
    "BEA_AUDIO_CALLLOG": "DirectSound call log path",
    "BEA_AUDIO_CLOCK": "DirectSound clock mode",
    "BEA_AUDIO_DUMP": "DirectSound played WAV dump dir",
    "BEA_AUDIO_MONITOR": "record-mode audio monitor stream",
    "BEA_AUDIO_RECORD": "DirectSound final mix WAV capture",
    "BEA_AUDIO_TRACE": "DirectSound play trace log",
    "BEA_BINK_LOG": "Bink call log path",
    "BEA_PORT_CRT_CALLERS": "CRT rand() caller logging",
    "BEA_PORT_CRT_DEPTH": "CRT rand() backtrace depth",
    "BEA_PORT_D3DLOG": "D3D9 draw call log",
    "BEA_PORT_D3DLOG_TEXDUMP": "D3D9 bound texture dump",
    "BEA_PORT_D3DLOG_VB": "D3D9 vertex buffer dump",
    "BEA_PORT_DEBUG": "Linux kernel debug logging",
    "BEA_PORT_INPUT_LOG": "DirectInput/window input trace log",
    "BEA_PORT_MODEL_QUALITY_LOG": "model quality selection diagnostic log",
    "BEA_PORT_TERRAIN_QUALITY_LOG": "terrain quality selection diagnostic log",
    "BEA_PORT_UPDATE_DUMP": "per-update simulation state dump path",
    "BEA_PORT_UPDATE_DUMP_FROM": "per-update dump start tick",
    "BEA_PORT_UPDATE_DUMP_TO": "per-update dump end tick",
    "BEA_PORT_UPDATE_DUMP_THINGS": "per-update dump thing list toggle",
    "BEA_PORT_FEEL_FLUSH_S": "frame-feel telemetry flush interval",
    "BEA_PORT_FEEL_GPU": "frame-feel D3D9 GPU timestamp toggle",
    "BEA_PORT_FEEL_HEAP": "frame-feel heap sampling toggle",
    "BEA_PORT_FEEL_LOG": "frame-feel telemetry log path",
    "BEA_PORT_FEEL_MAX_EVENTS": "frame-feel max event cap",
    "BEA_PORT_FEEL_PACE_HZ": "frame-feel virtual display pacing Hz",
    "BEA_PORT_FEEL_SCRIPT": "frame-feel scripted input timeline",
    "BEA_PORT_FEEL_SHOTS": "frame-feel consecutive frame capture",
    # Existing CE proof/diagnostic switches and experimental selectors.
    "BEA_PORT_COMMANDER_TERRAIN": "commander terrain renderer fallback mode",
    "BEA_PORT_COMMANDER_VIEW": "commander view diagnostic trap mode",
    "BEA_PORT_DEBRIEF_PROOF": "debrief overlay proof switch (requires BEA_PORT_CE_LAYER)",
    "BEA_PORT_DEVSTATS_PROOF": "devstats overlay proof switch (requires BEA_PORT_CE_LAYER)",
    "BEA_PORT_MUSEUM_MISSING_TEXTURE": "museum missing-texture fallback check",
    "BEA_PORT_MUSEUM_REFUSE": "museum visit refusal path check",
    "BEA_PORT_PHOTO_PROOF": "photo mode proof switch (requires BEA_PORT_CE_LAYER)",
    "BEA_PORT_TANK_DRIVE": "experimental play-unit drive law selector",
    "BEA_PORT_TANK_VIEW": "experimental play-unit camera mode selector",
    "BEA_PORT_TERRAIN_RESERVE_FAULT": "terrain reserve fallback injection",
    "BEA_PORT_WIDE_TEST": "widescreen synthetic mode test override",
    # Fence-only strings (present solely to detect/refuse network env vars in runmode/fences).
    "BEA_LOCKSTEP_": "fence prefix string",
    "BEA_LOCKSTEP_HOST": "fence check string (network compiled out in --public)",
    "BEA_LOCKSTEP_JOIN": "fence check string (network compiled out in --public)",
    "BEA_NET_": "fence prefix string",
    "BEA_NET_HANDOFF": "fence check string (network compiled out in --public)",
    "BEA_NET_HANDOFF_FILE": "fence check string (network compiled out in --public)",
    "BEA_NET_HOST": "fence check string (network compiled out in --public)",
    "BEA_NET_JOIN": "fence check string (network compiled out in --public)",
}

# Prefixes and exact names that belong ONLY to test builds (--test-kit, --marker-test,
# --commander-test, --play-unit-test, --test-automation) and must NEVER appear in a shipped public binary.
FORBIDDEN_EXACT = frozenset({
    "BEA_PORT_VERBS",
    "BEA_PORT_VERB_OUT",
    "BEA_PORT_VERB_RUN_DIR",
    "BEA_PORT_STACK_MARKER",
    "BEA_PORT_MARKER_STACK",
    "BEA_PORT_COMMANDER_PROOF",
    "BEA_PORT_COMMANDER_TAPE",
    "BEA_PORT_COMMANDER_PROBE",
    "BEA_PORT_COMMANDER_PROBE_EVERY",
    "BEA_PORT_COMMANDER_PROBE_MEMBERS",
    "BEA_PORT_COMMANDER_PROBE_MATCH",
    "BEA_PORT_COMMANDER_MAP",
    "BEA_PORT_COMMANDER_DUMP",
    "BEA_PORT_COMMANDER_TACTICAL",
    "BEA_PORT_COMMANDER_AUDIT",
    "BEA_PORT_COMMANDER_AUDIT_LEAK",
    "BEA_PORT_COMMANDER_CONTROL",
    "BEA_PORT_COMMANDER_HASHAT",
    "BEA_PORT_COMMANDER_PAUSEAT",
    "BEA_PORT_COMMANDER_CAMERA_TRACE",
    "BEA_PORT_COMMANDER_MUTANT_KEEP_TRAP",
    "BEA_NET_LOBBY_CAMPAIGN",
    "BEA_LOCKSTEP_SKEW_AT",
    "BEA_LOCKSTEP_POISON_STACK",
    "BEA_PORT_FAKE_KEYS_GAME",
    "BEA_PORT_FAKE_KEYS_RETURN",
})
FORBIDDEN_PREFIXES = (
    "BEA_PORT_VERB_",
    "BEA_PORT_PLAYUNIT_",
    "BEA_NET_TEST_",
    "BEA_LOCKSTEP_TEST_",
)

CLI_EXES: list[Path] = []
REQUIRE_BINARY = False


def extract_bea_tokens(data: bytes) -> set[str]:
    """Return every distinct BEA_* ASCII identifier present in raw binary bytes."""
    return {m.group(0).decode("ascii") for m in BEA_TOKEN.finditer(data)}


def judge_tokens(tokens: set[str]) -> list[str]:
    """Return failure messages for any forbidden test switch or unallowlisted BEA_* token."""
    problems: list[str] = []
    for name in sorted(tokens):
        if name in FORBIDDEN_EXACT or name.startswith(FORBIDDEN_PREFIXES):
            problems.append(f"forbidden test-build switch in public binary: {name}")
        elif name not in ALLOWED_PUBLIC_BEA:
            problems.append(f"unallowlisted BEA_* environment variable in public binary: {name}")
    return problems


class EnvAllowlistJudgeTests(unittest.TestCase):
    def test_allowlist_and_forbidden_sets_are_disjoint(self):
        overlap = [n for n in ALLOWED_PUBLIC_BEA if n in FORBIDDEN_EXACT or n.startswith(FORBIDDEN_PREFIXES)]
        self.assertEqual(overlap, [], f"allowlist overlaps forbidden test switches: {overlap}")

    def test_clean_public_subset_passes(self):
        sample = b"\x00BEA_GAME_DIR\x00BEA_USER_DIR\x00BEA_PORT_CLOCK\x00"
        tokens = extract_bea_tokens(sample)
        self.assertEqual(tokens, {"BEA_GAME_DIR", "BEA_USER_DIR", "BEA_PORT_CLOCK"})
        self.assertEqual(judge_tokens(tokens), [])

    def test_causal_mutants_are_caught(self):
        mutants = (
            ("test-kit switch", b"\x00BEA_PORT_VERBS\x00", "forbidden test-build switch"),
            ("stack marker switch", b"\x00BEA_PORT_STACK_MARKER\x00", "forbidden test-build switch"),
            ("play-unit god switch", b"\x00BEA_PORT_PLAYUNIT_GOD\x00", "forbidden test-build switch"),
            ("commander proof switch", b"\x00BEA_PORT_COMMANDER_PROOF\x00", "forbidden test-build switch"),
            ("network automation switch", b"\x00BEA_NET_TEST_BYPASS\x00", "forbidden test-build switch"),
            ("unknown variable", b"\x00BEA_PORT_UNDOCUMENTED_BACKDOOR\x00", "unallowlisted BEA_*"),
        )
        for label, payload, want in mutants:
            with self.subTest(mutant=label):
                problems = judge_tokens(extract_bea_tokens(payload))
                self.assertTrue(any(want in p for p in problems), f"{label}: got {problems}")


class BuiltBinaryEnvTests(unittest.TestCase):
    def _check_binary(self, exe: Path, label: str):
        if not exe.is_file():
            if REQUIRE_BINARY:
                self.fail(f"required {label} binary missing at {exe}")
            self.skipTest(f"SKIPPED, NOT PASSED: no built {label} binary at {exe}")
        tokens = extract_bea_tokens(exe.read_bytes())
        self.assertGreater(len(tokens), 10, f"{exe}: unexpectedly few BEA_* strings ({len(tokens)})")
        problems = judge_tokens(tokens)
        self.assertEqual(problems, [], f"{exe} failed env allowlist:\n" + "\n".join(problems))

    def test_built_public_binaries(self):
        if CLI_EXES:
            for exe in CLI_EXES:
                with self.subTest(exe=str(exe)):
                    self._check_binary(exe, exe.name)
            return
        targets = (
            ("linux", Path(os.environ["BEA_LINUX_EXE"]) if os.environ.get("BEA_LINUX_EXE") else build.BUILD / "linux/bea"),
            ("windows", Path(os.environ["BEA_WINDOWS_EXE"]) if os.environ.get("BEA_WINDOWS_EXE") else build.BUILD / "windows/bea.exe"),
        )
        checked = 0
        for label, exe in targets:
            if exe.is_file():
                checked += 1
                with self.subTest(target=label):
                    self._check_binary(exe, label)
            elif REQUIRE_BINARY:
                self.fail(f"required {label} binary missing at {exe}")
        if checked == 0:
            self.skipTest("SKIPPED, NOT PASSED: neither linux/bea nor windows/bea.exe is built yet")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", action="append", default=[], help="explicit binary path to inspect (repeatable)")
    ap.add_argument("--require-binary", action="store_true", help="fail instead of skipping if a binary is absent")
    ap.add_argument("-v", action="store_true")
    args = ap.parse_args()
    CLI_EXES = [Path(p) for p in args.exe]
    REQUIRE_BINARY = args.require_binary
    unittest.main(verbosity=2 if args.v else 1, argv=sys.argv[:1])
