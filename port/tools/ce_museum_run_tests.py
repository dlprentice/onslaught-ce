#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The museum's offscreen route test: a visit enters, pauses, resumes, leaves and every profile file is compared; each refusal comes back to the page.

Two parts:
  python3 port/tools/ce_museum_run_tests.py            host checks of the judges on authored data (a registered suite; no game runs)
  python3 port/tools/ce_museum_run_tests.py --run DIR  judge a finished run: DIR is a run folder (local-data/port/runs/NAME) and --start the
                                                      private start file the run began from; prints one line per check, exits 1 on a failure

A run is made with the lane's capture script (port/tools/run.py with --start, --fake-keys and BEA_PORT_QUIT); this file judges its result, so the
route can be repeated and the judge itself is tested with authored logs and profiles, never a retail file. The careers, the card id and ce.ini of the
profile must be byte-equal after a visit, and the setup history may appear. A run that ended on the fatal "Error loading level" fails whatever else it did.

The options file (defaultoptions.bea) must be byte-equal to its reference: the start file's copy, or with --options-reference the file a control run
of the same action wrote from the same career (the pause menu's Continue writes it in every level from the live career). The control must hold the
career the visit started from: a -level start and the main menu do not (a -level run writes a slot bit the main menu's career has clear), so for a
visit entered from the main menu the main menu's own Options > Back is the control. With --options-changed the run changes a setting on purpose: then only the options fields may differ from the reference (OPTION_BYTES and
everything after the career record), at least one of them must, and the career record (progress, slots, kills) must still equal the reference's.
"""
import argparse
import base64
import json
import re
import sys
import tempfile
from pathlib import Path

# What a visit may change in the writable folder; every other file in the start file must be byte-equal and present.
MAY_CHANGE = set()
MAY_APPEAR = {"setuphistory.txt"}
OPTIONS = "defaultoptions.bea"
# The options fields of the options file (the same layout as a career file: a version word, then CCareer, then the key bindings and the PC options).
# File offsets: the screen position in the top bytes of the first two kill counters (0x23f6 and 0x23fa, little-endian), then the volumes, the god
# flags, the four invert-Y flags, vibration and the controller presets (0x248e..0x24bd). Everything from CAREER_END on is the bindings and PC options.
# Every other byte up to CAREER_END is the career record: progress, goodies, kills, slots, the in-progress flag.
OPTION_BYTES = frozenset([0x23f9, 0x23fd]) | frozenset(range(0x248e, 0x24be))
CAREER_END = 0x24be
# A hall of machines (M2): at least this many, standing on ground whose height varies by no more than this many units under the whole site.
MIN_MACHINES = 3
MAX_HEIGHT_RANGE = 4.0


def start_files(path):
    """{relative name: bytes} of a run's start file (the launcher's TAPE.json shape: start_files, base64 values)."""
    data = json.loads(Path(path).read_text())
    return {name: base64.b64decode(blob) for name, blob in data["start_files"].items()}


def tree(folder):
    """{relative name: bytes} of every file under a run's user folder."""
    folder = Path(folder)
    return {str(p.relative_to(folder)): p.read_bytes() for p in sorted(folder.rglob("*")) if p.is_file()}


def options_problems(start, after, reference=None, changed=False):
    """The problems with the options file after a run. reference: the bytes an ordinary level's same action wrote (None: the start's file).
    changed: the run declared an options change, so only option fields may differ from the reference, and one must."""
    expected = start if reference is None else reference
    what = "the start" if reference is None else "the control run"
    if after is None:
        return [] if expected is None else ["missing after the visit: " + OPTIONS]
    if expected is None:
        return ["new file: " + OPTIONS + " (no reference to compare it with)"]
    if not changed:
        return [] if after == expected else [OPTIONS + " differs from " + what + " at " + offsets(expected, after)]
    if len(after) != len(expected) or len(after) < CAREER_END:
        return [OPTIONS + " changed length or has no career record, so its options fields cannot be told apart"]
    career = [i for i in range(CAREER_END) if after[i] != expected[i] and i not in OPTION_BYTES]
    option = [i for i in range(len(after)) if after[i] != expected[i] and (i in OPTION_BYTES or i >= CAREER_END)]
    problems = []
    if career:
        problems.append(OPTIONS + " career record differs from " + what + " at " + ", ".join(hex(i) for i in career[:8]))
    if not option:
        problems.append(OPTIONS + ": the run declared an options change but no options field differs from " + what)
    return problems


