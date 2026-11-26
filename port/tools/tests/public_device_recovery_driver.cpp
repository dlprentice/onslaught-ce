// SPDX-License-Identifier: GPL-3.0-only
// Reuse the existing counted device/window boundary; all recovery methods and shell branches are production code.
// The shell function runs whole (public_device_recovery_tests.py writes its text, with the pad poll, the release
// chain and the pad state they share, into public_device_recovery_shell.h): nothing of it is cut away.
#define main PreviousHostMain
#include "public_d3dapp_driver.cpp"
#undef main
#undef LT
#include "public_device_recovery_session.h"

// The integrated d3dapp.cpp and shell read display-mode and lockstep state this fixture does not model.
extern "C" int CE_DisplayModeUseSavedSize(){return 0;}
int PortWideStartSizeOnDesktop(unsigned,unsigned,int,unsigned,unsigned,unsigned *,unsigned *){return 0;}
extern "C" int CE_LockstepRequested(void){return 0;}

namespace {
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; };
MSG lastMessage{};
std::deque<MSG> messages;
const UINT WM_QUIT=18, PM_REMOVE=1;
BOOL PeekMessage(MSG *out,HWND,UINT,UINT,UINT)
{
    ++peekReads;
    if(messages.empty())return FALSE;
    *out=messages.front();messages.pop_front();events.push_back("message");return TRUE;
}
BOOL GetMessage(MSG *out,HWND h,UINT a,UINT b)
{
    ++getReads;
    if(messages.empty())throw std::runtime_error("authored inactive GetMessage has no queued message");
    return PeekMessage(out,h,a,b,PM_REMOVE)&&out->message!=WM_QUIT;
}
int TranslateAccelerator(HWND,void *,MSG *){return 0;}
BOOL TranslateMessage(MSG *){return FALSE;}
LRESULT DispatchMessage(MSG *msg){return SendMessage(msg->hwnd,msg->message,msg->wParam,msg->lParam);}
}

struct RecoveryApplication : Application
{
    bool nested=false,nestedDuringReset=false,inCallback=false;
    unsigned releases=0,inits=0,nestedChecks=0;
    void ReentrantControl()
    {
        if((!nested&&!nestedDuringReset)||inCallback)return;
        inCallback=true;
        const unsigned resets=resetCount;
        if(RecoverLostDevice()!=D3DERR_DEVICELOST||Resize3DEnvironment()!=D3DERR_DEVICELOST||
           Reset3DEnvironment(TRUE,false)!=D3DERR_DEVICELOST||ForceWindowed(TRUE)!=D3DERR_DEVICELOST||
           ToggleFullscreen()!=D3DERR_DEVICELOST||resetCount!=resets)
            throw std::runtime_error(nested?"reentrant recovery admitted":"an entry was admitted inside a running device change");
        ++nestedChecks;inCallback=false;
    }
    HRESULT ReleaseDeviceObjects(bool progress) override
    {++releases;ReentrantControl();return Application::ReleaseDeviceObjects(progress);}
    HRESULT InitDeviceObjects() override
    {++inits;ReentrantControl();return Application::InitDeviceObjects();}
};

