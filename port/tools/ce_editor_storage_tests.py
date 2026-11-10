#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native editor storage boundaries on an authored private folder.

No game, career, display or network is involved. Windows uses the same production
storage translation unit but needs its own platform acceptance; this driver is Linux.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path);args=p.parse_args()
    repo=Path(__file__).resolve().parents[2];root=repo/'local-data/native-editor-storage-tests';root.mkdir(parents=True,exist_ok=True)
    owner=(args.out or Path(tempfile.mkdtemp(prefix='run-',dir=root))).resolve();owner.mkdir(parents=True,exist_ok=True)
    compiler=shutil.which('clang++') or shutil.which('g++')
    if not compiler:raise SystemExit('A C++ compiler and SDL3 headers are required')
    exe=owner/'driver';cmd=[compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(repo/'port/game'),
        str(repo/'port/platform/common/ce_editor_storage.cpp'),str(repo/'port/tools/tests/ce_editor_storage_driver.cpp'),'-o',str(exe)]
    c=subprocess.run(cmd,capture_output=True,timeout=60);(owner/'compile.stdout').write_bytes(c.stdout);(owner/'compile.stderr').write_bytes(c.stderr)
    if c.returncode:print('editor storage compile FAILED; private output:',owner);return 1
    user=owner/'user';user.mkdir(mode=0o700);payload=owner/'authored.json';payload.write_text('{"authored":"project references only"}\n')
    results=[]
    def run(name,expected,*arguments):
        r=subprocess.run([str(exe),*map(str,arguments)],capture_output=True,timeout=10)
        (owner/(name+'.stdout')).write_bytes(r.stdout);(owner/(name+'.stderr')).write_bytes(r.stderr)
        results.append({'name':name,'exit':r.returncode,'expected':expected,'passed':r.returncode==expected})
        return r.stdout.decode().strip()
    run('empty-list',0,user,'list');results.append({'name':'list-does-not-create-folder','passed':not (user/'projects').exists()})
    first=run('first-save',0,user,'save',payload);second=run('second-save',0,user,'save',payload)
    folder=user/'projects';results.append({'name':'exclusive-revisions','passed':first!=second and (folder/first).read_bytes()==payload.read_bytes() and (folder/second).read_bytes()==payload.read_bytes()})
    out=owner/'readback';run('readback',0,user,'read',first,out);results.append({'name':'exact-readback','passed':out.read_bytes()==payload.read_bytes()})
    run('list-two',0,user,'list');run('bounded-list-refusal',1,user,'list','1')
    run('parent-traversal',1,user,'read','../authored.json');run('absolute-path',1,user,'read',payload)
    run('hidden-file',1,user,'read','.hidden.ceproject');run('unsupported-extension',1,user,'read','file.txt')
    (folder/'linked.ceproject').symlink_to(payload);run('leaf-link',1,user,'read','linked.ceproject')
    os.mkfifo(folder/'pipe.ceproject');run('nonblocking-fifo',1,user,'read','pipe.ceproject')
    (folder/'directory.ceproject').mkdir();run('directory',1,user,'read','directory.ceproject')
    (folder/'empty.ceproject').write_bytes(b'');run('empty-project',1,user,'read','empty.ceproject')
    with (folder/'oversize.ceproject').open('wb') as f:f.truncate((8<<20)+1)
    run('oversize-project',1,user,'read','oversize.ceproject')
    run('list-ignores-unsafe-leaves',0,user,'list')
    other=owner/'folder-link-user';other.mkdir();(other/'projects').symlink_to(folder,target_is_directory=True)
    before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir() if p.name in (first,second)}
    run('folder-link-read',1,other,'read',first);run('folder-link-save',1,other,'save',payload);run('folder-link-list',1,other,'list')
    ancestor=owner/'ancestor-link';ancestor.symlink_to(user,target_is_directory=True);run('ancestor-link-save',1,ancestor,'save',payload)
    nofolder=owner/'file-folder-user';nofolder.mkdir();(nofolder/'projects').write_text('authored sentinel')
    run('folder-is-file',1,nofolder,'save',payload)
    run('invalid-level-low',1,user,'save',payload,'0');run('invalid-level-high',1,user,'save',payload,'10000')
    empty=owner/'empty.json';empty.write_bytes(b'');run('empty-save',1,user,'save',empty)
    results.append({'name':'existing-files-preserved','passed':all(hashlib.sha256((folder/n).read_bytes()).hexdigest()==h for n,h in before.items())})
    results.append({'name':'refused-folder-file-preserved','passed':(nofolder/'projects').read_text()=='authored sentinel'})
    passed=all(r['passed'] for r in results)
    pins={str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
          (repo/'port/platform/common/ce_editor_storage.cpp',repo/'port/game/ce_editor_storage.h',repo/'port/updater/ce_report.h',repo/'port/tools/tests/ce_editor_storage_driver.cpp',Path(__file__).resolve())}
    (owner/'RESULT.private.json').write_text(json.dumps({'passed':passed,'scope':'authored Linux filesystem behavior only','cases':results,'sha256':pins},indent=2)+'\n')
    print('editor storage:', 'PASS' if passed else 'FAIL',len(results),'checks |',owner)
    return 0 if passed else 1

if __name__=='__main__':raise SystemExit(main())
