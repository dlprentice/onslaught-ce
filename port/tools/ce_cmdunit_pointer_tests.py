#!/usr/bin/env python3
"""Real strategic pointer bodies over the controller, with synthetic platform facts."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,tempfile
from ce_cmdunit_consumer_tests import function
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'port/game/ce_cmdmap.cpp';DRIVER=ROOT/'port/tools/tests/ce_cmdunit_pointer_driver.cpp'
def main():
    parent=ROOT/'local-data/ce-cmdunit-pointer-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));source=SOURCE.read_text()
    dispatch='\n'.join(function(source,n) for n in ['UnitRequestHas','UnitWholeSquad'])+'\nstruct UnitTakeContext { const char* reason; };\n'+'\n'.join(function(source,n) for n in ['UnitCanTake','UnitTake','UnitFocusPosition','DispatchUnitRequest'])
    pointer='\n'.join(function(source,n) for n in ['UnitPanelAt','DrainUnitRequest','UnitTargetAt','ResetStrategicGesture','UnitPointerSeed','UnitPointerObserve','StrategicPointer'])
    changes={
      'held_down_ignored':('!gUnitPointerFence.Press(key)','(!gUnitPointerFence.Press(key) && false)','held middle cannot begin pan on entry'),
      'focus_cancel_ignored':('gUnitUi.Cancel(); gUnitUi.SuppressUntilRelease();','/* omitted focus cancellation */','focus loss cancels owned controller drag'),
      'positive_release_ignored':('gUnitUi.PointerUp(float(MOUSE_X), float(MOUSE_Y), UnitControl::ClickModifiers{});','/* omitted observed release */','sampled release clears suppression without a release event'),
      'wheel_not_reset':('gStrategicWheel = 0;\n    gArmed = false;','/* wheel retained */\n    gArmed = false;','mode boundary clears wheel drag target and focus'),
    }
    variants={'control':(pointer,None)}
    for name,(before,after,label) in changes.items():assert before in pointer,name;variants[name]=(pointer.replace(before,after),label)
    results={}
    for name,(body,label) in variants.items():
        folder=out/name;folder.mkdir();(folder/'consumer-under-test.inc').write_text(dispatch);(folder/'pointer-under-test.inc').write_text(body)
        cmd=[shutil.which('c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(folder),'-I'+str(ROOT/'port/game'),str(DRIVER),str(ROOT/'port/game/ce_command_selection.cpp'),str(ROOT/'port/game/ce_command_panel.cpp'),'-o',str(folder/'check')]
        built=subprocess.run(cmd,capture_output=True,text=True);(folder/'compile.log').write_text(built.stdout+built.stderr);r={'compiled':built.returncode==0,'compile_exit':built.returncode,'expected_label':label}
        if built.returncode==0:
            ran=subprocess.run([str(folder/'check')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));output=ran.stdout+ran.stderr;(folder/'run.log').write_text(output);r.update(exit=ran.returncode,sanitizer_error='Sanitizer' in output or 'runtime error:' in output,failure_label=next((l[5:] for l in output.splitlines() if l.startswith('FAIL ')),None),counts=re.findall(r'checks=(\d+) failures=(\d+) skips=(\d+)',output))
        results[name]=r
        if name=='control' and r.get('exit')!=0:break
    passed=results.get('control',{}).get('exit')==0 and len(results)==len(variants) and all(r.get('exit')==1 and not r.get('sanitizer_error') and r.get('failure_label')==r['expected_label'] for n,r in results.items() if n!='control')
    inputs=[SOURCE,DRIVER,Path(__file__),ROOT/'port/tools/ce_cmdunit_consumer_tests.py',ROOT/'port/tools/tests/ce_cmdunit_consumer_driver.cpp']+[ROOT/'port/game'/n for n in ['ce_command_selection.cpp','ce_command_selection.h','ce_command_panel.cpp','ce_command_panel.h','ce_cmdpointer.h']]
    receipt=out/'RESULT.private.json';receipt.write_text(json.dumps({'passed':passed,'scope':'actual authored pointer/dispatch with real controller and synthetic platform/native callbacks; no native physical acceptance','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'results':results},indent=2)+'\n');print(json.dumps({'passed':passed,'control':results.get('control'),'mutants':len(results)-1,'receipt':str(receipt)}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