// ---- the game shell over that host: its own text for the message step, the pad poll and the release chain ----------
// Authored here: the pad devices and their factory, the device-object lists' members, the wait screen (it presents a
// frame, as the game's does, through the real frame-end function), and the part of InitDeviceObjects that is not
// the pad state (the device-object walk and the pad enumeration). Everything named Real* and HandlingSystemStuff,
// UpdateJoystick and FreeDirectInput is the patched ltshell.cpp's text.
#define MAX_JOYPADS 4
#define MEMTYPE_UNKNOWN 0
#define SAFE_DELETE(p) do { if (p) { delete (p); (p) = NULL; } } while (0)
#define ASSERT(x) do { if (!(x)) throw std::runtime_error("shell assertion"); } while (0)
using UBYTE=unsigned char;
void *operator new(size_t size,int){return ::operator new(size);}
const HRESULT DIERR_INPUTLOST=static_cast<HRESULT>(0x8007001eu);
struct DIJOYSTATE2 { LONG lX,lY,lZ,lRx,lRy,lRz,rglSlider[2]; DWORD rgdwPOV[4]; BYTE rgbButtons[128]; LONG more[24]; };
namespace {
unsigned padPolls,padReads,particleFrees,waitScreens,objectsAliveAtReset,releasesInsideRelease;
bool waitScreenOpen,insideShellRelease,shellOrdinary=true;
unsigned authoredPads=1;	// fewer pads than slots: the keyboard-and-mouse player's one placeholder
int sKeysRead[32],*spKeysRead=sKeysRead;
bool sShowLoadingProgress=true;
struct PadDevice
{
    unsigned refs=1;
    HRESULT Poll(){++padPolls;return S_OK;} HRESULT Acquire(){return S_OK;} HRESULT Unacquire(){return S_OK;}
    HRESULT GetDeviceState(size_t size,void *state){++padReads;memset(state,0,size);static_cast<DIJOYSTATE2 *>(state)->rgbButtons[2]=0x80;return S_OK;}
    unsigned Release(){if(!refs)throw std::runtime_error("pad double release");return --refs;}
};
struct PadFactory { unsigned refs=0; unsigned Release(){if(!refs)throw std::runtime_error("input factory double release");return --refs;} } padFactory;
std::vector<std::unique_ptr<PadDevice>> padDevices;
struct DeviceObject
{
    DeviceObject *mNext=nullptr;
    bool made=false,restored=false;
    HRESULT InitDeviceObjects(){made=true;return S_OK;}
    HRESULT RestoreDeviceObjects(){restored=true;return S_OK;}
    HRESULT InvalidateDeviceObjects(){restored=false;return S_OK;}
    HRESULT DeleteDeviceObjects(){made=false;return S_OK;}
};
struct CPARTICLETEXTURE { static void FreeAllSurfaces(){++particleFrees;} };
struct ShellConsole { bool mRenderingLoadingScreen=false; void RenderLoadingScreen(BOOL,char); } shellConsole;
}
HRESULT IDirect3DDevice9::SetIndices(void *){volatile unsigned *touch=&refs;*touch=*touch;return S_OK;}	// uses the device, as the real call does
HRESULT IDirect3DDevice9::SetStreamSource(UINT,void *,UINT,UINT){volatile unsigned *touch=&refs;*touch=*touch;return S_OK;}

struct ShellApplication : RecoveryApplication
{
    void *mhAccel=nullptr;
    DIJOYSTATE2 *mJoyState[MAX_JOYPADS]{},*mOldJoyState[MAX_JOYPADS]{};
    PadDevice *pJoystick[MAX_JOYPADS]{};
    PadFactory *pDI=nullptr;
    int mJoypads=0;
    BOOL mUnknown33474[MAX_JOYPADS]={TRUE,TRUE,TRUE,TRUE},mJoypadIsNewtype[MAX_JOYPADS]{},mD3DActive=FALSE;
    DeviceObject *mDeviceObjects=nullptr,*mFirstDeviceObjects=nullptr,objects[3];
    ShellApplication(){objects[0].mNext=&objects[1];mDeviceObjects=&objects[0];mFirstDeviceObjects=&objects[2];}
    // ltshell.cpp's text (public_device_recovery_shell.h)
    BOOL HandlingSystemStuff(bool first);
    HRESULT UpdateJoystick(int joypad,bool first);
    VOID FreeDirectInput();
    HRESULT RealInvalidateDeviceObjects();
    HRESULT RealDeleteDeviceObjects();
    HRESULT RealReleaseDeviceObjects(bool show_progress);
    void RealInitPadState();
    void D3D_SetTexture(int,void *){volatile unsigned *touch=&m_pd3dDevice->refs;*touch=*touch;}
    HRESULT InvalidateDeviceObjects() override{Application::InvalidateDeviceObjects();return RealInvalidateDeviceObjects();}
    HRESULT DeleteDeviceObjects() override{Application::DeleteDeviceObjects();return RealDeleteDeviceObjects();}
    HRESULT ReleaseDeviceObjects(bool progress) override
    {
        if(insideShellRelease)++releasesInsideRelease;
        RecoveryApplication::ReleaseDeviceObjects(progress);
        const bool outer=!insideShellRelease;insideShellRelease=true;
        HRESULT result=RealReleaseDeviceObjects(progress);
        if(outer){insideShellRelease=false;objectsAliveAtReset+=ObjectsRestored();}	// what a device reset would now find alive
        return result;
    }
    HRESULT InitDeviceObjects() override
    {
        RealInitPadState();
        // authored: the walk of both device-object lists, each object made and restored behind a wait screen
        for(DeviceObject *list:{mFirstDeviceObjects,mDeviceObjects})
        {
            for(DeviceObject *o=list;o;o=o->mNext){o->InitDeviceObjects();shellConsole.RenderLoadingScreen(0,2);}
            for(DeviceObject *o=list;o;o=o->mNext){o->RestoreDeviceObjects();shellConsole.RenderLoadingScreen(0,2);}
        }
        // authored: the pad enumeration; the count is the test's, and never four
        pDI=&padFactory;++padFactory.refs;mJoypads=static_cast<int>(authoredPads);
        for(int i=0;i<mJoypads;++i){padDevices.emplace_back(new PadDevice);pJoystick[i]=padDevices.back().get();}
        return RecoveryApplication::InitDeviceObjects();
    }
    unsigned ObjectsRestored(){unsigned n=0;for(auto &o:objects)n+=o.restored;return n;}
    bool PadStateReleased(){for(int i=0;i<MAX_JOYPADS;++i)if(mJoyState[i]||mOldJoyState[i])return false;return true;}
};
static ShellApplication *shell;
#define LT (*flipTarget)
#define CONSOLE shellConsole
#define mMsg lastMessage
#define sPortQuitRequested (shellOrdinary && quitCount)
extern "C" void PortCeDeleteGpuCaches(void) {}
#include "public_device_recovery_shell.h"
#undef sPortQuitRequested
#undef mMsg
#undef CONSOLE
// The wait screen: the real one's own re-entry guard, then a presented frame. Its time throttle is left out (every
// call draws, the worst case for nesting), and so is the message pump of its modes below 2, which no device callback uses.
void ShellConsole::RenderLoadingScreen(BOOL,char)
{
    if(mRenderingLoadingScreen)return;
    mRenderingLoadingScreen=true;++waitScreens;PLATFORM.Flip(FALSE);mRenderingLoadingScreen=false;
}

