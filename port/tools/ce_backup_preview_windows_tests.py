#!/usr/bin/env python3
"""Run real Windows preview and authored picker controls inside the maintained Wine boundary."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
DRIVER = 'port/tools/tests/ce_backup_preview_windows_driver.cpp'
SUPPORT = ['port/platform/windows/ce_backup_preview.cpp', 'port/platform/windows/ce_backup_paths.h',
           'port/platform/common/ce_backup_preview.cpp', 'port/platform/common/ce_backup_preview.h',
           'port/platform/common/ce_backup_preview_ui.h', 'port/platform/common/ce_backup_verify.h',
           'port/platform/common/ce_backup_manifest.h', 'port/platform/common/ce_backup_name.h',
           'port/game/ce_career_view.cpp', 'port/game/ce_career_view.h', 'port/updater/sha256.c',
           'port/updater/sha256.h', 'port/game/ce_extras.cpp', DRIVER, 'port/tools/ce_backup_preview_windows_tests.py']
CONTROLS = ['before-and-after-profile-roots-and-sets', 'list-and-sort', 'selected-unicode-career-and-options',
            'multi-career-and-disjoint-proposals', 'missing-manifest', 'corrupt-career', 'corrupt-options',
            'missing-member', 'unsupported-member', 'ambiguous-leaf', 'protected-alias-and-roots',
            'linked-member', 'manifest-shape', 'read-only-busy-member', 'normal-selected-card',
            'cancel-keeps-card', 'cancel-before-selection', 'stale-dialog-after-back',
            'typed-folder-card', 'recheck-replaced-set', 'leave-clears-card', 'writer-interoperability', 'held-byte-identical-replacement', 'final-owner-after-worker-back']
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def members(base):
    result = {}
    for p in sorted(base.rglob('*')):
        key = p.relative_to(base).as_posix()
        if p.is_symlink():
            result[key] = ['link', os.readlink(p)]
        elif p.is_file():
            st = p.stat()
            result[key] = ['file', st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, sha(p)]
        elif p.is_dir():
            result[key] = ['dir', p.stat().st_ino]
    return result

def seed(base):
    base.mkdir()
    for n in ('writer','user/savegames','game/savegames','package','sets','user/nested'):
        (base / n).mkdir(parents=True)
    raw = Path('/work/in/sample.bes')
    career = raw.read_bytes()
    for n in ('user/savegames/Backup Copy.bes', 'game/savegames/BACKUP COPY 2.BES',
              'user/savegames/Do not change.bes', 'user/ce.ini','game/defaultoptions.bea','package/keep'):
        (base / n).write_bytes(('Authored sentinel: '+n).encode())
    shutil.copytree('/work/in/published',base/'writer/published')
    main = 'Auréשל.bes'
    extra = b'authored options: never install'
    def make(name, files, actual=None, change=None):
        dest=base/'sets'/name
        dest.mkdir()
        rows=[]
        for leaf,data in files:
            rows.append(dict(Name=leaf,Original='C:\\never-follow\\'+leaf,Size=len(data),
                             Sha256=hashlib.sha256(data).hexdigest()))
        for leaf,data in (files if actual is None else actual):
            (dest/leaf).write_bytes(data)
        manifest=dict(Folder='C:\\never-follow',Created='2026-04-05T12:30:00Z',
                      Game='C:\\never-follow',Files=rows,Reason='Manual',Target=1)
        if change: change(manifest)
        (dest/'onslaught-backup.json').write_text(json.dumps(manifest,ensure_ascii=True),encoding='utf-8')
        return dest
    good=make('onslaught-backup-02',[(main,career),('defaultoptions.bea',extra)])
    make('onslaught-backup-01',[(main,career),('Pilot.bes',career),('defaultoptions.bea',extra)])
    compare=make('replacement', [('Other.bes',career)],change=lambda m:m.update(Reason='Changed'))
    make('bad-career',[(main,career)],[(main,bytes(len(career)))])
    make('bad-options',[(main,career),('defaultoptions.bea',extra)],[(main,career),('defaultoptions.bea',b'x'*len(extra))])
    make('missing',[(main,career)],[])
    make('bad-type',[(main,career),('read.me',b'x')])
    make('bad-leaf',[(main,career)],[(main,career)],lambda m:m['Files'][0].update(Name='Pilot\u202e.bes'))
    make('bad-shape',[(main,career)],change=lambda m:m.pop('Files'))
    make('held-same',[(main,career)])
    make('held-leave',[(main,career)])
    hard=make('hard',[(main,career)])
    os.link(hard/main,hard/'another')
    (base/'sets/onslaught-backup-00').mkdir()
    (base/'sets/no-manifest').mkdir()
    # The guest later maps a second drive to this existing protected subtree.
    inside=base/'user/nested/onslaught-backup-05'
    shutil.copytree(good,inside)
    return good, compare

def build(work, boundary_root, depdir):
    root = ROOT
    sources = [DRIVER,'port/platform/windows/ce_backup_preview.cpp','port/platform/common/ce_backup_preview.cpp',
               'port/game/ce_career_view.cpp']
    for arch, target in [('i686', boundary_root/'preview-shipping32.exe'),('x86_64', work/'bin/preview.exe')]:
        compiler = depdir / ('llvm-mingw/bin/'+arch+'-w64-mingw32-clang++')
        resource = boundary_root/(arch+'-manifest.o')
        options=[str(compiler),'-std=c++17','-O2','-static','-I'+str(depdir/'prefix-win32/include')]
        with (boundary_root/(arch+'-preview-compile.log')).open('xb') as out:
            subprocess.run([*options,*[str(root/s) for s in sources],str(resource),'-o',str(target)],
                           cwd=root,stdout=out,stderr=subprocess.STDOUT,check=True)
            if arch!='x86_64':continue
            # Run an additional instrumented executable solely for two held-handle
            # controls. The ordinary path and its card run in the exact binary above.
            reader=(root/'port/platform/windows/ce_backup_preview.cpp').read_text()
            tail='data.resize(static_cast<size_t>(initial.size));'
            if reader.count(tail)!=1:raise RuntimeError('unexpected Windows bounded read')
            rr=boundary_root/'observed-reader.cpp'
            rr.write_text('extern void CePreviewObserveHeld(const wchar_t *);\n'+reader.replace(
                          tail,'CePreviewObserveHeld(leaf.c_str());\n        '+tail))
            control=(root/'port/platform/common/ce_backup_preview.cpp').read_text()
            tail2='}).detach();'
            if control.count(tail2)!=2:raise RuntimeError('unexpected controller worker lifetime')
            cc=boundary_root/'observed-controller.cpp'
            cc.write_text('extern void CePreviewObserveCompleted();\n'+control.replace(
                          tail2,'CePreviewObserveCompleted();\n      '+tail2,1))
            subprocess.run([*options,'-I'+str(root/'port/platform/windows'),'-I'+str(root/'port/platform/common'),
                            str(root/DRIVER),str(rr),str(cc),str(root/'port/game/ce_career_view.cpp'),str(resource),
                            '-o',str(work/'bin/preview-observed.exe')],cwd=root,stdout=out,stderr=subprocess.STDOUT,check=True)
    samplegen=boundary_root/'make-sample'
    with (boundary_root/'sample-compile.log').open('xb') as out:
        subprocess.run(['c++','-std=c++17','-O1',str(root/'port/tools/tests/ce_backup_preview_driver.cpp'),
                        str(root/'port/platform/linux/ce_backup_preview.cpp'), str(root/'port/game/ce_career_view.cpp'),
                        '-o',str(samplegen)],stdout=out,stderr=subprocess.STDOUT,check=True)
        subprocess.run([str(samplegen),'fixture',str(work/'in/sample.bes')],stdout=out,stderr=subprocess.STDOUT,check=True)
        # Produce a real CE/Companion-format set with the existing writer, then
        # transport just that authored result across the boundary.
        seeder=boundary_root/'make-backup'
        subprocess.run(['c++','-std=c++17','-O1',str(root/'port/tools/tests/ce_backup_store_driver.cpp'),
                        str(root/'port/platform/linux/ce_backup_store.cpp'),'-o',str(seeder)],
                       stdout=out,stderr=subprocess.STDOUT,check=True)
        fixture=boundary_root/'writer-source'
        for n in ('user/savegames','game/savegames','package','external'):
            (fixture/n).mkdir(parents=True)
        original=fixture/'user/savegames/Writer.bes'
        shutil.copyfile(work/'in/sample.bes',original)
        subprocess.run([str(seeder),str(fixture/'user'),str(fixture/'game'),str(fixture/'package'),
                        str(fixture/'external'),original.name,str(original)],stdout=out,stderr=subprocess.STDOUT,check=True)
        sets=list((fixture/'external').iterdir())
        if len(sets)!=1 or sha(sets[0]/original.name)!=sha(original):
            raise RuntimeError('the actual CE writer did not produce an exact career')
        shutil.copytree(sets[0],work/'in/published')
    shutil.copyfile(__file__,work/'tools/ce_backup_preview_windows_tests.py')
    return {s:sha(root/s) for s in [*SUPPORT,'port/tools/tests/ce_backup_preview_driver.cpp',
                                    'port/platform/linux/ce_backup_preview.cpp','port/platform/linux/ce_backup_store.cpp',
                                    'port/tools/tests/ce_backup_store_driver.cpp']}

def exercise(work, prefix, run):
    base=Path(prefix)/'drive_c/ce-preview-authored'
    good, _=seed(base)
    before=members(base)
    (Path(prefix)/'dosdevices'/'t:').symlink_to('../drive_c/ce-preview-authored/user/nested')
    rc=run(['/usr/bin/wine','/work/bin/preview.exe'],'preview')
    ordinary_unchanged=before==members(base)
    if rc:
        return dict(wine_process=rc, unchanged=ordinary_unchanged, failed=['normal-windows-controls']),False
    # Test instrumentation pauses only the consumer of the real read. Replace
    # a named byte-identical member from this disposable guest namespace while
    # its old Windows handle is retained. The original entry is put back before
    # the independent final profile/backup inventory.
    import threading
    controls=base/'test-coordination'; controls.mkdir()
    actions=[]
    def file_controls():
        for n,dirname in [('same','held-same'),('leave','held-leave')]:
            ready=controls/(n+'-entered')
            deadline=time.monotonic()+18
            while not ready.exists() and time.monotonic()<deadline:time.sleep(.005)
            if not ready.exists():return
            member=base/'sets'/dirname/'Auréשל.bes'
            saved=controls/('original-'+n)
            if n=='same':
                member.rename(saved)
                shutil.copyfile(saved,member)
                actions.append(saved.stat().st_ino!=member.stat().st_ino and sha(member)==sha(saved))
            if n=='leave':
                while not (controls/'leave-closed').exists() and time.monotonic()<deadline:time.sleep(.005)
                if not (controls/'leave-closed').exists():return
            (controls/(n+'-release')).touch()
            done=controls/(n+'-done')
            while not done.exists() and time.monotonic()<deadline:time.sleep(.005)
            if n=='same':
                if member.exists():member.unlink()
                saved.rename(member)
            if not done.exists():return
    observer=threading.Thread(target=file_controls); observer.start()
    try:
        instrumented=run(['/usr/bin/wine','/work/bin/preview-observed.exe','observed'],'preview-observed')
    finally:
        observer.join(40)
    if observer.is_alive():raise RuntimeError('the authored observer did not finish')
    for f in controls.iterdir():
        if f.name.startswith('original-'):raise RuntimeError('original test member still held')
        f.unlink()
    controls.rmdir()
    after=members(base)
    lines=(work/'out/preview.log').read_text(errors='replace').splitlines()
    observed_lines=(work/'out/preview-observed.log').read_text(errors='replace').splitlines()
    reported=[line[5:].strip() for line in lines+observed_lines if line.startswith('PASS ')]
    failures=[line[5:].strip() for line in lines+observed_lines if line.startswith('FAIL ')]
    if before == after and ordinary_unchanged:
        reported.append(CONTROLS[0])
    backend = {}
    for name in ('01','02','writer'):
        line = next((l.split() for l in lines if l.startswith('READ '+name+' ')), [])
        if len(line)==5:
            backend[name] = dict(manifest=line[2], members=int(line[3]), careers=int(line[4]))
    manifests = {'01':base/'sets/onslaught-backup-01','02':good,'writer':base/'writer/published'}
    agrees = actions==[True] and len(backend)==3 and all(backend[n]['manifest']==sha(d/'onslaught-backup.json')
             and backend[n]['members']==len(json.loads((d/'onslaught-backup.json').read_text())['Files'])
             for n,d in manifests.items())
    result=dict(wine_process=rc, observed_process=instrumented, normal_unchanged=ordinary_unchanged, controls=reported, readback=backend, independent_manifest_readback=agrees,failed=failures,unchanged=(before==after),
                protected_and_backup_entries=len(before),sample_sha256=sha(work/'in/sample.bes'),
                manifest_sha256=sha(good/'onslaught-backup.json'),native_windows=False,
                game_ui=False,authored_picker_delivery=True)
    return result, rc==0 and instrumented==0 and agrees and not failures and sorted(reported)==sorted(CONTROLS)

if __name__=='__main__':
    sys.path.insert(0,str(ROOT/'port/tools'))
    import ce_career_backup_windows_tests as writer
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('name')
    a=parser.parse_args()
    sys.argv=[sys.argv[0],a.name,'--preview']
    raise SystemExit(writer.main())
