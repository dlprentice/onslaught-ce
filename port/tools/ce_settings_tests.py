#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The CE Settings page's table and the game's readers of ce.ini agree (port/game/ce_frontend.cpp: kSettings).

The page is a table: a row names a ce.ini line, the choices the line can hold and the choice that applies when the line is
absent or unusable. The game reads each line where it uses it, with a literal call in the consumer's own source; the launcher
reads the two lines it owns (update_check, record_sessions) with its own reader, ini_get. This test takes the table's text and
the item's lookup (CCeSettingItem::Lookup) out of ce_frontend.cpp and compiles them, unchanged, next to the actual readers
(port/platform/common/ce.cpp), on scratch user folders; the consumers' literal reads are read from their sources, and the
launcher's read_all and ini_get are compiled from ce_launch.c as they stand.

Checked:
- every row has a consumer and every consumer has a row; a switch's choices are 0 and 1 and its default is the consumer's default;
  a text choice (console_theme, music_playtype) offers the values the consumer tests for and the one it takes otherwise, which is
  the default; Your Music shows a line and offers no choice; the two lines the launcher reads have the values its test names; the
  grey "when it applies" line is the next level for the two HUD rows and the Dodge Button (ce_dodge_assist.cpp reads dodge_button
  when a level is entered), the console's next opening for Console Text (ce_palette.cpp reads console_theme each time the console
  opens, and once for the developer information text) and the next start for every other;
- for many ce.ini files (no file, no line, each value in several writings, other texts, a line twice, near misses of the key, a
  carriage return or a NUL byte in a line) the choice the page shows equals what the consumer decides when it asks the actual
  reader: the game's readers for the switches and the text choices, the launcher's own ini_get for update_check and record_sessions;
- mutants: a consumer's default, a table default, a key, a value, the lookup's fallback, the lookup's choice of reader, a reader's
  rule for a repeated line, four rules of the launcher-style reader, a row with no consumer and a consumer with no row must each be
  caught, and the test must say what each breaks;
- the known limit (port/PARITY.md, CE Settings), as it stands: for a ce.ini the game's readers refuse (over 1 MiB, a symbolic link,
  or in a linked folder) the page shows the launcher rows' defaults while the launcher's own reader follows the lines; the same
  lines in an ordinary file show what the launcher does; and in a ce.ini whose earlier line holds a NUL byte, the page's write of a
  launcher row is made after that byte, where the launcher (a C string) never reads, so the launcher and the page's next look both
  still take the default.

    python3 -I port/tools/ce_settings_tests.py
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
sys.path.insert(0, str(PORT / "tools"))
from ce_setting_text_tests import SDL_STUB  # noqa: E402  (the authored SDL header the readers' file includes)

FRONTEND = "port/game/ce_frontend.cpp"
CE_CPP = "port/platform/common/ce.cpp"

# key, the source that reads it, and how: bool (CE_SettingGetBool(key, default)), is (CE_SettingIs(key, text) for each text it
# tests, in order; the line's other values mean the default), text (CE_SettingGetText: a line only shown), or the launcher's
# own reader (ini_get: the first line, compared exactly): off0 (checking is off only for "0"), on1 (recording is on only for "1")
CONSUMERS = (
    ("hud_scale", "port/platform/common/hud_scale.cpp", "bool"),
    ("hud_anchor", "port/platform/common/hud_scale.cpp", "bool"),
    ("sharp_ui", "port/platform/common/widescreen.cpp", "bool"),
    ("debrief_stats", "port/game/ce_debrief.cpp", "bool"),
    ("rumble", "port/game/ce_rumble.cpp", "bool"),
    ("dodge_button", "port/game/ce_dodge_assist.cpp", "bool"),
    ("commander", "port/platform/common/ce.cpp", "mode"),
    ("dev_stats", "port/game/ce_palette.cpp", "bool"),
    ("console_theme", "port/game/ce_palette.cpp", "is"),
    ("music_playtype", "port/game/ce_music.cpp", "is"),
    ("music_dir", "port/game/ce_music.cpp", "text"),
    ("update_check", "port/updater/ce_update_v2.h", "off0"),
    ("record_sessions", "port/updater/ce_launch.c", "on1"),
)
SOURCES = sorted({FRONTEND, CE_CPP, *(source for _, source, _ in CONSUMERS)})


