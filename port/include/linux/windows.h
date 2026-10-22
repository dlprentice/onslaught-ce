// SPDX-License-Identifier: GPL-3.0-only
// port/include/linux/windows.h: dxvk-native's slim Windows header plus the Win32 subset the game's sources use
// (port/PLAN.md, "API surface"), with the SDK's names, values and i686 layouts. Linux only; the Windows build uses
// MinGW's headers and the real API. Functions declared here are implemented in port/platform (or redirected there by
// port.h on both targets).
#ifndef PORT_WINDOWS_H
#define PORT_WINDOWS_H

// dxvk-native defines a two-field MEMORYSTATUS (the game reads the whole SDK structure), and DWORD/LONG as
// uint32_t/int32_t where Win32 and the game's own declarations use unsigned long/long (same size on i686, different C++
// types). Its copies are renamed while it is included; IUnknown's ULONG and HRESULT stay dxvk's, so COM overrides
// match, and the D3D9 vtables are unaffected (same sizes, no C++ names cross the library boundary).
#define MEMORYSTATUS PORT_DXVK_MEMORYSTATUS
#define DWORD PORT_DXVK_DWORD
#define LONG PORT_DXVK_LONG
#define LPLONG PORT_DXVK_LPLONG
#define LPDWORD PORT_DXVK_LPDWORD
#define COLORREF PORT_DXVK_COLORREF
#include_next <windows.h>
#undef MEMORYSTATUS
#undef DWORD
#undef LONG
#undef LPLONG
#undef LPDWORD
#undef COLORREF
typedef unsigned long DWORD;
typedef long LONG;
typedef long *LPLONG;
typedef DWORD *LPDWORD;
typedef DWORD COLORREF;
typedef COLORREF *LPCOLORREF;

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- base types --------------------------------------------------------------------------------------------------
typedef DWORD *PDWORD;
typedef WORD *LPWORD, *PWORD;
typedef BYTE *LPBYTE, *PBYTE;
typedef BOOL *LPBOOL, *PBOOL;
typedef INT *LPINT, *PINT;
typedef UINT *LPUINT, *PUINT;
typedef LONG *PLONG;
typedef ULONG *PULONG;
typedef GUID *LPGUID;
typedef const GUID *LPCGUID;
typedef char TCHAR, *LPTSTR, *PTSTR;
typedef const char *LPCTSTR, *PCTSTR;
#define TEXT(x) x
#define __TEXT(x) x
typedef float *PFLOAT;
typedef CHAR *PCHAR, *LPCH, *PCH;
typedef const CHAR *LPCCH, *PCCH;
typedef const WCHAR *LPCWCH, *PCWSTR;
typedef WCHAR *LPWCH, *PWCH;
typedef unsigned char BOOLEAN;
typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;
typedef LONG_PTR LRESULT;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef WORD ATOM;
typedef DWORD LCID;
typedef HANDLE *PHANDLE, *LPHANDLE;
typedef HANDLE HGLOBAL, HLOCAL, GLOBALHANDLE, LOCALHANDLE;
typedef int (*FARPROC)(void);
typedef DWORD ACCESS_MASK;
typedef ACCESS_MASK REGSAM;
typedef LONG LSTATUS;

#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#define WINAPIV
#define WINBASEAPI
#define WINGDIAPI
#define WINADVAPI
#define NTAPI
#define FAR
#define NEAR
#define far
#define near
#define pascal
#define IN
#define OUT
#define OPTIONAL
#define UNALIGNED
#define DECLSPEC_NORETURN __attribute__((noreturn))

#define MAKEWORD(a, b) ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w) ((BYTE)((DWORD_PTR)(w) >> 8))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))
#define GetRValue(rgb) (LOBYTE(rgb))
#define GetGValue(rgb) (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) (LOBYTE((rgb) >> 16))
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#define IS_INTRESOURCE(r) ((((ULONG_PTR)(r)) >> 16) == 0)
#ifndef NOMINMAX		// as in the SDK: port code that uses the C++ library defines NOMINMAX
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#endif

#define CopyMemory(d, s, n) memcpy((d), (s), (n))
#define MoveMemory(d, s, n) memmove((d), (s), (n))
#define FillMemory(d, n, f) memset((d), (f), (n))

DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HTASK);
DECLARE_HANDLE(HRSRC);
typedef void *HGDIOBJ;
typedef HICON HCURSOR;
typedef HKEY *PHKEY;
typedef const RECT *LPCRECT;

// ---- errors and status ---------------------------------------------------------------------------------------------
#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_HANDLE_EOF 38L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_ARENA_TRASHED 7L
#define ERROR_INVALID_BLOCK 9L
#define ERROR_BAD_FORMAT 11L
#define ERROR_INVALID_DATA 13L
#define ERROR_BAD_UNIT 20L
#define ERROR_NOT_READY 21L
#define ERROR_CRC 23L
#define ERROR_NOT_DOS_DISK 26L
#define ERROR_READ_FAULT 30L
#define ERROR_GEN_FAILURE 31L
#define ERROR_OPEN_FAILED 110L
#define ERROR_DISK_OPERATION_FAILED 1127L
#define ERROR_IO_PENDING 997L
#define E_UNEXPECTED ((HRESULT)0x8000FFFF)
#define E_ACCESSDENIED ((HRESULT)0x80070005)
#define E_HANDLE ((HRESULT)0x80070006)
#define CO_E_NOTINITIALIZED ((HRESULT)0x800401F0)
#define SEVERITY_SUCCESS 0
#define SEVERITY_ERROR 1
#define FACILITY_WIN32 7
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : ((HRESULT)(((x) & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000)))
#define HRESULT_CODE(hr) ((hr) & 0xFFFF)
#define HRESULT_FACILITY(hr) (((hr) >> 16) & 0x1fff)
#define HRESULT_SEVERITY(hr) (((hr) >> 31) & 0x1)
#define IS_ERROR(Status) ((unsigned long)(Status) >> 31 == SEVERITY_ERROR)
#define INFINITE 0xFFFFFFFF
#define STILL_ACTIVE 259

// ---- kernel: memory, time, threads and synchronisation ------------------------------------------------------------
typedef struct _MEMORYSTATUS
{
	DWORD dwLength;
	DWORD dwMemoryLoad;
	SIZE_T dwTotalPhys;
	SIZE_T dwAvailPhys;
	SIZE_T dwTotalPageFile;
	SIZE_T dwAvailPageFile;
	SIZE_T dwTotalVirtual;
	SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef SECURITY_ATTRIBUTES *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef DWORD (WINAPI *PTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);
typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;

typedef struct _SYSTEMTIME
{
	WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _FILETIME
{
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _CRITICAL_SECTION
{
	void *DebugInfo;
	LONG LockCount;
	LONG RecursionCount;
	HANDLE OwningThread;
	HANDLE LockSemaphore;
	ULONG_PTR SpinCount;
} CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

typedef LARGE_INTEGER *PLARGE_INTEGER;

#define CREATE_SUSPENDED 0x00000004
#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_IDLE (-15)

void WINAPI Sleep(DWORD dwMilliseconds);
DWORD WINAPI SleepEx(DWORD dwMilliseconds, BOOL bAlertable);
BOOL WINAPI CloseHandle(HANDLE hObject);
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName);
BOOL WINAPI SetEvent(HANDLE hEvent);
BOOL WINAPI ResetEvent(HANDLE hEvent);
#define CreateEvent CreateEventA
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName);
#define CreateMutex CreateMutexA
BOOL WINAPI ReleaseMutex(HANDLE hMutex);
DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds);
HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
						   LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId);
BOOL WINAPI GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode);
BOOL WINAPI SetThreadPriority(HANDLE hThread, int nPriority);
HANDLE WINAPI GetCurrentThread(void);
DWORD WINAPI GetCurrentThreadId(void);
DWORD WINAPI ResumeThread(HANDLE hThread);
void WINAPI ExitThread(DWORD dwExitCode);
BOOL WINAPI TerminateThread(HANDLE hThread, DWORD dwExitCode);
DECLSPEC_NORETURN void WINAPI ExitProcess(UINT uExitCode);
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
LONG WINAPI InterlockedIncrement(LONG volatile *Addend);
LONG WINAPI InterlockedDecrement(LONG volatile *Addend);
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
DWORD WINAPI GetTickCount(void);
void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime);
void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime);
void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer);
HANDLE WINAPI GetProcessHeap(void);
LPVOID WINAPI HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
BOOL WINAPI HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
#define HEAP_ZERO_MEMORY 0x00000008
DWORD WINAPI GetLastError(void);
void WINAPI SetLastError(DWORD dwErrCode);
void WINAPI OutputDebugStringA(LPCSTR lpOutputString);
#define OutputDebugString OutputDebugStringA
HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName);
#define GetModuleHandle GetModuleHandleA
DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
#define GetModuleFileName GetModuleFileNameA
int WINAPI MulDiv(int nNumber, int nNumerator, int nDenominator);
LPSTR WINAPI GetCommandLineA(void);
#define GetCommandLine GetCommandLineA

