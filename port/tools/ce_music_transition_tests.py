#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual playlist -> actual stream worker -> actual mixer with distinguishable authored PCM.

The decoder, event scheduling and SDL device are controlled boundaries; this does not reproduce a player report,
exercise a real audio device, or establish Windows runtime behavior. Outputs and the receipt stay in local-data.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}', start) + 2] + '\n'


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/port/ce-music-transition-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    common = Path(subprocess.check_output(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                          cwd=ROOT, text=True).strip()).parent
    deps = common / 'local-data/port'
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_', 'SDL_AUDIO', 'LD_PRELOAD'))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
    pins = ('src/PCMusicStream.cpp', 'port/game/ce_music.cpp', 'port/game/ce_music.h', 'port/dsound/dsound.cpp',
            'port/tools/tests/audio_refill_driver.cpp', 'port/tools/tests/ce_music_transition_driver.cpp')
    receipt = {'scope': __doc__, 'pins': {name: digest(ROOT / name) for name in pins}, 'commands': [], 'cases': []}
    try:
        music = out / 'music'
        music.mkdir()
        shutil.copyfile(ROOT / 'src/PCMusicStream.cpp', music / 'PCMusicStream.cpp')
        for patch in sorted((ROOT / 'port/patches').glob('*.patch')):
            parts = [p for p in re.split(r'(?=^--- a/)', patch.read_text(), flags=re.M)
                     if p.startswith('--- a/PCMusicStream.cpp\n')]
            if parts:
                receipt['pins']['patch/' + patch.name] = digest(patch)
                p = subprocess.run(['patch', '--batch', '--fuzz=0', '-p1'], cwd=music, input=''.join(parts),
                                   capture_output=True, text=True, timeout=10)
                if p.returncode or 'fuzz' in p.stdout.lower(): raise RuntimeError(p.stdout + p.stderr)
        music_code = re.sub(r'^#include[^\n]*\n', '', (music / 'PCMusicStream.cpp').read_text(), flags=re.M)
        session = function((ROOT / 'port/platform/common/portlog.cpp').read_text(), 'bool PortSessionEnabled()')
        admission = function((ROOT / 'port/platform/common/ce.cpp').read_text(), 'bool PortCeLayer()')
        playlist = (ROOT / 'port/game/ce_music.cpp').read_text()
        variants = {'current': playlist}
        # Each mutant independently removes a stop. Actual mixed PCM must expose the original problem.
        old_play = '\tStopMusicStream(); // the stream must be stopped before a different reader can replace it\n'
        old_clear = '\t\tStopMusicStream();\n\t\tPlayMusicStream(MUSIC.mCurrentSong->mFilename);'
        if playlist.count(old_play) == 1 and playlist.count(old_clear) == 1:
            variants['no-play-stop'] = playlist.replace(old_play, '')
            variants['no-clear-stop'] = playlist.replace(old_clear, '\t\tPlayMusicStream(MUSIC.mCurrentSong->mFilename);')
        mixer = ROOT / 'port/dsound/dsound.cpp'
        for variant, code in variants.items():
            folder = out / variant
            folder.mkdir()
            (folder / 'music.inc').write_text(music_code)
            (folder / 'admission.inc').write_text(session)
            (folder / 'ce-admission.inc').write_text(admission)
            (folder / 'playlist.inc').write_text(re.sub(r'^#include[^\n]*\n', '', code, flags=re.M))
            binary = folder / 'driver'
            command = ['clang++', '-std=c++17', '-O1', '-g', '-pthread', '-DNOMINMAX', '-DINITGUID',
                       '-fms-extensions', '-fno-autolink', '-w', '-DCE_VERSION="test"',
                       '-I' + str(folder), '-I' + str(ROOT / 'port/game'), '-I' + str(ROOT / 'port/d3dx/include'),
                       '-I' + str(ROOT / 'port/include'), '-I' + str(ROOT / 'port/include/linux'),
                       '-I' + str(deps / 'dxvk-native/usr/include/dxvk'), '-I' + str(deps / 'prefix-sniper/include'),
                       '-DMIXER_SOURCE="' + str(mixer) + '"', str(ROOT / 'port/tools/tests/ce_music_transition_driver.cpp'),
                       '-o', str(binary)]
            receipt['commands'].append(command)
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90)
            if built.returncode: raise RuntimeError('build ' + variant + ': ' + built.stderr[-4000:])
            for case in ('add', 'next', 'clear', 'end', 'clear-idle', 'burst'):
                run = subprocess.run([str(binary), case], env=env, capture_output=True, text=True, timeout=20)
                expected_failure = (variant == 'no-play-stop' and case in ('add', 'next')) or (variant == 'no-clear-stop' and case == 'clear')
                expected_text = {'add': 'adddir immediately starts newly selected PCM',
                                 'next': 'next immediately starts newly selected PCM',
                                 'clear': 'clear immediately restores game PCM'}.get(case, '')
                passed = (run.returncode == 2 and expected_text in run.stderr) if expected_failure else run.returncode == 0
                receipt['cases'].append({'variant': variant, 'case': case, 'exit': run.returncode,
                                         'expected_failure': expected_failure, 'passed': passed,
                                         'stdout': run.stdout, 'stderr': run.stderr})
                if not passed: raise RuntimeError(f'{variant} {case}: {run.stdout} {run.stderr}')
        receipt['pass'] = len(variants) == 3
        if not receipt['pass']: raise RuntimeError('both independently controlled stop-removal mutants are required')
    except Exception as exc:
        receipt['pass'] = False
        receipt['failure'] = str(exc)
        raise
    finally:
        (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(out / 'result.json', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
