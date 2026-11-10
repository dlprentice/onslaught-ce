#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise CE gamepad admission and default binding lookup with authored game/device state.

The fixture compiles the actual admission, level classification, binding selection, and mapping tables.
Admission is the same in single player, in local two-player split screen (a retail multiplayer map: each
controller reads its own pad) and in network co-op; recordings and the other faithful runs stay retail.
It does not run a game, device, window, network, saved career or native Windows.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from ce_gamepad_tests import function

ROOT = Path(__file__).resolve().parents[2]
GAMEPAD = ROOT / 'port/game/ce_gamepad.cpp'
GAME = ROOT / 'src/game.cpp'
CONTROLLER = ROOT / 'src/Controller.h'


def declaration(source, signature):
    start = source.index(signature)
    end = source.index('};', start) + 2
    return source[start:end]


def source_parts():
    source, game, header = (p.read_text() for p in (GAMEPAD, GAME, CONTROLLER))
    definitions = '\n'.join(line for line in header.splitlines()
                            if re.match(r'^#define (?:ANALOGUE_|BUTTON_)', line))
    return {
        'definitions': definitions,
        'push_types': declaration(header, 'enum EJoyButtonPushType'),
        'input': declaration(header, 'struct SControlInput'),
        'pads': declaration(source, 'enum { PAD_B'),
        'stick': declaration(source, 'enum { STICK_THUMB'),
        'default': declaration(source, 'struct Default'),
        'menu': declaration(source, 'const Default kMenu[]'),
        'game': declaration(source, 'const Default kGame[]'),
        'flight': declaration(source, 'const Default kStick[]'),
        'ordinary': function(source, 'bool Ordinary()'),
        'allowed': function(source, 'bool Allowed(int pad)'),
        'bindings': function(source, 'bool NoJoystickBindings()'),
        'inputs': function(source, 'int PortCeGamepadInputs('),
        'multiplayer': function(game, 'BOOL CGame::IsMultiplayer()').replace('CGame::IsMultiplayer', 'GameStub::IsMultiplayer', 1),
        'lockstep': 'extern "C" int CE_LockstepRequested() { return network ? 1 : 0; }',
    }


