#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The lobby page's exact words for a refused or failed session, with mutants.

    python3 port/tools/network_lobby_notice_tests.py

port/platform/common/ce_netlobby.cpp is compiled whole, with only the transport faked (port/tools/tests/network_lobby_notice_driver.cpp: a phase, an error and a peer version the test sets), and
driven into each failure a player can meet on the lobby and Join pages: another version label or another build (host and guest side), a wrong code or a host that does not answer, failed
attempts and the new code after five, a host or guest that leaves, a port that is taken, a lobby that is not available, a start that was not answered or could not be sent or written, and a
code or an address that cannot be joined. Each scenario compares what the page would show with the words a player reads; the words are product text, so a change of wording is a change of this
test. Each mutant changes one word, label or condition of the lobby (or of the join-code problems in ce_netcode.cpp) and a named scenario must fail.
No game, window, network or player data: an authored scratch folder.
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

LOBBY = ROOT / "port/platform/common/ce_netlobby.cpp"
NETCODE = ROOT / "port/platform/common/ce_netcode.cpp"
DRIVER = ROOT / "port/tools/tests/network_lobby_notice_driver.cpp"
COMMON = ROOT / "port/platform/common"
NETWORK = ROOT / "port/network"
SHA = ROOT / "port/updater/sha256.c"
SCENARIOS_EXPECTED = 40    # checks the driver prints against the lobby (a fall below it is a scenario that stopped running)