// ---- kernel: strings ------------------------------------------------------------------------------------------------
LPSTR WINAPI lstrcpyA(LPSTR lpString1, LPCSTR lpString2);
LPSTR WINAPI lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength);
LPSTR WINAPI lstrcatA(LPSTR lpString1, LPCSTR lpString2);
int WINAPI lstrlenA(LPCSTR lpString);
int WINAPI lstrcmpA(LPCSTR lpString1, LPCSTR lpString2);
int WINAPI lstrcmpiA(LPCSTR lpString1, LPCSTR lpString2);
#define lstrcpy lstrcpyA
#define lstrcpyn lstrcpynA
#define lstrcat lstrcatA
#define lstrlen lstrlenA
#define lstrcmp lstrcmpA
#define lstrcmpi lstrcmpiA
#define CP_ACP 0
#define CP_UTF8 65001
#define MB_PRECOMPOSED 0x00000001
int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr,
							   int cchWideChar);
int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr,
							   int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar);
int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);
int WINAPI wvsprintfA(LPSTR, LPCSTR, va_list arglist);
#define wsprintf wsprintfA
#define wvsprintf wvsprintfA

// ---- kernel: files --------------------------------------------------------------------------------------------------
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

typedef struct _OVERLAPPED
{
	ULONG_PTR Internal;
	ULONG_PTR InternalHigh;
	DWORD Offset;
	DWORD OffsetHigh;
	HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct _WIN32_FIND_DATAA
{
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	CHAR cFileName[MAX_PATH];
	CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;
typedef LPWIN32_FIND_DATAA LPWIN32_FIND_DATA;

HANDLE WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
						  DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
						  DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
#define CreateFile CreateFileA
BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
					 LPOVERLAPPED lpOverlapped);
BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
					  LPOVERLAPPED lpOverlapped);
DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
DWORD WINAPI SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
BOOL WINAPI GetOverlappedResult(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait);
BOOL WINAPI DeleteFileA(LPCSTR lpFileName);
#define DeleteFile DeleteFileA
BOOL WINAPI CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
#define CreateDirectory CreateDirectoryA
DWORD WINAPI GetFileAttributesA(LPCSTR lpFileName);
#define GetFileAttributes GetFileAttributesA
HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
BOOL WINAPI FindClose(HANDLE hFindFile);
#define FindFirstFile FindFirstFileA
#define FindNextFile FindNextFileA
DWORD WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
BOOL WINAPI SetCurrentDirectoryA(LPCSTR lpPathName);
#define GetCurrentDirectory GetCurrentDirectoryA
#define SetCurrentDirectory SetCurrentDirectoryA

// ---- registry (ADVAPI32) and version info ---------------------------------------------------------------------------
#define HKEY_CLASSES_ROOT ((HKEY)(ULONG_PTR)((LONG)0x80000000))
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)((LONG)0x80000001))
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)((LONG)0x80000002))
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_OPTION_VOLATILE 0x00000001
#define REG_NONE 0
#define REG_SZ 1
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_CREATED_NEW_KEY 0x00000001
LONG WINAPI RegOpenKeyA(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult);
LONG WINAPI RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
LONG WINAPI RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions, REGSAM samDesired,
							LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition);
LONG WINAPI RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
LONG WINAPI RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData);
LONG WINAPI RegCloseKey(HKEY hKey);
#define RegOpenKey RegOpenKeyA
#define RegOpenKeyEx RegOpenKeyExA
#define RegCreateKeyEx RegCreateKeyExA
#define RegQueryValueEx RegQueryValueExA
#define RegSetValueEx RegSetValueExA

