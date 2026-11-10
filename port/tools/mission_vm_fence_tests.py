#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual mission VM/stack/resume ownership controls at controlled instruction/resource boundaries; no game.

Session role admission is covered separately by network_session_tests.py. The VM fixture controls the read-only
role API; it executes the complete actual VM, stack, monitor/reader/list and selected IScript method bodies.
It does not initialize real game actors, parse retail scripts or establish campaign/multiplayer acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile
from network_session_tests import body
ROOT=Path(__file__).resolve().parents[2]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def block(text,signature):return body(text,signature).rstrip()+";\n"
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default='clang++');p.add_argument('--asan',action='store_true');args=p.parse_args()
 resource.setrlimit(resource.RLIMIT_CORE,(0,0));parent=ROOT/'local-data/port/mission-vm-fence-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
 raw=out/'raw';(raw/'MissionScript').mkdir(parents=True);pins={}
 names=['MissionScript/vm.cpp','MissionScript/IScript.cpp','MissionScript/vminternal.h','MissionScript/DataType.h','MissionScript/ScriptObjectCode.cpp','MissionScript/asminstruction.h','SPtrSet.h','SPtrSet.cpp','Monitor.h','Monitor.cpp','Listener.h','activereader.h','activereader.cpp','Event.h','event.cpp','scheduledevent.h','scheduledevent.cpp']
 for name in names:
  src=ROOT/'src'/name;dest=raw/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest);pins[name]=digest(src)
 patch=ROOT/'port/patches/0075-pending-replica-mission-vm.patch';selected=''.join(part for part in re.split(r'(?=^--- a/)',patch.read_text(),flags=re.M) if any(part.startswith('--- a/'+name+'\n') for name in ('MissionScript/vm.cpp','MissionScript/IScript.cpp')));r=subprocess.run(['patch','--batch','--fuzz=0','-p1'],cwd=raw,input=selected,capture_output=True,text=True,timeout=10);(out/'patch.log').write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 vm=(raw/'MissionScript/vm.cpp').read_text();script=(raw/'MissionScript/IScript.cpp').read_text();types=(raw/'MissionScript/vminternal.h').read_text();data=(raw/'MissionScript/DataType.h').read_text();stack=(raw/'MissionScript/ScriptObjectCode.cpp').read_text()
 # All actual external VM entry sites are retained as a review receipt. Current Run calls are internal to these3.
 callers={}
 for src in (ROOT/'src').rglob('*.cpp'):
  lines=[{'line':i,'text':line.strip()} for i,line in enumerate(src.read_text().splitlines(),1) if re.search(r'\b(?:MISSION_SCRIPT_VM|vm|mAnimVM|mWaypointVM)\s*(?:\.|->)\s*Run\s*\(',line)]
  if lines:callers[str(src.relative_to(ROOT))]=lines
 if callers:raise RuntimeError('unreviewed external Run entry: '+repr(callers))
 original=(ROOT/'src/MissionScript/vm.cpp').read_text();internal={name:body(original,'void\tCVM::'+name+'(').count('Run();') for name in ('CallEvent','CallFunction','GotoInstruction')};assert sum(internal.values())==5
 def emit(folder,name,text):(folder/name).write_text(text)
 def prepare(folder,vm_text,script_text):
  folder.mkdir(exist_ok=True)
  for name in ('SPtrSet.h','Monitor.h','Listener.h','activereader.h','Event.h','scheduledevent.h'):
   text=(raw/name).read_text().replace('"ActiveReader.h"','"activereader.h"');emit(folder,name,text)
  for name,src in (('lists.inc','SPtrSet.cpp'),('monitor.inc','Monitor.cpp'),('reader.inc','activereader.cpp')):emit(folder,name,re.sub(r'^#include[^\n]*\n','',(raw/src).read_text(),flags=re.M))
  emit(folder,'events.inc',body((raw/'event.cpp').read_text(),'CEvent::CEvent(')+'\n'+re.sub(r'^#include[^\n]*\n','',(raw/'scheduledevent.cpp').read_text(),flags=re.M))
  emit(folder,'datatype.inc',block(data,'enum EDataType\n')+block(data,'class CDataType\n'))
  emit(folder,'stack_type.inc',re.search(r'^#define MAX_STACK_SIZE[^\n]+',types,re.M).group(0)+'\n'+block(types,'class CDataStack\n'));emit(folder,'vm_type.inc',block(types,'class CVM : public CMonitor'))
  emit(folder,'opcode.inc',block((raw/'MissionScript/asminstruction.h').read_text(),'enum EOpcode\n'))
  emit(folder,'stack.inc','\n'.join(body(stack,signature) for signature in ('CDataStack\t&CDataStack::operator=','void\tCDataStack::Clear(','void\tCDataStack::Push(','CDataType\t*CDataStack::Pop(','void\tCDataStack::RemoveTop(','CDataType\t*CDataStack::GetTop(')))
  emit(folder,'vm.inc','#define MAX_INSTRUCTIONS_PER_RUN 10000\nCVM MISSION_SCRIPT_VM;\n'+body(vm_text,'CVM\t&CVM::operator=')+'\n'+'\n'.join(body(vm_text,'void\tCVM::'+n+'(') for n in ('Clean','CallEvent','CallFunction','GotoInstruction','Run')))
  emit(folder,'script.inc','\n'.join(body(script_text,sig) for sig in ('IScript::IScript(','IScript::~IScript(','void\tIScript::Pause(','void\tIScript::FinishedPlayingAnim(','void\tIScript::UpdateWaypointFollowing(','void\tIScript::HandleEvent(')))
 prepare(out,vm,script);baseline=out/'baseline';prepare(baseline,original,(ROOT/'src/MissionScript/IScript.cpp').read_text())
 env=dict(os.environ,TMPDIR=str(out),ASAN_OPTIONS='detect_leaks=1:abort_on_error=1:disable_coredump=1');driver=ROOT/'port/tools/tests/mission_vm_fence_driver.cpp';commands=[]
 def compile_at(folder):
  command=[args.compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-deprecated-copy','-Wno-parentheses','-Wno-ignored-qualifiers','-I'+str(folder),str(driver),'-o',str(folder/'driver')]
  if args.asan:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
  commands.append(command);r=subprocess.run(command,capture_output=True,text=True,env=env,timeout=30);(folder/'build.log').write_text(r.stdout+r.stderr);return r.returncode
 receipt={'scope':__doc__,'source_sha256':pins,'patch_sha256':digest(patch),'session_source_sha256':digest(ROOT/'port/game/ce_network_session.cpp'),'session_header_sha256':digest(ROOT/'port/game/ce_network_session.h'),'driver_sha256':digest(driver),'runner_sha256':digest(Path(__file__)),'external_run_callers':callers,'internal_run_sites':internal,'commands':commands,'cases':[]}
 def run(folder,case,expected=0,reason=''):
  r=subprocess.run([str(folder/'driver'),case],capture_output=True,text=True,env=env,timeout=6);(folder/(case+'.log')).write_text(r.stdout+r.stderr);passed=r.returncode==expected and (reason in r.stderr if reason else True);observed=json.loads(r.stdout) if r.returncode==0 else {};receipt['cases'].append({'variant':folder.name,'case':case,'exit':r.returncode,'expected':expected,'reason':reason,'observed':observed,'stderr':r.stderr,'passed':passed});print(folder.name,case,'PASS' if passed else 'FAIL',flush=True);return observed
 status=compile_at(out);receipt['compile_exit']=status
 if not status:
  positives=('blocked-event','blocked-function','blocked-goto','stack-move','blocked-animation','blocked-message','blocked-waypoint','blocked-timer');normal=('normal-event','normal-function','normal-goto','normal-animation','normal-message','normal-waypoint','normal-timer')
  expected={case:run(out,case) for case in normal}
  for case in positives:run(out,case)
  if compile_at(baseline):raise RuntimeError('baseline must compile')
  for case in normal:assert run(baseline,case)==expected[case],case+' ordinary behavior changed'
  for case in ('blocked-event','blocked-function','blocked-goto'):run(baseline,case,2,'blocked entry precedes VM and code mutation')
  for case in ('blocked-animation','blocked-message','blocked-waypoint'):run(baseline,case,2,'blocked parent retains sole saved ownership until native destructor')
  mutations=(('no-argument-delete','vm.inc','for (int i = 0; i < num_args; ++i) delete args[i];','/* negative: transferred args leaked */','blocked-event','blocked entry destroys transferred and existing owned values exactly once',1),
   ('no-function-argument-delete','vm.inc','for (int i = 0; i < num_args; ++i) delete args[i];','/* negative: transferred args leaked */','blocked-function','blocked entry destroys transferred and existing owned values exactly once',2),
   ('no-goto-clean','vm.inc','if (CE_SessionMissionVMBlocked()) { Clean(); return; }','if (CE_SessionMissionVMBlocked()) return;','stack-move','fenced moved continuation destroys only new owner values',1),
   ('missing-animation-parent','script.inc','if (CE_SessionMissionVMBlocked()) return; // owned continuations remain with the native destructor','/* negative: parent executes */','blocked-animation','blocked parent retains sole saved ownership until native destructor',1),
   ('missing-event-parent','script.inc','if (CE_SessionMissionVMBlocked()) return; // owned continuations remain with the native destructor','/* negative: parent executes */','blocked-waypoint','blocked parent retains sole saved ownership until native destructor',2))
  for label,name,old,new,case,reason,ordinal in mutations:
   folder=out/label;folder.mkdir()
   for path in out.iterdir():
    if path.suffix in ('.h','.inc'):shutil.copyfile(path,folder/path.name)
   path=folder/name;text=path.read_text();at=[m.start() for m in re.finditer(re.escape(old),text)];assert len(at)>=ordinal;index=at[ordinal-1];path.write_text(text[:index]+new+text[index+len(old):])
   if compile_at(folder):raise RuntimeError('mutant must compile: '+label)
   run(folder,case,2,reason)
  # A Run-only guard is too late: callers already replace mCode/PC and mark init complete.
  folder=out/'run-only';prepare(folder,original,(ROOT/'src/MissionScript/IScript.cpp').read_text())
  path=folder/'vm.inc';text=path.read_text();signature=body(original,'void\tCVM::Run(');guarded=signature.replace('{','{\n if (CE_SessionMissionVMBlocked()) { Clean(); return; }',1);assert text.count(signature)==1;path.write_text(text.replace(signature,guarded))
  if compile_at(folder):raise RuntimeError('Run-only mutant must compile')
  run(folder,'blocked-event',2,'blocked entry precedes VM and code mutation')
 (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n');print(out/'result.json');return 0 if not status and all(c['passed'] for c in receipt['cases']) else 1
if __name__=='__main__':raise SystemExit(main())
