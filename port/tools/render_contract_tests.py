#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host-only checks of effect normalization and render-target failure cleanup.

Compile the actual patched CSnow::Init, CAnimal::Init/Move, CRenderTargetSwap and
CDXLandscape::RenderShadowMap with narrow device/scene fakes. Use the actual game
CRT generator. No game data, display, GPU, Wine or port dependencies are needed.
The old port is a mandatory negative control; its successful rendering behavior
and Windows normalization are controls for the fix. Synthetic outputs, source
snapshots and compiler diagnostics are retained under ignored local-data/port.
These checks establish CPU data and API ownership/state contracts, not pixels.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIX = "0064-render-contracts.patch"
SOURCES = ("DXSnow.cpp", "animal.cpp", "DXLandscape.cpp", "DXEngine.cpp")

COMMON = r"""
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>
using DWORD = uint32_t;
using UINT = unsigned;
using WORD = uint16_t;
using UWORD = uint16_t;
using UBYTE = unsigned char;
using SINT = int;
using HRESULT = int32_t;
#define FAILED(x) ((x) < 0)
#define SUCCEEDED(x) ((x) >= 0)
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)
#define TRUE 1
#define FALSE 0
#define __forceinline inline
"""

RANDOM = COMMON + r"""
extern "C" unsigned PortPresentCount() { return 0; }
@CRT@
#define rand bea_crt_rand
#undef RAND_MAX
#define RAND_MAX @RANDMAX@
enum { MEMTYPE_VBUFTEXTURE, D3DUSAGE_WRITEONLY, D3DUSAGE_SOFTWAREPROCESSING,
       D3DUSAGE_POINTS, D3DPT_POINTLIST, D3DPOOL_MANAGED, D3DFMT_INDEX16,
       CVar_float, CVar_bool, TF_IN_MAP_WHO=16, TF_DONT_RENDER=32,
       TF_DECLARED_SHUTDOWN=64, ANIMAL_EVENT_MOVE=3000, NEXT_FRAME=1 };
void *operator new(std::size_t n, int) { return ::operator new(n); }
void operator delete(void *p, int) noexcept { ::operator delete(p); }
struct Console {
    template<class... T> void RegisterVariable(T...) {}
    template<class... T> void Print(T...) {}
    void FatalError(bool condition, int, int) { if (!condition) throw std::runtime_error("fatal"); }
} CONSOLE;
#define TRACE(...) do {} while (0)
struct Buffer {
    DWORD data[4]{};
    void *GetBufferPointer() { return data; }
    DWORD GetBufferSize() { return sizeof(data); }
    void Release() { delete this; }
};
using LPD3DXBUFFER = Buffer *;
HRESULT D3DXAssembleShader(const char *, size_t, void *, void *, int, Buffer **out, Buffer **) {
    *out = new Buffer; return 0;
}
const char sSnowShaderSource[]="synthetic shader";
DWORD sSnowShaderCode[256];
struct CDXTexture {
    static CDXTexture *GetTextureByName(const char *) { static CDXTexture t; return &t; }
};
struct CVertexShader {
    static constexpr bool VertexShadersEnabled=true, mSoftware=false;
    static CVertexShader *Create(const char *, DWORD *, int, void *, int, int) {
        static CVertexShader s; return &s;
    }
};
@VERTEX@
struct CVBufTexture {
    std::vector<SSnowVertex> vertices;
    std::vector<WORD> indices;
    explicit CVBufTexture(void *) {}
    template<class... T> void SetVBFormat(T...) {}
    template<class... T> void SetIBFormat(T...) {}
    void SetPersist() {}
    WORD AddVertices(const SSnowVertex *p, int n) {
        WORD first=(WORD)vertices.size(); vertices.insert(vertices.end(),p,p+n); return first;
    }
    void AddIndices(const WORD *p,int n) { indices.insert(indices.end(),p,p+n); }
    void UnlockVB() {}
    void UnlockIB() {}
};
struct Vec4 { float x,y,z,w; };
struct CSnow {
    CDXTexture *mTexture;
    CVBufTexture *mVBuf;
    CVertexShader *mShader;
    float mPeriod{},mScale{},mFadeDistance{};
    bool mEnabled{};
    Vec4 mOffsets[50];
    void Init();
};
struct FVector {
    float X{},Y{},Z{};
    FVector()=default;
    FVector(float x,float y,float z):X(x),Y(y),Z(z){}
    FVector operator-(FVector v) const { return {X-v.X,Y-v.Y,Z-v.Z}; }
    FVector &operator+=(FVector v) { X+=v.X; Y+=v.Y; Z+=v.Z; return *this; }
    float Azimuth() const { return std::atan2(X,Y); }
    float Elevation() const { return 0; }
};
struct FMatrix {
    FMatrix()=default;
    template<class... T> explicit FMatrix(T...) {}
    FVector GetColumnY() const { return {0,1,0}; }
    void MakeRotationF(float,float,float) {}
    FVector operator*(FVector v) const { return v; }
};
constexpr float PI=3.14159265358979323846f;
struct CInitThing { FVector mVelocity,mPos; FMatrix mOrientation; };
struct CAnimalInitThing : CInitThing { int mType=1; };
struct CThing { void Init(CInitThing *) {} };
#define SUPERTYPE CThing
struct RenderThing { void Init(void *) {} };
RenderThing *CreateRenderThing(int) { return nullptr; }
struct CRenderData { struct Item { void *mThing; char mName[32]; } mInit[2]; int mRTID[2]; };
struct EventManager { template<class... T> void AddEvent(T...) {} } EVENT_MANAGER;
struct MapWho { void UpdateEntry(FVector) {} };
struct CAnimal : CThing {
    static CAnimal *sFirst;
    static int sCount;
    FVector mVelocity,mOldPos,mPos;
    FMatrix mOldOrientation,mOrientation;
    CAnimal *mLeader{},*mNext{};
    int mType{},mFlags{};
    float mClimb{},mHeight{};
    RenderThing *mRenderThing{};
    MapWho mMapWhoEntry;
    void Init(CInitThing *);
    void Move();
};
CAnimal *CAnimal::sFirst;
int CAnimal::sCount;
@SNOW_INIT@
@ANIMAL_INIT@
@YAW@
@ANIMAL_MOVE@
template<class T> void Emit(const T *p,size_t n=1) {
    if (std::fwrite(p,sizeof(T),n,stdout)!=n) throw std::runtime_error("write");
}
unsigned Calls() {
    unsigned calls,hold;
    PortCrtRandState(&calls,&hold);
    return calls;
}
int main() {
    const int sequence[]={41,18467,6334,26500,19169,15724,11478,29358,26962,24464};
    bea_crt_srand(1);
    for (int expected:sequence) if (bea_crt_rand()!=expected) return 4;
    for (unsigned seed : {1u,7u,0x12345678u,0xffffffffu}) {
        bea_crt_srand(seed);
        unsigned before=Calls();
        CSnow snow; snow.Init();
        if (Calls()-before!=4100) return 5;
        if (snow.mVBuf->vertices.size()!=4000 || snow.mVBuf->indices.size()!=6000) return 2;
        Emit(snow.mVBuf->vertices.data(),snow.mVBuf->vertices.size());
        Emit(snow.mVBuf->indices.data(),snow.mVBuf->indices.size());
        Emit(snow.mOffsets,50);
        delete snow.mVBuf;
        CAnimal::sFirst=nullptr; CAnimal::sCount=0;
        CAnimalInitThing init; CAnimal animal;
        bea_crt_srand(seed); before=Calls(); animal.Init(&init);
        if (Calls()-before!=1) return 6;
        Emit(&animal.mHeight);
    }
    // Exercise Move's random-climb arm inside its normal altitude band.
    unsigned tested=0;
    for (unsigned seed=0; seed<100000 && tested<8; ++seed) {
        bea_crt_srand(seed);
        int a=bea_crt_rand(), b=bea_crt_rand();
        if (a%50==1 || b%100!=0) continue;
        CAnimal animal,leader;
        animal.mLeader=&leader; animal.mPos={0,0,-45}; leader.mPos={1,1,-45};
        bea_crt_srand(seed); unsigned before=Calls(); animal.Move();
        if (Calls()-before!=3) return 7;
        Emit(&animal.mClimb); ++tested;
    }
    return tested==8 ? 0 : 3;
}
"""

