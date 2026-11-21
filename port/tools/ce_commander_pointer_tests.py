#!/usr/bin/env python3
"""Held/fresh pointer boundaries independent of native layout and input APIs."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];HEADER=ROOT/'port/game/ce_cmdpointer.h'
DRIVER=r'''#include "ce_cmdpointer.h"
#include <cstdio>
#include <cstdlib>
static int checks;static void Require(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){CeCmdPointerFence fence;int held[3]={0,0,0};
for(unsigned button=0;button<3;++button){
 held[button]=1;fence.Begin(held);
 Require(!fence.Press(button)&&!fence.Release(button),"held boundary button has no new edge");
 Require(!fence.Press(button),"release event cannot lift a held boundary");
 held[button]=0;fence.Observe(held);
 Require(fence.Press(button)&&!fence.Press(button),"fresh press accepted once");
 Require(fence.Release(button)&&!fence.Release(button),"fresh release accepted once");
}
fence.Begin(held);Require(fence.Press(0),"world press starts before focus loss");
held[1]=-1;auto lost=fence.Observe(held);
Require(!lost.available&&!fence.Press(0)&&!fence.Press(1)&&!fence.Press(2),"unavailable focus blocks every button");
Require(!fence.Release(0),"focus loss retires old world press");
Require(!fence.Press(0),"release event cannot lift unavailable boundary");
held[1]=0;auto regained=fence.Observe(held);
Require(regained.available&&!fence.Blocked(0)&&!fence.Blocked(1)&&!fence.Blocked(2),"fresh release samples restore availability");
Require(fence.Press(0)&&fence.Release(0),"fresh press after focus loss accepted");
held[0]=1;held[1]=1;held[2]=1;fence.Begin(held);fence.Observe(held);
Require(!fence.Press(0)&&!fence.Press(1)&&!fence.Press(2),"all held buttons stay blocked across observations");
held[1]=0;auto middle=fence.Observe(held);
Require(middle.released==2&&fence.Press(1)&&!fence.Press(0)&&!fence.Press(2),"release of one button cannot unblock its neighbours");
Require(fence.Release(1),"fresh middle cycle completes");
fence.Cancel();Require(!fence.Press(0)&&!fence.Release(2),"cancelled boundary has no edges");
held[0]=0;held[1]=-1;held[2]=0;fence.Begin(held);
Require(!fence.Press(0)&&!fence.Press(1)&&!fence.Press(2),"unavailable entry blocks every button");
held[0]=-1;held[1]=0;fence.Begin(held);
Require(!fence.Press(0),"negative entry is never a release");
held[0]=held[1]=held[2]=0;fence.Begin(held);
Require(fence.Press(0),"released entry accepts next press");
Require(fence.Observe(held).released==0&&fence.Release(0),"physical sampling cannot discard queued normal release");
Require(!fence.Press(3)&&!fence.Release(3)&&fence.Blocked(3),"unsupported pointer buttons unavailable");
std::printf("checks=%d failures=0 skips=0\n",checks);return 0;}
'''
def main():
    parent=ROOT/'local-data/ce-commander-pointer-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent));source=HEADER.read_text()
    variants={'control':(source,None),'held_admitted':(source.replace('if (held[i] != 0)','if (false)'),'held boundary button has no new edge'),'event_unblocks_boundary':(source.replace('pressed_ &= ~bit;','pressed_ &= ~bit; blocked_ &= ~bit;'),'release event cannot lift a held boundary'),'partial_unavailable_entry':(source.replace('if (held[i] < 0) return;','if (false) return;'),'unavailable entry blocks every button'),'normal_release_lost':(source.replace('return {released, true};','pressed_ = 0; return {released, true};'),'physical sampling cannot discard queued normal release')};results={}
    for name,(body,label) in variants.items():
        folder=out/name;folder.mkdir();(folder/HEADER.name).write_text(body);(folder/'check.cpp').write_text(DRIVER)
        r=subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-fsanitize=address,undefined',str(folder/'check.cpp'),'-o',str(folder/'check')],capture_output=True,text=True);(folder/'compile.log').write_text(r.stdout+r.stderr);record={'compiled':r.returncode==0}
        if r.returncode==0:
            r=subprocess.run([str(folder/'check')],capture_output=True,text=True);output=r.stdout+r.stderr;(folder/'run.log').write_text(output);record.update(exit=r.returncode,output=output.strip(),expected_label=label,sanitizer_error='Sanitizer' in output or 'runtime error:' in output)
        results[name]=record
    passed=results['control'].get('exit')==0 and all(r.get('exit')==1 and r.get('output')=='FAIL '+r['expected_label'] and not r.get('sanitizer_error') for n,r in results.items() if n!='control')
    receipt=out/'RESULT.private.json';receipt.write_text(json.dumps({'passed':passed,'inputs':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (HEADER,Path(__file__))},'scope':'authored pointer boundary only; no physical native acceptance','results':results},indent=2)+'\n');print(json.dumps({'passed':passed,'control':results['control'],'mutants':len(variants)-1,'receipt':str(receipt)}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
