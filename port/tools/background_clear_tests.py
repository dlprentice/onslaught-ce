#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual frame clear and five-face sky geometry on controlled host boundaries.

No game, display, GPU, retail file, profile or driver runs. A poison surface models
D3DSWAPEFFECT_DISCARD's unspecified pixels, not the reporter's hardware output.
Healthy low-altitude water is a required counterexample to claiming that an open
sky floor alone proves an ordinary-flight hole. Private/replay behavior is retained.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATCH = '0072-ce-clear-uncovered-background.patch'

CPP = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using DWORD=uint32_t; using HRESULT=int; using BOOL=int;
#define PC 1
#define XBOX 2
#define TARGET PC
#define FALSE 0
#define S_OK 0
#define D3DCLEAR_TARGET 1
#define D3DCLEAR_ZBUFFER 2
#define D3DCLEAR_STENCIL 4
static std::vector<std::string> calls;
static std::array<DWORD,8> surface;
static DWORD flagsSeen, colorSeen;
static bool gMirrorPass;
static int failure;
struct CViewport { DWORD words[6]; };
struct State { void UseDefault(){calls.push_back("default");} } STATE;
void SwitchAllLights(BOOL v){if(v)std::abort();calls.push_back("lights-off");}
struct Shell {
 HRESULT D3D_BeginScene(){calls.push_back("begin");return failure==2?-5:0;}
 HRESULT D3D_EndScene(){calls.push_back("end");return 0;}
 HRESULT D3D_Clear(unsigned n,void *rect,DWORD flags,DWORD color,float depth,unsigned long stencil){
  if(n||rect||depth!=1.0f||stencil)std::abort();
  calls.push_back("clear");flagsSeen=flags;colorSeen=color;
  if(failure==1)return -5;
  if(flags&D3DCLEAR_TARGET)surface.fill(color);return 0;
 }
} LT;
struct Platform { CViewport received{};void SetViewport(CViewport*p){received=*p;calls.push_back("viewport");} } PLATFORM;
struct CDXEngine { int mCurrentViewpoint=1; CViewport mViewport[2]{}; CViewport mCurrentViewport{}; float mRipplePhase=7; HRESULT PreRender(CViewport*); };
@CE@
@PRE@
@SKY@
struct V { double x,y,z; };
static V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V vertex(int i){return {sVerts[i][0],sVerts[i][1],sVerts[i][2]};}
static bool hit(V ray,V a,V b,V c){
 V e1=sub(b,a),e2=sub(c,a),p=cross(ray,e2);double det=dot(e1,p);if(std::abs(det)<1e-12)return false;
 double inv=1/det;V t={-a.x,-a.y,-a.z};double u=dot(t,p)*inv;if(u<0||u>1)return false;
 V q=cross(t,e1);double v=dot(ray,q)*inv;return v>=0&&u+v<=1&&dot(e2,q)*inv>0;
}
static bool sky(V ray){for(int i=0;i<20;i+=4)if(hit(ray,vertex(i),vertex(i+1),vertex(i+2))||hit(ray,vertex(i+2),vertex(i+1),vertex(i+3)))return true;return false;}
static bool water(V ray,double height){
 if(ray.z<=0)return false;double t=height/ray.z;double x=ray.x*t,y=ray.y*t;
 // On-axis ray, so far clipping is distance along the view direction.
 double distance=t*std::sqrt(dot(ray,ray));
 return distance<=@FAR@ && x>=@START@ && y>=@START@ && x<=@LAST@ && y<=@LAST@;
}
static int check(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL %s\n",message);return 1;}return 0;}
int main(int argc,char**argv){
 if(argc!=5)return 2;
 failure=std::atoi(argv[4]);
 bool expectedClear=std::atoi(argv[1]),mirror=std::atoi(argv[2]),covered=std::atoi(argv[3]);
 if(check(sky({0,1,0})&&sky({0,0,-1})&&sky({0,1,.25}),"actual sky covers horizontal/up/shallow-down controls"))return 1;
 if(check(!sky({0,0,1})&&!sky({0,1,1}),"actual five-face sky leaves downward rays uncovered"))return 1;
 if(check(water({0,1,1},150)&&water({0,0,1},150),"healthy low-altitude water covers the open sky floor"))return 1;
 if(check(!water({0,1,1},1000)&&!water({0,0,1},1000),"finite water/projection counterexample; not an admitted flight state"))return 1;
 for(unsigned pattern=0;pattern<3;pattern++){
  calls.clear();for(unsigned i=0;i<surface.size();i++)surface[i]=0xff000000u^(pattern*0x126abcdu+i*0x1234567u);
  auto poison=surface;gMirrorPass=mirror;CDXEngine engine;CViewport vp={{9,11,640,480,0,1}};
  if(check(engine.PreRender(&vp)==S_OK,"actual PreRender returns S_OK"))return 1;
  std::vector<std::string> order={"default","lights-off","begin","viewport","clear","end"};
  if(check(calls==order,"actual API order and one clear retained"))return 1;
  if(check(flagsSeen==DWORD(D3DCLEAR_ZBUFFER|((expectedClear||mirror)?D3DCLEAR_TARGET:0)),"target/depth flags"))return 1;
  if(check(colorSeen==0x00607080u,"existing background color retained"))return 1;
  if(check(engine.mCurrentViewpoint==0&&engine.mRipplePhase==8&&std::memcmp(&vp,&engine.mViewport[0],sizeof(vp))==0&&std::memcmp(&vp,&engine.mCurrentViewport,sizeof(vp))==0&&std::memcmp(&vp,&PLATFORM.received,sizeof(vp))==0,"viewport/ripple state unchanged"))return 1;
  if(failure==1&&!covered){if(check(surface==poison,"failed Clear has no background guarantee; original status path retained"))return 1;continue;}
  if(covered)surface.fill(0xffabcdefu); // A fully drawn surface hides the clear in either implementation.
  if(covered){if(check(std::all_of(surface.begin(),surface.end(),[](DWORD c){return c==0xffabcdefu;}),"fully drawn output unchanged"))return 1;}
  else if(expectedClear||mirror){if(check(std::all_of(surface.begin(),surface.end(),[](DWORD c){return c==0x00607080u;}),"uncovered pixels independent of discarded poison"))return 1;}
  else if(check(surface==poison,"faithful unfilled color storage preserved"))return 1;
 }
 std::puts("PASS actual PreRender; three poison patterns; source sky/water geometry; API/state controls");return 0;
}
'''


def function(text, declaration):
    m = re.search(r'^'+re.escape(declaration)+r'[^\n]*\n\{\n.*?^\}', text, re.M|re.S)
    if not m: raise ValueError('missing unique production function '+declaration)
    return m.group(0)


def main():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    parent=ROOT/'local-data/port/background-clear-tests';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    files=['src/DXEngine.cpp','src/DXKempyCube.cpp','src/BattleEngine.cpp','src/d3dapp.cpp','port/platform/public/d3dapp.cpp','port/platform/common/ce.cpp','port/patches/'+PATCH,'port/tools/background_clear_tests.py']
    receipt={'passed':False,'inputs':{},'commands':[],'cases':[],'limits':['Host controlled D3D clear/viewport and poison surface; no rendered GPU pixels or reporter reproduction.','Water clipping counterexample at height1000 is not an admitted ordinary-flight state; height150 control proves sky opening alone insufficient.','Faithful backbuffer residue remains unchanged; CE-only behavior gives uncovered pixels the existing background only after a successful Clear. Device/Clear failure status behavior is intentionally unchanged.']}
    try:
        for rel in files:
            p=out/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/rel).read_bytes());receipt['inputs'][rel]=hashlib.sha256(p.read_bytes()).hexdigest()
        raw=(out/'src/DXEngine.cpp').read_text()
        result=subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(out/'port/patches'/PATCH)],cwd=out/'src',capture_output=True,text=True,timeout=10)
        (out/'patch.log').write_text(result.stdout+result.stderr)
        if result.returncode or 'fuzz' in result.stdout:raise ValueError('patch not admitted exactly')
        fixed=(out/'src/DXEngine.cpp').read_text()
        sky=(out/'src/DXKempyCube.cpp').read_text()
        definitions='\n'.join(re.findall(r'^#define KC_.*$',sky,re.M))
        vertices=re.search(r'static float\s+sVerts\[20\]\[5\] =\n\{.*?\n\};',sky,re.S)
        if not vertices:raise ValueError('missing five-face vertex table')
        far=re.search(r'float howfar = (\d+);',raw)
        water=(ROOT/'src/DXWater.cpp').read_text()
        start=re.search(r'#define WATER_GRID_START\s+\(([-0-9.f]+)\)',water)
        step=re.search(r'x \+= ([0-9.f]+);',water)
        if not far or not start or not step:raise ValueError('projection/water constants changed')
        receipt['inputs']['src/DXWater.cpp']=hashlib.sha256(water.encode()).hexdigest()
        last=float(start[1].rstrip('f'))+24*float(step[1].rstrip('f'))
        for rel in ('src/d3dapp.cpp','port/platform/public/d3dapp.cpp'):
            if 'D3DSWAPEFFECT_DISCARD' not in (out/rel).read_text():raise ValueError('discard selection no longer present')
        if 'if (mPos.Z<-150.0f)' not in (out/'src/BattleEngine.cpp').read_text():raise ValueError('ordinary aircraft height countercontrol changed')
        ce=function((out/'port/platform/common/ce.cpp').read_text(),'bool PortCeLayer()')
        base=CPP.replace('@CE@',ce).replace('@SKY@',definitions+'\n'+vertices[0]).replace('@FAR@',far[1]).replace('@START@',start[1]).replace('@LAST@',repr(last))
        bodies={'fixed':function(fixed,'HRESULT CDXEngine::PreRender(CViewport *viewport)'), 'old':function(raw,'HRESULT CDXEngine::PreRender(CViewport *viewport)')}
        bodies['no-target']=bodies['fixed'].replace('clearFlags |= D3DCLEAR_TARGET;','clearFlags |= 0;')
        bodies['always-target']=bodies['fixed'].replace('if (PortCeLayer())','if (true)')
        env={k:v for k,v in os.environ.items() if not k.startswith(('BEA_PORT_',))}
        binaries={}
        for name,body in bodies.items():
            source=out/(name+'.cpp');source.write_text(base.replace('@PRE@',body))
            for ce_build in ([True,False] if name in ('fixed','old') else [True]):
                label=name+('-ce' if ce_build else '-private');binary=out/label
                cmd=[*shlex.split(os.environ.get('CXX','clang++')),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-Wno-unused-function',str(source),'-o',str(binary)]
                if ce_build:cmd.insert(-2,'-DCE_VERSION="test"')
                receipt['commands'].append(cmd)
                r=subprocess.run(cmd,capture_output=True,env=env,timeout=60);(out/(label+'.build.log')).write_bytes(r.stdout+r.stderr)
                if r.returncode:raise ValueError('compile failed '+label)
                binaries[label]=binary
        def run(label,clear,mirror=0,covered=0,extra=None,expected=0,failure=0):
            name=label+'-'+str(len(receipt['cases']));cmd=[str(binaries[label]),str(clear),str(mirror),str(covered),str(failure)]
            r=subprocess.run(cmd,env=dict(env,**(extra or {})),capture_output=True,timeout=10)
            (out/(name+'.log')).write_bytes(r.stdout+r.stderr)
            receipt['cases'].append({'binary':label,'clear':clear,'mirror':mirror,'covered':covered,'failure':failure,'env':extra or {},'exit':r.returncode,'expected':expected,'passed':r.returncode==expected})
            if r.returncode!=expected:raise ValueError('case failed '+name)
        run('fixed-ce',1);run('fixed-ce',1,covered=1);run('fixed-ce',1,mirror=1)
        for key in ('BEA_PORT_CLOCK','BEA_PORT_TAPE','BEA_PORT_PROBES','BEA_PORT_RECORD'):
            run('fixed-ce',0,extra={key:'1'});run('fixed-ce',0,mirror=1,extra={key:'1'})
        run('fixed-ce',0,extra={'BEA_PORT_CE_LAYER':'0'});run('fixed-ce',1,extra={'BEA_PORT_CLOCK':'shim','BEA_PORT_CE_LAYER':'1'})
        run('fixed-private',0);run('fixed-private',0,mirror=1)
        run('old-ce',1,expected=1);run('no-target-ce',1,expected=1);run('always-target-ce',0,extra={'BEA_PORT_CLOCK':'shim'},expected=1)
        run('old-ce',0);run('old-ce',0,mirror=1);run('old-private',0)
        for failure in (1,2):
            run('fixed-ce',1,failure=failure);run('fixed-ce',0,mirror=1,extra={'BEA_PORT_CLOCK':'shim'},failure=failure);run('fixed-private',0,failure=failure)
        receipt['passed']=True
    except (OSError,ValueError,subprocess.TimeoutExpired) as exc:receipt['error']=str(exc)
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'result':str(out/'result.json'),'passed':receipt['passed'],'groups':len(receipt['cases']),'error':receipt.get('error')}))
    return 0 if receipt['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