# (name, file, text in that file, replacement, a check of the driver that must fail)
MUTANTS = [
    ("host-version-branch-lost", "lobby", "if (versions && strcmp(peer, kVersion))\n\t\t\tsnprintf(L.st.notice, sizeof L.st.notice, \"A player with Onslaught CE %s",
     "if (false)\n\t\t\tsnprintf(L.st.notice, sizeof L.st.notice, \"A player with Onslaught CE %s", "A1 host: a joiner with another version label"),
    ("host-same-build-wording", "lobby", "\"A player with another Onslaught CE build (both say %s) tried to join: both players need the same download.\"", "\"Another build tried to join.\"",
     "A1 host: a joiner with the same label"),
    ("host-failed-count-stuck", "lobby", "\t\tL.st.failed++;\n", "", "A4 host: attempts 1 to 4"),
    ("host-count-wording", "lobby", "\"A connection attempt failed (%d of %d). A wrong code, or the player gave up.\"", "\"A connection attempt failed.\"", "A4 host: attempts 1 to 4"),
    ("host-no-new-code", "lobby", "if (L.st.failed >= kMaxFailed)", "if (false)", "A4 host: the fifth failed attempt"),
    ("host-new-code-wording", "lobby", "\"Five attempts failed, so this game has a new code. Give the other player the new code.\"", "\"New code.\"", "A4 host: the fifth failed attempt"),
    ("host-left-silent", "lobby", "Text(L.st.notice, sizeof L.st.notice, \"The other player left.\");", "L.st.notice[0] = 0;", "A6 host: the other player leaving"),
    ("host-listen-wording", "lobby", "\"This PC could not listen on UDP port %u: is another game using it?\"", "\"This PC could not listen on UDP port %u.\"", "A5 host: a taken port"),
    ("host-relisten-wording", "lobby", "\"This PC could not listen for players again.\"", "\"Could not listen.\"", "A5 host: when it cannot listen again"),
    ("host-bind-wording", "lobby", "\"BEA_NET_LOBBY_BIND is not an IPv4 address.\"", "\"Bad bind address.\"", "A5 host: a bind address"),
    ("host-launcher-wording", "lobby", "\"Network games need Onslaught CE started from its launcher (onslaught-ce), which names the game and \"", "\"Network games need the launcher and \"",
     "A5 host: a game not started from the launcher"),
    ("host-test-session-wording", "lobby", "\"Network game is unavailable while a co-op test session is selected.\"", "\"Unavailable.\"", "A5 host: a co-op test session"),
    ("host-no-answer-wording", "lobby", "\"The other player did not answer the start. Try Start again.\"", "\"No answer.\"", "A7 host"),
    ("host-go-refused-wording", "lobby", "\"The other player could not be told to start. Try Start again.\"", "\"Start failed.\"", "A9 host: a GO the transport refused"),
    ("host-start-file-wording", "lobby", "\"The game could not be started: its private start file could not be written.\"", "\"The game could not be started.\"",
     "A9 host: a start file that cannot be written is told"),
    ("host-start-file-relisten-wording", "lobby", "\"The game could not be started, and this PC could not listen again.\"", "\"The game could not be started.\"",
     "A9 host: a start file that cannot be written and a port"),
    ("guest-labels-swapped", "lobby", "\"Different Onslaught CE version: yours %s, theirs %s\", kVersion, peer", "\"Different Onslaught CE version: yours %s, theirs %s\", peer, kVersion",
     "A2 guest: another version label"),
    ("guest-same-build-wording", "lobby", "\"Different Onslaught CE build (both say %s): both need the same download\"", "\"Different build.\"", "A2 guest: the same label"),
    ("guest-no-answer-wording", "lobby", "\"No game answered at %s, port %u. The game cannot tell why:", "\"Nobody answered at %s, port %u. The game cannot tell why:", "A3 guest"),
    ("guest-no-answer-claims-a-cause", "lobby", "The game cannot tell why: the code or address may be wrong, the host may not be waiting", "The code or address is wrong, the host is not waiting", "A3 guest"),
    ("guest-no-answer-port-once", "lobby", "here, (unsigned)L.port, (unsigned)L.port);", "here, (unsigned)L.port, 0u);", "A3 guest"),
    ("guest-no-answer-pointer-lost", "lobby", " See \" CE_LOBBY_HELP_LABEL \".\", here,", "\", here,", "A3 guest"),
    ("host-nobody-wording", "lobby", "\"Nobody has reached this PC yet. The game cannot tell why:", "\"Nobody came. The game cannot tell why:", "A15 host: after 45 seconds"),
    ("host-nobody-claims-a-cause", "lobby", "wrongly, or a router or firewall may be blocking UDP port %u. See ", "wrongly, or a router or firewall is blocking UDP port %u. See ", "A15 host: after 45 seconds"),
    ("host-nobody-too-early", "lobby", "const uint64_t kNobodyWaitMs = 45000;", "const uint64_t kNobodyWaitMs = 20000;", "A15 host: after 45 seconds"),
    ("host-nobody-never", "lobby", "const uint64_t kNobodyWaitMs = 45000;", "const uint64_t kNobodyWaitMs = 4500000;", "A15 host: after 45 seconds"),
    ("host-nobody-over-a-failed-attempt", "lobby", "!L.nobodyTold && !L.st.failed && !L.st.notice[0] && now", "!L.nobodyTold && !L.st.notice[0] && now", "A15 host: a host whose attempt failed"),
    ("host-nobody-note-stays", "lobby", "if (L.nobodyTold)", "if (false)", "A15 host: a player who starts connecting"),
    ("guest-left-closed-swapped", "lobby", "was ? \"The host left the game.\" : \"The host closed the game.\"", "was ? \"The host closed the game.\" : \"The host left the game.\"",
     "A6 guest: the host leaving a lobby the guest was ready in"),
    ("guest-no-go-wording", "lobby", "\"The host did not start the game. Wait for the host to try again.\"", "\"Waiting.\"", "A8 guest"),
    ("join-address-wording", "lobby", "\"The address must be four numbers with dots, for example 203.0.113.5.\"", "\"Bad address.\"", "A14 join: an address that is not four numbers"),
    ("join-port-wording", "lobby", "\"The address's port must be a number from 1 to 65535.\"", "\"Bad port.\"", "A14 join: an address with a port"),
    ("join-setup-wording", "lobby", "\"The connection could not be set up on this PC.\"", "\"Failed.\"", "A14 join: a connection that cannot be set up"),
    ("code-empty-wording", "netcode", "\"Type or paste the host's code.\"", "\"Empty.\"", "A14 join: an empty code"),
    ("code-short-wording", "netcode", "\"The code is too short: it has 23 letters and digits.\"", "\"Short.\"", "A14 join: a short code"),
    ("code-typo-wording", "netcode", "\"The code has a typo: check it with the host.\"", "\"Typo.\"", "A14 join: a mistyped code"),
    ('auth_failure_maps_no_answer', 'lobby', 'CE_NET_FAILED', '(CE_NET_FAILED + 1000)', 'silent-host-auth'),
    ("host-disconnect-called-leave", "lobby",
     'if (s.error == CE_NET_PEER_LEFT && s.phase == CE_NET_CLOSED)\n\t\t{\n\t\t\tText(L.st.notice',
     'if (s.error == CE_NET_PEER_LEFT)\n\t\t{\n\t\t\tText(L.st.notice',
     "A16 host: raw disconnect keeps a listening lobby and reports connection loss"),
    ("host-unconfirmed-close-called-leave", "lobby",
     'if (s.error == CE_NET_PEER_LEFT && s.phase == CE_NET_CLOSED)\n\t\t{\n\t\t\tText(L.st.notice',
     'if (s.phase == CE_NET_CLOSED)\n\t\t{\n\t\t\tText(L.st.notice',
     "A16 host: unconfirmed close keeps a listening lobby and reports connection loss"),
    ("guest-disconnect-called-leave", "lobby",
     'if (s.error == CE_NET_PEER_LEFT && s.phase == CE_NET_CLOSED)\n\t{\n\t\tL.state = CE_LOBBY_LEFT;',
     'if (s.error == CE_NET_PEER_LEFT)\n\t{\n\t\tL.state = CE_LOBBY_LEFT;',
     "A16 guest: raw disconnect retires the session and reports connection loss"),
    ("guest-unconfirmed-close-called-leave", "lobby",
     'if (s.error == CE_NET_PEER_LEFT && s.phase == CE_NET_CLOSED)\n\t{\n\t\tL.state = CE_LOBBY_LEFT;',
     'if (s.phase == CE_NET_CLOSED)\n\t{\n\t\tL.state = CE_LOBBY_LEFT;',
     "A16 guest: unconfirmed close retires the session and reports connection loss"),
    ("guest-loss-marked-left", "lobby",
     '\tif (was)\n\t{\n\t\tL.state = CE_LOBBY_FAILED;',
     '\tif (was)\n\t{\n\t\tL.state = CE_LOBBY_LEFT;',
     "A16 guest: raw disconnect retires the session and reports connection loss"),
    ("host-loss-no-listener", "lobby",
     'L.waitingAt = CE_NetNowMs();\n\tL.nobodyTold = false;\n\tif (!Create())',
     'L.waitingAt = CE_NetNowMs();\n\tL.nobodyTold = false;\n\tif (false)',
     "A16 host: raw disconnect keeps a listening lobby and reports connection loss"),
    ("guest-loss-keeps-transport", "lobby", 'bool was = L.state == CE_LOBBY_READY;\n\tClose(false);',
     'bool was = L.state == CE_LOBBY_READY;',
     "A16 guest: raw disconnect retires the session and reports connection loss"),
]


