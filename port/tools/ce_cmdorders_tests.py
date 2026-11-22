#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Commander's pure rules, run apart from the game: the selection and its numbered groups and the drag's rectangle (port/game/ce_cmdsel.h), a
standing order's word (port/game/ce_cmdoutcome.h), where several squads sent to one place stand (port/game/ce_cmdgroup.h), and the order layer's and
the match's small decisions (port/game/ce_cmdrules.h: which marches hold on arrival, which spread, which level is a Commander match).

    python3 port/tools/ce_cmdorders_tests.py [--cxx g++]

Two drivers are compiled with the host compiler against the three headers and must say PASS (port/tools/ce_cmdorders_driver.cpp and
port/tools/ce_cmdgroup_driver.cpp). Then each mutant below, one changed line of one header, is compiled the same way and the driver must FAIL:
a rule no check holds would let its mutant pass. A mutant whose line is not in the header once, or that does not compile, fails this suite too
(the list has gone stale). No game data, device or network is used."""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = ("ce_cmdsel.h", "ce_cmdoutcome.h", "ce_cmdgroup.h", "ce_cmdrules.h")
DRIVERS = {"orders": ROOT / "port/tools/ce_cmdorders_driver.cpp", "group": ROOT / "port/tools/ce_cmdgroup_driver.cpp"}

# (name, header, the line as it is, the line as the mutant has it, the driver that must notice)
MUTANTS = (
    ("a squad may join twice", "ce_cmdsel.h", "if (id < 0 || s.n < 0 || s.n >= kCeSelMax || CeSelHas(s, id)) return false;",
     "if (id < 0 || s.n < 0 || s.n >= kCeSelMax) return false;", "orders"),
    ("a number below zero joins", "ce_cmdsel.h", "if (id < 0 || s.n < 0 || s.n >= kCeSelMax || CeSelHas(s, id)) return false;",
     "if (s.n < 0 || s.n >= kCeSelMax || CeSelHas(s, id)) return false;", "orders"),
    ("a full selection drops its last squad for a new one", "ce_cmdsel.h", "if (id < 0 || s.n < 0 || s.n >= kCeSelMax || CeSelHas(s, id)) return false;",
     "if (id < 0 || s.n < 0 || CeSelHas(s, id)) return false; if (s.n >= kCeSelMax) --s.n;", "orders"),
    ("the lead is the last one picked", "ce_cmdsel.h", "inline int CeSelLead(const CeCmdSel &s) { return s.n > 0 ? s.ids[0] : -1; }",
     "inline int CeSelLead(const CeCmdSel &s) { return s.n > 0 ? s.ids[s.n - 1] : -1; }", "orders"),
    ("removing a squad swaps the last one into its place", "ce_cmdsel.h", "for (int i = at + 1; i < s.n; ++i) s.ids[i - 1] = s.ids[i];",
     "s.ids[at] = s.ids[s.n - 1];", "orders"),
    ("a Shift click only ever adds", "ce_cmdsel.h", "if (CeSelRemove(s, id)) return false;", "if (CeSelHas(s, id)) return true;", "orders"),
    ("a plain click adds to what is picked", "ce_cmdsel.h", "\ts.n = 0;\n\tCeSelAdd(s, id);", "\tCeSelAdd(s, id);", "orders"),
    ("pruning keeps the squads that are gone", "ce_cmdsel.h", "if (keep(s.ids[i])) s.ids[kept++] = s.ids[i];", "if (!keep(s.ids[i])) s.ids[kept++] = s.ids[i];", "orders"),
    ("pruning reports nothing gone", "ce_cmdsel.h", "return before - kept;", "return 0;", "orders"),
    ("the last group cannot be kept", "ce_cmdsel.h", "if (k < 0 || k >= kCeSelGroups) return false;\n\tg.group[k] = s;",
     "if (k < 0 || k >= kCeSelGroups - 1) return false;\n\tg.group[k] = s;", "orders"),
    ("an empty group empties the selection", "ce_cmdsel.h", "if (k < 0 || k >= kCeSelGroups || g.group[k].n <= 0) return false;",
     "if (k < 0 || k >= kCeSelGroups) return false;", "orders"),
    ("every group is group 0", "ce_cmdsel.h", "\tg.group[k] = s;\n\treturn true;", "\tg.group[0] = s;\n\treturn true;", "orders"),
    ("a drag needs both axes", "ce_cmdsel.h", "return dx >= slack || dy >= slack;", "return dx >= slack && dy >= slack;", "orders"),
    ("a drag to the left is a click", "ce_cmdsel.h", "const float dx = x1 > x0 ? x1 - x0 : x0 - x1,", "const float dx = x1 - x0,", "orders"),
    ("the rectangle works one way only", "ce_cmdsel.h", "const float left = x0 < x1 ? x0 : x1, right = x0 < x1 ? x1 : x0,",
     "const float left = x0, right = x1,", "orders"),
    ("the rectangle leaves out its edge", "ce_cmdsel.h", "return px >= left && px <= right && py >= top && py <= bottom;",
     "return px > left && px < right && py > top && py < bottom;", "orders"),
    ("the rectangle ignores height", "ce_cmdsel.h", "return px >= left && px <= right && py >= top && py <= bottom;", "return px >= left && px <= right;", "orders"),
    ("a hold that fires is Holding", "ce_cmdoutcome.h", "if (!march) return engaged > 0 ? CE_OUT_FIGHTING : CE_OUT_HOLDING;", "if (!march) return CE_OUT_HOLDING;", "orders"),
    ("arrival is forgotten when the squad drifts", "ce_cmdoutcome.h", "if (!t.arrived && distance < radius) t.arrived = true;", "t.arrived = distance < radius;", "orders"),
    ("the radius itself counts as there", "ce_cmdoutcome.h", "if (!t.arrived && distance < radius) t.arrived = true;", "if (!t.arrived && distance <= radius) t.arrived = true;", "orders"),
    ("an arrived squad that fires is Arrived", "ce_cmdoutcome.h", "if (t.arrived) return engaged > 0 ? CE_OUT_FIGHTING : CE_OUT_ARRIVED;",
     "if (t.arrived) return CE_OUT_ARRIVED;", "orders"),
    ("any step nearer is progress", "ce_cmdoutcome.h", "if (distance <= t.best - kCeTrackProgress) { t.best = distance; t.since = tick; }",
     "if (distance < t.best) { t.best = distance; t.since = tick; }", "orders"),
    ("progress does not restart the wait", "ce_cmdoutcome.h", "if (distance <= t.best - kCeTrackProgress) { t.best = distance; t.since = tick; }",
     "if (distance <= t.best - kCeTrackProgress) { t.best = distance; }", "orders"),
    ("a fight does not restart the wait", "ce_cmdoutcome.h", "if (engaged > 0) { t.since = tick; return CE_OUT_FIGHTING; }", "if (engaged > 0) { return CE_OUT_FIGHTING; }", "orders"),
    ("a fight on the march is Moving", "ce_cmdoutcome.h", "if (engaged > 0) { t.since = tick; return CE_OUT_FIGHTING; }", "if (engaged > 0) { t.since = tick; }", "orders"),
    ("stuck one tick late", "ce_cmdoutcome.h", "return tick - t.since >= stuckTicks ? CE_OUT_STUCK : CE_OUT_MOVING;",
     "return tick - t.since > stuckTicks ? CE_OUT_STUCK : CE_OUT_MOVING;", "orders"),
    ("a march is never Stuck", "ce_cmdoutcome.h", "return tick - t.since >= stuckTicks ? CE_OUT_STUCK : CE_OUT_MOVING;", "return CE_OUT_MOVING;", "orders"),
    ("a new order keeps the old arrival", "ce_cmdoutcome.h", "\tt.since = tick;\n\tt.arrived = false;", "\tt.since = tick;", "orders"),
    ("a Fall back never becomes a Hold on arrival", "ce_cmdrules.h", "return arrived && (verb == CE_RULE_MOVE || verb == CE_RULE_FALLBACK);",
     "return arrived && verb == CE_RULE_MOVE;", "orders"),
    ("a Move never becomes a Hold on arrival", "ce_cmdrules.h", "return arrived && (verb == CE_RULE_MOVE || verb == CE_RULE_FALLBACK);",
     "return arrived && verb == CE_RULE_FALLBACK;", "orders"),
    ("a march holds before it arrives", "ce_cmdrules.h", "return arrived && (verb == CE_RULE_MOVE || verb == CE_RULE_FALLBACK);",
     "return (verb == CE_RULE_MOVE || verb == CE_RULE_FALLBACK);", "orders"),
    ("an Advance holds on arrival too", "ce_cmdrules.h", "return arrived && (verb == CE_RULE_MOVE || verb == CE_RULE_FALLBACK);",
     "return arrived && CeCmdMarches(verb);", "orders"),
    ("a Fall back is not a march", "ce_cmdrules.h", "return verb == CE_RULE_MOVE || verb == CE_RULE_ADVANCE || verb == CE_RULE_FALLBACK; }",
     "return verb == CE_RULE_MOVE || verb == CE_RULE_ADVANCE; }", "orders"),
    ("one squad spreads round the point", "ce_cmdrules.h", "return squads > 1 && CeCmdMarches(verb);", "return squads > 0 && CeCmdMarches(verb);", "orders"),
    ("a Hold spreads", "ce_cmdrules.h", "return squads > 1 && CeCmdMarches(verb);", "return squads > 1;", "orders"),
    ("any versus level is a Commander match", "ce_cmdrules.h", "return !network && multiplayer && players == 2 && (cliMatch || soloMatch);",
     "return !network && multiplayer && players == 2;", "orders"),
    ("a network game can be a match", "ce_cmdrules.h", "return !network && multiplayer && players == 2 && (cliMatch || soloMatch);",
     "return multiplayer && players == 2 && (cliMatch || soloMatch);", "orders"),
    ("a campaign level is a match", "ce_cmdrules.h", "return !network && multiplayer && players == 2 && (cliMatch || soloMatch);",
     "return !network && players == 2 && (cliMatch || soloMatch);", "orders"),
    ("a menu-chosen versus level counts", "ce_cmdrules.h", "return !network && multiplayer && players == 2 && (cliMatch || soloMatch);",
     "return !network && multiplayer && players == 2 && (cliMatch || soloMatch || cliLevel < 0);", "orders"),
    ("solo skirmish never counts as a match", "ce_cmdrules.h", "const bool soloMatch = soloSkirmish && level >= 850 && level <= 899;",
     "const bool soloMatch = false;", "orders"),
    ("solo skirmish ignores level bounds", "ce_cmdrules.h", "const bool soloMatch = soloSkirmish && level >= 850 && level <= 899;",
     "const bool soloMatch = soloSkirmish;", "orders"),
    ("the alerts forget when they started", "ce_cmdrules.h", "a.hurt = hurt; a.lastBaseHit = tick;\n\t\treturn CE_ALERT_NONE;",
     "a.hurt = hurt;\n\t\treturn CE_ALERT_NONE;", "orders"),
    ("the first update speaks", "ce_cmdrules.h", "\tif (!a.primed)\n", "\tif (false)\n", "orders"),
    ("a lost building is not said", "ce_cmdrules.h", "if (buildings < a.buildings) { alert = CE_ALERT_BUILDING_LOST; a.lastBaseHit = tick; }",
     "if (false) { alert = CE_ALERT_BUILDING_LOST; a.lastBaseHit = tick; }", "orders"),
    ("the base is under attack at every hit", "ce_cmdrules.h", "else if (baseLife < a.baseLife - 0.001f && tick - a.lastBaseHit >= quiet)",
     "else if (baseLife < a.baseLife - 0.001f)", "orders"),
    ("the alert interval is zero", "ce_cmdrules.h", "const unsigned kCeCmdAlertQuiet = 400;", "const unsigned kCeCmdAlertQuiet = 0;", "orders"),
    ("the alert interval is a tenth", "ce_cmdrules.h", "const unsigned kCeCmdAlertQuiet = 400;", "const unsigned kCeCmdAlertQuiet = 40;", "orders"),
    ("a quiet time one update short", "ce_cmdrules.h", "tick - a.lastBaseHit >= quiet)", "tick - a.lastBaseHit + 1 >= quiet)", "orders"),
    ("a view the player closed waits for the engine", "ce_cmdrules.h", "if (wasOpen && !open && !pilotUp && !playerClosed) { wanted = true; return CE_REOPEN_WAIT; }",
     "if (wasOpen && !open && !pilotUp) { wanted = true; return CE_REOPEN_WAIT; }", "orders"),
    ("the view waits with the engine in the field", "ce_cmdrules.h", "if (wasOpen && !open && !pilotUp && !playerClosed) { wanted = true; return CE_REOPEN_WAIT; }",
     "if (wasOpen && !open && !playerClosed) { wanted = true; return CE_REOPEN_WAIT; }", "orders"),
    ("the view opens while the engine is down", "ce_cmdrules.h", "if (wanted && !open && pilotUp && canOpen) { wanted = false; return CE_REOPEN_OPEN; }",
     "if (wanted && !open && canOpen) { wanted = false; return CE_REOPEN_OPEN; }", "orders"),
    ("the view opens where it cannot", "ce_cmdrules.h", "if (wanted && !open && pilotUp && canOpen) { wanted = false; return CE_REOPEN_OPEN; }",
     "if (wanted && !open && pilotUp) { wanted = false; return CE_REOPEN_OPEN; }", "orders"),
    ("the view opens again and again", "ce_cmdrules.h", "if (wanted && !open && pilotUp && canOpen) { wanted = false; return CE_REOPEN_OPEN; }",
     "if (wanted && !open && pilotUp && canOpen) { return CE_REOPEN_OPEN; }", "orders"),
    ("a finished battle keeps waiting", "ce_cmdrules.h", "if (!match) { wanted = false; return CE_REOPEN_NONE; }", "if (!match) { return CE_REOPEN_NONE; }", "orders"),
    ("the open key while down only asks", "ce_cmdrules.h", "\twanted = !wanted;\n\treturn true;", "\twanted = true;\n\treturn true;", "orders"),
    ("the open key while down is taken in the field too", "ce_cmdrules.h", "if (!match || pilotUp) return false;", "if (!match) return false;", "orders"),
    ("a Fall back may go into the water", "ce_cmdrules.h", "if (!places[i].land || places[i].kind < CE_PLACE_BUILDING", "if (places[i].kind < CE_PLACE_BUILDING", "orders"),
    ("a node before a building", "ce_cmdrules.h", "if (places[i].kind < bestKind || (places[i].kind == bestKind && d < bestD))", "if (best < 0 || d < bestD)", "orders"),
    ("the farthest building", "ce_cmdrules.h", "(places[i].kind == bestKind && d < bestD)", "(places[i].kind == bestKind && d > bestD)", "orders"),
    ("the rally stands on the building", "ce_cmdrules.h", "*rx = px + dx * away;", "*rx = px;", "orders"),
    ("the rally overshoots", "ce_cmdrules.h", "(standoff < d ? standoff / d : 1.f)", "(standoff / d)", "orders"),
    ("the base is never under attack", "ce_cmdrules.h", "else if (baseLife < a.baseLife - 0.001f && tick - a.lastBaseHit >= quiet)",
     "else if (false)", "orders"),
    ("the hurt mark is said at every update", "ce_cmdrules.h", "else if (hurt > a.hurt) alert", "else if (hurt > 0) alert", "orders"),
    ("badly hurt is said as hurt", "ce_cmdrules.h", "alert = hurt == 2 ? CE_ALERT_ENGINE_BADLY_HURT : CE_ALERT_ENGINE_HURT;", "alert = CE_ALERT_ENGINE_HURT;", "orders"),
    ("half its life is not hurt yet", "ce_cmdrules.h", "life < lifeFull * 0.5f ? 1 : 0", "life < lifeFull * 0.4f ? 1 : 0", "orders"),
    ("a mended engine is never hurt again", "ce_cmdrules.h", "if (!(hurt > a.hurt && alert == CE_ALERT_BUILDING_LOST)) a.hurt = hurt;",
     "if (!(hurt > a.hurt && alert == CE_ALERT_BUILDING_LOST) && hurt > a.hurt) a.hurt = hurt;", "orders"),
    ("a mark in a loss's update is never said", "ce_cmdrules.h", "if (!(hurt > a.hurt && alert == CE_ALERT_BUILDING_LOST)) a.hurt = hurt;", "a.hurt = hurt;", "orders"),
    ("every squad is sent to the place itself", "ce_cmdgroup.h", "\t\t\t\txs[made] = px;\n\t\t\t\tys[made] = py;", "\t\t\t\txs[made] = cx;\n\t\t\t\tys[made] = cy;", "group"),
    ("the nearest squad chooses first", "ce_cmdgroup.h", "if (d > farthest) { farthest = d; who = i; }", "if (farthest < 0.f || d < farthest) { farthest = d; who = i; }", "group"),
    ("a taken point is given again", "ce_cmdgroup.h", "\t\tused[best] = true;", "\t\tused[best] = false;", "group"),
    ("a formation is not moved inside the map", "ce_cmdgroup.h", "\t\txs[i] += dx; ys[i] += dy;", "\t\txs[i] += 0.f * dx; ys[i] += 0.f * dy;", "group"),
)


def run_driver(cxx, driver, headers, strict=True):
    """Compile the driver against these header texts and run it: (compiled, passed, output). A mutant is compiled without -Werror: a changed line
    may leave a parameter or a variable unused, and it is the driver that has to notice the change."""
    with tempfile.TemporaryDirectory() as directory:
        d = Path(directory)
        for name, text in headers.items():
            (d / name).write_text(text)
        binary = d / "driver"
        built = subprocess.run([cxx, "-std=c++17", "-O1", "-Wall", "-Wextra"] + (["-Werror"] if strict else []) + ["-I" + str(d), str(driver), "-o", str(binary)],
                               capture_output=True, text=True, timeout=120)
        if built.returncode != 0:
            return False, False, built.stderr[-600:]
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        return True, ran.returncode == 0 and ran.stdout.strip().endswith("PASS"), ran.stdout[-600:]


# The player's text: every string the map shows (a note, a line of the view, a button, the prompt, a card word) and every reason the order layer gives
# for a refusal. The game's small face has none of these characters (the interface guide's rule T2), and a key is never named in brackets or between
# dashes (W5); the old face printed each lacking character as a dash.
FORBIDDEN = set("$@[\\]^_{|}~`")
SHOWN = (r'\bNote\(\s*"((?:[^"\\]|\\.)*)"', r'\bText\([^;]*?"((?:[^"\\]|\\.)*)"', r'\*why\s*=\s*"((?:[^"\\]|\\.)*)"', r'\breturn\s+"((?:[^"\\]|\\.)*)"')
LISTS = (r"labels\[\]\s*=\s*\{([^}]*)\}", r"action\[5\]\s*=\s*\{([^}]*)\}", r"one\[\]\s*=\s*\{([^}]*)\}", r"several\[\]\s*=\s*\{([^}]*)\}")
SHOWN_FORMATS = ("Commander   Battle Line", "Order: %s", "%s   %d unit", "squad%s", "%d squads %s.", "Group %d")


def shown_strings(text):
    """The string literals of a source text that reach the player (see SHOWN and LISTS), with the literal formats of snprintf calls into the view's text."""
    import re
    out = []
    for pattern in SHOWN:
        out += re.findall(pattern, text)
    for pattern in LISTS:
        for body in re.findall(pattern, text):
            out += re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    for literal in re.findall(r'snprintf\([^;]*?"((?:[^"\\]|\\.)*)"', text):
        if any(literal.startswith(f) or f in literal for f in SHOWN_FORMATS):
            out.append(literal)
    return out