// ---- the Window mode row and its frame-end request: ce_video_options.cpp's text over authored menu and window ends ----
using WCHAR=wchar_t; using EPCText=int; enum EMemoryType { MenuMemory=0x80 };
void *operator new(size_t size,EMemoryType){return ::operator new(size);}
struct CMenuItem { virtual ~CMenuItem(){} };
struct CMenuMenuItem : CMenuItem
{
    int mSelection=-1,mValue=-1;
    CMenuMenuItem(EPCText,int,bool){}
    virtual WCHAR *GetText()=0; virtual int GetNumChoices()=0; virtual WCHAR *GetChoiceText(int)=0;
    virtual int GetValue()=0; virtual void SetValue(int)=0;
    void Refresh(){mSelection=mValue=GetValue();}
};
namespace {
struct WindowRequest { int windowed; unsigned width,height; bool insideRelease; unsigned resetsBefore; };
std::vector<WindowRequest> windowRequests;
unsigned windowEnds,windowCommits,modeStores;
}
extern "C" int CE_DisplayModeAvailable(){return 1;}
extern "C" int CE_DisplayModeChoice(int current){return current;}
extern "C" int CE_DisplayModeStore(int){++modeStores;return 1;}
extern "C" int PortCeDisplayWindowBegin(HWND,int windowed,unsigned width,unsigned height)
{
    windowRequests.push_back({windowed,width,height,insideShellRelease,resetCount});
    if(windowed)client={0,0,static_cast<LONG>(width),static_cast<LONG>(height)};	// the platform sizes the window to the render size
    return 1;
}
extern "C" int PortCeDisplayWindowReady(){return 1;}
extern "C" int PortCeDisplayInputReady(){return 1;}
extern "C" int PortCeDisplayWindowEnd(int commit){++windowEnds;windowCommits+=commit!=0;return 1;}
extern "C" unsigned PortPublicRefusedDeviceChanges(void) __attribute__((weak));	// absent from a host older than the nesting fix
#include VIDEO_SOURCE
CMenuItem *PortCeDisplayModeItem();
#undef LT
// A pad read as the game's controllers make it (ltshell.h's accessors): every slot the shell says has a pad.
static unsigned ReadPads(ShellApplication &app)
{
    unsigned held=0;
    for(int pad=0;pad<app.mJoypads;++pad)
        for(int button=0;button<32;++button)held+=app.mJoyState[pad]->rgbButtons[button]!=0||app.mOldJoyState[pad]->rgbButtons[button]!=0;
    return held;
}

static void Require(bool value,const char *message)
{if(!value)throw std::runtime_error(message);}
static void Queue(CD3DApplication &app,UINT message,WPARAM value=0)
{messages.push_back({app.m_hWnd,message,value,0});}

