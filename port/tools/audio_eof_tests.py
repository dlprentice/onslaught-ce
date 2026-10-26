#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual Ogg reader and music worker EOF/error controls, without a game, device or scheduler.

Compile the complete production reader, pinned decoder, CRT stream table and exact MusicStreamHandle body.
Pathname-open, buffer, event and logging endpoints are controlled. An authored Ogg and malformed chained header distinguish normal completion
from a decoder failure. Real owned-descriptor read and close failures must retain failed drain reads. --asset optionally repeats the offline contrast with a local private input; all outputs
remain ignored. This establishes reader status and drain behavior, not Windows/device/audio-quality acceptance. The existing
decoder tolerance for truncated or missing Ogg data is unchanged.
"""
import argparse
import filecmp
import hashlib
import json
import math
import os
from pathlib import Path
import re
import resource
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATCH = '0078-ce-ogg-eof-status.patch'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 0
    for end in range(opening, len(text)):
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
            if depth == 0:
                return text[start:end + 1] + '\n'
    raise ValueError(signature)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--asset', type=Path)
    args = ap.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/port/audio-eof-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    common = Path(subprocess.check_output(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT, text=True).strip()).parent
    data = common / 'local-data/port'
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_', 'SDL_AUDIO', 'LD_PRELOAD'))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1',
               ASAN_OPTIONS='detect_leaks=1:abort_on_error=0:exitcode=86:disable_coredump=1')
    receipt = {'scope': __doc__, 'commands': [], 'cases': [], 'pins': {'runner': sha(Path(__file__)),
               'driver': sha(ROOT / 'port/tools/tests/audio_eof_driver.cpp')}, 'comparisons': []}

    def run(command, label, expected=0, timeout=30):
        receipt['commands'].append(list(map(str, command)))
        p = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=timeout, env=env)
        (out / (label + '.log')).write_text(p.stdout + p.stderr)
        receipt['cases'].append({'case': label, 'exit': p.returncode, 'expected': expected,
                                 'passed': p.returncode == expected, 'stdout': p.stdout, 'stderr': p.stderr})
        if p.returncode != expected:
            raise RuntimeError(label + ': ' + p.stderr[-2000:])
        return p

    try:
        source = out / 'source'
        source.mkdir()
        for name in ('OggFileRead.cpp', 'OggFileRead.h', 'oggvorbis.h', 'PCMusicStream.cpp'):
            shutil.copyfile(ROOT / 'src' / name, source / name)
            receipt['pins']['src/' + name] = sha(source / name)
        for patch in sorted((ROOT / 'port/patches').glob('*.patch')):
            parts = [s for s in re.split(r'(?=^--- a/)', patch.read_text(), flags=re.M)
                     if s.startswith('--- a/PCMusicStream.cpp\n')]
            if parts:
                receipt['pins']['patch/' + patch.name] = sha(patch)
                p = subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-d', str(source)],
                                   input=''.join(parts), capture_output=True, text=True, timeout=10)
                if p.returncode:
                    raise RuntimeError(p.stdout + p.stderr)
        worker = function((source / 'PCMusicStream.cpp').read_text(), 'static int\tMusicStreamHandle(DWORD result)\n{')
        crt = ROOT / 'port/platform/common/crtfile.cpp'
        (source / 'crtfile.inc').write_text(re.sub(r'^#include[^\n]*\n', '', crt.read_text(), flags=re.M))
        receipt['pins']['crtfile'] = sha(crt)
        baseline = (source / 'OggFileRead.cpp').read_text()
        run(['patch', '--batch', '--fuzz=0', '-p1', '-d', source, '-i', ROOT / 'port/patches' / PATCH], 'patch-apply')
        receipt['pins']['patch/' + PATCH] = sha(ROOT / 'port/patches' / PATCH)
        fixed = (source / 'OggFileRead.cpp').read_text()
        assert baseline != fixed
        raw = out / 'authored.s16'
        with raw.open('wb') as f:
            for i in range(44100 * 14):
                t = i / 44100
                f.write(struct.pack('<hh', int(10000 * math.sin(2 * math.pi * (170 * t + 31 * t * t))),
                                    int(10000 * math.sin(2 * math.pi * (271 * t + 19 * t * t)))))
        run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', '44100', '-ac', '2', '-i', raw,
             '-c:a', 'libvorbis', '-q:a', '5', '-threads', '1', out / 'authored.ogg'], 'author-ogg')
        (out / 'bad-header.ogg').write_bytes(b'X' * 8192)
        (out / 'empty.ogg').write_bytes(b'')
        (out / 'bad-chain.ogg').write_bytes((out / 'authored.ogg').read_bytes() + b'X' * 8192)
        cc = data / 'llvm/bin/clang'
        cxx = data / 'llvm/bin/clang++'
        flags = ['--target=i686-pc-linux-gnu', '--sysroot=' + str(data / 'sysroot-sniper'), '-O1', '-g',
                 '-fms-extensions', '-malign-double', '-mms-bitfields', '-mno-sse', '-mfpmath=387']
        shutil.copyfile(ROOT / 'port/vorbis/vc6crt.c', out / 'vc6crt.c')
        receipt['pins']['decoder_library'] = sha(data / 'vorbis/lib/linux/libvorbis_vc6.a')
        run([cc, *flags, '-c', out / 'vc6crt.c', '-o', out / 'vc6crt.o'], 'shim-compile')
        variants = {'baseline': baseline, 'fixed': fixed, 'private': fixed,
                    'missing-reset': fixed.replace('\tmState = 0; // Close/Open must not retain', '\t/* missing reset */ // Close/Open must not retain'),
                    'read-error-ignored': fixed.replace('mState = portReadError || portCloseResult ? 0 : 3;', 'mState = portCloseResult ? 0 : 3;'),
                    'close-error-ignored': fixed.replace('mState = portReadError || portCloseResult ? 0 : 3;', 'mState = portReadError ? 0 : 3;'),
                    'decoder-error-muted': fixed.replace('*size_read = 0;\n\t\treturn E_FAIL;\n\t}\n\treturn S_OK;', '*size_read = 0;\n\t\treturn S_FALSE;\n\t}\n\treturn S_OK;')}
        assert all(variants[n] != fixed for n in ('missing-reset', 'decoder-error-muted', 'read-error-ignored', 'close-error-ignored'))
        libs = ':'.join(str(data / p) for p in ('sysroot-sniper/lib/i386-linux-gnu',
                         'sysroot-sniper/usr/lib/i386-linux-gnu', 'sysroot-sniper/usr/lib/gcc/i686-linux-gnu/10'))
        loader = [data / 'sysroot-sniper/lib/ld-linux.so.2', '--library-path', libs,
                  '--preload', data / 'sysroot-sniper/usr/lib/i386-linux-gnu/libasan.so.6']
        observations = {}
        for label, text in variants.items():
            folder = out / label
            folder.mkdir()
            for name in ('OggFileRead.h', 'oggvorbis.h'):
                target = 'reader.h' if name.endswith('.h') and name.startswith('Ogg') else name
                (folder / target).write_text(re.sub(r'^#include[^\n]*\n', '', (source / name).read_text(), flags=re.M))
            (folder / 'reader.inc').write_text(re.sub(r'^#include[^\n]*\n', '', text, flags=re.M))
            (folder / 'worker.inc').write_text(worker)
            shutil.copyfile(source / 'crtfile.inc', folder / 'crtfile.inc')
            receipt['pins']['reader/' + label] = sha(folder / 'reader.inc')
            cmd = [cxx, *flags, '-fsanitize=address', '-fno-sanitize-link-runtime', '-std=c++17', '-pthread', '-DCE_VERSION=1',
                   *(['-DFIXTURE_PRIVATE_READER=1'] if label == 'private' else []), '-I' + str(folder),
                   ROOT / 'port/tools/tests/audio_eof_driver.cpp', out / 'vc6crt.o', data / 'vorbis/lib/linux/libvorbis_vc6.a',
                   '-lm', '-no-pie', '--ld-path=' + str(data / 'llvm-mingw/bin/ld.lld'),
                   '-L' + str(data / 'sysroot-sniper/usr/lib/i386-linux-gnu'),
                   '-Wl,-rpath,' + str(data / 'sysroot-sniper/usr/lib/i386-linux-gnu'), '-l:libasan.so.6', '-o', folder / 'driver']
            run(cmd, label + '-compile')
            trials = [('clean', out / 'authored.ogg'), ('bad', out / 'bad-chain.ogg'), ('read-io', out / 'authored.ogg'), ('close-io', out / 'authored.ogg')]
            if label == 'missing-reset':
                trials = trials[:1]
            if label == 'decoder-error-muted':
                trials = trials[1:2]
            if label == 'read-error-ignored':
                trials = [('read-io', out / 'authored.ogg')]
            if label == 'close-error-ignored':
                trials = [('close-io', out / 'authored.ogg')]
            if args.asset and label in ('baseline', 'fixed'):
                trials.append(('asset', args.asset.resolve()))
            for mode, asset in trials:
                negative = label in ('missing-reset', 'decoder-error-muted', 'read-error-ignored', 'close-error-ignored')
                pcm = folder / (mode + '.pcm')
                p = run([*loader, folder / 'driver', 'baseline' if label in ('baseline', 'private') else 'fixed',
                         mode if mode in ('bad', 'read-io', 'close-io') else 'clean', asset, pcm, out / 'bad-header.ogg', out / 'empty.ogg'], label + '-' + mode, 2 if negative else 0, 20)
                receipt['pins']['input/' + mode] = sha(asset)
                if negative:
                    assert 'FAIL' in p.stderr
                else:
                    rows = [json.loads(s) for s in p.stdout.splitlines()]
                    assert rows[0]['checks'] > 0
                    observations[(label, mode)] = (rows, pcm)
            if label == 'baseline':
                run([*loader, folder / 'driver', 'worker-fixed', 'clean', out / 'authored.ogg', folder / 'old-as-fixed.pcm', out / 'bad-header.ogg', out / 'empty.ogg'],
                    'old-reader-under-fixed-contract', 2)
        for mode in ('clean', 'bad', 'read-io', 'close-io', *(['asset'] if args.asset else [])):
            before, a = observations[('baseline', mode)]
            after, b = observations[('fixed', mode)]
            assert before[0]['reader_size'] == after[0]['reader_size'] == 0x22f0
            assert filecmp.cmp(a, b, shallow=False), 'PCM changed: ' + mode
            assert [[r[k] for k in ('requested', 'got', 'format_before', 'format_after')] for r in before[0]['reads']] == [[r[k] for k in ('requested', 'got', 'format_before', 'format_after')] for r in after[0]['reads']], 'read/drain sequence changed: ' + mode
            for key in ('refills', 'short_reads', 'eof_drained'):
                assert before[0][key] == after[0][key], key
            assert before[0]['read_failed'] == (1 if mode == 'bad' else 2)
            assert after[0]['read_failed'] == (1 if mode == 'bad' else 2 if mode in ('read-io', 'close-io') else 0)
            assert before[0]['file_error'] == after[0]['file_error']
            assert before[0]['close_result'] == after[0]['close_result']
            receipt['comparisons'].append({'case': mode, 'pcm_equal': True, 'pcm_sha256': sha(a),
                                          'before': before, 'after': after})
        receipt['pass'] = True
    except Exception as exc:
        receipt['pass'] = False
        receipt['error'] = str(exc)
    (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'pass': receipt['pass'], 'receipt': str(out / 'result.json'), 'error': receipt.get('error')}))
    return 0 if receipt['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
