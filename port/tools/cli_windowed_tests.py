#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual command parser and public host on authored arguments and fake D3D boundaries.

--audit-only reproduces the unpatched parser before changing any product code. --syntax-targets creates the actual
normalized public mirror and syntax-checks its parser with both installed target compilers; no game build or run.
Inputs remain below the legacy parser's token/command bounds. No files are saved and no process/window API is run.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shlex
import subprocess
import resource
from public_d3dapp_tests import function, recovery_source

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'port'
PATCH = PORT / 'patches/0071-force-windowed-cli.patch'
KEYS = ('force', 'testeur', 'res_width', 'res_height', 'client_width', 'client_height', 'level', 'skip',
        'developer', 'e3', 'pal', 'backbuffers', 'music', 'sound', 'vsync')
CASES = {
    'native-r4': '-level 110 -skipfmv -forcewindowed -res 640 480',
    'record-default': '-level 100 -skipfmv -forcewindowed -res 640 480',
    'ordinary-window': '-forcewindowed -res 800 600',
    'force-after-resolution': '-res 640 480 -forcewindowed',
    'case-insensitive': '-FORCEWINDOWED -RES 640 480',
    'explicit-testeur-before': '-testeur -forcewindowed -res 640 480',
    'explicit-testeur-after': '-forcewindowed -testeur -res 640 480',
    'fullscreen-default': '-level 110 -skipfmv -res 640 480',
    'near-name': '-forcewindowed-extra -res 640 480',
    'missing-height': '-forcewindowed -res 800',
    'missing-resolution': '-forcewindowed -res',
    'malformed-resolution': '-forcewindowed -res nope 900',
    'minimum-resolution': '-forcewindowed -res -1 0',
    'unrelated-options': '-forcewindowed -nosound -nomusic -backbuffer2 -res 640 480',
    'spaced-arguments': '  -forcewindowed   -res 640 480  ',
}


def run(args):
    p = subprocess.run([str(a) for a in args], text=True, capture_output=True, timeout=60)
    if p.returncode: raise RuntimeError(shlex.join(map(str, args)) + '\n' + p.stdout + p.stderr)
    return p.stdout


def parser_source(text):
    constructor = function(text, r'^CCLIParams::CCLIParams\s*\(')
    raw = function(text, r'^void\s+CCLIParams::GetParams\(char \*text\)')
    vector = function(text, r'^void\s+CCLIParams::GetParams\(int num_parms, char\*\*parms\)')
    linux = (PORT / 'platform/linux/main.cpp').read_text()
    linux_join = linux[linux.index('size_t len = 1;'):linux.index('// The launcher already printed')]
    return r'''#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include "ce_launch_rules.h"
namespace ActualCLI {
using SINT=int;using BOOL=int;using DWORD=uint32_t;
const int TRUE=1,FALSE=0,MAX_PATH=260;
bool E3_BUILD=false,PLAYABLE_DEMO=false;
#include "CLIParams.h"
static_assert(sizeof(CCLIParams)==0x320,"actual parser field layout");
static_assert(__builtin_offsetof(CCLIParams,mForceWindowed)==0x38,"actual force flag");
static_assert(__builtin_offsetof(CCLIParams,mResWidth)==0x164,"actual resolution");
struct Shell { int width=640,height=480;void SetResolution(int w,int h){width=w;height=h;} } LT;
struct Method { void Set(float){} } LANDSCAPE_METHOD;
struct Platform { void CreateDirectoryPath(char *,BOOL){} } PLATFORM;
struct Log { void SetFilename(char *){} } SETUP_HISTORY;
int VERSION_MAJOR=1,VERSION_MINOR=0,VERSION_BUILD=0;
char DEFAULT_OPTIONS_NAME[256]="defaultoptions.bea",sSetupHistoryPath[MAX_PATH];
CCLIParams CLIPARAMS;
static void ExitProcess(int) { std::abort(); }
static int stricmp(const char *a,const char *b){while(*a&&std::tolower((unsigned char)*a)==std::tolower((unsigned char)*b)){++a;++b;}return std::tolower((unsigned char)*a)-std::tolower((unsigned char)*b);}
#define MAX_PARMS 30
#define PARM_LENGTH 256
''' + constructor + '\n' + raw + '\n' + vector + r'''
}
extern "C" void ActualCLIProbe(const char *input,int joining,unsigned *out)
{
    using namespace ActualCLI;
    std::string storage(input);std::vector<char *> args;
    for(size_t i=0;i<storage.size();){while(i<storage.size()&&storage[i]==' ')storage[i++]=0;if(i==storage.size())break;args.push_back(&storage[i]);while(i<storage.size()&&storage[i]!=' ')++i;}
    if(joining==1)CLIPARAMS.GetParams(args.size(),args.data());
    else if(joining==2){char cmd[8192];if(!ce_windows_command(cmd,sizeof(cmd),"C:\\test folder\\bea.exe",args.data(),args.size(),1))std::abort();CLIPARAMS.GetParams(strchr(cmd+1,'"')+2);}
    else if(joining==3){std::vector<char *> av{const_cast<char *>("bea")};av.insert(av.end(),args.begin(),args.end());int argc=av.size();char **argv=av.data();
''' + linux_join + r'''
    CLIPARAMS.GetParams(cmdline);free(cmdline);}
    else {std::vector<char> data(input,input+strlen(input)+1);CLIPARAMS.GetParams(data.data());}
    unsigned values[]={unsigned(CLIPARAMS.mForceWindowed),unsigned(CLIPARAMS.mTestEur),unsigned(CLIPARAMS.mResWidth),unsigned(CLIPARAMS.mResHeight),
        unsigned(LT.width),unsigned(LT.height),unsigned(CLIPARAMS.mLevelNo),unsigned(CLIPARAMS.mSkipFMV),unsigned(CLIPARAMS.mDeveloperMode),
        unsigned(E3_BUILD),unsigned(CLIPARAMS.mPal),unsigned(CLIPARAMS.mBackBuffers),unsigned(CLIPARAMS.mMusic),unsigned(CLIPARAMS.mSound),unsigned(CLIPARAMS.mVSync)};
    memcpy(out,values,sizeof(values));
}
'''


