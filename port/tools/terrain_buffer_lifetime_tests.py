#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual terrain counts across failed locks and index-buffer recreation.

No game, GPU, window or retail input. Compile the complete CalcLODs, its helpers,
view allocation, actual shell callbacks, multiplayer Calc gate and CIBuffer reset/lock
functions. Both target ABIs check actual terrain header layout. The terrain draw-count
consumer is extracted verbatim from RenderTerrain, with its original view/scale
locals. Checked synthetic resources turn invalid draws/indexes into observations,
not crashes. Old-source failures and exact successful data/API-trace comparisons
are mandatory. This proves CPU ownership/count contracts, not rendered pixels.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

import render_contract_tests as common


ROOT = Path(__file__).resolve().parents[2]
FIX = "0068-terrain-buffer-lifetime.patch"
FILES = ("DXLandscape.cpp", "DXLandscape.h", "landscape.h", "ibuffer.cpp", "deviceobject.h", "ltshell.cpp", "DXEngine.cpp")

HARNESS = common.COMMON + r"""
#include <sstream>
#include <cstddef>
using BOOL=int;
using D3DFORMAT=int;
enum { MEMTYPE_TEXTURE, MEMTYPE_LANDSCAPE, D3DPOOL_DEFAULT=0, D3DPOOL_MANAGED=1,
       D3DUSAGE_DYNAMIC=0x200, D3DLOCK_NOSYSLOCK=0x800, D3DLOCK_DISCARD=0x2000,
       D3DTS_TEXTURE0=16, D3DPT_TRIANGLESTRIP=5 };
constexpr HRESULT S_OK=0,E_FAIL=-1;
@DEVICE_OBJECT@
DeviceObject::~DeviceObject() {}
void *operator new(size_t n,int) { void *p=::operator new(n); std::memset(p,0xa5,n); return p; }
void *operator new[](size_t n,int) { void *p=::operator new[](n); std::memset(p,0xa5,n); return p; }
void operator delete(void *p,int) noexcept { ::operator delete(p); }
void operator delete[](void *p,int) noexcept { ::operator delete[](p); }
struct Unsafe { const char *reason; };
std::vector<std::string> trace;
template<class... A> void Record(const char *name,A... a) {
    std::ostringstream out;out<<name;((out<<':'<<a),...);trace.push_back(out.str());
}
struct FVector {
    float X{},Y{},Z{};
    FVector()=default;FVector(float x,float y,float z):X(x),Y(y),Z(z){}
    FVector operator-() const { return {-X,-Y,-Z}; }
    FVector operator*(float f) const { return {X*f,Y*f,Z*f}; }
    FVector operator+(FVector f) const { return {X+f.X,Y+f.Y,Z+f.Z}; }
};
struct FMatrix { FVector Row[3]{{1,0,0},{0,1,0},{0,0,1}}; };
struct Camera { FVector pos{4,4,-10}; FMatrix orientation; FVector GetPos(){return pos;} FMatrix GetOrientation(){return orientation;} };
struct Matrix { float _11{},_12{},_21{},_22{},_31{},_32{}; } gTextureMatrix;
struct Tweak { int mValue; void Set(float n){mValue=(int)n;} };
Tweak LANDSCAPE_MAXLEVELS{5},LANDSCAPE_MAXLEVELS_USER{5},LANDSCAPE_MINLOD_AT{64},LANDSCAPE_LOWRES_GEOM{0};
constexpr float CAMERA_LAG=.03f;
static SINT sPatchLevel[9]={-1,-1,0,0,1,1,1,1,2};
struct { bool freeCamera=false; bool IsFreeCameraOn(int){return freeCamera;} bool IsMultiplayer(){return true;} } GAME;
struct IDirect3DIndexBuffer9 {
    static int nextID;
    int id=++nextID;
    std::vector<WORD> words;
    bool valid=false,failNext=false,locked=false;
    explicit IDirect3DIndexBuffer9(unsigned bytes):words(bytes/2,0xffff){}
    HRESULT Lock(unsigned,unsigned,void **out,DWORD flags) {
        Record("lock",id,flags);
        if(failNext){failNext=false;return E_FAIL;}
        if(flags&D3DLOCK_DISCARD){std::fill(words.begin(),words.end(),0xffff);valid=false;}
        *out=words.data();locked=true;return S_OK;
    }
    HRESULT Unlock(){Record("unlock",id);locked=false;valid=true;return S_OK;}
    void Release(){Record("release",id);delete this;}
};
int IDirect3DIndexBuffer9::nextID;
struct CIBuffer:DeviceObject {
    IDirect3DIndexBuffer9 *mIB=nullptr;
    UINT mSize=40000;DWORD mUsage=D3DUSAGE_DYNAMIC;SINT mFormat=101,mPool=D3DPOOL_DEFAULT;
    UBYTE *mData=nullptr;bool mLocked=false;
    CIBuffer();HRESULT RestoreDeviceObjects() override;HRESULT InvalidateDeviceObjects() override;HRESULT LockDynamic(void **);HRESULT Unlock();
};
struct Device {
    IDirect3DIndexBuffer9 *indices=nullptr;
    void SetIndices(IDirect3DIndexBuffer9 *p){indices=p;Record("indices",p?p->id:0);}
    void SetTransform(int stage,const Matrix *m){Record("transform",stage,m->_11,m->_22,m->_31,m->_32);}
} device;
struct { void RenderLoadingScreen(int a,int b){Record("loading",a,b);} } CONSOLE;
bool sShowLoadingProgress=false;
struct PCLTShell {
    DeviceObject *mDeviceObjects=nullptr,*mFirstDeviceObjects=nullptr;
    void AddDeviceObject(DeviceObject *);HRESULT RestoreDeviceObjects();HRESULT InvalidateDeviceObjects();
    Device *GetDevice(){return &device;}
    HRESULT D3D_CreateIndexBuffer(UINT n,DWORD usage,D3DFORMAT format,int pool,IDirect3DIndexBuffer9 **out){
        *out=new IDirect3DIndexBuffer9(n);Record("create",(*out)->id,n,usage,format,pool);return S_OK;
    }
    void D3D_SetTexture(int stage,const void *p){Record("texture",stage,(uintptr_t)p);}
    HRESULT D3D_DrawIndexedPrimitive(int type,UINT min,UINT vertices,UINT start,UINT primitives){
        Record("draw",type,min,vertices,start,primitives);
        if(!device.indices || !device.indices->valid || device.indices->locked ||
           start+primitives+2>device.indices->words.size())throw Unsafe{"invalid GPU index range"};
        for(UINT i=start;i<start+primitives+2;++i)if(device.indices->words[i]>=vertices)throw Unsafe{"unwritten GPU index"};
        return S_OK;
    }
} LT;
struct CVBuffer { void SetStreamSourceOnly(int s){Record("stream",s);} };
struct CLandscapeVB:CVBuffer {
    UWORD mSlot=0xffff;int mNumVerts=9;
    void SetOrigin(int x,int y,int step,float complexity){Record("origin",x,y,step,complexity);}
};
using SLandscapePatch=CLandscapeVB;
struct SLandscapeCell {
    CLandscapeVB *mPatch=nullptr;
    signed char mUnknown10=-99;
    int mEdges=0;float mMinHeight=-128,mMaxHeight=127,mMidHeight=0,mComplexity=0;
};
struct CellAllocation {
    SLandscapeCell *data=nullptr;
    CellAllocation &operator=(SLandscapeCell *p){data=p;return *this;}
    operator SLandscapeCell *(){return data;}
    template<class I> SLandscapeCell &operator[](I index){if((uint64_t)index>=4096)throw Unsafe{"CPU tile index out of bounds"};return data[index];}
};
struct CLandscapeTexture {
    UWORD *mTileCache=nullptr;int mTileCacheSize=16;
    void *mTexture=nullptr;
    void Init(int level,int detail){mTexture=(void *)(uintptr_t)(level+1);mTileCache=new UWORD[mTileCacheSize];Record("texture-init",level,detail);}
    UWORD *TileEntry(UWORD tile){return &mTileCache[tile%16];}
    void RequestTile(UWORD tile,UWORD *){Record("tile",tile);}
    static void FlushUpdateQueue(){Record("flush");}
};
@LOD_STRUCT@
namespace NativeLayout {
@LAYOUT_HEADER@
#if UINTPTR_MAX == 0xffffffffu
static_assert(sizeof(CDXLandscape)==64, "terrain object layout changed");
static_assert(offsetof(CDXLandscape,mUnknown34)==0x34, "terrain far count moved");
static_assert(offsetof(CDXLandscape,mNumViews)==0x20, "terrain view count moved");
static_assert(offsetof(CDXLandscape,mIB)==0x2c, "terrain index buffer moved");
#endif
}
#undef CLANDSCAPE
#if UINTPTR_MAX == 0xffffffffu
static_assert(sizeof(SLandscapeLOD)==12, "terrain LOD layout changed");
static_assert(offsetof(SLandscapeLOD,mUnknown08)==8, "terrain LOD count moved");
#endif
struct CLandscapeView {
    CLandscapeTexture *mTextures;
    SLandscapeLOD *mLODs;
    CellAllocation mTiles;
    int mNumLevels;
    Camera *mCamera=nullptr;
    FVector mUnknown24,mLastPos;
    CLandscapeView();void CreateMipLevels(SINT);
};
struct CDXLandscape:DeviceObject {
    CVBuffer *mVB;
    CIBuffer *mIB;
    CLandscapeTexture *mTexture;
    CLandscapeView *mViews;
    void *mCloudTexture;BOOL mUnknown3C;void *mUnknown08,*mDepthSurface,*mDepthTexture,*mMirrorTexture,*mVertexShader,*mPixelShader;
    SINT mUnknown34,mNumViews;
    @INVALIDATE_DECL@
    CDXLandscape();void CalcLODs(struct C3DGamut *,int);void DrawCounts(int);
};
struct CLandscapeIB { IDirect3DIndexBuffer9 *mIB=nullptr;SINT mCount=2; };
struct PatchList {
    CLandscapeVB patch;
    CLandscapeVB *Allocate(int slot){patch.mSlot=(UWORD)slot;Record("allocate",slot);return &patch;}
};
struct { PatchList mLists[3];CLandscapeIB mIBs[48]; } PATCHMANAGER;
struct C3DGamut { char mSpans[64*64*2];int mGridSize=64,mMinX=0,mMaxX=0,mMinY=0,mMaxY=0; };
void ToWCHAR(char *) {throw Unsafe{"dead source call reached"};}
@FUNCTIONS@
struct EngineGate {
    C3DGamut *mGamut;CDXLandscape *mLandscape;
    void MaybeCalc(int viewpoint){
@ENGINE_GATE@
    }
};
void CDXLandscape::DrawCounts(int viewpoint){
    CLandscapeView *view=&mViews[viewpoint];float scale=1.f/512;
@DRAW_CONSUMER@
}
void InitializeTiles(CLandscapeView &view){
    for(unsigned i=0;i<4096;++i)view.mTiles[i]=SLandscapeCell{};
}
void Dump(CDXLandscape &land,int view,const char *unsafe,int constructorCount=0){
    std::printf("{\"unsafe\":\"%s\",\"far_count\":%d,\"constructor_lod_count\":%d,\"lod_counts\":[",unsafe,land.mUnknown34,constructorCount);
    for(int v=0;v<land.mNumViews;++v)for(int i=0;i<land.mViews[v].mNumLevels;++i)
        std::printf("%s%d",(v||i)?",":"",land.mViews[v].mLODs[i].mUnknown08);
    std::printf("],\"trace\":[");
    for(size_t i=0;i<trace.size();++i)std::printf("%s\"%s\"",i?",":"",trace[i].c_str());
    std::printf("],\"indices\":[");
    if(land.mUnknown34>=0 && land.mUnknown34<=20000)
        for(int i=0;i<land.mUnknown34;++i)std::printf("%s%u",i?",":"",land.mIB->mIB->words[i]);
    std::printf("],\"tiles\":[");
    for(unsigned i=0;i<4096;++i){auto &t=land.mViews[view].mTiles[i];std::printf("%s[%d,%d,%u]",i?",":"",(int)t.mUnknown10,t.mEdges,t.mPatch?t.mPatch->mSlot:65535);}
    auto &v=land.mViews[view];std::printf("],\"view\":[%a,%a,%a,%a,%a,%a]}",(double)v.mLastPos.X,(double)v.mLastPos.Y,
        (double)v.mLastPos.Z,(double)v.mUnknown24.X,(double)v.mUnknown24.Y,(double)v.mUnknown24.Z);
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    std::string test=argv[1];
    bool gated=test=="gated-far-reset"||test=="gated-near-reset"||test=="gated-recover";
    const int levels=gated?4:5; // native two-view path uses four levels; other controls exercise five
    CDXLandscape *land=new(MEMTYPE_LANDSCAPE) CDXLandscape;
    if(test=="pre-init-notification"||test=="partial-init-notification"){
        if(test=="partial-init-notification"){
            land->mNumViews=2;land->mViews=new CLandscapeView[2];
        }
        LT.AddDeviceObject(land);LT.InvalidateDeviceObjects();
        std::printf("{\"unsafe\":\"\",\"far_count\":%d,\"num_views\":%d,\"lod_counts\":[]}",land->mUnknown34,land->mNumViews);
        return 0;
    }
    CIBuffer index;land->mIB=&index;land->mNumViews=2;land->mViews=new CLandscapeView[2];
    Camera cameras[2];
    for(int v=0;v<2;++v){land->mViews[v].CreateMipLevels(levels);InitializeTiles(land->mViews[v]);land->mViews[v].mCamera=&cameras[v];}
    if(test=="constructor-counts"||test=="reinitialize-counts"){
        if(test=="reinitialize-counts")for(int v=0;v<2;++v)for(int l=0;l<levels;++l){
            auto &lod=land->mViews[v].mLODs[l];lod.mUnknown08=17+v+l;
            delete[] lod.mData;lod.Init(lod.mSize);
        }
        auto *fresh=new(MEMTYPE_LANDSCAPE) SLandscapeLOD;
        Dump(*land,0,"",fresh->mUnknown08);return 0;
    }
    index.RestoreDeviceObjects();
    for(auto &entry:PATCHMANAGER.mIBs){entry.mIB=new IDirect3DIndexBuffer9(8);entry.mIB->words={0,1,2,3};entry.mIB->valid=true;}
    C3DGamut gamut;std::fill(std::begin(gamut.mSpans),std::end(gamut.mSpans),0);
    if(test=="success-far"||test=="reset-fail"||test=="lock-fail"||test=="recover"||test=="gated-far-reset"||test=="gated-recover"){
        LANDSCAPE_MINLOD_AT.mValue=0;gamut.mMaxX=gamut.mMaxY=63;
    }
    if(test=="success-empty"){gamut.mMinX=64;gamut.mMaxX=63;}
    const char *unsafe="";
    EngineGate gate{&gamut,land};
    // Success controls start with defined prior counts in both views. Initial-failure controls keep allocation poison.
    if(test!="initial-fail")for(int v=0;v<2;++v)for(int l=0;l<levels;++l)land->mViews[v].mLODs[l].mUnknown08=0;
    trace.clear();
    bool fail=test=="initial-fail"||test=="reset-fail"||test=="lock-fail"||test=="recover"||
              test=="near-reset-fail"||test=="near-lock-fail"||test=="near-recover";
    if(fail&&test!="initial-fail"){
        // Both actual view callers have live counts before the shared buffer fails.
        land->CalcLODs(&gamut,1);
        land->CalcLODs(&gamut,0);device.indices=index.mIB;land->DrawCounts(0);
        if(test!="lock-fail"&&test!="near-lock-fail"){index.InvalidateDeviceObjects();index.RestoreDeviceObjects();}
    }
    if(gated){
        // Native Init registers landscape after its CIBuffer, so prepend in that order.
        LT.AddDeviceObject(land);land->CalcLODs(&gamut,1);gate.MaybeCalc(0);
        LT.InvalidateDeviceObjects();LT.RestoreDeviceObjects();
        gate.MaybeCalc(0); // actual multiplayer gate skips this view-0 update
    }else{
        if(fail)index.mIB->failNext=true;
        land->CalcLODs(&gamut,0);
    }
    device.indices=index.mIB;
    try {land->DrawCounts(0);land->DrawCounts(1);}catch(const Unsafe &u){unsafe=u.reason;}
    if(test=="recover"||test=="near-recover"||test=="gated-recover"){
        // A later valid rebuild must recover the same data, with no permanent invalid flag.
        trace.clear();land->CalcLODs(&gamut,1);
        if(test=="gated-recover")gate.MaybeCalc(0);else land->CalcLODs(&gamut,0);
        device.indices=index.mIB;unsafe="";
        try {land->DrawCounts(0);land->DrawCounts(1);}catch(const Unsafe &u){unsafe=u.reason;}
    }
    Dump(*land,0,unsafe);return 0;
}
"""


