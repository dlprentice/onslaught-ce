// SPDX-License-Identifier: GPL-3.0-only
// port/include/port.h: forced into every game translation unit of the native port (port/build.py). It maps the
// VC6-isms the game's sources use onto clang for Linux and llvm-mingw i686, and redirects the calls the SDL3 platform
// layer owns on both targets. Game sources are never edited for it.
#ifndef PORT_H
#define PORT_H

#if defined(__linux__)
// dxvk-native's windows_base.h defines __stdcall away (everything in the process is cdecl on Linux). It must be gone
// before any header that spells it, or a declaration seen first would be stdcall and its definition cdecl: the stack
// comes back 4 bytes off per call (found as a crash in SDL's SSE code after the port's own stdcall declaration below).
#define __stdcall
#endif

// ---- both targets -------------------------------------------------------------------------------------------------
// MemoryManager.h defines the game's operator new/delete `static inline`, which VC6 accepted at global scope: each game
// file had its own, so game code used the game's heap and NvTriStrip, the STL and the runtime the C runtime's. Patch
// 0002 makes them ordinary inline definitions marked with this, and port/build.py renames the four symbols in game
// objects to private ones (GAME_ALLOCATOR), which restores that split; nothing replaces the global operators.
#define PORT_GLOBAL_NEW
// VC6 has no array forms of operator new/delete: `new T[n]` and `delete [] p` use the game's operator new/delete above.
// C++ calls operator new[]/delete[], whose library versions would pair malloc/free with the game's heap (the first
// boot's free(): invalid size, ltshell.cpp WinMain's options buffer). Patch 0002 adds these after the game's operator
// delete; with -fno-sized-deallocation (port/build.py), every delete reaches them.
#define PORT_ARRAY_FORMS \
	inline void *operator new[](size_t size) { return operator new(size); } \
	inline void operator delete[](void *p) { operator delete(p); }
// VC6 emits an out-of-class `inline` virtual defined in one file (WorldPhysicsManager.cpp, ParticleSet.cpp, rtmesh.cpp)
// for the vtables other files emit; clang emits an inline function only where it is used, and its non-virtual thunks
// only beside a vtable that sees the body. Patch 0005 replaces `inline` on those 52 definitions (each defined only
// there) with this, so they become ordinary definitions that bring their thunks with them.
#define PORT_EMIT
// Patch 0006 calls this first in CGame::Update: the per-Update dump (port/game/updatedump.cpp), off unless asked.
#ifdef __cplusplus
void PortUpdateHook();
#endif
#define PORT_UPDATE_HOOK() PortUpdateHook()

// The game's Direct3D object comes through the port (port/platform/common/d3d9hooks.cpp: retail's x87 precision after
// CreateDevice, frame count and screenshots at Present).
struct IDirect3D9;
#ifdef __cplusplus
extern "C" struct IDirect3D9 *__stdcall PortDirect3DCreate9(unsigned int sdk);
#endif
#define Direct3DCreate9 PortDirect3DCreate9

// The registry is an ini file in the port's writable folder on both targets (port/platform/common/registry.cpp).
#define RegCreateKeyExA PortRegCreateKeyExA
#define RegOpenKeyExA PortRegOpenKeyExA
#define RegOpenKeyA PortRegOpenKeyA
#define RegQueryValueExA PortRegQueryValueExA
#define RegSetValueExA PortRegSetValueExA
#define RegCloseKey PortRegCloseKey

// The game's window, message loop and cursor are SDL3's on both targets (port/platform/common/window.cpp). The game
// spells the generic names (PeekMessage, CreateWindow); the headers map those onto these, so they come here too.
#define RegisterClassA PortRegisterClassA
#define UnregisterClassA PortUnregisterClassA
#define CreateWindowExA PortCreateWindowExA
#define DestroyWindow PortDestroyWindow
#define ShowWindow PortShowWindow
#define UpdateWindow PortUpdateWindow
#define SetActiveWindow PortSetActiveWindow
#define SetFocus PortSetFocus
#define GetFocus PortGetFocus
#define GetForegroundWindow PortGetForegroundWindow
#define SetForegroundWindow PortSetForegroundWindow
#define IsIconic PortIsIconic
#define IsZoomed PortIsZoomed
#define IsWindowVisible PortIsWindowVisible
#define DefWindowProcA PortDefWindowProcA
#define CallWindowProcA PortCallWindowProcA
#define PeekMessageA PortPeekMessageA
#define GetMessageA PortGetMessageA
#define TranslateMessage PortTranslateMessage
#define DispatchMessageA PortDispatchMessageA
#define PostMessageA PortPostMessageA
#define SendMessageA PortSendMessageA
#define PostQuitMessage PortPostQuitMessage
#define GetClientRect PortGetClientRect
#define GetWindowRect PortGetWindowRect
#define AdjustWindowRect PortAdjustWindowRect
#define AdjustWindowRectEx PortAdjustWindowRectEx
#define SetWindowPos PortSetWindowPos
#define MoveWindow PortMoveWindow
#define GetWindowLongA PortGetWindowLongA
#define SetWindowLongA PortSetWindowLongA
#define GetClassLongA PortGetClassLongA
#define SetClassLongA PortSetClassLongA
#define SetWindowTextA PortSetWindowTextA
#define SetCursor PortSetCursor
#define ShowCursor PortShowCursor
#define GetCursorPos PortGetCursorPos
#define SetCursorPos PortSetCursorPos
#define ClipCursor PortClipCursor
#define ScreenToClient PortScreenToClient
#define ClientToScreen PortClientToScreen

