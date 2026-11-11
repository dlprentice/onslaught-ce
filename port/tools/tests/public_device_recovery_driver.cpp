// SPDX-License-Identifier: GPL-3.0-only
// Reuse the existing counted device/window boundary; all recovery methods and shell branches are production code.
#define main PreviousHostMain
#include "public_d3dapp_driver.cpp"
#undef main
#undef LT
#include "public_device_recovery_session.h"

// The integrated d3dapp.cpp and shell read display-mode and lockstep state this fixture does not model.
extern "C" int CE_DisplayModeUseSavedSize(){return 0;}
int PortWideStartSizeOnDesktop(unsigned,unsigned,int,unsigned,unsigned,unsigned *,unsigned *){return 0;}
extern "C" int CE_LockstepRequested(void){return 0;}
extern "C" int CE_LockstepActive(void){return 0;}

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
#include "public_device_recovery_shell.h"

struct RecoveryApplication : Application
{
    bool nested=false,inCallback=false;
    unsigned releases=0,inits=0,nestedChecks=0;
    void ReentrantControl()
    {
        if(!nested||inCallback)return;
        inCallback=true;
        if(RecoverLostDevice()!=D3DERR_DEVICELOST||Resize3DEnvironment()!=D3DERR_DEVICELOST||
           Reset3DEnvironment(TRUE,false)!=D3DERR_DEVICELOST||ForceWindowed(TRUE)!=D3DERR_DEVICELOST||
           ToggleFullscreen()!=D3DERR_DEVICELOST)throw std::runtime_error("reentrant recovery admitted");
        ++nestedChecks;inCallback=false;
    }
    HRESULT ReleaseDeviceObjects(bool progress) override
    {++releases;ReentrantControl();return Application::ReleaseDeviceObjects(progress);}
    HRESULT InitDeviceObjects() override
    {++inits;ReentrantControl();return Application::InitDeviceObjects();}
};

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
        if(name=="resize")CLIPARAMS.mForceWindowed=TRUE;
        countedCallbacks=true;
        modes={{640,480,60,D3DFMT_X8R8G8B8},{800,600,60,D3DFMT_X8R8G8B8}};
        RecoveryApplication app;
        Require(SUCCEEDED(app.Create(nullptr)),"initial device creation");
        const unsigned original=app.m_pd3dDevice->identifier;
        events.clear();
        bool ordinary=name!="faithful";
        const auto step=[&]{return ActualRecoveryShell(app,ordinary);};
        unsigned checks=0;
        const auto check=[&](bool value,const char *why){++checks;Require(value,why);};
        if(name=="presentation-caller-retry"||name=="presentation-caller-null-device")
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
            s.requested=true;s.stage=RUNNING;
            cooperativeLevel=S_OK;
            if(name=="net-wmquit")
            {
                Queue(app,WM_QUIT);
                check(step()==FALSE&&serviceQuit==QT_QUIT_TO_SYSTEM,"real WM_QUIT retains genuine system quit");
                check(serviceObservations.empty()&&cooperativeQueries==0,"WM_QUIT precedes device work and service");
            }
            else if(name=="net-inactive")
            {
                app.m_bActive=FALSE;serviceWindowReasons=CE_SESSION_MINIMIZED;
                for(unsigned n=0;n<100;++n)
                {serviceNow+=10;check(step()==TRUE,"inactive active session keeps servicing");}
                check(getReads==0&&peekReads==100&&cooperativeQueries==0,"network inactivity never blocks or touches device");
                check(serviceObservations.size()==100&&servicePolls==100&&s.native_unready,
                      "actual service observes every inactive wait");
                serviceQuit=QT_USER_QUIT_TO_FRONTEND;
                check(step()==FALSE&&serviceObservations.size()==101,"genuine session quit reaches inactive outer caller");
            }
            else if(name=="net-shared-wait")
            {
                s.host=true;s.shared_wait=true;s.wait_phase=WAIT_HELD;
                check(step()==TRUE&&serviceObservations.size()==1&&serviceObservations.back().result==CE_SESSION_WAIT,
                      "zero clock delta cannot bypass actual shared wait");
                check(servicePolls==1&&serviceAvailability==1&&serviceWaits==1,"shared wait still performs bounded service");
                s.shared_wait=false;
                check(step()==FALSE&&serviceObservations.size()==2,"released shared wait permits healthy game work");
            }
            else if(name=="net-admission-error")
            {
                serviceAdmission=false;
                check(step()==FALSE&&serviceQuit==QT_USER_QUIT_TO_FRONTEND&&serviceFailures==1,
                      "actual service failure exits even on zero clock delta");
            }
            else if(name=="net-recovery"||name=="net-close"||name=="net-device-sticky")
            {
                cooperativeLevel=D3DERR_DEVICENOTRESET;
                unsigned losses=name=="net-recovery"?8:1;
                attempts.assign(losses,D3DERR_DEVICELOST);attempts.push_back(S_OK);
                check(step()==TRUE&&app.DeviceRecoveryPending()&&!app.m_bReady,"network reset starts deferred recovery");
                check(serviceObservations.size()==1&&serviceObservations.back().ready==0&&
                      serviceObservations.back().device==2&&s.device_unready,"first unavailable observation reaches service");
                if(name=="net-close")
                {
                    Queue(app,WM_CLOSE);
                    check(step()==FALSE&&quitCount==1&&serviceObservations.size()==2,"close reaches actual session quit service");
                    check(resetCount==1,"close does not attempt another reset");
                }
                else
                {
                    if(name=="net-device-sticky")
                    {
                        app.m_bActive=FALSE;serviceWindowReasons=CE_SESSION_MINIMIZED;
                        check(step()==TRUE&&serviceObservations.back().device==0&&s.device_unready,
                              "unknown inactive device cannot clear previously observed loss");
                        Queue(app,WM_SIZE,SIZE_RESTORED);serviceWindowReasons=0;
                        check(step()==TRUE&&serviceObservations.back().device==0&&s.device_unready,
                              "focus restore alone cannot publish device readiness");
                    }
                    if(name=="net-recovery")
                    {
                        cooperativeLevel=D3DERR_DEVICELOST;
                        for(unsigned n=0;n<3;++n)check(step()==TRUE,"lost cooperative state keeps servicing network");
                        check(resetCount==1,"lost cooperative state cannot reset");
                        cooperativeLevel=D3DERR_DEVICENOTRESET;
                    }
                    for(unsigned n=0;n<losses;++n)check(step()==TRUE,"network pending recovery is one service step");
                    check(!app.DeviceRecoveryPending()&&app.m_bReady&&!s.device_unready&&s.device_ready_seen,
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
                s.requested=false;
                check(step()==FALSE&&serviceObservations.size()==1,"dynamic session retirement restores offline path");
                s.requested=true;s.stage=OFF;
                check(step()==FALSE&&serviceObservations.size()==1,"OFF stage is not an active session");
            }
            for(unsigned ms:sleepDurations)check(ms==10,"network suspension uses only ten-millisecond boundary yields");
            unsigned waiting=0;
            for(auto observation:serviceObservations)if(observation.result==CE_SESSION_WAIT)++waiting;
            // A successful reset deliberately cancels its own frame, with one additional bounded yield.
            unsigned recovered=(name=="net-recovery"||name=="net-device-sticky")?1:0;
            check(sleepDurations.size()==waiting+recovered,"network wait yields exactly once per canceled service step");
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
        const unsigned beforeCleanup=app.releases;
        const bool pending=app.DeviceRecoveryPending();
        app.nested=false;
        app.Cleanup3DEnvironment();
        check(!app.DeviceRecoveryPending(),"cleanup clears pending owner");
        if(pending)check(app.releases==beforeCleanup,"pending cleanup never releases already released callbacks twice");
        check(DeviceRefs()==0&&QueryRefs()==0&&d3d.refs==0,"all references released");
        std::cout<<name<<" checks="<<checks<<" resets="<<resetCount<<" creates="<<createCount
                 <<" releases="<<app.releases<<" inits="<<app.inits<<" quits="<<quitCount<<"\n";
        return 0;
    }
    catch(const std::exception &error){std::cerr<<"control failed: "<<error.what()<<"\n";return 1;}
}
