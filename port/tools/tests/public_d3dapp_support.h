// SPDX-License-Identifier: GPL-3.0-only
// Synthetic device and window boundaries for the public graphics-host tests. No platform library or game data.
#ifndef PUBLIC_D3DAPP_SUPPORT_H
#define PUBLIC_D3DAPP_SUPPORT_H
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <stdexcept>
using DWORD = uint32_t; using UINT = unsigned; using LONG = int32_t; using BOOL = int; using VOID = void;
using FLOAT = float; using HRESULT = int32_t; using TCHAR = char; using BYTE = unsigned char;
using HWND = void *; using HINSTANCE = void *; using HICON = void *; using HCURSOR = void *; using HBRUSH = void *;
using WPARAM = uintptr_t; using LPARAM = intptr_t; using LRESULT = intptr_t; using INT_PTR = intptr_t;
#define CALLBACK
#define TEXT(x) x
#define ToTCHAR(x) const_cast<char *>(x)
#define MAKEINTRESOURCE(x) reinterpret_cast<char *>(static_cast<intptr_t>(x))
#define LOWORD(x) ((x) & 65535)
#define FAILED(x) (static_cast<HRESULT>(x) < 0)
#define SUCCEEDED(x) (!FAILED(x))
#define ZeroMemory(p,n) memset(p,0,n)
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)
#define _tcscpy strcpy
#define _tcscat strcat
#define lstrcpy strcpy
#define lstrcat strcat
#define TRACE(...) ((void)0)
const BOOL TRUE = 1, FALSE = 0;
const HRESULT S_OK = 0, E_FAIL = static_cast<HRESULT>(0x80004005u), E_NOTIMPL = static_cast<HRESULT>(0x80004001u);
const HRESULT D3DERR_DEVICELOST = static_cast<HRESULT>(0x88760868u);
enum APPMSGTYPE { MSG_NONE, MSGERR_APPMUSTEXIT, MSGWARN_SWITCHEDTOREF };
enum D3DFORMAT { D3DFMT_UNKNOWN=0,D3DFMT_R8G8B8=20,D3DFMT_A8R8G8B8=21,D3DFMT_X8R8G8B8=22,D3DFMT_R5G6B5=23,
    D3DFMT_A1R5G5B5=25,D3DFMT_A4R4G4B4=26,D3DFMT_D16=80,D3DFMT_D15S1=73,D3DFMT_D24S8=75,D3DFMT_D24X8=77,
    D3DFMT_D24X4S4=79,D3DFMT_D32=71,D3DFMT_DXT1=100,D3DFMT_DXT2=101,D3DFMT_DXT4=103 };
enum D3DDEVTYPE { D3DDEVTYPE_HAL=1,D3DDEVTYPE_REF=2,D3DDEVTYPE_SW=3 };
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_TEST_UNSET=-1,D3DMULTISAMPLE_NONE=0,D3DMULTISAMPLE_2_SAMPLES=2,D3DMULTISAMPLE_3_SAMPLES=3,
    D3DMULTISAMPLE_4_SAMPLES=4,D3DMULTISAMPLE_6_SAMPLES=6,D3DMULTISAMPLE_8_SAMPLES=8 };
const DWORD D3DCREATE_FPU_PRESERVE=2,D3DCREATE_PUREDEVICE=16,D3DCREATE_SOFTWARE_VERTEXPROCESSING=32,
    D3DCREATE_HARDWARE_VERTEXPROCESSING=64,D3DCREATE_MIXED_VERTEXPROCESSING=128,D3DCREATE_DISABLE_DRIVER_MANAGEMENT=256;
const DWORD D3DDEVCAPS_HWTRANSFORMANDLIGHT=1,D3DDEVCAPS_PUREDEVICE=2,D3DUSAGE_DEPTHSTENCIL=2;
const int D3DRTYPE_TEXTURE=3,D3DRTYPE_SURFACE=1,D3DSWAPEFFECT_DISCARD=1,D3DSWAPEFFECT_COPY=3,
    D3DPRESENT_INTERVAL_DEFAULT=0,D3DPRESENT_INTERVAL_ONE=1,D3DPRESENT_INTERVAL_IMMEDIATE=2,
    D3DPRESENTFLAG_LOCKABLE_BACKBUFFER=1,D3DBACKBUFFER_TYPE_MONO=0,D3DQUERYTYPE_RESOURCEMANAGER=5,D3D_SDK_VERSION=32;
struct POINT { LONG x=0,y=0; };
struct RECT { LONG left=0,top=0,right=0,bottom=0; };
inline void SetRect(RECT *r,LONG l,LONG t,LONG w,LONG h) { *r={l,t,w,h}; }
struct MINMAXINFO { POINT ptReserved,ptMaxSize,ptMaxPosition,ptMinTrackSize,ptMaxTrackSize; };
struct D3DCAPS9 { DWORD DevCaps=3,MaxTextureWidth=4096,MaxTextureHeight=4096; };
struct D3DDISPLAYMODE { UINT Width=0,Height=0,RefreshRate=0; D3DFORMAT Format=D3DFMT_UNKNOWN; };
struct D3DADAPTER_IDENTIFIER9 { char Driver[128]={},Description[128]={}; struct { DWORD HighPart=0,LowPart=0; } DriverVersion;
    DWORD VendorId=0,DeviceId=0,SubSysId=0,Revision=0,WHQLLevel=0; };
