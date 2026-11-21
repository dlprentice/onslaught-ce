#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for port/tools/test_cc.py and the cached weak-stub archive (port/tools/tests/ce_weak_stubs.cpp).

Verifies:
  1. Every ce_hook_* point declared in port/game/ce_hooks.h has a matching weak stub in ce_weak_stubs.cpp
     (and no stale hook stub remains).
  2. A host driver calling every hook point and common port query compiles, links, and returns inert defaults
     without writing a single hand stub.
  3. A driver providing strong definitions cleanly overrides the weak archive defaults.
  4. The weak-stub archive is reused from cache on repeat calls and invalidated when source bytes change.
"""
from __future__ import annotations

import re
import tempfile
import unittest
from pathlib import Path

import test_cc

HOOK_DECL = re.compile(r"^[A-Za-z_][\w \t\*]*?\b(ce_hook_\w+)\s*\(", re.M)
WEAK_DEF = re.compile(r"^CE_WEAK\s+[A-Za-z_][\w \t\*]*?\b(ce_hook_\w+)\s*\(", re.M)


class TestCcTests(unittest.TestCase):
    def test_weak_stubs_match_ce_hooks_header(self):
        declared = set(HOOK_DECL.findall(test_cc.HOOKS_HDR.read_text()))
        defined = set(WEAK_DEF.findall(test_cc.STUBS_SRC.read_text()))
        self.assertGreaterEqual(len(declared), 17, "hook declaration regex failed to match ce_hooks.h")
        self.assertEqual(sorted(declared - defined), [], "ce_hooks.h hooks missing from ce_weak_stubs.cpp")
        self.assertEqual(sorted(defined - declared), [], "stale hook stubs in ce_weak_stubs.cpp")

    def test_default_weak_stubs_link_and_are_inert(self):
        with tempfile.TemporaryDirectory(prefix="test-cc-") as tmp:
            root = Path(tmp)
            driver = root / "inert_driver.cpp"
            driver.write_text(r'''
#include "ce_hooks.h"
#include <cstdio>
bool PortCeLayer();
bool PortRecording();
bool PortCeMenus();
bool PortCeExperimental(const char *);
bool PortCeIniBool(const char *, const char *, bool);
bool PortWideModes();
int main() {
    ce_hook_boot();
    ce_hook_exit(CE_EXIT_SHELL);
    ce_hook_level_begin(110);
    ce_hook_level_end(110, 0);
    ce_hook_tick_pre();
    ce_hook_tick_post();
    ce_hook_input_actions(nullptr, 0);
    ce_hook_draw_world(nullptr, 0);
    ce_hook_draw_hud(nullptr, 0);
    ce_hook_draw_overlay();
    ce_hook_menu_rows(CE_MENU_BUILD, nullptr);
    ce_hook_save_gate(CE_SAVE_CAREER, nullptr, nullptr, 0);
    ce_hook_audio_clock(CE_AUDIO_REFILL, 0);
    CCamera *cam = reinterpret_cast<CCamera *>(0x10);
    CBattleEngine *be = reinterpret_cast<CBattleEngine *>(0x20);
    if (ce_hook_key_capture('W', false)) return 1;
    if (ce_hook_render_camera(0, cam, nullptr) != cam) return 2;
    if (ce_hook_hud_focus(nullptr, be) != be) return 3;
    if (!PortCeLayer() || PortRecording() || !PortCeMenus() || PortCeExperimental("x") || PortWideModes()) return 4;
    if (!PortCeIniBool("s", "k", true) || PortCeIniBool("s", "k", false)) return 5;
    std::puts("weak-ok");
    return 0;
}
''')
            cache = root / "cache"
            exe = test_cc.compile_driver([driver], root / "inert", cache_dir=cache)
            run = test_cc.run_driver(exe)
            self.assertEqual((run.returncode, run.stdout.strip()), (0, "weak-ok"), run.stderr)

    def test_strong_definitions_override_weak_archive(self):
        with tempfile.TemporaryDirectory(prefix="test-cc-override-") as tmp:
            root = Path(tmp)
            driver = root / "override_driver.cpp"
            driver.write_text(r'''
#include "ce_hooks.h"
#include <cstdio>
bool ce_hook_key_capture(unsigned char key, bool) { return key == 'K'; }
bool PortCeLayer() { return false; }
int main() {
    if (!ce_hook_key_capture('K', false) || ce_hook_key_capture('A', false)) return 1;
    if (PortCeLayer()) return 2;
    // Un-overridden hook still resolves from the weak archive.
    CCamera *cam = reinterpret_cast<CCamera *>(0x30);
    if (ce_hook_render_camera(0, cam, nullptr) != cam) return 3;
    std::puts("override-ok");
    return 0;
}
''')
            cache = root / "cache"
            exe = test_cc.compile_driver([driver], root / "override", cache_dir=cache)
            run = test_cc.run_driver(exe)
            self.assertEqual((run.returncode, run.stdout.strip()), (0, "override-ok"), run.stderr)

    def test_archive_is_cached_and_invalidates_on_change(self):
        with tempfile.TemporaryDirectory(prefix="test-cc-cache-") as tmp:
            root = Path(tmp)
            cache = root / "cache"
            a1 = test_cc.stubs_archive(cache_dir=cache)
            mtime1 = a1.stat().st_mtime_ns
            a2 = test_cc.stubs_archive(cache_dir=cache)
            self.assertEqual(a1, a2)
            self.assertEqual(mtime1, a2.stat().st_mtime_ns, "archive was unnecessarily rebuilt")
            mutated = root / "ce_weak_stubs.cpp"
            mutated.write_text(test_cc.STUBS_SRC.read_text() + "\n// cache-invalidation-comment\n")
            a3 = test_cc.stubs_archive(cache_dir=cache, stubs_src=mutated)
            self.assertNotEqual(a1.name, a3.name, "cache key did not invalidate on source change")
            self.assertTrue(a3.is_file())


if __name__ == "__main__":
    unittest.main()