def offsets(a, b):
    """The first differing offsets of two byte strings, for a problem line."""
    found = [i for i in range(max(len(a), len(b))) if i >= len(a) or i >= len(b) or a[i] != b[i]]
    return ", ".join(hex(i) for i in found[:8]) + (" and %d more" % (len(found) - 8) if len(found) > 8 else "")


def compare(before, after, reference=None, changed=False):
    """The problems with a profile after a visit: [] when every career, the card id and ce.ini are as they were and the options file is as its
    reference (options_problems)."""
    problems = options_problems(before.get(OPTIONS), after.get(OPTIONS), reference, changed)
    before = {name: blob for name, blob in before.items() if name != OPTIONS}
    after = {name: blob for name, blob in after.items() if name != OPTIONS}
    for name, blob in before.items():
        if name not in after:
            problems.append("missing after the visit: " + name)
        elif after[name] != blob and name not in MAY_CHANGE:
            problems.append("changed by the visit: " + name)
    for name in after:
        if name not in before and name not in MAY_APPEAR and not name.startswith(("logs/", "museum/")):
            problems.append("new file: " + name)
    return problems


def presents(log):
    """The Present number of the last session line of a log (the run's own clock), or -1."""
    found = re.findall(r"session pid=\d+ t_ms=\d+ P=(\d+) ", log)
    return int(found[-1]) if found else -1


FATAL = re.compile(r"error loading level|fatal", re.I)
VISIT_START = re.compile(r"^.*?game: museum units:", re.M)


def visits(log):
    """The log cut into one block per visit: a visit's hall is generated once, and its census line opens its block (a Retry reads the same hall and
    reports nothing new). A log with no visit gives []."""
    starts = [m.start() for m in VISIT_START.finditer(log)]
    return [log[a:b] for a, b in zip(starts, starts[1:] + [len(log)])]


def hall_problems(block):
    """What is wrong with the hall one visit reported: [] when it is a hall of distinct machines on level ground, every one placed and in view."""
    problems = []
    chosen = re.search(r"museum exhibits: \d+ definitions, (\d+) chosen \((\d+) machines\)", block)
    site = re.search(r"museum site: (\d+) of (\d+) exhibits placed, (\d+) in view of the visitor, dry ring (\d+), ground height range ([0-9.]+)", block)
    if not chosen or int(chosen.group(2)) < MIN_MACHINES:
        problems.append("the hall has fewer than %d machines" % MIN_MACHINES)
    if not site:
        problems.append("the hall's site was not reported")
    else:
        placed, offered, seen, _, spread = (float(g) for g in site.groups())
        if placed != offered:
            problems.append("not every exhibit was placed")
        if seen != placed:
            problems.append("not every exhibit is in view of the visitor")
        if spread > MAX_HEIGHT_RANGE:
            problems.append("the ground under the hall is not level (range %.1f, at most %.1f)" % (spread, MAX_HEIGHT_RANGE))
    names = re.findall(r"museum exhibit \d+: ([^(]+) \(([^)]+)\),", block)
    if len({name.strip() for name, _ in names}) != len(names) or len(names) < MIN_MACHINES:
        problems.append("the exhibits are not distinct, named machines")
    return problems


def same_every_visit(blocks):
    """A visit that ends must leave the game's definition list as it found it (ce_museum_visit_engine.cpp DropExhibitDefinitions): the next visit then finds the
    same number of definitions and builds the same hall. One that leaves its own definitions behind shows up as more definitions, and other machines, the second
    time."""
    problems = []
    counts = [re.search(r"museum exhibits: (\d+) definitions", b) for b in blocks]
    names = [tuple(n.strip() for n, _ in re.findall(r"museum exhibit \d+: ([^(]+) \(([^)]+)\),", b)) for b in blocks]
    if len({m.group(1) for m in counts if m}) > 1:
        problems.append("a later visit found a different number of definitions: the first visit's exhibits were left in the game's list")
    if len(set(names)) > 1:
        problems.append("a later visit built a different hall from the same game files")
    return problems


