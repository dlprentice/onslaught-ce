#!/usr/bin/env python3
"""Execute actual unit dispatch bodies over real selection and shaped callbacks."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'port/game/ce_cmdmap.cpp';DRIVER=ROOT/'port/tools/tests/ce_cmdunit_consumer_driver.cpp'
def function(source,name):
    start=re.search(r'^(?:bool|void) '+name+r'\([^;{}]*\)\s*\{',source,re.M).start();brace=source.index('{',start);depth=1;end=brace+1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
def main():
    parent=ROOT/'local-data/ce-cmdunit-consumer-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));source=SOURCE.read_text()
    body='\n'.join(function(source,n) for n in ['UnitRequestHas','UnitWholeSquad'])+'\nstruct UnitTakeContext { const char* reason; };\n'+'\n'.join(function(source,n) for n in ['UnitCanTake','UnitTake','UnitFocusPosition','DispatchUnitRequest'])
    changes={
      'solo_gate':('!CeCmdUnitOrdersEligible()','false','ordinary-solo gate refuses before refresh'),
      'context_gate':('request.context_generation != gUnitContextGeneration','false','old input context refused'),
      'world_gate':('request.world != CeCmdUnitWorld()','false','old world request refused before refresh'),
      'finite_point':('if (request.has_point &&','if (false && request.has_point &&','nonfinite target refused'),
      'incomplete_frame':('if (!gUnitSnapshotComplete || !gUnitUi.FrameUsable()','if (false || false','incomplete native snapshot refuses request'),
      'unusable_frame':('!gUnitUi.FrameUsable()','false','unusable view frame refuses request'),
      'subset_orders_squad':('if (!unit.commandable || !UnitRequestHas(request, unit.key))','if (!unit.commandable)','partial move keeps only intended key'),
      'cohort_refresh':('Refresh(); RefreshUnitFrame();\n            if','/* no refresh */\n            if','callback replacement cannot receive second cohort order'),
      'callback_camera':('if (!StrategicCanAct() || !CeCmdUnitOrdersEligible() || request.world != CeCmdUnitWorld() ||\n            request.context_generation','if (false || !CeCmdUnitOrdersEligible() || request.world != CeCmdUnitWorld() ||\n            request.context_generation','individual callback rechecks current camera authority'),
      'take_before_close':('Close();\n        if (!PortCeCmdMapOpen() && CeCmdUnitVisit(request.keys[0], UnitTake, &take)) return;','if (CeCmdUnitVisit(request.keys[0], UnitTake, &take)) { Close(); return; }','failed close cannot take control'),
      'take_ignores_open':('!PortCeCmdMapOpen() && CeCmdUnitVisit','CeCmdUnitVisit','failed close cannot take control'),
      'take_multiple':('request.key_count != 1 || request.selected_count != 1','false','Take requires exactly one intended selected unit'),
      'take_blocked_selection':('request.key_count != 1 || request.selected_count != 1','request.key_count != 1','blocked second selection cannot silently become single Take'),
    }
    variants={'control':(body,None)}
    for name,(before,after,label) in changes.items():
        assert before in body,name;variants[name]=(body.replace(before,after),label)
    results={}
    for name,(text,label) in variants.items():
        folder=out/name;folder.mkdir();(folder/'consumer-under-test.inc').write_text(text)
        cmd=[shutil.which('c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(folder),'-I'+str(ROOT/'port/game'),str(DRIVER),str(ROOT/'port/game/ce_command_selection.cpp'),str(ROOT/'port/game/ce_command_panel.cpp'),'-o',str(folder/'check')]
        built=subprocess.run(cmd,capture_output=True,text=True);(folder/'compile.log').write_text(built.stdout+built.stderr);r={'compiled':built.returncode==0,'compile_exit':built.returncode,'expected_label':label}
        if built.returncode==0:
            ran=subprocess.run([str(folder/'check')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));output=ran.stdout+ran.stderr;(folder/'run.log').write_text(output);r.update(exit=ran.returncode,sanitizer_error='Sanitizer' in output or 'runtime error:' in output,failure_label=next((l[5:] for l in output.splitlines() if l.startswith('FAIL ')),None),counts=re.findall(r'checks=(\d+) failures=(\d+) skips=(\d+)',output))
        results[name]=r
        if name=='control' and r.get('exit')!=0:break
    passed=results.get('control',{}).get('exit')==0 and len(results)==len(variants) and all(r.get('exit')==1 and not r.get('sanitizer_error') and r.get('failure_label')==r['expected_label'] for n,r in results.items() if n!='control')
    inputs=[SOURCE,DRIVER,Path(__file__)]+[ROOT/'port/game'/n for n in ['ce_command_selection.cpp','ce_command_selection.h','ce_command_panel.cpp','ce_command_panel.h','ce_cmdorders.h','ce_commander.h','ce_command_lifetime.h']]
    receipt=out/'RESULT.private.json';receipt.write_text(json.dumps({'passed':passed,'scope':'actual authored consumer dispatch with real controller and synthetic native callbacks; no native engine acceptance','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'results':results},indent=2)+'\n');print(json.dumps({'passed':passed,'control':results.get('control'),'mutants':len(results)-1,'receipt':str(receipt)}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