class Fail(Exception):
    pass


def region(text, start, end, inclusive_line=False):
    """The text from the first start up to (and with, when the end opens a line) the end's own line."""
    a = text.index(start)
    b = text.index(end, a)
    if inclusive_line:
        b = text.index("\n", b) + 1
    return text[a:b]


def lookup_text(frontend):
    cls = frontend.index("class CCeSettingItem")
    match = re.compile(r"^\tint\t\tLookup\(\)\n\t\{\n.*?^\t\}\n", re.S | re.M).search(frontend, cls)
    if not match:
        raise Fail("CCeSettingItem::Lookup is not where the test expects it")
    return match.group(0)


def launcher_reader(texts):
    """read_all and ini_get as they stand in the launcher's source (the reader of the two lines it owns)."""
    text = texts["port/updater/ce_launch.c"]
    read_all = re.search(r"^static char \*read_all\(const char \*p, size_t \*n\)\n\{.*?^\}\n", text, re.S | re.M)
    ini_get = re.search(r"^static int ini_get\(const char \*key, char \*out, size_t n\)\n\{.*?^\}\n", text, re.S | re.M)
    if not read_all or not ini_get:
        raise Fail("ce_launch.c's read_all or ini_get is not where the test expects")
    return read_all.group(0) + "\n" + ini_get.group(0)


def build_launcher(folder, texts, reads):
    """The launcher's reader and the two tests the updater and the launcher make of the lines they own, as a small program."""
    (folder / "launcher_read.inc").write_text(launcher_reader(texts))
    (folder / "launcher_off0.inc").write_text(reads["update_check"][1])
    (folder / "launcher_on1.inc").write_text(reads["record_sessions"][1])
    binary = folder / "launcher"
    command = ["gcc", "-std=gnu11", "-O0", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-I", str(folder),
               str(PORT / "tools/launcher_ini_driver.c"), "-o", str(binary)]
    done = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if done.returncode:
        raise Fail("the launcher's reader did not compile:\n" + done.stdout + done.stderr)
    return binary


def build(out, texts, reads, name):
    """The driver on the page's table and lookup, taken as they stand in texts, and ce.cpp as it stands in texts; with the
    launcher's own reader, from its source, as a second program. Returns both."""
    folder = out / name
    folder.mkdir()
    frontend = texts[FRONTEND]
    (folder / "settings_hint.inc").write_text(region(frontend, "struct SCeHint\n{", "};\n", False) + "};\n")
    (folder / "settings_table.inc").write_text(region(frontend, "struct SCeChoice\n{", "#define CE_NUM_SETTINGS", True))
    (folder / "settings_lookup.inc").write_text(lookup_text(frontend))
    include = '#include "../../updater/ce_report.h"'
    ce = texts[CE_CPP]
    if ce.count(include) != 1:
        raise Fail("ce.cpp no longer includes ce_report.h as the test expects")
    (folder / "ce.cpp").write_text(ce.replace(include, '#include "ce_report.h"'))
    (folder / "SDL3").mkdir()
    (folder / "SDL3/SDL.h").write_text(SDL_STUB)
    binary = folder / "driver"
    command = ["g++", "-std=c++17", "-O0", "-g", "-Wall", "-Wextra", "-Wno-unused-variable", "-Wno-unused-parameter",
               '-DCE_VERSION="test"', "-I", str(folder), "-I", str(PORT / "updater"), "-I", str(PORT / "platform/common"),
               str(PORT / "tools/ce_settings_driver.cpp"), str(folder / "ce.cpp"), "-o", str(binary)]
    done = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if done.returncode:
        raise Fail("the driver did not compile:\n" + done.stdout + done.stderr)
    return binary, build_launcher(folder, texts, reads)


