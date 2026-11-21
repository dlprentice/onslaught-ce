// SPDX-License-Identifier: GPL-3.0-only
// window.cpp's PortSetWindowPos, its text, with the Windows branch compiled on the host (WINDOWS_BRANCH=1) or the
// branch every other target compiles (0), over authored SDL and Win32 ends. window_topmost_tests.py writes the text.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>
using HWND=void *; using BOOL=int; using UINT=unsigned; using LPCSTR=const char *;
#define WINAPI
const BOOL TRUE=1,FALSE=0;
const UINT SWP_NOSIZE=1,SWP_NOMOVE=2,SWP_NOZORDER=4,SWP_NOACTIVATE=0x10,SWP_SHOWWINDOW=0x40,SWP_HIDEWINDOW=0x80;
#define HWND_TOP ((HWND)0)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
struct SDL_Window { int x=0,y=0,w=0,h=0; unsigned long long flags=0; int top=31,left=8,bottom=8,right=8; bool borders=true,shown=false; };
struct SDL_Rect { int x,y,w,h; };
const unsigned long long SDL_WINDOW_FULLSCREEN=1,SDL_WINDOW_BORDERLESS=0x10;
enum PortSessionCategory { PORT_SESSION_WINDOW };
static SDL_Window game;
static SDL_Rect usable={0,0,1920,1040};
static bool displayKnown=true;
static std::vector<std::string> native,notes;
static HWND gameHandle=&game;
static bool Find(HWND h){return h==gameHandle;}
static SDL_Window *Sdl(HWND h){return h==gameHandle?&game:nullptr;}
static void SDL_SetWindowPosition(SDL_Window *w,int x,int y){w->x=x;w->y=y;}
static void SDL_SetWindowSize(SDL_Window *w,int cx,int cy){w->w=cx;w->h=cy;}
static void SDL_ShowWindow(SDL_Window *w){w->shown=true;}
static void SDL_HideWindow(SDL_Window *w){w->shown=false;}
static unsigned long long SDL_GetWindowFlags(SDL_Window *w){return w->flags;}
static bool SDL_GetWindowBordersSize(SDL_Window *w,int *t,int *l,int *b,int *r){if(!w->borders)return false;*t=w->top;*l=w->left;*b=w->bottom;*r=w->right;return true;}
static int SDL_GetDisplayForWindow(SDL_Window *){return displayKnown?1:0;}
static bool SDL_GetDisplayUsableBounds(int display,SDL_Rect *r){if(!display)return false;*r=usable;return true;}
static void PortSessionLog(PortSessionCategory,bool,const char *format,...)
{char text[200];va_list a;va_start(a,format);vsnprintf(text,sizeof text,format,a);va_end(a);notes.push_back(text);}
// the real Win32 call, reached for a window the port does not own and for the z-order part of a request
static BOOL SetWindowPos(HWND h,HWND after,int x,int y,int cx,int cy,UINT flags)
{
    char text[160];snprintf(text,sizeof text,"%s after=%ld at=%d,%d size=%dx%d flags=%x",h==gameHandle?"game":"other",(long)(intptr_t)after,x,y,cx,cy,flags);
    native.push_back(text);return TRUE;
}
#if WINDOWS_BRANCH
#define WIN32_PASSTHROUGH(call) if (!Find(h)) return call;
#else
#define WIN32_PASSTHROUGH(call)
#endif
#define CE_VERSION "window-test"
#include "window_setpos.inc"
static void Require(bool ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(2);}}
int main(int argc,char **argv)
{
    if(argc!=2)return 3;
    const std::string name=argv[1];
    const UINT handBack=SWP_SHOWWINDOW;	// the graphics host's call after a windowed reset: not-topmost, the old bounds, shown
    if(name=="hand-back")
    {
        Require(PortSetWindowPos(gameHandle,HWND_NOTOPMOST,100,80,1280,720,handBack)==TRUE,"the call succeeds");
        Require(game.x==100&&game.y==80&&game.w==1280&&game.h==720&&game.shown,"SDL still places, sizes and shows the window");
#if WINDOWS_BRANCH
        Require(native.size()==1&&native[0]=="game after=-2 at=0,0 size=0x0 flags=13","the not-topmost request reaches the window itself, moving nothing");
#else
        Require(native.empty(),"no native call outside Windows");
#endif
    }
    else if(name=="plain-move")
    {
        Require(PortSetWindowPos(gameHandle,HWND_TOP,5,6,640,480,0)==TRUE&&native.empty()&&game.x==5&&game.w==640,"a plain move makes no z-order request");
        Require(PortSetWindowPos(gameHandle,HWND_NOTOPMOST,7,8,0,0,SWP_NOZORDER|SWP_NOSIZE)==TRUE&&native.empty()&&game.x==7,"nor does a request that says to leave the z-order");
    }
    else if(name=="other-window")
    {
        int foreign=0;
        const BOOL result=PortSetWindowPos(&foreign,HWND_NOTOPMOST,1,2,3,4,handBack);
#if WINDOWS_BRANCH
        Require(result==TRUE&&native.size()==1&&native[0]=="other after=-2 at=1,2 size=3x4 flags=40","a window the port does not own goes to the system whole");
#else
        Require(result==FALSE&&native.empty(),"an unknown window is refused outside Windows");
#endif
    }
    else if(name=="switch-corner"||name=="switch-clear"||name=="no-switch"||name=="switch-borderless"||name=="switch-unknown")
    {
        // the live switch out of exclusive fullscreen: the window, now with a border, is put back where it was
        const bool during=name!="no-switch";
        if(name=="switch-borderless")game.flags=SDL_WINDOW_BORDERLESS;
        if(name=="switch-unknown")displayKnown=false;
        if(during)PortHoldPointerForWindowChange(false);
        const int askedY=name=="switch-clear"?200:0;
        Require(PortSetWindowPos(gameHandle,HWND_NOTOPMOST,0,askedY,1920,1080,handBack)==TRUE,"the call succeeds");
        if(during)PortHoldPointerForWindowChange(true);
#if WINDOWS_BRANCH
        const bool moved=name=="switch-corner";
#else
        const bool moved=false;
#endif
        Require(game.x==0&&game.y==(moved?31:askedY)&&game.w==1920&&game.h==1080,moved?"the title bar is brought onto the display":"the asked place is kept");
        Require(notes.size()==(moved?1u:0u),"one note when the place was changed, none otherwise");
        Require(PortSetWindowPos(gameHandle,HWND_NOTOPMOST,0,0,1920,1080,handBack)==TRUE&&game.y==0,"after the switch a place is applied as asked");
    }
    else return 3;
    puts("window position pass");
    return 0;
}
