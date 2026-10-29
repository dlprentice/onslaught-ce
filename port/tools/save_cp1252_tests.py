#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Linux career names (patch 0101, port/platform/linux/save_cp1252.cpp): the codec against Windows' code page 1252, the
actual patched PCMemoryCard.cpp name and path helpers with the game's own ToWCHAR/FromWCHAR and IsCheatActive over an
authored folder, and proof that the Windows build's source is unchanged. No game, real save or display."""
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCHES = ROOT / "port/patches"
CODEC = ROOT / "port/platform/linux/save_cp1252.cpp"
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    return source[start:end]


def sections(patch):
    return {m.group(1): m.group(0) for m in re.finditer(
        r"(?ms)^--- a/([^\n]+)\n\+\+\+ b/[^\n]+\n.*?(?=^--- a/|\Z)", patch)}


def patched(out, name, upto=None):
    """src/name with every port patch's section for it applied in order (up to and including the patch named upto)."""
    (out / name).write_bytes((ROOT / "src" / name).read_bytes())
    for patch in sorted(PATCHES.glob("*.patch")):
        part = sections(patch.read_text(encoding="latin1")).get(name)
        if part:
            (out / "section.patch").write_text(part, encoding="latin1")
            r = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", "section.patch"], cwd=out,
                               capture_output=True, text=True, timeout=10, preexec_fn=no_core)
            assert r.returncode == 0 and "fuzz" not in r.stdout.lower(), patch.name + r.stdout + r.stderr
        if upto and patch.name == upto:
            break
    return (out / name).read_text(encoding="latin1")


def windows_view(text):
    """The lines a _WIN32 build compiles: #ifdef/#ifndef _WIN32 blocks resolved, every other directive kept."""
    out, stack = [], []		# stack entries: (is_win32_block, keeping_this_branch, parent_keeping)
    keep = True
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef)\s+_WIN32\b", s)
        if m:
            stack.append((True, keep))
            keep = keep and (m.group(1) == "ifdef")
            continue
        if re.match(r"#\s*if", s):
            stack.append((False, keep))
        elif re.match(r"#\s*else\b", s) and stack and stack[-1][0]:
            keep = stack[-1][1] and not keep
            continue
        elif re.match(r"#\s*endif\b", s):
            win32, parent = stack.pop()
            if win32:
                keep = parent
                continue
        if keep:
            out.append(re.sub(r"\s*//\s*port 0101\s*$", "", line))
    return "\n".join(l for l in out if l.strip())	# blank lines are not compiled


