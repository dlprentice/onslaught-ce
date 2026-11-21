#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The co-op pair runner's judge and key scripts, on log lines in the game's own words.

    python3 port/tools/coop_pair_run_tests.py

The logs below are lines the network flavour writes (ce_netlobby.cpp, ce_network_lockstep.cpp, dinput.cpp, portlog.cpp),
taken from a real pair run of coop_pair_run.py (2026-11-12, level 856) and trimmed to the lines the judge reads plus neighbours it must ignore;
join codes and addresses are replaced. Each case changes one thing a broken game would change and the judge must fail it
at the stage named. No game, network or player data.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import coop_pair_run as run  # noqa: E402

HOST = """\
onslaught-ce: session pid=101 t_ms=1053 P=0 video: extra graphics event=resource-outcome capable=1 requested=1 active=1 checked_resources=4 failed_resources=0
onslaught-ce: session pid=101 t_ms=9120 P=512 game: frontend page=23 requested=12 transition_ticks=0
onslaught-ce: network lobby: join code written for the test harness (offscreen run)
onslaught-ce: network lobby: state host_waiting
onslaught-ce: network lobby: hosting on UDP port 28461
onslaught-ce: network lobby: state host_connecting
onslaught-ce: network lobby: state ready
onslaught-ce: network lobby: both players are in the lobby (0.2.0-rc0, 0.2.0-rc0)
onslaught-ce: network lobby: start agreed (level 856, host); restarting into the game
onslaught-ce: network lobby: starting the network game as the host
onslaught-ce: co-op started (you are player 1); start state d7eea2e52248c01e
onslaught-ce: test: gameplay pad script begins at Present 211
onslaught-ce: co-op tick 1000 in step (50 hashes compared)
onslaught-ce: test: gameplay pad script ended
onslaught-ce: co-op mission selected state confirmed at tick 1584, hash 3ea4a8aec429ea66, 80 scheduled hashes compared
onslaught-ce: network lobby: returning to the front end (the game ended); the player is told: The network game has ended.
onslaught-ce: session pid=101 t_ms=88120 P=301 game: frontend page=23 requested=12 transition_ticks=0
"""
GUEST = """\
onslaught-ce: session pid=202 t_ms=544 P=0 video: extra graphics event=resource-outcome capable=1 requested=1 active=1 checked_resources=4 failed_resources=0
onslaught-ce: session pid=202 t_ms=8120 P=498 game: frontend page=23 requested=12 transition_ticks=0
onslaught-ce: network lobby: state join_connecting
onslaught-ce: network lobby: joining
onslaught-ce: network lobby: state ready
onslaught-ce: network lobby: both players are in the lobby (0.2.0-rc0, 0.2.0-rc0)
onslaught-ce: network lobby: start agreed (level 856, guest); restarting into the game
onslaught-ce: network lobby: starting the network game as the joining player
onslaught-ce: co-op started (you are player 2); start state d7eea2e52248c01e
onslaught-ce: test: gameplay pad script begins at Present 198
onslaught-ce: co-op tick 1000 in step (50 hashes compared)
onslaught-ce: test: gameplay pad script ended
onslaught-ce: co-op mission selected state confirmed at tick 1584, hash 3ea4a8aec429ea66, 80 scheduled hashes compared
onslaught-ce: network lobby: returning to the front end (the game ended); the player is told: The network game has ended.
onslaught-ce: session pid=202 t_ms=87990 P=290 game: frontend page=23 requested=12 transition_ticks=0
"""
# --ending leave, from a real run (2026-11-12): the guest's window closed 20 s into the mission.
HOST_LEAVE = HOST.split("onslaught-ce: co-op tick 1000", 1)[0] + """\
onslaught-ce: co-op session stopped: the other player left the co-op game
onslaught-ce: co-op network: 446 messages sent, 446 received, 0 packets rejected
onslaught-ce: network lobby: returning to the front end (the game stopped); the player is told: The network game stopped: the other player left the co-op game.
onslaught-ce: session pid=101 t_ms=1611 P=4 game: frontend page=23 requested=12 transition_ticks=0
"""
GUEST_LEAVE = GUEST.split("onslaught-ce: co-op tick 1000", 1)[0] + """\
onslaught-ce: co-op ended at tick 421; final state was not confirmed
onslaught-ce: co-op network: 446 messages sent, 446 received, 0 packets rejected
onslaught-ce: network lobby: the window was closed: not returning to the front end
"""
# --ending ring, from a real run (2026-11-12, ring1): the guest opened the mission's ring, closed it, opened it again and
# chose Leave Game and Yes; the host's game was told its friend left.
HOST_RING = HOST.split("onslaught-ce: co-op tick 1000", 1)[0] + """\
onslaught-ce: co-op session stopped: the other player left the co-op game
onslaught-ce: co-op network: 475 messages sent, 475 received, 0 packets rejected
onslaught-ce: network lobby: the network game stopped: the other player left the co-op game
onslaught-ce: network lobby: returning to the front end (the game stopped); the player is told: Your friend left the game.
onslaught-ce: session pid=101 t_ms=1304 P=0 game: frontend page=23 requested=12 transition_ticks=0
"""
GUEST_RING = GUEST.split("onslaught-ce: co-op tick 1000", 1)[0] + """\
onslaught-ce: network game: the ring opened (the battle goes on)
onslaught-ce: network game: the ring closed
onslaught-ce: network game: the ring opened (the battle goes on)
onslaught-ce: network game: this player left the game
onslaught-ce: co-op ended at tick 449; final state was not confirmed
onslaught-ce: co-op network: 475 messages sent, 475 received, 0 packets rejected
onslaught-ce: network lobby: the player left the network game
onslaught-ce: network lobby: returning to the front end (the game ended); the player is told: You left the game.
onslaught-ce: session pid=202 t_ms=1322 P=0 game: frontend page=23 requested=12 transition_ticks=0
"""
FILES = {"defaultoptions.bea": "a" * 64, "savegames/Career.bes": "b" * 64}


