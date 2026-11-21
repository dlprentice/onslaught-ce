#!/usr/bin/env python3
"""Compile actual authored authority/collector functions with deterministic facts."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'port/game/ce_commander.cpp'
DRIVER=ROOT/'port/tools/tests/ce_cmdunit_authority_driver.cpp'
HEADER=ROOT/'port/game/ce_cmdorders.h'
def function(source,name):
    match=re.search(r'^(?:bool|int) '+name+r'\(',source,re.M)
    start=match.start();brace=source.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
def main():
    parent=ROOT/'local-data/ce-cmdunit-authority-tests';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    functions={n:function(SOURCE.read_text(),n) for n in ['UnitAuthority','CeCmdUnitAuthorityNow','CeCmdUnitCollectNative']}
    edits={
      'shutdown_squad_authority':('CeCmdUnitAuthorityNow',' || gRecs[i].squad->IsShuttingDown()','','shutdown squad rejected before native readers'),
      'shutdown_squad_snapshot':('CeCmdUnitCollectNative',' || record.squad->IsShuttingDown()','','shutdown squad rejected before description'),
      'shutdown_reobserved':('UnitAuthority','unit->IsShuttingDown() ||','false ||','shutdown reader rejected before squad query'),
      'stale_membership':('UnitAuthority','unit->GetSquad() != record.squad || ','','transferred unit is not authorized by old record'),
      'squad_allegiance_only':('UnitAuthority',' && (int)unit->GetAllegiance() == allegiance','','own allegiance can revoke friendly squad authority'),
      'script_ignored':('UnitAuthority',' || unit->GetMissionScript()','','unit script independently revokes order authority'),
      'zero_life_accepted':('UnitAuthority','unit->GetLife() <= 0','false','zero-life native member is refused'),
      'dead_record_read':('CeCmdUnitAuthorityNow','if (!gRecs[i].alive || !gRecs[i].squad || gRecs[i].squad->IsShuttingDown()) continue;','(void)0;','dead record is skipped before native member traversal'),
      'overflow_claims_complete':('CeCmdUnitCollectNative','if (accepted == capacity) { all = false; continue; }','if (accepted == capacity) { continue; }','native overflow cannot claim complete snapshot'),
    }
    results={}
    for name in ['control',*edits]:
        values=dict(functions)
        if name!='control':
            fn,old,new,label=edits[name]
            if values[fn].count(old)!=1:raise RuntimeError('mutation anchor '+name)
            values[fn]=values[fn].replace(old,new)
        folder=out/name;folder.mkdir()
        (folder/'authority-under-test.inc').write_text('\n'.join(values.values())+'\n')
        command=[shutil.which('c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(folder),'-I'+str(ROOT/'port/game'),str(DRIVER),'-o',str(folder/'check')]
        built=subprocess.run(command,capture_output=True,text=True);(folder/'compile.log').write_text(built.stdout+built.stderr)
        record={'compiled':built.returncode==0,'compile_exit':built.returncode}
        if built.returncode==0:
            ran=subprocess.run([str(folder/'check')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
            output=ran.stdout+ran.stderr;(folder/'run.log').write_text(output)
            record.update(exit=ran.returncode,sanitizer_error='Sanitizer' in output or 'runtime error:' in output,failure_label=next((l[5:] for l in output.splitlines() if l.startswith('FAIL ')),None),counts=re.findall(r'checks=(\d+) failures=(\d+) skips=(\d+)',output))
        results[name]=record
        if name=='control' and record.get('exit')!=0:break
    passed=results.get('control',{}).get('exit')==0 and len(results)==len(edits)+1 and all(r.get('exit')==1 and not r.get('sanitizer_error') and r.get('failure_label')==edits[n][3] for n,r in results.items() if n!='control')
    report={'passed':passed,'scope':'actual authored authority and snapshot collection with synthetic native facts; no native acceptance','inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (SOURCE,HEADER,DRIVER,Path(__file__))},'results':results}
    receipt=out/'RESULT.private.json';receipt.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'passed':passed,'control':results.get('control'),'mutants':len(results)-1,'receipt':str(receipt)}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
