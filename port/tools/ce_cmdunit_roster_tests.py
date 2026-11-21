#!/usr/bin/env python3
"""Compile the actual pointer-only lifetime callback and native roster scanner."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];COMMANDER=ROOT/'port/game/ce_commander.cpp';ORDERS=ROOT/'port/game/ce_cmdorders.cpp';DRIVER=ROOT/'port/tools/tests/ce_cmdunit_roster_driver.cpp'
def function(source,signature):
    start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
def main():
    source=COMMANDER.read_text();orders=ORDERS.read_text();body='\n'.join([function(source,'Rec *Find('),function(source,'void Scan('),function(source,'void CeCmdSquadGone('),function(orders,'extern "C" void PortCeCmdThingGone(')])
    variants={'control':(body,None),'unwatched_ignored':(body.replace('    CeCmdSquadGone(thing);\n    if (!gWatching) return;','    if (!gWatching) return;\n    CeCmdSquadGone(thing);'),'unwatched departure retires authored roster'),'normal_metadata_reused':(body.replace('        record.alive = false;\n        record.squad = NULL;','        (void)record;'),'unwatched departure retires authored roster'),'neighbour_retired':(body.replace('if (record.squad != departed) continue;','if (false) continue;'),'unrelated roster entry preserved')}
    parent=ROOT/'local-data/ce-cmdunit-roster-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));results={}
    for name,(text,label) in variants.items():
        folder=out/name;folder.mkdir();(folder/'roster-under-test.inc').write_text(text+'\n');binary=folder/'check';r=subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(folder),str(DRIVER),'-o',str(binary)],capture_output=True,text=True);(folder/'compile.log').write_text(r.stdout+r.stderr);record={'compiled':r.returncode==0,'expected_label':label}
        if r.returncode==0:
            r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));output=r.stdout+r.stderr;(folder/'run.log').write_text(output);record.update(exit=r.returncode,sanitizer_error='Sanitizer' in output or 'runtime error:' in output,failure_label=next((l[5:] for l in output.splitlines() if l.startswith('FAIL ')),None),counts=re.findall(r'checks=(\d+) failures=(\d+) skips=(\d+)',output))
        results[name]=record
        if name=='control' and record.get('exit')!=0:break
    passed=results.get('control',{}).get('exit')==0 and len(results)==len(variants) and all(r.get('exit')==1 and not r.get('sanitizer_error') and r.get('failure_label')==r['expected_label'] for n,r in results.items() if n!='control')
    receipt=out/'RESULT.private.json';receipt.write_text(json.dumps({'passed':passed,'scope':'actual authored roster/destruction functions with synthetic native registry; no native engine acceptance','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (COMMANDER,ORDERS,DRIVER,Path(__file__))},'results':results},indent=2)+'\n');print(json.dumps({'passed':passed,'control':results.get('control'),'mutants':len(results)-1,'receipt':str(receipt)}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