def verdict(host=None, guest=None, before=None, after=None, exits=None, ending="quit"):
    host = host if host is not None else {"leave": HOST_LEAVE, "ring": HOST_RING}.get(ending, HOST)
    guest = guest if guest is not None else {"leave": GUEST_LEAVE, "ring": GUEST_RING}.get(ending, GUEST)
    before = before or {"host": dict(FILES), "guest": dict(FILES)}
    after = after or {"host": dict(FILES), "guest": dict(FILES)}
    ok, rows = run.judge({"host": host, "guest": guest}, before, after, exits or {"host": 0, "guest": 0}, ending)
    # the first stage each role missed (the later ones are "not reached"), and every other failed row
    failed = sorted({(r["role"], r["stage"]) for r in rows if not r["ok"] and r.get("note") != "not reached"})
    return ok, failed


def without(text, needle):
    lines = [l for l in text.splitlines(keepends=True) if needle not in l]
    assert len(lines) == len(text.splitlines()) - 1, needle
    return "".join(lines)


def swap(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


CASES = [
    # (name, keyword arguments, the (role, stage) pairs that must fail; None: must pass)
    ("a whole game passes", {}, None),
    ("guest never joined", dict(guest=without(GUEST, "network lobby: joining")), [("guest", "joining")]),
    ("host never hosted", dict(host=without(HOST, "hosting on UDP port")), [("host", "hosting")]),
    ("start never agreed", dict(host=without(HOST, "start agreed")), [("host", "start agreed")]),
    ("another level started", dict(guest=swap(GUEST, "start agreed (level 856,", "start agreed (level 100,")), [("guest", "start agreed")]),
    ("the pad never played", dict(host=without(HOST, "gameplay pad script begins")), [("host", "pad playing")]),
    ("no mission end", dict(guest=without(GUEST, "co-op mission selected state confirmed")), [("both", "same final tick and state"), ("guest", "mission end")]),
    ("end not confirmed", dict(guest=swap(GUEST, "co-op mission selected state confirmed at tick 1584, hash 3ea4a8aec429ea66, 80 scheduled hashes compared",
                                          "co-op ended at tick 1584; final state was not confirmed")),
     [("both", "same final tick and state"), ("guest", "mission end"), ("guest", "trouble")]),
    ("different final ticks", dict(host=swap(HOST, "confirmed at tick 1584,", "confirmed at tick 1580,")), [("both", "same final tick and state")]),
    ("different final states", dict(guest=swap(GUEST, "hash 3ea4a8aec429ea66,", "hash 3ea4a8aec429ea67,")), [("both", "same final tick and state")]),
    ("out of step", dict(host=HOST.replace("co-op tick 1000 in step (50 hashes compared)\n",
                                            "co-op session stopped: the two games went out of step at tick 1020 (this game 0011223344556677, the other 7766554433221100)\n")),
     [("host", "trouble")]),
    ("stopped instead of ended", dict(guest=swap(GUEST, "(the game ended)", "(the game stopped)")), [("guest", "returned"), ("guest", "trouble")]),
    ("window closed in the game", dict(host=HOST + "onslaught-ce: network lobby: the window was closed: not returning to the front end\n"), [("host", "trouble")]),
    ("no front end after the return", dict(guest=GUEST.rsplit("onslaught-ce: session pid=202 t_ms=87990", 1)[0]), [("guest", "front end")]),
    ("returned before it started", dict(host=swap(HOST, "onslaught-ce: network lobby: returning to the front end (the game ended); the player is told: The network game has ended.\n", "")
                                         .replace("onslaught-ce: network lobby: state host_waiting\n",
                                                  "onslaught-ce: network lobby: returning to the front end (the game ended); the player is told: The network game has ended.\nonslaught-ce: network lobby: state host_waiting\n")),
     [("host", "returned")]),
    ("a career changed", dict(after={"host": dict(FILES), "guest": dict(FILES, **{"savegames/Career.bes": "c" * 64})}), [("guest", "careers and options unchanged")]),
    ("a career appeared", dict(after={"host": dict(FILES, **{"savegames/New.bes": "d" * 64}), "guest": dict(FILES)}), [("host", "careers and options unchanged")]),
    ("options lost", dict(after={"host": {"savegames/Career.bes": "b" * 64}, "guest": dict(FILES)}), [("host", "careers and options unchanged")]),
    ("nothing to compare", dict(before={"host": {}, "guest": dict(FILES)}, after={"host": {}, "guest": dict(FILES)}), [("host", "careers and options unchanged")]),
    ("a game crashed", dict(exits={"host": 0, "guest": -11}), [("guest", "closed normally")]),
    ("different start states", dict(guest=swap(GUEST, "start state d7eea2e52248c01e", "start state d7eea2e52248c01f")), [("both", "same start state")]),
    # --ending leave
    ("a leave passes", dict(ending="leave"), None),
    ("the host is not told", dict(ending="leave", host=without(HOST_LEAVE, "the other player left the co-op game\n")),
     [("host", "friend left")]),
    ("a desync instead of a leave", dict(ending="leave", host=swap(HOST_LEAVE, "co-op session stopped: the other player left the co-op game\n",
                                                                 "co-op session stopped: the two games went out of step at tick 400 (this game 0011223344556677, the other 7766554433221100)\n")),
     [("host", "friend left"), ("host", "trouble")]),
    ("the host does not return", dict(ending="leave", host=HOST_LEAVE.rsplit("onslaught-ce: session pid=101 t_ms=1611", 1)[0]),
     [("host", "front end")]),
    ("the guest's window did not close", dict(ending="leave", guest=without(GUEST_LEAVE, "the window was closed")), [("guest", "window closed")]),
    ("the host's window closed too", dict(ending="leave", host=HOST_LEAVE + "onslaught-ce: network lobby: the window was closed: not returning to the front end\n"),
     [("host", "trouble")]),
    ("a career changed in a leave", dict(ending="leave", after={"host": dict(FILES, **{"savegames/Career.bes": "c" * 64}), "guest": dict(FILES)}),
     [("host", "careers and options unchanged")]),
    ("a whole game judged as a leave", dict(ending="leave", host=HOST, guest=GUEST), [("guest", "window closed"), ("host", "friend left")]),
    ("a leave judged as a whole game", dict(host=HOST_LEAVE, guest=GUEST_LEAVE), [("guest", "mission end"), ("guest", "trouble"), ("host", "mission end"),
                                                                                ("host", "trouble"), ("both", "same final tick and state")]),
    # --ending ring
    ("a leave by the ring passes", dict(ending="ring"), None),
    ("the ring never opened", dict(ending="ring", guest=GUEST_RING.replace("onslaught-ce: network game: the ring opened (the battle goes on)\n", "")),
     [("guest", "ring opened")]),
    ("the ring did not close", dict(ending="ring", guest=without(GUEST_RING, "the ring closed")), [("guest", "ring closed")]),
    ("the ring opened once only", dict(ending="ring", guest=swap(GUEST_RING, "the ring closed\nonslaught-ce: network game: the ring opened (the battle goes on)\n",
                                                                  "the ring closed\n")), [("guest", "ring opened again")]),
    ("Yes did not leave", dict(ending="ring", guest=without(GUEST_RING, "this player left the game")), [("guest", "left by the ring")]),
    ("the leaver is not told", dict(ending="ring", guest=swap(GUEST_RING, "the player is told: You left the game.", "the player is told: The network game has ended.")),
     [("guest", "told they left")]),
    ("out of step while the ring was open", dict(ending="ring", host=swap(HOST_RING, "co-op session stopped: the other player left the co-op game\n",
                                                                         "co-op session stopped: the two games went out of step at tick 300 (this game 0011223344556677, the other 7766554433221100)\n")),
     [("host", "friend left"), ("host", "trouble")]),
    ("the leaver's window closed", dict(ending="ring", guest=GUEST_RING.rsplit("onslaught-ce: session pid=202 t_ms=1322", 1)[0] +
                                        "onslaught-ce: network lobby: the window was closed: not returning to the front end\n"),
     [("guest", "front end"), ("guest", "trouble")]),
    ("a career changed in a ring leave", dict(ending="ring", after={"host": dict(FILES), "guest": dict(FILES, **{"savegames/Career.bes": "c" * 64})}),
     [("guest", "careers and options unchanged")]),
    ("a window-close leave judged as a ring leave", dict(ending="ring", host=HOST_LEAVE, guest=GUEST_LEAVE), [("guest", "ring opened"), ("guest", "trouble")]),
]


def script_checks():
    """The key scripts: well-formed tokens, every press released, the routes as the pages expect them."""
    bad = []
    token = re.compile(r"^(?:(\d+):(\d+):([0-9a-f]+):([du])|a(\d+):(\d+):(\d+)|b(\d+):1:([du]):(\d+):(\d+))$")
    for name, script in (("host", run.host_script()),
                         ("guest", run.guest_script("0123456789ABCDEFGHJKMNP")), ("ring", run.ring_script())):
        down = {}
        last = -1
        for t in script.split(","):
            m = token.match(t)
            if not m:
                bad.append("%s: token %r is not a key, pointer or button event" % (name, t))
                continue
            present = int(next(g for g in (m.group(1), m.group(5), m.group(8)) if g is not None))
            if present < last:
                bad.append("%s: token %r is out of Present order" % (name, t))
            last = present
            key = ("k", m.group(2)) if m.group(1) else ("b", "1") if m.group(8) else None
            edge = m.group(4) or m.group(9)
            if key and edge == "d":
                down[key] = present
            elif key and edge == "u":
                if key not in down or present - down.pop(key) > 3600:
                    bad.append("%s: %r releases what was not pressed within 3600 Presents" % (name, t))
        if down:
            bad.append("%s: pressed and never released: %r" % (name, down))
    keys_of = lambda s: [t.split(":")[1] for t in s if t[0].isdigit() and t.endswith(":d")]
    host = run.host_script().split(",")
    if keys_of(host) != ["40"] + ["81"] * 5 + ["40", "40", "40"]:
        bad.append("the host does not go title, Onslaught CE, Network Game, Host Game: %r" % keys_of(host))
    clicks = [t for t in host if t.startswith("b") and t.split(":")[2] == "d"]
    if not clicks or any(t.split(":")[3:] != [str(run.START_ROW[0]), str(run.START_ROW[1])] for t in clicks):
        bad.append("the host's clicks are not on the lobby's Start row")
    if not 295 <= run.START_ROW[1] < 315:
        bad.append("the Start click is not on the lobby's Start row (y 295 to 315), above the host page's rows (y 335 on)")
    guest = keys_of(run.guest_script("0123456789ABCDEFGHJKMNP").split(","))
    if guest[-1] != "40" or len(guest) != 1 + len(run.TO_NETWORK) + len(run.JOIN_ROUTE) + 23 + 1:
        bad.append("the guest does not type the code and press Enter once: %r" % guest[-3:])
    ring = keys_of(run.ring_script().split(","))
    if ring != ["41", "41", "41", "81", "40", "81", "40"]:
        bad.append("the ring route is not Escape, Escape, Escape, Down, Enter, Down, Enter: %r" % ring)
    for code in ("", "0123-4567", "abc", "0123456789ABCDEFGHJKMNU"):
        try:
            run.guest_script(code)
            bad.append("the guest script took %r as a code" % code)
        except ValueError:
            pass
    return bad


def main():
    results = []

    def report(ok, label):
        print(("PASS " if ok else "FAIL ") + label, flush=True)
        results.append(bool(ok))

    for name, kw, want in CASES:
        ok, failed = verdict(**kw)
        if want is None:
            report(ok and not failed, "%s (failed: %r)" % (name, failed) if failed else name)
        else:
            report(not ok and failed == sorted(want), "%s: fails at %r" % (name, failed))
    # The mouse route has its own control: an off-row release does nothing, No
    # keeps the battle live, and the next fresh click on Yes finally leaves.
    mouse_lines = GUEST_RING.splitlines()
    opened = next(line for line in mouse_lines if "the ring opened" in line)
    start = GUEST_RING.index(opened)
    last = GUEST_RING.index(run.RING + "this player left the game", start)
    observed = ["click released outside its row", "leave confirmation opened",
                "leave confirmation cancelled", "leave confirmation opened"]
    fixture = GUEST_RING[:start] + opened + "\n" + "".join(run.RING + step + "\n" for step in observed) + GUEST_RING[last:]
    def mouse_verdict(guest):
        return verdict(host=HOST_RING, guest=guest, ending="mouse")[0]
    report(mouse_verdict(fixture), "mouse: drag out, decline, then confirm")
    report(not mouse_verdict(fixture.replace(run.RING + observed[0] + "\n", "", 1)),
           "mouse: missing cancelled drag fails")
    report(not mouse_verdict(fixture.replace(run.RING + observed[2] + "\n", "", 1)),
           "mouse: missing No fails")
    report(not mouse_verdict(fixture.replace(run.RING + observed[1], run.RING + observed[1] + "\n" + run.RING + observed[1], 1)),
           "mouse: one click activating two rows fails")
    def rematch_log(text, role):
        initial = []
        for name, roles, rx in run.STAGES[2:6]:
            if role in roles:
                initial.append(next(line for line in text.splitlines() if rx.search(line)))
        return text.rstrip() + "\n" + run.LOBBY + "returning to the same lobby as " + role + "\n" + "\n".join(initial) + "\n"
    again_host, again_guest = rematch_log(HOST, "host"), rematch_log(GUEST, "guest")
    def rematch_ok(h, g):
        return verdict(host=h, guest=g, ending="rematch")[0]
    report(rematch_ok(again_host, again_guest), "rematch: both play two starts")
    report(not rematch_ok(HOST, again_guest), "rematch: one-sided return fails")
    report(not rematch_ok(again_host, GUEST), "rematch: ordinary return is not a second start")
    mismatch = list(run.START_STATE.finditer(again_guest))[-1]
    which = mismatch.start(1)
    changed = again_guest[:which] + ("0" if again_guest[which] != "0" else "1") + again_guest[which+1:]
    report(not rematch_ok(again_host, changed), "rematch: mismatched second start fails")
    report(not verdict(host=HOST_RING + "\n" + run.LOBBY + "returning to the same lobby as host\n",
                       guest=GUEST_RING, ending="ring")[0], "departure must never reopen the lobby")
    window_close = run.LOBBY + "the window was closed: not returning to the front end"
    report(not rematch_ok(window_close + "\n" + again_host, again_guest), "early close is still a failure")
    report(rematch_ok(again_host + window_close + "\n", again_guest), "controlled close after second play")
    bad = script_checks()
    report(not bad, "key scripts are well formed and follow the menu route")
    for b in bad:
        print("   FAIL " + b)
    label = run.stage_of(HOST, "host")
    report(label == "front end", "a full host log is labelled with its last stage (%s)" % label)
    report(run.stage_of(HOST.split("co-op started", 1)[0], "host") == "start agreed", "a log cut before the game is labelled start agreed")
    report(not run.CODE_LIKE.search("onslaught-ce: co-op tick 1000 in step (50 hashes compared)") and
           run.CODE_LIKE.search("PZD2-SP19-RDBH-R2M0-1E3C-0G8") is not None, "join codes are recognised for redaction and log lines are not")
    ok = all(results)
    print("co-op pair runner: " + ("OK" if ok else "FAILED"), "(%d checks)" % len(results))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
