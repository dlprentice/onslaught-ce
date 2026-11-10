#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Patch 0188: a static shadow bound to a thing that cannot hold it is left unbound (port/patches/0188-ce-static-shadow-unbound.patch).

CStaticShadows::Reattach binds each shadow to the thing whose number it names (`s->mThing = t`) and then asks the thing to take the shadow (`SetStaticShadow`). Only the
classes with a slot (features and buildings, and two unit classes) keep it; the base class does nothing. A shadow bound to any other thing kept a pointer that nothing ever
cleared, and the walk of the list when a level is torn down (Retry) used it after the thing was gone. The patch adds, inside `#if defined(CE_VERSION)`, one rule after that
call: a shadow whose thing does not report it back is unbound, which is the state the game's own removal path leaves (and ApplyToGrid and the destructor already treat a null
thing as that state).

This suite checks, without a game and without printing any of the game's source:
  1. the patch applies to src/StaticShadows.cpp with the build's own command and no fuzz, adds only its guarded lines and changes nothing else;
  2. in the patched function the added rule comes after the SetStaticShadow call and before the end of the function, inside its CE_VERSION guard;
  3. the added lines, compiled against stand-in thing and shadow classes, unbind a shadow whose thing does not hold it, keep one it does hold, and survive a shadow that has no thing;
     mutants of the added lines (the unbinding dropped, the comparison inverted, the null test dropped, the whole rule dropped) are each caught.
The level-888 stand-in's Retry (exit 139 before, 0 after) is a run, not this test (port/PARITY.md, Retry crash).

    python3 port/tools/static_shadow_unbound_tests.py
