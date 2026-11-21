"""Build and exercise production restore storage only on generated profiles."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent


def main():
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(ROOT / 'port/tools'))
    import save_flow_tests
    import deps
    root = ROOT / 'local-data/career-restore-storage'
    root.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='build-', dir=root))
    linux = ROOT / 'port/platform/linux'
    test = ROOT / 'port/tools/tests'
    bins = {k: output / k for k in ('fixture', 'preview', 'storage', 'mutant')}
    # The control that must fail: the same store with its protection removed
    # (the staging folder takes the listed name at once).
    source = (linux / 'ce_career_restore.cpp').read_text()
    protection = 'staging = ".pending-" + final;'
    assert source.count(protection) == 1, 'mutant target not found'
    (output / 'mutant-src').mkdir()
    mutant = output / 'mutant-src/ce_career_restore.cpp'
    mutant.write_text(source.replace(protection, 'staging = final;'))
    commands = {
        'fixture': [linux / 'ce_backup_preview.cpp', test / 'ce_backup_preview_driver.cpp'],
        'preview': [linux / 'ce_backup_preview.cpp', test / 'restore_custody/preview.cpp'],
        'storage': [linux / 'ce_career_restore.cpp', linux / 'ce_backup_preview.cpp', HERE / 'driver.cpp',
                    '-Wl,--wrap=openat,--wrap=write,--wrap=pread,--wrap=fsync,--wrap=linkat,--wrap=getrandom,--wrap=renameat2,--wrap=mkdirat'],
        'mutant': ['-I', linux, mutant, linux / 'ce_backup_preview.cpp', HERE / 'driver.cpp',
                   '-Wl,--wrap=openat,--wrap=write,--wrap=pread,--wrap=fsync,--wrap=linkat,--wrap=getrandom,--wrap=renameat2,--wrap=mkdirat'],
    }
    for key, objects in commands.items():
        with (output / (key + '-build.private.log')).open('xb') as log:
            subprocess.run(['c++', *(['-m32'] if key in ('storage', 'mutant') else []), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', *objects,
                            '-o', bins[key]], cwd=ROOT, check=True, stdout=log, stderr=log, timeout=90)
    bins['card'] = save_flow_tests.Build(output, True).binary
    exe = output / 'windows-failclosed.exe'
    cc = deps.DATA / 'llvm-mingw/bin/i686-w64-mingw32-clang++'
    with (output / 'windows-build.private.log').open('xb') as log:
        subprocess.run([cc, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-static',
                        ROOT / 'port/platform/windows/ce_career_restore.cpp',
                        ROOT / 'port/platform/windows/ce_backup_preview.cpp',
                        HERE / 'windows_closed.cpp',
                        '-o', exe], cwd=ROOT, check=True, stdout=log, stderr=log, timeout=90)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
        env.pop(key, None)
    args = [sys.executable, '-B', HERE / 'exercise.py']
    for key, value in bins.items():
        args.extend(['--' + key, value])
    with (output / 'test.private.log').open('xb') as log:
        subprocess.run(args, cwd=ROOT, env=env, stdout=log, stderr=log, timeout=180, check=True)
    test_result = (output / 'test.private.log').read_text().splitlines()[-1].split()[0]
    receipt = dict(source=subprocess.check_output(['git', '-C', ROOT, 'rev-parse', 'HEAD'], text=True).strip(),
                   binaries={k: hashlib.sha256(Path(v).read_bytes()).hexdigest() for k, v in bins.items()},
                   binary_paths={k: str(Path(v).relative_to(ROOT)) for k,v in bins.items()},
                   windows_failclosed_pe32=hashlib.sha256(exe.read_bytes()).hexdigest(),
                   windows_executed=False, backend_elf_class=32 if bins['storage'].read_bytes()[4]==1 else 64, test_result=test_result,
                   test_result_sha256=hashlib.sha256((ROOT / test_result).read_bytes()).hexdigest())
    p = output / 'build.private.json'
    p.write_text(json.dumps(receipt, indent=2) + '\n')
    print(p.relative_to(ROOT))


if __name__ == '__main__':
    os.umask(0o077)
    main()
