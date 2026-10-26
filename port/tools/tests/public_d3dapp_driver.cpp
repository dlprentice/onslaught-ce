// SPDX-License-Identifier: GPL-3.0-only
#include "public_d3dapp_support.h"
#include <cstdlib>
#include <iostream>
#include <memory>
TestCLI CLIPARAMS;
CDisplayModeKey gDisplayModeKey;
TestLog SETUP_HISTORY;
TestConsole CONSOLE;
TestShell LT;
CTweakSLONG SCREEN_SHAPE("SCREEN_SHAPE",0),TEXTURE_RES_LOSS_SHIFT("TEXTURE_RES_LOSS_SHIFT",0),TEXTURE_ALLOW_32_BIT("TEXTURE_ALLOW_32_BIT",0);
extern CTweakSLONG ALLOW_WIDESCREEN_MODES,TRY_LOCKABLE_BB,SUGGEST_MULTISAMPLE_TYPE;
namespace {
std::vector<std::string> events;
std::vector<D3DDISPLAYMODE> modes;
std::vector<D3DFORMAT> blockedDepth;
std::deque<HRESULT> attempts;
RECT client={0,0,640,480};
WNDPROC dispatch;
bool wide=false,rejectHal=false,rejectHardware=false;
bool skipPrimary=false,countedCallbacks=false,writeFailedDevice=false,permitWrongCreate=false;
bool windowAllowed=true,fullAllowed=true,windowColourSamples=true,windowDepthSamples=true,desktopAvailable=true;
bool desktopDepth=true,formatCaps=false,refWindowOnly=false;
bool capsAvailable=true,rejectRef=false,resetFails=false,createFails=false,finishFails=false,initFails=false,restoreFails=false;
int vpPolicy=0;
D3DDISPLAYMODE desktop={1920,1080,60,D3DFMT_X8R8G8B8};
struct TypeCheck { UINT adapter; D3DDEVTYPE type; D3DFORMAT colour; BOOL windowed; };
struct SampleCheck { D3DFORMAT format; BOOL windowed; };
std::vector<TypeCheck> typeChecks;
std::vector<SampleCheck> sampleChecks;
unsigned quitCount=0,createCount=0,resetCount=0;
unsigned ownedOutputWrites=0;
std::vector<UINT> createdAdapters,displayAdapters;
std::vector<std::unique_ptr<IDirect3DDevice9>> devices;
std::vector<std::unique_ptr<IDirect3DQuery9>> queries;
IDirect3D9 d3d; IDirect3DSurface9 surface;
D3DPRESENT_PARAMETERS last;
D3DPRESENT_PARAMETERS nativeParameters;
HRESULT Outcome() { if(attempts.empty()) return S_OK; HRESULT h=attempts.front(); attempts.pop_front(); return h; }
unsigned DeviceRelease(IDirect3DDevice9 *p) { if(!p->refs)throw std::runtime_error("device double release");if(!--p->refs)--d3d.refs;return p->refs; }
unsigned DeviceRefs(){unsigned refs=0;for(auto &d:devices)refs+=d->refs;return refs;}
unsigned QueryRefs(){unsigned refs=0;for(auto &q:queries)refs+=q->refs;return refs;}
IDirect3DDevice9 *NewDevice(UINT adapter){devices.emplace_back(new IDirect3DDevice9);auto p=devices.back().get();p->refs=1;p->adapter=adapter;p->identifier=devices.size();++d3d.refs;return p;}
}
unsigned IDirect3DQuery9::Release(){events.push_back("query-release");if(!refs)throw std::runtime_error("query double release");if(!--refs)DeviceRelease(owner);return refs;}
HRESULT IDirect3DSurface9::GetDesc(D3DSURFACE_DESC *p){p->Width=nativeParameters.BackBufferWidth;p->Height=nativeParameters.BackBufferHeight;return S_OK;}
unsigned IDirect3DSurface9::Release(){if(!refs)throw std::runtime_error("surface double release");if(!--refs)DeviceRelease(owner);return refs;}
HRESULT IDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS *p){++resetCount;events.push_back("reset");last=*p;HRESULT h=resetFails?E_FAIL:Outcome();if(SUCCEEDED(h))nativeParameters=*p;return h;}
HRESULT IDirect3DDevice9::GetDeviceCaps(D3DCAPS9 *p){*p=D3DCAPS9();return finishFails?E_FAIL:S_OK;}
HRESULT IDirect3DDevice9::GetBackBuffer(UINT,UINT,int,IDirect3DSurface9 **p){surface.refs=1;surface.owner=this;++refs;*p=&surface;return S_OK;}
HRESULT IDirect3DDevice9::CreateQuery(int,IDirect3DQuery9 **p){queries.emplace_back(new IDirect3DQuery9);*p=queries.back().get();(*p)->refs=1;(*p)->owner=this;++refs;events.push_back("query-create");return S_OK;}
BOOL IDirect3DDevice9::ShowCursor(BOOL b){return b;}
void IDirect3DDevice9::SetCursorPosition(int,int,DWORD){}
unsigned IDirect3DDevice9::GetAvailableTextureMem(){return 128u<<20;}
unsigned IDirect3DDevice9::Release(){events.push_back("device-release");return DeviceRelease(this);}
UINT IDirect3D9::GetAdapterCount(){return skipPrimary?2:1;}
HRESULT IDirect3D9::GetAdapterIdentifier(UINT,DWORD,D3DADAPTER_IDENTIFIER9 *p){*p=D3DADAPTER_IDENTIFIER9();return S_OK;}
HRESULT IDirect3D9::GetAdapterDisplayMode(UINT adapter,D3DDISPLAYMODE *p){displayAdapters.push_back(adapter);if(!desktopAvailable||(skipPrimary&&adapter==0))return E_FAIL;*p=desktop;return S_OK;}
UINT IDirect3D9::GetAdapterModeCount(UINT,D3DFORMAT f){unsigned n=0;for(auto &m:modes)if(m.Format==f)++n;return n;}
HRESULT IDirect3D9::EnumAdapterModes(UINT,D3DFORMAT f,UINT i,D3DDISPLAYMODE *p){for(auto &m:modes)if(m.Format==f){if(!i--){*p=m;return S_OK;}}return E_FAIL;}
HRESULT IDirect3D9::GetDeviceCaps(UINT,D3DDEVTYPE t,D3DCAPS9 *p){*p=D3DCAPS9();return !capsAvailable||(rejectHal&&t==D3DDEVTYPE_HAL)||(rejectRef&&t==D3DDEVTYPE_REF)?E_FAIL:S_OK;}
HRESULT IDirect3D9::CheckDeviceType(UINT a,D3DDEVTYPE t,D3DFORMAT colour,D3DFORMAT,BOOL windowed){typeChecks.push_back({a,t,colour,windowed});return (rejectHal&&t==D3DDEVTYPE_HAL)||!(windowed?windowAllowed:fullAllowed)||(!windowed&&refWindowOnly&&t==D3DDEVTYPE_REF)?E_FAIL:S_OK;}
HRESULT IDirect3D9::CheckDeviceFormat(UINT,D3DDEVTYPE,D3DFORMAT colour,DWORD usage,int,D3DFORMAT format){if(usage==D3DUSAGE_DEPTHSTENCIL)for(auto f:blockedDepth)if(f==format)return E_FAIL;if(formatCaps&&colour==D3DFMT_R8G8B8&&format==D3DFMT_DXT1)return E_FAIL;return S_OK;}
HRESULT IDirect3D9::CheckDeviceMultiSampleType(UINT,D3DDEVTYPE,D3DFORMAT format,BOOL windowed,D3DMULTISAMPLE_TYPE,DWORD *p){sampleChecks.push_back({format,windowed});*p=windowed&&((format==desktop.Format&&!windowColourSamples)||(format!=desktop.Format&&!windowDepthSamples))?0:1;return S_OK;}
HRESULT IDirect3D9::CheckDepthStencilMatch(UINT,D3DDEVTYPE,D3DFORMAT colour,D3DFORMAT,D3DFORMAT){return !desktopDepth&&colour==desktop.Format?E_FAIL:S_OK;}
HRESULT IDirect3D9::CreateDevice(UINT adapter,D3DDEVTYPE type,HWND,DWORD flags,D3DPRESENT_PARAMETERS *p,IDirect3DDevice9 **out){++createCount;events.push_back("create");createdAdapters.push_back(adapter);last=*p;if(*out&&(*out)->refs)++ownedOutputWrites;HRESULT h=createFails||(skipPrimary&&adapter==0&&!permitWrongCreate)?E_FAIL:Outcome();*out=nullptr;if(SUCCEEDED(h)||writeFailedDevice){*out=NewDevice(adapter);(*out)->creationFlags=flags;(*out)->type=type;}if(SUCCEEDED(h))nativeParameters=*p;return h;}
unsigned IDirect3D9::Release(){events.push_back("d3d-release");if(!refs)throw std::runtime_error("factory double release");return --refs;}
IDirect3D9 *Direct3DCreate9(UINT){++d3d.refs;return &d3d;}
HICON LoadIcon(HINSTANCE,const char *){return nullptr;} HCURSOR LoadCursor(HINSTANCE,const char *){return nullptr;}
void *GetStockObject(int){return nullptr;} BOOL RegisterClass(const WNDCLASS *p){dispatch=p->lpfnWndProc;return TRUE;}
BOOL AdjustWindowRect(RECT *,DWORD,BOOL){return TRUE;} void *LoadMenu(HINSTANCE,const char *){return nullptr;}
HWND CreateWindow(const char *,const char *,DWORD,int,int,int w,int h,HWND,void *,HINSTANCE,void *){client={0,0,w,h};return reinterpret_cast<HWND>(1);}
LONG GetWindowLong(HWND,int){return WS_VISIBLE;} LONG SetWindowLong(HWND,int,LONG){return 0;}
BOOL GetWindowRect(HWND,RECT *p){*p=client;return TRUE;} BOOL GetClientRect(HWND,RECT *p){*p=client;return TRUE;}
BOOL SetWindowPos(HWND,HWND,int,int,int,int,UINT){return TRUE;} LONG GetClassLong(HWND,int){return 0;}
LRESULT DefWindowProc(HWND,UINT,WPARAM,LPARAM){return 0;}
LRESULT SendMessage(HWND h,UINT m,WPARAM w,LPARAM l){return dispatch?dispatch(h,m,w,l):0;}
HCURSOR SetCursor(HCURSOR p){return p;} BOOL GetCursorPos(POINT *p){*p={0,0};return TRUE;}
BOOL ScreenToClient(HWND,POINT *){return TRUE;} int MessageBox(HWND,const char *,const char *,UINT){return 0;}
void Sleep(unsigned){events.push_back("sleep");}
FLOAT DXUtil_Timer(TIMER_COMMAND c){if(c==TIMER_START)events.push_back("timer-start");if(c==TIMER_STOP)events.push_back("timer-stop");return 0;}
int PortWideModes(){return wide;} int PortWideDefaultSize(unsigned *w,unsigned *h){*w=1920;*h=1080;return wide;}
int PortWideStartSize(unsigned w,unsigned h,int,unsigned *outW,unsigned *outH){if(!wide||w!=0)return 0;*outW=1920;*outH=1080;return 1;}
void TestShell::TriggerQuit(){++quitCount;events.push_back("quit");}
void LoadCardId(char *){} HRESULT D3DUtil_SetDeviceCursor(LPDIRECT3DDEVICE9,HCURSOR){return S_OK;}
void SetSetupMessage(int){}
struct Application : CD3DApplication {
    IDirect3DDevice9 *callbackDevice=nullptr;
    HRESULT ConfirmDevice(D3DCAPS9 *,DWORD flags,D3DFORMAT colour) override{if(vpPolicy&&colour==desktop.Format&&flags!=D3DCREATE_SOFTWARE_VERTEXPROCESSING)return E_FAIL;if(vpPolicy==2&&colour!=desktop.Format&&flags==D3DCREATE_SOFTWARE_VERTEXPROCESSING)return E_FAIL;return rejectHardware&&(flags&D3DCREATE_HARDWARE_VERTEXPROCESSING)?E_FAIL:S_OK;}
    HRESULT InitDeviceObjects() override{events.push_back("init");if(countedCallbacks){callbackDevice=m_pd3dDevice;++callbackDevice->refs;}return initFails?E_FAIL:S_OK;}
    HRESULT RestoreDeviceObjects() override{events.push_back("restore");return restoreFails?E_FAIL:S_OK;}
    HRESULT InvalidateDeviceObjects() override{events.push_back("invalidate");return S_OK;}
    HRESULT DeleteDeviceObjects() override{events.push_back("delete");if(callbackDevice){DeviceRelease(callbackDevice);callbackDevice=nullptr;}return S_OK;}
    HRESULT ReleaseDeviceObjects(bool progress) override{events.push_back(progress?"release-progress":"release");if(callbackDevice){DeviceRelease(callbackDevice);callbackDevice=nullptr;}return S_OK;}
    HRESULT FinalCleanup() override{events.push_back("final");return S_OK;}
};
#include "public_d3dapp_recovery.h"
#include "public_d3dapp_callers.h"
static CD3DApplication *flipTarget;
static unsigned savedWidth,savedHeight,saveCalls;
char gDisplayResetState=0;
#define MEMTYPE_SCRATCHPAD 1
void *operator new[](size_t size,int) { return ::operator new[](size); }
struct CCareer {
    static int SizeOfSaveGame(){return sizeof(CDisplayModeKey);}
    void Save(char *data){ActualWriteKey(*flipTarget);memcpy(data,&gDisplayModeKey,sizeof(gDisplayModeKey));}
} CAREER;
struct CFEPOptions {
    static void WriteDefaultOptionsFile(char *data,int size){CDisplayModeKey key;memcpy(&key,data,size);savedWidth=key.mWidth;savedHeight=key.mHeight;++saveCalls;}
};
struct CPlatform { void DeviceFlip(BOOL){} void Flip(BOOL); } PLATFORM;
#define LT (*flipTarget)
#include "public_d3dapp_flip.h"
#undef LT