def ask(binary, *args):
    env = {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": "/nonexistent"}
    done = subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=20, env=env)
    if done.returncode:
        raise Fail("the driver failed on %s: %s" % (args, done.stderr))
    return done.stdout


def compiled_table(binary):
    rows = []
    for line in ask(binary, "table").splitlines():
        key, kind, default, values, applies, after = line.split("\t")
        rows.append({"key": key, "shown": kind == "shown", "default": int(default), "values": values.split(",") if values else [],
                     "applies": applies, "after": after})
    return rows


def literal_reads(texts):
    """What each consumer's source asks of ce.ini: {key: (kind, data)}; a missing or conflicting read is a Fail."""
    reads = {}
    for key, source, kind in CONSUMERS:
        text = texts[source]
        if kind == "bool":
            found = re.findall(r'CE_SettingGetBool\("%s",\s*(-?\d+)\)' % key, text)
            if not found:
                raise Fail("%s no longer reads %s with CE_SettingGetBool" % (source, key))
            if len(set(found)) != 1:
                raise Fail("%s reads %s with different defaults %s" % (source, key, sorted(set(found))))
            reads[key] = (kind, int(found[0]))
        elif kind == "mode":
            # a line of the [Experimental] block: ce.cpp's PortCeExperimental reads it with CE_SettingGetBool(mode, default) for the name in kExperimentalKeys
            keys = re.search(r'kExperimentalKeys\[\d+\] = \{([^}]*)\}', text)
            found = re.findall(r'CE_SettingGetBool\(mode,\s*(-?\d+)\)', text)
            if not keys or '"%s"' % key not in keys.group(1) or len(found) != 1:
                raise Fail("%s no longer reads %s with CE_SettingGetBool through kExperimentalKeys" % (source, key))
            reads[key] = ("bool", int(found[0]))
        elif kind == "is":
            tested = list(dict.fromkeys(re.findall(r'CE_SettingIs\("%s",\s*"([^"]+)"\)' % key, text)))
            if not tested:
                raise Fail("%s no longer reads %s with CE_SettingIs" % (source, key))
            reads[key] = (kind, tested)
        elif kind == "text":
            if not re.search(r'CE_SettingGetText\("%s"' % key, text):
                raise Fail("%s no longer reads %s with CE_SettingGetText" % (source, key))
            reads[key] = (kind, None)
        elif kind in ("off0", "on1"):
            text_value = "0" if kind == "off0" else "1"
            found = re.search(r'ini_get\("%s", opt, sizeof opt\) && !strcmp\(opt, "%s"\)' % (key, text_value), text)
            if not found:
                raise Fail("%s no longer tests %s for exactly %s as the test expects" % (source, key, text_value))
            reads[key] = (kind, found.group(0))
    return reads


