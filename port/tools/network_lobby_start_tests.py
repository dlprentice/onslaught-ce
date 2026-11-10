#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The lobby's start handshake never starts one player alone, with mutants and the lobby as it was before the fix.

    python3 port/tools/network_lobby_start_tests.py

port/platform/common/ce_netlobby.cpp is compiled whole, with only the transport faked (port/tools/tests/network_lobby_start_driver.cpp: sends that can be refused, a scripted inbox, a
phase and a clock), and driven through one role at a time against a scripted peer. The handshake is START (host), ACK (guest), GO (host): the host writes its start file and queues GO
only after the guest's acknowledgement has reached it and leaves gracefully so that GO is delivered; the guest starts on GO and on nothing else. The scenarios are the failures that an
authored lobby control found in the handshake before GO existed (the guest restarted 400 ms after sending its acknowledgement whether or not the host had it): an acknowledgement refused
or delayed, a transport failure, the host leaving, and the deadlines on both sides (the guest's wait must outlast the host's plus the answer's way back: a GO 3.5 s after the START starts the
guest, one 5.5 s after it is ignored). Each mutant puts one piece of the fix back and a named scenario must fail; and the scenarios built
against the lobby of the commit before the fix (read from git when that commit is present) must fail.
No game, window, network or player data: an authored scratch folder.
"""
from network_lobby_restart_handoff_tests import function as source_function
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
DRIVER = ROOT / "port/tools/tests/network_lobby_start_driver.cpp"
COMMON = ROOT / "port/platform/common"
NETWORK = ROOT / "port/network"
SHA = ROOT / "port/updater/sha256.c"
# the lobby of the commit before the GO message (the fenced hub head): the scenarios must fail against it
BEFORE_FIX = "42d67693301922e0079ffe3069972c5ab1a6a255"
SCENARIOS_EXPECTED = 42    # checks the driver prints against the fixed lobby (a fall below it is a scenario that stopped running)

# (name, text in ce_netlobby.cpp, replacement, a check of the driver that must fail)
# (A host that reads a queued acknowledgement while the transport reports the guest lost is not a mutant: the transport refuses sends on a lost session, so GO cannot be queued and
# BeginRestart ends as it should; it is left out because no scenario can tell it from the fix.)
_cleanup_source = LOBBY.read_text()
_cleanup_start, _cleanup_end = source_function(_cleanup_source, "BeginRestart")
cleanup_anchor = _cleanup_source[_cleanup_start:_cleanup_end]
if cleanup_anchor.count("ClearPendingHandoff();") != 1:
    raise RuntimeError("unsent-start cleanup anchor changed")
if cleanup_anchor.count("\tif (!ok)\n") != 1:
    raise RuntimeError("start-file failure guard changed")

MUTANTS = [
    ("guest-restarts-after-drain", "SendAck();\n\t\tif (now - L.startAt > kGuestWaitMs)",
     "SendAck();\n\t\tif (now - L.startAt > 400)\n\t\t\tBeginRestart();\n\t\tif (now - L.startAt > kGuestWaitMs)", "the guest waits"),
    ("ack-refusal-ignored", 'if (L.net && L.startSent && !L.ackQueued && Send(L.campaign ? MSG_CAMPAIGN_ACK : MSG_ACK, L.nonce, 16))\n\t\tL.ackQueued = true;',
     'if (L.net && L.startSent && !L.ackQueued)\n\t{\n\t\tSend(L.campaign ? MSG_CAMPAIGN_ACK : MSG_ACK, L.nonce, 16);\n\t\tL.ackQueued = true;\n\t}', "the refused acknowledgement is asked again"),
    ("guest-go-number-unchecked", "if (!memcmp(nonce, L.nonce, 16))\n\t\t\t{\n\t\t\t\tBeginRestart();\t\t\t\t// the host has received the acknowledgement",
     "if (true)\n\t\t\t{\n\t\t\t\tBeginRestart();\t\t\t\t// the host has received the acknowledgement", "GO with another start number is ignored"),
    ("guest-go-before-ack", 'type == (L.campaign ? MSG_CAMPAIGN_GO : MSG_GO) && !L.host && L.startSent && L.ackQueued && m.size', 'type == (L.campaign ? MSG_CAMPAIGN_GO : MSG_GO) && !L.host && L.startSent && m.size',
     "GO before the acknowledgement is queued"),
    ("guest-closed-is-start", "\t\t\t}\n\t\t\tGuestFailure(s);", "\t\t\t}\n\t\t\tif (L.startSent)\n\t\t\t{\n\t\t\t\tBeginRestart();\n\t\t\t\treturn;\n\t\t\t}\n\t\t\tGuestFailure(s);",
     "the host leaving without GO is a failure"),
    ("guest-loss-judged-before-reading", "if (L.startSent && L.ackQueued)\n\t\t\t{\n\t\t\t\tReceive();", "if (false)\n\t\t\t{\n\t\t\t\tReceive();",
     "GO that arrived before the host's leave"),
    ("guest-no-deadline", "if (now - L.startAt > kGuestWaitMs)", "if (false)", "an acknowledgement refused until the deadline"),
    ("guest-newer-start-ignored", '(type == MSG_START || type == MSG_CAMPAIGN_START) && !L.host && m.size == 5 + 4 + 16 && L.state == CE_LOBBY_READY)',
     '(type == MSG_START || type == MSG_CAMPAIGN_START) && !L.host && m.size == 5 + 4 + 16 && L.state == CE_LOBBY_READY && !L.startSent)', "a newer START replaces the open attempt"),
    ("guest-number-kept", "\t\t\tmemcpy(L.nonce, nonce, 16);\t\t// a newer START replaces", "\t\t\t// a newer START replaces", "the newer attempt's GO"),
    ("host-sends-no-go", 'if (L.host && !Send(L.campaign ? MSG_CAMPAIGN_GO : MSG_GO, L.nonce, 16))', 'if (false)', "a valid acknowledgement is answered with one GO"),
    ("host-go-refusal-ignored", 'if (L.host && !Send(L.campaign ? MSG_CAMPAIGN_GO : MSG_GO, L.nonce, 16))', 'if (L.host && (Send(L.campaign ? MSG_CAMPAIGN_GO : MSG_GO, L.nonce, 16), false))', "GO refused by the transport"),
    ("host-writes-no-start-file-check", cleanup_anchor,
     cleanup_anchor.replace("\tif (!ok)\n", "\tif (!ok && !L.host)\n", 1), "the start file cannot be written"),
    ("host-leaves-without-flush", "Close(L.host);", "Close(false);", "GO is queued first, then the lobby leaves gracefully"),
    ("host-ack-number-unchecked", "if (!memcmp(nonce, L.nonce, 16))\n\t\t\t{\n\t\t\t\tBeginRestart();\t\t\t\t// the handoff, then GO",
     "if (true)\n\t\t\t{\n\t\t\t\tBeginRestart();\t\t\t\t// the handoff, then GO", "an acknowledgement with another start number is ignored"),
    ("host-late-ack-accepted", 'L.startSent = false;\n\t\tText(L.st.notice, sizeof L.st.notice, "The other player did not answer the start. Try Start again.");',
     'Text(L.st.notice, sizeof L.st.notice, "The other player did not answer the start. Try Start again.");', "a late acknowledgement of an expired attempt is ignored"),
    ("host-start-file-kept-after-refused-go", cleanup_anchor, cleanup_anchor.replace('ClearPendingHandoff();', '(void)0;', 1), "GO refused by the transport"),
    ("host-start-refusal-ignored", '\tif (!Send(L.campaign ? MSG_CAMPAIGN_START : MSG_START, extra, sizeof extra))\n\t\treturn 0;', '\tSend(L.campaign ? MSG_CAMPAIGN_START : MSG_START, extra, sizeof extra);', "a START the transport refused"),
    # the relation of the two waits (the guest's outlasts the host's plus the answer's way back): a guest that gives up before a GO the host can still send, and one that never gives up
    ("guest-wait-below-host-wait", "const uint64_t kStartWaitMs = 3000, kGuestWaitMs = 5000;", "const uint64_t kStartWaitMs = 3000, kGuestWaitMs = 1000;",
     "a GO that arrives 3.5 s after the START"),
    ("guest-wait-far-longer", "const uint64_t kStartWaitMs = 3000, kGuestWaitMs = 5000;", "const uint64_t kStartWaitMs = 3000, kGuestWaitMs = 20000;",
     "a GO that arrives 5.5 s after the START"),
]

MUTANTS += [('backend-bypass-factory',
  'CE_NetCreateLobby(',
  'CE_NetCreate(',
  'backend: host uses lobby factory'),
 ('backend-hide-creation-problem',
  'return CE_NetGameProblem(NULL);',
  'return "";',
  'backend: host creation reports actual backend problem'),
 ('backend-drop-route-problem',
  'Text(problem, size, backend_problem);\n    return true;',
  'Text(problem, size, backend_problem);\n    return false;',
  'backend: host route error survives destruction without silent retry'),
 ('backend-override-auth-cause',
  '(status.error != CE_NET_IO && status.error != CE_NET_TIMEOUT && status.error != '
  'CE_NET_BAD_CONFIG && status.error != CE_NET_PROTOCOL)',
  'false',
  'backend: authentication error keeps its own cause'),
 ('backend-drop-host-protocol',
  'void HostFailure(const CE_NetStatus &s)\n'
  '{\n'
  '    char route_problem[sizeof L.st.notice] = {};\n'
  '    const bool route_failed = GameRouteProblem(s, route_problem, sizeof route_problem);',
  'void HostFailure(const CE_NetStatus &s)\n'
  '{\n'
  '    char route_problem[sizeof L.st.notice] = {};\n'
  '    const bool route_failed = s.error != CE_NET_PROTOCOL && GameRouteProblem(s, route_problem, '
  'sizeof route_problem);',
  'backend: host protocol cause survives destruction without silent retry'),
 ('backend-drop-guest-protocol',
  'void GuestFailure(const CE_NetStatus &s)\n'
  '{\n'
  '    char route_problem[sizeof L.st.notice] = {};\n'
  '    const bool route_failed = GameRouteProblem(s, route_problem, sizeof route_problem);',
  'void GuestFailure(const CE_NetStatus &s)\n'
  '{\n'
  '    char route_problem[sizeof L.st.notice] = {};\n'
  '    const bool route_failed = s.error != CE_NET_PROTOCOL && GameRouteProblem(s, route_problem, '
  'sizeof route_problem);',
  'backend: guest protocol cause survives destruction without silent retry')]


def compile_driver(out, name, lobby_text, sanitize=False):
    cxx = shutil.which(os.environ.get("CXX", "clang++")) or shutil.which("g++")
    cc = shutil.which(os.environ.get("CC", "clang")) or shutil.which("gcc")
    sha = out / "sha256.o"
    if not sha.exists():
        subprocess.run([cc, "-c", "-O1", "-o", str(sha), str(SHA)], check=True)
    folder = out / name
    folder.mkdir()
    source = folder / "ce_netlobby.cpp"
    source.write_text(lobby_text.replace('"../../network/', '"'))
    exe = folder / "test"
    sdl = build.deps.PREFIX / "include"
    ice_include = os.environ.get('CE_LOBBY_TEST_ICE_INCLUDE')
    ice_flags = ['-DCE_NETWORK_ICE=1'] + (['-I', ice_include] if ice_include else [])
    cmd = [cxx, "-std=c++17", *ice_flags, "-Wall", "-Wextra", "-O1", "-g", *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitize else []), "-Wno-unused-function",
           '-DCE_NETWORK_SIMULATION_ID="authored-lobby-start-test"', '-DLOBBY_SOURCE="%s"' % source.as_posix(),
           "-I", str(COMMON), "-I", str(NETWORK), "-I", str(sdl), str(DRIVER), str(COMMON / "ce_netcode.cpp"), str(NETWORK / "codec.cpp"),
           str(ROOT / "port/game/ce_network_boot_authority.cpp"), str(sha), "-o", str(exe)]
    if 'CE_LobbyCampaignStart(' in lobby_text:
        cmd.append('-DCE_LOBBY_CAMPAIGN_FIXTURE')
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    (folder / "build.log").write_text(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(name + " did not compile; see " + str(folder / "build.log"))
    return exe


def run(exe, scratch):
    if scratch.exists():
        raise RuntimeError("refusing to replace retained fixture output")
    scratch.mkdir(parents=True)
    env = {"PATH": os.environ.get("PATH", "/usr/bin"), "LANG": "C", "LOBBY_TEST_SCRATCH": str(scratch), "ASAN_OPTIONS": "detect_leaks=0"}
    # the restart's arguments are this process's own: the lobby reads them from /proc/self/cmdline and the start file accepts these
    return subprocess.run([str(exe), "-skipfmv", "-forcewindowed", "-res", "640", "480"], capture_output=True, text=True, timeout=120, env=env)


def lines(trial, prefix):
    return [l[len(prefix):] for l in trial.stdout.splitlines() if l.startswith(prefix)]


def main():
    lobby_text = LOBBY.read_text(encoding="utf-8")
    parent = ROOT / "local-data/port/network-lobby-start-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    results = []

    def report(ok, label):
        print(("PASS " if ok else "FAIL ") + label, flush=True)
        results.append(bool(ok))

    exe = compile_driver(out, "actual", lobby_text, sanitize=True)
    trial = run(exe, out / "actual-scratch")
    (out / "actual" / "result.log").write_text(trial.stdout + trial.stderr)
    passed, failed = lines(trial, "PASS "), lines(trial, "FAIL ")
    report(trial.returncode == 0 and not failed and len(passed) >= SCENARIOS_EXPECTED,
           "the lobby's start handshake: every scenario passes against a scripted peer (%d checks, %d failures)" % (len(passed), len(failed)))
    for line in failed:
        print("   FAIL " + line)

    for name, needle, replacement, expected in (x for x in MUTANTS if '--backend-only' not in sys.argv or x[0].startswith('backend-')):
        if lobby_text.count(needle) != 1:
            raise RuntimeError("mutation is not unique: " + name)
        trial = run(compile_driver(out, name, lobby_text.replace(needle, replacement, 1)), out / (name + "-scratch"))
        (out / name / "result.log").write_text(trial.stdout + trial.stderr)
        report(trial.returncode != 0 and any(expected in line for line in lines(trial, "FAIL ")), "mutant %s is caught (%s)" % (name, expected))

    # the lobby of the commit before the fix: the same scenarios must fail against it
    old = subprocess.run(["git", "-C", str(ROOT), "show", "%s:port/platform/common/ce_netlobby.cpp" % BEFORE_FIX], capture_output=True, text=True)
    if old.returncode == 0:
        trial = run(compile_driver(out, "before-fix", old.stdout, sanitize=True), out / "before-fix-scratch")
        (out / "before-fix" / "result.log").write_text(trial.stdout + trial.stderr)
        failed_before = lines(trial, "FAIL ")
        report(trial.returncode != 0 and len(failed_before) >= 12,
               "the lobby of %s (before GO existed) fails the scenarios: %d of its %d checks fail" % (BEFORE_FIX[:8], len(failed_before), len(lines(trial, "PASS ")) + len(failed_before)))
    else:
        print("SKIPPED the lobby before the fix (%s is not in this clone's history): not counted as a pass" % BEFORE_FIX[:8])

    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    (out / "result.txt").write_text("\n".join("%s %s" % (path.relative_to(ROOT), sha(path)) for path in (LOBBY, DRIVER)) + "\n%d checks, %s\n" % (len(results), "OK" if all(results) else "FAILED"))
    print("lobby start controls: " + ("OK" if all(results) else "FAILED"), "(evidence: %s)" % out)
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