def text_lint(sources):
    """(problems, how many strings were read)."""
    problems, read = [], 0
    for name, text in sources.items():
        for s in shown_strings(text):
            read += 1
            bad = sorted(set(s) & FORBIDDEN)
            if bad:
                problems.append(f"{name}: {s!r} holds {''.join(bad)}")
    return problems, read


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cxx", default="g++")
    args = ap.parse_args()
    headers = {name: (ROOT / "port/game" / name).read_text() for name in HEADERS}
    ok = True
    for label, driver in DRIVERS.items():
        compiled, passed, output = run_driver(args.cxx, driver, headers)
        print(f"{'PASS' if compiled and passed else 'FAIL'} the {label} driver passes against the headers")
        if not (compiled and passed):
            print(output)
            ok = False
    for name, header, old, new, driver in MUTANTS:
        if headers[header].count(old) != 1:
            print(f"FAIL mutant: {name}: its line is in {header} {headers[header].count(old)} times, not once")
            ok = False
            continue
        mutated = dict(headers)
        mutated[header] = headers[header].replace(old, new)
        compiled, passed, output = run_driver(args.cxx, DRIVERS[driver], mutated, strict=False)
        if not compiled:
            print(f"FAIL mutant: {name}: it does not compile\n{output}")
            ok = False
        elif passed:
            print(f"FAIL mutant: {name}: the {driver} driver still passes")
            ok = False
        else:
            print(f"PASS mutant: {name} is caught")
    # the player's text, and the lint's own control: a pipe or a bracketed key slipped into a note must be found
    sources = {name: (ROOT / "port/game" / name).read_text() for name in ("ce_cmdmap.cpp", "ce_commander.cpp")}
    problems, read = text_lint(sources)
    print(f"{'PASS' if not problems and read >= 40 else 'FAIL'} the player's text holds none of the characters the small face lacks ({read} strings read)")
    for p in problems:
        print("  " + p)
    ok = ok and not problems and read >= 40
    for label, change in (("a pipe in a note", ('Note("Pick a squad first.");', 'Note("Pick a squad | first.");')),
                          ("a key in brackets on a button", ('"Move", "Advance", "Hold", "Fall Back"', '"Move", "Advance [A]", "Hold", "Fall Back"')),
                          ("an underscore in a refusal", ('*why = "Already on its own orders.";', '*why = "Already_on its own orders.";'))):
        name = "ce_cmdmap.cpp" if "why" not in change[0] else "ce_commander.cpp"
        if sources[name].count(change[0]) < 1:
            print(f"FAIL lint control: {label}: its line is gone from {name}")
            ok = False
            continue
        changed = dict(sources)
        changed[name] = sources[name].replace(change[0], change[1], 1)
        caught = bool(text_lint(changed)[0])
        print(f"{'PASS' if caught else 'FAIL'} lint control: {label} is found")
        ok = ok and caught
    print("commander order rules: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