#if defined(_WIN32)
// ---- Windows (llvm-mingw) -------------------------------------------------------------------------------------------
// MinGW's tchar.h leaves these as imports outside _MBCS/_UNICODE; VC6's single-byte mapping is the C library's.
#include <tchar.h>
#undef _tcsicmp
#undef _tcsncpy
#undef _tcscpy
#undef _tcscat
#undef _stprintf
#define _tcsicmp _stricmp
#define _tcsncpy strncpy
#define _tcscpy strcpy
#define _tcscat strcat
#define _stprintf sprintf
// Files: the game's relative paths go through the port's two roots (port/platform/windows/files.cpp), so the player's
// install is never written. io.h first: it defines _findfirst and _findnext as macros of its own.
#include <stdio.h>
#include <stdint.h>
#include <io.h>
#include <direct.h>
#undef _findfirst
#undef _findnext
#ifdef __cplusplus
extern "C" {
#endif
FILE *port_fopen(const char *name, const char *mode);
int port_access(const char *name, int mode);
int port_mkdir(const char *name);
intptr_t port_findfirst(const char *spec, struct _finddata_t *d);
int port_findnext(intptr_t h, struct _finddata_t *d);
int port_findclose(intptr_t h);
#ifdef __cplusplus
}
#endif
#define fopen port_fopen
#define access port_access
#define _access port_access
#define _mkdir port_mkdir
#define _findfirst port_findfirst
#define _findnext port_findnext
#define _findclose port_findclose
#define CreateFileA PortCreateFileA
#define FindFirstFileA PortFindFirstFileA
#define FindNextFileA PortFindNextFileA
#define FindClose PortFindClose
#define DeleteFileA PortDeleteFileA
#define CreateDirectoryA PortCreateDirectoryA
#define GetFileAttributesA PortGetFileAttributesA
#define mmioOpenA PortMmioOpenA
// Clocks: real, or the virtual clock (BEA_PORT_CLOCK=present; port/platform/windows/clock.cpp).
#define QueryPerformanceFrequency PortQueryPerformanceFrequency
#define QueryPerformanceCounter PortQueryPerformanceCounter
#define timeGetTime PortTimeGetTime
#define GetTickCount PortGetTickCount
#define GetSystemTimeAsFileTime PortGetSystemTimeAsFileTime
#define GetSystemTime PortGetSystemTime
#define GetLocalTime PortGetLocalTime
#define Sleep PortSleep
// MinGW's windef.h leaves out min/max in C++; the SDK's (and so VC6's, and the Linux port's windows.h) define them.
#ifndef NOMINMAX
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#endif
#endif

#if defined(__linux__)
// ---- VC6 CRT names over glibc (MinGW's CRT has them) -----------------------------------------------------------------
#include <strings.h>
#include <alloca.h>
#include <ctype.h>
#include <stdint.h>
#include <wchar.h>
#include <stdio.h>
#define stricmp strcasecmp
#define _stricmp strcasecmp
#define strcmpi strcasecmp
#define _strcmpi strcasecmp
#define strnicmp strncasecmp
#define _strnicmp strncasecmp
#define _alloca alloca
#define _snprintf snprintf
#define _vsnprintf vsnprintf
// Files: the game's relative Windows paths go through the port's path layer (port/platform/linux/paths.cpp).
#define fopen port_fopen
#define access port_access
#define _access port_access
#ifdef __cplusplus
extern "C" {
#endif
char *strlwr(char *s);
char *strupr(char *s);
// <process.h>'s spawn (console.cpp starts the message helper); VC6's headers reach it through stdlib.h.
#define _P_WAIT 0
#define _P_NOWAIT 1
intptr_t _spawnl(int mode, const char *cmdname, const char *arg0, ...);
FILE *port_fopen(const char *name, const char *mode);
int port_access(const char *name, int mode);

// Wide strings: the port builds with -fshort-wchar so WCHAR and L"" are 2 bytes as on Windows, which glibc's wide
// functions (4-byte wchar_t) cannot handle. Every wide function the game calls goes to port/platform instead.
size_t port_wcslen(const wchar_t *s);
wchar_t *port_wcscpy(wchar_t *d, const wchar_t *s);
wchar_t *port_wcsncpy(wchar_t *d, const wchar_t *s, size_t n);
wchar_t *port_wcscat(wchar_t *d, const wchar_t *s);
int port_wcscmp(const wchar_t *a, const wchar_t *b);
int port_wcsicmp(const wchar_t *a, const wchar_t *b);
wchar_t *port_wcsstr(const wchar_t *h, const wchar_t *n);
int port_swprintf(wchar_t *d, const wchar_t *fmt, ...);	// MSVC's legacy form: no count, %s is a wide string
#define wcslen port_wcslen
#define wcscpy port_wcscpy
#define wcsncpy port_wcsncpy
#define wcscat port_wcscat
#define wcscmp port_wcscmp
#define _wcsicmp port_wcsicmp
#define wcsicmp port_wcsicmp
#define wcsstr port_wcsstr
#define swprintf port_swprintf
// <float.h>'s MSVC floating-point control (ltshell.cpp masks every x87 exception at start-up).
#define _MCW_EM 0x0008001f
#define _EM_INEXACT 0x00000001
#define _EM_UNDERFLOW 0x00000002
#define _EM_OVERFLOW 0x00000004
#define _EM_ZERODIVIDE 0x00000008
#define _EM_INVALID 0x00000010
#define _EM_DENORMAL 0x00080000
#define _MCW_RC 0x00000300
#define _RC_NEAR 0x00000000
#define _RC_DOWN 0x00000100
#define _RC_UP 0x00000200
#define _RC_CHOP 0x00000300
#define _MCW_PC 0x00030000
#define _PC_64 0x00000000
#define _PC_53 0x00010000
#define _PC_24 0x00020000
unsigned int _controlfp(unsigned int newval, unsigned int mask);
#ifdef __cplusplus
}
#endif
#endif

#endif
