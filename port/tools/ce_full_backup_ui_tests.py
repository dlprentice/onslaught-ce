#!/usr/bin/env python3
"""Back Up All Careers: the page's controller seam on disposable folders.

Builds the real controller (port/platform/common/ce_full_backup_ui.cpp), the real Linux storage backend and the real Linux preview
reader into a host driver (port/tools/tests/ce_full_backup_ui_driver.cpp) that calls the plain boundary the game page calls, then checks
every profile itself: careers and options byte-equal and on the same inode, and only the expected set in the backup folder.

What stands in for the game, stated plainly:
- SDL: a shim header written below and SDL_GetBasePath in the driver. No window, dialog or renderer exists.
- Present: a counter the driver advances. This shows the controller's rule (Working drawn, then a Present, then one call); it is not
  evidence that the game renders or presents anything.
- Permission: an outcome each case chooses, not the game's gate.
The game page itself (port/game/ce_backup_all_page.cpp) needs the game's headers and is not compiled here.
A control that must fail: the same controller with its Present gate removed must fail the "complete" case.
"""
from pathlib import Path
import hashlib
import json
import resource
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PLATFORM = ROOT / 'port/platform'
TESTS = ROOT / 'port/tools/tests'
LOCK = 'user/.ce-career-custody.lock'
KILLS = 0x23F6  # ce_career_view.h kKillsOffset: a byte that keeps a career readable
SHIM = '''// Test shim only: the declaration the controller uses. Not SDL.
#pragma once
extern "C" const char *SDL_GetBasePath(void);
'''
GATE = 'unsigned(presentCount - s.drawnAt) == 0u'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files(base):
    out = {}
    for path in sorted(base.rglob('*')):
        rel = path.relative_to(base).as_posix()
        if path.is_file() and not path.is_symlink() and rel != LOCK and not rel.startswith('external/'):
            out[rel] = (sha(path.read_bytes()), path.stat().st_ino)
    return out


def build(job, controller, name):
    common = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-pthread', '-I', str(job / 'shim'),
              '-I', str(PLATFORM / 'common'), str(controller), str(PLATFORM / 'linux/ce_career_restore.cpp'),
              str(PLATFORM / 'linux/ce_backup_preview.cpp'), str(TESTS / 'ce_full_backup_ui_driver.cpp'), '-o', str(job / name)]
    with (job / (name + '-build.private.log')).open('wb') as log:
        subprocess.run(common, check=True, stdout=log, stderr=log, timeout=300)
    return job / name


def profile(job, name, fixture):
    base = job / name
    for sub in ('user/savegames', 'game/savegames', 'package', 'external'):
        (base / sub).mkdir(parents=True)
    career = job / 'career.bes'
    data = career.read_bytes()
    (base / 'user/savegames/Amber.bes').write_bytes(data)
    other = bytearray(data)
    other[KILLS] ^= 1
    (base / 'user/savegames/Birch.bes').write_bytes(bytes(other))
    (base / 'user/defaultoptions.bea').write_bytes(b'disposable options')
    return base


CASES = {
    'offer': 0, 'complete': 1, 'authority_at_start': 0, 'authority_before_call': 0, 'authority_inside_call': 0,
    'no_folder': 0, 'no_careers': 0, 'other_backup': 0, 'custody_lock_held': 0, 'other_thread': 0,
    'leave_before_call': 0,
}


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/port/ce-full-backup-ui-tests'
    parent.mkdir(parents=True, exist_ok=True)
    job = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    (job / 'shim/SDL3').mkdir(parents=True)
    (job / 'shim/SDL3/SDL.h').write_text(SHIM)
    fixture = job / 'fixture'
    subprocess.run(['c++', '-std=c++17', '-O1', str(PLATFORM / 'linux/ce_backup_preview.cpp'),
                    str(TESTS / 'ce_backup_preview_driver.cpp'), '-o', str(fixture)], check=True, timeout=180)
    subprocess.run([str(fixture), 'fixture', str(job / 'career.bes')], check=True, timeout=10)
    driver = build(job, PLATFORM / 'common/ce_full_backup_ui.cpp', 'driver')
    source = (PLATFORM / 'common/ce_full_backup_ui.cpp').read_text()
    if source.count(GATE) != 1:
        raise RuntimeError('control target not found')
    (job / 'mutant-src').mkdir()
    (job / 'mutant-src/ce_full_backup_ui.cpp').write_text(source.replace(GATE, '(presentCount & 0u) != 0u'))
    mutant = build(job, job / 'mutant-src/ce_full_backup_ui.cpp', 'mutant')

    results, failures = {}, []
    for case, sets in CASES.items():
        base = profile(job, case, fixture)
        before = files(base)
        run = subprocess.run([str(driver), str(base), case], capture_output=True, text=True, timeout=60)
        made = sorted(p.name for p in (base / 'external').iterdir())
        ok = (run.returncode == 0 and files(base) == before and len(made) == sets
              and all(m.startswith('onslaught-backup-') for m in made))
        results[case] = dict(ok=ok, driver_exit=run.returncode, sets_made=len(made),
                             saves_unchanged=files(base) == before, output=run.stdout.strip().splitlines()[-1:])
        if not ok:
            failures.append(case)
            print(run.stdout + run.stderr, file=sys.stderr)
    base = profile(job, 'control-no-present-gate', fixture)
    control = subprocess.run([str(mutant), str(base), 'complete'], capture_output=True, text=True, timeout=60)
    control_failed = control.returncode != 0
    receipt = dict(ok=not failures and control_failed, cases=results, failures=failures, skip_count=0,
                   must_fail_control=dict(name='present_gate_removed_complete', failed_as_required=control_failed,
                                          output=control.stdout.strip().splitlines()),
                   scope='host seam build: real controller, Linux storage and reader; no game, rendering or Windows run',
                   sources={str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in
                            [PLATFORM / 'common/ce_full_backup_ui.cpp', TESTS / 'ce_full_backup_ui_driver.cpp',
                             PLATFORM / 'linux/ce_career_restore.cpp']})
    (job / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if not control_failed:
        print('control passed: the cases cannot detect a call made before a Present (tool failure)')
    print(f"{len(results) - len(failures)} of {len(results)} Back Up All Careers controller cases passed; "
          f"control {'failed as required' if control_failed else 'PASSED (tool failure)'}. {job.name}")
    return 0 if receipt['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
