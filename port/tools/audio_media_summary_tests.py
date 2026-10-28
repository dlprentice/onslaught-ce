#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual mixer/worker/Bink and logger final-summary controls with authored PCM and controlled SDL.

No game, real audio device, private log or retail content is used. The logger is the production translation unit;
the normal and error event budgets are filled through its public API before the actual mixer teardown.
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


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}', start) + 2] + '\n'


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/port/audio-media-summary-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    common = Path(subprocess.check_output(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                          cwd=ROOT, text=True).strip()).parent
    deps = common / 'local-data/port'
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_', 'SDL_AUDIO', 'LD_PRELOAD'))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
    pins = ('port/dsound/dsound.cpp', 'port/platform/common/portlog.cpp', 'port/platform/common/portlog.h',
            'port/bink/bink.cpp', 'port/tools/tests/audio_refill_driver.cpp', 'port/tools/tests/audio_media_driver.cpp',
            'port/tools/tests/audio_media_summary_driver.cpp')
    receipt = {'scope': __doc__, 'pins': {name: sha(ROOT / name) for name in pins}, 'cases': [], 'commands': []}
    try:
        music = out / 'music'; music.mkdir()
        shutil.copyfile(ROOT / 'src/PCMusicStream.cpp', music / 'PCMusicStream.cpp')
        for patch in sorted((ROOT / 'port/patches').glob('*.patch')):
            parts = [p for p in re.split(r'(?=^--- a/)', patch.read_text(), flags=re.M)
                     if p.startswith('--- a/PCMusicStream.cpp\n')]
            if parts:
                receipt['pins']['patch/' + patch.name] = sha(patch)
                r = subprocess.run(['patch', '--batch', '--fuzz=0', '-p1'], cwd=music, input=''.join(parts),
                                   capture_output=True, text=True, timeout=10)
                if r.returncode or 'fuzz' in r.stdout.lower(): raise RuntimeError(r.stdout + r.stderr)
        music_code = re.sub(r'^#include[^\n]*\n', '', (music / 'PCMusicStream.cpp').read_text(), flags=re.M)
        admission = function((ROOT / 'port/platform/common/ce.cpp').read_text(), 'bool PortCeLayer()')
        bink_source = (ROOT / 'port/bink/bink.cpp').read_text()
        bink = '\n'.join(function(bink_source, x) for x in ('void MakeSound(Video *v)', 'void Free(Video *v)',
                                                             'S32 __stdcall BinkDoFrame(HBINK bink)'))
        mixer = (ROOT / 'port/dsound/dsound.cpp').read_text()
        logger = (ROOT / 'port/platform/common/portlog.cpp').read_text()
        variants = {'current': (mixer, logger)}
        snapshot = '\t\tPortSessionSetFinalMedia(&finalMedia);\n'
        summary = '\tLine("summary", text);\n'
        if mixer.count(snapshot) == 1 and logger.count(summary) == 1:
            variants['drop-final'] = (mixer.replace(snapshot, ''), logger)
            variants['quota-summary'] = (mixer, logger.replace(summary, '\tPortSessionLog(PORT_SESSION_AUDIO, false, "%s", text);\n'))
        for variant, (source, logsource) in variants.items():
            folder = out / variant
            mix = folder / 'port/dsound/dsound.cpp'; mix.parent.mkdir(parents=True)
            header = folder / 'port/platform/common/portlog.h'; header.parent.mkdir(parents=True)
            mix.write_text(source); shutil.copyfile(ROOT / 'port/platform/common/portlog.h', header)
            (folder / 'music.inc').write_text(music_code)
            (folder / 'ce-admission.inc').write_text(admission)
            (folder / 'bink-owner.inc').write_text(bink)
            (folder / 'session-logger.inc').write_text(re.sub(r'^#include[^\n]*\n', '', logsource, flags=re.M))
            binary = folder / 'driver'
            command = ['clang++', '-std=c++17', '-O1', '-g', '-pthread', '-DNOMINMAX', '-DINITGUID',
                       '-fms-extensions', '-fno-autolink', '-w', '-DCE_VERSION="test"', '-DAUDIO_REFILL_REAL_LOGGER',
                       '-I' + str(folder), '-I' + str(ROOT / 'port/d3dx/include'), '-I' + str(ROOT / 'port/include'),
                       '-I' + str(ROOT / 'port/include/linux'), '-I' + str(deps / 'dxvk-native/usr/include/dxvk'),
                       '-I' + str(deps / 'prefix-sniper/include'), '-DMIXER_SOURCE="' + str(mix) + '"',
                       str(ROOT / 'port/tools/tests/audio_media_summary_driver.cpp'), '-o', str(binary)]
            receipt['commands'].append(command)
            b = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90)
            if b.returncode: raise RuntimeError('build ' + variant + ': ' + b.stderr[-4000:])
            for case in ('normal', 'flood', 'empty', 'muted', 'saturate', 'queue-fail', 'disabled'):
                overrides = {'BEA_PORT_TAPE': 'authored'} if case == 'disabled' else {}
                r = subprocess.run([str(binary), case], env=dict(env, **overrides), capture_output=True, text=True, timeout=20)
                (folder / (case + '.log')).write_text(r.stdout + r.stderr)
                summaries = [line for line in r.stderr.splitlines() if ' summary: ' in line]
                finals = [line for line in r.stderr.splitlines() if 'media_final=1' in line]
                expected_lost = (variant == 'drop-final') or (variant == 'quota-summary' and case == 'flood')
                if case == 'disabled':
                    passed = r.returncode == 0 and not summaries and not finals
                elif expected_lost:
                    passed = r.returncode == 0 and not finals
                elif variant == 'quota-summary' and case == 'saturate':
                    # The ordinary event formatter truncates this complete maximum-value snapshot.
                    passed = (r.returncode == 0 and len(finals) == 1 and
                              'retired_queue_observations=18446744073709551615' not in finals[0])
                else:
                    expected_frames = '18446744073709551615' if case == 'saturate' else '0' if case == 'empty' else '64'
                    fields = ('mixed_music_frames=' + expected_frames, 'mixed_film_frames=' + expected_frames,
                              'mixed_both_role_frames=' + expected_frames, 'silence_or_mute_possible=1',
                              'output_device_unobserved=1')
                    passed = (r.returncode == 0 and len(finals) == 1 and all(field in finals[0] for field in fields)
                              and len(finals[0]) < 1200)
                    if variant == 'current': passed = passed and ' summary: ' in finals[0]
                    if case == 'flood':
                        passed = passed and 'ordinary event limit reached' in r.stderr and 'error event limit reached' in r.stderr \
                                 and 'media operation=device-stop' not in r.stderr
                    if case == 'saturate':
                        passed = passed and 'retired_queue_observations=18446744073709551615' in finals[0]
                    if case == 'queue-fail':
                        passed = passed and 'queue_samples=0' in finals[0] and 'queue_failures=1' in finals[0] \
                                 and 'queued_input_last_bytes=-1' in finals[0]
                receipt['cases'].append({'variant': variant, 'case': case, 'exit': r.returncode, 'passed': passed,
                                         'summary_count': len(summaries), 'final_count': len(finals)})
                if not passed: raise RuntimeError(f'{variant} {case}: expected final summary not established; see {folder / (case + ".log")}')
        receipt['pass'] = len(variants) == 3
        if not receipt['pass']: raise RuntimeError('both final-summary negative controls are required')
    except Exception as exc:
        receipt['pass'] = False; receipt['failure'] = str(exc)
        raise
    finally:
        (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(out / 'result.json', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
