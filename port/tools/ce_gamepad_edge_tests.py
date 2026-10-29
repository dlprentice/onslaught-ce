#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-coupled negative controls for CE's ordered controller buttons, with SDL virtual devices.

The controlled adapter and game callers run without a window, GPU or physical controller. Each mutant is written
under this worktree's ignored local-data and must fail the selected production-path case.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "port/tools/input_lifetime_tests.py"
MUTANTS = (
    ("watch-removed", "pad-quick-tap", "port/platform/common/dinput.cpp",
     "padWatching = SDL_AddEventWatch(PadWatch, NULL);", "padWatching = true;"),
    ("queue-collapsed", "pad-quick-tap", "port/platform/common/ce_pad_queue.h",
     "states[(head + count) % Capacity] = next;", "head = count = 0; states[0] = next;"),
    ("chord-serialized", "pad-edge-chord", "port/platform/common/ce_pad_queue.h",
     "if (changed & difference) break;", "if (changed) break;"),
    ("held-reads-physical", "pad-quick-tap", "port/game/ce_gamepad.cpp",
     "return sCurrent[pad][button];", "return LT.JoyButtonOn(pad, button);"),
    ("focus-state-ignored", "pad-edge-missing-focus-event", "port/platform/common/dinput.cpp",
     "if (focused != d->ceFocused)", "if (false)"),
    ("only-owner-pad-updated", "pad-edge-cross-binding", "port/game/ce_gamepad.cpp",
     "for (int pad = 0; pad < LT.mJoypads && pad < MAX_JOYPADS; ++pad) PortCeGamepadUpdate(pad);",
     "PortCeGamepadUpdate(consumer);"),
    ("each-consumer-drains", "pad-edge-two-consumers", "port/game/ce_gamepad.cpp",
     "if (!sCycleReady)", "if (true)"),
    ("duplicate-map-treated-as-simple", "pad-edge-duplicate-binding", "port/platform/common/dinput.cpp",
     "if (seen & bit) d->ceSampleOnly |= bit;", "if (false) d->ceSampleOnly |= bit;"),
    ("unplug-resets-other-player", "pad-edge-hotplug-two", "port/platform/common/dinput.cpp",
     "if (d->joystickId != v.device) continue;",
     "if (d->joystickId != v.device && v.type != SDL_EVENT_JOYSTICK_REMOVED) continue;"),
    ("held-reconnect-invents-press", "pad-edge-hotplug-two", "port/platform/common/dinput.cpp",
     "if (!d->ceButtons.active)\n\t{\n\t\td->ceButtons.Arm(observed);\n\t\t*out = observed;\n\t\treturn 0;",
     "if (!d->ceButtons.active)\n\t{\n\t\td->ceButtons.Arm(observed);\n\t\t*out = observed;\n\t\treturn 1;"),
    ("fresh-sample-omitted", "pad-edge-no-poll-held", "port/platform/common/dinput.cpp",
     "bool sampled = d->acquired && SUCCEEDED(d->GetDeviceState(sizeof(state), &state));", "bool sampled = d->acquired;"),
    ("overflow-does-not-rebase-all", "pad-edge-cross-overflow", "port/platform/common/dinput.cpp",
     "for (Device *d : gPadSlots)\n\t\t\tif (d)", "for (Device *d : gPadSlots)\n\t\t\tif (d && d == gPadSlots[0])"),
    ("tape-fence-removed", "pad-edge-tape", "port/game/ce_gamepad.cpp",
     "return ordinary && PortCeLayer();", "return PortCeLayer();"),
)


class OrderedButtonsTests(unittest.TestCase):
    def test_real_adapter_and_callers(self):
        cases = ("pad-quick-tap", "pad-edge-order", "pad-edge-chord", "pad-edge-chord-pulses", "pad-edge-overflow", "pad-edge-event-overflow", "pad-edge-overflow-focus", "pad-edge-focus",
                 "pad-edge-stale-focus", "pad-edge-hotplug", "pad-edge-hotplug-two", "pad-edge-two", "pad-edge-cross-binding", "pad-edge-two-consumers",
                 "pad-edge-repeat-consumer", "pad-edge-bonus-two", "pad-edge-no-poll-held", "pad-edge-duplicate-binding",
                 "pad-edge-no-watch", "pad-edge-missing-focus-event", "pad-edge-cross-overflow", "pad-edge-trigger", "pad-edge-multiplayer",
                 "pad-edge-tape", "pad-edge-record", "pad-edge-clock", "pad-edge-probes")
        args = [sys.executable, str(HARNESS), "--optimize"]
        for case in cases:
            args.extend(("--case", case))
        result = subprocess.run(args, capture_output=True, text=True, timeout=600)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for case in cases:
            self.assertIn(f"PASS {case}", result.stdout)

    def test_each_causal_mutation_is_detected(self):
        parent = ROOT / "local-data/port/controller-edge-mutants"
        parent.mkdir(parents=True, exist_ok=True)
        for name, case, path, old, new in MUTANTS:
            with self.subTest(mutant=name):
                root = Path(tempfile.mkdtemp(prefix=name + "-", dir=parent))
                (root / "src").symlink_to(ROOT / "src")
                (root / "port").mkdir()
                (root / "port/patches").symlink_to(ROOT / "port/patches")
                (root / "port/platform").mkdir()
                shutil.copytree(ROOT / "port/platform/common", root / "port/platform/common")
                shutil.copytree(ROOT / "port/game", root / "port/game")
                target = root / path
                text = target.read_text()
                self.assertEqual(text.count(old), 1, name)
                target.write_text(text.replace(old, new, 1))
                result = subprocess.run([sys.executable, str(HARNESS), "--source-root", str(root), "--case", case,
                                         "--optimize"], capture_output=True, text=True, timeout=600)
                self.assertNotEqual(result.returncode, 0, "mutant survived: " + name + "\n" + result.stdout)
                self.assertIn("FAIL " + case, result.stdout, "mutant did not reach its case: " + name + "\n" + result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