def check_static(rows, reads):
    """The table against the consumers' reads; the list of what disagrees."""
    problems = []
    keys = [row["key"] for row in rows]
    for key in keys:
        if key not in reads:
            problems.append("row %s has no consumer in the test's list" % key)
    for key in reads:
        if key not in keys:
            problems.append("the consumer of %s has no row on the page" % key)
    for row in rows:
        if row["key"] not in reads:
            continue
        kind, data = reads[row["key"]]
        values, default = row["values"], row["default"]
        shown = values[default] if values and 0 <= default < len(values) else None
        if kind == "text":
            if not row["shown"]:
                problems.append("%s is a line the page only shows, and it offers choices" % row["key"])
            continue
        if row["shown"]:
            problems.append("%s offers no choices and its consumer takes a value" % row["key"])
            continue
        if shown is None:
            problems.append("%s: the default is not one of the choices" % row["key"])
            continue
        if kind == "bool":
            if sorted(values) != ["0", "1"]:
                problems.append("%s is a switch read as 0 or 1; its choices are %s" % (row["key"], values))
            elif shown != str(data):
                problems.append("%s: the row's default is %s, the consumer's is %d" % (row["key"], shown, data))
        elif kind == "is":
            others = [v for v in values if v not in data]
            if any(v not in values for v in data) or len(others) != 1:
                problems.append("%s: the choices %s are not the consumer's %s and one other" % (row["key"], values, data))
            elif shown != others[0]:
                problems.append("%s: the default %s is not what the consumer takes otherwise (%s)" % (row["key"], shown, others[0]))
        elif kind in ("off0", "on1"):
            want_default = "1" if kind == "off0" else "0"      # checking is on, recording is off, unless the line says otherwise
            if sorted(values) != ["0", "1"] or shown != want_default:
                problems.append("%s: the choices %s with %s by default do not match the launcher's test" % (row["key"], values, shown))
        wanted = {"hud_scale": "next-level", "hud_anchor": "next-level", "dodge_button": "next-level", "console_theme": "console-open"}.get(row["key"], "next-start")
        if row["applies"] != wanted:
            problems.append("%s says it applies %s, not %s" % (row["key"], row["applies"], wanted))
        wanted_after = {"hud_scale": "hud", "hud_anchor": "hud", "record_sessions": "record"}.get(row["key"], "none")
        if row["after"] != wanted_after:
            problems.append("%s does %s after a write, not %s" % (row["key"], row["after"], wanted_after))
    return problems


def ini_variants(key, values):
    """The ce.ini files tried for a row: bytes, or None for no file."""
    cases = [None, b"", b"other=1\n", b"other=1\n" + b"\n" * 3]
    texts = list(values) + ["2", "", "yes", "00", "01"]
    for text in texts:
        for form in ("%s=%s\n", "%s=%s", "%s=%s\r\n", "%s= %s \t\n", "%s= %s\n", "%s=%s \n", "%s=%s\rjunk\n",
                     "other=1\n%s=%s\nlast=2\n",
                     # the [Experimental] block of the shipped ce.ini: a header and a comment line before and after the line, and the block's own lines
                     "[Experimental]\n; Unfinished features, off in this release. They may change or break. Set a line to 1 to try one.\n%s=%s\nmuseum=0\n",
                     "other=1\n%s=%s\n\n[Experimental]\n; Unfinished features, off in this release. They may change or break. Set a line to 1 to try one.\nmap_editor=0\nmuseum=0\ncommander=0\nplay_unit=0\n"):
            cases.append((form % (key, text)).encode())
        cases.append(("%s=%s\n" % (key, text.upper())).encode())
    for a in values:
        for b in values:
            cases.append(("%s=%s\n%s=%s\n" % (key, a, key, b)).encode())
    cases += [("%sx=%s\n" % (key, values[0])).encode(), ("x%s=%s\n" % (key, values[0])).encode(),
              ("%s =%s\n" % (key, values[0])).encode(), ("%s=%s\n" % (key.upper(), values[0])).encode()]
    cases += [b"a=\x00b\n" + ("%s=%s\n" % (key, value)).encode() for value in values]
    return cases


def check_dynamic(binary, launcher, rows, reads, scratch, only=None):
    """For each ce.ini file: the choice the page shows against what the consumer decides; the list of what differs."""
    problems = []
    count = 0
    for row in rows:
        key = row["key"]
        if (only and key not in only) or key not in reads or reads[key][0] not in ("bool", "is", "off0", "on1") or row["shown"]:
            continue
        kind, data = reads[key]
        for number, ini in enumerate(ini_variants(key, row["values"])):
            folder = scratch / ("%s-%d" % (key, number))
            folder.mkdir()
            if ini is not None:
                (folder / "ce.ini").write_bytes(ini)
            page = dict(line.split("\t") for line in ask(binary, "page", str(folder)).splitlines())
            index = int(page[key])
            if not 0 <= index < len(row["values"]):
                problems.append("%s shows no choice (%d) for %r" % (key, index, ini))
                continue
            shown = row["values"][index]
            if kind == "bool":
                decided = "1" if int(ask(binary, "bool", str(folder), key, str(data)).strip()) else "0"
            elif kind in ("off0", "on1"):
                answer = ask(launcher, str(folder / "ce.ini"), kind).strip() == "1"
                decided = ("0" if answer else "1") if kind == "off0" else ("1" if answer else "0")
            else:
                decided = next((v for v in data if ask(binary, "is", str(folder), key, v).strip() == "1"),
                               next((v for v in row["values"] if v not in data), None))
            count += 1
            if shown != decided:
                problems.append("%s: the page shows %s and the consumer takes %s for %r" % (key, shown, decided, ini))
    return problems, count