def syntax(out):
    spec = importlib.util.spec_from_file_location('cli_windowed_build', PORT / 'build.py')
    build = importlib.util.module_from_spec(spec); spec.loader.exec_module(build)
    build.PUBLIC = True
    build.build_mirror()
    source = build.MIRROR / 'CLIParams.cpp'
    result = {}
    for name in ('linux', 'windows'):
        target = build.TARGETS[name]
        inc = [PORT / 'd3dx/include', PORT / 'include'] + target['includes'] + [build.MIRROR, build.MIRROR / 'MissionScript']
        cmd = target['cxx'] + ['-std=gnu++14'] + build.MSCXX + build.COMMON + build.X87 + ['-fsyntax-only', '-include', PORT / 'include/port.h']
        cmd += [item for path in inc for item in ('-I', path)] + [source]
        result[name] = {'command': list(map(str, cmd)), 'output': run(cmd)}
    result['mirror_parser_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--audit-only',action='store_true');p.add_argument('--syntax-targets',action='store_true');a=p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    out=ROOT/'local-data/cli-windowed-tests';out.mkdir(parents=True,exist_ok=True)
    original=(ROOT/'src/CLIParams.cpp').read_bytes();current=out/'CLIParams.cpp';current.write_bytes(original)
    if not a.audit_only:run(['patch','-p1','-d',out,'-i',PATCH,'--forward','-r','-'])
    recovery_source(out,True)
    (out/'CLIParams.h').write_bytes((ROOT/'src/CLIParams.h').read_bytes())
    probe=out/'probe-main.cpp';probe.write_text('extern "C" void ActualCLIProbe(const char *,int,unsigned *);\n#include <cstdio>\n#include <cstdlib>\nint main(int argc,char **argv){if(argc!=3)return 2;unsigned values[15];ActualCLIProbe(argv[2],atoi(argv[1]),values);for(auto v:values)printf("%u ",v);puts("");}\n')
    host=out/'host-main.cpp';host.write_text(r'''#define main UnusedPublicMain
#include "public_d3dapp_driver.cpp"
#undef main
extern "C" void ActualCLIProbe(const char *,int,unsigned *);
int main(int argc,char **argv){if(argc!=2)return 2;unsigned v[15];ActualCLIProbe(argv[1],2,v);
desktop={1280,800,60,D3DFMT_X8R8G8B8};modes={desktop};CLIPARAMS.mForceWindowed=v[0];CLIPARAMS.mResWidth=v[2];
Application app;app.SetResolution(v[4],v[5]);app.m_bUseDepthBuffer=TRUE;HRESULT h=app.Create(nullptr);
printf("{\"force\":%u,\"testeur\":%u,\"result\":%d,\"creates\":%u,\"windowed\":%d,\"width\":%u,\"height\":%u}\n",v[0],v[1],int(h),createCount,app.m_bWindowed,app.m_d3dpp.BackBufferWidth,app.m_d3dpp.BackBufferHeight);
app.Cleanup3DEnvironment();return DeviceRefs()||QueryRefs()||d3d.refs?1:0;}
''')
    common=['clang++','-std=c++14','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',out,'-I',PORT/'tools/tests','-I',PORT/'updater']
    receipt={'source_sha256':hashlib.sha256(original).hexdigest(),'cases':{},'limits':['Full actual parser constructor/methods/header; authored arguments below parser limits, fake external calls. Actual public host with fake D3D/Win32; no game/device/VM.']}
    versions={'raw':original}
    if not a.audit_only:versions['port']=current.read_bytes()
    for version,text in versions.items():
        source=out/(version+'-parser.cpp');source.write_text(parser_source(text.decode('latin-1')))
        binary=out/(version+'-parser');run(common+[source,probe,'-o',binary])
        cases={}
        for name,text in CASES.items():
            observations=[dict(zip(KEYS,map(int,run([binary,j,text]).split()))) for j in range(4)]
            if observations.count(observations[0])!=4:raise AssertionError((version,name,'joining mismatch',observations))
            v=observations[0];cases[name]=v
            force='-forcewindowed' in text.lower().split()
            expected=force and (version=='port' or ('-testeur' in text.split() and text.index('-testeur')<text.lower().index('-forcewindowed')))
            assert v['force']==expected,(version,name,v)
            assert v['testeur']==('-testeur' in text.split()) and not v['developer'] and not v['e3'] and v['pal']==1,(version,name,v)
            if name in ('native-r4','record-default'):assert(v['res_width'],v['res_height'],v['client_width'],v['client_height'])==(640,480,640,480)
            if name in ('missing-height','missing-resolution'):assert(v['res_width'],v['res_height'],v['client_width'],v['client_height'])==(0,0,640,480)
            if name in ('malformed-resolution','minimum-resolution'):assert(v['res_width'],v['res_height'],v['client_width'],v['client_height'])==(640,480,640,480)
        receipt['cases'][version]=cases
        hostbin=out/(version+'-host');run(common+['-DBEA_PUBLIC_D3DAPP_TEST','-DBEA_PUBLIC_GRAPHICS_HOST=1',source,host,PORT/'platform/public/d3dapp.cpp','-o',hostbin])
        receipt[version+'-host']={name:json.loads(run([hostbin,CASES[name]])) for name in ('native-r4','record-default','fullscreen-default')}
    if not a.audit_only:
        for name in CASES:
            old=receipt['cases']['raw'][name].copy();new=receipt['cases']['port'][name].copy()
            del old['force'];del new['force']
            assert old==new,(name,'unrelated parser field changed',old,new)
        for name in ('native-r4','record-default'):
            old=receipt['raw-host'][name];new=receipt['port-host'][name]
            assert old['force']==0 and old['result']==-2113929213 and old['creates']==0,old
            assert new=={'force':1,'testeur':0,'result':0,'creates':1,'windowed':1,'width':640,'height':480},new
        assert receipt['raw-host']['fullscreen-default']==receipt['port-host']['fullscreen-default']
    if a.syntax_targets:receipt['target_syntax']=syntax(out)
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'pass':True,'audit_only':a.audit_only,'parser_cases_per_version':len(CASES)*4,'raw_native_r4':receipt['raw-host']['native-r4'],'port_native_r4':receipt.get('port-host',{}).get('native-r4'),'result':str(out/'result.json')}))


if __name__=='__main__':main()
