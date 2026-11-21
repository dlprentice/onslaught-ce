#!/usr/bin/env python3
"""Check the patched normal exit expression and preserve explicit quit statuses."""
from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH = "0262-ce-normal-quit-status.patch"


def masked(text):
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: "".join("\n" if c == "\n" else " " for c in m.group()), text, flags=re.S)


def function_span(text, name):
    source = masked(text)
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*(?:const\s*)?\{", source)
    if not match:
        raise AssertionError("owning function is absent")
    start = end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return start, end - 1


def normal_return(text):
    start, end = function_span(text, "PCLTShell::MainLoop")
    body = text[start:end]
    choices = [m for m in re.finditer(r"\breturn\s+([^;]+);", masked(body)) if "MsgInfo" in body[m.start():m.end()]]
    if len(choices) != 1:
        raise AssertionError("normal return is ambiguous")
    match = choices[0]
    return start + match.start(), start + match.end(), body[match.start():match.end()]


class NormalQuitStatus(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.reference = {name: (ROOT / "src" / name).read_bytes() for name in ("ltshell.cpp", "ltshell.h")}
        base = ROOT / "local-data" / "normal-quit-status-tests"
        base.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="check-", dir=base))
        sys.path.insert(0, str(ROOT / "port"))
        spec = importlib.util.spec_from_file_location("quit_status_build", ROOT / "port" / "build.py")
        build = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = build
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            spec.loader.exec_module(build)
        header = cls.reference["ltshell.h"].decode()
        first, last = function_span(header, "MsgInfo")
        getter = header[first:last]
        member = re.search(r"\b([A-Za-z_]\w*)(\.|->)wParam\b", getter)
        if not member:
            raise AssertionError("cached message payload getter changed")
        cache, access = member.groups()
        storage = "MSG " + cache + "{};" if access == "." else "MSG storage{}; MSG *" + cache + " = &storage;"
        cls.flavors = {}
        for network in (False, True):
            label = "network" if network else "default"
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                before = build.patched_text(["ltshell.cpp"], skip=(PATCH,), network=network)["ltshell.cpp"]
                after = build.patched_text(["ltshell.cpp"], network=network)["ltshell.cpp"]
            a, b, old = normal_return(before)
            c, d, new = normal_return(after)
            old_expression = old[len("return"):].rstrip(";").strip()
            new_expression = new[len("return"):].rstrip(";").strip()
            getter_call = re.search(r"\b(?:[A-Za-z_]\w*(?:::|->|\.))*MsgInfo\s*\(\s*\)", old_expression)
            if not getter_call:
                raise AssertionError("original status getter is absent")
            call = getter_call.group()
            guard_pattern = r"\(\s*" + re.escape(cache + access + "message") + r"\s*==\s*WM_QUIT\s*\?\s*" + re.escape(call) + r"\s*:\s*0\s*\)"
            guard = re.search(guard_pattern, new_expression)
            if not guard or new_expression.replace(guard.group(), call, 1) != old_expression:
                raise AssertionError("patch changes more than the normal cached-message status")
            old_model = "MsgInfo()"
            new_model = guard.group().replace(call, "MsgInfo()", 1)
            source = f'''#include <cstdint>
#include <cstdlib>
#include <iostream>
using SINT = int;
using UINT = std::uint32_t;
using WPARAM = std::uint32_t;
struct MSG {{ UINT message; WPARAM wParam; }};
constexpr UINT WM_QUIT = 0x12;
struct Shell {{
    {storage}
    int MsgInfo() {{ {getter} }}
    int before() {{ return {old_model}; }}
    int after() {{ return {new_model}; }}
}};
int main(int argc, char **argv) {{
    if (argc != 3) return 2;
    Shell shell;
    shell.{cache}{access}message = static_cast<UINT>(std::strtoul(argv[1], nullptr, 0));
    shell.{cache}{access}wParam = static_cast<WPARAM>(std::strtol(argv[2], nullptr, 0));
    std::cout << shell.before() << " " << shell.after() << "\\n";
}}
'''
            case = cls.out / label
            case.mkdir()
            (case / "model.private.cpp").write_text(source)
            command = ["c++", "-m32", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                       str(case / "model.private.cpp"), "-o", str(case / "model")]
            with (case / "build.private.log").open("w") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            cls.flavors[label] = {"before": before, "after": after, "old_span": (a, b), "new_span": (c, d), "old": old, "new": new,
                                  "program": case / "model"}

    def values(self, flavor, message, payload):
        result = subprocess.run([str(self.flavors[flavor]["program"]), str(message), str(payload)], capture_output=True, text=True, check=True)
        return tuple(map(int, result.stdout.split()))

    def test_only_the_normal_return_changes(self):
        for label, case in self.flavors.items():
            with self.subTest(flavor=label):
                a, b = case["old_span"]
                self.assertEqual(case["before"][:a] + case["new"] + case["before"][b:], case["after"])

    def test_keyboard_payload_is_a_discriminating_failure_control(self):
        for flavor in self.flavors:
            for key in (13, 32):
                with self.subTest(flavor=flavor, key=key):
                    before, after = self.values(flavor, 0x100, key)
                    self.assertEqual(before, key)
                    self.assertEqual(after, 0)

    def test_other_nonquit_messages_cannot_supply_an_exit_status(self):
        for flavor in self.flavors:
            for message, payload in ((0x101, 13), (0x200, 1), (0, 0)):
                with self.subTest(flavor=flavor, message=message):
                    self.assertEqual(self.values(flavor, message, payload)[1], 0)

    def test_real_quit_status_is_preserved_including_the_same_key_values(self):
        for flavor in self.flavors:
            for status in (0, 13, 32, 77, -5):
                with self.subTest(flavor=flavor, status=status):
                    self.assertEqual(self.values(flavor, 0x12, status), (status, status))

    def test_reference_source_and_header_are_unchanged(self):
        for name, expected in self.reference.items():
            self.assertEqual(hashlib.sha256((ROOT / "src" / name).read_bytes()).digest(), hashlib.sha256(expected).digest())


if __name__ == "__main__":
    unittest.main()