def compile_driver(out, name, lobby_text, netcode_text, sanitize=False):
    cxx = shutil.which(os.environ.get("CXX", "clang++")) or shutil.which("g++")
    cc = shutil.which(os.environ.get("CC", "clang")) or shutil.which("gcc")
    sha = out / "sha256.o"
    if not sha.exists():
        subprocess.run([cc, "-c", "-O1", "-o", str(sha), str(SHA)], check=True)
    folder = out / name
    folder.mkdir()
    lobby = folder / "ce_netlobby.cpp"
    lobby.write_text(lobby_text.replace('"../../network/', '"'))
    netcode = folder / "ce_netcode.cpp"
    netcode.write_text(netcode_text.replace('"../../network/', '"'))
    exe = folder / "test"
    sdl = build.deps.PREFIX / "include"
    cmd = [cxx, "-std=c++17", "-Wall", "-Wextra", "-O1", "-g", *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitize else []), "-Wno-unused-function",
           "-DCE_LOBBY_NOTICE_STANDALONE=1",
           '-DCE_NETWORK_SIMULATION_ID="authored-lobby-notice-test"', '-DLOBBY_SOURCE="%s"' % lobby.as_posix(),
           "-I", str(COMMON), "-I", str(NETWORK), "-I", str(sdl), str(DRIVER), str(netcode), str(NETWORK / "codec.cpp"), str(sha), "-o", str(exe)]
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    (folder / "build.log").write_text(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(name + " did not compile; see " + str(folder / "build.log"))
    return exe


def run(exe, scratch):
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    env = {"PATH": os.environ.get("PATH", "/usr/bin"), "LANG": "C", "LOBBY_TEST_SCRATCH": str(scratch), "ASAN_OPTIONS": "detect_leaks=0"}
    # the restart's arguments are this process's own: the lobby reads them from /proc/self/cmdline and the start file accepts these
    return subprocess.run([str(exe), "-skipfmv", "-forcewindowed", "-res", "640", "480"], capture_output=True, text=True, timeout=120, env=env)


def lines(trial, prefix):
    return [l[len(prefix):] for l in trial.stdout.splitlines() if l.startswith(prefix)]


def main():
    lobby_text = LOBBY.read_text(encoding="utf-8")
    netcode_text = NETCODE.read_text(encoding="utf-8")
    parent = ROOT / "local-data/port/network-lobby-notice-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    results = []

    def report(ok, label):
        print(("PASS " if ok else "FAIL ") + label, flush=True)
        results.append(bool(ok))

    exe = compile_driver(out, "actual", lobby_text, netcode_text, sanitize=True)
    trial = run(exe, out / "actual-scratch")
    (out / "actual" / "result.log").write_text(trial.stdout + trial.stderr)
    passed, failed = lines(trial, "PASS "), lines(trial, "FAIL ")
    report(trial.returncode == 0 and not failed and len(passed) >= SCENARIOS_EXPECTED,
           "the lobby page's words: every scenario passes (%d checks, %d failures)" % (len(passed), len(failed)))
    for line in failed:
        print("   FAIL " + line)

    for name, target, needle, replacement, expected in MUTANTS:
        text = lobby_text if target == "lobby" else netcode_text
        if text.count(needle) != 1:
            raise RuntimeError("mutation is not unique (%d found): %s" % (text.count(needle), name))
        mutated = text.replace(needle, replacement, 1)
        trial = run(compile_driver(out, name, mutated if target == "lobby" else lobby_text, mutated if target == "netcode" else netcode_text), out / (name + "-scratch"))
        (out / name / "result.log").write_text(trial.stdout + trial.stderr)
        report(trial.returncode != 0 and any(expected in line for line in lines(trial, "FAIL ")), "mutant %s is caught (%s)" % (name, expected))

    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    (out / "result.txt").write_text("\n".join("%s %s" % (path.relative_to(ROOT), sha(path)) for path in (LOBBY, NETCODE, DRIVER)) + "\n%d checks, %s\n" % (len(results), "OK" if all(results) else "FAILED"))
    print("lobby notice controls: " + ("OK" if all(results) else "FAILED"), "(evidence: %s)" % out)
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