typedef struct tagVS_FIXEDFILEINFO
{
	DWORD dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS, dwProductVersionMS, dwProductVersionLS;
	DWORD dwFileFlagsMask, dwFileFlags, dwFileOS, dwFileType, dwFileSubtype, dwFileDateMS, dwFileDateLS;
} VS_FIXEDFILEINFO;
DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen);
#define GetFileVersionInfoSize GetFileVersionInfoSizeA
#define GetFileVersionInfo GetFileVersionInfoA
#define VerQueryValue VerQueryValueA

// ---- user: windows, messages, input ---------------------------------------------------------------------------------
typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR (CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagWNDCLASSA
{
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HICON hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName;
	LPCSTR lpszClassName;
} WNDCLASSA, *PWNDCLASSA, *LPWNDCLASSA;
typedef WNDCLASSA WNDCLASS;

typedef struct tagMSG
{
	HWND hwnd;
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
	DWORD time;
	POINT pt;
} MSG, *PMSG, *LPMSG;

typedef struct tagMINMAXINFO
{
	POINT ptReserved;
	POINT ptMaxSize;
	POINT ptMaxPosition;
	POINT ptMinTrackSize;
	POINT ptMaxTrackSize;
} MINMAXINFO, *PMINMAXINFO, *LPMINMAXINFO;

typedef struct _ICONINFO
{
	BOOL fIcon;
	DWORD xHotspot;
	DWORD yHotspot;
	HBITMAP hbmMask;
	HBITMAP hbmColor;
} ICONINFO, *PICONINFO;

typedef struct tagPAINTSTRUCT
{
	HDC hdc;
	BOOL fErase;
	RECT rcPaint;
	BOOL fRestore;
	BOOL fIncUpdate;
	BYTE rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_NCHITTEST 0x0084
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_KEYLAST 0x0108
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_INITMENUPOPUP 0x0117
#define WM_MENUSELECT 0x011F
#define WM_ENTERIDLE 0x0121
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_MOUSELAST 0x020A
#define WM_ENTERMENULOOP 0x0211
#define WM_EXITMENULOOP 0x0212
#define WM_POWERBROADCAST 0x0218
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_CONTEXTMENU 0x007B
#define WM_USER 0x0400
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(wParam) ((short)HIWORD(wParam))

#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010

#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define SC_RESTORE 0xF120
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170

#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SIZE_MAXSHOW 3
#define SIZE_MAXHIDE 4

#define HTCLIENT 1
#define HTERROR (-2)
#define PBT_APMQUERYSUSPEND 0x0000
#define PBT_APMRESUMESUSPEND 0x0007
#define BROADCAST_QUERY_DENY 0x424D5144

#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_APPWINDOW 0x00040000L

#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define CS_OWNDC 0x0020

#define CW_USEDEFAULT ((int)0x80000000)

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10

#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)

#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)
#define GCL_HCURSOR (-12)
#define GCL_HICON (-14)
#define GCLP_HCURSOR (-12)

#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001

#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_YESNO 0x00000004L
#define MB_ICONHAND 0x00000010L
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONSTOP MB_ICONHAND
#define MB_ICONQUESTION 0x00000020L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONINFORMATION 0x00000040L
#define MB_SYSTEMMODAL 0x00001000L
#define MB_TASKMODAL 0x00002000L
#define MB_TOPMOST 0x00040000L

#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7

#define IDC_ARROW MAKEINTRESOURCE(32512)
#define IDC_WAIT MAKEINTRESOURCE(32514)
#define IDI_APPLICATION MAKEINTRESOURCE(32512)

#define CB_ADDSTRING 0x0143
#define CB_GETCOUNT 0x0146
#define CB_GETCURSEL 0x0147
#define CB_GETITEMDATA 0x0150
#define CB_RESETCONTENT 0x014B
#define CB_SETCURSEL 0x014E
#define CB_SETITEMDATA 0x0151
#define CB_ERR (-1)
#define CBN_SELCHANGE 1
#define CBN_SELENDOK 9
#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1
#define BST_UNCHECKED 0x0000
#define BST_CHECKED 0x0001

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F12 0x7B
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91

ATOM WINAPI RegisterClassA(const WNDCLASSA *lpWndClass);
#define RegisterClass RegisterClassA
BOOL WINAPI UnregisterClassA(LPCSTR lpClassName, HINSTANCE hInstance);
#define UnregisterClass UnregisterClassA
HWND WINAPI CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth,
							int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
#define CreateWindowEx CreateWindowExA
#define CreateWindowA(c, w, s, x, y, cx, cy, p, m, i, l) CreateWindowExA(0L, c, w, s, x, y, cx, cy, p, m, i, l)
#define CreateWindow CreateWindowA
BOOL WINAPI DestroyWindow(HWND hWnd);
BOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow);
BOOL WINAPI UpdateWindow(HWND hWnd);
HWND WINAPI SetActiveWindow(HWND hWnd);
HWND WINAPI SetFocus(HWND hWnd);
HWND WINAPI GetFocus(void);
HWND WINAPI GetForegroundWindow(void);
BOOL WINAPI SetForegroundWindow(HWND hWnd);
BOOL WINAPI IsIconic(HWND hWnd);
BOOL WINAPI IsZoomed(HWND hWnd);
BOOL WINAPI IsWindowVisible(HWND hWnd);
LRESULT WINAPI DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define DefWindowProc DefWindowProcA
LRESULT WINAPI CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define CallWindowProc CallWindowProcA
BOOL WINAPI PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL WINAPI TranslateMessage(const MSG *lpMsg);
LRESULT WINAPI DispatchMessageA(const MSG *lpMsg);
BOOL WINAPI PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
void WINAPI PostQuitMessage(int nExitCode);
#define PeekMessage PeekMessageA
#define GetMessage GetMessageA
#define DispatchMessage DispatchMessageA
#define PostMessage PostMessageA
#define SendMessage SendMessageA
HACCEL WINAPI LoadAcceleratorsA(HINSTANCE hInstance, LPCSTR lpTableName);
int WINAPI TranslateAcceleratorA(HWND hWnd, HACCEL hAccTable, LPMSG lpMsg);
#define LoadAccelerators LoadAcceleratorsA
#define TranslateAccelerator TranslateAcceleratorA
HMENU WINAPI LoadMenuA(HINSTANCE hInstance, LPCSTR lpMenuName);
#define LoadMenu LoadMenuA
HMENU WINAPI GetMenu(HWND hWnd);
BOOL WINAPI SetMenu(HWND hWnd, HMENU hMenu);
BOOL WINAPI DestroyMenu(HMENU hMenu);
DWORD WINAPI CheckMenuItem(HMENU hMenu, UINT uIDCheckItem, UINT uCheck);
BOOL WINAPI EnableMenuItem(HMENU hMenu, UINT uIDEnableItem, UINT uEnable);
#define MF_BYCOMMAND 0x00000000L
#define MF_ENABLED 0x00000000L
#define MF_GRAYED 0x00000001L
#define MF_UNCHECKED 0x00000000L
#define MF_CHECKED 0x00000008L
HICON WINAPI LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
#define LoadIcon LoadIconA
#define LoadCursor LoadCursorA
HCURSOR WINAPI SetCursor(HCURSOR hCursor);
int WINAPI ShowCursor(BOOL bShow);
BOOL WINAPI GetCursorPos(LPPOINT lpPoint);
BOOL WINAPI SetCursorPos(int X, int Y);
BOOL WINAPI ClipCursor(const RECT *lpRect);
BOOL WINAPI GetIconInfo(HICON hIcon, PICONINFO piconinfo);
BOOL WINAPI ScreenToClient(HWND hWnd, LPPOINT lpPoint);
BOOL WINAPI ClientToScreen(HWND hWnd, LPPOINT lpPoint);
BOOL WINAPI GetClientRect(HWND hWnd, LPRECT lpRect);
BOOL WINAPI GetWindowRect(HWND hWnd, LPRECT lpRect);
BOOL WINAPI AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu);
BOOL WINAPI AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);
BOOL WINAPI SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
BOOL WINAPI MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint);
LONG WINAPI GetWindowLongA(HWND hWnd, int nIndex);
LONG WINAPI SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong);
#define GetWindowLong GetWindowLongA
#define SetWindowLong SetWindowLongA
DWORD WINAPI GetClassLongA(HWND hWnd, int nIndex);
DWORD WINAPI SetClassLongA(HWND hWnd, int nIndex, LONG dwNewLong);
#define GetClassLong GetClassLongA
#define SetClassLong SetClassLongA
BOOL WINAPI SetWindowTextA(HWND hWnd, LPCSTR lpString);
#define SetWindowText SetWindowTextA
BOOL WINAPI SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom);
BOOL WINAPI SetRectEmpty(LPRECT lprc);
BOOL WINAPI CopyRect(LPRECT lprcDst, const RECT *lprcSrc);
BOOL WINAPI OffsetRect(LPRECT lprc, int dx, int dy);
BOOL WINAPI PtInRect(const RECT *lprc, POINT pt);
BOOL WINAPI InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase);
BOOL WINAPI ValidateRect(HWND hWnd, const RECT *lpRect);
HDC WINAPI BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint);
BOOL WINAPI EndPaint(HWND hWnd, const PAINTSTRUCT *lpPaint);
HDC WINAPI GetDC(HWND hWnd);
int WINAPI ReleaseDC(HWND hWnd, HDC hDC);
int WINAPI GetSystemMetrics(int nIndex);
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
int WINAPI MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
#define MessageBox MessageBoxA
INT_PTR WINAPI DialogBoxParamA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc,
							   LPARAM dwInitParam);
