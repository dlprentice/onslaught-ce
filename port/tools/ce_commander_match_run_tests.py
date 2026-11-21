#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests of the Commander match runner's reading and judging (port/tools/ce_commander_match_run.py), on an authored probe.

    python3 port/tools/ce_commander_match_run_tests.py

The probe below is written in the order layer's own line formats (ce_commander.cpp prints them: T, Q, S, E, O, A, P and the `#` notes), with
invented numbers and names: a level where the map opens, a squad is picked by a click, sent forward, arrives and fires, a second squad's order
is refused, and the level is won. Every check the runner knows must pass on it, and each must fail on a probe changed in the one way that
check exists to notice (a check that cannot fail proves nothing). The key and pointer script and the plan's own errors are tested too.
No game, device or network is used."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ce_commander_match_run as R  # noqa: E402

# One listing: the T line, a Q and an S line for each squad, and the E line. Squad 3 is the one ordered; 4 is friendly and scripted; 9 is the enemy.
def listing(tick, present, s3, s3verb="-", s3firing=0, s3arrived=0, s3outcome=0, heading=(0.25000, 0.96825), state=3, held=20, lost=30, units=(24, 31)):
    verb_number = {"-": 0, "move": 1, "advance": 2, "hold": 3, "fallback": 4}[s3verb]
    return [
        f"T {tick} {present} 41a00000 -1234567",
        f"Q 3 N 0 2 0 0 6 {s3[0]:.3f} {s3[1]:.3f} -14.000 {s3[0]:.3f} {s3[1]:.3f} 300.000 200.000 - 0 0 {s3verb} {s3firing} {s3firing}",
        "Q 4 N 0 0 0 0 5 120.000 80.000 -14.000 121.000 81.000 120.000 80.000 - 0 0 - 0 0",
        "Q 9 N 1 0 0 0 8 340.000 210.000 -12.000 341.000 211.000 340.000 210.000 S3 0 0 - 8 2",
        f"S 3 1 1 0 0 1 6 6 {verb_number} {s3arrived} {s3outcome} Authored Tank",
        "S 4 1 0 2 0 1 5 5 0 0 0 Authored Carrier",
        "S 9 0 0 1 0 1 8 8 0 0 0 Authored Raider",
        f"E {tick} {state} 1 0 0 57 {held} {lost} 2 2 6 6 {units[0]} {units[1]} 1 0.875 150.000 90.000 {heading[0]:.5f} {heading[1]:.5f} 100.0 120.0 140.0 160.0",
    ]


def good_probe():
    lines = ["# commander probe 1", "# level 851", "# game state -1 to 3 at tick 0, Present 61"]
    lines += listing(20, 91, (200.0, 100.0))
    lines += ["P 30 400 map open world 0badf00d solid 0badf00d things 300 components 4 time 41000000"]
    lines += listing(40, 431, (200.0, 100.0))
    lines += ["P 44 438 map drag 300 200 340 240 count 2", "P 46 439 map select all 2", "P 47 439 map group 1 keep 2"]
    lines += ["P 45 440 map pointer 0 318 222", "P 45 440 map press 0 318 222", "P 45 440 map select 3", "O 45 ui S=-1 T=-1 K=-1 X=0.00 Y=0.00 click ok at 318 222"]
    lines += ["P 50 450 map press 2 600 400", "O 50 advance S=3 T=-1 K=-1 X=299.20 Y=200.60 - map", "P 50 450 map order 3 2 299.2 200.6"]
    lines += ["O 50 advance S=5 T=-1 K=-1 X=259.50 Y=220.00 - map", "P 50 450 map order 5 2 259.5 220.0"]
    lines += ["P 55 460 map refused 4 3 that squad does not take orders"]
    lines += listing(60, 471, (205.0, 105.0), "advance", s3outcome=1)
    lines += listing(80, 501, (240.0, 140.0), "advance", s3firing=3)
    lines += ["A 95 3", "O 96 hold S=5 T=-1 K=-1 X=260.00 Y=219.00 - arrived"]
    lines += listing(100, 531, (297.0, 199.0), "advance", s3firing=1, s3arrived=1, held=40, lost=10, units=(22, 12))
    lines += ["O 105 release S=5 T=-1 K=-1 X=260.00 Y=219.00 - map"]
    lines += ["P 106 552 map fallback 4 home 140.0 120.0 goal 150.0 140.0 from 230.0 210.0 height -3.500 water 0.000",
              "O 106 fallback S=4 T=-1 K=-1 X=150.00 Y=140.00 - map"]
    lines += ["P 110 560 map close world 0badf00d solid 0badf00d things 280 components 4 time 41400000 frames 100 avg 0.50 ms max 1.00 ms"]
    lines += listing(120, 591, (298.0, 199.5), "advance", s3arrived=1, heading=(0.90000, 0.43589), held=55, lost=2, units=(22, 3))
    lines.append("S 5 1 1 0 0 1 4 4 0 0 0 Authored Tank")		# squad 5 after its release: no order on it
    lines += ["# game state 3 to 6 at tick 130, Present 610", "# game state 6 to 9 at tick 130, Present 700"]
    return "\n".join(lines) + "\n"