def run_all(out, texts, name, only=None):
    """Compile on texts and return everything that disagrees, with how many ce.ini files were compared."""
    reads = literal_reads(texts)
    binary, launcher = build(out, texts, reads, name)
    rows = compiled_table(binary)
    problems = check_static(rows, reads)
    scratch = out / (name + "-ini")
    scratch.mkdir()
    dynamic, count = ([], 0) if only is not None and not only else check_dynamic(binary, launcher, rows, reads, scratch, only)
    return problems + dynamic, count, rows


def read_sources():
    return {source: (ROOT / source).read_text() for source in SOURCES}


def swap(old, new):
    """A mutation of one file's text: old, which must be there once, becomes new."""
    def mutate(text):
        if text.count(old) != 1:
            raise Fail("the text a mutant changes is not unique: %r" % old[:60])
        return text.replace(old, new)
    return mutate


def drop_row(label):
    """The page's row with this label taken out of the table."""
    def mutate(text):
        text, n = re.subn(r'\t\{L"%s: ".*?CEAF_\w+, [01]\},\n' % re.escape(label), "", text, count=1, flags=re.S)
        if n != 1:
            raise Fail("the row %s is not in the table" % label)
        return text
    return mutate


# (name, the file it changes, the change, the keys the comparison on the actual readers must look at (none: the table's own
# checks must catch it), and what the test must say about it)
MUTANTS = (
    ("a consumer's default is changed", "port/platform/common/hud_scale.cpp",
     swap('CE_SettingGetBool("hud_scale", 1)', 'CE_SettingGetBool("hud_scale", 0)'), {"hud_scale"},
     "hud_scale: the row's default is 1, the consumer's is 0"),
    ("another consumer's default is changed", "port/platform/common/widescreen.cpp",
     swap('CE_SettingGetBool("sharp_ui", 0)', 'CE_SettingGetBool("sharp_ui", 1)'), {"sharp_ui"},
     "sharp_ui: the row's default is 0, the consumer's is 1"),
    ("a table default is changed", FRONTEND,
     swap('{L"Sharper Text: ", "sharp_ui", CE_CHOICES(kNoYes), 0,', '{L"Sharper Text: ", "sharp_ui", CE_CHOICES(kNoYes), 1,'),
     {"sharp_ui"}, "sharp_ui: the row's default is 1, the consumer's is 0"),
    ("a text row's default is changed", FRONTEND,
     swap('{L"Console Text: ", "console_theme", CE_CHOICES(kConsoleTexts), 0,',
          '{L"Console Text: ", "console_theme", CE_CHOICES(kConsoleTexts), 1,'), {"console_theme"},
     "console_theme: the default green is not what the consumer takes otherwise (yellow)"),
    ("a key is mistyped", FRONTEND, swap('"hud_anchor", CE_CHOICES(kHudLayouts)', '"hud_ancor", CE_CHOICES(kHudLayouts)'), set(),
     "row hud_ancor has no consumer"),
    ("a switch's value is renamed", FRONTEND, swap('{L"Yes", "1"}', '{L"Yes", "on"}'), set(), "is a switch read as 0 or 1"),
    ("the lookup's fallback is the first choice", FRONTEND,
     swap("\t\treturn mSetting->mDefault;\n\t}\n\n\tconst SCeSetting", "\t\treturn 0;\n\t}\n\n\tconst SCeSetting"), {"hud_scale"},
     "hud_scale: the page shows 0 and the consumer takes 1"),
    ("a reader keeps the first of a repeated line", CE_CPP,
     swap("\t\tif (seen) { value = def; break; }\n", "\t\tif (seen) { break; }\n"), {"rumble"},
     "rumble: the page shows 1 and the consumer takes 0"),
    ("the launcher's rows are looked up the game's way", FRONTEND,
     swap("if (mSetting->mLauncher ? CE_SettingLauncherIs(", "if (false ? CE_SettingLauncherIs("), {"update_check", "record_sessions"},
     "the page shows"),
    ("the launcher-style reader takes the last of a repeated line", CE_CPP,
     swap("\t\tmatch = end - pos == want && !memcmp(line + pos, value, want);\n\t\tbreak;\n",
          "\t\tmatch = end - pos == want && !memcmp(line + pos, value, want);\n"), {"update_check"},
     "update_check: the page shows"),
    ("the launcher-style reader trims blanks", CE_CPP,
     swap("\t\tsize_t pos = size + 1, end = pos;\n\t\twhile (end < n && line[end] != '\\r') ++end;\n",
          "\t\tsize_t pos = size + 1, end;\n\t\twhile (pos < n && (line[pos] == ' ' || line[pos] == '\\t')) ++pos;\n\t\tend = pos;\n"
          "\t\twhile (end < n && line[end] != '\\r') ++end;\n"), {"update_check"}, "update_check: the page shows"),
    ("the launcher-style reader sees past a NUL byte", CE_CPP,
     swap("\ttext[len] = 0;\n\tlen = strlen(text);\n", "\ttext[len] = 0;\n"), {"update_check"}, "update_check: the page shows"),
    ("the launcher-style reader runs a value to the end of the line", CE_CPP,
     swap("while (end < n && line[end] != '\\r') ++end;", "while (end < n && line[end] != '\\n') ++end;"), {"update_check"},
     "update_check: the page shows"),
    ("a row has no consumer", FRONTEND,
     swap('\t{L"Record Play for Bug Reports: "',
          '\t{L"Unread Row: ", "unread_key", CE_CHOICES(kNoYes), 0,\n\t {L"A row nothing reads.", kNextStart}, CEAF_NONE, 0},\n'
          '\t{L"Record Play for Bug Reports: "'), set(), "row unread_key has no consumer"),
    ("a consumer has no row", FRONTEND, drop_row("Sharper Text"), set(), "the consumer of sharp_ui has no row"),
    ("the console row says the next start", FRONTEND,
     swap('developer text.", kNextConsole}', 'developer text.", kNextStart}'), set(),
     "console_theme says it applies next-start, not console-open"),
)


