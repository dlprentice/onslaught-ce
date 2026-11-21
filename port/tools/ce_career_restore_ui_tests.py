#!/usr/bin/env python3
"""Restore as New: the page's controller seam on disposable folders.

Builds the real preview controller, the real restore controller, the real Linux reader and the real Linux storage backend into a host
driver (port/tools/tests/ce_career_restore_ui_driver.cpp) that calls the same two plain boundaries the game page calls, then checks every
folder itself, independently of what the driver reports.

What stands in for the game, stated plainly:
- SDL: a shim header written below and three functions in the driver. No window, dialog or renderer exists.
- Present: a counter the driver advances. This shows the controller's rule (Working drawn, then a Present, then one call); it is not
  evidence that the game renders or presents anything.
- Permission and filename compatibility: outcomes each case chooses. They are not the game's gates and not the game's filename
  semantics; the game-side adapter is exercised separately.
- Windows: the "unsupported" case is a Linux host build with the Windows storage source and wording. Nothing ran on Windows.
The game page itself (port/game/ce_backup_preview.cpp) needs the game's headers and is not compiled here.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PLATFORM = ROOT / 'port/platform'
TESTS = ROOT / 'port/tools/tests'
SET = 'onslaught-backup-20260405-123000'
LOCK = 'user/.ce-career-custody.lock'      # the storage backend's own lock file: the only file it may leave in the profile
KILLS = 0x23F6                             # ce_career_view.h kKillsOffset: a byte that keeps a career readable

SHIM = '''// Test shim only: the declarations the preview controller uses. Not SDL.
#pragma once
#define SDLCALL
extern "C" {
struct SDL_Window;
typedef void (*SDL_DialogFileCallback)(void *userdata, const char *const *filelist, int filter);
const char *SDL_GetBasePath(void);
SDL_Window *SDL_GetKeyboardFocus(void);
void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
                              const char *default_location, bool allow_many);
}
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files(base):
    """Every regular file under base by relative name, with its hash and inode; the backend's lock file aside."""
    out = {}
    for path in sorted(base.rglob('*')):
        rel = path.relative_to(base).as_posix()
        if path.is_file() and not path.is_symlink() and rel != LOCK:
            out[rel] = (sha(path.read_bytes()), path.stat().st_ino)
    return out


def build(job):
    shim = job / 'shim/SDL3'
    shim.mkdir(parents=True)
    (shim / 'SDL.h').write_text(SHIM)
    fixture = job / 'fixture'
    subprocess.run(['c++', '-std=c++17', '-O1', str(PLATFORM / 'linux/ce_backup_preview.cpp'),
                    str(TESTS / 'ce_backup_preview_driver.cpp'), '-o', str(fixture)], check=True, timeout=180)
    common = ['c++', '-std=c++17', '-Wall', '-Wextra', '-O1', '-pthread', '-I', str(job / 'shim'),
              str(PLATFORM / 'common/ce_career_restore_ui.cpp'), str(PLATFORM / 'common/ce_backup_preview.cpp'),
              str(PLATFORM / 'linux/ce_backup_preview.cpp'), str(TESTS / 'ce_career_restore_ui_driver.cpp'),
              '-Wl,--wrap=flock,--wrap=linkat,--wrap=fsync,--wrap=_Z20CePreviewRestoreCardR13CeRestoreCard']
    linux, windows = job / 'driver-linux', job / 'driver-windows-source'
    for exe, extra in ((linux, [str(PLATFORM / 'linux/ce_career_restore.cpp')]),
                       (windows, [str(PLATFORM / 'windows/ce_career_restore.cpp'), '-DCE_RESTORE_UI_TEST_WINDOWS_COPY=1'])):
        with (job / (exe.name + '-build.private.log')).open('wb') as log:
            subprocess.run(common + extra + ['-o', str(exe)], check=True, stdout=log, stderr=log, timeout=300)
    return fixture, linux, windows


def profile(job, name, careers):
    """A disposable profile: one current career and options, an empty game folder, and one backup set in the backup folder."""
    base = job / name
    for sub in ('user/savegames', 'game/savegames', 'package', 'external/' + SET):
        (base / sub).mkdir(parents=True)
    (base / 'AUTHORED').write_text('disposable restore ui fixture\n')
    (base / 'user/savegames/Current.bes').write_bytes(CURRENT)
    (base / 'user/defaultoptions.bea').write_bytes(b'authored current options')
    (base / 'user/ce.ini').write_bytes(b'authored configuration\n')
    listed = []
    for leaf, content in careers:
        (base / 'external' / SET / leaf).write_bytes(content)
        listed.append(dict(Name=leaf, Original='/recorded/elsewhere/' + leaf, Size=len(content), Sha256=sha(content)))
    manifest = dict(Folder='/recorded/folder', Created='2026-04-05T12:30:00Z', Game='/recorded/game', Files=listed,
                    Reason='Manual', Target=1)
    (base / 'external' / SET / 'onslaught-backup.json').write_text(json.dumps(manifest, ensure_ascii=True))
    return base


def run(exe, base, case):
    env = dict(os.environ)
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
        env.pop(key, None)
    done = subprocess.run([str(exe), str(base), case], capture_output=True, text=True, timeout=180, env=env)
    lines = done.stdout.splitlines()
    failed = [line for line in lines if line.startswith('FAIL ')]
    assert done.returncode == 0 and not failed, (case, done.returncode, failed)
    assert lines and lines[-1] == 'RESULT pass', (case, 'no result line')
    return dict(line.split('=', 1) for line in lines if '=' in line and not line.startswith('FAIL '))