CODEC_DRIVER = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" unsigned PortSaveNameFromFileCp1252(const char *, void *, unsigned);
extern "C" int PortSaveFileFromNameCp1252(const void *, char *, unsigned);
extern "C" int PortSaveLegacyFromNameCp1252(const void *, char *, unsigned);
static void check(bool c, const char *m) { if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(2); } }
static unsigned decode(const char *s, uint16_t *out, unsigned cap = 64) { return PortSaveNameFromFileCp1252(s, out, cap); }
int main() {
    uint16_t w[64];
    check(decode("lat\xc3\xaate", w) == 6 && w[3] == 0xea && w[6] == 0, "UTF-8 latete gives the byte 0xEA");
    check(decode("Linux Test 1", w) == 0, "ASCII keeps the game's own conversion");
    check(decode("lat\xeate", w) == 0, "a Latin-1 name is not UTF-8: unchanged");
    check(decode("\xe2\x82\xac", w) == 1 && w[0] == 0x20ac, "the euro sign reads as U+20AC, as the keyboard and the Windows build hold it");
    check(decode("\xc2\x81", w) == 1 && w[0] == 0x81, "Windows round-trips the undefined 0x81");
    check(decode("\xc5\xb8", w) == 1 && w[0] == 0x178, "Y with diaeresis reads as U+0178");
    check(decode("\xc0\xaf", w) == 0 && decode("\xe0\x80\xaf", w) == 0, "overlong forms are refused");
    check(decode("\xed\xa0\x80", w) == 0, "surrogates are refused");
    check(decode("\xf0\x9f\x98\x80", w) == 0, "characters code page 1252 lacks keep the old name");
    check(decode("\xc4\x80", w) == 0, "A with macron is not in code page 1252");
    check(decode("ab\xc3", w) == 0 && decode("\xc3", w) == 0, "a cut sequence is refused");
    check(decode("\xc3\xa9\xc3\xa9", w, 3) == 2 && decode("\xc3\xa9\xc3\xa9", w, 2) == 0 && decode("\xc3\xa9", w, 2) == 1, "capacity includes the terminator");
    check(PortSaveNameFromFileCp1252(nullptr, w, 4) == 0 && PortSaveNameFromFileCp1252("\xc3\xa9", nullptr, 4) == 0, "null inputs");
    char f[64];
    uint16_t name[8] = {'l', 'a', 't', 0xea, 't', 'e', 0};
    check(PortSaveFileFromNameCp1252(name, f, sizeof f) == 1 && !std::strcmp(f, "lat\xc3\xaate"), "0xEA is saved as UTF-8");
    uint16_t ascii[4] = {'a', 'b', 'c', 0};
    check(PortSaveFileFromNameCp1252(ascii, f, sizeof f) == 0, "ASCII keeps the game's own file name");
    uint16_t wide[3] = {'a', 0x20ac, 0};
    check(PortSaveFileFromNameCp1252(wide, f, sizeof f) == 1 && !std::strcmp(f, "a\xe2\x82\xac"), "a character above 0xFF is written as itself");
    uint16_t tm[3] = {'A', 0x2122, 0}, quote[3] = {'A', '"', 0}, oe[3] = {0x153, 0x152, 0}, cjk[3] = {'A', 0x4e2d, 0};
    check(PortSaveFileFromNameCp1252(tm, f, sizeof f) == 1 && !std::strcmp(f, "A\xe2\x84\xa2"), "the keyboard's trademark sign is its UTF-8, never narrowed to a quote");
    check(PortSaveFileFromNameCp1252(oe, f, sizeof f) == 1 && !std::strcmp(f, "\xc5\x93\xc5\x92"), "oe ligatures");
    check(PortSaveFileFromNameCp1252(cjk, f, sizeof f) == 1 && !std::strcmp(f, "A\xe4\xb8\xad"), "a character code page 1252 lacks is written as itself, not collapsed");
    check(PortSaveFileFromNameCp1252(quote, f, sizeof f) == 0, "A\" keeps the game's own ASCII name");
    check(PortSaveLegacyFromNameCp1252(tm, f, sizeof f) == 1 && !std::strcmp(f, "A\x99"), "the trademark sign's older one-byte name is code page 1252 0x99");
    check(PortSaveLegacyFromNameCp1252(cjk, f, sizeof f) == 0, "a character code page 1252 lacks has no older one-byte name");
    check(PortSaveLegacyFromNameCp1252(oe, f, sizeof f) == 1 && !std::strcmp(f, "\x9c\x8c"), "oe ligatures are 0x9C, 0x8C");
    uint16_t euro[2] = {0x80, 0};
    check(PortSaveFileFromNameCp1252(euro, f, sizeof f) == 1 && !std::strcmp(f, "\xe2\x82\xac"), "0x80 is the euro sign");
    check(PortSaveFileFromNameCp1252(euro, f, 3) == 0 && PortSaveFileFromNameCp1252(euro, f, 4) == 1, "room for the bytes and terminator");
    // Every byte above 0x7F round-trips: name -> file -> name.
    for (unsigned b = 0x80; b < 0x100; ++b) {
        uint16_t one[2] = {(uint16_t)b, 0}, back[4];
        check(PortSaveFileFromNameCp1252(one, f, sizeof f) == 1, "every high byte has a UTF-8 file name");
        char again[8];
        check(decode(f, back) == 1 && PortSaveFileFromNameCp1252(back, again, sizeof again) == 1 && !std::strcmp(again, f),
              "and reads back as the same character (the same file)");
    }
    std::puts("code page 1252 codec passed");
}
'''

CARD_DRIVER = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
typedef uint16_t WCHAR; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define SAVE_DIRECTORY		"savegames\\"
#define SAVE_EXTENSION		".bes"
static std::set<std::string> files;			// the folder: "savegames\\<name>.bes" byte strings
static int _access(const char *path, int) { return files.count(path) ? 0 : -1; }
static size_t wcslen16(const WCHAR *s) { size_t n = 0; while (s[n]) ++n; return n; }
#define wcslen wcslen16
#include "wchar.inc"
#undef wcslen
#include "helpers.inc"
// The game's cheat check (FEPSaveGame.cpp), on the name the load list gives.
struct { bool mAutoConfigTest = false; } CLIPARAMS;
char gAllCheatsEnabled = 0;
#include "cheats.inc"
struct CFEPSaveGame { WCHAR mSaveGameName[256]; BOOL IsCheatActive(int cheatno); };
#include "is_cheat.inc"
static void check(bool c, const char *m) { if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(2); } }
static std::string path(const char *name) { return std::string(SAVE_DIRECTORY) + name + SAVE_EXTENSION; }
static bool listed(const char *file) { return PortSaveEntryOK((std::string(file) + ".bes").c_str()); }
static bool latete(const char *file) {
    CFEPSaveGame card;
    char base[256]; std::snprintf(base, sizeof base, "%s", file);
    WCHAR *name = PortSaveWide(base);
    size_t n = 0; for (; name[n]; ++n) card.mSaveGameName[n] = name[n]; card.mSaveGameName[n] = 0;
    return card.IsCheatActive(5) && !card.IsCheatActive(0);
}
int main() {
    // A career named on Linux in UTF-8 now carries the cheat; Latin-1 and ASCII names behave as before.
    check(latete("lat\xc3\xaate"), "UTF-8 latete applies the cheat");
    check(latete("my lat\xc3\xaate career"), "anywhere in the name");
    check(latete("lat\xeate"), "a Latin-1 file name still does");
    check(!latete("latete"), "ASCII latete does not (the code has the accent)");
    check(!latete("lat\xc3\x83\xc2\xaate"), "a doubly encoded name does not");
    // Paths: a new career with the accent is saved as UTF-8; an existing Latin-1 file keeps its own name.
    WCHAR name[8] = {'l', 'a', 't', 0xea, 't', 'e', 0};
    check(path(PortSaveFile(name)) == path("lat\xc3\xaate"), "new careers are saved under their UTF-8 name");
    files.insert(path("lat\xeate"));
    check(path(PortSaveFile(name)) == path("lat\xeate"), "an existing Latin-1 file keeps its name");
    WCHAR plain[8] = {'H', 'a', 'w', 'k', 0};
    check(!std::strcmp(PortSaveFile(plain), "Hawk"), "ASCII names unchanged");
    // The listing: a UTF-8 twin of a Latin-1 career is listed once.
    check(listed("lat\xeate"), "the Latin-1 file is listed");
    check(!listed("lat\xc3\xaate"), "its UTF-8 twin is not listed (every lookup chooses the Latin-1 file)");
    // An old byte-for-byte name that is another career's UTF-8 file is not this career's file (the review's alias):
    // "Bob" + A-circumflex + (c), written byte for byte, is the UTF-8 file of the career "Bob(c)".
    files.clear();
    files.insert(path("Bob\xc2\xa9"));
    WCHAR alias[8] = {'B', 'o', 'b', 0xc2, 0xa9, 0};
    check(path(PortSaveFile(alias)) == path("Bob\xc3\x82\xc2\xa9"), "a different career never resolves to another career's file");
    check(listed("Bob\xc2\xa9"), "the existing career stays listed");
    files.insert(path("Bob\xc3\x82\xc2\xa9"));
    check(listed("Bob\xc3\x82\xc2\xa9"), "and the new one is listed beside it, not hidden behind it");
    WCHAR bob[8] = {'B', 'o', 'b', 0xa9, 0};
    check(path(PortSaveFile(bob)) == path("Bob\xc2\xa9"), "Bob(c) still resolves to its own file");
    // The review's second alias (0.1.8, Linux): the keyboard's trademark sign narrowed to a quote, so "A(TM)" took the
    // file of the career "A\"". Now the trademark sign is its own UTF-8; an older one-byte "A\x99" is the same name.
    files.clear();
    files.insert(path("A\""));
    WCHAR tmname[4] = {'A', 0x2122, 0};
    check(path(PortSaveFile(tmname)) == path("A\xe2\x84\xa2"), "A(TM) never takes the file of the career A\"");
    WCHAR quoted[4] = {'A', '"', 0};
    check(path(PortSaveFile(quoted)) == path("A\"") && listed("A\""), "the career A\" (and a 0.1.8 A(TM) saved as it) still opens");
    files.insert(path("A\xe2\x84\xa2"));
    check(listed("A\xe2\x84\xa2") && listed("A\""), "both are listed");
    files.clear();
    files.insert(path("A\x99"));
    check(path(PortSaveFile(tmname)) == path("A\x99"), "an older one-byte A(TM) save is that career's file");
    {   // A save copied from Windows, A\x99: it lists as A(TM) (0x99 is U+2122), so after "A(TM)" overwrites it, the
        // lookup that follows (CreateSave's _wcsicmp over the listed names) finds it.
        char b[8] = "A\x99";
        WCHAR *shown = PortSaveWide(b);
        check(shown[0] == 'A' && shown[1] == 0x2122 && shown[2] == 0, "a copied Windows save A\\x99 lists as A(TM), as Windows shows it");
        check(path(PortSaveFile(shown)) == path("A\x99"), "and opens as itself");
    }
    // Every name of one or two characters the virtual keyboard can type (and the quote): distinct names, distinct files.
    {
        std::vector<WCHAR> keys = {VK_KEYS};
        if (std::find(keys.begin(), keys.end(), (WCHAR)'"') == keys.end()) keys.push_back('"');
        std::set<std::string> seen;
        unsigned names = 0;
        files.clear();
        for (size_t i = 0; i < keys.size(); ++i)
            for (size_t j = 0; j <= keys.size(); ++j) {
                WCHAR n[4] = {keys[i], j < keys.size() ? keys[j] : (WCHAR)0, 0};
                std::string f = PortSaveFile(n);
                char shown[16];
                std::snprintf(shown, sizeof shown, "%04x %04x", (unsigned)n[0], (unsigned)n[1]);
                static std::map<std::string, std::string> owner;
                if (!seen.insert(f).second) std::fprintf(stderr, "names %s and %s share the file %s\n", owner[f].c_str(), shown, f.c_str());
                owner[f] = shown;
                check(owner.size() == seen.size() && seen.size() == names + 1, "two names typed on the keyboard share a file");
                ++names;
            }
        std::printf("keyboard sweep: %u names, %zu files\n", names, seen.size());
    }
    files.clear();
    check(listed("lat\xc3\xaate") && listed("Linux Test 1") && listed("x"), "otherwise every career is listed");
    check(listed("abc"), "short names");
    // GetSaveName's output is later copied into the virtual keyboard's 32-unit array. Count the actual returned
    // 16-bit name, including byte-for-byte fallbacks, not the number of UTF-8 bytes or displayed characters.
    check(listed(std::string(31, 'Q').c_str()), "31 ASCII units are admitted");
    check(!listed(std::string(32, 'Q').c_str()), "32 ASCII units are refused");
    check(!listed(std::string(255, 'Q').c_str()) && !listed(std::string(256, 'Q').c_str()),
          "long and over-capacity names are refused without a truncated identity");
    for (int n : {31, 32}) {
        std::string utf8, legacy, malformed;
        for (int i = 0; i < n; ++i) { utf8 += "\xc3\xa9"; legacy += char(0xe9); malformed += char(0xff); }
        check(listed(utf8.c_str()) == (n == 31), "UTF-8 1252 names use returned units");
        check(listed(legacy.c_str()) == (n == 31), "legacy names use returned units");
        check(listed(malformed.c_str()) == (n == 31), "non-UTF8 names use returned units");
    }
    std::string emoji7, emoji8;
    for (int i = 0; i < 7; ++i) emoji7 += "\xf0\x9f\x98\x80";
    emoji8 = emoji7 + "\xf0\x9f\x98\x80";
    check(listed(emoji7.c_str()) && !listed(emoji8.c_str()), "supplementary UTF-8 follows the existing byte fallback");
    std::string twin31utf, twin31raw, twin32utf, twin32raw;
    for (int i = 0; i < 31; ++i) { twin31utf += "\xc3\xa9"; twin31raw += char(0xe9); }
    twin32utf = twin31utf + "\xc3\xa9"; twin32raw = twin31raw + char(0xe9);
    files.clear();
    files.insert(path(twin31raw.c_str())); files.insert(path(twin32raw.c_str()));
    check(listed(twin31raw.c_str()) && !listed(twin31utf.c_str()), "eligible twins retain legacy precedence");
    check(!listed(twin32raw.c_str()) && !listed(twin32utf.c_str()), "oversized twins are both skipped");
    check(!PortSaveEntryOK(nullptr) && !PortSaveEntryOK("") && !PortSaveEntryOK(".bes") &&
          !PortSaveEntryOK("odd") && !PortSaveEntryOK("file.txt") && PortSaveEntryOK("valid.BES"),
          "malformed or empty entries are refused; the suffix remains case-insensitive");
    std::puts("Linux career names passed");
}
'''