class SettingsAgree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/ce-settings-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        cls.out = Path(cls.tmp.name)
        cls.texts = read_sources()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_table_and_the_consumers_agree_on_the_actual_readers(self):
        problems, count, rows = run_all(self.out, self.texts, "as-is")
        self.assertEqual(problems, [])
        self.assertGreaterEqual(count, 300, "too few ce.ini files were compared")
        self.assertTrue(all(row["key"] in dict((k, 1) for k, _, _ in CONSUMERS) for row in rows))

    def test_a_file_the_game_refuses_shows_the_defaults_while_the_launcher_reads_it(self):
        """The launcher opens ce.ini its own way (fopen, no size limit, links followed), so where the game's readers refuse the
        file the two launcher rows show their defaults (checking on, recording off) while the launcher acts on the lines."""
        reads = literal_reads(self.texts)
        binary, launcher = build(self.out, self.texts, reads, "refused")
        rows = {row["key"]: row for row in compiled_table(binary)}
        lines = b"update_check=0\nrecord_sessions=1\n"
        scratch = self.out / "refused-ini"
        scratch.mkdir()
        folders = {name: scratch / name for name in ("plain", "big", "linked", "target")}
        for folder in folders.values():
            folder.mkdir()
        (folders["plain"] / "ce.ini").write_bytes(lines)
        (folders["big"] / "ce.ini").write_bytes(lines + b"# padding\n" * ((1 << 20) // 10 + 1))
        self.assertGreater((folders["big"] / "ce.ini").stat().st_size, 1 << 20)
        (folders["linked"] / "real.ini").write_bytes(lines)
        (folders["linked"] / "ce.ini").symlink_to("real.ini")
        (folders["target"] / "ce.ini").write_bytes(lines)
        (scratch / "folder-link").symlink_to("target")
        refused = {"over 1 MiB": folders["big"], "a symbolic link": folders["linked"], "in a linked folder": scratch / "folder-link"}

        def shown(folder):
            page = dict(line.split("\t") for line in ask(binary, "page", str(folder)).splitlines())
            return {key: rows[key]["values"][int(page[key])] for key in ("update_check", "record_sessions")}

        def launcher_says(folder):
            return {kind: ask(launcher, str(folder / "ce.ini"), kind).strip() for kind in ("off0", "on1")}

        self.assertEqual(shown(folders["plain"]), {"update_check": "0", "record_sessions": "1"}, "an ordinary file shows its lines")
        self.assertEqual(launcher_says(folders["plain"]), {"off0": "1", "on1": "1"})
        for name, folder in refused.items():
            with self.subTest(file=name):
                self.assertEqual(shown(folder), {"update_check": "1", "record_sessions": "0"}, "the page shows the defaults")
                self.assertEqual(launcher_says(folder), {"off0": "1", "on1": "1"}, "the launcher turns checking off and records")

    def test_a_write_after_a_nul_byte_is_not_seen_by_the_launcher(self):
        """A ce.ini whose earlier line holds a NUL byte (a corrupt file): the page's write of a launcher row is made, by the setter that
        takes the file by length, after that byte, which the launcher, reading a C string, never reaches. The row then shows the new
        choice until the page opens again (CCeSettingItem keeps what it wrote), while the launcher and the page's next look both still
        take the default."""
        reads = literal_reads(self.texts)
        binary, launcher = build(self.out, self.texts, reads, "nul")
        rows = {row["key"]: row for row in compiled_table(binary)}
        scratch = self.out / "nul-ini"
        scratch.mkdir()
        for key, value, kind in (("update_check", "0", "off0"), ("record_sessions", "1", "on1")):
            with self.subTest(key=key):
                folder = scratch / key
                folder.mkdir()
                (folder / "ce.ini").write_bytes(b"a=\x00b\n")
                self.assertEqual(ask(binary, "set", str(folder), key, value).strip(), "1", "the write is made")
                written = (folder / "ce.ini").read_bytes()
                self.assertIn(b"\x00", written)
                self.assertIn(("%s=%s" % (key, value)).encode(), written.split(b"\x00", 1)[1], "after the NUL byte")
                self.assertEqual(ask(launcher, str(folder / "ce.ini"), kind).strip(), "0", "the launcher never reads it")
                page = dict(line.split("\t") for line in ask(binary, "page", str(folder)).splitlines())
                self.assertEqual(int(page[key]), rows[key]["default"], "the page's next look shows the default")
                # the same write in a file without the byte is what the launcher does
                (folder / "ce.ini").write_bytes(b"a=b\n")
                self.assertEqual(ask(binary, "set", str(folder), key, value).strip(), "1")
                self.assertEqual(ask(launcher, str(folder / "ce.ini"), kind).strip(), "1", "without it the launcher reads the write")

    def test_each_mutant_is_caught(self):
        for number, (name, source, mutate, only, said) in enumerate(MUTANTS):
            with self.subTest(mutant=name):
                texts = dict(self.texts)
                texts[source] = mutate(texts[source])
                try:
                    problems, _, _ = run_all(self.out, texts, "mutant-%d" % number, only)
                except Fail as error:
                    problems = [str(error)]
                self.assertTrue(any(said in problem for problem in problems), "the test did not say %r: %s" % (said, problems[:3]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
