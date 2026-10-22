// SPDX-License-Identifier: GPL-3.0-only
// port/include/DXUtil.h: the declarations the game takes from the DirectX 9 SDK sample framework's DXUtil.h (not
// redistributable, and absent from MinGW), with the SDK's names and signatures. The bodies the game uses are its
// own src/dxutil.cpp. Both targets.
#ifndef PORT_DXUTIL_H
#define PORT_DXUTIL_H
#include <windows.h>
#include <tchar.h>

#define SAFE_DELETE(p) { if (p) { delete (p); (p) = NULL; } }
#define SAFE_DELETE_ARRAY(p) { if (p) { delete[] (p); (p) = NULL; } }
#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

enum TIMER_COMMAND { TIMER_RESET, TIMER_START, TIMER_STOP, TIMER_ADVANCE, TIMER_GETABSOLUTETIME, TIMER_GETAPPTIME,
					 TIMER_GETELAPSEDTIME };
FLOAT __stdcall DXUtil_Timer(TIMER_COMMAND command);

HRESULT DXUtil_ConvertAnsiStringToWideCch(WCHAR *wstrDestination, const CHAR *strSource, int cchDestChar);
HRESULT DXUtil_ConvertWideStringToAnsiCch(CHAR *strDestination, const WCHAR *wstrSource, int cchDestChar);
#define DXUtil_ConvertAnsiStringToWide(d, s, n) DXUtil_ConvertAnsiStringToWideCch(d, s, n)
#define DXUtil_ConvertWideStringToAnsi(d, s, n) DXUtil_ConvertWideStringToAnsiCch(d, s, n)
#endif
