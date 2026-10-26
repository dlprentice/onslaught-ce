#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded, synchronized witnesses for the launcher's actual log_open/log_file callers.

Host-only authored writer processes, no game/profile/display/network/SDL initialization. Forced rename failure is
an explicit filesystem-boundary injection; successful active-old unlink is observed on this host, not Windows proof.
All sources, separate output pipes, phase snapshots and identities remain in a fresh ignored local-data directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import select
import shutil
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
PORT=HERE.parents[1]
ROOT=PORT.parent
sys.path.insert(0,str(PORT/'tools'))
import deps


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


class Writer:
    def __init__(self,binary,folder,label,mode,env):
        self.folder=folder;self.label=label;self.events=[];self.finished=False
        self.process=subprocess.Popen([str(binary),str(folder),label,mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,bufsize=0)
        self.identity={'pid':self.process.pid,'command':self.process.args,'start_ticks':Path(f'/proc/{self.process.pid}/stat').read_text().split()[21]}
        self.ready=None
    def ack(self,wanted):
        for _ in range(16):
            if not select.select([self.process.stdout],[],[],5)[0]:raise RuntimeError('writer acknowledgement timeout: '+self.label)
            raw=self.process.stdout.readline()
            if not raw:raise RuntimeError('writer exited before acknowledgement: '+self.label)
            event=json.loads(raw);self.events.append(event)
            if event.get('ack')==wanted:return event
        raise RuntimeError('too many writer events: '+self.label)
    def command(self,token):
        self.process.stdin.write((token+'\n').encode());self.process.stdin.flush();return self.ack(token)
    def finish(self):
        if self.finished:return
        self.command('CLOSE');out,err=self.process.communicate(timeout=5)
        if out:raise RuntimeError('unexpected trailing control output')
        self.finished=True
        (self.folder.parent/(self.label+'.stderr')).write_bytes(err)
        (self.folder.parent/(self.label+'.stdout.jsonl')).write_text(''.join(json.dumps(e)+'\n' for e in self.events))
        if self.process.returncode:raise RuntimeError('writer failed: '+self.label)
        return err
    def cleanup(self):
        if self.process.poll() is None:
            try:self.finish()
            except (OSError,RuntimeError,subprocess.TimeoutExpired):
                command=Path(f'/proc/{self.process.pid}/cmdline').read_bytes().split(b'\0')
                if command[0]!=os.fsencode(self.process.args[0]):raise RuntimeError('owned writer identity changed')
                self.process.terminate()
                try:self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:self.process.kill();self.process.wait(timeout=2)
        if not self.finished:
            out,err=self.process.communicate(timeout=2)
            (self.folder.parent/(self.label+'.stderr')).write_bytes(err)
            (self.folder.parent/(self.label+'.stdout.jsonl')).write_text(''.join(json.dumps(e)+'\n' for e in self.events))
            if out:(self.folder.parent/(self.label+'.stdout.trailing')).write_bytes(out)
            self.finished=True


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source-dir',type=Path,required=True,help='frozen pre-lease launcher source/header directory');args=parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    parent=ROOT/'local-data/port/log-concurrency-tests';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));selected=out/'selected';selected.mkdir()
    inputs={}
    for name in ('ce_launch.c','ce_launch_rules.h','ce_report.h','sha256.h'):
        shutil.copyfile(args.source_dir/name,selected/name);inputs['port/updater/'+name]=sha(selected/name)
    shutil.copyfile(HERE/'log_concurrency_driver.c',selected/'driver.c');inputs['driver']=sha(selected/'driver.c')
    binary=out/'log-writer';mc=deps.SRC/'monocypher-4.0.2/src'
    command=[str(deps.DATA/'llvm/bin/clang'),'--target=i686-pc-linux-gnu',*deps.sysroot_flags(),'-std=gnu11','-O1','-g',
             '-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-ffunction-sections','-fdata-sections',
             f'-I{deps.PREFIX/"include"}',f'-I{mc}',f'-I{mc/"optional"}',f'-I{selected}',
             f'-DCE_LAUNCH_SOURCE="{selected/"ce_launch.c"}"','-DCE_VERSION="fixture"','-DCE_COMMIT="log-concurrency"',
             str(selected/'driver.c'),'-Wl,--gc-sections',f'-L{deps.PREFIX/"lib"}','-lSDL3',f'-Wl,-rpath,{deps.PREFIX/"lib"}',
             '-o',str(binary),*deps.sysroot_link_flags()]
    receipt={'inputs':inputs,'fixture_checkout_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
             'selected_source_directory':str(args.source_dir.resolve()),
             'cases':[],'pass':False,'limits':['Actual complete launcher source included; only renamed main is unreachable.',
             'Only filesystem rename failure is injected. Host unlink/open-sharing is not native Windows evidence.',
             'No game, player files, display, network, downloads or SDL subsystem initialization.']}
    writers=[]
    def snapshot(case,phase):
        result={};folder=case/'snapshots';folder.mkdir(exist_ok=True)
        for name in ('onslaught-ce.log','onslaught-ce.old.log'):
            path=case/'user'/name
            if path.exists():
                raw=path.read_bytes();copy=folder/(phase+'-'+name);copy.write_bytes(raw);st=path.stat()
                result[name]={'bytes':len(raw),'sha256':sha(copy),'inode':st.st_ino,'links':st.st_nlink,'snapshot':str(copy)}
        return result
    def create(case,label,mode='normal',user=None):
        user=user or case/'user';user.mkdir(exist_ok=True)
        writer=Writer(binary,user,label,mode,env);writers.append(writer);writer.ready=writer.ack('OPEN');return writer
    def check(ok,message):
        if not ok:raise RuntimeError(message)
    try:
        built=subprocess.run(command,capture_output=True,text=True,timeout=60);(out/'compile.log').write_text(built.stdout+built.stderr)
        receipt['compile']={'command':command,'exit':built.returncode,'output':built.stdout+built.stderr}
        check(built.returncode==0,'fixture compile failed')
        env={k:v for k,v in os.environ.items() if not k.startswith(('BEA_','SDL_')) and k not in ('DISPLAY','WAYLAND_DISPLAY')}
        env['HOME']=str(out/'home');(out/'home').mkdir()
        # Forced failure is a condition witness, not a claim that the host naturally refuses rotation.
        case=out/'forced-rename';case.mkdir();a=create(case,'A');a.command('A_BEFORE');before=snapshot(case,'before-B')
        b=create(case,'B','force-rename');after=snapshot(case,'after-B-open')
        check(a.ready['inode']==b.ready['inode'],'forced rename did not retain the same inode')
        check(b'writer=A token=A_BEFORE' in Path(before['onslaught-ce.log']['snapshot']).read_bytes(),'first marker missing before collision')
        check(b'writer=A token=A_BEFORE' not in Path(after['onslaught-ce.log']['snapshot']).read_bytes(),'failed rotation did not truncate')
        b.command('B_BEFORE');snapshot(case,'B-before');astate=a.command('A_AFTER');snapshot(case,'A-after');b.command('B_AFTER');final=snapshot(case,'final')
        apipe=a.finish();bpipe=b.finish();check(b'A_BEFORE' in apipe and b'A_AFTER' in apipe and b'B_BEFORE' in bpipe and b'B_AFTER' in bpipe,'terminal pipe lost markers')
        check(astate['file_error']==0 and any(e.get('event')=='rename' and e.get('forced') and e['result']==-1 for e in b.events),'forced condition not measured')
        receipt['cases'].append({'name':'forced-rename-successful-open','writers':[a.identity,b.identity],'before':before,'after_open':after,'final':final,'reproduced_loss':True})
        # Natural POSIX rotation keeps A in .old until C unlinks that pathname while A remains open.
        case=out/'active-old';case.mkdir();a=create(case,'A');a.command('A_BEFORE');b=create(case,'B');b.command('B_BEFORE');a.command('A_AFTER_B');before=snapshot(case,'before-C')
        check(b'A_AFTER_B' in Path(before['onslaught-ce.old.log']['snapshot']).read_bytes(),'active writer did not follow renamed inode')
        c=create(case,'C');astate=a.command('A_AFTER_C');bstate=b.command('B_AFTER_C');c.command('C_AFTER');after=snapshot(case,'after-C')
        check(astate['links']==0 and astate['file_error']==0,'old active inode was not unlinked while writable')
        check(bstate['links']==1 and bstate['inode']==before['onslaught-ce.log']['inode'],'second writer did not become new old inode')
        check(all(b'A_AFTER_C' not in Path(info['snapshot']).read_bytes() for info in after.values()),'unlinked writer remained reachable by current/old')
        apipe=a.finish();b.finish();c.finish();check(b'A_AFTER_C' in apipe,'terminal marker missing for unlinked writer')
        receipt['cases'].append({'name':'three-writer-active-old-unlink','writers':[a.identity,b.identity,c.identity],'before':before,'after':after,'unlinked_state':astate,'reproduced_loss':True})
        case=out/'sequential';case.mkdir();a=create(case,'A');a.command('A_COMPLETE');a.finish();b=create(case,'B');b.command('B_COMPLETE');snap=snapshot(case,'final');b.finish()
        check(b'A_COMPLETE' in Path(snap['onslaught-ce.old.log']['snapshot']).read_bytes() and b'B_COMPLETE' in Path(snap['onslaught-ce.log']['snapshot']).read_bytes(),'sequential control failed')
        receipt['cases'].append({'name':'sequential-rotation-control','snapshots':snap,'pass':True})
        case=out/'distinct';case.mkdir();a=create(case,'A',user=case/'user-A');b=create(case,'B',user=case/'user-B');c=create(case,'C',user=case/'user-C')
        for w in (a,b,c):w.command('DISTINCT_COMPLETE');w.finish();check(b'DISTINCT_COMPLETE' in (w.folder/'onslaught-ce.log').read_bytes(),'distinct-folder control failed')
        receipt['cases'].append({'name':'three-distinct-user-folders-control','writers':[a.identity,b.identity,c.identity],'pass':True})
        case=out/'cap';case.mkdir();a=create(case,'A');state=a.command('CAP');snap=snapshot(case,'capped');a.finish();raw=Path(snap['onslaught-ce.log']['snapshot']).read_bytes()
        check(len(raw)<=4<<20 and raw.count(b'this log reached 4 MB')==1,'normal log cap failed')
        receipt['cases'].append({'name':'single-owner-cap-control','state':state,'snapshots':snap,'pass':True})
        receipt['pass']=True
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:receipt['error']=str(error)
    finally:
        for writer in writers:writer.cleanup()
        receipt['owned_writers']=[dict(w.identity,exit=w.process.poll(),pipes_preserved=w.finished) for w in writers]
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n');print(('PASS' if receipt['pass'] else 'FAIL')+' '+str(out/'result.json'))
    return 0 if receipt['pass'] else 1

if __name__=='__main__':raise SystemExit(main())
