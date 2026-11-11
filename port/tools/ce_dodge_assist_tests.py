#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Dodge Button (port/game/ce_dodge_assist*.{h,cpp}, patch 0220): its rules, its game glue and its two hooks, run beside the game's own code.

The fixture compiles the actual CController::DoMappings as patch 0220 leaves it (and, for the comparisons, as it stands without the
patch), the hook the patch puts at the top of CController::SendButtonAction, the actual StrafeLeft and StrafeRight of the walker (the
dash: ce_dodge_assist_rules.h says what it keeps), GetMappedInputValue, the CE gamepad provider and the Dodge Button's own header and
source, against synthetic game, clock, console, ce.ini and device boundaries. It then presses keys and a pad update by update.

Checked, each by a mode of the driver:
- the double tap the assist sends is the double tap the game accepts: one dash, toward the chosen side, for the button and the direction
  tapped or held, pressed in either order or together, at every frame rate from 20 to 144 per second; the same dash as the double tap
  typed by hand gives, update for update, with the walker left in the same state; never a dash the wrong way, including when the player
  pushed the direction a moment before (the first push would complete a dash the other way), after strafing the other way (the game
  would ignore a first push), through a slow frame (once more, then no more) and at 10 updates a second (it cannot fit, and stops);
- the player's own strafe is not passed on while the double tap runs, and passes again after it; two players dodge with their own controls;
- the assist's copy of what the walker remembers (the last strafe value, when each side's full push began) is the walker's own after every
  update of random play (keys, a gamepad's button and stick, update lengths of 10 to 50 ms, lost footing, pauses, new levels, lost players), so
  that no dodge is ever sent the wrong way: not after the walker refused the second push in the air, not after a push taken while the assist was
  idle (the first update after a pause), not at the first update of a new level, not after a player was lost and the game spawned a new Battle
  Engine with a new walker while its clock ran on (modes air-refusal, idle-begin, air-retry, chaos, chaos-air, clock-restart, new-level,
  new-walker); a change from one direction to the other with the button held is a press (switch);
- the edges where a float comparison decides, with explicit game times (rules); how far a stick must be pushed (stick-threshold); the send
  window at update lengths of 5 to 105 ms (window); how long a press is kept (buffer); the attempts (slow); an idle stretch forgets what it
  should and keeps the walker's memory (pause-mid, pause-want, pause-memory); g_dash_time and its limits (dash-time, dash-time-change,
  dash-time-range, no-console-vars), g_dash_start (dash-start, dash-start-retail); ce.ini is asked once on the way into a level, never from the pause menu or the front end (ini-quiet);
- without the button, or with it where the assist is off (ce.ini dodge_button=0, the CE layer off, a recording, a replay or the
  virtual clock, probes, a pilot, a network game or its boot, a demo or attract loop, a stress test, or the controller not driving a
  living, powered walker on the ground in a running game, a key or button the player has bound, a flight stick, the keyboard of player
  two), what reaches the player is, update for update, what the controller without the patch sends;
- ce.ini is read when a level is entered, not every update;
- mutants: each of those rules, removed or bent, must be caught, and the test says which check caught it;
- the production sources compile for both targets with the build's own flags (--mirror: the build's mirror).

    python3 -I port/tools/ce_dodge_assist_tests.py [--mirror local-data/port/mirror/src]
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'port'))
import build
import quiet_crash

PATCH = '0220-ce-dodge-assist.patch'


def body(source, pattern):
    m = re.search(pattern, source, re.M)
    if not m:
        raise ValueError('missing source: ' + pattern)
    brace = source.index('{', m.end())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end] + '\n'


