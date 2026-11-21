#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the CE Level 100 dodge and zoom cues through the game's own patched code; no game, saves or display.

The fixture compiles the exact patched bodies of the script calls, the radio queue (enqueue, advance, start), the help
renderer, the binding table and lookup, the dash code, the native word wrapper, the port's DirectInput key mapping and
the CE guidance, against synthetic game, clock, font and SDL keyboard-layout boundaries, then checks causal mutants
and both targets' production objects. The mirror provides the pinned-source headers: --mirror names an existing one, and without it the suite builds a full one
in its scratch folder (premirror.py, which needs BEA_GAME_DIR as every mirror does). Fixture rendering uses
artificial font metrics and does not establish visual acceptance.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
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
import premirror


def default_jobs():
    env = os.environ.get('BEA_MUTANT_JOBS', '').strip()
    if env:
        value = int(env)
        if value < 1:
            raise ValueError(f'BEA_MUTANT_JOBS must be >= 1, got {value}')
        return value
    return min(6, os.cpu_count() or 1)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def body(source, pattern):
    m = re.search(pattern, source, re.M)
    if not m:
        raise ValueError('missing source: ' + pattern)
    start = m.start()
    brace = source.index('{', m.end())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


PATCHED = ('HelpText.cpp', 'MissionScript/IScript.cpp', 'MessageBox.cpp', 'Controller.cpp')
QUEUE = (r'^void\s+CMessageBox::SwitchMessagesOn\(', r'^void\s+CMessageBox::TryAdvance\(',
         r'^void\s+CMessageBox::AddMessage\(', r'^void\s+CMessageBox::StartMessage\(')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mirror', type=Path, help="port/build.py's mirror (local-data/port/mirror/src); default: build a full one in the scratch folder")
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('-j', '--jobs', type=int, default=None, help='max parallel compiles/runs (default: BEA_MUTANT_JOBS or min(6, cpu_count))')
    args = parser.parse_args()
    jobs = default_jobs() if args.jobs is None else args.jobs
    if jobs < 1:
        raise SystemExit(f'--jobs must be >= 1, got {jobs}')
    parent = ROOT / 'local-data/port/ce-tutorial-guidance-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    mirror = out / 'mirror'
    mirror.mkdir()
    if args.mirror is None:
        args.mirror = premirror.scratch_mirror(out / 'premirror')
    for src in args.mirror.rglob('*'):
        if src.is_file() and (src.suffix.lower() in ('.h', '.hpp', '.inl', '.stf') or
                              str(src.relative_to(args.mirror)) in PATCHED):
            dest = mirror / src.relative_to(args.mirror)
            dest.parent.mkdir(parents=True, exist_ok=True)
            if src.suffix.lower() == '.cpp':
                # the text a public build compiles: the guidance does not depend on the network, whose hooks are out (build.network_view)
                dest.write_bytes(build.network_view(src.read_bytes().decode('latin-1'), off=True).encode('latin-1'))
            else:
                shutil.copyfile(src, dest)
    # A later Commander presentation patch inserts preprocessor declarations
    # between the UI includes and this tutorial hook. Ignore only those neighboring
    # declarations when reversing/reapplying this hook in the private scratch mirror.
    boxfile=mirror/'MessageBox.cpp'
    box=boxfile.read_text()
    neighboring=""
    match=re.search(r'^#include\s+"game\.h"[^\n]*',box,re.M)
    point=match.start() if match else -1
    directive=box.find('#ifdef CE_VERSION',match.end()) if point>=0 else -1
    if directive>=0 and not box[match.end():directive].strip():
        end=box.find('#endif',directive)
        if end>=0 and 'PortCeOneScreenMatch' in box[directive:end] and 'PortCeTutorial' not in box[directive:end]:
            end=box.index('\n',end)+1
            neighboring=box[directive:end]
            boxfile.write_text(box[:directive]+box[end:])
    patch = ROOT / 'port/patches/0090-ce-tutorial-guidance-IScript.patch'
    # A mirror from port/build.py already carries this patch: take it back out of the copy first.
    reverse = subprocess.run(['patch', '--batch', '--fuzz=0', '-R', '-p1', '--dry-run', '-i', str(patch)], cwd=mirror,
                             capture_output=True, text=True, timeout=10)
    if not reverse.returncode:
        subprocess.run(['patch', '--batch', '--fuzz=0', '-R', '-p1', '-i', str(patch)], cwd=mirror, check=True,
                       capture_output=True, text=True, timeout=10)
    pins = {str(p.relative_to(mirror)): sha(p) for p in sorted(mirror.rglob('*')) if p.is_file()}
    original_script = (ROOT / 'src/MissionScript/IScript.cpp').read_text()
    original_help = (ROOT / 'src/HelpText.cpp').read_text()
    for pattern in (r'^void\s+IScript::AddHelpMessage\(', r'^void\s+IScript::PlayCharMessageWait\('):
        assert body(original_script, pattern) == body((mirror/'MissionScript/IScript.cpp').read_text(), pattern), pattern
    for pattern in (r'^void\s+CHelpTextDisplay::AddMessage\(', r'^void\s+CHelpTextDisplay::Render\('):
        assert body(original_help, pattern) == body((mirror/'HelpText.cpp').read_text(), pattern), pattern
    original_box = (ROOT / 'src/MessageBox.cpp').read_text()
    for pattern in QUEUE[:3]:	# StartMessage also carries patch 0022's virtual-clock voice wait: the fixture runs that form
        assert body(original_box, pattern) == body((mirror/'MessageBox.cpp').read_text(), pattern), pattern
    assert 'PortClockVirtual()' in body((mirror/'MessageBox.cpp').read_text(), QUEUE[3])
    result = subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch)], cwd=mirror,
                            capture_output=True, text=True, timeout=10)
    (out / 'patch.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError('patch did not apply without fuzz: ' + str(out / 'patch.log'))
    if neighboring:
        transformed=boxfile.read_text()
        at=re.search(r'^#include\s+"game\.h"[^\n]*\n',transformed,re.M).end()
        boxfile.write_text(transformed[:at]+neighboring+transformed[at:])
    script = (mirror / 'MissionScript/IScript.cpp').read_text()
    help_source = (mirror / 'HelpText.cpp').read_text()
    box_source = (mirror / 'MessageBox.cpp').read_text()
    dinput = (ROOT / 'port/platform/common/dinput.cpp').read_text()
    keys = (ROOT / 'port/platform/common/ce_tutorial_keys.cpp').read_text()
    controller_h = (ROOT / 'src/Controller.h').read_text()
    controller = (ROOT / 'src/Controller.cpp').read_text()
    console_h = (ROOT / 'src/console.h').read_text()
    game_source = (ROOT / 'src/game.cpp').read_text()
    pc = (ROOT / 'src/PCController.cpp').read_text()
    code = (ROOT / 'port/game/ce_tutorial_guidance.cpp').read_text()
    ce = (ROOT / 'port/platform/common/ce.cpp').read_text()
    keynames = (ROOT / 'port/platform/linux/user.cpp').read_text()
    walker = (ROOT / 'src/BattleEngineWalkerPart.cpp').read_text()
    font = (ROOT / 'src/Font.cpp').read_text()
    joy = re.search(r'enum EJoyButtonPushType\s*\{.*?\};', controller_h, re.S).group(0)
    action_lines = [line for line in controller_h.splitlines() if re.match(r'#define BUTTON_\w+\s', line)]
    axis_lines = [line for line in controller_h.splitlines() if re.match(r'#define ANALOGUE_(?:X1|Y1|X2|Y2|POV_X|POV_Y|ACT_AS_DIGITAL_THRESHOLD)\s', line)]
    structs = controller_h[controller_h.index('struct SControlInput'):controller_h.index('class CController\n')]
    patched_controller = (mirror/'Controller.cpp').read_text()
    mappings = body(patched_controller, r'^void\s+CController::DoMappings\(')
    assert 'int pad = mPadNumber;' in mappings
    assert re.search(r'int input_num = pad;.*?if \(GAME.IsMultiplayer\(\) \|\| input_num != 0\)\s*break;\s*input_num = 1;', mappings, re.S)
    control = joy + '\n' + '\n'.join(action_lines + axis_lines) + '\n' + structs
    constructors = controller[controller.index('SControlBinding::SControlBinding(bool'):controller.index('HRESULT InitMouse()')]
    names = ('BUTTON_MECH_STRAFE_LEFT', 'BUTTON_MECH_STRAFE_RIGHT', 'BUTTON_MECH_CHANGE_ZOOM_IN', 'BUTTON_MECH_CHANGE_ZOOM_OUT')
    rows = []
    for name in names:
        matches = [line.strip() for line in pc.splitlines() if re.match(r'SControlBinding\(1, '+name+r',', line.strip())]
        assert len(matches) == 1, name
        rows.extend(matches)
    dik = (ROOT / 'port/include/linux/dinput.h').read_text()
    dik_codes = '\n'.join(re.findall(r'^#define\s+DIK_\w+\s+0x[0-9a-fA-F]+', dik, re.M))
    assert 'DIK_A ' in dik_codes and 'DIK_RIGHT ' in dik_codes
    bindings = dik_codes + '\n' + constructors + '\nSControlBinding CONTROL_BINDINGS[] = {\n' + '\n'.join(rows) + '\nSControlBinding()};\n'
    pieces = {
        'control.inc': control,
        'bindings.inc': bindings,
        'lookup.inc': body(controller, r'^SControlBinding \*FindControlBinding\('),
        'player_pad.inc': re.search(r'int GetPadNumber\(\) \{[^}]+\}', controller_h).group(0) + '\n',
        'game_controller.inc': body(game_source, r'^CCONTROLLER \*CGame::GetController\('),
        'console_types.inc': (re.search(r'enum ECVarType\s*\{.*?\};', console_h, re.S).group(0) + '\n' +
                              re.search(r'struct CConsoleVar\s*\{.*?\};', console_h, re.S).group(0)),
        'console_find.inc': body(console_h, r'CConsoleVar\s*\*FindVariable\(char \*name\)'),
        'analog_dead_zone.inc': '\n'.join(re.findall(r'^#define ANALOGUE_DEAD_ZONE(?:_SCALE)?\s+[^\n]+', controller, re.M)) + '\n',
        'mapped_input.inc': body(controller, r'^float\s+CController::GetMappedInputValue\('),
        'mapping_probe.inc': body(patched_controller, r'^inline float DigitalValueForButton\(') +
                             mappings.replace('void CController::DoMappings()', 'void MappingProbe::DoMappings()', 1),
        'key_name.inc': body(keynames, r'^extern "C" int WINAPI GetKeyNameTextA\('),
        'font.inc': body(font, r'^int\s+CDXBitmapFont::WordWrap\('),
        'ce_layer.inc': body(ce, r'^bool PortCeLayer\('),
        'pad_header.inc': (ROOT/'port/include/ce_tutorial_pad.h').read_text(),
        'pad_info.inc': body(dinput, r'^bool OrdinaryCeInput\(') +
                        body(dinput, r'^bool BindableGamepadTriggers\(') +
                        body(dinput, r'^extern "C" int PortCeTutorialPadInfo\('),
        'gamepad.inc': re.sub(r'^#include\s+.*$', '', build.network_view((ROOT/'port/game/ce_gamepad.cpp').read_text(), off=True), flags=re.M),
        'walker.inc': '\n'.join(re.search(r'^(?:float|int) CBattleEngineWalkerPart::'+n+r'\s*=.*?;', walker, re.M).group(0)
                                for n in ('mDashTime', 'mDashStart', 'mDashEnd', 'mDashLength', 'mDashVelocity')) + '\n' +
                      body(walker, r'^void\s+CBattleEngineWalkerPart::StrafeLeft\(') +
                      body(walker, r'^void\s+CBattleEngineWalkerPart::StrafeRight\('),
        'guidance.inc': re.sub(r'^#include\s+.*$', '', code, flags=re.M),
        'help.inc': body(help_source, r'^void\s+CHelpTextDisplay::AddMessage\(') + body(help_source, r'^void\s+CHelpTextDisplay::Render\('),
        'script.inc': body(script, r'^void\s+IScript::AddHelpMessage\(') + body(script, r'^void\s+IScript::PlayCharMessageWait\('),
        'message.inc': ''.join(body(box_source, p) for p in QUEUE),
        'dik.inc': body(dinput, r'^unsigned char DikFor\(') + '#ifdef CE_VERSION\n' +
                   body(dinput, r'^SDL_Scancode PortScancodeForDik\(') + '#endif\n',
        'key_layout.inc': re.sub(r'^#include\s+.*$', '', keys, flags=re.M),
    }
    for name, value in pieces.items():
        (out / name).write_text(value)
    driver = ROOT / 'port/tools/ce_tutorial_guidance_driver.cpp'
    SDL_INCLUDE = build.deps.PREFIX / 'include'	# the selected pinned SDL3 headers (types only; SDL is not linked)
    if not (SDL_INCLUDE / 'SDL3/SDL.h').exists():
        raise RuntimeError('pinned SDL3 headers missing; run port/tools/deps.py build: ' + str(SDL_INCLUDE))
    cases, commands = [], []

    def compile_case(folder, version=True):
        binary = folder / 'fixture'
        cmd = [args.compiler, '-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I'+str(folder), '-I'+str(SDL_INCLUDE), '-I'+str(ROOT / 'port/game'),
               *(['-DCE_VERSION="fixture"'] if version else []), str(driver), '-o', str(binary)]
        run = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        (folder / 'build.log').write_text(run.stdout+run.stderr)
        if run.returncode:
            raise RuntimeError('fixture compilation failed: '+str(folder / 'build.log'))
        return binary, cmd

    def run_check(binary, mode, wanted=0, message=''):
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
        p = quiet_crash.run([str(binary), mode], capture_output=True, text=True, env=env, timeout=30)
        ok = p.returncode == wanted and (not message or message in p.stderr)
        case = {'mode': mode, 'binary': binary.parent.name, 'expected': wanted, 'exit': p.returncode,
                'stdout': p.stdout, 'stderr': p.stderr, 'passed': ok}
        return case, ('PASS ' if ok else 'FAIL ') + binary.parent.name + ' ' + mode

    def variant(name, edits):
        folder = out / name
        folder.mkdir()
        for file, value in pieces.items():
            for target, old, new, count in edits:
                if file == target:
                    assert value.count(old) == count, (name, target, old, value.count(old))
                    value = value.replace(old, new)
            (folder / file).write_text(value)
        return folder

    modes = ['normal', 'expire', 'refresh', 'yield', 'slots', 'layer-force', 'empty-tape',
             # the radio queue: the cue starts with the message, never at enqueue, and never for a dropped message
             'queue-busy', 'queue-blocked', 'dead-queued', 'deleted-queued', 'dead-before-start',
             # the player's keyboard layout and bindings, and the generic text when a binding cannot be named
             'layout-azerty', 'layout-nonlatin', 'layout-unknown', 'remap-keys', 'remap-arrows', 'remap-layout', 'secondary',
             'joystick', 'unbound', 'duplicate', 'strafe-once', 'wheel-held', 'unprintable', 'long-labels', 'font-no-dot',
             'pad-default', 'stick-default', 'pad-custom', 'pad-incomplete', 'pad-missing-axis', 'pad-missing-button',
             'pad-disconnected', 'pad-hotplug', 'pad-swap', 'pad-wrong-sign', 'pad-long-keys', 'pad-face', 'pad-disabled',
             'pad-second', 'pad-nonlatin', 'pad-fence', 'pad-player-two', 'keyboard-player-two', 'no-controller', 'stick-no-twist', 'pad-strafe-buttons',
             'timing-change', 'pad-timing-change', 'timing-missing', 'timing-missing-generic', 'timing-type', 'timing-null', 'timing-nan', 'timing-small', 'timing-large',
             'pad-mouse-blocked', 'pad-mouse-normal', 'pad-mouse-player-two', 'pad-mouse-default', 'pad-mouse-zoom',
             # the Dodge Button in the cue, on and off
             'dodge-keys', 'dodge-both', 'dodge-pad', 'dodge-pad-bound', 'dodge-stick', 'dodge-key-bound', 'dodge-off', 'dodge-player-two',
             'mouse-blocked-keys', 'mouse-unbound-keys',
             # faithful, replay and network fences, and missing game objects
             'layer-off', 'demo', 'autoconfig', 'record-demo', 'play-demo', 'multiplayer', 'other-level', 'language',
             'font-missing', 'font-invalid', 'event-exhausted', 'box-missing', 'help-missing']
    modes += ['forced-'+name for name in ('BEA_PORT_CLOCK', 'BEA_PORT_TAPE', 'BEA_PORT_PROBES', 'BEA_PORT_RECORD', 'BEA_NET_HOST', 'BEA_NET_JOIN')]

    # Key names (24 characters, the longest admitted) that overflow the native line together: the cue must use the
    # generic text, still under 100.
    long_name = [('guidance.inc', '"Right arrow"', '"Right arrow on keypad 6X"', 1), ('guidance.inc', '"Right Shift"', '"Right Shift (left side)X"', 1)]
    long_dir = variant('long-names', long_name)
    long_unbounded_dir = variant('long-names-unbounded', long_name + [('guidance.inc', 'const int kBufferSize = 100;', 'const int kBufferSize = 192;', 1)])
    private = out / 'private'
    private.mkdir()
    for name, value in pieces.items():
        (private / name).write_text(value)

    mutants = [
        # each must be caught: (name, edits, mode, the failing check)
        ('cue-at-enqueue', [('message.inc', 'PortCeTutorialRadioStart(mCurrent->mStringId);', '', 1),
                            ('script.inc', 'GAME.GetMessageBox()->AddMessage(message);', 'GAME.GetMessageBox()->AddMessage(message), PortCeTutorialRadioStart(args[1]->GetInt());', 1)],
         'queue-busy', 'no cue when the script only queues the radio'),
        ('cue-at-advance', [('message.inc', 'PortCeTutorialRadioStart(mCurrent->mStringId);', '', 1),
                            ('message.inc', 'mStartTime = EVENT_MANAGER.GetTime();', 'mStartTime = EVENT_MANAGER.GetTime(); PortCeTutorialRadioStart(m->mStringId);', 1)],
         'queue-busy', 'no cue before the radio'),
        ('cue-before-speaker-check', [('message.inc', 'PortCeTutorialRadioStart(mCurrent->mStringId);', '', 1),
                                      ('message.inc', 'void\tCMessageBox::StartMessage()\t// FUNCTION: 0x004b7ea0\n{', 'void\tCMessageBox::StartMessage()\t// FUNCTION: 0x004b7ea0\n{ PortCeTutorialRadioStart(mCurrent->mStringId);', 1)],
         'dead-before-start', 'a speaker dead at the start shows no cue'),
        ('no-radio-hook', [('message.inc', 'PortCeTutorialRadioStart(mCurrent->mStringId);', '', 1)], 'normal', 'dodge cue admission'),
        ('us-table', [('key_layout.inc', 'SDL_GetKeyFromScancode(scan, SDL_KMOD_NONE, false)',
                       "(scan >= SDL_SCANCODE_A && scan <= SDL_SCANCODE_Z ? (SDL_Keycode)('a' + scan - SDL_SCANCODE_A) : SDL_GetKeyFromScancode(scan, SDL_KMOD_NONE, false))", 1)],
         'layout-azerty', 'dodge names the current keys'),
        ('accept-any-keycode', [('key_layout.inc', "if (!((current >= 'A' && current <= 'Z') || (current >= '0' && current <= '9')))", 'if (!current)', 1)],
         'layout-nonlatin', 'use the generic text'),
        ('stale-on-remap', [('help.inc', 'PortCeTutorialRefresh(this);', '(void)this;', 1)], 'refresh', 'refreshed before the draw'),
        ('no-dedupe', [('guidance.inc', 'if (sKind[owned] == kind)', 'if (false)', 1)], 'normal', 'a repeated start does not duplicate'),
        ('stack-cues', [('guidance.inc', 'if (retire >= 0)\n\t\thelp->mMessages[retire].mActive = FALSE;', '', 1),
                        ('guidance.inc', 'memcpy(sText[0], text, sizeof(text));\n\tsKind[0] = kind;\n\thelp->AddMessage(sText[0]);',
                         'int k = retire >= 0 ? 1 : 0; memcpy(sText[k], text, sizeof(text)); sKind[k] = kind; help->AddMessage(sText[k]);', 1)],
         'normal', 'one CE cue at a time'),
        ('forced-tape-cues', [('guidance.inc', '"BEA_PORT_TAPE"', '"BEA_UNUSED_TAPE"', 1)], 'forced-BEA_PORT_TAPE', 'dodge cue admission'),
        ('network-cues', [('guidance.inc', '"BEA_NET_JOIN"', '"BEA_UNUSED_JOIN"', 1)], 'forced-BEA_NET_JOIN', 'dodge cue admission'),
        ('multiplayer-cues', [('guidance.inc', 'GAME.IsMultiplayer() || ', '', 1)], 'multiplayer', 'dodge cue admission'),
        ('wrong-wheel', [('guidance.inc', '"Mouse wheel up", "Mouse wheel down"', '"Mouse wheel down", "Mouse wheel up"', 1)], 'normal', 'actual default wheel'),
        ('unplugged-pad', [('pad_info.inc', 'd->pad && SDL_GamepadConnected(d->pad)', 'd->pad', 1)], 'pad-disconnected', 'dodge names the current keys'),
        ('ignore-custom', [('gamepad.inc', 'if (!n && NoJoystickBindings())', 'if (!n)', 1)], 'pad-incomplete', 'dodge names the current keys'),
        ('ignore-axis-capability', [('pad_info.inc', 'if (SDL_GamepadHasAxis(d->pad, axes[i])) result.axes |= 1u << i;', 'result.axes |= 1u << i;', 1)],
         'pad-missing-axis', 'dodge names the current keys'),
        ('unrotated-pad-buttons', [('guidance.inc', 'int raw = rotated && button < 4 ? (button + 1) % 4 : button;', 'int raw = button;', 1)],
         'pad-face', 'rotated physical face buttons named'),
        ('pad-stale-hotplug', [('help.inc', 'PortCeTutorialRefresh(this);', '(void)this;', 1)], 'pad-hotplug', 'connecting a pad refreshes'),
        ('ignore-pad-disabled', [('guidance.inc', '!LT.mUnknown33474[pad] || ', '', 1)], 'pad-disabled', 'dodge names the current keys'),
        ('gamepad-wrong-zoom', [('gamepad.inc', '{BUTTON_MECH_CHANGE_ZOOM_IN, BUTTON_ONCE, PAD_RB}',
                                 '{BUTTON_MECH_CHANGE_ZOOM_IN, BUTTON_ONCE, PAD_LB}', 1)],
         'pad-default', 'available default controller zoom and mouse named'),
        ('wrong-player-port', [('guidance.inc', 'return pad == 0 || pad == 1 ? pad : -1;',
                                'return pad == 0 || pad == 1 ? 0 : -1;', 1)],
         'pad-player-two', 'a connected pad remains usable'),
        ('wrong-binding-slots', [('guidance.inc', 'for (int i = playerPad; i < 2; ++i)', 'for (int i = 0; i < 2; ++i)', 2)],
         'keyboard-player-two', 'dodge names the current keys'),
        ('mouse-axis-slot-bypass', [('guidance.inc',
                                    'return first.mType != MOUSE_AXIS_PLUS_ANY && first.mType != MOUSE_AXIS_MINUS_ANY &&\n\t\tfirst.mType != MOUSE_AXIS_PLUS && first.mType != MOUSE_AXIS_MINUS;',
                                    'return true;', 1)], 'pad-mouse-blocked', 'unsupported movement binding uses the generic text'),
        ('mouse-rest-ignored', [('mapping_probe.inc', 'if (sMouse)\n\t\t\t\t\t\t{', 'if (false)\n\t\t\t\t\t\t{', 1)],
         'pad-mouse-blocked', 'slot-0 mouse rest suppresses slot-1 joystick'),
        ('mouse-default-blocked', [('mapping_probe.inc', '(IsButtonSet(mapping->mId) && !mouse_rest_only)',
                                    'IsButtonSet(mapping->mId)', 1)],
         'pad-mouse-default', 'centered mouse does not suppress the CE default'),
        ('no-original-priority', [('script.inc', 'PortCeTutorialYield(help);', '(void)help;', 1)], 'yield', 'CE cue yields'),
        ('altered-radio', [('script.inc', 'args[1]->GetInt(), time, from, done, 10', 'args[1]->GetInt(), time + 1.f, from, done, 10', 1)], 'normal', 'radio message and schedule'),
        ('wrong-dash-window', [('walker.inc', 't > ( EVENT_MANAGER.GetTime() - mDashTime)', 't < ( EVENT_MANAGER.GetTime() - mDashTime)', 2)], 'normal', 'dash timing'),
        ('weak-dash-start', [('walker.inc', 'float CBattleEngineWalkerPart::mDashStart = 0.9f;',
                              'float CBattleEngineWalkerPart::mDashStart = 0.1f;', 1)], 'normal', 'partial first strafe cannot start a dash'),
        ('weak-dash-end', [('walker.inc', 'float CBattleEngineWalkerPart::mDashEnd = 0.8f;',
                            'float CBattleEngineWalkerPart::mDashEnd = 0.1f;', 1)], 'normal', 'partial opposite strafe cannot finish a dash'),
        ('no-deadzone', [('analog_dead_zone.inc', '#define ANALOGUE_DEAD_ZONE 0.15f', '#define ANALOGUE_DEAD_ZONE 0.f', 1),
                         ('analog_dead_zone.inc', '#define ANALOGUE_DEAD_ZONE_SCALE ((float)(1.0 / (1.0 - 0.15)))',
                          '#define ANALOGUE_DEAD_ZONE_SCALE 1.f', 1)],
         'normal', 'partial first deflection fails after deadzone'),
        ('fixed-dash-time', [('guidance.inc', 'float duration = *static_cast<float *>(var->mData);', 'float duration = 0.2f;', 1)],
         'timing-change', 'registered dash timing changes the cue'),
        ('dodge-off-ignored', [('guidance.inc', 'const bool on = PortCeDodgeAssistOn();', 'const bool on = true;', 1)], 'dodge-off',
         'without a usable Dodge Button the cue is the one built from the bindings'),
        ('dodge-key-bound-ignored', [('guidance.inc', 'on && PlayerPad() == 0 && PortCeDodgeAssistKeyFree()', 'on && PlayerPad() == 0', 1)], 'dodge-key-bound',
         'without a usable Dodge Button the cue is the one built from the bindings'),
        ('dodge-pad-bound-ignored', [('guidance.inc', '|| !PortCeDodgeAssistPadFree(playerPad)) return false;', ') return false;', 1)], 'dodge-pad-bound',
         'no gamepad button when it is bound to another control'),
        ('dodge-stick-button', [('guidance.inc', '|| info.kind == CE_TUTORIAL_STICK) return false;\n\tSControlInput input', ') return false;\n\tSControlInput input', 1)], 'dodge-stick',
         'no gamepad button when it is bound to another control, or on a flight stick'),
        ('dodge-key-on-player-two', [('guidance.inc', 'on && PlayerPad() == 0 && PortCeDodgeAssistKeyFree()', 'on && PortCeDodgeAssistKeyFree()', 1)], 'dodge-player-two',
         'dodge names the current keys'),
        ('dodge-no-plain-form', [('guidance.inc', 'for (int form = 0; form < 2; ++form)', 'for (int form = 0; form < 1; ++form)', 1)], 'dodge-both',
         'the key and the gamepad button; the double tap does not fit and is left out'),
        ('displace-help', [('guidance.inc', 'return;\t\t// never displace the game\'s own help', '{ help->mMessages[0].mActive = FALSE; }', 1)], 'slots', 'never overwrite'),
    ]
    mutant_folders = [(variant(name, edits), mode, message) for name, edits, mode, message in mutants]

    def run_mutant(folder, mode, message):
        m_bin, cmd = compile_case(folder)
        case, line = run_check(m_bin, mode, 2, message)
        return cmd, case, line

    toolchains = {}
    alloc_syms = {}
    GAME_SOURCES = (ROOT/'port/game/ce_tutorial_guidance.cpp', ROOT/'port/game/ce_gamepad.cpp',
                    mirror/'HelpText.cpp', mirror/'MissionScript/IScript.cpp', mirror/'MessageBox.cpp')
    for platform in ('linux', 'windows'):
        target = build.TARGETS[platform]
        compiler = Path(target['cxx'][0])
        version = subprocess.run([str(compiler), '--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
        if platform == 'linux' and not version.startswith(build.LINUX_CLANG_VERSION):
            raise RuntimeError('wrong Linux compiler: '+version)
        toolchains[platform] = {'version': version, 'binary_sha256': sha(compiler)}
        sym_dir = out / f'syms-{platform}'
        sym_dir.mkdir()
        alloc_syms[platform] = build.allocator_syms(platform, sym_dir)

    def run_syntax(platform, source):
        target = build.TARGETS[platform]
        includes = [build.PORT/'d3dx/include', build.PORT/'include', build.PORT/'game', *target['includes'], mirror, mirror/'MissionScript']
        cmd = [*target['cxx'], '-std=gnu++14', *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS,
               '-DCE_VERSION="fixture"', '-fsyntax-only', '-include', str(build.PORT/'include/port.h'),
               *['-I'+str(p) for p in includes], str(source)]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        name = platform+'-'+source.name+'.log'
        (out / name).write_text(p.stdout+p.stderr)
        rec = {'platform': platform, 'source': source.name, 'exit': p.returncode, 'command': cmd, 'log': name}
        return rec, ('PASS ' if not p.returncode else 'FAIL ') + platform + ' syntax ' + source.name

    obj_names = {
        'ce_tutorial_guidance.cpp': ('PortCeTutorialHelpCue(int)', 'PortCeTutorialRadioStart(int)', 'PortCeTutorialYield(CHelpTextDisplay*)',
                                     'PortCeTutorialRefresh(CHelpTextDisplay*)', 'PortCeTutorialPrintableKey'),
        'ce_gamepad.cpp': ('PortCeGamepadInputs(int, int, SControlInput*)',),
        'IScript.cpp': ('PortCeTutorialHelpCue(int)', 'PortCeTutorialYield(CHelpTextDisplay*)'),
        'HelpText.cpp': ('PortCeTutorialRefresh(CHelpTextDisplay*)',),
        'MessageBox.cpp': ('PortCeTutorialRadioStart(int)',),
    }

    def run_game_obj(platform, public, source):
        target = build.TARGETS[platform]
        includes = [build.PORT/'d3dx/include', build.PORT/'include', build.PORT/'game', *target['includes'], mirror, mirror/'MissionScript']
        tag = platform + ('-ce-' if public else '-faithful-') + source.stem
        ir, obj = out/(tag+'.ll'), out/(tag+'.o')
        cmds = [
            [*target['cxx'], '-std=gnu++14', *build.MSCXX, *build.COMMON, *build.X87, *build.NO_MATH_BUILTINS,
             *(['-DCE_VERSION="fixture"'] if public else []), '-include', str(build.PORT/'include/port.h'),
             *['-I'+str(p) for p in includes], '-S', '-emit-llvm', '-Xclang', '-disable-llvm-passes', '-o', str(ir), str(source)],
            [sys.executable, str(build.PORT/'tools/qifist.py'), str(ir)],
            [*target['cxx'], *build.COMMON, *build.X87, '-c', '-o', str(obj), str(ir)],
            [build.objcopy(platform), '--redefine-syms='+str(alloc_syms[platform]), str(obj)],
        ]
        logs = []
        for cmd in cmds:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
            logs.append(p.stdout+p.stderr)
            if p.returncode:
                break
        (out/(tag+'.log')).write_text(''.join(logs))
        if p.returncode:
            raise RuntimeError('production object pipeline failed: '+str(out/(tag+'.log')))
        nm = build.deps.DATA / ('llvm/bin/llvm-nm' if platform == 'linux' else 'llvm-mingw/bin/llvm-nm')
        symbols = subprocess.run([str(nm), '-C', str(obj)], capture_output=True, text=True, check=True).stdout
        expected = obj_names[source.name] if public else ()
        good = all(n in symbols for n in expected) and (public or 'PortCeTutorial' not in symbols)
        rec = {'platform': platform, 'ce': public, 'source': source.name, 'sha256': sha(obj), 'symbols_ok': good, 'commands': cmds}
        return rec, ('PASS ' if good else 'FAIL ') + tag + ' production object symbols'

    layout = ['-std=gnu++17', '-DNOMINMAX', '-fms-extensions', '-fno-autolink', '-fno-strict-aliasing',
              '-fwrapv', '-mms-bitfields', '-fshort-wchar', '-g', '-O2', '-w']

    def run_dinput_obj(platform, public):
        target = build.TARGETS[platform]
        prefix = build.deps.PREFIX if platform == 'linux' else build.deps.PREFIX_WIN
        includes = [build.PORT/'d3dx/include', build.PORT/'include', *target['includes'], prefix/'include']
        tag = platform + ('-ce-' if public else '-faithful-') + 'dinput'
        obj = out/(tag+'.o')
        cmd = [*target['cxx'], *layout, *(['-DCE_VERSION="fixture"'] if public else []),
               *['-I'+str(p) for p in includes], '-c', '-o', str(obj), str(ROOT/'port/platform/common/dinput.cpp')]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        (out/(tag+'.log')).write_text(p.stdout+p.stderr)
        if p.returncode:
            raise RuntimeError('production port-object build failed: '+str(out/(tag+'.log')))
        nm = build.deps.DATA / ('llvm/bin/llvm-nm' if platform == 'linux' else 'llvm-mingw/bin/llvm-nm')
        symbols = subprocess.run([str(nm), '-C', str(obj)], capture_output=True, text=True, check=True).stdout
        good = ('PortCeTutorialPadInfo' in symbols) == public
        rec = {'platform': platform, 'ce': public, 'source': 'dinput.cpp', 'sha256': sha(obj), 'symbols_ok': good, 'commands': [cmd]}
        return rec, ('PASS ' if good else 'FAIL ') + tag + ' production port-object symbols'

    syntax_tasks = [(platform, source) for platform in ('linux', 'windows') for source in GAME_SOURCES]
    game_obj_tasks = [(platform, public, source) for platform in ('linux', 'windows') for public in (True, False) for source in GAME_SOURCES]
    dinput_tasks = [(platform, public) for platform in ('linux', 'windows') for public in (True, False)]
    faithful_modes = ('normal', 'queue-busy', 'refresh', 'layout-azerty')
    syntax, objects = [], []

    if jobs == 1:
        binary, base_cmd = compile_case(out)
        commands.append(base_cmd)
        for mode in modes:
            case, line = run_check(binary, mode)
            cases.append(case); print(line, flush=True)
        long_bin, long_cmd = compile_case(long_dir)
        commands.append(long_cmd)
        case, line = run_check(long_bin, 'long-names')
        cases.append(case); print(line, flush=True)
        long_ub_bin, long_ub_cmd = compile_case(long_unbounded_dir)
        commands.append(long_ub_cmd)
        case, line = run_check(long_ub_bin, 'long-names', 2, 'cue fits the native 100-character line')
        cases.append(case); print(line, flush=True)
        faithful, faithful_cmd = compile_case(private, False)
        commands.append(faithful_cmd)
        for mode in faithful_modes:
            case, line = run_check(faithful, mode)
            cases.append(case); print(line, flush=True)
        for folder, mode, message in mutant_folders:
            cmd, case, line = run_mutant(folder, mode, message)
            commands.append(cmd); cases.append(case); print(line, flush=True)
        for st in syntax_tasks:
            rec, line = run_syntax(*st)
            syntax.append(rec); print(line, flush=True)
        for gt in game_obj_tasks:
            rec, line = run_game_obj(*gt)
            objects.append(rec); print(line, flush=True)
        for dt in dinput_tasks:
            rec, line = run_dinput_obj(*dt)
            objects.append(rec); print(line, flush=True)
    else:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            f_base = pool.submit(compile_case, out, True)
            f_long = pool.submit(compile_case, long_dir, True)
            f_long_ub = pool.submit(compile_case, long_unbounded_dir, True)
            f_faithful = pool.submit(compile_case, private, False)
            mutant_futures = [pool.submit(run_mutant, folder, mode, message) for folder, mode, message in mutant_folders]
            syntax_futures = [pool.submit(run_syntax, *st) for st in syntax_tasks]
            game_obj_futures = [pool.submit(run_game_obj, *gt) for gt in game_obj_tasks]
            dinput_futures = [pool.submit(run_dinput_obj, *dt) for dt in dinput_tasks]

            binary, base_cmd = f_base.result()
            long_bin, long_cmd = f_long.result()
            long_ub_bin, long_ub_cmd = f_long_ub.result()
            faithful, faithful_cmd = f_faithful.result()
            commands.extend([base_cmd, long_cmd, long_ub_cmd, faithful_cmd])

            mode_futures = [pool.submit(run_check, binary, mode) for mode in modes]
            f_long_check = pool.submit(run_check, long_bin, 'long-names')
            f_long_ub_check = pool.submit(run_check, long_ub_bin, 'long-names', 2, 'cue fits the native 100-character line')
            faithful_futures = [pool.submit(run_check, faithful, mode) for mode in faithful_modes]

            for future in mode_futures:
                case, line = future.result()
                cases.append(case); print(line, flush=True)
            for future in (f_long_check, f_long_ub_check, *faithful_futures):
                case, line = future.result()
                cases.append(case); print(line, flush=True)
            for future in mutant_futures:
                cmd, case, line = future.result()
                commands.append(cmd); cases.append(case); print(line, flush=True)
            for future in syntax_futures:
                rec, line = future.result()
                syntax.append(rec); print(line, flush=True)
            for future in game_obj_futures:
                rec, line = future.result()
                objects.append(rec); print(line, flush=True)
            for future in dinput_futures:
                rec, line = future.result()
                objects.append(rec); print(line, flush=True)
    inputs = [patch, driver, ROOT/'port/game/ce_tutorial_guidance.cpp', ROOT/'port/game/ce_gamepad.cpp',
              ROOT/'port/include/ce_tutorial_pad.h', ROOT/'src/Controller.h',
              ROOT/'src/Controller.cpp', ROOT/'src/PCController.cpp', ROOT/'src/game.cpp', ROOT/'src/console.h', ROOT/'src/MissionScript/IScript.cpp',
              ROOT/'src/HelpText.cpp', ROOT/'src/Font.cpp', ROOT/'src/BattleEngineWalkerPart.cpp', ROOT/'port/platform/linux/user.cpp', ROOT/'port/platform/common/ce.cpp',
              ROOT/'src/MessageBox.cpp', ROOT/'port/platform/common/dinput.cpp', ROOT/'port/platform/common/ce_tutorial_keys.cpp']
    receipt = {'inputs':{str(p.relative_to(ROOT)):sha(p) for p in inputs}, 'pieces':{k:sha(out/k) for k in pieces},
               'mirror_inputs':pins, 'toolchains':toolchains, 'cases':cases, 'syntax':syntax, 'objects':objects, 'commands':commands,
               'limits':['Synthetic font, game, message and device boundaries; no actual game, graphics, audio or player tested.',
                         'Mirror headers are external to this test; actual asset contents are not logged.']}
    (out / 'result.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(out / 'result.json')
    return 0 if all(c['passed'] for c in cases) and all(s['exit']==0 for s in syntax) and all(o['symbols_ok'] for o in objects) else 1


if __name__ == '__main__':
    raise SystemExit(main())
