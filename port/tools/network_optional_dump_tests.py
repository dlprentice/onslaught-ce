#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual post-load diagnostic call and failed-write prelude; authored CPU fixtures, no game launch.

The exact patched caller fragment executes with the actual memory-dump prefix through its refused InitFromMem
return. It does not model successful dump serialization, graphics, networking or retail runtime acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile

import deps
from session_caller_admission import lex, values, one, controls as abort_controls

ROOT = deps.PORT.parent
PATTERN = '''static int lTime = 0;
char buf[256];
sprintf(buf,"Post Load %d",lTime++);
if (!CE_BootRequested()) MEM_MANAGER.DumpMemory(buf);'''


def function(text, marker):
    start = text.index(marker)
    brace = text.index("{", start)
    depth = 1
    for token, begin, end in lex(text[brace + 1:]):
        depth += (token == "{") - (token == "}")
        if not depth:
            return text[start:brace + 1 + end]
    raise ValueError("incomplete source function")


def admit(text):
    abort_controls(text)
    body = function(text, "EQuitType\tCGame::RestartLoopRunLevel")
    ts = lex(body); vs = [t[0] for t in ts]; expected = values(PATTERN)
    at = one(vs, expected, "post-load optional diagnostic")
    if sum((x == "{") - (x == "}") for x in vs[:at]) != 1:
        raise ValueError("optional diagnostic is not at unconditional native load-stage depth")
    start, end = ts[at][1], ts[at + len(expected) - 1][2]
    hashes={position for token,position,end_ in ts if token=="#"}
    conditions=[]
    for match in re.finditer(r"(?m)^[\t ]*#\s*(if|ifdef|ifndef|elif|else|endif)\b[^\n]*", body):
        position=body.index("#",match.start(),match.end())
        if position not in hashes:continue
        if position>=end:break
        directive=match.group(1)
        if directive in ("if","ifdef","ifndef"):conditions.append(position)
        elif directive=="endif":
            if not conditions:raise ValueError("unbalanced diagnostic preprocessing")
            conditions.pop()
        if start<=position<end:raise ValueError("diagnostic fragment has conditional preprocessing")
    if conditions:raise ValueError("diagnostic hidden behind conditional preprocessing")
    loaded = one(vs, values("if (!CE_SessionAfterLoad()) { return (EQuitType)CE_SessionGameAbortLoad(); }"), "loaded guard")
    automatic = one(vs, values("int ce_script = CE_BootAutomaticScript();"), "automatic script")
    if not loaded < at < automatic:
        raise ValueError("diagnostic native stage order changed")
    # There is one active post-load DumpMemory site in this native function; comments do not count.
    if vs.count("DumpMemory") != 1 or vs.count("lTime") != 2:
        raise ValueError("unknown dump or counter site")
    return body[start:end]


def prelude():
    text = (ROOT / "src/MemoryManager.cpp").read_text()
    method = function(text, "void CMemoryManager::DumpMemory(char *tracename)")
    end = method.index("\n\tchar buffer[8192];")
    prefix = method[:end]
    if prefix.count("mDumpingMemory=TRUE;") != 1 or "mDumpingMemory=FALSE" in prefix:
        raise ValueError("actual refused-write dump flag semantics changed")
    if values('if (!mb->InitFromMem(filename,MEMTYPE_DUMPTEMP)) { SAFE_DELETE(mb); return; }') != values(prefix[prefix.index('if (!mb->InitFromMem'):]):
        raise ValueError("actual failed InitFromMem return changed")
    # Success is deliberately outside this prefix fixture and must never silently pass.
    return prefix + '\n std::abort();\n}\n'


