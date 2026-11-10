#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The lockstep start deadline survives a slow peer, the transport admits it, and a player who typed a wrong invite is told where to look.

Two co-op sessions on a loaded machine ended 'the other player did not connect in time' with nothing wrong on the network: the peers load their levels independently
(45 s apart there), and each peer's deadline runs from the end of its own load. kStartMs, which sets that deadline and every start barrier, is therefore three
minutes; the transport refuses a handshake window beyond its own maximum (CE_NetCreate then returns nothing and the session fails to start), so the two numbers must
stay in step. A wrong invite secret looks like an absent peer (the host answers the guest's hello, the guest cannot verify the answer and refuses it, and the host never receives
the guest's proof), so the guest's verdict names the invite code and the host waiting; the host's names how many packets it refused when it refused any (measured: none in
the wrong-invite case). The in-game silence limit is not part of this: it stays 30 s.
A difference in the start messages (level, delay, hash spacing, end tick, pre-run flags) is named on both sides: the peer that finds it sends its own start message before it leaves.

The suite reads the sources, checks each requirement and shows with mutants of the numbers and the text that each can fail. No game data, device or network is used.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCKSTEP = (ROOT / "port/game/ce_network_lockstep.cpp").read_text(encoding="utf-8")
TRANSPORT = (ROOT / "port/network/transport.cpp").read_text(encoding="utf-8")

MIN_START_MS = 120000


def check(good, name, results):
    print(("PASS " if good else "FAIL ") + name)
    results.append(bool(good))


def start_ms(text):
    m = re.search(r"\bkStartMs\s*=\s*(\d+)", text)
    return int(m.group(1)) if m else None


def give_up_ms(text):
    m = re.search(r"\bkGiveUpMs\s*=\s*(\d+)", text)
    return int(m.group(1)) if m else None


def transport_max(text):
    m = re.search(r"c->handshake_ms\s*>\s*(\d+)", text)
    return int(m.group(1)) if m else None


def deadline_ok(lockstep, transport):
    start, top = start_ms(lockstep), transport_max(transport)
    return start is not None and top is not None and MIN_START_MS <= start <= top


def hints_ok(text):
    guest = re.search(r'L\.host \? "the other player did not connect in time" :\s*"the other player did not connect in time \(no answer from the host: check the invite code and that the host is waiting\)"', text)
    host = re.search(r'L\.host && st\.rejected_packets\)\s*snprintf\(connect_why, sizeof connect_why, "the other player did not connect in time \(%u packets were refused: check the invite code\)"', text)
    wired = re.search(r"\}, kStartMs, connect_why\)\) return 0;", text)
    # a transport that gives up before the wait's own deadline says the same (the two timers are 180 s each and which fires first is a matter of milliseconds)
    same = re.search(r"if \(!L\.connection_established && L\.stage == STARTING && connect_why\[0\]\) return Fail\(connect_why\);\s*return Fail\(status\.error == CE_NET_TIMEOUT", text)
    return bool(guest and host and wired and same)


def mismatch_named_both_ways(text):
    """Each start-message difference is preceded by this game's own start message when it has not gone out, so the other player compares and names it too."""
    first = re.search(r'if \(level != L\.level \|\| delay != L\.delay \|\| every != L\.hash_every \|\| end != L\.quit_at\) \{\s*if \(!L\.ready_sent\) SendReady\(\);\s*'
                      r'return Fail\("the other player started a different level or co-op setting"\);\s*\}', text)
    second = re.search(r'if \(memcmp\(digest,L\.pre_run_digest,sizeof digest\)!=0\) \{\s*if \(!L\.ready_sent\) SendReady\(\);\s*'
                       r'return Fail\("the two games have different pre-run career flags"\);\s*\}', text)
    marked = re.search(r"if \(w\.error \|\| !Send\(b, w\.size, 0\)\) return false;\s*L\.ready_sent = true;", text)
    return bool(first and second and marked)


