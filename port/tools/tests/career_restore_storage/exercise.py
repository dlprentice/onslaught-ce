"""Fresh authored profiles; calls the linked production storage implementation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[4]
H = lambda b: hashlib.sha256(b).hexdigest()
RESULTS = []
TIMINGS = []
COMPLETE, REFUSED, BUSY, CANCELLED, CHANGED, OCCUPIED, UNCERTAIN, UNSUPPORTED = range(8)


def check(name, value):
    if not value:
        raise AssertionError(name)
    RESULTS.append(name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('fixture', 'preview', 'card', 'storage', 'mutant'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    tools = {k: v.resolve(strict=True) for k, v in vars(a).items()}
    home = ROOT / 'local-data/career-restore-storage'
    work = Path(tempfile.mkdtemp(prefix='profiles-', dir=home))
    generated = work / 'authored.bes'
    subprocess.run([tools['fixture'], 'fixture', generated], check=True, timeout=10, capture_output=True)
    present = generated.read_bytes()
    def variant(option):
        return subprocess.run([tools['preview'], option], input=present, capture_output=True,
                              check=True, timeout=10).stdout
    alternate, previous = variant('--authored-flag'), variant('--authored-progress')
    assert len({H(present), H(alternate), H(previous)}) == 3

    def setup():
        d = Path(tempfile.mkdtemp(prefix='authored-', dir=work))
        for n in ('user/savegames', 'game/savegames', 'package', 'external',
                  'source/onslaught-backup-20260405-123000'):
            (d / n).mkdir(parents=True)
        (d / 'AUTHORED').write_text('disposable storage fixture\n')
        (d / 'user/savegames/Amber.bes').write_bytes(present)
        (d / 'user/savegames/Birch.bes').write_bytes(alternate)
        (d / 'game/savegames/Protected.bes').write_bytes(alternate)
        (d / 'user/defaultoptions.bea').write_bytes(b'disposable options')
        src = d / 'source/onslaught-backup-20260405-123000'
        (src / 'Amber.bes').write_bytes(previous)
        (src / 'onslaught-backup.json').write_text(json.dumps(dict(
            Folder='/authored-only', Game='/authored-only', Created='2026-04-05T12:30:00Z',
            Reason='Manual', Target=1, Files=[dict(Name='Amber.bes', Original='/authored-only/Amber.bes',
                                                   Size=len(previous), Sha256=H(previous))])))
        return d

    def protected(d):
        return (all((d / n).read_bytes() == b for n, b in {
            'user/savegames/Amber.bes': present, 'user/savegames/Birch.bes': alternate,
            'game/savegames/Protected.bes': alternate, 'user/defaultoptions.bea': b'disposable options',
            'source/onslaught-backup-20260405-123000/Amber.bes': previous}.items()))

    def env(d):
        value = dict(os.environ, BEA_USER_DIR=str(d / 'user'), BEA_GAME_DIR=str(d / 'game'),
                     HOME=str(d), BEA_REPORT_NO_OPEN='1', BROWSER='/bin/true')
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
            value.pop(key, None)
        return value

    def parse(r, label="boundary_control"):
        assert r.returncode == 0, 'driver failed before API verdict: %s rc=%s' % (label, r.returncode)
        row = r.stdout.decode().splitlines()
        assert len(row) == 5
        times=[int(x) for x in row[4].split()]
        assert len(times)==3
        TIMINGS.append(dict(label=label,status=int(row[0]),stamp_us=times[0],suggest_us=times[1],operation_us=times[2]))
        return int(row[0]), row[1], row[2], row[3]

    def run(d, mode='normal', op='restore', tool='storage'):
        return parse(subprocess.run([tools[tool], d, mode, op], capture_output=True,
                                    env=env(d), cwd=d, timeout=20),op+"_"+mode)

    def card(d, verb, name='', arg='0'):
        return subprocess.run([tools['card'], 'card', verb, name, arg], capture_output=True,
                              env=env(d), cwd=d, timeout=10)

    def slots(d):
        return {p.name: H(p.read_bytes()) for p in (d / 'user/savegames').iterdir() if p.is_file()}

    def complete(d, out):
        code, backup, identity, name = out
        if code != COMPLETE or not re.fullmatch(r'Amber restored(?: [1-9][0-9]*)?\.bes', name):
            return False
        reopened = card(d, 'read', name[:-4], str(len(previous)))
        return (reopened.stdout.startswith(b'read=0 ') and reopened.stderr == previous and protected(d)
                and (d / 'user/savegames' / name).stat().st_nlink == 1)

    def fullset(d, out, extra=None):
        code, backup, identity, name = out
        if not backup or not re.fullmatch('[0-9a-f]{64}', identity):
            return False
        b = Path(backup)
        expected = {'Amber.bes': present, 'Birch.bes': alternate, 'Protected.bes': alternate,
                    'defaultoptions.bea': b'disposable options'}
        expected.update(extra or {})
        m = json.loads((b / 'onslaught-backup.json').read_bytes())
        return (len(m['Files']) == len(expected) and
                {x['Name']: x['Sha256'] for x in m['Files']} == {k: H(v) for k, v in expected.items()} and
                all((b / k).read_bytes() == v for k, v in expected.items()) and
                subprocess.run([tools['preview'], b, d / 'user', d / 'game', d / 'package', 'Amber.bes'],
                               capture_output=True, timeout=10).stdout.startswith(identity.encode()))

    def waiting(d, mode, action, op='restore'):
        rd, wr = os.pipe()
        cfg = env(d)
        cfg['CE_STORAGE_TEST_CONTROL_FD'] = str(rd)
        proc = subprocess.Popen([tools['storage'], d, mode, op], cwd=d, env=cfg,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                pass_fds=(rd,))
        os.close(rd)
        try:
            deadline = time.monotonic() + 12
            notice = d / 'notice'
            while time.monotonic() < deadline and not notice.exists() and proc.poll() is None:
                time.sleep(.01)
            assert notice.is_file(), 'controlled syscall boundary not reached'
            line = notice.read_text().splitlines()
            leaf = line[1] if len(line) > 1 else ''
            if action == 'kill':
                os.kill(proc.pid, signal.SIGKILL)
            else:
                action(d, leaf)
                os.write(wr, b'c')
            output, error = proc.communicate(timeout=15)
            (d / 'driver.private.log').write_bytes(output + error)
            if action == 'kill':
                assert proc.returncode == -signal.SIGKILL
                return leaf
            return parse(subprocess.CompletedProcess([], proc.returncode, output, error))
        finally:
            os.close(wr)
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=5)

    d = setup()
    initial = slots(d)
    out = run(d, op='backup')
    check('production_full_set_all_visible_careers_and_user_options', out[0] == COMPLETE and fullset(d, out)
          and initial == slots(d) and protected(d))
    d = setup()
    out = run(d)
    check('production_restores_selected_older_bytes_into_complete_new_card_slot', complete(d, out) and fullset(d, out))
    listing = card(d, 'list')
    check('actual_card_lists_readable_slot', listing.returncode == 0 and out[3][:-4].encode() in listing.stdout)
    check('actual_card_cannot_admit_existing_readable_slot_as_new',
          b'create=6' in card(d, 'create', out[3][:-4]).stdout and complete(d, out))

    written = card(d, 'write', out[3][:-4], str(generated))
    reopened = card(d, 'read', out[3][:-4], str(len(present)))
    check('ordinary_card_can_save_again_into_restored_readable_slot',
          b'write=0' in written.stdout and reopened.stdout.startswith(b'read=0 ')
          and reopened.stderr == present and protected(d))

    for mode, expected in [('no_authority', REFUSED), ('no_callback', REFUSED),
                           ('no_name_approval', REFUSED), ('no_name_callback', REFUSED),
                           ('cancel', CANCELLED), ('stale_preview', CHANGED), ('no_stamp', CHANGED),
                           ('unsupported', UNSUPPORTED), ('stage_readback', REFUSED),
                           ('stage_flush', REFUSED), ('no_space', REFUSED), ('backup_publish', REFUSED)]:
        d = setup()
        before = slots(d)
        out = run(d, mode)
        check(mode + '_refuses_without_a_new_slot', out[0] == expected and before == slots(d) and protected(d))

    for mode in ('final_readback', 'final_flush'):
        d = setup()
        out = run(d, mode)
        check(mode + '_does_not_claim_success_or_remove_a_complete_publication',
              out[0] == UNCERTAIN and (d / 'user/savegames' / out[3]).read_bytes() == previous and protected(d))

    d = setup()
    fixed = 'Amber restored.bes'
    (d / 'confirmed-name').write_text(fixed)
    (d / 'user/savegames' / fixed).write_bytes(alternate)
    out = run(d, 'fixed_name')
    check('occupied_confirmed_name_never_replaced_or_retried', out[0] == OCCUPIED and not out[3]
          and (d / 'user/savegames' / fixed).read_bytes() == alternate and protected(d))

    d = setup()
    (d / 'confirmed-name').write_text('Amber restored.bes')
    (d / 'user/savegames' / 'AMBER RESTORED.BES').write_bytes(alternate)
    out = run(d, 'fixed_name')
    check('existing_case_and_extension_variant_blocks_confirmed_name', out[0] == OCCUPIED
          and not out[3] and (d / 'user/savegames' / 'AMBER RESTORED.BES').read_bytes() == alternate)

    d = setup()
    def old_writer(d, target):
        r = card(d, 'create', target[:-4])
        assert b'create=0' in r.stdout
    out = waiting(d, 'before_link', old_writer)
    new = set(slots(d)) - {'Amber.bes', 'Birch.bes'}
    check('real_old_memory_card_wins_exact_target_race_without_taking_new_lock',
          out[0] == OCCUPIED and not out[3] and len(new) == 1
          and (d / 'user/savegames' / next(iter(new))).read_bytes() == b'' and protected(d))

    d = setup()
    def simultaneous(d, target):
        other = run(d)
        assert other[0] == BUSY and not other[3]
    out = waiting(d, 'before_link', simultaneous)
    check('second_new_api_is_busy_without_affecting_first', complete(d, out) and fullset(d, out))
    second = run(d)
    check('fresh_second_api_attempt_creates_another_slot_with_first_in_its_backup', complete(d, second)
          and fullset(d, second, {out[3]: previous}) and second[3] != out[3])

    for mode, published in [('kill_before_backup', False), ('kill_after_backup', False),
                             ('kill_before_stage', False), ('kill_partial_stage', False),
                             ('kill_after_stage', False), ('kill_before_link', False),
                             ('kill_after_link', True), ('kill_before_readback', True)]:
        d = setup()
        before = slots(d)
        target = waiting(d, mode, 'kill')
        observed = slots(d)
        check(mode + '_all_old_files_and_no_empty_new_career',
              protected(d) and ((observed == dict(before, **{target: H(previous)})
                                 and (d / 'user/savegames' / target).stat().st_nlink == 1)
                                if published else observed == before))

    for stop, expected in [('cancel', CANCELLED), ('revoke', REFUSED)]:
        d = setup()
        def deny(d, name):
            (d / stop).write_text('stop')
        out = waiting(d, 'after_backup', deny)
        check(stop + '_after_backup_refuses_the_career_write', out[0] == expected and not out[3]
              and set(slots(d)) == {'Amber.bes', 'Birch.bes'} and protected(d))
    d = setup()
    out = waiting(d, 'after_link', lambda d, name: (d / 'revoke').write_text('stop'))
    check('revoked_authority_after_publication_warns_without_deleting_new_file',
          out[0] == UNCERTAIN and (d / 'user/savegames' / out[3]).read_bytes() == previous and protected(d))

    for what in ('source', 'source_identity', 'live', 'options', 'new_slot', 'backup'):
        d = setup()
        def mutate(d, backup_name):
            if what == 'source':
                (d / 'source/onslaught-backup-20260405-123000/Amber.bes').write_bytes(b'outside source change')
            elif what == 'source_identity':
                replacement = d / 'source/replacement.bes'
                replacement.write_bytes(previous)
                os.replace(replacement, d / 'source/onslaught-backup-20260405-123000/Amber.bes')
            elif what == 'live':
                (d / 'user/savegames/Birch.bes').write_bytes(present)
            elif what == 'options':
                (d / 'user/defaultoptions.bea').write_bytes(b'outside preferences change')
            elif what == 'new_slot':
                (d / 'user/savegames/Later.bes').write_bytes(alternate)
            else:
                (d / 'external' / backup_name / 'Amber.bes').write_bytes(b'outside backup change')
        out = waiting(d, 'after_backup', mutate)
        check(what + '_changed_after_backup_refuses_career_publication', out[0] != COMPLETE and not out[3]
              and (d / 'user/savegames/Amber.bes').read_bytes() == present)

    for what in ('original', 'suffix_variant', 'changed_existing', 'new_slot'):
        d = setup()
        def race(d, target):
            if what == 'original':
                (d / 'game/savegames' / target).write_bytes(alternate)
            elif what == 'suffix_variant':
                (d / 'user/savegames' / (target[:-4] + '.BES')).write_bytes(alternate)
            elif what == 'changed_existing':
                (d / 'user/savegames/Birch.bes').write_bytes(present)
            else:
                (d / 'user/savegames/Later.bes').write_bytes(alternate)
        out = waiting(d, 'before_link', race)
        check(what + '_uncooperative_write_in_final_gap_is_uncertain_and_never_reverted',
              out[0] == UNCERTAIN and (d / 'user/savegames' / out[3]).read_bytes() == previous
              and (d / 'user/savegames/Amber.bes').read_bytes() == present)

    d = setup()
    (d / 'game/savegames/Amber.bes').write_bytes(previous)
    out = run(d)
    check('effective_user_namespace_precedence_and_shadowed_original_preserved',
          complete(d, out) and fullset(d, out) and (d / 'game/savegames/Amber.bes').read_bytes() == previous)
    d = setup()
    (d / 'user/defaultoptions.bea').unlink()
    out = run(d, op='backup')
    check('absent_user_preferences_not_fabricated', out[0] == COMPLETE and
          {x['Name'] for x in json.loads((Path(out[1]) / 'onslaught-backup.json').read_bytes())['Files']}
          == {'Amber.bes', 'Birch.bes', 'Protected.bes'} and not (d / 'user/defaultoptions.bea').exists())

    for kind in ('casefold', 'bad_career', 'symlink_file', 'hardlink_file', 'linked_root'):
        d = setup()
        if kind == 'casefold':
            (d / 'user/savegames/aMBER.bes').write_bytes(alternate)
        elif kind == 'bad_career':
            (d / 'user/savegames/Unknown.bes').write_bytes(b'unsupported')
        elif kind == 'symlink_file':
            (d / 'user/savegames/Alias.bes').symlink_to(d / 'user/savegames/Amber.bes')
        elif kind == 'hardlink_file':
            os.link(d / 'user/savegames/Amber.bes', d / 'user/savegames/Alias.bes')
        else:
            (d / 'external').rmdir()
            (d / 'external').symlink_to(d / 'user', target_is_directory=True)
        before = slots(d)
        out = run(d)
        check(kind + '_refuses_entire_operation_without_silently_skipping_member',
              out[0] == REFUSED and not out[3] and slots(d) == before)

    def whole_tree(d):
        return {str(x.relative_to(d)): H(x.read_bytes()) for x in d.rglob('*') if x.is_file()}

    def select_source_name(d, name):
        src=d / 'source/onslaught-backup-20260405-123000'
        (src / 'Amber.bes').unlink()
        (src / name).write_bytes(previous)
        manifest=json.loads((src / 'onslaught-backup.json').read_bytes())
        manifest['Files'][0]['Name']=name
        manifest['Files'][0]['Original']='/authored-only/'+name
        (src / 'onslaught-backup.json').write_text(json.dumps(manifest))
        (d / 'source-name').write_text(name)

    d=setup()
    before=whole_tree(d)
    out=run(d,op='suggest')
    check('readable_suggestion_is_entirely_read_only', out[0]==COMPLETE
          and out[3]=='Amber restored.bes' and whole_tree(d)==before)
    check('suggestion_needs_no_semantic_approval_but_restore_still_does',
          run(d,'no_name_callback','suggest')[3]=='Amber restored.bes'
          and run(d,'no_name_callback')[0]==REFUSED and set(slots(d))=={'Amber.bes','Birch.bes'})

    d=setup()
    (d / 'user/savegames/AMBER RESTORED.bes').write_bytes(alternate)
    (d / 'game/savegames/Amber restored 2.BES').write_bytes(alternate)
    out=run(d,op='suggest')
    check('suggestion_avoids_case_and_extension_aliases_across_both_locations',
          out[0]==COMPLETE and out[3]=='Amber restored 3.bes')

    d=setup()
    suggestion=run(d,op='suggest')[3]
    (d / 'confirmed-name').write_text(suggestion)
    (d / 'user/savegames' / suggestion).write_bytes(alternate)
    out=run(d)
    check('suggestion_is_not_a_reservation_and_confirmation_never_auto_renames',
          out[0]==OCCUPIED and not out[3] and (d/'user/savegames'/suggestion).read_bytes()==alternate
          and 'Amber restored 2.bes' not in slots(d))

    for source_name, wanted in [('Amber restored.bes','Amber restored 2.bes'),
                                 ('Amber restored 8.bes','Amber restored 9.bes'),
                                 ('The Restored Pilot.bes','The Restored Pilot restored.bes')]:
        d=setup();select_source_name(d,source_name)
        out=run(d,op='suggest')
        check('repeated_copy_proposal_'+source_name, out[0]==COMPLETE and out[3]==wanted)

    d=setup();select_source_name(d,'A'*31+'.bes')
    out=run(d,op='suggest')
    check('long_name_proposal_stays_within_actual_native_name_limit',
          out[0]==COMPLETE and out[3]=='A'*22+' restored.bes' and len(out[3][:-4])==31)
    restored=run(d)
    readback=card(d,'read',restored[3][:-4],str(len(previous)))
    check('actual_game_lists_reopens_and_saves_bounded_long_readable_name',
          restored[0]==COMPLETE and restored[3][:-4].encode() in card(d,'list').stdout
          and readback.stdout.startswith(b'read=0 ') and readback.stderr==previous
          and b'write=0' in card(d,'write',restored[3][:-4],str(generated)).stdout)

    d=setup();select_source_name(d,'A'*27+'KEEP.bes')
    proposed=run(d,'protected_tail','suggest')
    out=run(d,'protected_tail')
    check('unsafe_shortening_is_not_semantically_approved_by_suggestion',
          proposed[0]==COMPLETE and 'KEEP' not in proposed[3]
          and out[0]==REFUSED and not out[3] and set(slots(d))=={'Amber.bes','Birch.bes'})

    d=setup();(d/'confirmed-name').write_text('Other restored.bes')
    out=run(d,'immutable_pair')
    check('semantic_approval_cannot_be_reused_for_another_confirmed_pair',
          out[0]==REFUSED and not out[3] and set(slots(d))=={'Amber.bes','Birch.bes'})

    for bad in ('../Escape.bes','CON.bes','Trailing .bes','A'*32+'.bes'):
        d=setup();(d/'confirmed-name').write_text(bad)
        out=run(d)
        check('nonportable_confirmed_name_refuses_'+bad,
              out[0]==REFUSED and not out[3] and set(slots(d))=={'Amber.bes','Birch.bes'})

    d=setup()
    out=waiting(d,'after_backup',lambda d, name:(d/'deny-name').write_text('deny'))
    check('revoked_pair_approval_before_publication_refuses',out[0]==REFUSED and not out[3])
    d=setup()
    out=waiting(d,'after_link',lambda d, name:(d/'deny-name').write_text('deny'))
    check('revoked_pair_approval_after_publication_warns_without_rollback',
          out[0]==UNCERTAIN and (d/'user/savegames'/out[3]).read_bytes()==previous and protected(d))

    d=setup()
    for n in range(2048):
        (d/'user/savegames'/('irrelevant-'+str(n))).touch()
    before=slots(d)
    proposed=run(d,op='suggest')
    out=run(d)
    check('irrelevant_directory_entries_are_bounded_for_suggestion_and_publication',
          proposed[0]==REFUSED and not proposed[3] and out[0]==REFUSED and not out[3]
          and slots(d)==before and protected(d))

    # Full backup set: every cut leaves no new listed set and an older good set as it was.
    def listed(d, tool='storage'):
        # Backup Preview's own reader: (name, identity or '-', members, careers, reason) per listed set.
        r = subprocess.run([tools[tool], d, 'normal', 'list'], capture_output=True, env=env(d), cwd=d, timeout=20)
        assert r.returncode == 0, 'list driver failed'
        lines = r.stdout.decode().splitlines()
        assert lines[0] == '1', 'backup folder could not be listed'
        return [tuple(x.split(' ', 4)) for x in lines[1:]]

    def tree(folder):
        return {str(x.relative_to(folder)): H(x.read_bytes()) for x in sorted(folder.rglob('*')) if x.is_file()}

    def older_good(d):
        out = run(d, op='backup')
        assert out[0] == COMPLETE and fullset(d, out)
        older = Path(out[1])
        return older, tree(older), listed(d)

    d = setup()
    # An older per-career set, as Back Up Career writes its manifest, in the same folder.
    single = d / 'external/onslaught-backup-20260405-123000'
    shutil.copytree(d / 'source/onslaught-backup-20260405-123000', single)
    m = json.loads((single / 'onslaught-backup.json').read_bytes())
    m['Reason'] = 'Manual: selected CE career'
    (single / 'onslaught-backup.json').write_text(json.dumps(m))
    single_rows = listed(d)
    out = run(d, op='backup')
    rows = [r for r in listed(d) if r not in single_rows]
    check('full_set_reason_and_contents_read_by_preview_reader',
          out[0] == COMPLETE and fullset(d, out) and len(rows) == 1 and rows[0][0] == Path(out[1]).name
          and rows[0][1] == out[2] and rows[0][2:] == ('4', '3', 'All careers')
          and json.loads((Path(out[1]) / 'onslaught-backup.json').read_bytes())['Reason'] == 'All careers')
    check('full_set_beside_older_per_career_and_restore_sets',
          run(d, op='restore')[0] == COMPLETE and
          sorted(r[4] for r in listed(d)) == ['All careers', 'Manual', 'Manual: selected CE career'])

    CUTS = {}
    def cut_case(mode, before_rename, tool='storage'):
        d = setup()
        older, older_tree, before = older_good(d)
        out = run(d, mode, 'backup', tool)
        after = listed(d, tool)
        new = [r for r in after if r not in before]
        pending = sorted(x.name for x in (d / 'external').iterdir() if x.name.startswith('.pending-'))
        CUTS[mode + ('' if tool == 'storage' else '_' + tool)] = dict(
            status=out[0], listed_before=len(before), listed_after=len(after),
            new_listed=[dict(name=r[0], verified=r[1] != '-') for r in new], private_staging_left=len(pending))
        older_same = tree(older) == older_tree and before[0] in after
        if before_rename:
            # Nothing new is listed at all: the cut left at most a private staging folder.
            return out[0] not in (COMPLETE, UNCERTAIN) and not new and older_same and protected(d) and initial_slots(d)
        # After the rename: the one new listed set is the set verified while staged, never a partial one.
        return (out[0] == UNCERTAIN and len(new) == 1 and new[0][1] != '-' and new[0][2:] == ('4', '3', 'All careers')
                and older_same and protected(d) and initial_slots(d))

    def initial_slots(d):
        return set(slots(d)) == {'Amber.bes', 'Birch.bes'}

    for mode in ('cut_staging', 'cut_member_1', 'cut_member_2', 'cut_member_3', 'cut_member_4',
                 'cut_member_fsync', 'cut_manifest', 'cut_set_fsync', 'cut_staged_verify', 'backup_publish'):
        check(mode + '_lists_no_new_set_and_keeps_older_set', cut_case(mode, True))
    for mode in ('cut_root_fsync', 'cut_readback'):
        check(mode + '_after_rename_is_uncertain_and_lists_only_the_verified_set', cut_case(mode, False))

    d = setup()
    older, older_tree, before = older_good(d)
    def collide(d, leaf):
        # An existing good set already holds the exact listed name being published.
        shutil.copytree(older, d / 'external' / leaf)
    out = waiting(d, 'collide_before_rename', collide, 'backup')
    check('full_set_never_replaces_a_set_of_the_same_name',
          out[0] == REFUSED and tree(older) == older_tree and
          len([r for r in listed(d) if r[1] != '-']) == 2 and
          all(tree(d / 'external' / r[0]) == older_tree for r in listed(d)))

    for kind, change in (('no_careers', lambda d: [(d / n).unlink() for n in
                          ('user/savegames/Amber.bes', 'user/savegames/Birch.bes', 'game/savegames/Protected.bes')]),
                         ('too_many_members', lambda d: [(d / 'user/savegames' / ('Extra%02d.bes' % i)).write_bytes(present)
                                                         for i in range(29)])):
        d = setup()
        change(d)
        out = run(d, op='backup')
        check(kind + '_refused_before_anything_is_written', out[0] == REFUSED and not out[1] and
              not any((d / 'external').iterdir()))

    # Take a verified full set or refuse: Complete carries set, identity and stamp; a cut is a refusal.
    d = setup()
    out = run(d, op='take')
    check('take_full_set_returns_set_identity_and_stamp',
          out[0] == COMPLETE and fullset(d, out) and re.fullmatch('[0-9a-f]{64}', out[3])
          and listed(d)[0][1] == out[2])
    for mode, expected in (('cut_member_2', REFUSED), ('cut_staged_verify', REFUSED), ('no_authority', REFUSED),
                           ('cut_readback', UNCERTAIN), ('cut_rebind', UNCERTAIN)):
        d = setup()
        out = run(d, mode, 'take')
        check('take_full_set_refuses_' + mode, out[0] == expected and not out[2] and not out[3]
              and bool(out[1]) == (expected == UNCERTAIN) and protected(d)
              and (expected != UNCERTAIN or [r[0] for r in listed(d) if r[1] != '-'] == [Path(out[1]).name]))

    # Restore as New takes one career out of a full set, adds it under a new name and changes no existing career.
    d = setup()
    full = run(d, op='backup')
    assert full[0] == COMPLETE
    (d / 'source-set').write_text(full[1])
    (d / 'source-name').write_text('Birch.bes')
    full_tree = tree(Path(full[1]))
    before = slots(d)
    out = run(d)
    after = slots(d)
    check('restore_as_new_takes_one_career_out_of_a_full_set',
          out[0] == COMPLETE and out[3] == 'Birch restored.bes'
          and (d / 'user/savegames' / out[3]).read_bytes() == alternate
          and {k: v for k, v in after.items() if k != out[3]} == before and protected(d)
          and tree(Path(full[1])) == full_tree and Path(out[1]) != Path(full[1]))

    # The control that must fail: with the protection removed, the same cut must break the same assertion.
    control = cut_case('cut_manifest', True, 'mutant')
    CONTROL = dict(name='mutant_staging_takes_listed_name_cut_manifest', assertion_held=control,
                   evidence=CUTS['cut_manifest_mutant'])
    if control:
        raise AssertionError('control passed: the cut assertion cannot detect a partial listed set (tool failure)')

    result = dict(source=subprocess.check_output(['git', '-C', ROOT, 'rev-parse', 'HEAD'], text=True).strip(),
                  checks=RESULTS, timings=TIMINGS, skip_count=0, cuts=CUTS, must_fail_control=CONTROL, production_backend=True, game_memory_card_reader=True,
                  private_profiles=str(work.relative_to(ROOT)), ordinary_save_source_changed=False,
                  no_ui=True, no_real_profiles=True, native_windows=False,
                  limitations=['Readable confirmed filename requires exact pair approval on the game thread; suggestions do not authorize a name.',
                               'Other writers can change files after the final check; postpublication warnings preserve all files.',
                               'Only supported ASCII card namespace and supported local filesystems; Windows unsupported.'])
    path = work / 'result.private.json'
    path.write_text(json.dumps(result, indent=2) + '\n')
    print(path.relative_to(ROOT), len(RESULTS))


if __name__ == '__main__':
    os.umask(0o077)
    main()