#define DialogBoxParam DialogBoxParamA
#define DialogBoxA(i, t, p, f) DialogBoxParamA(i, t, p, f, 0L)
#define DialogBox DialogBoxA
BOOL WINAPI EndDialog(HWND hDlg, INT_PTR nResult);
HWND WINAPI GetDlgItem(HWND hDlg, int nIDDlgItem);
BOOL WINAPI EnableWindow(HWND hWnd, BOOL bEnable);
LRESULT WINAPI SendDlgItemMessageA(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam);
#define SendDlgItemMessage SendDlgItemMessageA
BOOL WINAPI CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck);
UINT WINAPI IsDlgButtonChecked(HWND hDlg, int nIDButton);
int WINAPI GetKeyNameTextA(LONG lParam, LPSTR lpString, int cchSize);
#define GetKeyNameText GetKeyNameTextA
SHORT WINAPI GetAsyncKeyState(int vKey);
SHORT WINAPI GetKeyState(int nVirtKey);
UINT WINAPI MapVirtualKeyA(UINT uCode, UINT uMapType);
#define MapVirtualKey MapVirtualKeyA
UINT_PTR WINAPI SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, void *lpTimerFunc);
BOOL WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent);

// ---- accessibility keys (ltshell.cpp turns the sticky/toggle/filter-key shortcuts off while the game runs) -------------
typedef struct tagSTICKYKEYS
{
	UINT cbSize;
	DWORD dwFlags;
} STICKYKEYS, *LPSTICKYKEYS;
typedef struct tagTOGGLEKEYS
{
	UINT cbSize;
	DWORD dwFlags;
} TOGGLEKEYS, *LPTOGGLEKEYS;
typedef struct tagFILTERKEYS
{
	UINT cbSize;
	DWORD dwFlags;
	DWORD iWaitMSec;
	DWORD iDelayMSec;
	DWORD iRepeatMSec;
	DWORD iBounceMSec;
} FILTERKEYS, *LPFILTERKEYS;
#define SKF_STICKYKEYSON 0x00000001
#define SKF_HOTKEYACTIVE 0x00000004
#define SKF_CONFIRMHOTKEY 0x00000008
#define TKF_TOGGLEKEYSON 0x00000001
#define TKF_HOTKEYACTIVE 0x00000004
#define TKF_CONFIRMHOTKEY 0x00000008
#define FKF_FILTERKEYSON 0x00000001
#define FKF_HOTKEYACTIVE 0x00000004
#define FKF_CONFIRMHOTKEY 0x00000008
#define SPI_GETFILTERKEYS 0x0032
#define SPI_SETFILTERKEYS 0x0033
#define SPI_GETTOGGLEKEYS 0x0034
#define SPI_SETTOGGLEKEYS 0x0035
#define SPI_GETSTICKYKEYS 0x003A
#define SPI_SETSTICKYKEYS 0x003B
#define SPI_GETSCREENSAVEACTIVE 0x0010
#define SPI_SETSCREENSAVEACTIVE 0x0011
BOOL WINAPI SystemParametersInfoA(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni);
#define SystemParametersInfo SystemParametersInfoA

