// SPDX-License-Identifier: GPL-3.0-only
// Host boundaries for the unchanged production mode enumerators. No SDL/D3D installation or game data.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <set>
#include <utility>
#include <new>
using UINT = unsigned;
using DWORD = unsigned;
using HRESULT = int;
using D3DFORMAT = int;
#define __stdcall
#define STDMETHODCALLTYPE
#define PORT_LOG_PREFIX "[display-modes-test] "
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
const HRESULT D3D_OK = 0, D3DERR_INVALIDCALL = -1;
const D3DFORMAT D3DFMT_X8R8G8B8 = 22, D3DFMT_A8R8G8B8 = 21, D3DFMT_R8G8B8 = 20, D3DFMT_R5G6B5 = 23;
struct D3DDISPLAYMODE { UINT Width, Height, RefreshRate; D3DFORMAT Format; };
bool desktop_available=true, raw_enumeration=false;
struct IDirect3D9;
UINT ModeCount(IDirect3D9 *,UINT,D3DFORMAT);
HRESULT EnumModes(IDirect3D9 *,UINT,D3DFORMAT,UINT,D3DDISPLAYMODE *);
struct IDirect3D9;
UINT RealCount(IDirect3D9 *,UINT,D3DFORMAT);
HRESULT RealEnum(IDirect3D9 *,UINT,D3DFORMAT,UINT,D3DDISPLAYMODE *);
void OriginalCreateDevice() {}
struct IDirect3D9 {
    void *hooks[64]{};
    IDirect3D9() { hooks[6]=(void *)&RealCount;hooks[7]=(void *)&RealEnum;hooks[16]=(void *)&OriginalCreateDevice; }
    std::vector<D3DDISPLAYMODE> modes[2][4];
    D3DDISPLAYMODE desktop[2] = {{1920,1080,60,22},{2560,1440,75,22}};
    UINT GetAdapterModeCount(UINT a,D3DFORMAT f) { return raw_enumeration ? RealCount(this,a,f) : ModeCount(this,a,f); }
    HRESULT EnumAdapterModes(UINT a,D3DFORMAT f,UINT i,D3DDISPLAYMODE *out) { return raw_enumeration ? RealEnum(this,a,f,i,out) : EnumModes(this,a,f,i,out); }
    HRESULT GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *out) {
        if (!desktop_available || adapter >= 2 || !out) return D3DERR_INVALIDCALL;
        *out = desktop[adapter]; return D3D_OK;
    }
};
int slot(D3DFORMAT f) { return f==22?0:f==21?1:f==20?2:f==23?3:-1; }
UINT RealCount(IDirect3D9 *d, UINT a, D3DFORMAT f) {
    return a<2 && slot(f)>=0 ? (UINT)d->modes[a][slot(f)].size() : 0;
}
HRESULT RealEnum(IDirect3D9 *d, UINT a, D3DFORMAT f, UINT n, D3DDISPLAYMODE *out) {
    if (!out || n>=RealCount(d,a,f)) return D3DERR_INVALIDCALL;
    *out=d->modes[a][slot(f)][n]; return D3D_OK;
}
bool wide = true;
int PortWideModes() { return wide; }
int PortWideDesktopSize(unsigned *w,unsigned *h) { *w=1920; *h=1080; return 1; }
int PortWideDisplaySizes(unsigned (*out)[2],int max) {
    if (max<2) return 0;
    out[0][0]=1920;out[0][1]=1080;out[1][0]=1280;out[1][1]=720;return 2;
}
@PRODUCTION@
// Actual factory body below; only native COM/OS boundaries and the unused CreateDevice hook target are faked.
using CreateDeviceFn = void (*)();
CreateDeviceFn gCreateDevice;
const int kSlots=64;
unsigned gShotFrame,gShotEvery,gQuitFrame;
char gShotPath[1024],gShotDir[1024];
IDirect3D9 *next_created;
void PortAudioGuardCheck() {}
void CreateDevice() {}
IDirect3D9 *Direct3DCreate9(UINT) { return next_created; }
void **HookTable(void *object,int) { return ((IDirect3D9 *)object)->hooks; }
void SetSlot(void **table,int slot,void *fn) { table[slot]=fn; }
@FACTORY@
@CONSUMER_HELPER@
struct { bool mValue=false; } ALLOW_WIDESCREEN_MODES;
@COLOUR_HELPERS@
unsigned ConsumerCount(const std::vector<D3DDISPLAYMODE> &modes) {
    unsigned accepted=0;
    for(auto DisplayMode:modes) {
@CONSUMER_FILTER@
        ++accepted;
    }
    return accepted;
}
// Keep the selected production collection loop and its real array bound. Extra storage makes an intentional
// unchecked old-consumer overflow observable without writing outside this owned frame.
template<unsigned N> struct ModeFrame {
    D3DDISPLAYMODE modes[N]{};
    unsigned char guard[sizeof(D3DDISPLAYMODE)*200];
    ModeFrame() { std::memset(guard,0x5a,sizeof guard); }
    bool intact() const { for(auto byte:guard) if(byte!=0x5a) return false;return true; }
};
struct CollectionResult { unsigned count;bool guard_intact;unsigned capacity;const char *scenario; };
std::vector<CollectionResult> collections;
CollectionResult CollectDeviceModes(IDirect3D9 &d) {
@CONSUMER_COLLECTION@
}
struct Query { UINT adapter; D3DFORMAT format; std::vector<D3DDISPLAYMODE> modes; };
static std::vector<Query> queries;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); } } while (0)
void add(IDirect3D9 &d,UINT a,D3DFORMAT f,UINT w,UINT h,UINT refresh=60) { d.modes[a][slot(f)].push_back({w,h,refresh,f}); }
std::vector<D3DDISPLAYMODE> list(IDirect3D9 &d,UINT a,D3DFORMAT f) {
    std::vector<D3DDISPLAYMODE> out;
    UINT count=ModeCount(&d,a,f);CHECK(count<=1000);
    for (UINT i=0;i<count && i<1000;++i) { D3DDISPLAYMODE m{};CHECK(EnumModes(&d,a,f,i,&m)==D3D_OK);out.push_back(m); }
    D3DDISPLAYMODE sentinel{7,11,13,17}, before=sentinel;
    CHECK(EnumModes(&d,a,f,count,&sentinel)==D3DERR_INVALIDCALL);
    CHECK(!std::memcmp(&sentinel,&before,sizeof before));
    CHECK(EnumModes(&d,a,f,0,nullptr)==D3DERR_INVALIDCALL);
    queries.push_back({a,f,out});
    return out;
}
bool has(const std::vector<D3DDISPLAYMODE> &v,UINT w,UINT h) { for (auto m:v) if(m.Width==w&&m.Height==h)return true;return false; }
bool same(const std::vector<D3DDISPLAYMODE> &a,const std::vector<D3DDISPLAYMODE> &b) {
    if(a.size()!=b.size())return false;
    for(unsigned i=0;i<a.size();++i)if(std::memcmp(&a[i],&b[i],sizeof a[i]))return false;
    return true;
}
bool only_real(const std::vector<D3DDISPLAYMODE> &v,const std::vector<D3DDISPLAYMODE> &real) {
    for(auto m:v) { bool found=false;for(auto r:real)if(!std::memcmp(&m,&r,sizeof m))found=true;if(!found)return false; }return true;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    gModeCount=RealCount;gEnumModes=RealEnum;
    IDirect3D9 a,b;
    const char *name=argv[1];
    if(!std::strcmp(name,"consumer-array-guard")) {
        ALLOW_WIDESCREEN_MODES.mValue=true;
        for(UINT i=0;i<64;++i) { add(a,0,22,640+4*i,480+3*i);add(a,0,21,896+4*i,672+3*i); }
        auto wrapped=CollectDeviceModes(a);wrapped.scenario="wrapped-disjoint-64-plus-64";collections.push_back(wrapped);
        CHECK(wrapped.count<=64&&wrapped.guard_intact);
        for(UINT i=0;i<64;++i) { add(b,0,22,640+4*i,480+3*i);add(b,0,21,640+4*i,480+3*i); }
        raw_enumeration=true;
        auto overlap=CollectDeviceModes(b);overlap.scenario="raw-overlap-64-plus-64";collections.push_back(overlap);
        CHECK(overlap.count==64&&overlap.guard_intact);
        auto disjoint=CollectDeviceModes(a);disjoint.scenario="raw-disjoint-64-plus-64";collections.push_back(disjoint);
        CHECK(disjoint.count==128);
        CHECK(disjoint.guard_intact==(disjoint.capacity>=128));
        IDirect3D9 large;
        for(UINT i=0;i<100;++i) { add(large,0,22,640+4*i,480+3*i);add(large,0,21,1040+4*i,780+3*i); }
        auto excessive=CollectDeviceModes(large);excessive.scenario="raw-disjoint-100-plus-100";collections.push_back(excessive);
        if(excessive.capacity==100) { CHECK(excessive.count==200&&!excessive.guard_intact); }
        else { CHECK(excessive.count==excessive.capacity&&excessive.guard_intact); }
    } else if(!std::strcmp(name,"address-reuse")) {
        alignas(IDirect3D9) unsigned char storage[sizeof(IDirect3D9)];
        auto *p=new(storage) IDirect3D9;add(*p,0,22,1920,1080);
        next_created=p;CHECK(PortDirect3DCreate9(32)==p);auto x=list(*p,0,22);p->~IDirect3D9();
        p=new(storage) IDirect3D9;add(*p,0,22,1360,768,85);
        next_created=p;CHECK(PortDirect3DCreate9(32)==p);auto y=list(*p,0,22);
        CHECK(has(y,1360,768));
#ifdef _WIN32
        CHECK(only_real(y,p->modes[0][0]));
#endif
        p->~IDirect3D9();
    } else if(!std::strcmp(name,"second-format-desktop")) {
        a.desktop[0]={2560,1440,144,21};
        for(UINT i=0;i<64;++i)add(a,0,22,640+i,480+i);
        add(a,0,21,2560,1440,144);auto x=list(a,0,22),y=list(a,0,21);
        std::set<std::pair<UINT,UINT>> sizes;
        for(auto m:x) sizes.insert({m.Width,m.Height});
        for(auto m:y) sizes.insert({m.Width,m.Height});
        CHECK(sizes.size()<=64);
#ifdef _WIN32
        CHECK(has(y,2560,1440)&&y.size()==1&&only_real(y,a.modes[0][1]));
        b=a;auto reverse_y=list(b,0,21),reverse_x=list(b,0,22);
        CHECK(same(x,reverse_x)&&same(y,reverse_y));
#else
        CHECK(has(y,1920,1080));
#endif
    } else if(!std::strcmp(name,"faithful-consumer")) {
        wide=false;add(a,0,22,1920,1080);auto x=list(a,0,22);
#ifdef _WIN32
        CHECK(x.size()==1&&only_real(x,a.modes[0][0]));
        CHECK(ConsumerCount(x)==0); // Deliberately unsupported, with an actionable production diagnostic required by the runner.
#else
        CHECK(ConsumerCount(x)==6);
#endif
    } else if(!std::strcmp(name,"faithful-other-format")) {
        wide=false;add(a,0,22,1920,1080);add(a,0,21,800,600,75);
        auto x=list(a,0,22),y=list(a,0,21);CHECK(ConsumerCount(y)==1);
    } else if(!std::strcmp(name,"unsigned-index")) {
        add(a,0,22,1920,1080);auto x=list(a,0,22);D3DDISPLAYMODE m{7,11,13,17},before=m;
        CHECK(EnumModes(&a,0,22,~0u,&m)==D3DERR_INVALIDCALL);CHECK(!std::memcmp(&m,&before,sizeof m));
#ifdef _WIN32
        CHECK(ModeCount(&a,~0u,22)==0);CHECK(EnumModes(&a,~0u,22,0,&m)==D3DERR_INVALIDCALL);
#endif
    } else if(!std::strcmp(name,"desktop-unavailable")) {
        desktop_available=false;add(a,0,22,1280,720,75);auto x=list(a,0,22);
#ifdef _WIN32
        CHECK(x.size()==1&&only_real(x,a.modes[0][0]));
#else
        CHECK(has(x,1280,720)&&has(x,1920,1080));
#endif
    } else if(!std::strcmp(name,"adapters")) {
        add(a,0,22,1280,720);add(a,0,22,1920,1080);add(a,1,22,1288,728,75);add(a,1,22,2560,1440,75);
        auto x=list(a,0,22),y=list(a,1,22),z=list(a,0,22);
#ifdef _WIN32
        CHECK(only_real(x,a.modes[0][0])&&only_real(y,a.modes[1][0]));CHECK(x.size()==2&&y.size()==2);CHECK(has(y,2560,1440));
#else
        CHECK(has(x,1920,1080)&&has(y,1288,728));
#endif
        CHECK(same(x,z));
    } else if(!std::strcmp(name,"formats")) {
        add(a,0,22,1600,900,75);add(a,0,21,1288,808,85);
        auto x=list(a,0,22),y=list(a,0,21),z=list(a,0,22);
#ifdef _WIN32
        CHECK(x.size()==1&&y.size()==1);CHECK(only_real(x,a.modes[0][0])&&only_real(y,a.modes[0][1]));
#else
        CHECK(has(y,1288,808));
#endif
        CHECK(same(x,z));CHECK(list(a,0,20).empty());
    } else if(!std::strcmp(name,"objects")) {
        add(a,0,22,1920,1080);add(b,0,22,1360,768,85);
        auto x=list(a,0,22),y=list(b,0,22),z=list(a,0,22);
#ifdef _WIN32
        CHECK(y.size()==1&&only_real(y,b.modes[0][0]));
#else
        CHECK(has(y,1360,768));
#endif
        CHECK(same(x,z));
    } else if(!std::strcmp(name,"single-native")) {
        add(a,0,22,1920,1080);auto x=list(a,0,22),empty=list(a,0,21);
#ifdef _WIN32
        CHECK(x.size()==1&&only_real(x,a.modes[0][0]));CHECK(empty.empty());
#else
        CHECK(has(x,640,480)&&has(x,1280,720)&&has(x,1920,1080));CHECK(!empty.empty());
#endif
    } else if(!std::strcmp(name,"native-larger-than-primary")) {
        add(a,0,22,3840,2160,120);auto x=list(a,0,22);
#ifdef _WIN32
        CHECK(x.size()==1&&only_real(x,a.modes[0][0]));
#else
        CHECK(!has(x,3840,2160));
#endif
    } else if(!std::strcmp(name,"fold-refresh")) {
        add(a,0,22,1920,1080,75);add(a,0,22,1920,1080,120);auto x=list(a,0,22);
#ifdef _WIN32
        CHECK(x.size()==1&&x[0].RefreshRate==75&&only_real(x,a.modes[0][0]));
#else
        CHECK(has(x,1920,1080));
#endif
    } else if(!std::strcmp(name,"faithful-missing-4by3")) {
        wide=false;add(a,0,22,1920,1080);auto x=list(a,0,22);
#ifdef _WIN32
        CHECK(x.size()==1&&same(x,a.modes[0][0]));
#else
        CHECK(x.size()==7&&has(x,640,480)&&has(x,1400,1050)&&!has(x,1600,1200));
#endif
    } else if(!std::strcmp(name,"faithful-existing-4by3")) {
        wide=false;add(a,0,22,800,600,75);add(a,0,22,1920,1080);CHECK(same(list(a,0,22),a.modes[0][0]));
    } else if(!std::strcmp(name,"nonwide-format")) {
        add(a,0,23,1920,1080);auto x=list(a,0,23);
#ifdef _WIN32
        CHECK(same(x,a.modes[0][3]));
#else
        CHECK(x.size()==7&&has(x,640,480));
#endif
    } else if(!std::strcmp(name,"desktop-preserved")) {
        a.desktop[0]={3440,1440,144,22};
        for(UINT i=0;i<180;++i) add(a,0,22,640+i,480+i);
        add(a,0,22,3440,1440,144);auto x=list(a,0,22);
#ifdef _WIN32
        CHECK(x.size()<=64&&has(x,3440,1440)&&only_real(x,a.modes[0][0]));
#else
        CHECK(has(x,1920,1080)&&!has(x,3440,1440));
#endif
    } else if(!std::strcmp(name,"bounded-union")) {
        for(UINT i=0;i<180;++i) { add(a,0,22,640+i,480+i);add(a,0,21,1000+i,700+i); }
        auto x=list(a,0,22),y=list(a,0,21);std::set<std::pair<UINT,UINT>> sizes;
        for(auto m:x) sizes.insert({m.Width,m.Height});
        for(auto m:y) sizes.insert({m.Width,m.Height});
        CHECK(sizes.size()<=64);CHECK(x.size()<=64&&y.size()<=64);
#ifdef _WIN32
        CHECK(only_real(x,a.modes[0][0])&&only_real(y,a.modes[0][1]));
#endif
    } else return 2;
    std::printf("{\"case\":\"%s\",\"checks\":%d,\"failures\":%d,\"queries\":[",name,checks,failures);
    for(unsigned i=0;i<queries.size();++i) {
        auto &q=queries[i];std::printf("%s{\"adapter\":%u,\"format\":%d,\"modes\":[",i?",":"",q.adapter,q.format);
        for(unsigned j=0;j<q.modes.size();++j) { auto m=q.modes[j];std::printf("%s[%u,%u,%u,%d]",j?",":"",m.Width,m.Height,m.RefreshRate,m.Format); }
        std::printf("]}");
    }
    std::printf("],\"collections\":[");
    for(unsigned i=0;i<collections.size();++i) {
        auto c=collections[i];std::printf("%s{\"scenario\":\"%s\",\"count\":%u,\"capacity\":%u,\"guard_intact\":%s}",i?",":"",c.scenario,c.count,c.capacity,c.guard_intact?"true":"false");
    }
    std::puts("]}");
    return failures?1:0;
}
