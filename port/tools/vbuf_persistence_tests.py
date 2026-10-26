#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual batch-owner reuse/reset checks on authored allocation and buffer boundaries.

Compile the real header, factory, constructor/destructor, reference release, level
cleanup and frame reset. An owned allocator deliberately reuses a prior water
owner's slot without clearing its payload, as an allocator is allowed to do.
Water's explicit SetPersist call remains persistent. No game, device, retail
payload, driver, GPU or window is opened; these are lifetime contracts, not pixels.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATCH = '0080-vbuf-persistence-init.patch'
CASES = ('fresh-zero', 'fresh-dirty', 'reused-water', 'reused-transient',
         'water-persistent', 'shared-persistent', 'shared-reuse', 'mixed-owners',
         'already-unlocked', 'repeat-reset')
OLD_FAILURES = {'fresh-dirty', 'reused-water', 'shared-reuse', 'mixed-owners'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


HARNESS = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
using DWORD=uint32_t;using UINT=unsigned;using UWORD=uint16_t;using UBYTE=unsigned char;using SINT=int;using HRESULT=int;
static unsigned checks,allocations,frees;
static void Check(bool ok,const char*why){++checks;if(!ok){fprintf(stderr,"FAIL %s\n",why);exit(2);}}
static void* AllocateOwner(size_t);
static bool ReturnOwner(void*);
void* operator new(size_t n){void*p=malloc(n);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p) noexcept {if(!ReturnOwner(p))free(p);}
void operator delete(void*p,size_t) noexcept {operator delete(p);}
enum {MEMTYPE_VBUFTEXTURE};
void* operator new(size_t n,int){return AllocateOwner(n);}
void operator delete(void*p,int) noexcept {operator delete(p);}
struct FVector {float X,Y,Z;};
class CVBufTexture;
struct CDXTexture {
 CVBufTexture*mVBufTexture=nullptr;unsigned refs=1;
 void AddRef(){++refs;}void Release(){Check(refs>1,"owned texture reference released exactly once");--refs;}
 char*GetName(){return (char*)"authored";}
};
struct CVBuffer {unsigned unlocks=0;HRESULT Unlock(){++unlocks;return 0;}};
struct CIBuffer {unsigned unlocks=0;HRESULT Unlock(){++unlocks;return 0;}};
#define SAFE_DELETE(p) do{delete(p);(p)=nullptr;}while(0)
#define TRACE(...) ((void)0)
@HEADER@
CVBufTexture*CVBufTexture::mFirst=nullptr;
bool CVBufTexture::mDoubleBuffer=false;
struct Slot {alignas(CVBufTexture) UBYTE bytes[sizeof(CVBufTexture)]{};bool used=false;} slots[4];
static void*AllocateOwner(size_t n){Check(n==sizeof(CVBufTexture),"actual factory allocation size");for(auto&s:slots)if(!s.used){s.used=true;++allocations;return s.bytes;}std::abort();}
static bool ReturnOwner(void*p){for(auto&s:slots)if(p==s.bytes){Check(s.used,"release a live owned slot");s.used=false;++frees;return true;}return false;}
@ACTUAL@
// This is the exact persistence request in the real water grid builder.
static void WaterPersistence(CVBufTexture*mVB){@WATER@}
static void Fill(CVBufTexture*v,bool locked=true){v->mVB[0]=new CVBuffer;v->mIB=new CIBuffer;v->mVUsed=720;v->mIUsed=120;v->mVLocked=v->mILocked=locked;v->mVData=v->mIData=locked?(UBYTE*)1:nullptr;}
static void ResetCheck(CVBufTexture*v,bool persistent,bool locked=true){
 CVBufTexture::ResetAll();Check(v->mPersist==persistent,"persistence belongs to this owner");
 Check(v->mVUsed==(persistent?720:0)&&v->mIUsed==(persistent?120:0),"frame resets both transient counts only");
 Check(v->mVB[0]->unlocks==unsigned(!persistent&&locked)&&v->mIB->unlocks==unsigned(!persistent&&locked),"frame unlocks both transient buffers exactly once");
 Check(persistent||(v->mVData==nullptr&&v->mIData==nullptr),"transient mapping pointers revoked");
}
static void Drop(CVBufTexture*v,CDXTexture&t){v->Release();CVBufTexture::FreeLevelResources();Check(!t.mVBufTexture&&t.refs==1,"cleanup removes texture cache/reference");}
int main(int argc,char**argv){
 if(argc!=2)return 1;const char*c=argv[1];CDXTexture t,oldTexture;
 bool dirty=!strcmp(c,"fresh-dirty")||!strcmp(c,"mixed-owners");
 if(dirty)for(auto&s:slots)memset(s.bytes,1,sizeof s.bytes); // canonical true bool left by an unrelated owner
 CVBufTexture*old=nullptr;
 if(!strcmp(c,"reused-water")||!strcmp(c,"reused-transient")||!strcmp(c,"shared-reuse")){
  old=CVBufTexture::GetOrCreate(&oldTexture,0);
  if(strcmp(c,"reused-transient"))WaterPersistence(old);
  if(!strcmp(c,"shared-reuse")){
   auto*shared=CVBufTexture::GetOrCreate(&oldTexture,0);Check(shared==old&&shared->mRefCount==2,"shared owner lookup retains identity");
   old->Release();CVBufTexture::FreeLevelResources();Check(CVBufTexture::mFirst==old&&frees==0,"live shared owner not retired");
  }
  Drop(old,oldTexture);Check(frees==1,"prior owner retired before reuse");
 }
 auto*v=CVBufTexture::GetOrCreate(&t,0);if(old)Check(v==old,"real factory reuses authored old slot");
 bool persistent=!strcmp(c,"water-persistent")||!strcmp(c,"shared-persistent");
 if(persistent)WaterPersistence(v);
 if(!strcmp(c,"shared-persistent")){auto*s=CVBufTexture::GetOrCreate(&t,0);Check(s==v&&s->mPersist,"shared lookup preserves explicit persistence");s->Release();}
 Fill(v,strcmp(c,"already-unlocked"));
 CVBufTexture*water=nullptr;CDXTexture waterTexture;
 if(!strcmp(c,"mixed-owners")){water=CVBufTexture::GetOrCreate(&waterTexture,0);WaterPersistence(water);Fill(water);}
 ResetCheck(v,persistent,strcmp(c,"already-unlocked"));
 if(water)ResetCheck(water,true);
 if(!strcmp(c,"repeat-reset"))ResetCheck(v,false);
 printf("{\"case\":\"%s\",\"checks\":%u,\"vertex_used\":%d,\"index_used\":%d,\"persistent\":%s,\"allocations\":%u,\"frees_before_cleanup\":%u}\n",c,checks,v->mVUsed,v->mIUsed,v->mPersist?"true":"false",allocations,frees);
 Drop(v,t);if(water)Drop(water,waterTexture);Check(CVBufTexture::mFirst==nullptr&&allocations==frees,"all owned resources retired");return 0;
}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler', default='g++')
    ap.add_argument('--sanitizers', action='store_true')
    args = ap.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/port/vbuf-persistence-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    env = dict(os.environ, TMPDIR=str(out), ASAN_OPTIONS='halt_on_error=1:detect_leaks=1:exitcode=65',
               UBSAN_OPTIONS='halt_on_error=1:exitcode=66')
    inputs = ('src/VBufTexture.h', 'src/vbuftexture.cpp', 'src/DXWater.cpp', 'port/patches/' + PATCH,
              'port/tools/vbuf_persistence_tests.py')
    receipt = dict(scope=__doc__, inputs={p:sha(ROOT/p) for p in inputs}, cases=[], commands=[], passed=False)
    try:
        compiler = subprocess.run([*shlex.split(args.compiler), '--version'],capture_output=True,text=True,check=True,timeout=10)
        receipt['compiler'] = compiler.stdout
        before = (ROOT/'src/vbuftexture.cpp').read_text()
        staged = out/'vbuftexture.cpp';staged.write_text(before)
        p = subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT/'port/patches'/PATCH)],
                           cwd=out, capture_output=True, text=True, timeout=10)
        (out/'patch.log').write_text(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError('0080 did not apply exactly')
        after = staged.read_text()
        header = (ROOT/'src/VBufTexture.h').read_text().replace('#include "maths.h"', '')
        water = (ROOT/'src/DXWater.cpp').read_text()
        call = '\t\tmVB->SetPersist();'
        if water.count(call) != 1:raise RuntimeError('water persistence call is not unique')
        signatures = ('CVBufTexture::CVBufTexture(CDXTexture *texture)', 'CVBufTexture::~CVBufTexture()',
                      'void CVBufTexture::SetPersist()', 'void CVBufTexture::UnlockVB()',
                      'void CVBufTexture::UnlockIB()', 'void CVBufTexture::ResetAll()',
                      'inline CVBufTexture *CVBufTexture::Find(CDXTexture *texture)',
                      'CVBufTexture *CVBufTexture::GetOrCreate(CDXTexture *texture, SINT unique)',
                      'void CVBufTexture::Release()', 'void CVBufTexture::FreeLevelResources()')
        variants = {'before':before, 'fixed':after,
                    'mutant-persistent-default':after.replace('mPersist = false;', 'mPersist = true;', 1),
                    'mutant-no-water-persist':after.replace('mPersist = true;', 'mPersist = false;', 1),
                    'mutant-ignore-persist':after.replace('&& !v->mPersist', '&& true')}
        for label, source in variants.items():
            actual = '\n'.join(function(source,s) for s in signatures)
            code = HARNESS.replace('@HEADER@',header).replace('@ACTUAL@',actual).replace('@WATER@',call)
            cpp = out/(label+'.cpp');cpp.write_text(code)
            binary = out/label
            cmd = [*shlex.split(args.compiler), '-std=c++17', '-O1', '-g', '-fno-sized-deallocation',
                   *(['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitizers else []),
                   str(cpp), '-o', str(binary)]
            receipt['commands'].append(cmd)
            p = subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=30)
            (out/(label+'-build.log')).write_text(p.stdout+p.stderr)
            if p.returncode:raise RuntimeError('host compile failed: '+label)
            cases = CASES if label in ('before','fixed') else ('fresh-zero',) if label=='mutant-persistent-default' else ('water-persistent',)
            for case in cases:
                expected = 2 if (label=='before' and case in OLD_FAILURES) or label.startswith('mutant-') else 0
                p = subprocess.run([str(binary),case],env=env,capture_output=True,text=True,timeout=5)
                (out/(label+'-'+case+'.log')).write_text(p.stdout+p.stderr)
                passed=p.returncode==expected and (expected==0 or 'FAIL ' in p.stderr)
                receipt['cases'].append(dict(variant=label,case=case,exit=p.returncode,expected=expected,passed=passed,
                                             observed=json.loads(p.stdout) if p.returncode==0 else None,stderr=p.stderr))
                print(label,case,'PASS' if passed else 'FAIL',flush=True)
                if not passed:raise RuntimeError('unexpected control: '+label+'/'+case)
        receipt['passed']=True
    except (OSError,RuntimeError,ValueError,subprocess.TimeoutExpired) as e:
        receipt['error']=str(e)
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(out/'result.json')
    return 0 if receipt['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