def pieces(patched, with_patch):
    controller_h = (ROOT / 'src/Controller.h').read_text()
    controller = (ROOT / 'src/Controller.cpp').read_text()
    pc = (ROOT / 'src/PCController.cpp').read_text()
    console_h = (ROOT / 'src/console.h').read_text()
    walker = (ROOT / 'src/BattleEngineWalkerPart.cpp').read_text()
    dik = (ROOT / 'port/include/linux/dinput.h').read_text()
    joy = re.search(r'enum EJoyButtonPushType\s*\{.*?\};', controller_h, re.S).group(0)
    action_lines = [l for l in controller_h.splitlines() if re.match(r'#define BUTTON_\w+\s', l)]
    axis_lines = [l for l in controller_h.splitlines() if re.match(r'#define ANALOGUE_(?:X1|Y1|X2|Y2|POV_X|POV_Y|ACT_AS_DIGITAL_THRESHOLD)\s', l)]
    structs = controller_h[controller_h.index('struct SControlInput'):controller_h.index('class CController\n')]
    constructors = controller[controller.index('SControlBinding::SControlBinding(bool'):controller.index('HRESULT InitMouse()')]
    rows = []
    for name in ('BUTTON_MECH_STRAFE_LEFT', 'BUTTON_MECH_STRAFE_RIGHT', 'BUTTON_MECH_LANDING_JETS', 'BUTTON_MECH_MORPH'):
        found = [l.strip() for l in pc.splitlines() if re.match(r'SControlBinding\(1, ' + name + r',', l.strip())]
        assert len(found) == 1, name
        rows.extend(found)
    dik_codes = '\n'.join(re.findall(r'^#define\s+DIK_\w+\s+0x[0-9a-fA-F]+', dik, re.M))
    for key in ('DIK_A ', 'DIK_D ', 'DIK_LCONTROL ', 'DIK_LSHIFT ', 'DIK_RSHIFT '):
        assert key in dik_codes, key
    controller_patched = patched['Controller.cpp']
    mappings = body(controller_patched, r'^void\s+CController::DoMappings\(')
    send = body(controller_patched, r'^void\s+CController::SendButtonAction\(')
    hook = re.search(r'#if defined\(CE_VERSION\)\n\tif \(!sCeDodgeSending && PortCeDodgeAssistFilter\(this, mPadNumber, button, ana_val\)\)\n[^\n]*\n#endif\n', send)
    if with_patch:
        # the patch's hook is right after SetButton and before anything is handed on
        assert hook and send.index('SetButton(button)') < hook.start() < send.index('ReceiveButtonAction'), 'the filter is not at the top of SendButtonAction'
        assert 'PortCeDodgeAssistEnd(this, pad,' in mappings and mappings.rindex('PortCeDodgeAssistEnd') > mappings.rindex('BUTTON_FRONTEND_CHEAT'), \
            'the end hook is not after the player\'s own inputs'
    else:
        assert not hook and 'PortCeDodgeAssist' not in mappings
    glue = (ROOT / 'port/game/ce_dodge_assist.cpp').read_text()
    glue = re.sub(r'^#include\s+.*$', '', glue, flags=re.M).replace('__attribute__((weak))', '')
    gamepad = re.sub(r'^#include\s+.*$', '', build.network_view((ROOT / 'port/game/ce_gamepad.cpp').read_text(), off=True), flags=re.M)
    return {
        'control.inc': joy + '\n' + '\n'.join(action_lines + axis_lines) + '\n' + structs,
        'bindings.inc': dik_codes + '\n' + constructors + '\nSControlBinding CONTROL_BINDINGS[] = {\n' + '\n'.join(rows) + '\nSControlBinding()};\n',
        'lookup.inc': body(controller, r'^SControlBinding \*FindControlBinding\('),
        'console_types.inc': re.search(r'enum ECVarType\s*\{.*?\};', console_h, re.S).group(0) + '\n' + re.search(r'struct CConsoleVar\s*\{.*?\};', console_h, re.S).group(0),
        'console_find.inc': body(console_h, r'CConsoleVar\s*\*FindVariable\(char \*name\)'),
        'walker.inc': '\n'.join(re.search(r'^(?:float|int) CBattleEngineWalkerPart::' + n + r'\s*=.*?;', walker, re.M).group(0)
                                for n in ('mDashTime', 'mDashStart', 'mDashEnd', 'mDashLength', 'mDashVelocity')) + '\n' +
                      body(walker, r'^void\s+CBattleEngineWalkerPart::StrafeLeft\(') + body(walker, r'^void\s+CBattleEngineWalkerPart::StrafeRight\('),
        'gamepad.inc': gamepad,
        'analog_dead_zone.inc': '\n'.join(re.findall(r'^#define ANALOGUE_DEAD_ZONE(?:_SCALE)?\s+[^\n]+', controller, re.M)) + '\n',
        'mapped_input.inc': body(controller, r'^float\s+CController::GetMappedInputValue\('),
        'mapping.inc': body(controller_patched, r'^inline float DigitalValueForButton\(') + mappings,
        'send.inc': 'void CController::SendButtonAction(int button, float ana_val)\n{\n\tSetButton(button);\n' + (hook.group(0) if with_patch else '') +
                    '\tReach(*this, button, ana_val);\n}\n',
        'dodge_glue.inc': glue,
        'ce_dodge_assist_rules.h': (ROOT / 'port/game/ce_dodge_assist_rules.h').read_text(),
        'ce_dodge_assist.h': (ROOT / 'port/game/ce_dodge_assist.h').read_text(),
    }


