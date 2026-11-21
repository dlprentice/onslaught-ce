#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile actual SDL fake-pad parser and session transition with authored adapters."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tempfile

from ce_gamepad_tests import function

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / 'port/platform/common/dinput.cpp').read_text()
    struct = source[source.index('struct FakePad\n'):source.index('// A DIPROP_', source.index('struct FakePad\n'))]
    clock = source.split('// CE_FAKE_PAD_GAME_BEGIN:', 1)[1].split('\n', 1)[1].split('// CE_FAKE_PAD_GAME_END', 1)[0]
    parts = {'struct': struct, 'attach': function(source, 'void FakePadAttach()'),
             'clock': clock, 'step': function(source, 'void FakePadStep()')}
    driver = (ROOT / 'port/tools/tests/lockstep_fake_pad_driver.cpp').read_text()
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('C++ compiler required; no checks ran')
    parent = ROOT / 'local-data/lockstep-active-input-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    variants = [('actual', None, None, None, None), ('without-provider', None, None, None, None),
                ('faithful', None, None, None, None),
                ('start-before-running', 'clock', 'if (!running || !f.pad) return present;', 'if (!f.pad) return present;', 'connection wait cannot start'),
                ('absolute-game-clock', 'clock', 'return present - f.game_present;', 'return present;', 'relative movement starts'),
                ('missing-entry-neutral', 'clock', '\t\tFakePadNeutral(f);', '\t\t/* deliberately retain menu controls */', 'entry releases every control'),
                ('missing-exit-neutral', 'clock', '\t\tFakePadNeutral(f);\n\t\tf.game_stopped', '\t\t/* deliberately retain active controls */\n\t\tf.game_stopped', 'departure neutralizes'),
                ('end-on-game-clock', 'clock', '\tif (f.game_stopped) return present - f.end_present;\n', '', 'end script moves on its own clock'),
                ('allow-restart', 'clock', 'if (!f.game_started)', 'if (!f.game_started || f.game_stopped)', 'later session cannot restart'),
                ('parser-uses-global-clock', 'step', 'if (now < present)', 'if (PortPresentCount() < present)', 'relative movement starts'),
                ('late-attach-leaks-menu', 'step', 'now = FakePadPresent(f, PortPresentCount());\n\t\t\t\t\tif (!was_started',
                 '/* omit first attached poll switch */\n\t\t\t\t\tif (!was_started', 'late attach cannot leak a menu press')]
    results = []
    for name, part, old, new, witness in variants:
        selected = dict(parts)
        if part:
            assert old in selected[part], (name, 'mutation anchor missing')
            selected[part] = selected[part].replace(old, new, 1 if name != 'parser-uses-global-clock' else 99)
        text = driver
        for key, value in selected.items():
            text = text.replace('@' + key + '@', value)
        case = out / name; case.mkdir()
        cpp = case / 'driver.cpp'; cpp.write_text(text)
        binary = case / 'driver'
        args = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-address', '-Wno-unused-function', '-Wno-unused-variable']
        if name == 'parser-uses-global-clock': args.append('-Wno-unused-but-set-variable')
        if name != 'faithful': args.append('-DCE_VERSION="test"')
        if name == 'without-provider': args.append('-DNO_LOCKSTEP_PROVIDER')
        build = subprocess.run(args + [str(cpp), '-o', str(binary)], capture_output=True, timeout=40)
        (case / 'build.private.log').write_bytes(build.stdout + build.stderr)
        run = subprocess.run([str(binary)], capture_output=True, timeout=10) if build.returncode == 0 else None
        if run: (case / 'run.private.log').write_bytes(run.stdout + run.stderr)
        passed = build.returncode == 0 and run is not None and ((run.returncode == 0) if not witness else
                    (run.returncode == 1 and witness.encode() in run.stderr))
        results.append({'name': name, 'build_exit': build.returncode, 'run_exit': run.returncode if run else None,
                        'expected_witness': witness, 'passed': passed})
    result = {'passed': all(x['passed'] for x in results), 'cases': results,
              'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
              'scope': 'Actual fake-pad parser and transition, authored SDL/lockstep adapters. No actual game or device acceptance.'}
    (out / 'RESULT.private.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'passed': result['passed'], 'cases': len(results), 'evidence': str(out)}))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()