def new_sets(base, before):
    """The backup sets made during the case: whole folders that were not there before."""
    made = sorted(p.name for p in (base / 'external').iterdir() if p.name != SET)
    assert not [n for n in made if n.startswith('.')], 'an unfinished backup folder was left behind'
    assert all(not rel.startswith('external/') or rel.startswith('external/' + SET + '/') for rel in before)
    return made


def main():
    global CURRENT
    parent = ROOT / 'local-data/port/ce-career-restore-ui-tests'
    parent.mkdir(parents=True, exist_ok=True)
    job = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    fixture, linux, windows = build(job)
    authored = job / 'authored.bes'
    subprocess.run([str(fixture), 'fixture', str(authored)], check=True, timeout=60)
    amber = authored.read_bytes()

    def variant(value):
        data = bytearray(amber)
        data[KILLS] = value
        return bytes(data)

    CURRENT, second = variant(3), variant(7)
    assert len({sha(amber), sha(CURRENT), sha(second)}) == 3
    report = []

    def case(name, exe=linux, careers=(('Amber.bes', amber),)):
        base = profile(job, name, careers)
        before = files(base)
        out = run(exe, base, name)
        after = files(base)
        report.append(name)
        return base, before, after, out

    # Nothing may change: not opened at all, read-only browsing, cancel and Back, every refusal, a stale worker.
    # The card re-bound between Confirm and the storage call (stamp only, verified again, picked again): CHANGED, no call.
    for name in ('ordinary', 'browse', 'cancel', 'changed', 'reverified', 'stamp_at_confirm', 'stamp_at_pump',
                 'reverify_after_confirm', 'repick_after_confirm', 'name_refused', 'name_revoked', 'authority',
                 'authority_late', 'authority_inside', 'wrong_thread', 'stale_worker'):
        base, before, after, out = case(name)
        if name == 'changed':
            # the case itself rewrote one backup file with the same bytes: same content, nothing else touched
            assert {k: v[0] for k, v in before.items()} == {k: v[0] for k, v in after.items()}, name
        else:
            assert before == after, name
        assert not new_sets(base, before), name
        if name not in ('changed',):
            assert out['locks'] == '0', (name, out['locks'])
    assert not (job / 'ordinary' / LOCK).exists() and not (job / 'cancel' / LOCK).exists()

    base, before, after, out = case('unsupported', exe=windows)
    assert before == after and not new_sets(base, before) and out['locks'] == '0'

    # One confirmed restore: one new career with the chosen backup career's bytes, one whole backup set, nothing else touched.
    for name, careers, target, payload in (
            ('restore', (('Amber.bes', amber),), 'Amber restored.bes', amber),
            ('multi', (('Amber.bes', amber), ('Second.bes', second)), 'Second restored.bes', second)):
        base, before, after, out = case(name, careers=careers)
        assert out['target'] == target and out['locks'] == '1', name
        made = new_sets(base, before)
        assert len(made) == 1 and made[0].startswith('onslaught-backup-'), (name, len(made))
        added = {k: v for k, v in after.items() if k not in before}
        assert {k: v for k, v in after.items() if k in before} == before, name
        assert added.pop('user/savegames/' + target)[0] == sha(payload), name
        inside = 'external/' + made[0] + '/'
        assert sorted(added) == sorted(inside + leaf for leaf in ('Current.bes', 'defaultoptions.bea', 'onslaught-backup.json')), name
        assert added[inside + 'Current.bes'][0] == sha(CURRENT), name
        if name == 'restore':
            assert out['backup'] == str(base / 'external' / made[0]) and out['saves'] == str(base / 'user/savegames')

    # The confirmed name was taken first: the occupying file is untouched and no other name was written.
    base, before, after, out = case('duplicate')
    added = {k: v for k, v in after.items() if k not in before}
    assert {k: v for k, v in after.items() if k in before} == before
    assert list(added) == ['user/savegames/Amber restored.bes'] and added['user/savegames/Amber restored.bes'][0] == sha(CURRENT)
    assert not new_sets(base, before) and out['locks'] == '1' and out['second'] == 'Amber restored 2.bes'

    # The saves folder refuses new files after the backup step: one backup set made, no career, nothing else touched.
    base, before, after, out = case('readonly_saves')
    made = new_sets(base, before)
    assert len(made) == 1 and out['locks'] == '1', (len(made), out['locks'])
    assert {k: v for k, v in after.items() if k in before} == before
    assert not any(k.startswith('user/savegames/') for k in after if k not in before)
    assert out['backup'] == str(base / 'external' / made[0])

    # Uncertain after publication: the possible new career and the backup set are both still there, and nothing was retried.
    base, before, after, out = case('uncertain')
    made = new_sets(base, before)
    assert len(made) == 1 and out['locks'] == '1' and out['target'] == 'Amber restored.bes'
    assert {k: v for k, v in after.items() if k in before} == before
    assert after['user/savegames/Amber restored.bes'][0] == sha(amber)
    assert out['backup'] == str(base / 'external' / made[0]) and (base / 'external' / made[0] / 'onslaught-backup.json').is_file()

    (job / 'result.json').write_text(json.dumps(dict(cases=report, host_seam_only=True, game_page_compiled=False,
                                                     rendered=False, windows_executed=False,
                                                     compatibility='explicit test outcomes, not the game adapter'), indent=2) + '\n')
    print(f'{len(report)} Restore as New controller cases passed on a host seam build '
          f'(no game, no rendering, no Windows run). {job.name}')


CURRENT = b''

if __name__ == '__main__':
    os.umask(0o077)
    main()