def driver(fragment):
    return r'''#include "fixture_guard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
static int requested,world_active,write_attempts,allocations,case_checks;
static char last_label[256],attempted_path[256];
int CE_BootRequested() { return requested; }
int CE_WorldActive() { return world_active; }
#define TRUE 1
#define FALSE 0
#define MEMTYPE_DUMPTEMP 0
void RecordTrace(const char *text){std::snprintf(last_label,sizeof last_label,"%s",text);}
#define TRACE(x) RecordTrace(x)
#define SAFE_DELETE(x) do { delete x; x=nullptr; } while(0)
struct CMEMBUFFER {
 static void *operator new(size_t n,int) { ++allocations;return ::operator new(n); }
 static void operator delete(void *p) { ::operator delete(p); }
 bool InitFromMem(const char *name,int) { ++write_attempts;std::snprintf(attempted_path,sizeof attempted_path,"%s",name);return false; }
};
struct CMemoryManager {
 int mDumpingMemory=0,mTraceNumber=0;
 void DumpMemory(char *tracename);
} MEM_MANAGER;
''' + prelude() + r'''
void NativePostLoad() {
''' + fragment + r'''
}
void Check(bool ok,const char *message){++case_checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(2);}}
int main(int argc,char **argv){
 if(!FixtureGuard()||argc!=2)return 1;
 bool network=std::strcmp(argv[1],"offline")!=0;
 requested=network;world_active=std::strcmp(argv[1],"network-active")==0;
 int initial_flag=std::strcmp(argv[1],"network-already-dumping")==0;MEM_MANAGER.mDumpingMemory=initial_flag;
 // The sink is the actual dump prefix. Its first write failure latches the existing flag.
 NativePostLoad();
 if(network){Check(!write_attempts&&!allocations&&MEM_MANAGER.mDumpingMemory==initial_flag&&!last_label[0],"network skips before any dump allocation/write/flag effect");}
 else{Check(write_attempts==1&&MEM_MANAGER.mDumpingMemory==TRUE,"offline retains actual refused-write flag semantics");Check(!std::strcmp(attempted_path,"MemoryDumps\\dump0.mem"),"offline exact native dump name");Check(!std::strcmp(last_label,"Writing memory dump 'Post Load 0'\n"),"offline original counter label");}
 // Requested failure/retirement remains requested even with World inactive. Later offline probing observes
 // the next lTime without reading private game state; reset only this authored fixture's dump flag.
 requested=0;world_active=0;MEM_MANAGER.mDumpingMemory=0;
 NativePostLoad();Check(write_attempts==(network?1:2),"post-load counter does not suppress later offline diagnostic");
 Check(!std::strcmp(last_label,"Writing memory dump 'Post Load 1'\n"),"unconditional counter increments once even when network skips");
 std::printf("PASS %s %d\n",argv[1],case_checks);return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mirror", type=Path, required=True, help="internal mirror supplies only the two pinned data includes")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    owner = deps.WORK / "network-optional-dump-tests"; owner.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=owner)); print(out, flush=True)
    import sys
    sys.path.insert(0, str(deps.PORT)); import build
    build.PUBLIC=True; build.GAME=args.mirror; build.MIRROR_ROOT=out / "mirror"; build.MIRROR=build.MIRROR_ROOT / "src"
    build.build_mirror()
    text=build.network_view((build.MIRROR / "game.cpp").read_text(), off=False); fragment=admit(text)    # as a build with the network sees it
    records=[]
    def execute(label, command, expected=0):
        r=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=90)
        (out / (label+'.log')).write_text(r.stdout+r.stderr)
        records.append({'label':label,'command':list(map(str,command)),'exit':r.returncode})
        (out/'result.json').write_text(json.dumps({'pass':False,'stage':'checks incomplete or failed','cases':records},indent=2)+'\n')
        if r.returncode!=expected:raise RuntimeError(label+': '+r.stdout+r.stderr)
        return r
    variants={'current':fragment,'unguarded':fragment.replace('if (!CE_BootRequested())',''),
              'inverted':fragment.replace('!CE_BootRequested()','CE_BootRequested()'),
              'world-only':fragment.replace('!CE_BootRequested()','!CE_WorldActive()'),
              'counter-under-guard':'if (!CE_BootRequested()) { '+fragment+' }',
              'counter-no-increment':fragment.replace('lTime++','lTime')}
    for label,source in variants.items():
        path=out/(label+'.cpp');path.write_text(driver(source));exe=out/label
        execute(label+'-build',[*build.TARGETS['linux']['cxx'],'-std=gnu++17','-O1','-I'+str(deps.PORT/'network/tests'),path,'-o',exe])
        for case in ('offline','network-active','network-failed-or-retired','network-already-dumping'):
            bad=(label=='unguarded' and case not in ('offline','network-already-dumping'))or(label=='inverted')or(label=='world-only' and case=='network-failed-or-retired')or(label=='counter-under-guard' and case!='offline')or(label=='counter-no-increment')
            execute(label+'-'+case,[exe,case],2 if bad else 0)
    # Source-level counter and unknown-site controls complement the compiled causal guard controls.
    for label,variant in [('counter-under-guard',text.replace(fragment,'if (!CE_BootRequested()) { '+fragment+' }',1)),
                          ('counter-no-increment',text.replace('"Post Load %d",lTime++','"Post Load %d",lTime',1)),
                          ('extra-dump',text.replace(fragment,fragment+'\nMEM_MANAGER.DumpMemory(buf);',1)),
                          ('preprocessed-out',text.replace(fragment,'#if 0\n'+fragment+'\n#endif',1))]:
        try:admit(variant)
        except ValueError:records.append({'label':label,'refused':True})
        else:raise RuntimeError('source control admitted '+label)
    for target in ('linux','windows'):
        includes=[deps.PORT/'d3dx/include',deps.PORT/'include',*build.TARGETS[target]['includes'],build.MIRROR,build.MIRROR/'MissionScript',deps.PORT/'game']
        cmd=[*build.TARGETS[target]['cxx'],'-std=gnu++14',*build.MSCXX,*build.COMMON,*build.X87,*build.NO_MATH_BUILTINS,'-DCE_VERSION="test"','-include',deps.PORT/'include/port.h',*[f'-I{x}'for x in includes],'-fsyntax-only',build.MIRROR/'game.cpp']
        execute('full-game-caller-'+target,cmd)
    receipt={'pass':True,'cases':records,'fragment':fragment,'caller_sha256':hashlib.sha256(text.encode()).hexdigest(),
             'patch_sha256':hashlib.sha256((deps.PORT/'patches/0084-network-optional-memory-dump.patch').read_bytes()).hexdigest(),
             'limits':__doc__}
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({'pass':True,'checks':len(records),'receipt':str(out/'result.json')}))

if __name__=='__main__':main()