PLAN = {
    "name": "authored", "level": 851, "quit": 700,
    "expect": [
        {"check": "exit"},
        {"check": "opened", "by": 400},
        {"check": "closed"},
        {"check": "match"},
        {"check": "commandable", "min": 1},
        {"check": "press", "button": 2, "at": [600, 400]},
        {"check": "selected", "squad": 3},
        {"check": "order", "squad": 3, "verb": "advance", "near": [300, 200], "within": 3},
        {"check": "refused", "squad": 4},
        {"check": "standing", "squad": 3, "verb": "advance"},
        {"check": "moved", "squad": 3, "toward": [300, 200], "by": 90},
        {"check": "arrived", "squad": 3},
        {"check": "firing", "squad": 3, "min": 3, "from": 50},
        {"check": "steady"},
        {"check": "state", "is": "won", "by": 610},
        {"check": "outcome", "squad": 3, "is": "moving", "at": 60},
        {"check": "spread", "squads": [3, 5], "min": 15},
        {"check": "dragged", "min": 2},
        {"check": "picked_all", "min": 2},
        {"check": "group", "number": 1, "size": 2},
        {"check": "held_on_arrival", "squad": 5},
        {"check": "released", "squad": 5},
        {"check": "fell_back_home", "squad": 4},
    ],
}

# For each check, a change of the probe (or of the run's record) after which that check, and it alone among its own kind, must fail.
BREAKS = {
    "exit": lambda t: (t, {"exit": 124}),
    "opened": lambda t: (t.replace("P 30 400 map open", "P 30 401 map open"), None),
    "closed": lambda t: ("\n".join(l for l in t.split("\n") if " map close " not in l) + "\n", None),
    "match": lambda t: (t.replace("E 60 3 1 0", "E 60 3 0 0"), None),
    "commandable": lambda t: (t.replace("S 3 1 1 0 0 1 6 6 2 1 0 Authored Tank", "S 3 1 0 3 0 1 6 6 2 1 0 Authored Tank")
                              .replace("S 5 1 1 0 0 1 4 4 0 0 0 Authored Tank", "S 5 1 0 3 0 1 4 4 0 0 0 Authored Tank"), None),
    "press": lambda t: (t.replace("map press 2 600 400", "map press 2 512 384"), None),
    "selected": lambda t: (t.replace("map select 3", "map select 4"), None),
    "order": lambda t: (t.replace("X=299.20 Y=200.60 - map", "X=280.00 Y=200.60 - map"), None),
    "refused": lambda t: (t.replace("map refused 4 3", "map refused 3 3"), None),
    "standing": lambda t: (t.replace("300.000 200.000 - 0 0 advance 0 0\nQ 4 N 0 0 0 0 5 120.000 80.000 -14.000 121.000 81.000 120.000 80.000 - 0 0 - 0 0\nQ 9 N 1 0 0 0 8 340.000 210.000 -12.000 341.000 211.000 340.000 210.000 S3 0 0 - 8 2\nS 3 1 1 0 0 1 6 6 2 1 0 Authored Tank\nS 4 1 0 2 0 1 5 5 0 0 0 Authored Carrier\nS 9 0 0 1 0 1 8 8 0 0 0 Authored Raider\nE 120",
                                     "300.000 200.000 - 0 0 hold 0 0\nQ 4 N 0 0 0 0 5 120.000 80.000 -14.000 121.000 81.000 120.000 80.000 - 0 0 - 0 0\nQ 9 N 1 0 0 0 8 340.000 210.000 -12.000 341.000 211.000 340.000 210.000 S3 0 0 - 8 2\nS 3 1 1 0 0 1 6 6 2 1 0 Authored Tank\nS 4 1 0 2 0 1 5 5 0 0 0 Authored Carrier\nS 9 0 0 1 0 1 8 8 0 0 0 Authored Raider\nE 120"), None),
    "moved": lambda t: (t.replace("Q 3 N 0 2 0 0 6 298.000 199.500 -14.000 298.000 199.500", "Q 3 N 0 2 0 0 6 215.000 110.000 -14.000 215.000 110.000"), None),
    "arrived": lambda t: (t.replace("A 95 3", "A 95 4"), None),
    "firing": lambda t: (t.replace("advance 3 3", "advance 2 2"), None),
    "steady": lambda t: (t.replace("0.875 150.000 90.000 0.25000 0.96825 100.0 120.0 140.0 160.0\nA 95 3", "0.875 150.000 90.000 0.30000 0.95394 100.0 120.0 140.0 160.0\nA 95 3"), None),
    "state": lambda t: (t.replace("# game state 3 to 6 at tick 130, Present 610", "# game state 3 to 7 at tick 130, Present 610"), None),
    "outcome": lambda t: (t.replace("S 3 1 1 0 0 1 6 6 2 0 1 Authored Tank", "S 3 1 1 0 0 1 6 6 2 0 4 Authored Tank"), None),
    "spread": lambda t: (t.replace("S=5 T=-1 K=-1 X=259.50 Y=220.00 - map", "S=5 T=-1 K=-1 X=299.00 Y=201.00 - map"), None),
    "dragged": lambda t: (t.replace("map drag 300 200 340 240 count 2", "map drag 300 200 340 240 count 1"), None),
    "picked_all": lambda t: (t.replace("map select all 2", "map select all 1"), None),
    "group": lambda t: (t.replace("map group 1 keep 2", "map group 1 keep 3"), None),
    "held_on_arrival": lambda t: (t.replace("O 96 hold S=5", "O 96 hold S=6"), None),
    "released": lambda t: (t.replace("O 105 release S=5", "O 105 hold S=5"), None),
    "fell_back_home": lambda t: (t.replace("height -3.500 water 0.000", "height 2.500 water 0.000"), None),
}

