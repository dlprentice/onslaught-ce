#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual replacement mixer, complete music worker and Bink buffer-lifecycle diagnostic controls.

SDL/output queues and the Bink DecodeAudio endpoint are controlled; no decoder-fidelity, game, device,
Windows runtime or audible-overlap result is claimed. Retained outputs use authored PCM only.
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


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}', start) + 2] + '\n'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/port/audio-media-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    common = Path(subprocess.check_output(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT, text=True).strip()).parent
    deps = common / 'local-data/port'
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_', 'SDL_AUDIO', 'LD_PRELOAD'))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
    receipt = {'scope': __doc__, 'commands': [], 'cases': [], 'pins': {}, 'pcm_comparisons': []}
    for name in ('port/dsound/dsound.cpp', 'port/bink/bink.cpp', 'port/platform/common/portlog.h',
                 'port/tools/tests/audio_media_driver.cpp', 'port/tools/tests/audio_refill_driver.cpp',
                 'port/tools/tests/audio_media_baseline.patch', 'port/tools/audio_media_tests.py'):
        receipt['pins'][name] = sha(ROOT / name)

    def run(command, label, cwd=ROOT, expected=0, reason='', variables=None):
        receipt['commands'].append(list(map(str, command)))
        r = subprocess.run(list(map(str, command)), cwd=cwd, env=dict(env, **(variables or {})), capture_output=True, text=True, timeout=45)
        (out / (label + '.log')).write_text(r.stdout + r.stderr)
        passed = r.returncode == expected and (not reason or reason in r.stderr)
        row = {'label': label, 'exit': r.returncode, 'expected': expected, 'passed': passed, 'stdout': r.stdout, 'stderr': r.stderr}
        receipt['cases'].append(row)
        if not passed:
            raise RuntimeError(label + ': ' + r.stderr[-3000:])
        return r

    try:
        music = out / 'music'; music.mkdir()
        shutil.copyfile(ROOT / 'src/PCMusicStream.cpp', music / 'PCMusicStream.cpp')
        for patch in sorted((ROOT / 'port/patches').glob('*.patch')):
            parts = [p for p in re.split(r'(?=^--- a/)', patch.read_text(), flags=re.M) if p.startswith('--- a/PCMusicStream.cpp\n')]
            if parts:
                receipt['pins']['patch/' + patch.name] = sha(patch)
                r = subprocess.run(['patch', '--batch', '--fuzz=0', '-p1'], cwd=music, input=''.join(parts), capture_output=True, text=True, timeout=10)
                if r.returncode: raise RuntimeError(r.stdout + r.stderr)
        music_code = re.sub(r'^#include[^\n]*\n', '', (music / 'PCMusicStream.cpp').read_text(), flags=re.M)
        session = function((ROOT / 'port/platform/common/portlog.cpp').read_text(), 'bool PortSessionEnabled()')
        admission = function((ROOT / 'port/platform/common/ce.cpp').read_text(), 'bool PortCeLayer()')
        bink_source = (ROOT / 'port/bink/bink.cpp').read_text()
        bink = '\n'.join(function(bink_source, x) for x in ('void MakeSound(Video *v)', 'void Free(Video *v)', 'S32 __stdcall BinkDoFrame(HBINK bink)'))
        variants = {
            'current': None,
            'before': None,
            'no-music-tag': ('music.inc', '!PortAudioMediaRole(sBuffer, PORT_SESSION_MUSIC)', 'false', 'music', 'active-role processed frame spans'),
            'no-film-tag': ('bink-owner.inc', '!PortAudioMediaRole(b, PORT_SESSION_FILM)', 'false', 'film', 'active-role processed frame spans'),
            'whole-block-roles': ('port/dsound/dsound.cpp', 'if (b->mediaKind == PORT_SESSION_FILM && i > filmSpan) filmSpan = i;', 'if (b->mediaKind == PORT_SESSION_FILM) filmSpan = frames;', 'short-film', 'active-role processed frame spans'),
            'lost-overlap': ('port/dsound/dsound.cpp', 'add(bothRoleFrames, (unsigned)(musicSpan < filmSpan ? musicSpan : filmSpan));', 'add(bothRoleFrames, 0);', 'overlap', 'active-role processed frame spans'),
            'wrong-queue-zero': ('port/dsound/dsound.cpp', 'gMixer.queueLastBytes = queued < 0 ? -1 : queued;', 'gMixer.queueLastBytes = queued < 0 ? 0 : queued;', 'queue-fail', 'remains unknown'),
            'no-lifetime-epoch': ('port/dsound/dsound.cpp', 'else ++streamGeneration;', 'else (void)0;', 'reopen', 'retire and publish stream epochs'),
            'no-epoch-join': ('port/dsound/dsound.cpp', 'generation != gMixer.streamGeneration ||', 'false ||', 'epoch', 'newer epoch'),
        }
        pcm = {}
        normal_cases = ('music', 'film', 'overlap', 'short-film', 'tail', 'muted', 'decoder-fail', 'track-selection')
        for variant, mutation in variants.items():
            folder = out / variant; source = folder / 'port/dsound/dsound.cpp'; source.parent.mkdir(parents=True)
            header = folder / 'port/platform/common/portlog.h'; header.parent.mkdir(parents=True)
            shutil.copyfile(ROOT / 'port/dsound/dsound.cpp', source); shutil.copyfile(ROOT / 'port/platform/common/portlog.h', header)
            (folder / 'music.inc').write_text(music_code); (folder / 'bink-owner.inc').write_text(bink)
            (folder / 'admission.inc').write_text(session); (folder / 'ce-admission.inc').write_text(admission)
            if variant == 'before':
                run(['patch', '--batch', '--fuzz=0', '-p1', '-i', ROOT / 'port/tools/tests/audio_media_baseline.patch'], 'restore-before', cwd=source.parent)
                # The frozen pre-observation mixer also carries the independently guarded refill publisher.
                assert sha(source) == '7f2f61e8909c2e63c6c13de4522dc501b56da6ae0bbab887bf95b7ab7e752479'
            if mutation:
                name, old, new, _, _ = mutation; p = folder / name; text = p.read_text()
                assert text.count(old) == (2 if variant == 'no-lifetime-epoch' else 1), (variant, old); p.write_text(text.replace(old, new))
            binary = folder / 'driver'
            command = ['clang++', '-std=c++17', '-O1', '-g', '-pthread', '-DNOMINMAX', '-DINITGUID', '-fms-extensions', '-fno-autolink', '-w',
                       '-DCE_VERSION="test"', *(['-DAUDIO_MEDIA_BASELINE'] if variant == 'before' else []),
                       '-I' + str(folder), '-I' + str(ROOT / 'port/d3dx/include'), '-I' + str(ROOT / 'port/include'),
                       '-I' + str(ROOT / 'port/include/linux'), '-I' + str(deps / 'dxvk-native/usr/include/dxvk'),
                       '-I' + str(deps / 'prefix-sniper/include'), '-DMIXER_SOURCE="' + str(source) + '"',
                       str(ROOT / 'port/tools/tests/audio_media_driver.cpp'), '-o', str(binary)]
            run(command, 'build-' + variant)
            cases = normal_cases if variant in ('before', 'current') else (mutation[3],)
            if variant == 'current': cases += ('queue-fail', 'epoch', 'reopen', 'resume-failure', 'invalid-tags', 'reused-buffer', 'saturation')
            for case in cases:
                r = run([binary, case], variant + '-' + case, expected=2 if mutation else 0, reason=mutation[4] if mutation else '')
                if not mutation: pcm[(variant, case)] = json.loads(r.stdout)
            if variant == 'current':
                for label, variables in (('faithful', {'BEA_PORT_CE_LAYER':'0'}), ('clock', {'BEA_PORT_CLOCK':'shim'}),
                                         ('replay', {'BEA_PORT_TAPE':'fixture'}), ('record', {'BEA_PORT_RECORD':'fixture'}),
                                         ('probes', {'BEA_PORT_PROBES':'fixture'})):
                    run([binary, 'disabled'], 'excluded-' + label, variables=variables)
        for case in normal_cases:
            a, b = pcm[('before', case)], pcm[('current', case)]
            same = all(a[k] == b[k] for k in ('pcm_hash', 'frames'))
            receipt['pcm_comparisons'].append({'case':case, 'same':same, 'frames':b['frames'], 'pcm_hash':b['pcm_hash']})
            assert same, case
        receipt['pass'] = True
    except Exception as e:
        receipt['pass'] = False; receipt['failure'] = str(e)
        raise
    finally:
        (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(out / 'result.json', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