struct D3DSURFACE_DESC { D3DFORMAT Format=D3DFMT_X8R8G8B8; UINT Width=640,Height=480; };
// The selected implementation logs the 32-bit parameters into a fixed buffer. Keep that wire-sized field 32-bit
// even when this synthetic executable runs on a 64-bit host; actual target compilation checks the platform types.
struct Handle32 { uint32_t value=0; Handle32 &operator=(HWND p) { value=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));return *this; } };
struct D3DPRESENT_PARAMETERS { UINT BackBufferWidth=0,BackBufferHeight=0; D3DFORMAT BackBufferFormat=D3DFMT_UNKNOWN;
    UINT BackBufferCount=0; D3DMULTISAMPLE_TYPE MultiSampleType=D3DMULTISAMPLE_NONE; DWORD MultiSampleQuality=0;
    int SwapEffect=0; Handle32 hDeviceWindow; BOOL Windowed=0,EnableAutoDepthStencil=0;
    D3DFORMAT AutoDepthStencilFormat=D3DFMT_UNKNOWN; DWORD Flags=0,FullScreen_RefreshRateInHz=0,PresentationInterval=0; };
struct IDirect3DDevice9;
struct IDirect3DQuery9 { unsigned refs=0; IDirect3DDevice9 *owner=nullptr; unsigned Release(); };
struct IDirect3DSurface9 { unsigned refs=0; IDirect3DDevice9 *owner=nullptr; HRESULT GetDesc(D3DSURFACE_DESC *); unsigned Release(); };
struct IDirect3DDevice9 {
    unsigned refs=0,identifier=0; UINT adapter=0; D3DDEVTYPE type=D3DDEVTYPE_HAL; DWORD creationFlags=0;
    HRESULT Reset(D3DPRESENT_PARAMETERS *); HRESULT GetDeviceCaps(D3DCAPS9 *);
    HRESULT GetBackBuffer(UINT,UINT,int,IDirect3DSurface9 **); HRESULT CreateQuery(int,IDirect3DQuery9 **);
    BOOL ShowCursor(BOOL); void SetCursorPosition(int,int,DWORD); unsigned GetAvailableTextureMem(); unsigned Release();
};
struct IDirect3D9 {
    unsigned refs=0;
    UINT GetAdapterCount(); HRESULT GetAdapterIdentifier(UINT,DWORD,D3DADAPTER_IDENTIFIER9 *);
    HRESULT GetAdapterDisplayMode(UINT,D3DDISPLAYMODE *); UINT GetAdapterModeCount(UINT,D3DFORMAT);
    HRESULT EnumAdapterModes(UINT,D3DFORMAT,UINT,D3DDISPLAYMODE *); HRESULT GetDeviceCaps(UINT,D3DDEVTYPE,D3DCAPS9 *);
    HRESULT CheckDeviceType(UINT,D3DDEVTYPE,D3DFORMAT,D3DFORMAT,BOOL);
    HRESULT CheckDeviceFormat(UINT,D3DDEVTYPE,D3DFORMAT,DWORD,int,D3DFORMAT);
    HRESULT CheckDeviceMultiSampleType(UINT,D3DDEVTYPE,D3DFORMAT,BOOL,D3DMULTISAMPLE_TYPE,DWORD *);
    HRESULT CheckDepthStencilMatch(UINT,D3DDEVTYPE,D3DFORMAT,D3DFORMAT,D3DFORMAT);
    HRESULT CreateDevice(UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS *,IDirect3DDevice9 **); unsigned Release();
};
using LPDIRECT3D9 = IDirect3D9 *; using LPDIRECT3DDEVICE9 = IDirect3DDevice9 *; using LPDIRECT3DSURFACE9 = IDirect3DSurface9 *;
using WNDPROC = LRESULT (*)(HWND,UINT,WPARAM,LPARAM);
struct WNDCLASS { UINT style=0; WNDPROC lpfnWndProc=nullptr; int cbClsExtra=0,cbWndExtra=0; HINSTANCE hInstance=nullptr;
    HICON hIcon=nullptr; HCURSOR hCursor=nullptr; HBRUSH hbrBackground=nullptr; const char *lpszMenuName=nullptr,*lpszClassName=nullptr; };
const UINT WM_PAINT=15,WM_CLOSE=16,WM_GETMINMAXINFO=36,WM_SIZE=5,WM_ENTERSIZEMOVE=561,WM_EXITSIZEMOVE=562,
    WM_SETCURSOR=32,WM_MOUSEMOVE=512,WM_ENTERMENULOOP=529,WM_EXITMENULOOP=530,WM_CONTEXTMENU=123,
    WM_NCHITTEST=132,WM_POWERBROADCAST=536,WM_SYSCOMMAND=274,WM_COMMAND=273;