# A break that takes a second check with it, and why.
ALSO = {"closed": {"steady"}}		# a map that never closes is open to the end, when the authored pilot has turned

failures = 0


def expect(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def verdicts(text, run=None):
    result = R.judge(R.parse_probe(text), PLAN, run or {"exit": 0})
    return {v["check"]["check"]: v["passed"] for v in result["checks"]}, result


def main():
    text = good_probe()
    probe = R.parse_probe(text)
    expect(probe["unread"] == 0, "every line of the authored probe is read")
    expect(probe["level"] == 851 and len(probe["ticks"]) == 6 and len(probe["E"]) == 6, "the level, the listings and the battle lines are counted")
    expect(R.end_state(probe) == ("won", 610), "the level's end is the first state after playing, with its Present")
    expect(R.open_spans(probe) == [(30, 110)], "the map's open stretch runs from its open note to its close note")
    good, result = verdicts(text)
    for name in BREAKS:
        expect(good.get(name) is True, f"the {name} check passes on the authored probe")
    expect(result["passed"], "the plan passes on the authored probe")
    expect(set(BREAKS) == {c["check"] for c in PLAN["expect"]}, "every check of the plan has its break below")
    for name, change in BREAKS.items():
        changed, run = change(text)
        if run is None and changed == text:
            expect(False, f"the {name} break changes the probe")
            continue
        broken, broken_result = verdicts(changed, run)
        expect(broken.get(name) is False, f"the {name} check fails when its fact is gone")
        expect(not broken_result["passed"], f"and the plan fails with it ({name})")
        others = [k for k, v in broken.items() if k != name and not v and k not in ALSO.get(name, ())]
        expect(not others, f"the {name} break fails that check alone" + (f" (also failed: {', '.join(others)})" if others else ""))
    # a spread of Fall backs is judged when the plan names that verb, and only then
    falls = text.replace("O 50 advance S=3", "O 50 fallback S=3").replace("O 50 advance S=5", "O 50 fallback S=5")
    fb = dict(PLAN, expect=[{"check": "spread", "squads": [3, 5], "min": 15, "verbs": ["fallback"]}])
    expect(R.judge(R.parse_probe(falls), fb, {"exit": 0})["passed"], "two Fall backs to places apart pass a spread check that names the verb")
    expect(not R.judge(R.parse_probe(falls), dict(PLAN, expect=[{"check": "spread", "squads": [3, 5], "min": 15}]), {"exit": 0})["passed"],
           "and are not marches for one that does not")
    together = falls.replace("S=5 T=-1 K=-1 X=259.50 Y=220.00 - map", "S=5 T=-1 K=-1 X=299.00 Y=201.00 - map")
    expect(not R.judge(R.parse_probe(together), fb, {"exit": 0})["passed"], "two Fall backs to one place fail it")
    # the map's own notes: found by their whole words (or by their first words when the words end with "..."), from a Present on, after another note
    noted = lambda c, on=probe: R.judge(on, dict(PLAN, expect=[dict(c, check="noted")]), {"exit": 0})["passed"]
    expect(noted({"what": "group 1 keep 2"}) and noted({"what": "select all 2"}), "a note of the map is found by its words")
    expect(not noted({"what": "group 1 keep"}) and not noted({"what": "drag"}), "the first words of a note are not the note")
    expect(noted({"what": "group 1 keep ..."}) and noted({"what": "drag ..."}), "but they find it when they end with ...")
    expect(not noted({"what": "reopen"}), "a note the map never made is not")
    expect(noted({"what": "press 2 600 400", "from": 450}) and not noted({"what": "press 2 600 400", "from": 451}), "nor one before the Present asked for")
    expect(noted({"what": "select 3", "after": "drag ..."}) and not noted({"what": "drag ...", "after": "close ..."}), "nor one before the note it must follow")
    # the round-2 review's case: a view that waited to come back and never did must not pass "reopen" after "reopen wait"
    waited = R.parse_probe(text + "P 140 720 map reopen wait\n")
    expect(noted({"what": "reopen wait"}, waited) and not noted({"what": "reopen", "after": "reopen wait"}, waited),
           "a view that only waited to come back did not come back")
    back = R.parse_probe(text + "P 140 720 map reopen wait\nP 150 811 map reopen\n")
    expect(noted({"what": "reopen", "after": "reopen wait"}, back), "and one that came back did")
    expect(R.open_spans(R.parse_probe(text.replace("map open world", "map opened world"))) == [], "only the open note opens the map's stretch")
    expect(noted({"what": "select all 2", "times": 1}) and not noted({"what": "select all 2", "times": 2}), "a note can be counted")
    one = lambda c, text_=text: R.judge(R.parse_probe(text_), dict(PLAN, expect=[c]), {"exit": 0})["passed"]
    expect(one({"check": "open_at", "present": 450}) and not one({"check": "open_at", "present": 570}) and not one({"check": "open_at", "present": 390}),
           "the map is open between its open note and its close note, and not outside them")
    expect(one({"check": "generator"}) and not one({"check": "generator"}, text + "# the game's generator moved inside the layer at tick 120: a bug\n"),
           "the generator check fails on the probe's own bug line")
    # steady against a run that never opened the view: the authored pilot turns at tick 120 in both runs, so beside that run he is steady
    steady_vs = dict(PLAN, expect=[{"check": "steady", "against": "never"}])
    turned = R.parse_probe(text)
    expect(R.judge(probe, steady_vs, {"exit": 0, "references": {"never": turned}})["passed"], "a turn the level makes in both runs is steady beside the other run")
    other = R.parse_probe(text.replace("0.875 150.000 90.000 0.25000 0.96825 100.0 120.0 140.0 160.0\nA 95 3", "0.875 150.000 90.000 0.30000 0.95394 100.0 120.0 140.0 160.0\nA 95 3"))
    expect(not R.judge(probe, steady_vs, {"exit": 0, "references": {"never": other}})["passed"], "a heading that differs from the other run's is not")
    expect(not R.judge(probe, steady_vs, {"exit": 0})["passed"], "and without the other run's probe the check fails")
    # a release that did not last: the squad was ordered again after it
    again, _ = verdicts(text.replace("S 5 1 1 0 0 1 4 4 0 0 0 Authored Tank", "S 5 1 1 0 0 1 4 4 3 0 0 Authored Tank"))
    expect(again["released"] is False, "a squad ordered again after its release is not released")
    # a Fall back that did not go home: from a place outside the side's buildings, or away from home
    fb = lambda text_: verdicts(text_)[0]["fell_back_home"]
    expect(fb(text.replace("map fallback 4 home 140.0 120.0", "map fallback 4 home 52.0 459.0")) is False, "a home that is not one of the side's buildings is not home")
    expect(fb(text.replace("goal 150.0 140.0 from 230.0 210.0", "goal 300.0 300.0 from 230.0 210.0")) is False, "a goal farther from home than the squad stood is not going home")
    expect(fb(text.replace("goal 150.0 140.0 from 230.0 210.0", "goal 150.0 140.0 from 150.0 150.0")) is True, "a squad already at home stays there")
    expect(fb(text.replace(" 100.0 120.0 140.0 160.0", "")) is False, "without the side's buildings in the battle line nothing is judged")
    # limits a reader relies on
    late, _ = verdicts(text.replace("Present 610", "Present 611"))
    expect(late["state"] is False, "a win after the Present asked for is not in time")
    measured = result["measured"]
    expect(measured["census_last"]["by_reason"] == {"takes orders": {"squads": 2, "units": 10}, "scripted": {"squads": 1, "units": 5}, "enemy": {"squads": 1, "units": 8}},
           "the census sorts squads by why they take no orders")
    expect(measured["battle_first"]["held"] == 20 and measured["battle_last"]["held"] == 55 and measured["battle_last"]["units"] == [22, 3],
           "the battle's two ends are reported")
    expect(measured["end"] == {"state": "won", "present": 610} and measured["orders_from_map"] == 4 and measured["orders_refused"] == 1, "and the end and the orders")
    playing = dict(PLAN, expect=[{"check": "playing"}])
    expect(R.judge(probe, playing, {"exit": 0})["passed"] is False, "a level that was won is not still playing")
    quit_only = text.replace("# game state 3 to 6 at tick 130, Present 610\n# game state 6 to 9 at tick 130, Present 700", "# game state 3 to 9 at tick 130, Present 700")
    expect(R.judge(R.parse_probe(quit_only), playing, {"exit": 0})["passed"] is True, "a run that only reached its own end was still playing")
    expect(R.judge(R.parse_probe(quit_only), dict(PLAN, expect=[{"check": "state", "is": "won"}]), {"exit": 0})["passed"] is False, "and was not won")
    empty = R.judge(R.parse_probe(""), PLAN, {"exit": 0})
    expect(not empty["passed"] and sum(1 for v in empty["checks"] if v["passed"]) == 1, "an empty probe passes nothing but the exit status")
    expect(not R.judge(probe, dict(PLAN, expect=[]), {"exit": 0})["passed"], "a plan with no checks does not pass")
    expect(not R.judge(probe, dict(PLAN, expect=[{"check": "wished"}]), {"exit": 0})["passed"], "a check the runner does not know fails")
    expect(not R.judge(probe, dict(PLAN, expect=[{"check": "order", "squad": 3}]), {"exit": 0})["passed"], "a check written without what it needs fails")
    garbled = R.parse_probe(text + "Q 3 N zero\nZ 1 2 3\n")
    expect(garbled["unread"] == 2, "lines the runner cannot read are counted, not guessed at")
    # the script of keys and pointer events
    keys = R.fake_keys({"keys": [[400, "M"], [500, "E", "down"], [600, "E", "up"], [450, "tab"]], "pointer": [[555, "move", 600, 400], [560, "right", 600, 400]]})
    expect(keys == "400:16:32:d,401:16:32:u,450:43:f:d,451:43:f:u,500:8:12:d,a555:600:400,b560:3:d:600:400,b562:3:u:600:400,600:8:12:u",
           "keys and pointer events come out in Present order, a tap as a press and a release")
    drag = R.fake_keys({"keys": [[700, "DELETE"]], "pointer": [[600, "leftdown", 100, 120], [610, "move", 300, 320], [620, "leftup", 300, 320]]})
    expect(drag == "b600:1:d:100:120,a610:300:320,b620:1:u:300:320,700:76:153:d,701:76:153:u", "a held left button is a press, moves and a release, and Delete is the extended key")
    group_min = dict(PLAN, expect=[{"check": "group", "number": 1, "min": 2}])
    expect(R.judge(probe, group_min, {"exit": 0})["passed"] and not R.judge(probe, dict(PLAN, expect=[{"check": "group", "number": 1, "min": 3}]), {"exit": 0})["passed"],
           "a group's size can be asked as at least a number")
    for bad in ({"keys": [[1, "NOKEY"]]}, {"keys": [[1, "M", "twice"]]}, {"pointer": [[1, "wave", 2, 3]]}):
        try:
            R.fake_keys(bad)
            expect(False, f"a plan with {bad} is refused")
        except R.PlanError:
            expect(True, f"a plan with {list(bad)[0]} the runner does not know is refused")
    print("commander match run tests: " + ("OK" if not failures else f"{failures} FAILED"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
