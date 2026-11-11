// SPDX-License-Identifier: GPL-3.0-only
// Actual runtime window transaction against an authored SDL boundary; no window or driver is opened.
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using HWND=void *;
using SDL_WindowID=unsigned;
using SDL_WindowFlags=unsigned long long;
using Uint64=uint64_t;
struct SDL_DisplayMode { unsigned displayID;int w,h;float refresh_rate; };
struct SDL_Window {
    unsigned id=7;int x=90,y=70,w=1600,h=900;float density=1.25f;
    bool fullscreen=true,relative=true,focused=true,minimized=false;bool modeSet=false;SDL_DisplayMode mode{};
} mainWindow,otherWindow;
constexpr SDL_WindowFlags SDL_WINDOW_FULLSCREEN=1;
constexpr SDL_WindowFlags SDL_WINDOW_MINIMIZED=2,SDL_WINDOW_INPUT_FOCUS=4;
static bool available=true,visible=false,hasPosition=true;
static SDL_Window *owned=&mainWindow;
static HWND handle=(HWND)1;
static HWND gMouseWindow=handle;
static Uint64 ticks=10000;
static std::map<std::string,int> calls;
static std::map<std::string,std::vector<int>> failures;
static int errors;
static bool hit(const char *name) {
    int n=++calls[name];for(int at:failures[name])if(n==at)return false;return true;
}
SDL_Window *PortSdlWindow(HWND h){return h==handle?owned:nullptr;}
SDL_WindowID SDL_GetWindowID(SDL_Window *w){return w->id;}
bool SDL_GetWindowSize(SDL_Window *w,int *x,int *y){*x=w->w;*y=w->h;return hit("get-size");}
bool SDL_GetWindowPosition(SDL_Window *w,int *x,int *y){*x=w->x;*y=w->y;return hasPosition;}
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *w){return (w->fullscreen?SDL_WINDOW_FULLSCREEN:0)|(w->minimized?SDL_WINDOW_MINIMIZED:0)|(w->focused?SDL_WINDOW_INPUT_FOCUS:0);}
bool SDL_CursorVisible(){return visible;}
bool SDL_GetWindowRelativeMouseMode(SDL_Window *w){return w->relative;}
const SDL_DisplayMode *SDL_GetWindowFullscreenMode(SDL_Window *w){return w->modeSet?&w->mode:nullptr;}
float SDL_GetWindowPixelDensity(SDL_Window *w){return w->density;}
bool SDL_SetWindowFullscreen(SDL_Window *w,bool on){if(!hit("fullscreen"))return false;w->fullscreen=on;return true;}
bool SDL_SetWindowSize(SDL_Window *w,int x,int y){if(!hit("size"))return false;w->w=x;w->h=y;return true;}
bool SDL_SetWindowPosition(SDL_Window *w,int x,int y){if(!hit("position"))return false;w->x=x;w->y=y;return true;}
bool SDL_SetWindowFullscreenMode(SDL_Window *w,const SDL_DisplayMode *m){if(!hit("mode"))return false;w->modeSet=m!=nullptr;if(m)w->mode=*m;return true;}
bool SDL_SyncWindow(SDL_Window *){return hit("sync");}
bool SDL_SetWindowRelativeMouseMode(SDL_Window *w,bool on){if(!hit("relative"))return false;w->relative=on;return true;}
bool SDL_ShowCursor(){if(!hit("cursor"))return false;visible=true;return true;}
bool SDL_HideCursor(){if(!hit("cursor"))return false;visible=false;return true;}
extern "C" int CE_DisplayModeAvailable(){return available;}
constexpr int PORT_SESSION_VIDEO=1,PORT_SESSION_WINDOW=2;
#define PORT_LOG_PREFIX "fixture: "
bool PortDinputMouseAcquired(){return gMouseWindow!=nullptr;}
Uint64 SDL_GetTicks(){return ticks;}
const char *SDL_GetError(){return "authored refusal";}
bool PortDevLog(){return false;}
void PortSessionLog(int,bool error,const char *,...){errors+=error;}
static unsigned gBoxW=1920,gBoxH=1080;
static SDL_Window *gBoxWindow=&mainWindow;
// window.cpp's pointer hold (input_lifetime_tests' window-mode-pointer cases run the real one): counted here.
static int holdsBefore,holdsAfter;
void PortHoldPointerForWindowChange(bool after){(after?holdsAfter:holdsBefore)++;}
#include "runtime_display_window.inc"
static void check(bool yes,const char *why){if(!yes){fprintf(stderr,"FAIL: %s\n",why);exit(2);}}
static void checkOriginal(){
    check(mainWindow.w==1600&&mainWindow.h==900&&mainWindow.fullscreen,"original size/fullscreen restored");
    check(mainWindow.relative&&!visible,"original relative input/cursor restored");
    check(gBoxW==1920&&gBoxH==1080&&gBoxWindow==&mainWindow,"original mouse mapping restored");
}
static int writes(){int n=0;for(auto &p:calls)if(p.first!="get-size")n+=p.second;return n;}
int main(int argc,char **argv){
    if(argc!=2)return 3;std::string what=argv[1];
    if(what=="windowed"){
        check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"windowed preparation accepted");
        check(holdsBefore==1&&holdsAfter==0,"pointer held before the window changes");
        check(!mainWindow.fullscreen&&mainWindow.w==1536&&mainWindow.h==864,"pixel resolution converted to logical size");
        mainWindow.relative=false;visible=true;gBoxW=gBoxH=0;gBoxWindow=nullptr;
        check(PortCeDisplayWindowReady()==1&&ceDisplayWindow.active,"input restored before settings commit, snapshot retained");
        check(PortCeDisplayWindowEnd(1)==1,"commit accepted");
        check(holdsBefore==1&&holdsAfter==1,"pointer hold extended at commit");
        check(mainWindow.relative&&visible&&!mainWindow.fullscreen,"relative input preserved and new presentation retained");
        check(gBoxW==0&&gBoxH==0,"committed mouse mapping retained");
    }else if(what=="fullscreen"){
        mainWindow.fullscreen=false;
        check(PortCeDisplayWindowBegin(handle,0,1920,1080)==1,"fullscreen preparation accepted");
        check(writes()==0,"fullscreen device hook owns transition");
        mainWindow.fullscreen=true;
        check(PortCeDisplayWindowReady()==1,"fullscreen input ready");
        check(PortCeDisplayWindowEnd(1)==1&&mainWindow.fullscreen,"fullscreen commit retained");
        check(holdsBefore==1&&holdsAfter==1,"fullscreen switch holds the pointer before and after");
    }else if(what=="rollback"){
        check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"rollback setup");
        mainWindow.relative=false;visible=true;gBoxW=gBoxH=0;gBoxWindow=nullptr;
        check(PortCeDisplayWindowEnd(0)==1,"rollback accepted");checkOriginal();
        check(holdsBefore==1&&holdsAfter==1,"rollback keeps the pointer hold");
    }else if(what=="mode-copy"){
        mainWindow.modeSet=true;mainWindow.mode={77,1600,900,60.f};
        check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"mode setup");
        mainWindow.mode.w=640;
        check(PortCeDisplayWindowEnd(0)==1,"mode rollback");
        check(mainWindow.modeSet&&mainWindow.mode.w==1600,"fullscreen mode snapshot copied");checkOriginal();
    }else if(what=="nested"){
        check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"first transaction");
        int n=writes();check(PortCeDisplayWindowBegin(handle,0,640,480)==0&&writes()==n,"nested request refused before writes");
        check(holdsBefore==1,"a refused nested request does not renew the hold");
        check(PortCeDisplayWindowEnd(0)==1,"outer snapshot retained");checkOriginal();
    }else if(what=="replacement-id"||what=="replacement-pointer"){
        check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"replacement setup");
        if(what=="replacement-id")mainWindow.id++;else owned=&otherWindow;
        check(PortCeDisplayWindowReady()==0,"replacement refused before settings commit");
        int n=writes();check(PortCeDisplayWindowEnd(0)==0&&writes()==n,"replacement window never changed");
        check(!ceDisplayWindow.active&&errors==1,"replacement refusal finalized and logged");
    }else if(what=="prepare-fullscreen"||what=="prepare-size"||what=="prepare-sync"){
        const char *key=what=="prepare-fullscreen"?"fullscreen":what=="prepare-size"?"size":"sync";
        failures[key]={1};check(PortCeDisplayWindowBegin(handle,1,1920,1080)==0,"preparation refusal");
        check(calls[key]>=2,"failed SDL boundary and restoration both exercised");checkOriginal();
        check(!ceDisplayWindow.active&&errors==1,"preparation failure closed and logged");
    }else if(what=="prepare-rollback-fails"){
        failures["size"]={1,2};check(PortCeDisplayWindowBegin(handle,1,1920,1080)==-1,"failed restoration distinguished");
        check(!ceDisplayWindow.active&&errors==1,"failed restoration closed and logged");
    }else if(what=="relative-commit-fails"){
        check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"relative setup");mainWindow.relative=false;
        failures["relative"]={1};check(PortCeDisplayWindowReady()==0&&errors==1,"relative-mode failure reported before commit");
        check(ceDisplayWindow.active,"input refusal retains rollback snapshot");
        check(PortCeDisplayWindowEnd(0)==1,"input refusal can restore the prior presentation");checkOriginal();
    }else if(what=="position-unavailable"){
        hasPosition=false;check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"position not required");
        check(PortCeDisplayWindowEnd(0)==1&&calls["position"]==0,"unsupported positioning not attempted");checkOriginal();
    }else if(what=="position-compositor"){
        failures["position"]={1};check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"compositor setup");
        check(PortCeDisplayWindowEnd(0)==1,"compositor-owned position not a display failure");checkOriginal();
    }else if(what.rfind("input-",0)==0){
        bool enable=what=="input-enable"||what=="input-owner-release";
        if(what=="input-zero-clock")ticks=0;
        mainWindow.relative=enable;gMouseWindow=enable?handle:nullptr;
        check(PortCeDisplayWindowBegin(handle,1,1920,1080)==1,"input recovery setup");
        mainWindow.relative=!enable;failures["relative"]={1};
        check(PortCeDisplayWindowReady()==0&&ceDisplayInputRetry.active,"refused input retains pending repair");
        check(PortCeDisplayInputReady()==0,"same-mode persistence sees pending input repair");
        check(PortCeDisplayWindowEnd(1)==1&&!mainWindow.fullscreen,"valid new presentation retained");
        if(what=="input-replaced-id"||what=="input-replaced-pointer"){
            if(what=="input-replaced-id")mainWindow.id++;else owned=&otherWindow;
            int n=writes();ticks+=1000;PortCeRetryDisplayInput();
            check(writes()==n&&!ceDisplayInputRetry.active,"old repair cannot touch a replacement window");
        }else if(what=="input-cancel"){
            available=false;int n=writes();PortCeRetryDisplayInput();
            check(!ceDisplayInputRetry.active&&writes()==n,"cancelled CE path drops repair without writes");
        }else if(what=="input-owner-release"){
            gMouseWindow=nullptr;FixturePump();
            check(!ceDisplayInputRetry.active&&!mainWindow.relative&&calls["relative"]==1,"release supersedes earlier acquired-input target");
        }else if(what=="input-owner-acquire"){
            gMouseWindow=handle;ticks+=1000;FixturePump();
            check(!ceDisplayInputRetry.active&&mainWindow.relative&&calls["relative"]==1,"new acquisition supersedes earlier menu release");
        }else if(what=="input-enable"){
            FixturePump();check(mainWindow.relative&&!ceDisplayInputRetry.active&&calls["relative"]==2,"normal acquired-input owner restores capture and clears repair");
        }else if(what=="input-minimized"){
            mainWindow.minimized=true;ticks+=1000;FixturePump();
            check(ceDisplayInputRetry.active&&calls["relative"]==1,"minimized window waits for an active input pump");
            mainWindow.minimized=false;FixturePump();check(!mainWindow.relative&&!ceDisplayInputRetry.active,"unminimized menu releases capture");
        }else if(what=="input-disable"||what=="input-zero-clock"){
            ticks+=999;FixturePump();check(calls["relative"]==1&&ceDisplayInputRetry.active,"release retry waits one second");
            ticks++;FixturePump();check(!mainWindow.relative&&!ceDisplayInputRetry.active&&calls["relative"]==2,"normal pump restores menu pointer without device reset");
        }else if(what=="input-clock-back"){
            ticks--;FixturePump();check(!mainWindow.relative&&!ceDisplayInputRetry.active&&calls["relative"]==2,"backward clock permits one recovery attempt");
        }else if(what=="input-repeated-failure"){
            failures["relative"]={1,2,3};
            for(int i=0;i<2;i++){
                ticks+=1000;FixturePump();int n=calls["relative"];
                for(int j=0;j<50;j++)FixturePump();
                check(calls["relative"]==n&&ceDisplayInputRetry.active,"refused retries stay bounded and pending");
            }
            ticks+=1000;FixturePump();check(!mainWindow.relative&&!ceDisplayInputRetry.active&&calls["relative"]==4,"eventual release clears pending repair");
        }else return 3;
        check(!mainWindow.fullscreen&&mainWindow.w==1536&&mainWindow.h==864,"input repair never changes the accepted window mode");
        check(PortCeDisplayInputReady()==1,"retired input repair admits later persistence");
    }else if(what=="invalid"){
        check(PortCeDisplayWindowReady()==0&&PortCeDisplayWindowEnd(0)==0,"no transaction");
        available=false;check(PortCeDisplayWindowBegin(handle,1,1920,1080)==0,"faithful fence");available=true;
        check(PortCeDisplayWindowBegin((HWND)2,1,1920,1080)==0,"foreign handle");
        check(PortCeDisplayWindowBegin(handle,2,1920,1080)==0,"invalid mode");
        check(PortCeDisplayWindowBegin(handle,1,0,1080)==0,"empty width");
        check(PortCeDisplayWindowBegin(handle,1,1920,0)==0,"empty height");
        check(PortCeDisplayWindowBegin(handle,1,65536,1080)==0,"oversized width");
        mainWindow.id=0;check(PortCeDisplayWindowBegin(handle,1,1920,1080)==0,"missing SDL identity");mainWindow.id=7;
        for(float d:{0.f,-1.f,NAN,INFINITY}){mainWindow.density=d;check(PortCeDisplayWindowBegin(handle,1,1920,1080)==0,"invalid density");}
        check(writes()==0&&!ceDisplayWindow.active,"invalid requests have no SDL writes");
        check(holdsBefore==0&&holdsAfter==0,"refused requests never hold the pointer");
    }else return 3;
    puts("window transaction pass");return 0;
}