"""
import re
import resource
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "port/patches/0188-ce-static-shadow-unbound.patch"
SOURCE = ROOT / "src/StaticShadows.cpp"


def no_core():
    """The stand-in program of a mutant may crash on purpose (a null thing dereferenced). Its core limit is zero, so that a crash of it leaves no core file. (The kernel still tells systemd-coredump of a
    crash that dumps nothing, and that journals it and raises the desktop's crash notice: so STUBS also catches SIGSEGV itself and exits, and the test asserts that no run dies of a signal.)"""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def added_lines(patch_text: str) -> list:
    return [line[1:] for line in patch_text.splitlines() if line.startswith("+") and not line.startswith("+++")]


def apply_to(source_bytes: bytes):
    tmp = Path(tempfile.mkdtemp(prefix="static-shadow-unbound-"))
    (tmp / "StaticShadows.cpp").write_bytes(source_bytes)
    done = subprocess.run(["patch", "-p1", "-d", str(tmp), "-i", str(PATCH), "--forward", "-r", "-"], capture_output=True, text=True)
    return tmp, done


STUBS = r'''
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
struct CStaticShadow;
struct CThing { virtual CStaticShadow *GetStaticShadow() { return NULL; } virtual void SetStaticShadow(CStaticShadow *) {} virtual ~CThing() {} };
struct CHolder : CThing { CStaticShadow *slot = NULL; CStaticShadow *GetStaticShadow() override { return slot; } void SetStaticShadow(CStaticShadow *s) override { slot = s; } };
struct CStaticShadow { CThing *mThing; };
#define CE_VERSION 1
static bool run(CThing *thing, bool call_set) {
    CStaticShadow shadow; CStaticShadow *s = &shadow;
    s->mThing = thing;
    if (s->mThing && call_set) s->mThing->SetStaticShadow(s);
@RULE@
    return s->mThing != NULL;
}
static void crashed(int) { _exit(139); }    // a mutant that dereferences the null thing is a failed run, told by its exit status: it dies of no signal, so there is no core and no crash notice
int main() {
    std::signal(SIGSEGV, crashed);
    CThing slotless; CHolder holder;
    bool a = run(&slotless, true);      // a thing that cannot hold the shadow: unbound
    bool b = run(&holder, true);        // a thing that holds it: kept
    bool c = run(NULL, true);           // no thing at all: stays null, no crash
    std::printf("%d %d %d\n", int(a), int(b), int(c));
    return 0;
}
'''


class PatchTests(unittest.TestCase):
    def test_the_patch_applies_with_the_builds_command_no_fuzz_and_adds_only_its_guarded_lines(self):
        data = SOURCE.read_bytes()
        tmp, done = apply_to(data)
        try:
            fuzz = [line for line in done.stdout.splitlines() if "fuzz" in line]
            self.assertEqual((0, []), (done.returncode, fuzz))
            patched = (tmp / "StaticShadows.cpp").read_bytes()
            old_lines, new_lines = data.splitlines(), patched.splitlines()
            self.assertEqual(len(new_lines), len(old_lines) + len(added_lines(PATCH.read_text())))
            it = iter(new_lines)
            self.assertTrue(all(any(x == y for y in it) for x in old_lines), "every old line is still there, in order")     # nothing was changed or removed
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_the_added_rule_comes_after_the_call_and_inside_the_function_and_its_guard(self):
        tmp, done = apply_to(SOURCE.read_bytes())
        try:
            text = (tmp / "StaticShadows.cpp").read_text(errors="replace")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        start = text.index("CStaticShadows::Reattach")
        brace = text.index("{", text.index(")", start))
        depth, end = 0, brace
        for k in range(brace, len(text)):
            depth += (text[k] == "{") - (text[k] == "}")
            if depth == 0:
                end = k
                break
        body = text[brace:end]
        set_at = body.index("SetStaticShadow")
        guard_at = body.index("#if defined(CE_VERSION)")
        rule_at = body.index("GetStaticShadow()")
        endif_at = body.index("#endif", guard_at)
        self.assertTrue(set_at < guard_at < rule_at < endif_at, "the rule is after the call and inside its guard")
        self.assertEqual(1, body.count("#if defined(CE_VERSION)"), "one guard in the function")


class RuleTests(unittest.TestCase):
    """The added lines against stand-in classes."""

    @staticmethod
    def rule_text() -> str:
        lines = added_lines(PATCH.read_text())
        i, j = lines.index("#if defined(CE_VERSION)"), lines.index("#endif")
        return "\n".join(lines[i:j + 1])

    def compile_and_run(self, rule: str):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertTrue(compiler, "a C++ compiler is required")
        tmp = Path(tempfile.mkdtemp(prefix="static-shadow-rule-"))
        try:
            source = tmp / "rule.cpp"
            source.write_text(STUBS.replace("@RULE@", rule))
            built = subprocess.run([compiler, "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", str(source), "-o", str(tmp / "rule")], capture_output=True, text=True)
            self.assertEqual(0, built.returncode, "the stand-in program compiles")
            ran = subprocess.run([str(tmp / "rule")], capture_output=True, text=True, timeout=10, preexec_fn=no_core)
            return ran.returncode, ran.stdout.strip()
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_a_shadow_whose_thing_does_not_hold_it_is_unbound_one_that_does_is_kept_and_no_thing_is_safe(self):
        self.assertEqual((0, "0 1 0"), self.compile_and_run(self.rule_text()))

    def test_mutants_of_the_rule_are_caught(self):
        rule = self.rule_text()
        mutants = {
            "the unbinding dropped": rule.replace("s->mThing = NULL;", ";"),
            "the comparison inverted": rule.replace("GetStaticShadow() != s", "GetStaticShadow() == s"),
            "the null test dropped": rule.replace("s->mThing && ", ""),
            "the whole rule dropped": "",
        }
        for name, text in mutants.items():
            with self.subTest(name):
                self.assertNotEqual(rule, text, "the mutant changed the text")
                code, out = self.compile_and_run(text)
                self.assertNotEqual((0, "0 1 0"), (code, out), name)
                self.assertGreaterEqual(code, 0, f"{name}: the stand-in died of a signal (a core, or at least a crash notice)")


if __name__ == "__main__":
    unittest.main(verbosity=1)