MODES = ['key-right', 'key-left', 'pad-right', 'pad-left', 'tap-and-hold-alike', 'typed-equals-assisted', 'sweep', 'hazard', 'chain',
         'old-history', 'hitch', 'slow', 'dash-time', 'pause-mid', 'pause-memory', 'two-players', 'ini-reload',
         'air-refusal', 'idle-begin', 'air-retry', 'chaos', 'chaos-air', 'clock-restart', 'new-level', 'new-walker', 'typed-sweep', 'switch', 'buffer',
         'stick-threshold', 'window', 'rules', 'pause-want', 'other-actions', 'env-empty', 'dash-time-change', 'dash-time-range', 'dash-start', 'dash-start-retail', 'no-console-vars',
         'ini-quiet', 'two-players-shared-keyboard', 'key-unused-slot', 'power-start']
# modes whose output is compared, line for line, with the controller that has no patch (the assist does nothing in them)
SAME = (['plain-keys', 'plain-pad'] +
        ['off-' + n for n in ('setting', 'layer', 'recording', 'boot', 'session', 'demo', 'autoconfig', 'record-demo', 'play-demo', 'stress', 'attract')] +
        ['off-env-' + n for n in ('BEA_PORT_CLOCK', 'BEA_PORT_TAPE', 'BEA_PORT_PROBES', 'BEA_PORT_RECORD', 'BEA_PORT_PILOT', 'BEA_PORT_AUTOPILOT',
                                  'BEA_NET_HOST', 'BEA_NET_JOIN')] +
        ['gate-' + n for n in ('bound-key', 'bound-key-once', 'bound-key-release', 'pad-bound', 'pad-bound-on', 'pad-bound-repeat', 'pad-bound-release', 'stick',
                               'paused', 'menu', 'not-playing', 'airborne', 'dying', 'jet', 'unpowered', 'ctrl-only', 'both-directions', 'player-two-key',
                               'pad-disconnected', 'keytrap')])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mirror', type=Path, help="port/build.py's mirror (local-data/port/mirror/src): compile the production sources for both targets")
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--no-mutants', action='store_true')
    parser.add_argument('--only-mutants', help='comma-separated mutant names: run only those (and the comparisons they need), not the modes')
    args = parser.parse_args()
    parent = ROOT / 'local-data/port/ce-dodge-assist-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    patched = build.patched_text(['Controller.cpp'], network=False)
    base = build.patched_text(['Controller.cpp'], skip=(PATCH,), network=False)
    driver = ROOT / 'port/tools/ce_dodge_assist_driver.cpp'
    results = []

    def make(name, texts, with_patch, edits=()):
        folder = out / name
        folder.mkdir()
        files = pieces(texts, with_patch)
        for target, old, new, count in edits:
            assert files[target].count(old) == count, (name, target, old, files[target].count(old))
            files[target] = files[target].replace(old, new)
        for file, value in files.items():
            (folder / file).write_text(value)
        binary = folder / 'fixture'
        cmd = [args.compiler, '-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-sign-compare', '-fno-diagnostics-show-caret',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(folder), '-I' + str(ROOT / 'port/game'),
               '-DCE_VERSION="fixture"', str(driver), '-o', str(binary)]
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        (folder / 'build.log').write_text(run.stdout + run.stderr)
        if run.returncode:
            errors = [l for l in (run.stdout + run.stderr).splitlines() if 'error' in l][:12]
            raise RuntimeError('fixture compilation failed (' + name + '): ' + '\n'.join(errors))
        return binary

    def run(binary, mode, wanted=0, message=''):
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
        p = quiet_crash.run([str(binary), mode], capture_output=True, text=True, env=env, timeout=120)
        ok = p.returncode == wanted and (not message or message in p.stderr)
        results.append({'mode': mode, 'binary': binary.parent.name, 'expected': wanted, 'exit': p.returncode, 'stdout': p.stdout.strip()[:300],
                        'stderr': p.stderr.strip()[:300], 'passed': ok})
        print(('PASS ' if ok else 'FAIL ') + binary.parent.name + ' ' + mode + ('' if ok else '  ' + p.stderr.strip()[:200]), flush=True)
        return p

    patch_text = (ROOT / 'port/patches' / PATCH).read_text(encoding='latin-1')
    assert re.findall(r'^\+\+\+ b/(\S+)', patch_text, re.M) == ['Controller.cpp'], 'the patch touches one file'
    assert not [l for l in patch_text.splitlines() if l.startswith('-') and not l.startswith('---')], 'the patch only adds'
    assert patch_text.count('\n@@') == 3, 'the patch has the declarations, the filter and the end hook'

    with_patch = make('patched', patched, True)
    without = make('base', base, False)
    only = set(x for x in (args.only_mutants or '').split(',') if x)
    for mode in ([] if only else MODES):
        run(with_patch, mode)
    def digest(binary, mode):
        p = quiet_crash.run([str(binary), mode], capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
        return p.returncode, [l for l in p.stdout.splitlines() if l.startswith(mode + ' ')], p.stderr

    base_digest = {}
    for mode in ([] if only else SAME):
        code_a, line_a, _ = digest(with_patch, mode)
        code_b, line_b, _ = digest(without, mode)
        base_digest[mode] = line_b
        ok = code_a == 0 and code_b == 0 and len(line_a) == 1 and line_a == line_b
        results.append({'mode': mode + ' = unpatched', 'passed': ok, 'patched': line_a, 'base': line_b})
        print(('PASS ' if ok else 'FAIL ') + mode + ' sends what the controller without the patch sends', flush=True)

    if not args.no_mutants:
        hook = 'if (!sCeDodgeSending && PortCeDodgeAssistFilter(this, mPadNumber, button, ana_val))\n\t\treturn;'
        # The rules: a stand-in for each statement and number, one at a time
        R, G, M = 'ce_dodge_assist_rules.h', 'dodge_glue.inc', 'mapping.inc'
        mutants = [
            # (name, piece, old, new, count, a mode that must notice[, more edits as (piece, old, new, count) for a mutant of several places])
            ('keeps-players-strafe', R, 'if (mOwning)\n\t\t\treturn true;', 'if (false)\n\t\t\treturn true;', 1, 'key-right'),
            ('owning-neutral-passes', R, 'if (mOwning)\n\t\t\treturn true;', 'if (mOwning && value != 0.f)\n\t\t\treturn true;', 1, 'rules'),
            ('no-hazard-wait', R, 'return age > w.mTime * (0.5f - Margin()) && age < w.mTime * (1.f + Margin());', 'return false;', 1, 'hazard'),
            ('completes-edges-closed', R, 'return age > w.mTime * (0.5f - Margin()) && age < w.mTime * (1.f + Margin());',
             'return age >= w.mTime * (0.5f - Margin()) && age <= w.mTime * (1.f + Margin());', 1, 'rules'),
            ('push-too-soon', R, 'w.mTime * (0.5f + Margin())', 'w.mTime * (0.3f)', 1, 'key-right'),
            ('push-too-late', R, 'hi = w.mTime * (1.f - Margin())', 'hi = w.mTime * (1.5f)', 1, 'hitch'),
            ('push-window-whole', R, 'hi = w.mTime * (1.f - Margin())', 'hi = w.mTime', 1, 'window'),
            ('push-window-margin-over', R, 'hi = w.mTime * (1.f - Margin())', 'hi = w.mTime * (1.f + Margin())', 1, 'window'),
            ('push-window-lower-open', R, 'if (elapsed < lo)', 'if (elapsed <= lo)', 1, 'rules'),
            ('push-window-upper-open', R, 'if (elapsed <= hi)', 'if (elapsed < hi)', 1, 'rules'),
            ('first-push-same-side', R, 'out[count++] = Tap{other, true};\n\t\tDeliver(other, (float)other, now, w);',
             'out[count++] = Tap{side, true};\n\t\tDeliver(side, (float)side, now, w);', 1, 'key-right'),
            ('no-neutral-first', R, 'if (mLastX * (float)other >= w.mStart)', 'if (false)', 1, 'hitch'),
            ('neutral-not-at-start', R, 'if (mLastX * (float)other >= w.mStart)', 'if (mLastX * (float)other > w.mStart)', 1, 'rules'),
            ('neutral-at-any-last', R, 'if (mLastX * (float)other >= w.mStart)', 'if (mLastX * (float)other > 0.f)', 1, 'rules'),
            ('neutral-at-dash-end', R, 'if (mLastX * (float)other >= w.mStart)', 'if (mLastX * (float)other >= w.mEnd)', 1, 'rules'),
            ('begin-at-start-counts', R, 'if (last < w.mStart && toward > w.mStart)', 'if (last <= w.mStart && toward > w.mStart)', 1, 'rules'),
            ('begin-to-start-counts', R, 'if (last < w.mStart && toward > w.mStart)', 'if (last < w.mStart && toward >= w.mStart)', 1, 'rules'),
            ('begin-at-dash-end', R, 'if (last < w.mStart && toward > w.mStart)', 'if (last < w.mEnd && toward > w.mEnd)', 1, 'rules'),
            ('repeats-while-held', R, 'const bool pressed = pushing != 0 && pushing != mPushing && !mAdopt;', 'const bool pressed = pushing != 0 && !mAdopt;', 1, 'chain'),
            ('direction-change-is-no-press', R, 'const bool pressed = pushing != 0 && pushing != mPushing && !mAdopt;', 'const bool pressed = pushing != 0 && mPushing == 0 && !mAdopt;', 1, 'switch'),
            ('forgets-a-let-go-press', R, 'if (now - mWantAt > Patience())', 'if (pushing == 0 || now - mWantAt > Patience())', 1, 'hazard'),
            ('press-kept-for-ever', R, 'static float Patience() { return 0.35f; }', 'static float Patience() { return 1.0f; }', 1, 'buffer'),
            ('press-kept-closed', R, 'if (now - mWantAt > Patience())', 'if (now - mWantAt >= Patience())', 1, 'rules'),
            ('stick-light-touch', R, 'static float Direction() { return 0.5f; }', 'static float Direction() { return 0.3f; }', 1, 'stick-threshold'),
            ('stick-needs-more', R, 'static float Direction() { return 0.5f; }', 'static float Direction() { return 0.7f; }', 1, 'stick-threshold'),
            ('stick-half-is-not-enough', R, 'mSeen[0] >= Direction(), right = mSeen[1] >= Direction()', 'mSeen[0] > Direction(), right = mSeen[1] > Direction()', 1, 'rules'),
            ('tries-for-ever', R, 'static int Attempts() { return 2; }', 'static int Attempts() { return 99; }', 1, 'slow'),
            ('tries-three-times', R, 'static int Attempts() { return 2; }', 'static int Attempts() { return 3; }', 1, 'slow'),
            ('first-attempt-not-counted', R, 'mAttempt = 1;', 'mAttempt = 0;', 1, 'slow'),
            ('forgets-walker-memory-on-cancel', R, 'void Cancel()\n\t{', 'void Cancel()\n\t{\n\t\tmLastX = 0.f; mStart[0] = mStart[1] = -1e9f;', 1, 'pause-memory'),
            ('held-through-idle-is-a-press', R, 'mAdopt = true;', 'mAdopt = false;', 1, 'rules'),
            ('cancel-keeps-the-press', R, 'mWant = 0;\n\t\tmWantAt = 0.f;\n\t\tmSide = 0;', 'mWantAt = 0.f;\n\t\tmSide = 0;', 1, 'pause-want'),
            ('cancel-keeps-the-double-tap', R, 'void Cancel()\n\t{\n\t\tmWaiting = false;', 'void Cancel()\n\t{', 1, 'pause-mid'),
            ('cancel-keeps-holding-back', R, 'mWaiting = false;\n\t\tmOwning = false;\n\t\tmWant = 0;', 'mWaiting = false;\n\t\tmWant = 0;', 1, 'rules'),
            ('idle-does-not-cancel', R, 'Cancel();\n\t\t\treturn 0;', 'return 0;', 1, 'pause-mid'),
            ('no-walker-window-still-acts', R, 'if (!u.mEligible || !(w.mTime > 0.f))', 'if (!u.mEligible)', 1, 'rules'),
            ('clock-backwards-ignored', R, 'if (mHaveNow && u.mNow < mLastNow)\n\t\t\tReset();', 'if (false)\n\t\t\tReset();', 1, 'new-level'),
            ('clock-equal-is-backwards', R, 'if (mHaveNow && u.mNow < mLastNow)', 'if (mHaveNow && u.mNow <= mLastNow)', 1, 'rules'),
            ('clock-backwards-late-for-real-pushes', R, 'if (mHaveNow && now < mLastNow)\n\t\t\tReset();\t\t\t\t\t\t// the clock ran backwards', 'if (false)\n\t\t\tReset();\t\t\t\t\t\t// the clock ran backwards', 1, 'rules'),
            ('fresh-assist-remembers-a-push', R, 'mStart[0] = mStart[1] = -1e9f;\n\t\tmLastNow = 0.f;', 'mStart[0] = mStart[1] = 0.f;\n\t\tmLastNow = 0.f;', 1, 'rules'),
            ('ignores-walker-refusal', R, 'if (accepted)\n\t\t\tDeliver', 'if (true)\n\t\t\tDeliver', 2, 'chain'),
            ('second-push-recorded-when-refused', R, 'if (u.mAccepts)\n\t\t\t\tDeliver(mSide, (float)mSide, now, w);', 'Deliver(mSide, (float)mSide, now, w);', 1, 'air-refusal'),
            ('second-try-in-the-air', R, 'mAttempt < Attempts() && u.mAccepts && !Completes(mSide, now, w)', 'mAttempt < Attempts() && !Completes(mSide, now, w)', 1, 'air-retry'),
            ('idle-push-ignores-refusal', R, 'Reset();\n\t\tif (accepted)\n\t\t\tDeliver', 'Reset();\n\t\tif (true)\n\t\t\tDeliver', 1, 'chaos-air'),
            # The glue: the game's state, the walker's numbers, the fences and the setting
            ('accepts-never-set', G, 'u.mAccepts = CanDodge(engine);', 'u.mAccepts = true;', 1, 'air-refusal'),
            ('end-accepts-ignores-power', G, 'u.mAccepts = CanDodge(engine);', 'u.mAccepts = Accepts(engine);', 1, 'chaos-air'),
            ('follow-ignores-power', G, 's.mAssist.Follow(side, value, EVENT_MANAGER.GetTime(), s.mWalker, CanDodge(engine));', 's.mAssist.Follow(side, value, EVENT_MANAGER.GetTime(), s.mWalker, Accepts(engine));', 1, 'chaos-air'),
            ('real-ignores-power', G, 'return s.mAssist.Real(side, value, EVENT_MANAGER.GetTime(), s.mWalker, CanDodge(engine))', 'return s.mAssist.Real(side, value, EVENT_MANAGER.GetTime(), s.mWalker, Accepts(engine))', 1, 'chaos-air'),
            # the copy of the walker's memory taking what the game drops while the engine is unpowered is caught together in all three places (the idle push, the button's push and the end of the update); each alone is caught by the random play
            ('power-term-gone', G, 'u.mAccepts = CanDodge(engine);', 'u.mAccepts = Accepts(engine);', 1, 'power-start',
             (G, 's.mAssist.Follow(side, value, EVENT_MANAGER.GetTime(), s.mWalker, CanDodge(engine));', 's.mAssist.Follow(side, value, EVENT_MANAGER.GetTime(), s.mWalker, Accepts(engine));', 1),
             (G, 'return s.mAssist.Real(side, value, EVENT_MANAGER.GetTime(), s.mWalker, CanDodge(engine))', 'return s.mAssist.Real(side, value, EVENT_MANAGER.GetTime(), s.mWalker, Accepts(engine))', 1)),
            ('idle-push-not-followed', G, 's.mAssist.Follow(side, value, EVENT_MANAGER.GetTime(), s.mWalker, CanDodge(engine));', '', 1, 'idle-begin'),
            ('idle-asks-ce-ini-in-the-menus', G, 'if (!engine || !SettingOn()) return 0;', 'if (!SettingOn()) return 0;', 1, 'ini-quiet'),
            ('new-level-not-told-by-the-menus', G, 'if (ordinary && GAME.GetGameState() != GAME_STATE_PLAYING)\n\t{\n\t\ts.mAssist.Reset();\n\t\ts.mOf = NULL;\n\t}\n', '', 1, 'new-level'),
            ('dying-engine-keeps-the-copy', G, 'if (engine->IsDying())\n\t{\n\t\ts.mAssist.Reset();\n\t\ts.mOf = NULL;\n\t\treturn false;\n\t}\n', '', 1, 'new-walker'),
            ('other-walker-keeps-the-copy', G, 'if (s.mOf) s.mAssist.Reset();', '', 1, 'new-walker'),
            ('walker-not-checked-by-the-filter', G, '\tif (engine && !OfThisWalker(s, engine)) return 0;\n\tif (!s.mHaveWalker)', '\tif (!s.mHaveWalker)', 1, 'new-walker'),
            ('walker-not-checked-at-the-end', G, '\tif (engine) OfThisWalker(s, engine);\n\tif (!engine) gSettingStale = true;', '\tif (!engine) gSettingStale = true;', 1, 'new-walker'),
            ('fixed-dash-time', G, 'Float("g_dash_time", 0.004f, 10.f, &w.mTime);', '', 1, 'dash-time'),
            ('dash-time-not-read-again-while-waiting', G, 'if (!s.mHaveWalker || u.mDodgeDown || s.mAssist.Waiting())', 'if (!s.mHaveWalker || u.mDodgeDown)', 1, 'dash-time-change'),
            ('dash-time-too-small-believed', G, 'Float("g_dash_time", 0.004f, 10.f, &w.mTime);', 'Float("g_dash_time", 0.f, 10.f, &w.mTime);', 1, 'dash-time-range'),
            ('dash-time-too-large-believed', G, 'Float("g_dash_time", 0.004f, 10.f, &w.mTime);', 'Float("g_dash_time", 0.004f, 1e9f, &w.mTime);', 1, 'dash-time-range'),
            ('dash-time-nan-believed', G, 'if (!(value >= low && value <= high)) return false;', 'if (value < low || value > high) return false;', 1, 'dash-time-range'),
            ('walker-start-not-read', G, 'Float("g_dash_start", 0.01f, 1.f, &w.mStart);', '', 1, 'dash-start'),
            ('retail-dash-start-wrong', G, 'CeDodge::Walker w = {0.2f, 0.9f, 0.8f};', 'CeDodge::Walker w = {0.2f, 0.8f, 0.8f};', 1, 'dash-start-retail'),
            ('retail-dash-time-wrong', G, 'CeDodge::Walker w = {0.2f, 0.9f, 0.8f};', 'CeDodge::Walker w = {0.25f, 0.9f, 0.8f};', 1, 'no-console-vars'),
            ('no-fence-tape', G, '"BEA_PORT_TAPE", ', '', 1, 'off-env-BEA_PORT_TAPE'),
            ('no-fence-net-join', G, ', "BEA_NET_JOIN"', '', 1, 'off-env-BEA_NET_JOIN'),
            ('empty-variable-fences', G, 'return value && *value;', 'return value != 0;', 1, 'env-empty'),
            ('no-fence-recording', G, 'if (!PortCeLayer() || PortRecording()) return false;', 'if (!PortCeLayer()) return false;', 1, 'off-recording'),
            ('no-fence-boot', G, 'if (CE_BootRequested && CE_BootRequested()) return false;', '', 1, 'off-boot'),
            ('no-fence-session', G, 'if (CE_SessionActive && CE_SessionActive()) return false;', '', 1, 'off-session'),
            ('no-fence-attract', G, 'CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode) return false;', 'CLIPARAMS.mStressTest) return false;', 1, 'off-attract'),
            ('no-fence-demo', G, 'if (PLAYABLE_DEMO || ', 'if (', 1, 'off-demo'),
            ('setting-ignored', G, 'u.mEligible = engine && SettingOn();', 'u.mEligible = engine != NULL;', 1, 'off-setting'),
            ('setting-read-every-update', G, 'if (gSettingStale)\n\t{', 'gSettingStale = true;\n\tif (gSettingStale)\n\t{', 1, 'ini-reload'),
            ('cue-asks-ce-ini', G, 'return (gSetting >= 0 ? gSetting != 0 : SettingOn());', 'return SettingOn();', 1, 'ini-reload'),
            ('setting-never-read-again', G, 'if (!engine) gSettingStale = true;', '', 1, 'ini-reload'),
            ('bound-key-used', G, 'return PortCeDodgeAssistKeyFree() ? DIK_LCONTROL : 0;', 'return DIK_LCONTROL;', 1, 'gate-bound-key'),
            ('key-bound-once-used', G, 'b->mInput[i].mType == KEY_ON || b->mInput[i].mType == KEY_ONCE || b->mInput[i].mType == KEY_RELEASE',
             'b->mInput[i].mType == KEY_ON || b->mInput[i].mType == KEY_RELEASE', 1, 'gate-bound-key-once'),
            ('key-bound-release-used', G, 'b->mInput[i].mType == KEY_ON || b->mInput[i].mType == KEY_ONCE || b->mInput[i].mType == KEY_RELEASE',
             'b->mInput[i].mType == KEY_ON || b->mInput[i].mType == KEY_ONCE', 1, 'gate-bound-key-release'),
            ('unused-slot-takes-the-key', G, 'if (b->mInput[i].mPad != -1 && b->mInput[i].mKey == key &&', 'if (b->mInput[i].mKey == key &&', 1, 'key-unused-slot'),
            ('bound-button-used', G, 'return pad >= 0 && pad < kPads && !PortCeGamepadIsStick(pad) && !PadButtonBound(pad, CE_DODGE_PAD_BUTTON);',
             'return pad >= 0 && pad < kPads && !PortCeGamepadIsStick(pad);', 1, 'gate-pad-bound'),
            ('button-bound-on-used', G, 'b->mInput[i].mType == BUTTON_ON || b->mInput[i].mType == BUTTON_ONCE', 'b->mInput[i].mType == BUTTON_ONCE', 1, 'gate-pad-bound-on'),
            ('repeat-bound-button-used', G, ' || b->mInput[i].mType == BUTTON_REPEAT ||\n\t\t\t\t b->mInput[i].mType == BUTTON_RELEASE))', ' ||\n\t\t\t\t b->mInput[i].mType == BUTTON_RELEASE))', 1, 'gate-pad-bound-repeat'),
            ('release-bound-button-used', G, ' ||\n\t\t\t\t b->mInput[i].mType == BUTTON_RELEASE))', '))', 1, 'gate-pad-bound-release'),
            ('stick-used', G, '!PortCeGamepadIsStick(pad) && ', '', 1, 'gate-stick'),
            ('keyboard-for-player-two', G, 'if (pad != 0 || !gState[0].mActive || LT.GetKeytrap()) return 0;', 'if (!gState[pad].mActive || LT.GetKeytrap()) return 0;', 1, 'gate-player-two-key'),
            ('acts-when-paused', G, 'if (GAME.GetGameState() != GAME_STATE_PLAYING || GAME.IsPaused()) return NULL;', '', 1, 'gate-paused'),
            ('acts-when-not-driving', G, 'if (player && controller->GetToControl() == player)', 'if (player)', 1, 'gate-menu'),
            ('acts-in-the-air', G, 'return walker && engine->IsOnGround() && walker->GetDashCount() == 0;', 'return walker && walker->GetDashCount() == 0;', 1, 'gate-airborne'),
            ('acts-in-flight', G, 'engine->GetState() == BATTLE_ENGINE_STATE_WALKER ? engine->GetWalkerPart() : NULL', 'engine ? engine->GetWalkerPart() : NULL', 1, 'gate-jet'),
            ('acts-unpowered', G, 'engine && !engine->IsDying() && engine->GetIsPoweredUp()', 'engine && !engine->IsDying()', 1, 'gate-unpowered'),
            ('keytrap-ignored', G, ' || LT.GetKeytrap()) return 0;', ') return 0;', 1, 'gate-keytrap'),
            ('filters-every-action', G, 'if ((button != BUTTON_MECH_STRAFE_LEFT && button != BUTTON_MECH_STRAFE_RIGHT) || pad < 0 || pad >= kPads) return 0;',
             'if (pad < 0 || pad >= kPads) return 0;', 1, 'other-actions'),
            ('wrong-pad-button', 'ce_dodge_assist.h', 'CE_DODGE_PAD_BUTTON = 0', 'CE_DODGE_PAD_BUTTON = 1', 1, 'key-right'),
            ('one-state-for-every-pad', G, 'if (pad < 0 || pad >= kPads) return 0;\n\tState &s = gState[pad];\n\tCeDodge::Update u',
             'if (pad < 0 || pad >= kPads) return 0;\n\tState &s = gState[0];\n\tCeDodge::Update u', 1, 'two-players'),
            # The patch's hooks (its added lines only)
            ('no-filter-hook', 'send.inc', hook, '', 1, 'key-right'),
            ('filter-after-hand-over', 'send.inc', '\tSetButton(button);\n', '\tSetButton(button);\n\tReach(*this, button, ana_val);\n\tif (button) return;\n', 1, 'key-right'),
            ('taps-filtered-too', 'send.inc', 'if (!sCeDodgeSending && ', 'if (', 1, 'key-right'),
            ('taps-not-marked', M, '\t\tsCeDodgeSending = true;\n', '', 1, 'key-right'),
            ('taps-always-neutral', M, 'tap_hard[k] ? DigitalValueForButton(tap_ids[k]) : 0.0f', '0.0f', 1, 'key-right'),
            ('taps-always-full', M, 'tap_hard[k] ? DigitalValueForButton(tap_ids[k]) : 0.0f', 'DigitalValueForButton(tap_ids[k])', 1, 'hitch'),
            ('key-not-read', M, 'dodge_key && GetKeyOn(dodge_key) == TRUE', 'dodge_key && FALSE', 1, 'key-right'),
            ('first-controller-reads-every-key', M, 'int dodge_key = PortCeDodgeAssistKey(pad);', 'int dodge_key = PortCeDodgeAssistKey(0);', 1, 'two-players-shared-keyboard'),
        ]
        if only:
            unknown = only - {m[0] for m in mutants}
            assert not unknown, 'no such mutant: ' + ', '.join(sorted(unknown))
            for mode in {m[5] for m in mutants if m[0] in only and m[5] in SAME}:
                base_digest[mode] = digest(without, mode)[1]
        for name, target, old, new, count, mode, *more in mutants:
            if only and name not in only:
                continue
            binary = make('mutant-' + name, patched, True, [(target, old, new, count)] + [tuple(e) for e in more])
            code, line, err = digest(binary, mode)
            if mode in SAME:
                caught = code != 0 or line != base_digest[mode]
            else:
                caught = code != 0
            results.append({'mutant': name, 'mode': mode, 'caught': caught, 'passed': caught})
            print(('PASS ' if caught else 'FAIL ') + 'mutant ' + name + ' is caught by ' + mode + (': ' + err.strip().splitlines()[0][:110] if caught and err.strip() else ''), flush=True)
    if args.mirror:
        mirror = args.mirror.resolve()
        for platform in ('linux', 'windows'):
            target = build.TARGETS[platform]
            includes = [build.PORT / 'd3dx/include', build.PORT / 'include', build.PORT / 'game', *target['includes'], mirror, mirror / 'MissionScript']
            for source in (ROOT / 'port/game/ce_dodge_assist.cpp', ROOT / 'port/game/ce_gamepad.cpp', mirror / 'Controller.cpp'):
                cmd = [*target['cxx'], '-std=gnu++14', *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS, '-DCE_VERSION="fixture"',
                       '-DCE_NETWORK_OFF=1', '-fsyntax-only', '-fno-caret-diagnostics', '-include', str(build.PORT / 'include/port.h'),
                       *['-I' + str(p) for p in includes], str(source)]
                done = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
                ok = done.returncode == 0
                results.append({'platform': platform, 'source': source.name, 'passed': ok})
                print(('PASS ' if ok else 'FAIL ') + platform + ' syntax ' + source.name, flush=True)
    (out / 'results.json').write_text(json.dumps(results, indent=1))
    failed = [r for r in results if not r.get('passed')]
    print('%d checks, %d failed; %s' % (len(results), len(failed), out / 'results.json'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