int PublicationControl(const std::string &name)
{
    unsigned failures=0,checks=0;
    auto check=[&](bool condition,const char *description){++checks;if(!condition){++failures;std::cout<<"FAIL: "<<description<<"\n";}};
    desktop={1280,800,60,D3DFMT_X8R8G8B8};modes={desktop};CLIPARAMS.mForceWindowed=TRUE;rejectRef=true;
    if(name=="pending-index"||name=="fullscreen-reset")modes={{640,480,60,D3DFMT_X8R8G8B8},{1024,768,60,D3DFMT_X8R8G8B8}};
    Application app;app.m_bUseDepthBuffer=TRUE;
    check(SUCCEEDED(app.Create(nullptr)),"precondition window starts");
    client={0,0,800,600};app.MsgProc(app.m_hWnd,WM_EXITSIZEMOVE,0,0);
    auto *d=app.GetCurrentDevice();DWORD activeIndex=d->dwCurrentMode;
    D3DModeInfo active=d->modes[activeIndex];D3DPRESENT_PARAMETERS parameters=app.m_d3dpp;
    ActualWriteKey(app);
    check(app.m_bReady&&gDisplayModeKey.mWidth==800&&gDisplayModeKey.mHeight==600&&
          nativeParameters.BackBufferWidth==800&&nativeParameters.BackBufferHeight==600,"precondition successful resize and saved key are 800x600");
    if(name=="query"||name=="pending-index")desktopAvailable=false;
    if(name=="pending-index")d->dwCurrentMode=1;
    if(name=="caps")capsAvailable=false;
    if(name=="depth")desktopDepth=false;
    if(name=="size")app.m_rcWindowClient.right=0;
    if(name=="reset"||name=="resize-reset"||name=="fullscreen-reset")resetFails=true;
    if(name=="fullscreen-reset"){d->dwCurrentMode=1;app.m_bWindowed=FALSE;d->bWindowed=FALSE;}
    if(name=="create")createFails=true;
    if(name=="finish")finishFails=true;
    if(name=="init")initFails=true;
    if(name=="resize-restore")restoreFails=true;
    bool resize=name=="resize-reset"||name=="resize-restore";
    bool direct=name=="create";
    if(name=="reset"||direct||name=="finish"||name=="init"||resize)
    {
        // A requested new client/format/caps record must stay provisional until the entire host change succeeds.
        app.m_rcWindowClient={0,0,1024,768};
        desktop.Format=D3DFMT_R8G8B8;formatCaps=true;
    }
    HRESULT result=E_FAIL;
    if(resize)
    {
        app.m_d3dpp.BackBufferWidth=1024;app.m_d3dpp.BackBufferHeight=768;
        app.m_bReady=FALSE;result=app.Resize3DEnvironment();
    }
    else if(direct){app.m_bReady=FALSE;result=app.Initialize3DEnvironment(false);}
    else{flipTarget=&app;gDisplayResetState=2;PLATFORM.Flip(FALSE);}
    if(resize||direct)
    {
        check(FAILED(result),"injected direct change fails");
        ActualWriteKey(app);savedWidth=gDisplayModeKey.mWidth;savedHeight=gDisplayModeKey.mHeight;
    }
    else check(saveCalls==1&&gDisplayResetState==0,"actual Flip still invokes the options writer after reset failure");
    check(savedWidth==800&&savedHeight==600,"failed change preserves last successful client key");
    check(d->dwCurrentMode==activeIndex&&memcmp(&active,&d->modes[activeIndex],sizeof(active))==0,
          "failed change preserves the exact active record and selected index");
    check(app.m_bWindowed&&d->bWindowed&&app.m_bReady==FALSE,"failed change retains window domain without admitting rendering");
    check(app.m_d3dpp.BackBufferWidth==800&&app.m_d3dpp.BackBufferHeight==600&&
          app.m_d3dpp.BackBufferFormat==parameters.BackBufferFormat&&app.mScreenShapeScale==1.f,"failed change rolls back presentation and projection metadata");
    app.Cleanup3DEnvironment();
    check(DeviceRefs()==0&&QueryRefs()==0&&d3d.refs==0,"failure cleanup releases all references");
    std::cout<<"publication="<<name<<" saved="<<savedWidth<<"x"<<savedHeight<<" native-last-success="
        <<nativeParameters.BackBufferWidth<<"x"<<nativeParameters.BackBufferHeight<<" saves="<<saveCalls<<" checks="<<checks<<" failures="<<failures<<"\n";
    return failures?1:0;
}
int WindowControl(const std::string &name)
{
    unsigned failures=0,checks=0;
    auto check=[&](bool condition,const char *description){++checks;if(!condition){++failures;std::cout<<"FAIL: "<<description<<"\n";}};
    desktop={1280,800,60,D3DFMT_X8R8G8B8};
    modes={desktop}; CLIPARAMS.mForceWindowed=TRUE;
    if(name=="desktop-secondary")skipPrimary=true;
    if(name=="fullscreen-only"||name=="physical-fullscreen")CLIPARAMS.mForceWindowed=FALSE;
    if(name=="window-rejected")windowAllowed=false;
    if(name=="desktop-depth-rejected")desktopDepth=false;
    if(name=="msaa-colour-rejected")windowColourSamples=false;
    if(name=="msaa-depth-rejected")windowDepthSamples=false;
    if(name=="window-vp")rejectHardware=true;
    bool transitions=name=="mode-transitions"||name=="vp-transitions"||name=="vp-incompatible";
    if(transitions||name=="ref-window-only"||name=="physical-fullscreen"||name=="window-rejected")
        modes={{640,480,60,D3DFMT_R8G8B8},{800,600,60,D3DFMT_X8R8G8B8}};
    if(name=="window-resize"||name=="resize-shape"||name=="msaa-colour-rejected"||name=="msaa-depth-rejected")
        modes={{640,480,60,D3DFMT_X8R8G8B8},{800,600,60,D3DFMT_X8R8G8B8}};
    if(transitions)formatCaps=true;
    if(name=="vp-transitions"||name=="vp-incompatible")vpPolicy=name=="vp-incompatible"?2:1;
    if(name=="desktop-unavailable")desktopAvailable=false;
    if(name=="ref-window-only")refWindowOnly=true;
    if(name=="mode-capacity")for(unsigned i=0;i<150;++i)modes.push_back({640+4*i,480+3*i,60,D3DFMT_X8R8G8B8});
    Application app;
    app.m_bUseDepthBuffer=TRUE; // The actual game shell enables depth before Create.
    std::vector<D3DModeInfo> physicalModes;
    if(transitions)
    {
        app.GetCurrentDevice()->dwCurrentMode=0;
        app.m_pD3D=&d3d;
        if(SUCCEEDED(app.BuildDeviceList()))
        {
            auto *d=app.GetCurrentDevice();physicalModes.assign(d->modes,d->modes+d->dwNumModes);
        }
        app.m_pD3D=nullptr;
    }
    if(name=="explicit-client")app.SetResolution(800,600);
    if(name=="client-shape")app.SetResolution(800,500);
    if(name=="client-width-limit")app.SetResolution(65536,480);
    if(name=="client-height-limit")app.SetResolution(640,32768);
    if(name=="client-zero")app.SetResolution(0,480);
    if(name=="client-negative")app.SetResolution(-1,480);
    bool refused=name=="fullscreen-only"||name=="window-rejected"||name=="desktop-depth-rejected"||
        name=="desktop-unavailable"||name=="client-width-limit"||name=="client-height-limit"||name=="client-zero"||name=="client-negative";
    HRESULT result=app.Create(nullptr);
    if(refused)
    {
        check(FAILED(result),"unusable requested domain is refused");
        check(createCount==0,"refused admission does not call CreateDevice");
    }
    else
    {
        check(SUCCEEDED(result),"requested presentation domain starts");
        if(SUCCEEDED(result))
        {
            auto *d=app.GetCurrentDevice();
            check(d->dwCurrentMode<d->dwNumModes&&d->dwNumModes<=150,"legacy current-mode reader is bounded");
            check(app.m_pd3dDevice->adapter==(skipPrimary?1u:0u),"physical adapter ordinal is retained");
            check(app.m_pd3dDevice->type==d->DeviceType,"native device type matches selected provider");
            if(name=="physical-fullscreen")
            {
                check(!app.m_bWindowed&&last.BackBufferWidth==640&&last.BackBufferHeight==480,"real 4:3 fullscreen control");
                check(last.BackBufferFormat==D3DFMT_R8G8B8,"fullscreen uses its physical format");
            }
            else
            {
                unsigned width=name=="explicit-client"||name=="client-shape"?800:640;
                unsigned height=name=="client-shape"?500:name=="explicit-client"?600:480;
                check(app.m_bWindowed&&last.Windowed,"actual creation is windowed");
                check(last.BackBufferWidth==width&&last.BackBufferHeight==height,"requested client dimensions stay exact");
                check(last.BackBufferFormat==desktop.Format&&last.AutoDepthStencilFormat==D3DFMT_D24X8,"desktop colour and validated depth are used");
                check(app.mScreenShapeScale==(4.f/3.f)*(float(height)/width)&&!wide,"faithful projection formula does not enable wide modes");
                ActualWriteKey(app);
                check(gDisplayModeKey.mWidth==width&&gDisplayModeKey.mHeight==height&&gDisplayModeKey.m32Bit==1,"actual Career writer persists client, not physical desktop");
                check(d->modes[d->dwCurrentMode].Width==width&&d->modes[d->dwCurrentMode].Height==height,"legacy mode readers see client dimensions");
                bool trueCheck=false,falseCheck=false,wrongPhysical=false;
                for(auto c:typeChecks){trueCheck|=c.windowed&&c.colour==desktop.Format;falseCheck|=!c.windowed;wrongPhysical|=c.adapter!=(skipPrimary?1u:0u);}
                check(trueCheck&&falseCheck&&!wrongPhysical,"TRUE and FALSE capability domains use physical ordinal independently");
                bool colourSample=false,depthSample=false;
                for(auto c:sampleChecks)if(c.windowed){colourSample|=c.format==desktop.Format;depthSample|=c.format==D3DFMT_D24X8;}
                check(colourSample,"windowed colour MSAA is queried with TRUE");
                if(name!="msaa-colour-rejected")check(depthSample,"windowed depth MSAA is queried with TRUE");
                check(((d->modes[d->dwCurrentMode].mUnknown10>>5)&31u)==
                    ((windowColourSamples&&windowDepthSamples)?31u:0u),"published MSAA bits use both windowed colour and depth");
            }
            if(name=="window-vp")check(app.m_dwCreateFlags==D3DCREATE_MIXED_VERTEXPROCESSING,"windowed VP rejection follows existing fallback order");
            if(name=="desktop-only"||name=="desktop-secondary")check(d->dwNumModes==1&&modes.size()==1&&modes[0].Width==1280,"window-only descriptor does not add an enumerated physical mode");
            if(name=="window-only-escape")
            {
                unsigned before=resetCount;
                check(FAILED(app.ToggleFullscreen())&&FAILED(app.ForceWindowed(FALSE)),"all fullscreen transitions refuse the window-only domain");
                check(resetCount==before&&app.m_bWindowed,"refusal preserves the current window/device");
                app.m_bWindowed=FALSE;
                check(FAILED(app.Initialize3DEnvironment(true))&&resetCount==before,"direct fullscreen initialization cannot select the client descriptor");
                check(FAILED(app.Resize3DEnvironment())&&resetCount==before,"direct fullscreen resize cannot select the client descriptor");
                app.m_bWindowed=TRUE;
            }
            if(transitions)
            {
                if(name=="mode-transitions")
                {
                    // Select while windowed through the actual PauseMenu assignment, before host Reset.
                    check(SUCCEEDED(ActualSelectMode(app,1)),"actual menu selects a different slot while windowed");
                    check(d->modes[0].Format==D3DFMT_X8R8G8B8&&d->modes[0].Width==800,"old slot is restored before publishing selected slot");
                    ActualWriteKey(app);check(gDisplayModeKey.mWidth==640,"menu selection cannot persist a wide desktop");
                    check(SUCCEEDED(app.ToggleFullscreen()),"windowed to physical fullscreen reset");
                    check(physicalModes.size()==d->dwNumModes&&
                          memcmp(physicalModes.data(),d->modes,physicalModes.size()*sizeof(D3DModeInfo))==0,
                          "both physical records restore byte-exactly after slot 0 to 1 publication");
                    check(d->modes[0].Width==800&&d->modes[0].Height==600&&d->modes[0].Format==D3DFMT_X8R8G8B8&&d->modes[0].mUnknown10==1023,
                          "physical slot zero metadata restored");
                    check(d->modes[1].Width==640&&d->modes[1].Height==480&&d->modes[1].Format==D3DFMT_R8G8B8&&d->modes[1].mUnknown10==1022,
                          "physical slot one metadata restored");
                    gDisplayModeKey={1234,900,1};ActualReadKey(app);
                    check(d->dwCurrentMode==0&&gDisplayModeKey.mWidth==0,"actual Career reader clamps missing saved choice");
                }
                else
                {
                    check(app.m_dwCreateFlags==D3DCREATE_SOFTWARE_VERTEXPROCESSING,"desktop VP chosen independently");
                    // Select the R8 physical slot; its preferred VP differs from the already-created native device.
                    d->dwCurrentMode=1;
                    unsigned before=resetCount;
                    HRESULT full=app.ToggleFullscreen();
                    if(name=="vp-incompatible")check(FAILED(full)&&resetCount==before,"incompatible existing VP is refused before Reset");
                    else
                    {
                        check(SUCCEEDED(full),"compatible existing VP survives format transition");
                        check(app.m_dwCreateFlags==app.m_pd3dDevice->creationFlags&&app.m_dwCreateFlags==D3DCREATE_SOFTWARE_VERTEXPROCESSING,"Reset never changes recorded native VP flags");
                        check(SUCCEEDED(app.ToggleFullscreen())&&createCount==1&&resetCount==2,"return to desktop validates actual native creation flags");
                    }
                }
            }
            if(name=="window-resize"||name=="resize-shape")
            {
                unsigned height=name=="resize-shape"?500:600;
                client={0,0,800,static_cast<LONG>(height)};app.MsgProc(app.m_hWnd,WM_EXITSIZEMOVE,0,0);
                ActualWriteKey(app);
                check(app.m_bReady&&resetCount==1&&last.BackBufferWidth==800&&last.BackBufferHeight==height,"actual window resize resets to client size");
                check(d->modes[d->dwCurrentMode].Width==800&&d->modes[d->dwCurrentMode].Height==height&&gDisplayModeKey.mWidth==800&&gDisplayModeKey.mHeight==height,"resize updates current metadata and actual Career key");
                check(app.mScreenShapeScale==(4.f/3.f)*(float(height)/800),"resize refreshes faithful projection compensation");
            }
            if(name=="ref-window-only")
            {
                app.m_bWindowed=FALSE;d->bWindowed=FALSE;attempts={E_FAIL};unsigned before=createCount;
                check(FAILED(app.Initialize3DEnvironment(false)),"fullscreen failure cannot fallback to window-only REF");
                check(createCount==before+1&&app.GetCurrentDevice()->DeviceType==D3DDEVTYPE_HAL,"failed fullscreen does not select REF client proxy");
            }
            if(name=="mode-capacity")check(d->dwNumModes==150,"window descriptor publication never appends past 150 physical slots");
        }
    }
    app.Cleanup3DEnvironment();
    if(name=="window-rebuild"||name=="failed-hal-slot"||name=="owner-reuse")
    {
        CLIPARAMS.mForceWindowed=FALSE;modes={{640,480,60,D3DFMT_X8R8G8B8}};
        if(name=="failed-hal-slot")rejectHal=true;
        if(name=="owner-reuse"){app.~Application();new(&app)Application;app.m_bUseDepthBuffer=TRUE;}
        result=app.Create(nullptr);check(SUCCEEDED(result),"rebuild/factory address reuse discards window-only state");
        if(SUCCEEDED(result))check(!app.m_bWindowed&&last.Windowed==FALSE&&last.BackBufferWidth==640&&
            app.GetCurrentDevice()->DeviceType==(rejectHal?D3DDEVTYPE_REF:D3DDEVTYPE_HAL),"reused logical slot has only its new provider/domain");
        app.Cleanup3DEnvironment();
    }
    check(DeviceRefs()==0&&QueryRefs()==0&&d3d.refs==0,"all host/factory references released");
    std::cout<<"window-control="<<name<<" checks="<<checks<<" failures="<<failures<<" creates="<<createCount<<" resets="<<resetCount<<"\n";
    return failures?1:0;
}
int main(int argc,char **argv)
{
    if(argc==3&&std::string(argv[1])=="publication-control")return PublicationControl(argv[2]);
    if(argc==3&&std::string(argv[1])=="window-control")return WindowControl(argv[2]);
    if(argc!=2)return 2;
    std::string name=argv[1];
    bool ownership=name=="owned-reset-ref"||name=="owned-query-reset-ref"||name=="owned-reset-ref-fail"||name=="owned-failed-create";
    bool mapping=name=="physical-secondary"||name=="physical-recovery"||name=="physical-primary";
    if(ownership)countedCallbacks=true;
    if(name=="owned-query-reset-ref")CLIPARAMS.mUnknown185=true;
    if(name=="physical-secondary"||name=="physical-recovery")skipPrimary=true;
    if(name=="physical-recovery")permitWrongCreate=true;
    if(name=="owned-failed-create"){writeFailedDevice=true;attempts={E_FAIL,S_OK};}
    modes={{800,600,60,D3DFMT_X8R8G8B8},{640,480,75,D3DFMT_X8R8G8B8},{640,480,60,D3DFMT_X8R8G8B8},
        {1024,768,60,D3DFMT_X8R8G8B8},{1280,1024,60,D3DFMT_X8R8G8B8},{1920,1080,60,D3DFMT_X8R8G8B8}};
    if(name=="wide")wide=true;
    if(name=="wide-only-on")wide=true;
    if(name=="wide-only-on" || name=="wide-only-off")modes={{1920,1080,60,D3DFMT_X8R8G8B8}};
    if(name=="mixed")modes={{1024,768,60,D3DFMT_R8G8B8},{800,600,60,D3DFMT_A8R8G8B8},{640,480,60,D3DFMT_X8R8G8B8}};
    if(name=="saved")gDisplayModeKey={1024,768,1};
    if(name=="windowed")CLIPARAMS.mForceWindowed=TRUE;
    if(name=="query-reset")CLIPARAMS.mUnknown185=true;
    if(name=="vp-fallback")rejectHardware=true;
    if(name=="msaa-fallback"){SUGGEST_MULTISAMPLE_TYPE.mValue=4;attempts={E_FAIL,S_OK};}
    if(name=="lost-once")attempts={D3DERR_DEVICELOST,S_OK};
    if(name=="fallback") { TRY_LOCKABLE_BB.mValue=1; attempts={E_FAIL,S_OK}; }
    Application app;
    if(name=="saved-msaa") { app.GetCurrentDevice()->MultiSampleType=D3DMULTISAMPLE_4_SAMPLES; SUGGEST_MULTISAMPLE_TYPE.mValue=2; }
    if(name=="preset-index")app.GetCurrentDevice()->dwCurrentMode=2;
    if(name=="no-modes" || name=="wide-only-off")
    {
        if(name=="no-modes")modes.clear();
        app.m_pD3D=&d3d;
        std::cout<<"result="<<app.BuildDeviceList()<<" adapters="<<app.m_dwNumAdapters<<"\n";
        return 0;
    }
    if(name=="depth" || name=="depth16-fallback" || name=="depth-stencil" || name=="depth32")app.m_bUseDepthBuffer=TRUE;
    if(name=="depth16-fallback")blockedDepth={D3DFMT_D24X8,D3DFMT_D24S8,D3DFMT_D32};
    if(name=="depth-stencil"){app.m_dwMinDepthBits=24;app.m_dwMinStencilBits=4;}
    if(name=="depth32")app.m_dwMinDepthBits=32;
    HRESULT result=app.Create(nullptr);
    if(name!="owned-failed-create")events.clear();
    if(ownership)
    {
        if(name!="owned-failed-create")
        {
            attempts=name=="owned-reset-ref-fail"?std::deque<HRESULT>{E_FAIL,E_FAIL}:std::deque<HRESULT>{E_FAIL,S_OK};
            writeFailedDevice=name=="owned-reset-ref-fail";
            result=app.Reset3DEnvironment(TRUE,true);
        }
        if(name=="owned-reset-ref-fail")std::cout<<"failed-ready="<<app.m_bReady<<"\n";
        app.Cleanup3DEnvironment();
        std::cout<<"result="<<result<<" device-refs="<<DeviceRefs()<<" query-refs="<<QueryRefs()<<" factory-refs="<<d3d.refs
            <<" owned-output-writes="<<ownedOutputWrites<<"\n";
        std::cout<<"events=";for(auto &e:events)std::cout<<e<<",";std::cout<<"\n";return 0;
    }
    if(mapping)
    {
        if(SUCCEEDED(result)&&name=="physical-recovery") {app.m_bWindowed=TRUE;ActualRecoveryDisplayMode(app);}
        std::cout<<"result="<<result<<" logical="<<app.m_dwAdapter<<" creates=";for(auto a:createdAdapters)std::cout<<a<<",";
        std::cout<<" recovery="<<(displayAdapters.empty()?99:displayAdapters.back());
#if !defined(PUBLIC_GRAPHICS_BEFORE)
        std::cout<<" invalid-map="<<PortPublicAdapterOrdinal(19);
#endif
        std::cout<<"\n";app.Cleanup3DEnvironment();return 0;
    }
    if(name=="reset")result=app.Reset3DEnvironment(TRUE,true);
    if(name=="reset-retry"){attempts={E_FAIL,S_OK};result=app.Reset3DEnvironment(TRUE,true);}
    if(name=="query-reset")result=app.Reset3DEnvironment(TRUE,false);
    if(name=="paused-reset"){app.m_bFrameMoving=FALSE;result=app.Reset3DEnvironment(TRUE,false);}
    if(name=="picker")result=app.Reset3DEnvironment(FALSE,false);
    if(name=="close")app.MsgProc(app.m_hWnd,WM_CLOSE,0,0);
    if(name=="minimize") { app.MsgProc(app.m_hWnd,WM_SIZE,SIZE_MINIMIZED,0); std::cout<<"minimized="<<app.m_bActive<<"\n";app.MsgProc(app.m_hWnd,WM_SIZE,SIZE_RESTORED,0); }
    if(name=="pause") { app.Pause(TRUE);app.Pause(TRUE);app.Pause(FALSE);std::cout<<"nested-ready="<<app.m_bReady<<"\n";app.Pause(FALSE); }
    if(name=="resize") { app.m_bWindowed=TRUE;client={0,0,900,700};app.MsgProc(app.m_hWnd,WM_EXITSIZEMOVE,0,0); }
    if(name=="cleanup")app.Cleanup3DEnvironment();
    D3DDeviceInfo *d=app.GetCurrentDevice();
    std::cout<<"result="<<result<<" active="<<app.m_bActive<<" ready="<<app.m_bReady<<" windowed="<<app.m_bWindowed
        <<" adapters="<<app.m_dwNumAdapters<<" devices="<<app.m_Adapters[0].dwNumDevices<<" modes="<<d->dwNumModes
        <<" selected="<<d->dwCurrentMode<<" presets="<<d->dwLowDetailMode<<","<<d->dwMediumDetailMode<<","<<d->dwHighDetailMode
        <<" size="<<app.m_d3dpp.BackBufferWidth<<"x"<<app.m_d3dpp.BackBufferHeight<<" depth="<<app.m_d3dpp.AutoDepthStencilFormat
        <<" samples="<<app.m_d3dpp.MultiSampleType<<" flags="<<app.m_dwCreateFlags
        <<" caps="<<d->modes[d->dwCurrentMode].mUnknown10<<" creates="<<createCount<<" resets="<<resetCount<<" quits="<<quitCount
        <<" aspect="<<app.mScreenShapeScale<<"\n";
    std::cout<<"events=";for(auto &e:events)std::cout<<e<<",";std::cout<<"\n";
}