int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    try
    {
        std::string name=argv[1];
        const bool network=name.compare(0,4,"net-")==0;
        elapsedAppTime=name=="zero-elapsed"||name=="zero-healthy"||name=="initial-cooperative-lost-zero"||network?0.f:0.01f;
        if(name=="resize"||name=="resize-refused")CLIPARAMS.mForceWindowed=TRUE;
        countedCallbacks=true;
        modes={{640,480,60,D3DFMT_X8R8G8B8},{800,600,60,D3DFMT_X8R8G8B8}};
        const bool apply=name.compare(0,6,"apply-")==0;
        if(name=="apply-to-fullscreen")CLIPARAMS.mForceWindowed=TRUE;
        if(name=="four-pads")authoredPads=4;
        ShellApplication app;shell=&app;flipTarget=&app;
        Require(SUCCEEDED(app.Create(nullptr)),"initial device creation");
        const unsigned original=app.m_pd3dDevice->identifier;
        events.clear();
        shellOrdinary=name!="faithful";
        const auto step=[&]{return app.HandlingSystemStuff(true);};
        unsigned checks=0;
        const auto check=[&](bool value,const char *why){++checks;Require(value,why);};
        check(app.mJoypads==static_cast<int>(authoredPads)&&app.mD3DActive&&!app.PadStateReleased(),"the shell starts with its pad state made");
        // The frame-end reset a menu row asks for, as the game makes it: the request flag, then the frame's present.
        const auto optionsReset=[&]{gDisplayResetState=1;PLATFORM.Flip(FALSE);};
        const auto refusedEntries=[&]{return PortPublicRefusedDeviceChanges?PortPublicRefusedDeviceChanges():0u;};
        if(apply)
        {
            // One Apply with the Window mode row and a row that asks for the options reset (here Screen mode: the
            // mode index the row writes, then the request every reset row makes through the one shared function).
            const bool toWindow=name!="apply-to-fullscreen";
            const bool refused=name=="apply-reset-refused";
            CMenuMenuItem *row=static_cast<CMenuMenuItem *>(PortCeDisplayModeItem());
            check(row->GetValue()==(toWindow?0:1),"the row starts on the running mode");
            unsigned width=640,height=480;
            if(toWindow)
            {
                D3DDeviceInfo *device=app.GetCurrentDevice();
                DWORD chosen=device->dwNumModes;
                for(DWORD i=0;i<device->dwNumModes;++i)if(device->modes[i].Width==800&&device->modes[i].Height==600)chosen=i;
                check(chosen<device->dwNumModes&&chosen!=device->dwCurrentMode,"a different Screen mode is listed");
                device->dwCurrentMode=chosen;width=800;height=600;
            }
            row->SetValue(toWindow?1:0);
            if(refused)resetFails=createFails=true;	// every device the host can try refuses
            const unsigned resetsBefore=resetCount;
            optionsReset();
            if(refused)
            {
                check(windowRequests.empty()&&!app.m_bReady&&app.DeviceRecoveryPending(),"a refused options reset starts no window switch and is owned");
                check(row->GetValue()==1,"the choice stays queued");
                cooperativeLevel=D3DERR_DEVICELOST;
                if(app.m_pd3dDevice)check(step()==TRUE&&quitCount==0&&windowRequests.empty(),"while the device is lost the shell waits, without a frame");
            }
            else
            {
                check(windowRequests.size()==1,"one window request for one Apply");
                check(!windowRequests[0].insideRelease&&releasesInsideRelease==0,"the window switch does not run inside the options reset's release");
                check(windowRequests[0].resetsBefore==resetsBefore+1,"the window switch follows the options reset");
                check(windowRequests[0].width==width&&windowRequests[0].height==height,"the window is prepared from the mode the options reset installed");
                check(resetCount==resetsBefore+2,"two device resets: the options reset, then the window switch");
                check(objectsAliveAtReset==0,"no device object is still restored when a release ends");
                check(app.m_bWindowed==(toWindow?TRUE:FALSE)&&app.m_bReady&&!app.DeviceRecoveryPending(),"the chosen window mode is running");
                check(last.BackBufferWidth==width&&last.BackBufferHeight==height,"at the chosen size");
                check(windowEnds==1&&windowCommits==1&&modeStores==1&&row->GetValue()==(toWindow?1:0),"committed and saved once");
                check(refusedEntries()==0,"no nested entry was attempted");
                cooperativeLevel=S_OK;
                check(step()==FALSE&&padPolls>0,"the next idle step is a frame, with the pads polled");
            }
            if(refused)
            {
                // the device comes back: the shell recovers it, and the first presented frame runs the queued request
                // (the authored refusal covered every device the host could try; a later test of the same failure
                // closing the game is options-reset-refused)
                resetFails=createFails=false;cooperativeLevel=S_OK;
                for(unsigned n=0;n<4&&app.DeviceRecoveryPending();++n)check(step()==TRUE,"recovery is an idle step");
                check(!app.DeviceRecoveryPending()&&app.m_bReady&&windowRequests.empty(),"recovered; the request waits for a frame");
                check(step()==FALSE,"a frame is admitted");
                PLATFORM.Flip(FALSE);
                check(windowRequests.size()==1&&!windowRequests[0].insideRelease&&app.m_bWindowed&&app.m_bReady,
                      "the queued window switch runs at that frame's end");
                check(windowRequests[0].width==width&&windowRequests[0].height==height,"from the recovered mode");
            }
            delete row;
        }
        else if(name=="options-reset-refused"||name=="options-reset-recovers"||name=="toggle-refused")
        {
            // A reset caller that does not (or cannot) act on a refusal: the frame-end options reset, and the
            // fullscreen toggle. The reset fails with the device objects released; the game then calls the shell.
            resetFails=createFails=true;cooperativeLevel=S_OK;	// every device the host can try refuses
            if(name=="toggle-refused")check(FAILED(app.ToggleFullscreen()),"refused toggle is reported");
            else optionsReset();
            check(!app.m_bReady&&app.PadStateReleased()&&!app.mD3DActive,"the refused reset leaves the objects released");
            check(app.DeviceRecoveryPending(),"and the host owns the failure");
            app.Pause(TRUE);app.Pause(FALSE);
            check(!app.m_bReady,"an unpause cannot admit a frame on released objects");
            if(name=="options-reset-recovers")
            {
                Queue(app,WM_SIZE,SIZE_RESTORED);
                check(step()==TRUE&&app.PadStateReleased(),"a waiting message is handled first, with no pad poll on released state");
                resetFails=createFails=false;
                check(step()==TRUE&&!app.DeviceRecoveryPending()&&app.m_bReady&&!app.PadStateReleased(),"the shell's retry restores the objects");
                check(step()==FALSE&&quitCount==0,"and the next idle step is a frame");
            }
            else
            {
                check(step()==TRUE&&quitCount==1,"the shell's retry is refused too: the game is closed");
                check(app.PadStateReleased(),"no pad was polled on released state");
            }
        }
        else if(name=="window-switch-messages-first")
        {
            // A window switch whose reset and whose rollback both fail, and the window messages the switch leaves
            // queued: the shell handles those before its first recovery attempt.
            client={0,0,640,480};resetFails=true;
            check(FAILED(app.ForceWindowed(TRUE))&&!app.m_bReady&&app.DeviceRecoveryPending(),"double failure is owned");
            check(app.PadStateReleased(),"with the objects released");
            Queue(app,WM_SIZE,SIZE_RESTORED);Queue(app,WM_SIZE,SIZE_RESTORED);
            check(step()==TRUE&&step()==TRUE&&resetCount==2,"queued window messages drain without a reset");
            resetFails=false;cooperativeLevel=S_OK;
            check(step()==TRUE&&app.m_bReady&&!app.DeviceRecoveryPending()&&!app.m_bWindowed,"then the shell restores the old presentation");
            check(step()==FALSE,"and admits a frame");
        }
        else if(name=="resize-refused")
        {
            // The resize caller invalidates without releasing: the pad state stays, the refusal closes the game.
            check(app.m_bWindowed,"windowed start");
            resetFails=true;client={0,0,900,700};Queue(app,WM_EXITSIZEMOVE);cooperativeLevel=S_OK;
            check(step()==TRUE&&quitCount==1&&!app.m_bReady,"a refused resize closes the game");
            check(!app.PadStateReleased(),"its pad state was never released");
        }
        else if(name=="reset-reentrant"||name=="four-pads")
        {
            // Every entry is refused while the options reset is between its release and its restore, and is counted.
            if(name=="reset-reentrant")app.nestedDuringReset=true;
            cooperativeLevel=S_OK;
            optionsReset();
            check(app.m_bReady&&resetCount==1,"the options reset itself completes, once");
            if(name=="reset-reentrant")
            {
                check(app.nestedChecks==2,"release and initialization reentry both refused");
                if(PortPublicRefusedDeviceChanges)check(refusedEntries()==10,"each refused entry is counted");
            }
            check(step()==FALSE&&padPolls==authoredPads,"a frame follows, with every attached pad polled");
        }
        else if(name=="presentation-caller-retry"||name=="presentation-caller-null-device")
        {
            nativeWindowOverride=true;
            app.m_d3dpp.Windowed=TRUE;
            client={0,0,640,480};
            resetFails=name=="presentation-caller-retry";
            initFails=name=="presentation-caller-null-device";
            check(FAILED(app.ForceWindowed(TRUE)),"refused presentation is reported");
            check(!app.m_bReady&&!app.m_bWindowed,"refused restoration retains logical fullscreen and blocks rendering");
            if(initFails)check(app.m_pd3dDevice==nullptr,"failed initialization leaves no device to query");
            resetFails=initFails=false;cooperativeLevel=S_OK;
            check(step()==TRUE,"actual shell services pending presentation restoration before ready work");
            check(app.m_bReady&&!app.DeviceRecoveryPending()&&!app.m_bWindowed&&app.m_pd3dDevice,
                  "actual shell restores resources and clears the fence with a healthy cooperative status");
            check(step()==FALSE,"actual shell resumes ordinary game work on the following iteration");
        }
        else if(network)
        {
            lockstepActive=true;
            cooperativeLevel=S_OK;
            if(name=="net-wmquit")
            {
                Queue(app,WM_QUIT);
                check(step()==FALSE&&serviceQuit==QT_QUIT_TO_SYSTEM,"real WM_QUIT retains genuine system quit");
                check(serviceObservations.empty()&&cooperativeQueries==0,"WM_QUIT precedes device work and service");
            }
            else if(name=="net-wmquit-released"||name=="net-exit-released")
            {
                // A quit or the session's own end while the device objects are released (the device needs a reset that
                // keeps answering "lost"): the step returns FALSE and the caller's last pass reads the pads.
                cooperativeLevel=D3DERR_DEVICENOTRESET;
                attempts.assign(8,D3DERR_DEVICELOST);attempts.push_back(S_OK);
                check(step()==TRUE&&app.DeviceRecoveryPending()&&!app.mD3DActive&&app.PadStateReleased(),
                      "the session's device reset leaves the objects released");
                if(name=="net-wmquit-released")
                {
                    Queue(app,WM_QUIT);
                    check(step()==FALSE&&serviceQuit==QT_QUIT_TO_SYSTEM,"a WM_QUIT during the session reaches the caller");
                }
                else
                {
                    serviceQuit=QT_USER_QUIT_TO_FRONTEND;
                    check(step()==FALSE&&serviceObservations.back().result==CE_SESSION_EXIT,
                          "the session's end reaches the caller");
                }
                bool made=!app.mD3DActive;
                for(int pad=0;pad<app.mJoypads;++pad)made=made&&app.mJoyState[pad]&&app.mOldJoyState[pad];
                check(made&&ReadPads(app)==0,"the caller's last pass reads idle pads while the objects are released");
            }
            else if(name=="net-inactive")
            {
                app.m_bActive=FALSE;
                for(unsigned n=0;n<100;++n)
                    check(step()==TRUE,"inactive active session keeps servicing");
                check(getReads==0&&peekReads==100&&cooperativeQueries==0,"network inactivity never blocks or touches device");
                check(serviceObservations.size()==100&&serviceObservations.back().ready==0,
                      "actual service observes every inactive wait");
                serviceQuit=QT_USER_QUIT_TO_FRONTEND;
                check(step()==FALSE&&serviceObservations.size()==101&&serviceObservations.back().result==CE_SESSION_EXIT,
                      "genuine session quit reaches inactive outer caller");
            }
            else if(name=="net-recovery"||name=="net-close")
            {
                cooperativeLevel=D3DERR_DEVICENOTRESET;
                unsigned losses=name=="net-recovery"?8:1;
                attempts.assign(losses,D3DERR_DEVICELOST);attempts.push_back(S_OK);
                check(step()==TRUE&&app.DeviceRecoveryPending()&&!app.m_bReady,"network reset starts deferred recovery");
                check(serviceObservations.size()==1&&serviceObservations.back().ready==0&&
                      serviceObservations.back().device==2&&serviceObservations.back().result==CE_SESSION_WAIT,
                      "first unavailable observation reaches service");
                if(name=="net-close")
                {
                    Queue(app,WM_CLOSE);
                    check(step()==FALSE&&quitCount==1&&serviceObservations.size()==2&&
                          serviceObservations.back().result==CE_SESSION_EXIT,"close reaches actual session quit service");
                    check(resetCount==1,"close does not attempt another reset");
                }
                else
                {
                    cooperativeLevel=D3DERR_DEVICELOST;
                    for(unsigned n=0;n<3;++n)check(step()==TRUE,"lost cooperative state keeps servicing network");
                    check(resetCount==1,"lost cooperative state cannot reset");
                    cooperativeLevel=D3DERR_DEVICENOTRESET;
                    for(unsigned n=0;n<losses;++n)check(step()==TRUE,"network pending recovery is one service step");
                    check(!app.DeviceRecoveryPending()&&app.m_bReady,
                          "successful reset explicitly restores device readiness at same service boundary");
                    check(serviceObservations.back().device==1&&serviceObservations.back().ready==1,
                          "actual success carries known device/host readiness");
                    check(app.m_pd3dDevice->identifier==original&&createCount==1&&app.releases==1&&app.inits==2,
                          "network recovery retains HAL and balances callbacks");
                    cooperativeLevel=S_OK;
                    check(step()==FALSE,"healthy later network idle permits game work");
                }
            }
            else
            {
                check(step()==FALSE&&serviceObservations.size()==1&&serviceObservations.back().device==1,
                      "healthy active session services even with zero clock delta");
                lockstepActive=false;
                check(step()==FALSE&&serviceObservations.size()==1,"dynamic session retirement restores offline path");
            }
            for(unsigned ms:sleepDurations)check(ms==10,"network suspension uses only ten-millisecond boundary yields");
            unsigned expectedSleeps=0;
            if(name=="net-inactive")expectedSleeps=100;
            else if(name=="net-wmquit-released"||name=="net-exit-released"||name=="net-close")expectedSleeps=1;
            else if(name=="net-recovery")expectedSleeps=1+3+8;
            check(sleepDurations.size()==expectedSleeps,"network wait yields exactly once per canceled service step");
        }
        else if(name=="initial-cooperative-error")
        {
            cooperativeLevel=E_FAIL;
            check(step()==TRUE&&step()==TRUE,"pre-existing initial cooperative-error loop remains canceled");
            check(!app.DeviceRecoveryPending()&&resetCount==0&&quitCount==0,
                  "initial unknown cooperative error is an unchanged limitation, not a claimed pending-recovery fix");
        }
        else if(name=="initial-cooperative-lost-zero")
        {
            cooperativeLevel=D3DERR_DEVICELOST;
            check(step()==TRUE&&!app.DeviceRecoveryPending(),"zero-delta initial loss never permits game work");
            check(resetCount==0&&app.releases==0&&sleepDurations==std::vector<unsigned>{10},
                  "offline lost-device path retains its one bounded yield");
        }
        else if(name=="no-loss"||name=="zero-healthy")
        {
            cooperativeLevel=S_OK;
            check(step()==FALSE,"healthy idle allows game work");
            check(resetCount==0&&!app.DeviceRecoveryPending(),"healthy device unchanged");
        }
        else if(name=="race-before-reset")
        {
            cooperativeLevels={D3DERR_DEVICENOTRESET,D3DERR_DEVICELOST};
            check(step()==TRUE,"new loss cancels this frame");
            check(!app.DeviceRecoveryPending()&&app.m_bReady&&app.releases==0&&resetCount==0,
                  "race before resource release preserves live state");
            check(step()==TRUE&&resetCount==1&&!app.DeviceRecoveryPending(),"later idle recovers normally");
        }
        else
        {
            unsigned losses=1;
            if(name.compare(0,5,"loss-")==0)losses=(unsigned)std::stoul(name.substr(5));
            if(name=="permanent")losses=64;
            check(losses&&losses<=64,"bounded authored loss count");
            attempts.assign(losses,D3DERR_DEVICELOST);attempts.push_back(S_OK);
            if(name=="reentrant")app.nested=true;
            check(step()==TRUE,"first transient reset cancels rendering");
            check(app.DeviceRecoveryPending()&&!app.m_bReady&&app.releases==1&&resetCount==1,
                  "transient loss retains a pending, nonrenderable owner");
            check(app.m_pd3dDevice&&app.m_pd3dDevice->identifier==original&&createCount==1,
                  "transient loss does not replace HAL");
            if(name=="cooperative-lost")
            {
                cooperativeLevel=D3DERR_DEVICELOST;
                for(unsigned n=0;n<12;++n)check(step()==TRUE,"lost cooperative state yields");
                check(resetCount==1&&app.releases==1&&app.DeviceRecoveryPending(),"no reset while cooperative level lost");
                cooperativeLevel=D3DERR_DEVICENOTRESET;
            }
            if(name=="cooperative-ok-pending")cooperativeLevel=S_OK;
            if(name=="quit-message")
            {
                Queue(app,WM_QUIT);
                unsigned before=resetCount;
                check(step()==TRUE&&lastMessage.message==WM_QUIT,"quit message retained for the outer owner");
                check(step()==TRUE&&resetCount==before,"pending recovery cannot run past WM_QUIT");
            }
            else if(name=="cooperative-error")
            {
                cooperativeLevel=E_FAIL;
                check(step()==TRUE&&quitCount==1,"permanent cooperative failure follows close path");
                check(step()==FALSE,"ordinary quit latch reaches caller even while unready");
            }
            else if(name=="permanent"||name=="close"||name=="cleanup")
            {
                if(name=="permanent")
                {
                    for(unsigned n=1;n<losses;++n)check(step()==TRUE,"bounded permanent-loss prefix");
                    check(resetCount==64&&createCount==1&&app.releases==1&&!app.m_bReady,
                          "permanent loss neither times out into REF nor renders");
                }
                if(name!="cleanup")
                {
                    Queue(app,WM_CLOSE);
                    unsigned before=resetCount;
                    check(step()==TRUE&&quitCount==1,"close dispatched before retry");
                    check(step()==FALSE&&resetCount==before,"ordinary quit consumed without another reset");
                }
            }
            else
            {
                if(name=="restore"||name=="repeated-restore"||name=="minimize")
                {
                    app.MsgProc(app.m_hWnd,WM_SIZE,SIZE_MINIMIZED,0);
                    unsigned count=name=="repeated-restore"?4:1;
                    for(unsigned n=0;n<count;++n)Queue(app,WM_SIZE,SIZE_RESTORED);
                    for(unsigned n=0;n<count;++n)check(step()==TRUE&&resetCount==1,"restore messages drain before reset");
                }
                if(name=="resize")
                {
                    client={0,0,900,700};Queue(app,WM_EXITSIZEMOVE);
                    check(step()==TRUE&&resetCount==1&&!app.m_bReady,"resize defers nested reset");
                }
                if(name=="pause")
                {
                    app.Pause(TRUE);app.Pause(FALSE);
                    check(!app.m_bReady,"pause cannot publish pending resources as ready");
                    app.Pause(TRUE);
                }
                if(name=="reset-error")attempts={E_FAIL,E_FAIL};
                if(name=="init-error")initFails=true;
                for(unsigned n=1;n<=losses;++n)check(step()==TRUE,"retry remains an idle pump step");
                if(name=="reset-error"||name=="init-error")
                {
                    check(!app.DeviceRecoveryPending()&&!app.m_bReady&&quitCount==1,
                          "genuine reset/initialization failure is not a transient pass");
                }
                else
                {
                    check(!app.DeviceRecoveryPending()&&app.m_pd3dDevice->identifier==original&&createCount==1,
                          "recovered existing HAL without REF creation");
                    check(resetCount==losses+1&&app.releases==1&&app.inits==2,"callbacks balanced across all transient attempts");
                    if(name.compare(0,5,"loss-")==0)
                    {   // every retry that still finds the device lost waits one ten-millisecond yield, in the default shell as in the network one
                        check(sleepDurations.size()==losses-1,"each still-lost retry yields exactly once");
                        for(unsigned ms:sleepDurations)check(ms==10,"the wait is the bounded ten-millisecond yield");
                    }
                    if(name=="pause")
                    {check(!app.m_bReady,"active pause survives recovery");app.Pause(FALSE);}
                    check(app.m_bReady,"restored resources become ready");
                    if(name=="resize")check(last.BackBufferWidth==900&&last.BackBufferHeight==700,
                                            "pending recovery uses latest client size");
                    if(name=="reentrant")check(app.nestedChecks==2,"release and initialization reentry both refused");
                    cooperativeLevel=S_OK;
                    check(step()==FALSE,"only a later healthy idle step permits game work");
                }
            }
        }
        if(shellOrdinary&&quitCount&&!network)
        {
            // A quit was asked for: the shell lets its caller's last pass run, and that pass reads the pads.
            check(step()==FALSE,"a requested quit reaches the caller");
            check(ReadPads(app)==0||app.mD3DActive,"the caller's last pass reads idle pads while the objects are released");
        }
        const unsigned beforeCleanup=app.releases;
        const bool pending=app.DeviceRecoveryPending();
        app.nested=app.nestedDuringReset=false;
        app.Cleanup3DEnvironment();
        check(!app.DeviceRecoveryPending(),"cleanup clears pending owner");
        if(pending)check(app.releases==beforeCleanup,"pending cleanup never releases already released callbacks twice");
        check(DeviceRefs()==0&&QueryRefs()==0&&d3d.refs==0,"all references released");
        for(int i=0;i<MAX_JOYPADS;++i){delete app.mJoyState[i];delete app.mOldJoyState[i];app.mJoyState[i]=app.mOldJoyState[i]=nullptr;}	// idle pads made for a last pass
        std::cout<<name<<" checks="<<checks<<" resets="<<resetCount<<" creates="<<createCount
                 <<" releases="<<app.releases<<" inits="<<app.inits<<" quits="<<quitCount<<"\n";
        return 0;
    }
    catch(const std::exception &error){std::cerr<<"control failed: "<<error.what()<<"\n";return 1;}
}