DRIVER = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MAX_JOYPADS 4
@definitions@
@push_types@
@input@
struct SControlBinding { bool mConfigurable; int mId; SControlInput mInput[2]; };
SControlBinding CONTROL_BINDINGS[2];
struct GameStub { int mCurrentlyRunningLevel; int GetCurrentLevel() { return mCurrentlyRunningLevel; } BOOL IsMultiplayer(); } GAME;
struct { int mJoypads; bool mUnknown33474[MAX_JOYPADS]; } LT;
bool ce_layer = true, network = false, stick_device = false;
bool PortCeLayer() { return ce_layer; }
@lockstep@
bool IsStick(int) { return stick_device; }
@pads@
@stick@
@default@
@menu@
@game@
@flight@
@multiplayer@
@ordinary@
@allowed@
@bindings@
@inputs@
int main(int argc, char **argv) {
    if (argc != 2) return 4;
    GAME.mCurrentlyRunningLevel = 100; LT.mJoypads = 1; LT.mUnknown33474[0] = true;
    CONTROL_BINDINGS[0].mId = 31; CONTROL_BINDINGS[0].mConfigurable = true;
    CONTROL_BINDINGS[0].mInput[0].mPad = -1; CONTROL_BINDINGS[0].mInput[1].mPad = -1;
    CONTROL_BINDINGS[1].mId = -1;
    int pad = 0;
    if (!std::strcmp(argv[1], "solo100")) {}
    else if (!std::strcmp(argv[1], "network100")) network = true;
    else if (!std::strcmp(argv[1], "network856-host") || !std::strcmp(argv[1], "network856-guest")) { network = true; GAME.mCurrentlyRunningLevel = 856; }
    else if (!std::strcmp(argv[1], "network858")) { network = true; GAME.mCurrentlyRunningLevel = 858; }
    else if (!std::strcmp(argv[1], "split856")) GAME.mCurrentlyRunningLevel = 856;
    else if (!std::strcmp(argv[1], "split858")) GAME.mCurrentlyRunningLevel = 858;
    else if (!std::strcmp(argv[1], "split-player2")) { GAME.mCurrentlyRunningLevel = 856; LT.mJoypads = 2; LT.mUnknown33474[1] = true; pad = 1; }
    else if (!std::strcmp(argv[1], "split-no-second-pad")) { GAME.mCurrentlyRunningLevel = 856; LT.mUnknown33474[1] = true; pad = 1; }   // a stale device flag past the pad count
    else if (!std::strcmp(argv[1], "split-record")) { GAME.mCurrentlyRunningLevel = 856; setenv("BEA_PORT_RECORD", "1", 1); }
    else if (!std::strcmp(argv[1], "split-clock")) { GAME.mCurrentlyRunningLevel = 856; setenv("BEA_PORT_CLOCK", "1", 1); }
    else if (!std::strcmp(argv[1], "split-tape")) { GAME.mCurrentlyRunningLevel = 856; setenv("BEA_PORT_TAPE", "1", 1); }
    else if (!std::strcmp(argv[1], "split-probes")) { GAME.mCurrentlyRunningLevel = 856; setenv("BEA_PORT_PROBES", "1", 1); }
    else if (!std::strcmp(argv[1], "split-ce-off")) { GAME.mCurrentlyRunningLevel = 856; ce_layer = false; }
    else if (!std::strcmp(argv[1], "split-custom")) { GAME.mCurrentlyRunningLevel = 856; CONTROL_BINDINGS[0].mInput[0] = {0, BUTTON_ON, 3, 0}; }
    else if (!std::strcmp(argv[1], "classify")) { std::printf("%d %d %d\n", (GAME.mCurrentlyRunningLevel = 100, int(GAME.IsMultiplayer() != 0)), (GAME.mCurrentlyRunningLevel = 856, int(GAME.IsMultiplayer() != 0)), (GAME.mCurrentlyRunningLevel = 858, int(GAME.IsMultiplayer() != 0))); return 0; }
    else if (!std::strcmp(argv[1], "network-record")) { GAME.mCurrentlyRunningLevel = 856; network = true; setenv("BEA_PORT_RECORD", "1", 1); }
    else if (!std::strcmp(argv[1], "network-clock")) { GAME.mCurrentlyRunningLevel = 856; network = true; setenv("BEA_PORT_CLOCK", "1", 1); }
    else if (!std::strcmp(argv[1], "network-tape")) { GAME.mCurrentlyRunningLevel = 856; network = true; setenv("BEA_PORT_TAPE", "1", 1); }
    else if (!std::strcmp(argv[1], "network-probes")) { GAME.mCurrentlyRunningLevel = 856; network = true; setenv("BEA_PORT_PROBES", "1", 1); }
    else if (!std::strcmp(argv[1], "network-ce-off")) { GAME.mCurrentlyRunningLevel = 856; network = true; ce_layer = false; }
    else if (!std::strcmp(argv[1], "bad-pad")) { GAME.mCurrentlyRunningLevel = 856; network = true; pad = 4; }
    else if (!std::strcmp(argv[1], "negative-pad")) { GAME.mCurrentlyRunningLevel = 856; network = true; pad = -1; }
    else if (!std::strcmp(argv[1], "no-device")) { GAME.mCurrentlyRunningLevel = 856; network = true; LT.mUnknown33474[0] = false; }
    else if (!std::strcmp(argv[1], "network-custom")) { GAME.mCurrentlyRunningLevel = 856; network = true; CONTROL_BINDINGS[0].mInput[0] = {0, BUTTON_ON, 3, 0}; }
    else if (!std::strcmp(argv[1], "network-stick")) { GAME.mCurrentlyRunningLevel = 856; network = true; stick_device = true; }
    else return 5;
    SControlInput out[2] = {};
    bool allowed = Allowed(pad);
    int n = PortCeGamepadInputs(BUTTON_MECH_FORWARD, pad, out);
    bool forward = n == 1 && out[0].mPad == pad && out[0].mType == ANALOGUE_MINUS &&
                   out[0].mKey == (stick_device ? ANALOGUE_POV_Y : ANALOGUE_Y1);
    n = PortCeGamepadInputs(BUTTON_FRONTEND_MENU_SELECT, pad, out);
    bool menu = n == 1 && out[0].mPad == pad && out[0].mType == BUTTON_ONCE && out[0].mKey == PAD_A;
    std::printf("%d %d %d\n", int(allowed), int(forward), int(menu));
    return 0;
}
'''


def program(parts):
    text = DRIVER
    for name, value in parts.items():
        token = '@' + name + '@'
        if text.count(token) != 1:
            raise RuntimeError('fixture token mismatch: ' + name)
        text = text.replace(token, value)
    return text


def main():
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('a C++ compiler is required')
    parent = ROOT / 'local-data/port/ce-gamepad-network-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    base = source_parts()
    yes, no = (1, 1, 1), (0, 0, 0)
    cases = {
        'solo100': yes, 'network100': yes, 'network856-host': yes, 'network856-guest': yes,
        'network858': yes, 'split856': yes, 'split858': yes, 'split-player2': yes, 'split-no-second-pad': no,
        'split-record': no, 'split-clock': no, 'split-tape': no, 'split-probes': no, 'split-ce-off': no,
        'split-custom': (1, 0, 1), 'classify': (0, 1, 1),
        'network-record': no, 'network-clock': no, 'network-tape': no, 'network-probes': no,
        'network-ce-off': no, 'bad-pad': no, 'negative-pad': no, 'no-device': no,
        'network-custom': (1, 0, 1), 'network-stick': yes,
    }
    mutants = [
        ('multiplayer-excluded', 'allowed', 'return Ordinary() && pad >= 0', 'return Ordinary() && !GAME.IsMultiplayer() && pad >= 0', 'split856', no),
        ('multiplayer-excluded-858', 'allowed', 'return Ordinary() && pad >= 0', 'return Ordinary() && !GAME.IsMultiplayer() && pad >= 0', 'split858', no),
        ('lockstep-refused', 'allowed', 'return Ordinary() && pad >= 0', 'return Ordinary() && !CE_LockstepRequested() && pad >= 0', 'network856-host', no),
        ('lockstep-only', 'allowed', 'return Ordinary() && pad >= 0', 'return Ordinary() && CE_LockstepRequested() && pad >= 0', 'split856', no),
        ('faithful-bypass', 'allowed', 'return Ordinary() && pad >= 0', 'return pad >= 0', 'network-record', yes),
        ('faithful-bypass-split', 'allowed', 'return Ordinary() && pad >= 0', 'return pad >= 0', 'split-record', yes),
        ('clock-bypass-split', 'ordinary', '"BEA_PORT_CLOCK", ', '', 'split-clock', yes),
        ('missing-device', 'allowed', '&& LT.mUnknown33474[pad];', ';', 'no-device', yes),
        ('pad-range', 'allowed', 'pad < LT.mJoypads && ', '', 'split-no-second-pad', yes),
        ('ignore-custom-binding', 'bindings', 'return false;', 'return true;', 'network-custom', yes),
    ]
    variants = [('actual', None, None, None)]
    for label, part, before, after, expected_case, mutant_result in mutants:
        if base[part].count(before) != 1:
            raise RuntimeError('mutant anchor is not unique: ' + label)
        mutated = dict(base)
        mutated[part] = mutated[part].replace(before, after, 1)
        variants.append((label, mutated, expected_case, mutant_result))
    results = []
    for label, altered, expected_case, mutant_result in variants:
        body = program(base if altered is None else altered)
        target = out / (label + '.cpp')
        target.write_text(body)
        exe = out / label
        built = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', str(target), '-o', str(exe)],
                               capture_output=True, text=True, timeout=60, env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
        (out / (label + '-build.log')).write_text(built.stdout + built.stderr)
        if built.returncode:
            raise RuntimeError('fixture failed to compile: ' + label)
        for name in (cases if expected_case is None else (expected_case,)):
            ran = subprocess.run([str(exe), name], capture_output=True, text=True, timeout=5,
                                 env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
            try:
                got = tuple(int(x) for x in ran.stdout.strip().split())
            except ValueError:
                got = ()
            correct = ran.returncode == 0 and got == cases[name]
            passed = correct if expected_case is None else ran.returncode == 0 and got == mutant_result
            results.append({'variant': label, 'case': name, 'passed': passed, 'exit': ran.returncode})

    # A public build (CE_NETWORK_OFF) admits pads exactly as a network build does and needs no network symbol: the driver for it
    # does not define CE_LockstepRequested, so an admission that called it would not link.
    public = dict(base, lockstep='')
    for label in ('public-off',):
        target = out / (label + '.cpp')
        target.write_text(program(public))
        exe = out / label
        built = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-DCE_NETWORK_OFF=1',
                                str(target), '-o', str(exe)], capture_output=True, text=True, timeout=60,
                               env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
        (out / (label + '-build.log')).write_text(built.stdout + built.stderr)
        if built.returncode:
            raise RuntimeError('public fixture failed to compile: ' + label)
        for name in cases:
            ran = subprocess.run([str(exe), name], capture_output=True, text=True, timeout=5,
                                 env={'PATH': '/usr/bin:/bin', 'LANG': 'C'})
            try:
                got = tuple(int(x) for x in ran.stdout.strip().split())
            except ValueError:
                got = ()
            results.append({'variant': label, 'case': name, 'passed': ran.returncode == 0 and got == cases[name],
                            'exit': ran.returncode})

    sys.path.insert(0, str(ROOT / 'port'))
    import build
    public_view = build.network_view(GAMEPAD.read_text(), off=True).encode()
    pins = [line.split()[0] for line in (ROOT / 'port/network/MAIN-TEXT.sha256').read_text().splitlines()
            if line.endswith('  port/game/ce_gamepad.cpp')]
    public_identity = len(pins) == 1 and hashlib.sha256(public_view).hexdigest() == pins[0]
    results.append({'variant': 'public-off', 'case': 'pinned-source-identity', 'passed': public_identity, 'exit': 0})
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = {'scope': 'actual extracted admission and binding selection; authored states; no game, device or network',
               'inputs': {str(p.relative_to(ROOT)): sha(p) for p in (GAMEPAD, GAME, CONTROLLER)}, 'results': results}
    (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('%d checks, %d failed; evidence: %s' % (len(results), sum(not x['passed'] for x in results), out))
    return 0 if all(x['passed'] for x in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
