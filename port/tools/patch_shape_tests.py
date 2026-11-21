#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every hunk of port/patches/*.patch applies at exactly one place, by what its lines say.

The patch program looks for a hunk's old lines (its unchanged and removed lines) near the line its header names and takes
the nearest place they match. It never looks above the place where the previous hunk of the same file went, and a patch
of which one hunk finds no place fails as a whole. A hunk whose old lines match two places it can reach is put right only
while nothing above it moves by more than half the distance between them; a hunk with no old lines at all is put at its
line number whatever is there (patch 0189 once placed two calls that way; patch_control_body_tests.py refuses that shape
from the patch text alone). Neither can fail, and the build's "no fuzz" test sees neither. This check walks the queue as
the build does (port/build.py build_mirror: the mirror's form of each file, the patches in name order) and, for every
hunk, counts the places its old lines match in the file as it stands before that patch, below the previous hunk:

- none: the patch does not apply;
- one: placed by content;
- several: a finding if the patch could also be put together at another of them: the hunk taken there, the rest of the
  patch's hunks for that file still find a place each (so a drift would give other text without a word). An other place
  after which a later hunk finds none is a drift that fails loudly, not a finding; identical hunks that change every
  place alike are the commonest case. A finding may be listed in KNOWN with the line it must land on and its reason; a
  listed hunk that lands elsewhere fails, and one that is no longer found fails as stale.

A hunk the patch program can only put at the head or the end of the file (old range 0,0; or fewer unchanged lines on
the side that touches the file's edge) is judged at that edge alone. The files the public build replaces
(build.PUBLIC_REPLACEMENTS) are walked in both forms when the tree holds both: the original with every patch, and the
replacement without the patches the public build skips.

The walk is this file's own (it must know every candidate place); it is compared with the patch program itself: every
file's text after the whole queue must equal what `patch --fuzz=0` produces from the same start, and the walk must have
put each hunk where the program does.

Controls: authored files and patches for each verdict and for a KNOWN entry, and patch 0189 with and without its context
lines against the real game.cpp with a few lines added above its hunks or between its two calls, as an earlier patch
could add them: with context its calls stay after the two state assignments; without, the patch program reports success
and puts them elsewhere.

    python3 -I port/tools/patch_shape_tests.py            # the queue, then the controls
    python3 -I port/tools/patch_shape_tests.py --list     # also print every hunk that is placed at an offset

A finding names the patch, the file, the hunk's line in the patch and line numbers; no source text is printed. No game
data, build or device is used (the dependency cache, when present, only settles the letter case of a few system header
names, as in the build).
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parent
PATCHES = PORT / "patches"
sys.path.insert(0, str(PORT))
sys.path.insert(0, str(PORT / "tools"))
import build  # noqa: E402
import patch_control_body_tests as shape  # noqa: E402

SEVERAL = "the hunk's old lines match {n} places it can reach (lines {where}); the patch also goes together at line {other}, and nothing but its line number chooses"
NOWHERE = "the hunk's old lines match nowhere in the file as the earlier patches leave it"
EDGE = "the hunk can only apply at the {edge} of the file and its old lines are not there"

# Findings kept for a reason, by (patch, file, the header's old first line): (the line of the file before that patch where the
# hunk must land, why it stays). None today. The three hunks once listed here (0058 and 0061 in PCMusicStream.cpp, 0097 in
# DropshipGuide.cpp) match a second place that the patch program cannot take: above the previous hunk of the same patch for
# 0058 and 0097, and for 0061 one after which the patch's next hunk finds no place, so a drift there fails the build.
KNOWN = {}


class Hunk:
    def __init__(self, header, line):
        m = shape.HEADER.match(header)
        self.old_start, self.old_count = int(m[1]), 1 if m[2] is None else int(m[2])
        self.line = line            # the header's line number in the patch
        self.body = []              # (mark, text)

    @property
    def old(self):
        return [text for mark, text in self.body if mark != "+"]

    @property
    def new(self):
        return [text for mark, text in self.body if mark != "-"]

    def context(self):
        """(unchanged lines before the first change, after the last change)."""
        marks = [mark for mark, _ in self.body]
        lead = next((i for i, mark in enumerate(marks) if mark != " "), len(marks))
        trail = next((i for i, mark in enumerate(reversed(marks)) if mark != " "), len(marks))
        return lead, trail


def sections(text):
    """[(file, [Hunk])] of one patch's text, in order."""
    out, hunk = [], None
    for number, line in enumerate(text.split("\n"), 1):
        if line.startswith("+++ "):
            out.append((line[4:].split("\t")[0].removeprefix("b/"), []))
            hunk = None
        elif line.startswith("--- "):
            hunk = None
        elif line.startswith("@@") and out and shape.HEADER.match(line):
            hunk = Hunk(line, number)
            out[-1][1].append(hunk)
        elif hunk is not None and line[:1] in (" ", "+", "-"):
            hunk.body.append((line[0], line[1:]))
    return out


def places(lines, old):
    """Every 0-based line at which the block OLD occurs in LINES."""
    if not old:
        return []
    first, n = old[0], len(old)
    return [i for i in range(len(lines) - n + 1) if lines[i] == first and lines[i:i + n] == old]


def walk_section(lines, hunks, cursor=0, last_offset=0, forced=None):
    """Apply one file's hunks to LINES (the file before this patch) as the patch program would, from line CURSOR on; FORCED
    puts the first hunk at that 0-based line instead of where the program would.
    Returns (new lines, findings, offsets): findings are (hunk, reason, known key or None, 0-based line it went to);
    offsets are (hunk, lines moved)."""
    findings, offsets = [], []
    out = lines[:cursor]            # cursor: the first line of LINES not yet copied (the program never looks above it)
    for index, hunk in enumerate(hunks):
        old, new = hunk.old, hunk.new
        lead, trail = hunk.context()
        guess = (hunk.old_start if not old else hunk.old_start - 1) + last_offset
        if not old:
            # No old lines: the program takes its guess. Only the head of the file is a place nothing can move
            # (patch_control_body_tests reports the others).
            at = min(max(guess, cursor), len(lines))
        else:
            found = places(lines, old)
            if lead < trail and hunk.old_start <= 1:
                found, edge = [p for p in found if p == 0], "head"
            elif trail < lead:
                found, edge = [p for p in found if p + len(old) == len(lines)], "end"
            else:
                edge = None
            usable = [p for p in found if p >= cursor]
            if not usable:
                findings.append((hunk, EDGE.format(edge=edge) if edge else NOWHERE, None, None))
                continue
            # The program tries its guess, then one line further each way, the later line first.
            at = min(usable, key=lambda p: (abs(p - guess), p < guess))
            if index == 0 and forced is not None:
                at = forced
            elif edge is None and len(usable) > 1:
                other = next((p for p in usable if p != at and goes_together(lines, hunks[index:], p)), None)
                if other is not None:
                    where = ", ".join(str(p + 1) for p in usable)
                    findings.append((hunk, SEVERAL.format(n=len(usable), where=where, other=other + 1), hunk.old_start, at))
            last_offset = at - (hunk.old_start - 1)
            if last_offset:
                offsets.append((hunk, last_offset))
        out += lines[cursor:at] + new
        cursor = at + len(old)
    return out + lines[cursor:], findings, offsets


def goes_together(lines, hunks, at):
    """Whether HUNKS (one file's, from a hunk with several places on) still all find a place when the first is put at AT."""
    first = hunks[0]
    _, findings, _ = walk_section(lines, hunks, cursor=0, last_offset=at - (first.old_start - 1), forced=at)
    return not any(reason == NOWHERE or reason.startswith("the hunk can only apply") for _, reason, _, _ in findings)


def split(text):
    """(lines, whether the text ends with a newline)."""
    lines = text.split("\n")
    return (lines[:-1], True) if lines[-1] == "" else (lines, False)


class Sources:
    """The mirror's form of src/NAME before any patch (build.normalise, as build_mirror writes it)."""

    def __init__(self):
        build.SYSTEM_NAMES.update(build.system_header_names())
        rows = [l.split(maxsplit=1) for l in (ROOT / "config/game-data.sha256").read_text().splitlines() if l.strip()]
        targets = ([build.MIRROR_ROOT / f.relative_to(ROOT) for f in build.tracked_sources()] + [build.MIRROR / r for _, r in rows] +
                   [build.MIRROR / "data/NVShaderFragments.inc"])
        targets += [build.MIRROR / name for name in build.PUBLIC_REPLACEMENTS]
        self.index = build.CaseIndex(targets)

    def text(self, path, name):
        raw = Path(path).read_bytes().decode("latin-1")
        return build.normalise(raw, (build.MIRROR / name).parent, self.index, [build.MIRROR, build.MIRROR / "MissionScript"])


def forms(name):
    """[(label, source path, patches this form skips)] for one file the patches name."""
    out = []
    original, replacement = ROOT / "src" / name, PORT / "platform/public" / name
    if name in build.PUBLIC_REPLACEMENTS and replacement.exists():
        if original.exists():
            out.append(("original", original, frozenset()))
        out.append(("public", replacement, frozenset(p for p, f in build.PUBLIC_SKIPPED_PATCHES.items() if f == name)))
    elif original.exists():
        out.append(("", original, frozenset()))
    return out


def by_program(start, name, queue):
    """START (text) with QUEUE's sections for NAME applied by the patch program at fuzz 0, as build.patched_text does;
    raises RuntimeError naming the patch that does not apply."""
    with tempfile.TemporaryDirectory(prefix="ce-patch-shape-") as tmp:
        target = Path(tmp) / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(start.encode("latin-1"))
        for patch_name, text in queue:
            parts = [part for part in re.split(r"(?=^--- a/)", text, flags=re.M) if part.startswith(f"--- a/{name}\n")]
            if not parts:
                continue
            done = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", tmp, "--no-backup-if-mismatch"],
                                  input="".join(parts).encode("latin-1"), capture_output=True)
            said = (done.stdout + done.stderr).decode("latin-1")
            if done.returncode or "fuzz" in said.lower():
                raise RuntimeError(f"{patch_name} does not apply to {name}")
        return target.read_bytes().decode("latin-1")


def walk(queue, start_of, names=None):
    """Walk QUEUE ([(patch name, text)] in order). start_of(name) gives [(label, start text, skipped patch names)].
    Returns (findings, offsets, {(name, label): final text}, hunk count): a finding is (patch, file and form, hunk line,
    reason, known key or None, 0-based line it went to or None)."""
    parsed = [(patch_name, sections(text)) for patch_name, text in queue]
    wanted = sorted({name for _, parts in parsed for name, _ in parts} if names is None else names)
    findings, offsets, texts, count = [], [], {}, 0
    for name in wanted:
        for label, start, skipped in start_of(name):
            lines, newline = split(start)
            shown = f"{name} ({label})" if label else name
            for patch_name, parts in parsed:
                if patch_name in skipped:
                    continue
                for file, hunks in parts:
                    if file != name:
                        continue
                    count += len(hunks)
                    lines, found, moved = walk_section(lines, hunks)
                    findings += [(patch_name, shown, hunk.line, reason, key and (patch_name, name, key), at) for hunk, reason, key, at in found]
                    offsets += [(patch_name, shown, hunk.line, by) for hunk, by in moved]
            texts[(name, label)] = "\n".join(lines) + ("\n" if newline else "")
    return findings, offsets, texts, count


def judge(findings, known):
    """The findings against KNOWN-style entries. Returns (lines to print, failures, the entries found)."""
    said, failures, seen = [], 0, set()
    for patch_name, shown, line, reason, key, at in findings:
        if key in known:
            seen.add(key)
            if at + 1 == known[key][0]:
                said.append(f"KNOWN {patch_name} {shown} patch line {line}: {reason} ({known[key][1]})")
                continue
            said.append(f"FAIL {patch_name} {shown} patch line {line}: a listed hunk lands at line {at + 1}, not at line {known[key][0]}")
        else:
            said.append(f"FAIL {patch_name} {shown} patch line {line}: {reason}")
        failures += 1
    for key in sorted(set(known) - seen):
        said.append(f"FAIL stale exception: {key[0]} {key[1]} at old line {key[2]} is no longer found; remove it from KNOWN")
        failures += 1
    return said, failures, seen


def check_queue(listing):
    """The real queue. Returns the number of failures."""
    failures = 0
    queue = [(p.name, p.read_text(encoding="latin-1")) for p in sorted(PATCHES.glob("*.patch"))]
    sources = Sources()
    starts = {}

    def start_of(name):
        if name not in starts:
            starts[name] = [(label, sources.text(path, name), skipped) for label, path, skipped in forms(name)]
        return starts[name]

    findings, offsets, texts, count = walk(queue, start_of)
    missing = sorted(name for name in starts if not starts[name])
    for name in missing:
        print(f"FAIL {name}: a patch names it and the tree does not hold it")
        failures += 1
    said, failed, seen = judge(findings, KNOWN)
    print("\n".join(said)) if said else None
    failures += failed
    if listing:
        for patch_name, shown, line, by in offsets:
            print(f"OFFSET {patch_name} {shown} patch line {line}: placed {by:+d} lines from its header")
    # The walk against the patch program: the same text for every file and form.
    for (name, label), text in sorted(texts.items()):
        skipped = next(s for l, _, s in starts[name] if l == label)
        start = next(t for l, t, _ in starts[name] if l == label)
        try:
            program = by_program(start, name, [(n, t) for n, t in queue if n not in skipped])
        except RuntimeError as error:
            print(f"FAIL {error}")
            failures += 1
            continue
        if program != text:
            print(f"FAIL {name}{' (' + label + ')' if label else ''}: the walk and the patch program give different text")
            failures += 1
    if len(queue) < 100 or count < 500:
        print(f"FAIL the queue looks empty ({len(queue)} patches, {count} hunks walked)")
        failures += 1
    elif not failures:
        print(f"PASS {len(queue)} patches, {count} hunks walked over {len(texts)} files and forms: each is placed by its lines at one "
              f"place ({len(seen)} listed exceptions, {len(offsets)} placed at an offset from their header), and the patch program gives the same text")
    return failures


# Authored controls (made-up code): a file, a patch for it, and the reasons expected.
FILE = "".join(f"line {i}\n" for i in range(1, 9)) + "\tbegin();\n\tdraw();\n\tend();\n" + "".join(
    f"line {i}\n" for i in range(12, 20)) + "\tbegin();\n\tdraw();\n\tend();\n" + "".join(f"line {i}\n" for i in range(23, 31))


def authored(old_start, new_start, body):
    old = sum(1 for line in body if line[0] != "+")
    new = sum(1 for line in body if line[0] != "-")
    return f"@@ -{old_start},{old} +{new_start},{new} @@\n" + "".join(line + "\n" for line in body)


CONTROLS = [
    ("old lines that match one place", [], authored(7, 7, [" line 7", " line 8", " \tbegin();", "+\tcount();", " \tdraw();", " \tend();", " line 12"])),
    ("old lines that match two places", ["SEVERAL"], authored(9, 9, [" \tbegin();", "+\tcount();", " \tdraw();", " \tend();"])),
    ("the same change made at both places by identical hunks", [],
     authored(9, 9, [" \tbegin();", "+\tcount();", " \tdraw();", " \tend();"]) +
     authored(20, 21, [" \tbegin();", "+\tcount();", " \tdraw();", " \tend();"])),
    ("two places changed differently by one patch (a drift of the first leaves the second no place: it fails loudly)", [],
     authored(9, 9, [" \tbegin();", "+\tcount();", " \tdraw();", " \tend();"]) +
     authored(20, 21, [" \tbegin();", "+\tcount(2);", " \tdraw();", " \tend();"])),
    ("old lines whose other match is above the previous hunk, out of the program's reach", [],
     authored(14, 14, [" line 14", "+// here", " line 15", " line 16"]) +
     authored(20, 21, [" \tbegin();", "+\tcount();", " \tdraw();", " \tend();"])),
    ("old lines that match two places, the second hunk with room after either", ["SEVERAL"],
     authored(9, 9, [" \tbegin();", "+\tcount();", " \tdraw();", " \tend();"]) +
     authored(26, 27, [" line 26", "+// here", " line 27", " line 28"])),
    ("old lines that match nowhere", ["NOWHERE"], authored(9, 9, [" \tbegin();", "+\tcount();", " \tdrawn();", " \tend();"])),
    ("an insertion at the head of the file", [], authored(0, 1, ["+#include \"made_up.h\""])),
    ("an insertion before the first lines, with context after it only", [], authored(1, 1, ["+#include \"made_up.h\"", " line 1", " line 2", " line 3"])),
    ("an addition at the end of the file, with context before it only", [], authored(28, 28, [" line 28", " line 29", " line 30", "+// the end"])),
    ("an addition that claims the end of the file and is not there", ["EDGE"], authored(26, 26, [" line 26", " line 27", " line 28", "+// the end"])),
    ("a removal", [], authored(14, 14, [" line 14", "-line 15", " line 16"])),
]


def check_controls():
    failures = 0
    # A KNOWN entry: kept where it lands on its line, refused when it lands elsewhere, stale when nothing is found.
    text = "--- a/made_up.cpp\n+++ b/made_up.cpp\n" + authored(9, 9, [" \tbegin();", "+\tcount();", " \tdraw();", " \tend();"])
    findings = walk([("authored.patch", text)], lambda name: [("", FILE, frozenset())])[0]
    key = ("authored.patch", "made_up.cpp", 9)
    for label, known, want in (("lands on its listed line", {key: (9, "a reason")}, 0), ("lands elsewhere", {key: (20, "a reason")}, 1),
                               ("is no longer found", {("authored.patch", "made_up.cpp", 1): (1, "a reason")}, 2)):
        said, failed, _ = judge(findings, known)
        ok = failed == want and (want != 1 or "lands at line 9, not at line 20" in said[0])
        failures += not ok
        print(("PASS " if ok else "FAIL ") + f"control: a listed hunk that {label} ({failed} failures; expected {want})")
    for label, expected, hunk_text in CONTROLS:
        text = "--- a/made_up.cpp\n+++ b/made_up.cpp\n" + hunk_text
        findings, _, texts, _ = walk([("authored.patch", text)], lambda name: [("", FILE, frozenset())])
        got = ["SEVERAL" if "places" in reason else "NOWHERE" if reason == NOWHERE else "EDGE" for _, _, _, reason, _, _ in findings]
        ok = got == expected
        if ok and not expected:
            # A patch with no finding must also be the patch program's result.
            ok = by_program(FILE, "made_up.cpp", [("authored.patch", text)]) == texts[("made_up.cpp", "")]
        elif ok and expected == ["SEVERAL"]:
            # The program accepts it all the same, silently (by_program raises on a refusal or fuzz): that is why the walk counts.
            ok = by_program(FILE, "made_up.cpp", [("authored.patch", text)]) != FILE
        failures += not ok
        print(("PASS " if ok else "FAIL ") + f"control: {label} ({', '.join(got) or 'no finding'}; expected {', '.join(expected) or 'none'})")
    return failures


def check_0189():
    """Patch 0189 against game.cpp with lines added above its hunks, with and without its context. Returns failures."""
    name, file = "0189-network-campaign-outcomes.patch", "game.cpp"
    queue = [(p.name, p.read_text(encoding="latin-1")) for p in sorted(PATCHES.glob("*.patch"))]
    below = [(n, t) for n, t in queue if n < name]
    own = dict(queue)[name]
    sources = Sources()
    start = sources.text(ROOT / "src" / file, file)
    before = by_program(start, file, below)
    failures = 0

    def calls(text):
        """For each added call of the patch, the nearest code line above it (the statement it follows)."""
        lines = text.split("\n")
        out = []
        for i, line in enumerate(lines):
            if "CE_LockstepCampaignOutcome(" in line:
                k = i - 1
                while k >= 0 and (not lines[k].strip() or lines[k].lstrip().startswith("#")):
                    k -= 1
                out.append(lines[k].strip())
        return out

    def state(result, word):
        return len(result) == 2 and all(re.fullmatch(r"\w+\s*=\s*GAME_STATE_LEVEL_" + w + r"\s*;", line) for w, line in zip(word, result))

    plain = calls(by_program(before, file, [(name, own)]))
    ok = state(plain, ("WON", "LOST"))
    failures += not ok
    print(("PASS " if ok else "FAIL ") + "control: patch 0189 puts each campaign outcome call straight after its state assignment")
    # An earlier patch adds lines above both call hunks (after the head of the file, which 0189's first hunk inserts at), or
    # between them. The patch program carries one hunk's offset to the next, so only lines added between the two hunks show
    # that the second is placed by its own lines.
    lines = before.split("\n")
    # Where the two call hunks go, by the old lines of the tracked patch (the patch under test may have none).
    hunks = [h for f, hs in sections((PORT / "patches" / name).read_text(encoding="latin-1")) if f == file for h in hs if h.old]
    found = [places(lines, h.old) for h in hunks]
    if len(found) != 2 or any(len(f) != 1 for f in found) or found[1][0] - (found[0][0] + len(hunks[0].old)) < 10:
        print(f"FAIL control: the two call hunks of patch 0189 must each match one place in game.cpp, well apart ({found})")
        return failures + 1
    between = (found[0][0] + len(hunks[0].old) + found[1][0]) // 2
    for extra, at, where in ((1, 20, "above"), (4, 20, "above"), (40, 20, "above"), (4, between, "between the two calls"), (40, between, "between the two calls")):
        moved = "\n".join(lines[:at] + ["// a line an earlier patch adds"] * extra + lines[at:])
        try:
            with_context = calls(by_program(moved, file, [(name, own)]))
        except RuntimeError:
            with_context = None
        ok = with_context is not None and state(with_context, ("WON", "LOST"))
        failures += not ok
        print(("PASS " if ok else "FAIL ") + f"control: with {extra} lines added {where}, patch 0189 still puts both calls after their state assignments")
        # The same patch without its context lines (as it once was): the program reports success and misplaces both.
        mutant = shape.strip_context(own)
        try:
            without = calls(by_program(moved, file, [(name, mutant)]))
        except RuntimeError:
            without = None
        ok = without is not None and len(without) == 2 and not state(without, ("WON", "LOST"))
        failures += not ok
        print(("PASS " if ok else "FAIL ") + f"control: with {extra} lines added {where}, patch 0189 without its context lines applies without "
              "complaint and puts the calls elsewhere")
    return failures


def main():
    failures = check_queue("--list" in sys.argv)
    failures += check_controls()
    failures += check_0189()
    print("patch shape check: " + ("OK" if not failures else f"{failures} FAILED"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
