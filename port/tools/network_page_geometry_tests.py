#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Where the network pages put their lists and words: inside the game's 640x480 menu screen, with mutants.

    python3 port/tools/network_page_geometry_tests.py

port/game/ce_netpages.cpp needs the game's menu classes, so this test reads its text. Every CMenuOptionsList is
placed in the virtual 640x480 screen the front end draws in (CE Settings and the Network pages use x 320 and a row
centre between y 104 and 476). A list placed in 0-to-1 screen fractions is drawn in the top-left pixel and its page
looks empty, which is how the host's address page stayed blank while the menu suites passed. The words a page draws
above its list are placed from ListTop() in pixels, so a fractional offset there is the same mistake.
The pages also call none of the C library's wide-string functions: the game's WCHAR is 2 bytes, theirs 4.
No game, window, network or player data.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGES = ROOT / "port/game/ce_netpages.cpp"

LIST = re.compile(r"CMenuOptionsList\(\s*\w+\s*,\s*([0-9.]+)f\s*,\s*([0-9.]+)f\s*,")
# ListTop(...) - ... - <offset>f : the words above a list end this many pixels above it
LIST_TOP_OFFSET = re.compile(r"ListTop\([^;]*?-\s*([0-9]*\.[0-9]+|[0-9]+\.)f\s*;")

X_RANGE = (110.0, 530.0)    # the safe band of wrapped text and centred lists (house style L7)
Y_RANGE = (104.0, 476.0)    # below the title bar, above the bottom edge (L2, L6)


def check(text):
    bad = []
    lists = LIST.findall(text)
    if len(lists) < 5:
        bad.append("found only %d menu lists in the pages: the pattern no longer matches the source" % len(lists))
    for x, y in lists:
        fx, fy = float(x), float(y)
        if not (X_RANGE[0] <= fx <= X_RANGE[1]) or not (Y_RANGE[0] <= fy <= Y_RANGE[1]):
            bad.append("a list is placed at (%s, %s), outside the 640x480 menu screen" % (x, y))
    for off in LIST_TOP_OFFSET.findall(text):
        if float(off) < 1.0:
            bad.append("words above a list end %s px above it: a screen fraction, not pixels" % off)
    # The game's WCHAR is 2 bytes (build.py: -fshort-wchar) but the C library's wide-string functions read 4-byte units, so
    # a wcscat or wcslen on a page's text runs past its buffer (a wcscat on a row label corrupted the heap on opening).
    code = re.sub(r"//[^\n]*", "", text)
    for name in sorted(set(re.findall(r"\b(wcs[a-z]+|wmem[a-z]+|swprintf)\s*\(", code))):
        bad.append("the pages call %s: the C library's wide-string functions do not fit the game's 2-byte WCHAR" % name)
    return bad


MUTANTS = [
    ("host-list-fraction", "mList[NP_HOST_ADDRESS] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 380.f,",
     "mList[NP_HOST_ADDRESS] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, .5f, .72f,"),
    ("words-offset-fraction", "const float top = ListTop(mList[NP_MENU]) - 6.f;", "const float top = ListTop(mList[NP_MENU]) - .04f;"),
    ("wide-string-call", "mLabel8 = label; Widen(mLabelWide, 64, text);", "mLabel8 = label; Widen(mLabelWide, 60, text); wcscat(mLabelWide, L\":\");"),
    ("lobby-off-screen", "mList[NP_LOBBY] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 320.f,",
     "mList[NP_LOBBY] = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 520.f,"),
]


def main():
    text = PAGES.read_text(encoding="utf-8")
    results = []

    def report(ok, label):
        print(("PASS " if ok else "FAIL ") + label, flush=True)
        results.append(bool(ok))

    failures = check(text)
    report(not failures, "every network list and its words are placed in the 640x480 menu screen")
    for line in failures:
        print("   FAIL " + line)
    for name, needle, replacement in MUTANTS:
        if text.count(needle) != 1:
            report(False, "mutant %s: its source text is not found exactly once" % name)
            continue
        report(bool(check(text.replace(needle, replacement, 1))), "mutant %s is caught" % name)
    ok = all(results)
    print("page geometry: " + ("OK" if ok else "FAILED"), "(%d checks)" % len(results))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