def definition(source, start):
    hits = list(re.finditer(r"^"+re.escape(start), source, re.MULTILINE))
    if len(hits) != 1:
        raise RuntimeError(f"expected one function: {start}")
    end = source.index("\n}", hits[0].start())+2
    return source[hits[0].start():end]


def terrain_driver(sources):
    landscape = sources["DXLandscape.cpp"]
    functions = []
    for start in ("CLandscapeView::CLandscapeView()", "void CLandscapeView::CreateMipLevels(SINT levels)",
                  "CDXLandscape::CDXLandscape()", "static inline BOOL AllocatePatch(",
                  "static inline FVector PositionLagTerm(", "static inline void SmoothLandscapeView(",
                  "static inline void RetireLandscapePatch(", "static inline void CloseLandscapeStrip(",
                  "static inline SINT ChooseLandscapeTexture(", "static inline void StitchLandscapeCell(",
                  "static inline void AppendLandscapeFarCell(", "static inline FVector LandscapeColumnY(",
                  "void CDXLandscape::CalcLODs("):
        functions.append(definition(landscape, start))
    if "HRESULT CDXLandscape::InvalidateDeviceObjects()" in landscape:
        functions.append(definition(landscape, "HRESULT CDXLandscape::InvalidateDeviceObjects()"))
    functions.insert(0, definition(sources["DXLandscape.h"], "inline void SLandscapeLOD::Init("))
    for start in ("CIBuffer::CIBuffer()", "HRESULT CIBuffer::RestoreDeviceObjects()", "HRESULT CIBuffer::InvalidateDeviceObjects()",
                  "HRESULT CIBuffer::LockDynamic(", "HRESULT CIBuffer::Unlock()"):
        functions.insert(0, definition(sources["ibuffer.cpp"], start))
    for start in ("void\tPCLTShell::AddDeviceObject(", "HRESULT PCLTShell::InvalidateDeviceObjects()",
                  "HRESULT PCLTShell::RestoreDeviceObjects()"):
        functions.insert(0, definition(sources["ltshell.cpp"], start))
    lod = re.search(r"^struct SLandscapeLOD\n\{.*?^\};", sources["landscape.h"], re.MULTILINE | re.DOTALL)
    if not lod:
        raise RuntimeError("missing actual LOD structure")
    terrain = definition(landscape, "void CDXLandscape::RenderTerrain(")
    draw_start = terrain.index("\tif (mUnknown34 > 2)")
    draw_end = terrain.index("\n\tif (LANDSCAPE_METHOD.GetMethod() >= 2)", draw_start)
    consumer = terrain[draw_start:draw_end]
    engine = definition(sources["DXEngine.cpp"], "HRESULT CDXEngine::Render(int viewpoint)")
    gate = engine[engine.index("\tif (GAME.IsMultiplayer())"):engine.index("\n\t// And then start doing drawing.")]
    declaration = re.search(r"^\s*virtual HRESULT\s+InvalidateDeviceObjects\(\);[^\n]*", sources["landscape.h"], re.MULTILINE)
    return (HARNESS.replace("@LOD_STRUCT@", lod[0]).replace("@FUNCTIONS@", "\n".join(functions))
            .replace("@DRAW_CONSUMER@", consumer).replace("@DEVICE_OBJECT@", sources["deviceobject.h"])
            .replace("@ENGINE_GATE@", gate).replace("@INVALIDATE_DECL@", declaration[0] if declaration else "")
            .replace("@LAYOUT_HEADER@", re.sub(r'^#include[^\n]*', '', sources["landscape.h"], flags=re.MULTILINE)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--compile-cxx", action="append", default=[], metavar="COMMAND",
                        help="also compile each actual-source fixture to an object (for cross-target ABI checks)")
    args = parser.parse_args()
    parent = ROOT / "local-data/port/terrain-buffer-lifetime-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"patches": [], "compiles": [], "cases": [], "checks": []}
    passed = False
    try:
        common.FIX, common.SOURCES = FIX, FILES
        texts = common.variants(args.source_root, out, receipt)
        observations = {}
        for variant, sources in texts.items():
            driver = terrain_driver(sources)
            binary = common.compile_driver(args.cxx, out, variant+"-terrain", driver, receipt)
            for i, compiler in enumerate(args.compile_cxx):
                source, obj = out / (variant+"-terrain.cpp"), out / f"{variant}-target-{i}.o"
                command = [*shlex.split(compiler), "-std=c++17", "-O0", "-Wall", "-Wextra", "-c", str(source), "-o", str(obj)]
                proc = subprocess.run(command, capture_output=True, text=True, timeout=45, env=dict(os.environ, TMPDIR=str(out)))
                receipt["compiles"].append({"name": f"{variant}-target-{i}", "command": command,
                    "exit": proc.returncode, "output": proc.stdout+proc.stderr,
                    "object_sha256": hashlib.sha256(obj.read_bytes()).hexdigest() if obj.exists() else None})
                if proc.returncode:
                    raise RuntimeError(f"{variant}: cross compile {i} failed")
            for case in ("pre-init-notification", "partial-init-notification", "gated-far-reset", "gated-near-reset", "gated-recover", "constructor-counts", "reinitialize-counts", "initial-fail", "reset-fail", "lock-fail",
                         "near-reset-fail", "near-lock-fail", "recover", "near-recover", "success-far", "success-near", "success-empty"):
                proc = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=5)
                if proc.returncode:
                    raise RuntimeError(f"{variant}/{case}: exited {proc.returncode}: {proc.stderr}")
                # C++ hex floats are exact strings rather than non-JSON numeric tokens.
                data = re.sub(r"(?<![\w\"])(-?0x[0-9a-f.]+p[+-]?[0-9]+)", r'"\1"', proc.stdout)
                value = json.loads(data)
                (out / f"{variant}-{case}.json").write_text(json.dumps(value, indent=2)+"\n")
                observations[variant, case] = value
                receipt["cases"].append({"variant": variant, "case": case, "unsafe": value["unsafe"],
                    "far_count": value["far_count"], "lod_counts": value["lod_counts"],
                    "observation_sha256": hashlib.sha256(proc.stdout.encode()).hexdigest()})
        for case in ("pre-init-notification", "partial-init-notification"):
            after = observations["patched", case]
            receipt["checks"].append({"name": case+": callback safe with no initialized views", "pass":
                after["far_count"] == 0 and after["num_views"] == (2 if case == "partial-init-notification" else 0)})
        for case in ("gated-far-reset", "gated-near-reset"):
            before, after = observations["baseline", case], observations["patched", case]
            receipt["checks"].append({"name": case+": reset before optional Calc leaves old draw counts", "pass":
                bool(before["unsafe"]) if case == "gated-far-reset" else any(before["lod_counts"][:len(before["lod_counts"])//2]) and any(before["lod_counts"][len(before["lod_counts"])//2:])})
            receipt["checks"].append({"name": case+": actual shell invalidation revokes every draw count", "pass":
                not after["unsafe"] and after["far_count"] == 0 and not any(after["lod_counts"])})
        for case in ("constructor-counts", "reinitialize-counts"):
            before, after = observations["baseline", case], observations["patched", case]
            receipt["checks"].append({"name": case+": old undefined/stale counts reproduced", "pass": bool(any(before["lod_counts"])) and before["constructor_lod_count"] != 0})
            receipt["checks"].append({"name": case+": defined empty draw counts", "pass": after["far_count"] == 0 and after["constructor_lod_count"] == 0 and not any(after["lod_counts"])})
        for case in ("initial-fail", "reset-fail"):
            before, after = observations["baseline", case], observations["patched", case]
            receipt["checks"].append({"name": case+": old invalid consumption reproduced", "pass": bool(before["unsafe"])})
            receipt["checks"].append({"name": case+": all invalid counts cleared", "pass": after["far_count"] == 0 and not any(after["lod_counts"])})
            receipt["checks"].append({"name": case+": invalid terrain consumption prevented", "pass": not after["unsafe"]})
        for case in ("success-far", "success-near", "success-empty", "recover", "near-recover", "gated-recover"):
            before, after = observations["baseline", case], observations["patched", case]
            receipt["checks"].append({"name": case+": exact API trace and data unchanged", "pass": before == after and not after["unsafe"]})
        for case in ("lock-fail", "near-reset-fail", "near-lock-fail"):
            before, after = observations["baseline", case], observations["patched", case]
            receipt["checks"].append({"name": case+": old retained draw authorization reproduced", "pass":
                before["far_count"] > 2 if case == "lock-fail" else any(before["lod_counts"][:len(before["lod_counts"])//2]) and any(before["lod_counts"][len(before["lod_counts"])//2:])})
            receipt["checks"].append({"name": case+": all views fail closed", "pass":
                not after["unsafe"] and after["far_count"] == 0 and not any(after["lod_counts"])})
        passed = all(check["pass"] for check in receipt["checks"])
    except Exception as error:
        receipt["error"] = str(error)
    receipt["pass"] = passed
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print(json.dumps({"pass": passed, "receipt": str(out / "receipt.json"), "checks": receipt["checks"], "error": receipt.get("error")}, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
