#!/usr/bin/env python3
"""Compile the actual current-pointer query with authored platform samples."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'port/platform/common/window.cpp'
def main():
    source=SOURCE.read_text();start=source.index('extern "C" int PortMouseHeldNow(');brace=source.index('{',start);depth=1;end=brace+1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    actual=source[start:end];parent=ROOT/'local-data/ce-commander-mouse-state-tests';parent.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    prelude='''#include <cstdio>\n#include <cstdlib>\nusing SDL_WindowFlags=unsigned;using SDL_MouseButtonFlags=unsigned;
struct SDL_Window{};static SDL_Window window;static bool exists=true;static unsigned flags=1,buttons=0;static int samples=0;
constexpr unsigned SDL_WINDOW_INPUT_FOCUS=1,SDL_WINDOW_MINIMIZED=2,SDL_WINDOW_HIDDEN=4,SDL_BUTTON_LMASK=1,SDL_BUTTON_MMASK=4,SDL_BUTTON_RMASK=2;
static SDL_Window* PortMainSdlWindow(){return exists?&window:nullptr;}static unsigned SDL_GetWindowFlags(SDL_Window*){return flags;}
static unsigned SDL_GetMouseState(void*,void*){++samples;return buttons;}
'''
    driver='''
static int checks=0;static void Require(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\\n",label);std::exit(1);}}
int main(){
 Require(PortMouseHeldNow(0)==0&&PortMouseHeldNow(1)==0&&PortMouseHeldNow(2)==0,"released pointer sample");
 buttons=1;Require(PortMouseHeldNow(0)==1&&PortMouseHeldNow(1)==0&&PortMouseHeldNow(2)==0,"left held only");
 buttons=2;Require(PortMouseHeldNow(0)==0&&PortMouseHeldNow(1)==0&&PortMouseHeldNow(2)==1,"right uses native trap index");
 buttons=4;Require(PortMouseHeldNow(0)==0&&PortMouseHeldNow(1)==1&&PortMouseHeldNow(2)==0,"middle uses native trap index");
 buttons=7;Require(PortMouseHeldNow(0)==1&&PortMouseHeldNow(1)==1&&PortMouseHeldNow(2)==1,"simultaneous held buttons");
 int before=samples;flags=0;Require(PortMouseHeldNow(0)==-1&&samples==before,"focus loss is unavailable without sampling");
 flags=3;Require(PortMouseHeldNow(1)==-1&&samples==before,"minimized pointer is unavailable");
 flags=5;Require(PortMouseHeldNow(2)==-1&&samples==before,"hidden pointer is unavailable");
 flags=1;exists=false;Require(PortMouseHeldNow(0)==-1&&samples==before,"absent window is unavailable");exists=true;
 Require(PortMouseHeldNow(3)==-1&&samples==before,"unsupported button is unavailable");
 buttons=0;Require(PortMouseHeldNow(0)==0,"fresh release observed");buttons=1;Require(PortMouseHeldNow(0)==1,"fresh repress observed");
 std::printf("checks=%d failures=0 skips=0\\n",checks);return 0;
}
'''
    edits={'control':(actual,None),'right_middle_swapped':(actual.replace('SDL_BUTTON_LMASK, SDL_BUTTON_MMASK, SDL_BUTTON_RMASK','SDL_BUTTON_LMASK, SDL_BUTTON_RMASK, SDL_BUTTON_MMASK'),'right uses native trap index'),'unfocused_is_release':(actual.replace('!(flags & SDL_WINDOW_INPUT_FOCUS)','false'),'focus loss is unavailable without sampling')};results={}
    for name,(body,label) in edits.items():
        folder=out/name;folder.mkdir();cpp=folder/'check.cpp';cpp.write_text(prelude+body+driver);binary=folder/'check';r=subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-fsanitize=address,undefined',str(cpp),'-o',str(binary)],capture_output=True,text=True);(folder/'compile.log').write_text(r.stdout+r.stderr);record={'compiled':r.returncode==0}
        if r.returncode==0:
            r=subprocess.run([str(binary)],capture_output=True,text=True);output=r.stdout+r.stderr;(folder/'run.log').write_text(output);record.update(exit=r.returncode,output=output.strip(),expected_label=label,sanitizer_error='Sanitizer' in output or 'runtime error:' in output)
        results[name]=record
    passed=results['control'].get('exit')==0 and all(r.get('exit')==1 and r.get('output')=='FAIL '+r['expected_label'] and not r.get('sanitizer_error') for n,r in results.items() if n!='control')
    receipt=out/'RESULT.private.json';receipt.write_text(json.dumps({'passed':passed,'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'scope':'actual authored platform query with SDL-shaped samples only','results':results},indent=2)+'\n');print(json.dumps({'passed':passed,'checks':12,'mutants':2,'receipt':str(receipt)}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