TARGETS = COMMON + r"""
enum { D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTA_TEXTURE, D3DTA_DIFFUSE,
       D3DTSS_ALPHAOP, D3DTOP_DISABLE, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2,
       D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2, D3DSAMP_MINFILTER,
       D3DTEXF_ANISOTROPIC, D3DSAMP_MAXANISOTROPY, D3DTSS_COLOROP,
       D3DCLEAR_TARGET=32, D3DCLEAR_ZBUFFER=64, D3DRS_DITHERENABLE=1,
       D3DPT_TRIANGLESTRIP=2, D3DTEXF_LINEAR=3, D3DTTFF_DISABLE=4,
       D3DTOP_MODULATE=5, D3DRS_DIFFUSEMATERIALSOURCE=6, D3DMCS_COLOR1=7,
       D3DRS_ALPHABLENDENABLE=8, D3DRS_ALPHATESTENABLE=9 };
struct Ref {
    int refs=1;
    void AddRef() { ++refs; }
    void Release() { if (--refs<0) throw std::runtime_error("negative reference"); }
};
struct IDirect3DSurface9 : Ref {};
struct IDirect3DPixelShader9 : Ref {};
const char *fault;
bool Fail(const char *name) { if (fault && !std::strcmp(fault,name)) { fault=nullptr; return true; } return false; }
struct IDirect3DTexture9 {
    IDirect3DSurface9 surface;
    HRESULT GetSurfaceLevel(int,IDirect3DSurface9 **out) {
        if (Fail("get-surface")) return -1;
        surface.AddRef(); *out=&surface; return 0;
    }
};
struct Device {
    IDirect3DSurface9 oldRT,oldDS;
    IDirect3DPixelShader9 previousPS;
    IDirect3DSurface9 *rt=&oldRT,*ds=&oldDS;
    IDirect3DPixelShader9 *ps=nullptr;
    int draws=0,clears=0;
    Device() { oldRT.AddRef(); oldDS.AddRef(); }
    HRESULT GetRenderTarget(int,IDirect3DSurface9 **out) {
        if (Fail("get-rt")) return -1;
        rt->AddRef(); *out=rt; return 0;
    }
    HRESULT GetDepthStencilSurface(IDirect3DSurface9 **out) {
        if (Fail("get-ds")) return -1;
        ds->AddRef(); *out=ds; return 0;
    }
    HRESULT SetRenderTarget(int,IDirect3DSurface9 *p) {
        if (Fail("set-rt")) return -1;
        p->AddRef(); rt->Release(); rt=p; return 0;
    }
    HRESULT SetDepthStencilSurface(IDirect3DSurface9 *p) {
        if (Fail("set-ds")) return -1;
        p->AddRef(); ds->Release(); ds=p; return 0;
    }
    HRESULT SetPixelShader(IDirect3DPixelShader9 *p) {
        if (Fail("set-ps")) return -1;
        if (p) p->AddRef();
        if (ps) ps->Release();
        ps=p; return 0;
    }
    HRESULT SetIndices(void *) { return 0; }
    void SetPixelShaderConstantF(int,float *,int) {}
    void Clear(int,void *,int,DWORD,float,int) { ++clears; }
};
using LPDIRECT3DDEVICE9=Device *;
struct CVertexShader { void SetConstants() {} };
struct CVBuffer { void SetStreamSource(int) {} void SetStreamSourceOnly(int) {} };
struct CLandscapeVB : CVBuffer { int mNumVerts=4; };
struct CIBuffer { void *mIB=nullptr; };
struct CLandscapeIB { void *mIB=nullptr; int mCount=2; };
struct SLandscapeCell { int mUnknown10=0,mEdges=0; void *mPatch=nullptr; };
struct CLandscapeView {
    int mNumLevels=0;
    struct LOD { UWORD *mData=nullptr; int mUnknown08=0; } mLODs[1];
    SLandscapeCell mTiles[1];
};
struct { CLandscapeIB mIBs[1]; } PATCHMANAGER;
struct CRenderTargetSwap {
    IDirect3DSurface9 *mRenderTarget,*mDepthStencil;
    CRenderTargetSwap();
    ~CRenderTargetSwap();
    bool Begin(IDirect3DTexture9 *);
    void End();
};
struct CDXLandscape {
    IDirect3DSurface9 *mDepthSurface;
    IDirect3DTexture9 *mUnknown08;
    IDirect3DPixelShader9 *mPixelShader;
    CVertexShader *mVertexShader;
    CVBuffer *mVB;
    CIBuffer *mIB;
    int mUnknown34=5;
    CLandscapeView mViews[1];
    bool RenderShadowMap(int);
};
struct Shell {
    Device *m_pd3dDevice=nullptr;
    DWORD mRenderStates[32]{};
    Device *GetDevice() { return m_pd3dDevice; }
    template<class... T> void SetModulate(T...) {}
    template<class... T> void STS(T...) {}
    template<class... T> void SSS_NoCache(T...) {}
    template<class... T> void D3D_SetTexture(T...) {}
    void SetVertexShader(CVertexShader *) {}
    void SRS(int type,DWORD value) { mRenderStates[type]=value; }
    template<class... T> void D3D_DrawIndexedPrimitive(T...) { ++m_pd3dDevice->draws; }
} LT;
using PCLTShell=Shell;
struct { void Apply(bool,bool) {} } RENDERINFO;
struct { bool multiplayer=false; bool IsMultiplayer() { return multiplayer; } } GAME;
struct { int mValue=1; void Set(int n) { mValue=n; } } EXTRA_GEFORCE_FX_FEATURES;
struct { template<class... T> void AddMessage(T...) {} } SETUP_HISTORY;
const char *DXGetErrorString9(HRESULT) { return "scripted failure"; }
struct Water { int shadows=0; void RenderShadow() { ++shadows; } } water;
struct {
    CDXLandscape *landscape;
    CDXLandscape *GetLandscape() { return landscape; }
    Water *GetWater() { return &water; }
} ENGINE;
@FUNCTIONS@
int main(int argc,char **argv) {
    if (argc!=4) return 2;
    bool shadow=!std::strcmp(argv[1],"shadow"), prior=!std::strcmp(argv[3],"prior");
    bool success=!std::strcmp(argv[2],"success");
    Device device; IDirect3DTexture9 target; IDirect3DSurface9 depth;
    IDirect3DPixelShader9 pixel; CVertexShader vertex; CVBuffer vb; CIBuffer ib;
    CDXLandscape landscape{&depth,&target,&pixel,&vertex,&vb,&ib,5,{}};
    ENGINE.landscape=&landscape; LT.m_pd3dDevice=&device;
    LT.mRenderStates[D3DRS_DITHERENABLE]=17;
    if (prior) device.SetPixelShader(&device.previousPS);
    fault=success ? nullptr : argv[2];
    bool result;
    if (shadow) result=landscape.RenderShadowMap(0);
    else { CRenderTargetSwap swap; result=swap.Begin(&target); if (result) swap.End(); }
    bool refs=device.oldRT.refs==2 && device.oldDS.refs==2 && target.surface.refs==1 && depth.refs==1 && pixel.refs==1;
    bool targetRestored=device.rt==&device.oldRT && device.ds==&device.oldDS;
    // This pass establishes fixed-function drawing after a bound shadow shader,
    // just as its existing success cleanup does. No GetPixelShader query: pure
    // devices need not support it. A failed shader bind leaves its input alone.
    bool beforeBind=!std::strcmp(argv[2],"set-ps");
    IDirect3DPixelShader9 *expectedPS=(prior && (!shadow || beforeBind)) ? &device.previousPS : nullptr;
    bool shaderRestored=device.ps==expectedPS && device.previousPS.refs==(expectedPS ? 2 : 1);
    bool work=device.draws==(shadow && success ? 1 : 0) && device.clears==(shadow && success ? 1 : 0) &&
              water.shadows==(shadow && success ? 1 : 0) && LT.mRenderStates[D3DRS_DITHERENABLE]==17;
    bool enabled=EXTRA_GEFORCE_FX_FEATURES.mValue==(success ? 1 : 0);
    bool pass=result==success && refs && targetRestored && shaderRestored && work && enabled;
    std::printf("{\"pass\":%s,\"returned\":%s,\"references\":%s,\"targets\":%s,\"shader\":%s,\"work\":%s,\"features\":%d}",
        pass?"true":"false",result?"true":"false",refs?"true":"false",targetRestored?"true":"false",
        shaderRestored?"true":"false",work?"true":"false",EXTRA_GEFORCE_FX_FEATURES.mValue);
    return 0;
}
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def function(source, signature):
    pattern = r"^" + re.escape(signature) + r"[^\n]*\n\{\n.*?^\}"
    matches = re.findall(pattern, source, re.MULTILINE | re.DOTALL)
    if len(matches) != 1:
        raise RuntimeError(f"expected one complete function: {signature}")
    return matches[0]


def variants(source_root, out, receipt):
    raw = {name: (source_root / "src" / name).read_bytes() for name in SOURCES}
    receipt["sources"] = {name: digest(data) for name, data in raw.items()}
    result = {}
    for variant in ("baseline", "patched"):
        folder = out / variant
        folder.mkdir()
        for name, data in raw.items():
            (folder / name).write_bytes(data)
        for patch in sorted((source_root / "port/patches").glob("*.patch")):
            if variant == "baseline" and patch.name == FIX:
                continue
            data = patch.read_bytes()
            sections = [s for s in re.split(br"(?=^--- a/)", data, flags=re.MULTILINE)
                        if any(s.startswith(f"--- a/{name}\n+++ b/{name}\n".encode()) for name in SOURCES)]
            if not sections:
                continue
            proc = subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(folder), "-r", "-"],
                                  input=b"".join(sections), capture_output=True, timeout=10)
            receipt["patches"].append({"variant": variant, "name": patch.name, "sha256": digest(data),
                                       "exit": proc.returncode, "output": (proc.stdout+proc.stderr).decode()})
            if proc.returncode:
                raise RuntimeError(f"{variant}: {patch.name} did not apply")
        result[variant] = {name: (folder / name).read_text(encoding="latin-1") for name in SOURCES}
    return result


def compile_driver(cxx, out, name, text, receipt):
    source, binary = out / f"{name}.cpp", out / name
    source.write_text(text)
    cmd = [*shlex.split(cxx), "-std=c++17", "-O0", "-Wall", "-Wextra", str(source), "-o", str(binary)]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=45, env=dict(os.environ, TMPDIR=str(out)))
    receipt["compiles"].append({"name": name, "source_sha256": digest(source.read_bytes()), "command": cmd,
                                "exit": proc.returncode, "output": proc.stdout+proc.stderr})
    if proc.returncode:
        raise RuntimeError(f"{name} did not compile")
    return binary


def random_parts(data):
    """Separate actual generated data so one corrected subsystem cannot mask another."""
    result = {name: bytearray() for name in ("snow vertices", "snow indices", "snow offsets", "animal height")}
    for start in range(0, 371216, 92804):
        for name, offset, size in (("snow vertices", 0, 80000), ("snow indices", 80000, 12000),
                                   ("snow offsets", 92000, 800), ("animal height", 92800, 4)):
            result[name].extend(data[start+offset:start+offset+size])
    result["animal climb"] = data[371216:]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    args = parser.parse_args()
    parent = ROOT / "local-data/port/render-contract-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    receipt = {"source_root": str(args.source_root.resolve()), "patches": [], "compiles": [], "checks": [], "cases": []}
    passed = False
    try:
        texts = variants(args.source_root, out, receipt)
        inventory = {}
        for path in (args.source_root / "src").rglob("*"):
            if path.suffix.lower() not in (".cpp", ".c", ".h"):
                continue
            active = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="latin-1"), flags=re.DOTALL)
            count = len(re.findall(r"\bRAND_MAX\b", active))
            if count:
                inventory[str(path.relative_to(args.source_root / "src"))] = count
        receipt["rand_max_inventory"] = inventory
        receipt["checks"].append({"name": "all active game RAND_MAX uses inventoried",
            "pass": inventory == {"DXSnow.cpp": 6, "animal.cpp": 2}})
        # The game objects alone rename rand; these functions use that very generator.
        crt = (args.source_root / "port/platform/common/crtrand.cpp").read_text()
        receipt["crt_sha256"] = digest(crt.encode())
        outputs = {}
        for variant, sources in texts.items():
            snow, animal = sources["DXSnow.cpp"], sources["animal.cpp"]
            vertex = re.search(r"^struct SSnowVertex\n\{.*?^\};", snow, re.MULTILINE | re.DOTALL)
            if not vertex:
                raise RuntimeError("missing snow vertex definition")
            random = RANDOM.replace("@CRT@", crt).replace("@VERTEX@", vertex[0])
            for token, signature, source in (
                ("@SNOW_INIT@", "void CSnow::Init()", snow),
                ("@ANIMAL_INIT@", "void\tCAnimal::Init(CInitThing *init)", animal),
                ("@ANIMAL_MOVE@", "void\tCAnimal::Move()", animal),
                ("@YAW@", "static __forceinline float YawGap(float want, float yaw)", animal),
            ):
                random = random.replace(token, function(source, signature))
            for platform, maximum in (("linux", "2147483647"), ("windows", "32767")):
                name = f"{variant}-{platform}-random"
                binary = compile_driver(args.cxx, out, name, random.replace("@RANDMAX@", maximum), receipt)
                proc = subprocess.run([str(binary)], capture_output=True, timeout=10)
                (out / f"{name}.bin").write_bytes(proc.stdout)
                if proc.returncode or len(proc.stdout) != 371248:
                    raise RuntimeError(f"{name}: unexpected exit/record size {proc.returncode}/{len(proc.stdout)}")
                outputs[variant, platform] = proc.stdout
        parts = {key: random_parts(data) for key, data in outputs.items()}
        receipt["random_outputs"] = {f"{variant}/{platform}": {name: digest(data) for name, data in values.items()}
            for (variant, platform), values in parts.items()}
        for name in parts["baseline", "windows"]:
            reference = parts["baseline", "windows"][name]
            controls = (("old Linux mismatch reproduced", parts["baseline", "linux"][name] != reference),
                        ("Windows data unchanged", parts["patched", "windows"][name] == reference),
                        ("Linux equals 15-bit Windows contract", parts["patched", "linux"][name] == reference))
            for label, ok in controls:
                if name == "snow indices" and label == "old Linux mismatch reproduced":
                    label, ok = "old index topology unchanged", not ok
                receipt["checks"].append({"name": f"{name}: {label}", "pass": ok})
        receipt["ranges"] = {}
        for key, values in parts.items():
            height = struct.unpack("<4f", values["animal height"])
            climb = struct.unpack("<8f", values["animal climb"])
            offsets = struct.unpack("<800f", values["snow offsets"])
            xy = [x for index, x in enumerate(offsets) if index % 4 < 2]
            receipt["ranges"]["/".join(key)] = {"animal_height": [min(height), max(height)],
                "animal_climb": [min(climb), max(climb)], "snow_offsets_xy": [min(xy), max(xy)]}
        landscape_functions = (
            ("DXEngine.cpp", "CRenderTargetSwap::CRenderTargetSwap()"),
            ("DXLandscape.cpp", "static inline void RestoreTargets(IDirect3DSurface9 *rt, IDirect3DSurface9 *ds)"),
            ("DXLandscape.cpp", "bool CRenderTargetSwap::Begin(IDirect3DTexture9 *target)"),
            ("DXLandscape.cpp", "void CRenderTargetSwap::End()"),
            ("DXLandscape.cpp", "CRenderTargetSwap::~CRenderTargetSwap()"),
            ("DXLandscape.cpp", "static inline HRESULT SetLandscapeIndices(PCLTShell *shell, IDirect3DIndexBuffer9 *indices)"),
            ("DXLandscape.cpp", "bool CDXLandscape::RenderShadowMap(int viewpoint)"),
        )
        # Device indices are opaque for this ownership/state boundary.
        target_prefix = "using IDirect3DIndexBuffer9=void;\nstatic float SHADOW_PS_CONSTANTS[12]{};\n"
        binaries = {}
        for variant, sources in texts.items():
            functions = "\n".join(function(sources[name], sig) for name, sig in landscape_functions)
            binaries[variant] = compile_driver(args.cxx, out, f"{variant}-targets",
                TARGETS.replace("@FUNCTIONS@", target_prefix+functions), receipt)
        results = {}
        for variant, binary in binaries.items():
            for scope in ("swap", "shadow"):
                faults = ["success", "get-rt", "get-ds", "get-surface", "set-rt", "set-ds"]
                if scope == "shadow":
                    faults += ["set-ps"]
                for fault in faults:
                    for prior in ("null", "prior"):
                        proc = subprocess.run([str(binary), scope, fault, prior], capture_output=True, text=True, timeout=5)
                        if proc.returncode:
                            raise RuntimeError(f"{variant}/{scope}/{fault}/{prior}: exit {proc.returncode}")
                        actual = json.loads(proc.stdout)
                        receipt["cases"].append({"variant": variant, "scope": scope, "fault": fault, "prior": prior, **actual})
                        results[variant, scope, fault, prior] = actual
        for scope in ("swap", "shadow"):
            for prior in ("null", "prior"):
                key = (scope, "success", prior)
                receipt["checks"].append({"name": f"{scope} success unchanged ({prior})",
                    "pass": results[("baseline",)+key] == results[("patched",)+key]})
        for fault in ("set-rt", "set-ds"):
            receipt["checks"].append({"name": f"old surface leak reproduced ({fault})",
                "pass": not results["baseline", "swap", fault, "null"]["references"]})
        receipt["checks"].append({"name": "old shadow pixel-shader leak reproduced",
            "pass": not results["baseline", "shadow", "get-rt", "prior"]["shader"]})
        receipt["checks"].append({"name": "all patched device cases pass",
            "pass": all(case["pass"] for case in receipt["cases"] if case["variant"] == "patched")})
        passed = all(check["pass"] for check in receipt["checks"])
    except Exception as error:
        receipt["error"] = str(error)
    receipt["pass"] = passed
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print(json.dumps({"pass": passed, "receipt": str(out / "receipt.json"), "checks": receipt["checks"],
                      "error": receipt.get("error")}, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
