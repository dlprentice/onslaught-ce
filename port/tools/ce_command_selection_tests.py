#!/usr/bin/env python3
"""Compile the exact authored selection controller and causal bad variants."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'port/game/ce_command_selection.cpp';PANEL=ROOT/'port/game/ce_command_panel.cpp';DRIVER=ROOT/'port/tools/tests/ce_command_selection_driver.cpp'
def main():
    parent=ROOT/'local-data/ce-command-selection-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));source=SOURCE.read_text()
    edits={
      'id_truncation':('if (native.id >= kMaxU32) return false;','if (false) return false;','FAIL [identity round trip and overflow] !cs::ToPanelKey(Key(kWorld, 0xFFFFFFFFull, 1), &panel_key)'),
      'generation_truncation':('native.generation == 0 || native.generation > kMaxU32','native.generation == 0','FAIL [identity round trip and overflow] !cs::ToPanelKey(Key(kWorld, 3, 0x100000000ull), &panel_key)'),
      'incomplete_accepted':('if (!frame.complete) return FrameStatus::Incomplete;','if (false) return FrameStatus::Incomplete;','FAIL [invalid frame: incomplete] result.pending_cancelled'),
      'queue_overwritten':('if (pending_ == Pending::Queued) return ArmStatus::QueueFull;','if (false) return ArmStatus::QueueFull;','FAIL [one-shot queue without overwrite] Arm(cp::Action::Advance) == cs::ArmStatus::QueueFull'),
      'nonfinite_target':('if (!Finite(point.x) || !Finite(point.y) || !Finite(point.z)) {','if (false) {','FAIL [finite target coordinates] g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(not_number, 0.0f, 0.0f), &request) == cs::TargetStatus::NonFinitePoint'),
      'stale_target_rebound':('if (!target_token || target_token != arm_serial_) return TargetStatus::StaleArm;','if (false) return TargetStatus::StaleArm;','FAIL [delayed target keeps exact arm] g_ctl.SubmitTarget(old,Point(2,3,4),&request)==cs::TargetStatus::StaleArm'),
      'frame_kills_live_press':('if (r.world_changed || r.context_changed) KillGesture();','KillGesture();','FAIL [world click across ordinary frame] gesture.outcome == cs::GestureOutcome::Click && Sel(0)'),
      'frame_context_keeps_press':('if (r.world_changed || r.context_changed) KillGesture();','(void)0;','FAIL [world press across context change] gesture.outcome==cs::GestureOutcome::None&&g_ctl.SelectionCount()==1&&Sel(3)'),
      'rearmed_press_rebound':('arm_serial_ == press_arm_serial_', 'true', 'FAIL [target arm replaced during press] gesture.outcome==cs::GestureOutcome::None&&g_ctl.HasArmedTarget()'),
      'held_button_accepted':('    suppress_ = true;','    suppress_ = false;','FAIL [cancel and held button] g.outcome == cs::GestureOutcome::Suppressed'),
    }
    variants={'control':source}
    for name,(old,new,_) in edits.items():
        assert source.count(old)==1,name;variants[name]=source.replace(old,new)
    results={}
    for name,body in variants.items():
        folder=out/name;folder.mkdir();cpp=folder/SOURCE.name;cpp.write_text(body);binary=folder/'check'
        r=subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'port/game'),str(PANEL),str(cpp),str(DRIVER),'-o',str(binary)],capture_output=True,text=True);(folder/'compile.private.log').write_text(r.stdout+r.stderr);record={'compiled':r.returncode==0}
        if r.returncode==0:
            r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));output=r.stdout+r.stderr;(folder/'run.private.log').write_text(output)
            first=next((l for l in output.splitlines() if l.startswith('FAIL ')),None)
            record.update(exit=r.returncode,sanitizer_error='Sanitizer' in output or 'runtime error:' in output,first_failure=re.sub(r' \(line \d+\)$','',first) if first else None,expected_failure=edits[name][2] if name!='control' else None,counts=[list(x) for x in re.findall(r'driver: (\d+) assertions, (\d+) failed',output)])
        results[name]=record
        if name=='control' and record.get('exit')!=0:break
    passed=results.get('control',{}).get('exit')==0 and bool(results['control'].get('counts')) and results['control']['counts'][0][1]=='0' and len(results)==len(variants) and all(r.get('exit')==1 and not r.get('sanitizer_error') and r.get('first_failure')==r['expected_failure'] for n,r in results.items() if n!='control')
    inputs=[SOURCE,PANEL,DRIVER,ROOT/'port/game/ce_command_selection.h',ROOT/'port/game/ce_command_panel.h',ROOT/'port/game/ce_command_lifetime.h',Path(__file__)]
    receipt=out/'RESULT.private.json';receipt.write_text(json.dumps({'passed':passed,'scope':'actual authored selection/controller and panel, synthetic host facts only; no native acceptance','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'results':results},indent=2)+'\n')
    print(json.dumps({'passed':passed,'control':results.get('control'),'mutants':len(results)-1,'receipt':str(receipt)}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