# Where a visit says it placed its machines: the generated hall's "museum site:" line, or the authored world's "museum world: NAME; N installed
# exhibits" line (ce_museum_visit_engine.cpp logs the second; it prints "museum site:" only when it cannot place them).
PLACED = re.compile(r"museum site: \d+ of \d+ exhibits placed|museum world: [^;\n]+; [1-9]\d* installed exhibits")


def placed(log):
    return bool(PLACED.search(log))


def judge_log(log, scenario):
    """The problems with a session log for a scenario: 'visit' (a full visit and a leave), 'hall' (a visit that built its hall, once or more) or 'refused' (a
    load the museum refused)."""
    problems = []
    if FATAL.search(log):
        problems.append("the run ended on a fatal error")
    upfront = scenario == "refused" and "museum refused before loading" in log
    if upfront:
        # a refusal that comes before the load (a damaged level archive, another mode open): the player's log says why, no load starts and no level is played
        if re.search(r"game: load level=\d+ begin", log) or re.search(r"game: level=\d+ phase=3", log):
            problems.append("a refused visit started loading or reached play anyway")
        return problems
    if not re.search(r"game: load level=\d+ begin", log):
        problems.append("the visit never started loading")
    if scenario == "midquit":
        # the window closed in the middle of a visit: nothing ended the visit but the process, so the run is judged on the load, the hall it placed and the
        # profile (a career file the visit touched is the failure this scenario exists for)
        if not placed(log):
            problems.append("the visit never placed its exhibits")
        if not re.search(r"game: post-load level=\d+ ready", log):
            problems.append("the visit never reached play")
        return problems
    leaves = [(int(p), int(q)) for p, q in re.findall(r"P=(\d+) game: leave level=\d+ phase=\d+ quit=(\d+)", log)]
    if not leaves:
        problems.append("the level never ended")
    elif presents(log) < leaves[-1][0] + 60:
        problems.append("the game did not go on after the level ended")
    if scenario in ("sea", "edge"):
        # a visitor that reached the sea's reach (sea) or flew past the hall's edge (edge) was put back at the start, and the visit still ended as the player chose
        # (not as a lost level)
        reason = "the sea's reach" if scenario == "sea" else "the hall's edge"
        if not re.search(r"game: museum visitor put back at the start \(%s\)" % reason, log):
            problems.append("the visitor was never put back at the start (%s)" % reason)
    if scenario in ("visit", "hall", "sea", "edge"):
        if not placed(log):
            problems.append("the visit never placed its exhibits")
        if not any(q in (1, 6) for _, q in leaves):
            problems.append("the visit did not end as a quit to the front end")
    if scenario == "hall":
        # every visit in the log built a hall of machines on level ground, and every later visit the same hall as the first
        blocks = visits(log)
        if not blocks:
            problems.append("the hall's site was not reported")
        for number, block in enumerate(blocks, 1):
            problems += [("visit %d: " % number if len(blocks) > 1 else "") + why for why in hall_problems(block)]
        problems += same_every_visit(blocks)
    if scenario == "refused":
        # the level never reached play, and it ended as a load error that the game went on from
        if re.search(r"game: level=\d+ phase=3", log):
            problems.append("a refused visit reached play anyway")
        if not any(q == 3 for _, q in leaves):
            problems.append("the refused load did not end as a load error")
    return problems


def judge_run(run, start, scenario, reference=None, changed=False):
    """reference: a level's run folder whose options file the visit's must equal (None: the start's)."""
    log = (Path(run) / "stderr.log").read_text(errors="replace")
    problems = judge_log(log, scenario)
    wanted = None
    if reference is not None:
        source = Path(reference) / "user" / OPTIONS
        if not source.is_file():
            return problems + ["the control run has no " + OPTIONS + " to compare with: " + str(source)]
        wanted = source.read_bytes()
    problems += compare(start_files(start), tree(Path(run) / "user"), wanted, changed)
    return problems


# ---------------------------------------------------------------- authored checks of the judges

