#!/usr/bin/env python3
"""Causal selected-control handoff checks; possession suffix is not simulated."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'port/game/ce_playunit.cpp';DRIVER=ROOT/'port/tools/tests/ce_cmdunit_take_driver.cpp'
def function(source,name):
    start=source.index('bool '+name+'(');brace=source.index('{',start);depth=1;end=brace+1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
def main():
    parent=ROOT/'local-data/ce-cmdunit-take-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    source=SOURCE.read_text();take=function(source,'Take');marker='\tgPreviousSquad = u->GetSquad();';end=take.index(marker)+len(marker)
    prefix=take[:end]+'\n    return true;\n}\n';selected=function(source,'CePlayUnitTakeSelection')
    release='\tCeCmdUnitOrderRelease(u);\n'
    failure='    if (!Take(player, engine, controller, unit, kind)) {\n        if (reason) *reason = "Unit control is no longer available";\n        return false;\n    }'
    assert failure in selected and prefix.count(release)==1
    cases={
      'control':(prefix,selected,None),
      'false_success':(prefix,selected.replace(failure,'    Take(player, engine, controller, unit, kind);'),'accepted selection eligibility cannot hide Take refusal'),
      'premature_release':(prefix.replace(release,'').replace('\tif (kind ==',release+'\tif (kind ==',1),selected,'Take refusal preserves current individual owner'),
      'missing_release':(prefix.replace(release,''),selected,'accepted Take releases selected individual ownership once'),
      'late_release':(prefix.replace(release,'').replace(marker,marker+'\n'+release.rstrip()),selected,'ownership releases before handback captures'),
    }
    results={}
    for name,(admission,wrapper,label) in cases.items():
        folder=out/name;folder.mkdir();(folder/'take-under-test.inc').write_text(admission+'\n'+wrapper+'\n')
        command=[shutil.which('c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(folder),str(DRIVER),'-o',str(folder/'check')]
        built=subprocess.run(command,capture_output=True,text=True);(folder/'compile.log').write_text(built.stdout+built.stderr);record={'compiled':built.returncode==0,'compile_exit':built.returncode,'expected_label':label}
        if built.returncode==0:
            ran=subprocess.run([str(folder/'check')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));output=ran.stdout+ran.stderr;(folder/'run.log').write_text(output)
            record.update(exit=ran.returncode,sanitizer_error='Sanitizer' in output or 'runtime error:' in output,failure_label=next((l[5:] for l in output.splitlines() if l.startswith('FAIL ')),None),counts=re.findall(r'checks=(\d+) failures=(\d+) skips=(\d+)',output))
        results[name]=record
        if name=='control' and record.get('exit')!=0:break
    passed=results.get('control',{}).get('exit')==0 and len(results)==len(cases) and all(r.get('exit')==1 and not r.get('sanitizer_error') and r.get('failure_label')==r['expected_label'] for n,r in results.items() if n!='control')
    receipt=out/'RESULT.private.json';receipt.write_text(json.dumps({'passed':passed,'scope':'actual selected wrapper and Take admission/ownership capture prefix only; not full possession or native acceptance','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (SOURCE,DRIVER,Path(__file__))},'results':results},indent=2)+'\n')
    print(json.dumps({'passed':passed,'control':results.get('control'),'mutants':len(results)-1,'receipt':str(receipt)}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