const DWORD WS_POPUP=0x80000000u,WS_CAPTION=0xc00000,WS_SYSMENU=0x80000,WS_THICKFRAME=0x40000,WS_MINIMIZEBOX=0x20000,
    WS_VISIBLE=0x10000000;
const int SIZE_MAXHIDE=4,SIZE_MINIMIZED=1,SIZE_RESTORED=0,GWL_STYLE=-16,GCL_HCURSOR=-12,HTCLIENT=1,
    SC_MOVE=0xf010,SC_SIZE=0xf000,SC_MAXIMIZE=0xf030,SC_KEYMENU=0xf100,SC_MONITORPOWER=0xf170,SWP_SHOWWINDOW=64,
    MB_OK=0,MB_ICONERROR=16,IDI_MAIN_ICON=101,IDR_MENU=102,IDM_EXIT=40001,IDM_CHANGEDEVICE=40002,WHITE_BRUSH=0,CW_USEDEFAULT=0;
#define HWND_NOTOPMOST reinterpret_cast<HWND>(static_cast<intptr_t>(-2))
#define IDC_ARROW reinterpret_cast<char *>(32512)
HICON LoadIcon(HINSTANCE,const char *); HCURSOR LoadCursor(HINSTANCE,const char *); void *GetStockObject(int);
BOOL RegisterClass(const WNDCLASS *); BOOL AdjustWindowRect(RECT *,DWORD,BOOL); void *LoadMenu(HINSTANCE,const char *);
HWND CreateWindow(const char *,const char *,DWORD,int,int,int,int,HWND,void *,HINSTANCE,void *);
LONG GetWindowLong(HWND,int); LONG SetWindowLong(HWND,int,LONG); BOOL GetWindowRect(HWND,RECT *);
BOOL GetClientRect(HWND,RECT *); BOOL SetWindowPos(HWND,HWND,int,int,int,int,UINT); LONG GetClassLong(HWND,int);
LRESULT DefWindowProc(HWND,UINT,WPARAM,LPARAM); LRESULT SendMessage(HWND,UINT,WPARAM,LPARAM);
HCURSOR SetCursor(HCURSOR); BOOL GetCursorPos(POINT *); BOOL ScreenToClient(HWND,POINT *);
int MessageBox(HWND,const char *,const char *,UINT); void Sleep(unsigned);
inline const char *DXGetErrorString9(HRESULT) { return "synthetic error"; }
inline void OutputDebugString(const char *) {}
const int IDOK=1,IDD_SELECTDEVICE=103;
inline HINSTANCE GetModuleHandle(const char *) { return nullptr; }
inline INT_PTR PublicTestPicker(HWND,UINT,WPARAM,LPARAM) { throw std::runtime_error("inactive picker"); }
inline int DialogBoxParam(HINSTANCE,const char *,HWND,INT_PTR (*)(HWND,UINT,WPARAM,LPARAM),LPARAM) { throw std::runtime_error("inactive picker"); }
enum TIMER_COMMAND { TIMER_RESET,TIMER_START,TIMER_STOP,TIMER_ADVANCE,TIMER_GETABSOLUTETIME,TIMER_GETAPPTIME,TIMER_GETELAPSEDTIME };
FLOAT DXUtil_Timer(TIMER_COMMAND);
IDirect3D9 *Direct3DCreate9(UINT);
int PortWideModes(); int PortWideDefaultSize(unsigned *,unsigned *);
int PortWideStartSize(unsigned,unsigned,int,unsigned *,unsigned *);
struct CTweakSLONG { int mValue; CTweakSLONG(const char *,int v):mValue(v){} void Set(float f){mValue=static_cast<int>(f);} };
extern CTweakSLONG SCREEN_SHAPE,TEXTURE_RES_LOSS_SHIFT,TEXTURE_ALLOW_32_BIT;
void LoadCardId(char *);
HRESULT D3DUtil_SetDeviceCursor(LPDIRECT3DDEVICE9,HCURSOR);
struct TestCLI { int mBackBuffers=1; BOOL mForceWindowed=FALSE,mVSync=FALSE; int mResWidth=0; bool mCardID=false,mUnknown185=false; };
extern TestCLI CLIPARAMS;
struct D3DModeInfo;
struct CDisplayModeKey { DWORD mWidth:16,mHeight:15,m32Bit:1; void Set(D3DModeInfo *);
    bool operator==(CDisplayModeKey &b) { return mWidth==b.mWidth&&mHeight==b.mHeight&&m32Bit==b.m32Bit; } };
extern CDisplayModeKey gDisplayModeKey;
struct TestLog { void AddMessage(const char *,...) {} }; extern TestLog SETUP_HISTORY;
struct TestConsole { void FatalError(BOOL,int,int) { throw std::runtime_error("fatal"); } }; extern TestConsole CONSOLE;
struct TestShell { void TriggerQuit(); }; extern TestShell LT;
void SetSetupMessage(int);
#include "../../platform/public/d3dapp.h"
#endif
