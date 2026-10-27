#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual launcher log lease callers, synchronized owned processes and authored files only.

No game/profile/display/network/VM execution. Native Windows is crosslinked, not executed. Baseline loss witnesses
remain separate. Receipts preserve pipes, source pins, slot states and bounded log hashes in ignored local-data.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
from log_concurrency_tests import Writer,sha,HERE,PORT,ROOT,deps

NAMES=('onslaught-ce.log','onslaught-ce.old.log','onslaught-ce.concurrent-1.log','onslaught-ce.concurrent-2.log')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source-dir',type=Path,default=HERE.parent);args=parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0));parent=ROOT/'local-data/port/log-lease-tests';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));selected=out/'selected';selected.mkdir();writers=[]
    r={'passed':False,'inputs':{},'cases':[],'limits':['Host authored writer processes, not game/VM/user-profile execution.',
       'Only selected filesystem/stdio failure boundaries are injected; no native Windows runtime proof.','Leases coordinate cooperating launchers; no game single-instance or updater serialization.']}
    for name in ('ce_launch.c','ce_launch_rules.h','ce_report.h','ce_update_v2.h','sha256.c','sha256.h'):
        shutil.copyfile(args.source_dir/name,selected/name);r['inputs']['port/updater/'+name]=sha(selected/name)
    shutil.copyfile(HERE/'log_lease_driver.c',selected/'driver.c');r['inputs']['driver']=sha(selected/'driver.c');r['inputs']['generator']=sha(Path(__file__));r['inputs']['control_support']=sha(HERE/'log_concurrency_tests.py')
    mc=deps.SRC/'monocypher-4.0.2/src';binary=out/'writer'
    common=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-ffunction-sections','-fdata-sections',
            f'-I{mc}',f'-I{mc/"optional"}',f'-I{selected}','-DCE_VERSION="fixture"','-DCE_COMMIT="log-lease"']
    command=[str(deps.DATA/'llvm/bin/clang'),'--target=i686-pc-linux-gnu',*deps.sysroot_flags(),*common,f'-I{deps.PREFIX/"include"}',
             f'-DCE_LAUNCH_SOURCE="{selected/"ce_launch.c"}"',str(selected/'driver.c'),'-Wl,--gc-sections',f'-L{deps.PREFIX/"lib"}',
             '-lSDL3',f'-Wl,-rpath,{deps.PREFIX/"lib"}','-o',str(binary),*deps.sysroot_link_flags()]
    env={k:v for k,v in os.environ.items() if not k.startswith(('BEA_','SDL_')) and k not in ('DISPLAY','WAYLAND_DISPLAY')};env['HOME']=str(out/'home');(out/'home').mkdir()
    def check(ok,message):
        if not ok:raise RuntimeError(message)
    def folder(name):p=out/name;p.mkdir();(p/'user').mkdir();return p
    def start(case,label,mode='normal',user=None):
        w=Writer(binary,user or case/'user',label,mode,env);writers.append(w);w.ready=w.ack('OPEN');return w
    def logs(user):return {n:{'sha256':sha(user/n),'bytes':(user/n).stat().st_size} for n in NAMES if (user/n).is_file() and not (user/n).is_symlink()}
    def note(name,case):r['cases'].append({'case':name,'logs':logs(case/'user'),'passed':True})
    try:
        c=subprocess.run(command,capture_output=True,text=True,timeout=60);r['compile']={'command':command,'exit':c.returncode,'output':c.stdout+c.stderr};check(c.returncode==0,'driver compile failed')
        case=folder('four-shared');live=[start(case,x) for x in 'ABCD'];check([w.ready['slot'] for w in live]==[0,1,2,-1],'four-writer slot admission')
        for w in live:w.command(w.label+'_BEFORE')
        for w in live:w.command(w.label+'_AFTER')
        check(len({w.ready['inode'] for w in live[:3]})==3 and all(w.ready['lease_cloexec'] for w in live),'leases/data shared or inherited')
        for w,name in zip(live[:3],(NAMES[0],NAMES[2],NAMES[3])):check((w.label+'_BEFORE').encode() in (case/'user'/name).read_bytes() and (w.label+'_AFTER').encode() in (case/'user'/name).read_bytes(),'active log marker lost')
        for w in live:pipe=w.finish();check((w.label+'_AFTER').encode() in pipe,'terminal output lost')
        check(b'no safe log slot' in (case/'D.stderr').read_bytes(),'fourth writer silent about terminal fallback');note('two-three-four-writers',case)
        case=folder('crash-release');a=start(case,'A');a.command('A_BEFORE_KILL');b=start(case,'B');lease=(case/'user'/(NAMES[0]+'.lock')).stat().st_ino
        check(Path(f'/proc/{a.process.pid}/cmdline').read_bytes().split(b'\0')[0]==os.fsencode(binary),'owned kill identity changed')
        a.process.kill();outpipe,err=a.process.communicate(timeout=5);a.finished=True;(case/'A.stderr').write_bytes(err);(case/'A.stdout.jsonl').write_text(''.join(json.dumps(e)+'\n' for e in a.events));check(a.process.returncode==-9,'owned abrupt exit failed')
        c=start(case,'C');check(c.ready['slot']==0 and (case/'user'/(NAMES[0]+'.lock')).stat().st_ino==lease,'crash left stale lease or replaced lease path')
        b.command('B_SURVIVES');c.command('C_PRIMARY');b.finish();c.finish();check(b'A_BEFORE_KILL' in (case/'user'/NAMES[1]).read_bytes(),'crash log lost during closed rotation');note('kernel-crash-release',case)
        case=folder('forced-rotation');a=start(case,'A');a.command('A_COMPLETE');a.finish();before=sha(case/'user'/NAMES[0]);b=start(case,'B','force-rename');check(b.ready['slot']==1 and sha(case/'user'/NAMES[0])==before,'failed rotation truncated primary');b.command('B_FALLBACK');b.finish();note('rotation-failure-fallback',case)
        case=folder('continue');a=start(case,'A');a.command('A_COMPLETE');a.finish();before=(case/'user'/NAMES[0]).read_bytes();b=start(case,'B','continue');b.command('B_CONTINUED');b.finish();check((case/'user'/NAMES[0]).read_bytes().startswith(before),'continuation replaced previous bytes');note('guarded-primary-append',case)
        case=folder('continue-busy');a=start(case,'A');a.command('A_ACTIVE');b=start(case,'B','continue');check(b.ready['slot']==1,'busy continuation appended active primary');b.command('B_NEW_FALLBACK');a.finish();b.finish();note('busy-continuation-fresh-fallback',case)
        case=folder('cap-continue');a=start(case,'A');a.command('CAP');a.finish();before=sha(case/'user'/NAMES[0]);b=start(case,'B','continue');b.command('AFTER_CAP');b.finish();check((case/'user'/NAMES[0]).stat().st_size<=4<<20 and sha(case/'user'/NAMES[0])==before,'continuation exceeded cap');note('cap-preserved-on-continuation',case)
        case=folder('distinct');a=start(case,'A');other=case/'user-B';other.mkdir();b=start(case,'B',user=other);check(a.ready['slot']==b.ready['slot']==0,'distinct user folder lease interfered');a.command('A_DISTINCT');b.command('B_DISTINCT');a.finish();b.finish();note('distinct-user-folders',case)
        case=folder('foreign-files');payload=b'unselected foreign bytes\x00\xff';target=case/'foreign';target.write_bytes(payload)
        (case/'user'/NAMES[0]).write_bytes(payload);(case/'user'/NAMES[2]).write_bytes(payload);(case/'user'/NAMES[3]).symlink_to(target)
        a=start(case,'A');check(a.ready['slot']==-1,'foreign slot unexpectedly admitted');a.command('TERMINAL_ONLY');a.finish();check(target.read_bytes()==payload and (case/'user'/NAMES[0]).read_bytes()==payload and (case/'user'/NAMES[2]).read_bytes()==payload and (case/'user'/NAMES[3]).is_symlink(),'foreign/link file changed');note('foreign-and-link-data-preserved',case)
        case=folder('lease-link');target=case/'foreign';target.write_bytes(payload);(case/'user'/(NAMES[0]+'.lock')).symlink_to(target);a=start(case,'A');check(a.ready['slot']==-1,'link lease admitted');a.finish();check(target.read_bytes()==payload and not any((case/'user'/n).exists() for n in NAMES),'lease link touched data');note('lease-link-refusal',case)
        case=folder('unsupported');a=start(case,'A','unsupported-lease');check(a.ready['slot']==-1,'unsupported lock admitted');a.finish();check(not any((case/'user'/n).exists() for n in NAMES),'unsupported lease wrote log');note('unsupported-lease-terminal-only',case)
        case=folder('null-close-release');(case/'user'/NAMES[0]).write_bytes(payload);(case/'user'/NAMES[2]).write_bytes(payload);(case/'user'/NAMES[3]).write_bytes(payload)
        a=start(case,'A');check(not a.ready['persisted'],'foreign-file control unexpectedly opened data');check(a.command('LEASE_PROBE')['slot']==0,'open failure leaked primary lease');a.finish()
        b=start(case,'B');check(b.command('LEASE_PROBE')['slot']==0,'NULL file close failed to release lease');b.finish();check(all((case/'user'/name).read_bytes()==payload for name in (NAMES[0],NAMES[2],NAMES[3])),'NULL-close test changed foreign bytes');note('null-file-close-releases-owned-lease',case)
        case=folder('fallback-reuse');a=start(case,'A');b=start(case,'B');b.command('B_COMPLETE');b.finish();c=start(case,'C');check(c.ready['slot']==1,'closed app fallback was not reused');a.command('A_ACTIVE');c.command('C_REUSED');a.finish();c.finish();check(b'C_REUSED' in (case/'user'/NAMES[2]).read_bytes(),'fallback reuse missing new marker');note('closed-app-fallback-reuse',case)
        case=folder('create-race');a=start(case,'A','race-create');check(a.ready['slot']==1 and (case/'user'/NAMES[0]).read_bytes()==b'raced foreign sentinel','exclusive create overwrote race');a.finish();note('exclusive-create-race',case)
        case=folder('flush-failure');a=start(case,'A');state=a.command('FAIL_FLUSH');check(state['slot']==-1 and not state['persisted'],'flush failure retained lease');b=start(case,'B');check(b.ready['slot']==0,'flush failure blocked next primary');a.command('TERMINAL_CONTINUES');a.finish();b.finish();check(b'write/flush failed' in (case/'A.stderr').read_bytes(),'flush error silent');note('write-failure-close-before-release',case)
        case=folder('close-failure');a=start(case,'A','close-failure');a.finish();b=start(case,'B');check(b.ready['slot']==0,'failed close retained lease');b.finish();check(b'log close failed' in (case/'A.stderr').read_bytes(),'close failure silent');note('close-failure-releases-lease',case)
        case=folder('metadata-report');a=start(case,'A');b=start(case,'B');c=start(case,'C');(case/'user'/'keep.dat').write_bytes(b'fixture')
        a.command('METADATA');a.command('REPORT');a.finish();b.finish();c.finish();meta=json.loads((case/'A.metadata.json').read_text());check(meta=={'keep.dat':'Zml4dHVyZQ=='},'logging artifacts entered recording start files')
        text=(case/'A.stderr').read_text();url=next(line[11:] for line in text.splitlines() if line.startswith('report_url='));body=urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)['body'][0]
        check(all(name in body for name in NAMES) and 'same folder' in body,'report URL omitted current/old/concurrent log hints');note('recording-artifact-exclusion-and-report-url',case)
        for name,mode in (('current','normal'),('old','normal'),('fallback','normal'),('continue','continue'),('lease','normal')):
            case=folder('hardlink-'+name);outside=case/'outside';outside.write_bytes(b'Onslaught CE vfixture, build fixture (linux-i686)\nORIGINAL OUTSIDE\x00\xff');initial=outside.read_bytes()
            if name=='old':(case/'user'/NAMES[0]).write_bytes(b'Onslaught CE vfixture\ncurrent sentinel')
            target=NAMES[1] if name=='old' else NAMES[2] if name=='fallback' else NAMES[0]+'.lock' if name=='lease' else NAMES[0]
            os.link(outside,case/'user'/target);inode=outside.stat().st_ino
            if name=='fallback':(case/'user'/NAMES[0]).write_bytes(b'foreign primary sentinel')
            a=start(case,'A',mode);a.command('MUST_NOT_TOUCH_OUTSIDE');a.finish()
            check(outside.read_bytes()==initial and (case/'user'/target).stat().st_ino==inode,'hardlinked '+name+' was modified/replaced')
            check(a.ready['slot']==(-1 if name=='lease' else 2 if name=='fallback' else 1),'hardlinked '+name+' was admitted')
            note('hardlinked-'+name+'-preserved',case)
        for mode in ('stat-error','stat-zero'):
            case=folder(mode);a=start(case,'A',mode);check(a.ready['slot']==-1,'failed native metadata query admitted');a.finish();check(not any((case/'user'/n).exists() for n in NAMES),'query failure wrote log');note('native-'+mode+'-refusal',case)
        r['links']=[]
        for target in ('linux','windows'):
            prefix=deps.PREFIX if target=='linux' else deps.PREFIX_WIN
            cc=[str(deps.DATA/'llvm/bin/clang'),'--target=i686-pc-linux-gnu',*deps.sysroot_flags()] if target=='linux' else [str(deps.DATA/'llvm-mingw/bin/i686-w64-mingw32-clang')]
            exe=out/('launcher-'+target+('.exe' if target=='windows' else ''));sources=[selected/'ce_launch.c',selected/'sha256.c',mc/'monocypher.c',mc/'optional/monocypher-ed25519.c']
            cmd=cc+['-std=gnu11','-O2','-Wall',f'-I{prefix/"include"}',f'-I{mc}',f'-I{mc/"optional"}',f'-I{selected}',*map(str,sources),f'-L{prefix/"lib"}','-lSDL3','-o',str(exe)]
            cmd+=deps.sysroot_link_flags()+['-Wl,-rpath,'+str(prefix/'lib')] if target=='linux' else ['-mwindows','-lz','-lwinhttp','-lbcrypt','-ladvapi32','-lshell32','-static-libgcc']
            p=subprocess.run(cmd,capture_output=True,text=True,timeout=60);r['links'].append({'target':target,'command':cmd,'exit':p.returncode,'output':p.stdout+p.stderr,'sha256':sha(exe) if exe.exists() else None});check(p.returncode==0,'full launcher crosslink failed: '+target)
        case=folder('header-flush-failure');a=start(case,'A','header-flush-failure');check(not a.ready['persisted'] and a.ready['slot']==-1 and not a.ready['holding'] and a.ready['hold_bytes']==0 and not a.ready['hold_present'],'failed header retained output holding');a.finish();check(b'write/flush failed' in (case/'A.stderr').read_bytes(),'header failure silent');note('first-header-flush-cleans-hold',case)
        production=(selected/'ce_launch.c').read_text();bodies=[]
        for name in ('log_native_error','log_invalid_file','log_missing','log_path','log_release_lease','log_acquire_lease','log_open_file'):
            found=re.findall(r'^static [^\n]*\b'+re.escape(name)+r'\([^\n]*\)\n\{\n.*?^\}',production,re.M|re.S)
            check(len(found)==1,'Windows helper body extraction changed: '+name);bodies+=found
        functions=out/'windows-functions.inc';functions.write_text('\n'.join(bodies)+'\n')
        shutil.copyfile(HERE/'log_lease_windows_boundary.c',selected/'windows-boundary.c');r['inputs']['windows_boundary']=sha(selected/'windows-boundary.c');r['inputs']['windows_functions']=sha(functions)
        model=out/'windows-error-model';cmd=[str(deps.DATA/'llvm/bin/clang'),'--target=i686-pc-linux-gnu',*deps.sysroot_flags(),'-std=gnu11','-O1','-Wall','-Wextra','-Wno-unused-function',f'-DLOG_WINDOWS_FUNCTIONS="{functions}"',str(selected/'windows-boundary.c'),'-o',str(model),*deps.sysroot_link_flags()]
        p=subprocess.run(cmd,capture_output=True,text=True,timeout=30);check(p.returncode==0,'Windows error model compile: '+p.stderr)
        p=subprocess.run([str(model)],capture_output=True,text=True,timeout=5);r['windows_error_model']={'compile_command':cmd,'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr,'limit':'Nine actual Windows helper error/control paths with mocked WinAPI on host; realSDK fulllauncher crosslink above, no nativeWindows execution.'};check(p.returncode==0,'Windows error model refusal/control failed')
        r['passed']=True
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:r['error']=str(error)
    finally:
        for w in writers:w.cleanup()
        r['owned_writers']=[dict(w.identity,exit=w.process.poll(),pipes_preserved=w.finished) for w in writers]
    (out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(('PASS' if r['passed'] else 'FAIL')+' '+str(out/'result.json'))
    if 'error' in r:print(r['error'])
    return 0 if r['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