ENUM_DRIVER = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
typedef uint16_t WCHAR; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define MCE_SUCCESS 0
#define _A_HIDDEN 2
#define _A_SYSTEM 4
#define _A_SUBDIR 16
#define SKIP_ATTRIBUTES (_A_HIDDEN | _A_SYSTEM | _A_SUBDIR)
#define SAVE_DIRECTORY "savegames\\"
#define SAVE_EXTENSION ".bes"
#define SAVE_PATTERN "savegames\\*.bes"
static std::set<std::string> files;
static int _access(const char *path, int) { return files.count(path) ? 0 : -1; }
static size_t wcslen16(const WCHAR *s) { size_t n = 0; while (s[n]) ++n; return n; }
#define wcslen wcslen16
#include "wchar.inc"
#undef wcslen
#include "helpers.inc"
struct _finddata_t { unsigned attrib; char name[256]; };
static long sFindHandle = -1;
static _finddata_t sFindData;
struct Entry { std::string name; unsigned attrib; };
static std::vector<Entry> entries;
static size_t position;
static void fill(_finddata_t *out) {
    out->attrib = entries[position].attrib;
    std::snprintf(out->name, sizeof out->name, "%s", entries[position].name.c_str());
}
static long _findfirst(const char *, _finddata_t *out) {
    position = 0; if (entries.empty()) return -1; fill(out); return 1;
}
static int _findnext(long, _finddata_t *out) {
    if (++position >= entries.size()) return -1;
    fill(out); return 0;
}
static int _findclose(long) { return 0; }
static WCHAR *copy16(WCHAR *to, const WCHAR *from) {
    WCHAR *original = to; while ((*to++ = *from++)) {} return original;
}
#define wcscpy copy16
struct CPCMemoryCard {
    int GetNumSaves(int, int *);
    int GetSaveName(int, int, WCHAR *);
};
#include "enumerator.inc"
#undef wcscpy
static void check(bool c, const char *m) { if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(2); } }
int main() {
    std::string utf31;
    for (int i = 0; i < 31; ++i) utf31 += "\xc3\xa9";
    entries = {{std::string(32,'Q') + ".bes", 0}, {"hidden.bes", _A_HIDDEN}, {"plain.bes", 0},
               {std::string(100,'Q') + ".bes", 0}, {utf31 + ".BES", 0}, {std::string(32,'Z') + ".bes", 0}};
    CPCMemoryCard card; int count = -1;
    check(card.GetNumSaves(0, &count) == MCE_SUCCESS && count == 2, "FirstSave and NextSave skip unsafe entries");
    WCHAR buffer[64]; for (auto &unit : buffer) unit = 0x5a5a;
    check(card.GetSaveName(0, 0, buffer) == MCE_SUCCESS && wcslen16(buffer) == 5 && buffer[32] == 0x5a5a,
          "first returned name is safe and in order");
    for (auto &unit : buffer) unit = 0x5a5a;
    check(card.GetSaveName(0, 1, buffer) == MCE_SUCCESS && wcslen16(buffer) == 31 && buffer[32] == 0x5a5a,
          "31-unit UTF-8 name reaches the existing callers safely");
    entries = {{std::string(32,'Q') + ".bes", 0}, {std::string(100,'Q') + ".bes", 0}};
    check(card.GetNumSaves(0, &count) == MCE_SUCCESS && count == 0 && card.GetSaveName(0, 0, buffer) != MCE_SUCCESS,
          "an all-unsafe enumeration is empty");
    std::puts("Linux save enumeration boundary passed");
}
'''


def keyboard_keys():
    """Every character key of the virtual keyboard (FEPVirtualKeyboard.cpp SetKeyboardKey), printable ones only."""
    text = (ROOT / "src/FEPVirtualKeyboard.cpp").read_text(encoding="latin1")
    keys = set()
    for value in re.findall(r"SetKeyboardKey\([^,]+,\s*('(?:\\.|[^'])'|0x[0-9a-fA-F]+|\d+)", text):
        code = ord(value[1]) if value.startswith("'") and len(value) == 3 else int(value, 0) if not value.startswith("'") else ord("\\")
        if code >= 0x20:
            keys.add(code)
    return sorted(keys)


class SaveCp1252Tests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/save-cp1252-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def compile_run(self, name, sources, expect):
        exe = self.out / name
        cmd = [os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter",
               *SANITIZE, "-I", str(self.out), *map(str, sources), "-o", str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        self.assertEqual(built.returncode, 0, built.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30, preexec_fn=no_core,
                             env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn(expect, ran.stdout)
        return ran

    def test_codec(self):
        (self.out / "codec_driver.cpp").write_text(CODEC_DRIVER, encoding="latin1")
        self.compile_run("codec", [self.out / "codec_driver.cpp", CODEC], "code page 1252 codec passed")

    def test_patched_card_and_cheat(self, mutate=None):
        card = patched(self.out, "PCMemoryCard.cpp")
        if mutate:
            self.assertIn(mutate[0], card)
            card = card.replace(mutate[0], mutate[1])
        start = card.index("#ifndef _WIN32\n// Port 0101")
        end = card.index("#endif", card.index("static char *PortSaveFile(")) + len("#endif")
        entry = function(card, "static inline BOOL PortSaveEntryOK(const char *filename)")
        entry = re.sub(r"#ifdef _WIN32.*?#else\n", "", entry, flags=re.DOTALL).replace("#endif\n", "")
        (self.out / "helpers.inc").write_text(card[start:end].replace("#ifndef _WIN32", "").replace("#endif", "") + "\n" + entry,
                                              encoding="latin1")
        um = (ROOT / "src/UMTexture.cpp").read_text(encoding="latin1")
        (self.out / "wchar.inc").write_text(
            "static char sFromWCHAR[4][0x1000];\nstatic WCHAR sToWCHAR[4][0x1000];\nstatic int sToWCHARNext, sFromWCHARNext;\n" +
            function(um, "WCHAR\t*ToWCHAR(char *text)") + "\n" + function(um, "char\t*FromWCHAR(WCHAR *text)") + "\n",
            encoding="latin1")
        save = (ROOT / "src/FEPSaveGame.cpp").read_text(encoding="latin1")
        table = save[save.index("char cheatname[6][256]"):save.index("extern char gAllCheatsEnabled")]
        (self.out / "cheats.inc").write_text(table, encoding="latin1")
        (self.out / "is_cheat.inc").write_text(function(save, "BOOL CFEPSaveGame::IsCheatActive(int cheatno)"), encoding="latin1")
        (self.out / "card_driver.cpp").write_text(CARD_DRIVER.replace("VK_KEYS", ", ".join(map(str, keyboard_keys()))), encoding="latin1")
        if mutate:
            return self.compile_fails("card-mutant", [self.out / "card_driver.cpp", CODEC], mutate[2])
        result = self.compile_run("card", [self.out / "card_driver.cpp", CODEC], "Linux career names passed")
        notice = "a career filename exceeds the supported name length; it was not listed or modified"
        self.assertEqual(result.stderr.count(notice), 1, result.stderr)
        self.assertNotIn("Q" * 32, result.stderr)  # The bounded diagnostic carries no player-supplied name.
        signatures = (
            "static inline BOOL NextSave()", "static inline BOOL FirstSave()",
            "int CPCMemoryCard::GetNumSaves(int card,int *num)",
            "int CPCMemoryCard::GetSaveName(int card,int save,WCHAR *name)")
        actual_functions = [function(card, name) for name in signatures]
        actual_enumerator = "\n".join(actual_functions)
        (self.out / "enumerator.inc").write_text(actual_enumerator, encoding="latin1")
        (self.out / "enum_driver.cpp").write_text(ENUM_DRIVER, encoding="latin1")
        enumeration = self.compile_run("enum", [self.out / "enum_driver.cpp", CODEC], "Linux save enumeration boundary passed")
        self.assertEqual(enumeration.stderr.count(notice), 1, enumeration.stderr)
        # Independently remove the admission call at each production enumerator site: both mutants must fail.
        guard = "!PortSaveEntryOK(sFindData.name)"
        for index, label in ((0, "next"), (1, "first")):
            self.assertEqual(actual_functions[index].count(guard), 1)
            mutant = list(actual_functions)
            mutant[index] = mutant[index].replace(guard, "FALSE")
            (self.out / f"enumerator-{label}.inc").write_text("\n".join(mutant), encoding="latin1")
            driver = ENUM_DRIVER.replace('"enumerator.inc"', f'"enumerator-{label}.inc"')
            (self.out / f"enum-{label}.cpp").write_text(driver, encoding="latin1")
            self.compile_fails(f"enum-{label}", [self.out / f"enum-{label}.cpp", CODEC],
                               "FirstSave and NextSave skip unsafe entries")
        # Every Linux path builder goes through PortSaveFile, and the listing and lookup through PortSaveWide.
        linux = card[card.index("int CPCMemoryCard::GetSaveName"):]
        self.assertNotIn("FromWCHAR(name)", linux)
        self.assertEqual(linux.count("PortSaveFile(name)"), 4)
        self.assertEqual(linux.count("PortSaveWide(buffer)"), 2)

    def compile_fails(self, name, sources, message):
        exe = self.out / name
        cmd = [os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter",
               *SANITIZE, "-I", str(self.out), *map(str, sources), "-o", str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        self.assertEqual(built.returncode, 0, built.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30, preexec_fn=no_core,
                             env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        self.assertNotEqual(ran.returncode, 0, "mutant survived: " + ran.stdout)
        self.assertIn(message, ran.stderr)

    def test_mutant_without_alias_guard_loses_a_career(self):
        # The guard removed: an existing old byte-for-byte file is chosen whatever career's UTF-8 file it is.
        self.test_patched_card_and_cheat(mutate=(
            "PortSaveLegacyFromNameCp1252(name, legacy, sizeof(legacy)) && PortSaveLegacyTwin(legacy, name)",
            "PortSaveLegacyFromNameCp1252(name, legacy, sizeof(legacy)) && PortSaveLegacyExists(legacy)",
            "a different career never resolves to another career's file"))

    def test_mutant_narrowing_old_name_loses_a_career(self):
        # The older one-byte name made with FromWCHAR again: the trademark sign narrows to a quote.
        self.test_patched_card_and_cheat(mutate=(
            "PortSaveLegacyFromNameCp1252(name, legacy, sizeof(legacy)) && PortSaveLegacyTwin(legacy, name)",
            "PortSaveLegacyTwin(FromWCHAR(name), name)",
            "FAIL: "))	# caught on the first name that narrowing breaks

    def test_mutant_one_byte_names_listed_byte_for_byte(self):
        # One-byte names listed byte for byte again: A\x99 lists as U+0099, so a typed A(TM) never finds its own save.
        self.test_patched_card_and_cheat(mutate=(
            "\tif (PortSaveNameFromLegacyCp1252(base, wide, 256))",
            "\tif (false && PortSaveNameFromLegacyCp1252(base, wide, 256))",
            "lists as A(TM)"))

    def test_mutant_without_name_limit_reaches_unsafe_caller(self):
        self.test_patched_card_and_cheat(mutate=(
            "if (units == 32)", "if (false && units == 32)", "32 ASCII units are refused"))

    def test_windows_source_unchanged(self):
        before = patched(self.out, "PCMemoryCard.cpp", upto="0066-save-name-utf8.patch")
        after = patched(self.out, "PCMemoryCard.cpp", upto="0101-save-name-cp1252-linux.patch")   # later patches are their own tests
        self.assertNotEqual(before, after)
        self.assertEqual(windows_view(before), windows_view(after), "patch 0101 changes no line a Windows build compiles")
        self.assertIn("PortSaveNameUtf16(sFindData.name, name, 32)", windows_view(after))
        self.assertNotIn("Cp1252", windows_view(after))
        build = (ROOT / "port/build.py").read_text()
        self.assertIn('for d in ("common", target)', build)	# platform/linux sources are Linux-only


if __name__ == "__main__":
    unittest.main(verbosity=2)