// ---- structured exceptions (ltshell.cpp installs an unhandled-exception filter) ----------------------------------------
#define EXCEPTION_MAXIMUM_PARAMETERS 15
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
typedef struct _EXCEPTION_RECORD
{
	DWORD ExceptionCode;
	DWORD ExceptionFlags;
	struct _EXCEPTION_RECORD *ExceptionRecord;
	PVOID ExceptionAddress;
	DWORD NumberParameters;
	ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct _CONTEXT CONTEXT, *PCONTEXT;
typedef struct _EXCEPTION_POINTERS
{
	PEXCEPTION_RECORD ExceptionRecord;
	PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;
typedef LONG (WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS *ExceptionInfo);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
HANDLE WINAPI GetCurrentProcess(void);
DWORD WINAPI GetCurrentProcessId(void);

// ---- GDI ------------------------------------------------------------------------------------------------------------
typedef struct tagBITMAPINFOHEADER
{
	DWORD biSize;
	LONG biWidth;
	LONG biHeight;
	WORD biPlanes;
	WORD biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG biXPelsPerMeter;
	LONG biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagRGBQUAD
{
	BYTE rgbBlue;
	BYTE rgbGreen;
	BYTE rgbRed;
	BYTE rgbReserved;
} RGBQUAD, *LPRGBQUAD;

typedef struct tagBITMAPINFO
{
	BITMAPINFOHEADER bmiHeader;
	RGBQUAD bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;

#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER
{
	WORD bfType;
	DWORD bfSize;
	WORD bfReserved1;
	WORD bfReserved2;
	DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER, *LPBITMAPFILEHEADER;
#pragma pack(pop)

typedef struct tagBITMAP
{
	LONG bmType;
	LONG bmWidth;
	LONG bmHeight;
	LONG bmWidthBytes;
	WORD bmPlanes;
	WORD bmBitsPixel;
	LPVOID bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

#define BI_RGB 0L
#define DIB_RGB_COLORS 0
#define MM_TEXT 1
#define TA_TOP 0
#define TA_LEFT 0
#define ETO_OPAQUE 0x0002
#define FW_NORMAL 400
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define ANSI_CHARSET 0
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define ANTIALIASED_QUALITY 4
#define NONANTIALIASED_QUALITY 3
#define VARIABLE_PITCH 2
#define DEFAULT_PITCH 0
#define FF_DONTCARE (0 << 4)
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define BITSPIXEL 12
#define HORZRES 8
#define VERTRES 10
#define BLACK_BRUSH 4
#define WHITE_BRUSH 0
#define GRAY_BRUSH 2

HDC WINAPI CreateCompatibleDC(HDC hdc);
BOOL WINAPI DeleteDC(HDC hdc);
HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h);
BOOL WINAPI DeleteObject(HGDIOBJ ho);
HGDIOBJ WINAPI GetStockObject(int i);
int WINAPI GetObjectA(HANDLE h, int c, LPVOID pv);
#define GetObject GetObjectA
int WINAPI GetDeviceCaps(HDC hdc, int index);
HBITMAP WINAPI CreateDIBSection(HDC hdc, const BITMAPINFO *pbmi, UINT usage, void **ppvBits, HANDLE hSection, DWORD offset);
int WINAPI GetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage);
int WINAPI SetMapMode(HDC hdc, int iMode);
HFONT WINAPI CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic,
						 DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
						 DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName);
#define CreateFont CreateFontA
COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color);
COLORREF WINAPI SetBkColor(HDC hdc, COLORREF color);
UINT WINAPI SetTextAlign(HDC hdc, UINT align);
BOOL WINAPI ExtTextOutA(HDC hdc, int x, int y, UINT options, const RECT *lprect, LPCSTR lpString, UINT c, const INT *lpDx);
#define ExtTextOut ExtTextOutA
BOOL WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl);
#define GetTextExtentPoint32 GetTextExtentPoint32A

#ifdef __cplusplus
}
#endif

// As in the SDK (without WIN32_LEAN_AND_MEAN), windows.h brings in the multimedia API.
#include <mmsystem.h>
#endif
