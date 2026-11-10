#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CE_SettingSetText (port/platform/common/ce.cpp), the text setter of the CE Settings page, on the actual ce_ini_write
(port/updater/ce_report.h) and read back by the actual readers the game's consumers use (CE_SettingIs, CE_SettingGetText,
CE_SettingGetBool). Scratch user folders only: BEA_USER_DIR names one for every run, so the player's own ce.ini is never read
or written, and no game, window, device or network is opened (ce_setting_text_driver.cpp; the SDL header is an authored stub).

Checked: the line is added to a missing file, replaced in place, and every other byte is kept; duplicates collapse to one line; a
longer key with the same start is not the line; what is written reads back through the readers; a key or value that is not part
of one line of text, an unreadable, linked, non-regular or oversized file and a folder that cannot take the new file leave the
file as it was, with the log line saying so; a failed write leaves no temporary file. Causal mutants of the setter must each be
caught by the scenarios alone.

    python3 -I port/tools/ce_setting_text_tests.py
"""
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SDL_STUB = """#pragma once
#include <stdint.h>
typedef uint64_t Uint64;
extern "C" {
Uint64 SDL_GetTicks(void);
bool SDL_OpenURL(const char *url);
}
"""

# One-line weakenings of the setter: (name, old, new). Each must make the scenarios fail on its own.
MUTANTS = (
    ("a control byte in a value is written",
     "if ((unsigned char)*c < 0x20 || *c == 0x7f)\n\t\t\tplain = false;", "if (false)\n\t\t\tplain = false;"),
    ("a key with a blank or an equals sign is written",
     "if ((unsigned char)*c <= 0x20 || *c == 0x7f || *c == '=')\n\t\t\tplain = false;", "if (false)\n\t\t\tplain = false;"),
    ("an empty value is written", "bool plain = key && *key && value && *value;", "bool plain = key && *key && value;"),
    ("a failed write is reported as written",
     'could not write ce.ini (%s=%s); it is unchanged\\n", key, value);\n\t\treturn 0;',
     'could not write ce.ini (%s=%s); it is unchanged\\n", key, value);\n\t\treturn 1;'),
    ("the value written is not the value given", "ce_ini_write(path, key, value)", 'ce_ini_write(path, key, "0")'),
    ("the key written is not the key given", "ce_ini_write(path, key, value)", 'ce_ini_write(path, "other", value)'),
    ("the failure is not logged",
     'fprintf(stderr, PORT_LOG_PREFIX "could not write ce.ini (%s=%s); it is unchanged\\n", key, value);',
     "(void)key;"),
)


class Fail(Exception):
    pass


def expect(condition, message):
    if not condition:
        raise Fail(message)


def build(out, source_text, name):
    """The driver on a copy of ce.cpp (its relative include of ce_report.h made a -I one so a copy can sit anywhere)."""
    folder = out / name
    folder.mkdir()
    include = '#include "../../updater/ce_report.h"'
    if source_text.count(include) != 1:
        raise RuntimeError("ce.cpp no longer includes ce_report.h as the test expects")
    (folder / "ce.cpp").write_text(source_text.replace(include, '#include "ce_report.h"'))
    (folder / "SDL3").mkdir()
    (folder / "SDL3/SDL.h").write_text(SDL_STUB)
    binary = folder / "driver"
    command = ["g++", "-std=c++17", "-O0", "-g", "-Wall", "-Wextra", "-DCE_VERSION=\"test\"", "-I", str(folder),
               "-I", str(PORT / "updater"), "-I", str(PORT / "platform/common"),
               str(PORT / "tools/ce_setting_text_driver.cpp"), str(folder / "ce.cpp"), "-o", str(binary)]
    done = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if done.returncode:
        raise RuntimeError("the driver did not compile:\n" + done.stdout + done.stderr)
    return binary


class Scratch:
    """Fresh user folders under one scratch root, and the driver run against them."""

    def __init__(self, binary, root):
        self.binary, self.root, self.count = binary, root, 0

    def user(self):
        self.count += 1
        return self.root / ("user-%d" % self.count)

    def run(self, *args, ini=None, user=None, timeout=10):
        user = user or self.user()
        if ini is not None:
            user.mkdir(parents=True, exist_ok=True)
            (user / "ce.ini").write_bytes(ini)
        env = {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": str(self.root), "BEA_USER_DIR": str(user)}
        done = subprocess.run([str(self.binary), *args], capture_output=True, timeout=timeout, env=env)
        path = user / "ce.ini"
        after = path.read_bytes() if path.is_file() and not path.is_symlink() else None
        leftovers = sorted(p.name for p in user.iterdir() if ".tmp-" in p.name) if user.is_dir() else []
        return done.returncode, done.stdout.decode("latin-1"), done.stderr.decode("latin-1"), after, leftovers, user


def adds_to_a_missing_file(s):
    rc, _, _, after, left, user = s.run("set", "music_playtype", "random")
    expect(rc == 0 and after == b"music_playtype=random\n" and not left, "a missing file (and its folder) is made with the line: %r" % after)


def replaces_the_line_and_keeps_every_other_byte(s):
    old = b"before=\xff\x00bytes\r\nmusic_playtype=linear\r\nlast=no-newline"
    rc, _, _, after, left, _ = s.run("set", "music_playtype", "random", ini=old)
    expect(rc == 0 and after == b"before=\xff\x00bytes\r\nmusic_playtype=random\nlast=no-newline\n" and not left,
           "the line is replaced in place and the other bytes stay: %r" % after)


def adds_a_new_line_at_the_end(s):
    rc, _, _, after, _, _ = s.run("set", "hud_scale", "0", ini=b"a=1\nb=2\n")
    expect(rc == 0 and after == b"a=1\nb=2\nhud_scale=0\n", "a line that is not there yet is added at the end: %r" % after)


def duplicates_collapse_to_one_line(s):
    rc, _, _, after, _, _ = s.run("set", "console_theme", "yellow", ini=b"a=1\nconsole_theme=green\nb=2\nconsole_theme=green\n")
    expect(rc == 0 and after == b"a=1\nconsole_theme=yellow\nb=2\n", "duplicate lines collapse into the one written: %r" % after)


def a_longer_key_is_not_the_line(s):
    rc, _, _, after, _, _ = s.run("set", "hud_scale", "0", ini=b"hud_scale_extra=5\nxhud_scale=3\n")
    expect(rc == 0 and after == b"hud_scale_extra=5\nxhud_scale=3\nhud_scale=0\n",
           "only the whole key followed by = is the line: %r" % after)


def the_same_value_again_changes_nothing(s):
    user = s.user()
    s.run("set", "rumble", "1", ini=b"a=1\n", user=user)
    first = (user / "ce.ini").read_bytes()
    rc, _, _, after, left, _ = s.run("set", "rumble", "1", user=user)
    expect(rc == 0 and after == first and not left, "writing the same value again leaves the same file")


def what_is_written_reads_back_through_the_readers(s):
    user = s.user()
    for key, value in (("music_playtype", "single"), ("console_theme", "green"), ("update_check", "0"), ("hud_anchor", "1")):
        rc, *_ = s.run("set", key, value, user=user)
        expect(rc == 0, "%s=%s is written" % (key, value))
        expect(s.run("is", key, value, user=user)[0] == 0, "CE_SettingIs reads %s=%s back" % (key, value))
        expect(s.run("is", key, value.upper(), user=user)[0] == 0, "CE_SettingIs ignores the case of %s" % value)
        expect(s.run("is", key, value + "x", user=user)[0] == 1, "CE_SettingIs does not match a longer text")
        rc, out, *_ = s.run("text", key, "64", user=user)
        expect(rc == 0 and out == value, "CE_SettingGetText reads %s=%s back: %r" % (key, value, out))
    expect(s.run("bool", "update_check", "1", user=user)[1] == "0" and s.run("bool", "hud_anchor", "0", user=user)[1] == "1",
           "CE_SettingGetBool reads the 0 and the 1 back")


def a_value_that_is_not_one_line_is_refused(s):
    for value in ("a\nb=1", "a\rb", "a\x01b", "x\x7f", "", "a\tb"):
        rc, _, err, after, left, _ = s.run("set", "music_playtype", value, ini=b"keep=1\n")
        expect(rc == 1 and after == b"keep=1\n" and not left and "refused" in err,
               "the value %r is refused and the file stays: rc %d %r %r" % (value, rc, after, err))


def a_key_that_is_not_one_word_is_refused(s):
    for key in ("a b", "a=b", "a\nb", "", "a\tb", "a\x7f"):
        rc, _, err, after, left, _ = s.run("set", key, "1", ini=b"keep=1\n")
        expect(rc == 1 and after == b"keep=1\n" and not left and "refused" in err,
               "the key %r is refused and the file stays: rc %d %r %r" % (key, rc, after, err))


def null_arguments_are_refused(s):
    for command, argument in (("set-null-key", "1"), ("set-null-value", "k")):
        rc, _, _, after, _, _ = s.run(command, argument, ini=b"keep=1\n")
        expect(rc == 1 and after == b"keep=1\n", "%s is refused and the file stays" % command)


def a_failed_write_says_so_and_leaves_the_file(s):
    if os.geteuid() == 0:
        return
    user = s.user()
    user.mkdir()
    (user / "ce.ini").write_bytes(b"music_playtype=linear\n")
    user.chmod(0o500)
    try:
        rc, _, err, after, left, _ = s.run("set", "music_playtype", "random", user=user)
    finally:
        user.chmod(0o700)
    expect(rc == 1 and after == b"music_playtype=linear\n" and not left, "a folder that cannot take the new file: the old file stays")
    expect("could not write ce.ini (music_playtype=random); it is unchanged" in err, "and the log says so: %r" % err)


def odd_files_are_refused(s):
    user = s.user()
    user.mkdir()
    (user / "ce.ini").mkdir()
    rc, _, _, _, left, _ = s.run("set", "rumble", "0", user=user)
    expect(rc == 1 and (user / "ce.ini").is_dir() and not left, "a folder in the place of ce.ini is refused")
    user = s.user()
    user.mkdir()
    sentinel = s.root / ("sentinel-%d" % s.count)
    sentinel.write_bytes(b"keep sentinel")
    (user / "ce.ini").symlink_to(sentinel)
    rc, _, _, _, left, _ = s.run("set", "rumble", "0", user=user)
    expect(rc == 1 and sentinel.read_bytes() == b"keep sentinel" and (user / "ce.ini").is_symlink() and not left,
           "a link in the place of ce.ini is refused and its target stays")
    user = s.user()
    user.mkdir()
    os.mkfifo(user / "ce.ini")
    rc, *_ = s.run("set", "rumble", "0", user=user, timeout=10)
    expect(rc == 1 and stat.S_ISFIFO((user / "ce.ini").lstat().st_mode), "a FIFO is refused without blocking")
    big = b"a=1\n" * (((1 << 20) // 4) + 8)
    rc, _, _, after, left, _ = s.run("set", "rumble", "0", ini=big)
    expect(rc == 1 and after == big and not left, "a file over a mebibyte is not a ce.ini and is refused")


SCENARIOS = (adds_to_a_missing_file, replaces_the_line_and_keeps_every_other_byte, adds_a_new_line_at_the_end,
             duplicates_collapse_to_one_line, a_longer_key_is_not_the_line, the_same_value_again_changes_nothing,
             what_is_written_reads_back_through_the_readers, a_value_that_is_not_one_line_is_refused,
             a_key_that_is_not_one_word_is_refused, null_arguments_are_refused,
             a_failed_write_says_so_and_leaves_the_file, odd_files_are_refused)


def failures(binary, root):
    found = []
    scratch = Scratch(binary, root)
    for scenario in SCENARIOS:
        try:
            scenario(scratch)
        except Fail as problem:
            found.append("%s: %s" % (scenario.__name__, problem))
    return found


class SettingText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/ce-setting-text-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.directory = tempfile.mkdtemp(prefix="run-", dir=parent)
        cls.out = Path(cls.directory)
        cls.source = (PORT / "platform/common/ce.cpp").read_text()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.directory, ignore_errors=True)

    def test_scenarios_pass_on_the_actual_setter(self):
        root = self.out / "scratch-base"
        root.mkdir()
        self.assertEqual([], failures(build(self.out, self.source, "base"), root))

    def test_every_mutant_is_caught_by_the_scenarios_alone(self):
        for number, (name, old, new) in enumerate(MUTANTS):
            with self.subTest(mutant=name):
                self.assertEqual(1, self.source.count(old), "the mutant's text is in ce.cpp exactly once")
                root = self.out / ("scratch-mutant-%d" % number)
                root.mkdir()
                found = failures(build(self.out, self.source.replace(old, new), "mutant-%d" % number), root)
                self.assertTrue(found, "mutant not caught: " + name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