def differences_named(text):
    """A peer whose files, build or level differ is told so by both players, and 'theirs' is only said when the two labels differ."""
    guest_labels = re.search(r'if \(peer\[0\] && strcmp\(peer, L\.config\.version\)\)\s*snprintf\(text, sizeof text, "the other player has a different Onslaught CE version \(yours %s, theirs %s\)", L\.config\.version, peer\);', text)
    guest_equal = re.search(r'else\s*snprintf\(text, sizeof text, "the other player\'s game files, Onslaught CE build or level differ from yours \(both say %s\)", L\.config\.version\);', text)
    host = re.search(r'if \(L\.host && st\.error == CE_NET_INCOMPATIBLE\)[^\n]*\n\s*snprintf\(connect_why, sizeof connect_why, "the other player did not connect in time \(a game whose files, Onslaught CE build or level differ tried to join\)"\);', text)
    return bool(guest_labels and guest_equal and host)


def main():
    results = []
    check(deadline_ok(LOCKSTEP, TRANSPORT), "kStartMs (%s ms) is at least %d ms and within the transport's handshake maximum (%s ms)" %
          (start_ms(LOCKSTEP), MIN_START_MS, transport_max(TRANSPORT)), results)
    check(give_up_ms(LOCKSTEP) == 30000, "the in-game silence limit stays 30 s (%s ms)" % give_up_ms(LOCKSTEP), results)
    check(hints_ok(LOCKSTEP), "the guest's verdict names the invite code and the host waiting; the host's names the refused packets; the wait uses them", results)
    check(mismatch_named_both_ways(LOCKSTEP), "a start-message difference is preceded by this game's own start message, so both players name it", results)
    check(differences_named(LOCKSTEP), "a difference in files, build or level is named to the guest (labels only when they differ) and to the host", results)

    # mutants
    mutated = re.sub(r"kStartMs\s*=\s*\d+", "kStartMs = 60000", LOCKSTEP, count=1)
    check(not deadline_ok(mutated, TRANSPORT), "mutant: a one-minute start deadline is caught", results)
    mutated = re.sub(r"c->handshake_ms\s*>\s*\d+", "c->handshake_ms > 60000", TRANSPORT, count=1)
    check(not deadline_ok(LOCKSTEP, mutated), "mutant: a transport maximum below the start deadline is caught", results)
    mutated = re.sub(r"kStartMs\s*=\s*\d+", "kStartMs = 900000", LOCKSTEP, count=1)
    check(not deadline_ok(mutated, TRANSPORT), "mutant: a start deadline beyond the transport maximum is caught", results)
    check(not hints_ok(LOCKSTEP.replace("check the invite code and that the host is waiting", "try again")), "mutant: a guest verdict without the hint is caught", results)
    check(not hints_ok(LOCKSTEP.replace("packets were refused", "packets")), "mutant: a host verdict without the refused count is caught", results)
    check(not hints_ok(LOCKSTEP.replace("}, kStartMs, connect_why)) return 0;", '}, kStartMs, "the other player did not connect in time")) return 0;')),
          "mutant: a wait that does not use the hints is caught", results)
    check(not hints_ok(LOCKSTEP.replace("if (!L.connection_established && L.stage == STARTING && connect_why[0]) return Fail(connect_why);\n", "", 1)),
          "mutant: a transport that gives up before the wait's deadline and says something else is caught", results)
    check(not mismatch_named_both_ways(LOCKSTEP.replace("if (!L.ready_sent) SendReady();\n\t\t\t\treturn Fail(\"the other player started", "return Fail(\"the other player started", 1)),
          "mutant: a level or setting difference that does not send this game's start message first is caught", results)
    check(not mismatch_named_both_ways(LOCKSTEP.replace("if (!L.ready_sent) SendReady();\n\t\t\t\treturn Fail(\"the two games have different", "return Fail(\"the two games have different", 1)),
          "mutant: a pre-run flag difference that does not send this game's start message first is caught", results)
    check(not mismatch_named_both_ways(LOCKSTEP.replace("L.ready_sent = true;", "", 1)), "mutant: a start message that is never marked as sent is caught", results)
    check(not differences_named(LOCKSTEP.replace("(theirs: %s)", "(theirs: %s", 1).replace("both say %s", "both say", 1)), "mutant: a guest text that loses its equal-labels form is caught", results)
    check(not differences_named(LOCKSTEP.replace("if (L.host && st.error == CE_NET_INCOMPATIBLE)", "if (false)", 1)), "mutant: a host that never names the difference is caught", results)
    check(not differences_named(LOCKSTEP.replace("if (peer[0] && strcmp(peer, L.config.version))", "if (peer[0])", 1)), "mutant: a guest that says 'theirs' for equal labels is caught", results)

    ok = all(results)
    print("lockstep start-deadline controls: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
