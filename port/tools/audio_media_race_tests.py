#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise media snapshot versus stream teardown with ThreadSanitizer and controlled SDL boundaries.

The production mixer is compiled unchanged; the SDL destroy/mutex boundary schedules the snapshot while teardown
proceeds. A mutant restores the unlocked stream-pointer read and must produce the intended race report. No game,
device, retail data or Windows runtime is used.
"""
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/port/audio-media-race-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    common = Path(subprocess.check_output(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                         cwd=ROOT, text=True).strip()).parent
    deps = common / 'local-data/port'
    env = {k: v for k, v in os.environ.items() if not k.startswith(('BEA_', 'SDL_AUDIO', 'LD_PRELOAD'))}
    env.update(TMPDIR=str(out), OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1',
               TSAN_OPTIONS='halt_on_error=1:exitcode=66')
    source = ROOT / 'port/dsound/dsound.cpp'
    header = ROOT / 'port/platform/common/portlog.h'
    driver = ROOT / 'port/tools/tests/audio_lifecycle_driver.cpp'
    receipt = {'scope': __doc__, 'pins': {str(p.relative_to(ROOT)): sha(p) for p in (source, header, driver)},
               'cases': [], 'commands': []}
    try:
        for label in ('fixed', 'unlocked-read'):
            folder = out / label
            mixer = folder / 'port/dsound/dsound.cpp'
            mixer.parent.mkdir(parents=True)
            local_header = folder / 'port/platform/common/portlog.h'
            local_header.parent.mkdir(parents=True)
            shutil.copyfile(header, local_header)
            text = source.read_text()
            if label == 'unlocked-read':
                old = 'currentGeneration = gMixer.mediaStreamActive &&'
                if text.count(old) != 1:
                    raise RuntimeError('unlocked-read mutation is not unique')
                text = text.replace(old, 'currentGeneration = gMixer.stream &&', 1)
            mixer.write_text(text)
            binary = folder / 'driver'
            command = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=thread', '-fno-omit-frame-pointer',
                       '-pthread', '-DNOMINMAX', '-DINITGUID', '-DAUDIO_MEDIA_CONCURRENCY', '-fms-extensions',
                       '-fno-autolink', '-w', '-I' + str(ROOT / 'port/d3dx/include'), '-I' + str(ROOT / 'port/include'),
                       '-I' + str(ROOT / 'port/include/linux'), '-I' + str(deps / 'dxvk-native/usr/include/dxvk'),
                       '-I' + str(deps / 'prefix-sniper/include'), '-DMIXER_SOURCE="' + str(mixer) + '"',
                       str(driver), '-o', str(binary)]
            receipt['commands'].append(command)
            build = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
            (folder / 'build.log').write_text(build.stdout + build.stderr)
            if build.returncode:
                raise RuntimeError(label + ' compile failed: ' + build.stderr[-2000:])
            run = subprocess.run([str(binary), 'media-stop-snapshot', 'fixed'], cwd=ROOT, env=env,
                                 capture_output=True, text=True, timeout=15)
            (folder / 'run.log').write_text(run.stdout + run.stderr)
            if label == 'fixed':
                passed = run.returncode == 0 and 'ThreadSanitizer:' not in run.stderr
            else:
                passed = (run.returncode == 66 and 'ThreadSanitizer: data race' in run.stderr and
                          'MediaSnapshot' in run.stderr and 'Mixer::Stop' in run.stderr)
            receipt['cases'].append({'variant': label, 'exit': run.returncode, 'passed': passed,
                                     'source_sha256': sha(mixer), 'stdout': run.stdout, 'stderr': run.stderr})
            if not passed:
                raise RuntimeError(label + ' failed: ' + run.stderr[-2000:])
        receipt['pass'] = True
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