def self_test():
    failures = []

    def check(name, condition):
        print(("ok   " if condition else "FAIL ") + name)
        if not condition:
            failures.append(name)

    careers = {"savegames/one.bes": b"career one", "savegames/two.bes": b"career two", "cardid.txt": b"id", "ce.ini": b"[Experimental]\nmuseum=1\n",
               "defaultoptions.bea": b"options"}
    check("an untouched profile passes", compare(careers, dict(careers)) == [])
    check("the setup history may appear", compare(careers, dict(careers, **{"setuphistory.txt": b"x"})) == [])
    check("a changed options file fails with no declared change", compare(careers, dict(careers, **{OPTIONS: b"options changed"})) != [])
    gone = dict(careers)
    del gone[OPTIONS]
    check("a missing options file fails", compare(careers, gone) != [])
    check("an options file that appears with no reference fails", compare(gone, careers) != [])
    # an options file shaped as the game writes it: a version word, the career record, the bindings and PC options
    start = bytes(range(256)) * 40
    level = bytearray(start)
    level[0x2] ^= 1
    level[0x26e6] ^= 0x80                       # a level's Continue rewrites the in-memory career and the display mode
    level = bytes(level)
    visit = bytearray(level)
    visit[0x2411] = 0                           # a career record other than the reference (here a slot byte) written into the options file
    visit = bytes(visit)
    profile = dict(careers, **{OPTIONS: start})
    check("an options file equal to the level's run passes with that reference", compare(profile, dict(careers, **{OPTIONS: level}), level) == [])
    check("an options file equal to the level's run fails against the start", compare(profile, dict(careers, **{OPTIONS: level})) != [])
    check("a career record in the options file other than the reference's (a slot byte) fails",
          compare(profile, dict(careers, **{OPTIONS: visit}), level) != [])
    check("... and fails even when an options change is declared", compare(profile, dict(careers, **{OPTIONS: visit}), level, True) != [])
    for at, name in ((0x248e, "a volume"), (0x24a6, "an invert flag"), (0x24b6, "a controller preset"), (0x23f9, "a screen position byte"),
                     (0x24be, "a key binding"), (0x26e8, "a PC option")):
        option = bytearray(level)
        option[at] ^= 0x40
        check(name + " that changed passes when an options change is declared",
              compare(profile, dict(careers, **{OPTIONS: bytes(option)}), level, True) == [])
        check(name + " that changed fails when no change is declared", compare(profile, dict(careers, **{OPTIONS: bytes(option)}), level) != [])
    for at, name in ((0x0002, "the pending goodies"), (0x1f46, "a goodie"), (0x23f6, "a kill count (low byte)"), (0x2411, "a slot"),
                     (0x248a, "the in-progress flag"), (0x0, "the version word")):
        career = bytearray(level)
        career[at] ^= 0x40
        career[0x248e] ^= 0x40                  # with a real options change beside it
        check(name + " that changed fails even with a declared options change",
              compare(profile, dict(careers, **{OPTIONS: bytes(career)}), level, True) != [])
    check("a declared options change that changed nothing fails", compare(profile, dict(careers, **{OPTIONS: level}), level, True) != [])
    check("a declared options change in a file of another length fails", compare(profile, dict(careers, **{OPTIONS: level + b"x"}), level, True) != [])
    for name in ("savegames/one.bes", "cardid.txt", "ce.ini"):
        check("a changed " + name + " fails", compare(careers, dict(careers, **{name: b"other"})) != [])
        gone = dict(careers)
        del gone[name]
        check("a missing " + name + " fails", compare(careers, gone) != [])
    check("a new career file fails", compare(careers, dict(careers, **{"savegames/three.bes": b"new"})) != [])
    check("a logs and museum folder file does not fail", compare(careers, dict(careers, **{"logs/a.txt": b"x", "museum/world.body": b"y"})) == [])

    good = ("P=1250 game: frontend page=21 requested=0\nP=1255 game: load level=100 begin\nP=1259 game: museum site: 3 of 4 exhibits placed\n"
            "P=1261 game: post-load level=100 ready players=1\nP=3269 game: leave level=100 phase=9 quit=6\nP=3700 music: play requested\n")
    pre = "onslaught-ce: session pid=1 t_ms=5 "
    good = "".join(pre + line + "\n" for line in good.strip().split("\n"))
    check("a complete visit log passes", judge_log(good, "visit") == [])
    check("a fatal load error fails", judge_log(good + "Error loading level - data files are corrupt\n", "visit") != [])
    check("a visit that never loads fails", judge_log(pre + "P=1250 game: frontend page=21 requested=0\n", "visit") != [])
    check("a level that never ends fails", judge_log(good.replace("leave level", "stay level"), "visit") != [])
    check("a game that stops at the leave fails", judge_log(good.replace("P=3700 music", "P=3300 music"), "visit") != [])
    check("a visit that ends as a load error fails", judge_log(good.replace("quit=6", "quit=3"), "visit") != [])
    middle = pre + "P=1255 game: load level=100 begin\n" + pre + "P=1259 game: museum site: 3 of 3 exhibits placed\n" + pre + "P=1261 game: post-load level=100 ready players=1\n"
    check("a visit cut off by closing the window passes when it reached play", judge_log(middle, "midquit") == [])
    check("a cut-off visit that never reached play fails", judge_log(middle.replace("post-load", "stay"), "midquit") != [])
    check("a cut-off visit that ended on a fatal error fails", judge_log(middle + "Error loading level\n", "midquit") != [])
    sea = good.replace("P=1261 game: post-load", "P=1260 game: museum visitor put back at the start (the sea's reach), from 1.0,2.0 at height -3.0 over water -2.0\n" + pre + "P=1261 game: post-load")
    check("a visit in which the visitor was put back passes the sea scenario", judge_log(sea, "sea") == [])
    check("a visit in which the visitor was never put back fails the sea scenario", judge_log(good, "sea") != [])
    check("a sea visit that ended as a lost level fails", judge_log(sea.replace("quit=6", "quit=3"), "sea") != [])
    edge = good.replace("P=1261 game: post-load", "P=1260 game: museum visitor put back at the start (the hall's edge), from 1.0,2.0 at height -3.0 over water -2.0\n" + pre + "P=1261 game: post-load")
    check("a visit in which the visitor flew past the hall's edge and was put back passes the edge scenario", judge_log(edge, "edge") == [])
    check("a visit with no put-back fails the edge scenario", judge_log(good, "edge") != [])
    check("a sea put-back does not pass for the edge scenario, nor the edge for the sea", judge_log(sea, "edge") != [] and judge_log(edge, "sea") != [])
    # a refusal that comes before the load (the player's log names it, nothing loads, nothing is played)
    upfront = pre + "P=1250 game: frontend page=21 requested=0\n" + pre + "P=1251 error game: museum refused before loading: The museum could not read your game files. Check that the game is installed, then try again.\n"
    check("an up-front refusal with its log line passes the refused scenario", judge_log(upfront, "refused") == [])
    check("an up-front refusal that started loading anyway fails", judge_log(upfront + pre + "P=1255 game: load level=100 begin\n", "refused") != [])
    check("a run with no load and no refusal line fails the visit scenario", judge_log(pre + "P=1250 game: frontend page=21 requested=0\n", "visit") != [])
    check("a visit with no exhibits fails", judge_log(good.replace("museum site:", "museum nothing:"), "visit") != [])
    refused = ("P=1255 game: load level=100 begin\nP=1256 game: load level=100 failed in WORLD.Load\nP=1257 game: leave level=100 phase=1 quit=3\n"
               "P=1900 music: play requested\n")
    refused = "".join(pre + line + "\n" for line in refused.strip().split("\n"))
    check("a refused visit that goes on passes", judge_log(refused, "refused") == [])
    check("a refused visit that played anyway fails", judge_log(good.replace("quit=6", "quit=3") + pre + "P=1300 game: level=100 phase=3 paused=0\n", "refused") != [])
    check("a refused visit that ended as a chosen exit fails", judge_log(refused.replace("quit=3", "quit=6"), "refused") != [])
    check("the refusal that fails after the read passes", judge_log(refused.replace("failed in WORLD.Load", "ready players=1"), "refused") == [])
    check("the last Present is read", presents(good) == 3700 and presents("") == -1)

    hall = good.replace("game: museum site: 3 of 4 exhibits placed",
                        "game: museum units: 160 definitions, 19 with a loaded model\n" + pre +
                        "P=1259 game: museum exhibits: 160 definitions, 3 chosen (3 machines); turned away: 0\n" + pre +
                        "P=1259 game: museum site: 3 of 3 exhibits placed, 3 in view of the visitor, dry ring 24, ground height range 2.5\n" + pre +
                        "P=1259 game: museum exhibit 1: Forseti Tank (Ground vehicle), footprint 1.0 by 1.0\n" + pre +
                        "P=1259 game: museum exhibit 2: Muspell Fighter (Fighter aircraft), footprint 1.0 by 1.0\n" + pre +
                        "P=1259 game: museum exhibit 3: Walker (Walking machine), footprint 1.0 by 1.0")
    check("a hall of three machines on level ground passes", judge_log(hall, "hall") == [])
    check("a hall on a hillside fails", judge_log(hall.replace("range 2.5", "range 16.3"), "hall") != [])
    check("a hall with a machine out of view fails", judge_log(hall.replace("3 in view", "2 in view"), "hall") != [])
    check("a hall that lost an exhibit fails", judge_log(hall.replace("3 of 3 exhibits placed", "2 of 3 exhibits placed"), "hall") != [])
    check("a hall of two machines fails", judge_log(hall.replace("3 machines", "2 machines"), "hall") != [])
    check("a damaged archive's fatal error text fails a run", judge_log(good + pre + "message ~File not found: data\\Resources\\100_res_PC.aya~Fatal error  sorry~\n", "visit") != [])
    # two visits in one log: each is judged on its own block, and the second must find the game's list as the first did
    first = hall.index(pre + "P=1255 game: load level=100 begin")
    cut = hall.index(pre + "P=3269 game: leave")
    visit, leave = hall[first:cut], hall[cut:]
    twice = hall[:cut] + visit.replace("P=12", "P=42") + leave
    check("two visits of the same hall in one log pass", judge_log(twice, "hall") == [])
    check("two visits are judged one block each, not by the names across the whole log", len(visits(twice)) == 2)

    def second_visit(old, new):
        """twice with one thing changed in the second visit only."""
        at = twice.rindex(old)
        return twice[:at] + new + twice[at + len(old):]

    check("a second visit that finds more definitions fails (the first visit's exhibits were left in the list)",
          judge_log(second_visit("museum exhibits: 160 definitions", "museum exhibits: 166 definitions"), "hall") != [])
    check("a second visit that builds other machines fails", judge_log(second_visit("Forseti Tank", "Sabre Tank"), "hall") != [])
    check("a second visit that lost a machine fails", judge_log(second_visit("3 machines", "2 machines"), "hall") != [])
    check("a hall of one name three times fails", judge_log(hall.replace("Muspell Fighter", "Forseti Tank").replace("Walker (", "Forseti Tank ("), "hall") != [])

    with tempfile.TemporaryDirectory() as folder:
        start = Path(folder) / "start.json"
        start.write_text(json.dumps({"start_files": {k: base64.b64encode(v).decode() for k, v in careers.items()}}))
        check("a start file decodes", start_files(start) == careers)
        run = Path(folder) / "run"
        (run / "user/savegames").mkdir(parents=True)
        for name, blob in careers.items():
            (run / "user" / name).write_bytes(blob)
        (run / "stderr.log").write_text(good)
        check("a run folder is judged whole", judge_run(run, start, "visit") == [])
        reference = Path(folder) / "level"
        (reference / "user").mkdir(parents=True)
        (reference / "user" / OPTIONS).write_bytes(b"options as a level saved them")
        check("a run folder is judged against a level's run when given one", judge_run(run, start, "visit", reference) != [])
        (run / "user" / OPTIONS).write_bytes(b"options as a level saved them")
        check("... and passes when its options file equals the level's", judge_run(run, start, "visit", reference) == [])
        check("... and fails against the start", judge_run(run, start, "visit") != [])
        check("a level's run with no options file fails the judgement", judge_run(run, start, "visit", Path(folder) / "none") != [])
        (run / "user/savegames/one.bes").write_bytes(b"damaged")
        check("a damaged career in a run folder fails the run", judge_run(run, start, "visit") != [])
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", type=Path, help="a finished run folder to judge")
    parser.add_argument("--start", type=Path, help="the start file the run began from")
    parser.add_argument("--scenario", choices=("visit", "refused", "hall", "midquit", "sea", "edge"), default="visit")
    parser.add_argument("--options-reference", type=Path, help="a control run of the same action from the same career: the options file must equal its file")
    parser.add_argument("--options-changed", action="store_true", help="the run changes a setting: only the options fields may differ, and one must")
    args = parser.parse_args()
    if not args.run:
        return self_test()
    if not args.start:
        parser.error("--run needs --start")
    problems = judge_run(args.run, args.start, args.scenario, args.options_reference, args.options_changed)
    for line in problems:
        print("FAIL " + line)
    print("ok   the run passed" if not problems else "the run failed")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
