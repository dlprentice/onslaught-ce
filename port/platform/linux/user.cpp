// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/user.cpp: the USER32, GDI32, ADVAPI32 and VERSION calls the game makes, on SDL3 (Linux).
// Windows are SDL windows: an HWND is the SDL_Window itself, as dxvk-native expects. SDL's events become the
// messages the game's window procedure (CD3DApplication::MsgProc via WndProc) handles. The sample framework's
// device dialog and menus are inert; GDI is the memory-DC subset the debug font and cursor code touch.
#include "handles.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

extern "C" BOOL WINAPI SetRect(LPRECT r, int l, int t, int rt, int b)
{
	r->left = l;
	r->top = t;
	r->right = rt;
	r->bottom = b;
	return TRUE;
}
extern "C" BOOL WINAPI SetRectEmpty(LPRECT r) { return SetRect(r, 0, 0, 0, 0); }
extern "C" BOOL WINAPI CopyRect(LPRECT d, const RECT *s) { *d = *s; return TRUE; }
extern "C" BOOL WINAPI OffsetRect(LPRECT r, int dx, int dy)
{
	r->left += dx;
	r->right += dx;
	r->top += dy;
	r->bottom += dy;
	return TRUE;
}
extern "C" BOOL WINAPI PtInRect(const RECT *r, POINT p) { return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
extern "C" BOOL WINAPI InvalidateRect(HWND, const RECT *, BOOL) { return TRUE; }
extern "C" BOOL WINAPI ValidateRect(HWND, const RECT *) { return TRUE; }
extern "C" int WINAPI GetSystemMetrics(int index)
{
	const SDL_DisplayMode *m = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay()) : NULL;
	if (index == SM_CXSCREEN)
		return m ? m->w : 1920;
	if (index == SM_CYSCREEN)
		return m ? m->h : 1080;
	return 0;
}

// ---- keyboard ---------------------------------------------------------------------------------------------------
extern "C" int WINAPI GetKeyNameTextA(LONG lParam, LPSTR out, int n)
{
	// lParam bits 16..23 are the set-1 scan code (the game passes DIK codes this way), bit 24 the extended flag.
	int scan = (lParam >> 16) & 0xff;
	bool extended = (lParam >> 24) & 1;
	static const char *const kNames[0x59] = {
		NULL, "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace", "Tab",
		"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Ctrl", "A", "S",
		"D", "F", "G", "H", "J", "K", "L", ";", "'", "`", "Shift", "\\", "Z", "X", "C", "V",
		"B", "N", "M", ",", ".", "/", "Right Shift", "Num *", "Alt", "Space", "Caps Lock", "F1", "F2", "F3", "F4", "F5",
		"F6", "F7", "F8", "F9", "F10", "Pause", "Scroll Lock", "Num 7", "Num 8", "Num 9", "Num -", "Num 4", "Num 5", "Num 6", "Num +", "Num 1",
		"Num 2", "Num 3", "Num 0", "Num Del", NULL, NULL, "\\", "F11", "F12",
	};
	const char *name = NULL;
	if (extended)
	{
		switch (scan)
		{
		case 0x1c: name = "Num Enter"; break;
		case 0x1d: name = "Right Ctrl"; break;
		case 0x35: name = "Num /"; break;
		case 0x38: name = "Right Alt"; break;
		case 0x47: name = "Home"; break;
		case 0x48: name = "Up"; break;
		case 0x49: name = "Page Up"; break;
		case 0x4b: name = "Left"; break;
		case 0x4d: name = "Right"; break;
		case 0x4f: name = "End"; break;
		case 0x50: name = "Down"; break;
		case 0x51: name = "Page Down"; break;
		case 0x52: name = "Insert"; break;
		case 0x53: name = "Delete"; break;
		}
	}
	else if (scan < 0x59)
		name = kNames[scan];
	if (!name || n <= 0)
		return 0;
	snprintf(out, n, "%s", name);
	return (int)strlen(out);
}

extern "C" SHORT WINAPI GetAsyncKeyState(int) { return 0; }
extern "C" SHORT WINAPI GetKeyState(int) { return 0; }
extern "C" UINT WINAPI MapVirtualKeyA(UINT, UINT) { return 0; }
extern "C" UINT_PTR WINAPI SetTimer(HWND, UINT_PTR id, UINT, void *) { return id; }
extern "C" BOOL WINAPI KillTimer(HWND, UINT_PTR) { return TRUE; }

// ---- dialogs, menus, accelerators, icons, message boxes ----------------------------------------------------------------
extern "C" INT_PTR WINAPI DialogBoxParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM) { return IDCANCEL; }
extern "C" BOOL WINAPI EndDialog(HWND, INT_PTR) { return TRUE; }
extern "C" HWND WINAPI GetDlgItem(HWND, int) { return NULL; }
extern "C" BOOL WINAPI EnableWindow(HWND, BOOL) { return FALSE; }
extern "C" LRESULT WINAPI SendDlgItemMessageA(HWND, int, UINT, WPARAM, LPARAM) { return 0; }
extern "C" BOOL WINAPI CheckDlgButton(HWND, int, UINT) { return TRUE; }
extern "C" UINT WINAPI IsDlgButtonChecked(HWND, int) { return BST_UNCHECKED; }
extern "C" HACCEL WINAPI LoadAcceleratorsA(HINSTANCE, LPCSTR) { return NULL; }
extern "C" int WINAPI TranslateAcceleratorA(HWND, HACCEL, LPMSG) { return 0; }
extern "C" HMENU WINAPI LoadMenuA(HINSTANCE, LPCSTR) { return NULL; }
extern "C" HMENU WINAPI GetMenu(HWND) { return NULL; }
extern "C" BOOL WINAPI SetMenu(HWND, HMENU) { return TRUE; }
extern "C" BOOL WINAPI DestroyMenu(HMENU) { return TRUE; }
extern "C" DWORD WINAPI CheckMenuItem(HMENU, UINT, UINT) { return 0; }
extern "C" BOOL WINAPI EnableMenuItem(HMENU, UINT, UINT) { return TRUE; }
extern "C" HICON WINAPI LoadIconA(HINSTANCE, LPCSTR) { return NULL; }
extern "C" HCURSOR WINAPI LoadCursorA(HINSTANCE, LPCSTR) { return NULL; }
extern "C" BOOL WINAPI GetIconInfo(HICON, PICONINFO) { return FALSE; }

extern "C" int WINAPI MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
{
	// No dialog on the desktop: the text goes to the log, and the answer is OK.
	fprintf(stderr, "bea-port: message box: %s: %s\n", caption ? caption : "", text ? text : "");
	return IDOK;
}

extern "C" BOOL WINAPI SystemParametersInfoA(UINT action, UINT, PVOID param, UINT)
{
	// The accessibility-key queries answer "off", so the game has nothing to restore.
	if (param && (action == SPI_GETSTICKYKEYS || action == SPI_GETTOGGLEKEYS || action == SPI_GETFILTERKEYS))
	{
		UINT size = *(UINT *)param;
		memset(param, 0, size);
		*(UINT *)param = size;
	}
	return TRUE;
}

// ---- GDI: memory DCs and DIB sections (the sample framework's debug font, the cursor copy) ----------------------------------
namespace
{
struct Dib
{
	int magic;
	void *bits;
};
const int kDibMagic = 0x44494221;
}

extern "C" HDC WINAPI GetDC(HWND) { return (HDC)(intptr_t)0x4443; }
extern "C" int WINAPI ReleaseDC(HWND, HDC) { return 1; }
extern "C" HDC WINAPI BeginPaint(HWND, LPPAINTSTRUCT ps) { memset(ps, 0, sizeof(*ps)); return GetDC(NULL); }
extern "C" BOOL WINAPI EndPaint(HWND, const PAINTSTRUCT *) { return TRUE; }
extern "C" HDC WINAPI CreateCompatibleDC(HDC) { return (HDC)(intptr_t)0x4444; }
extern "C" BOOL WINAPI DeleteDC(HDC) { return TRUE; }
extern "C" HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ h) { return h; }
extern "C" BOOL WINAPI DeleteObject(HGDIOBJ h)
{
	Dib *d = (Dib *)h;
	if (d && d->magic == kDibMagic)
	{
		free(d->bits);
		free(d);
	}
	return TRUE;
}
extern "C" HGDIOBJ WINAPI GetStockObject(int) { return NULL; }
extern "C" int WINAPI GetObjectA(HANDLE, int, LPVOID) { return 0; }
extern "C" int WINAPI GetDeviceCaps(HDC, int index) { return index == LOGPIXELSX || index == LOGPIXELSY ? 96 : index == BITSPIXEL ? 32 : 0; }

extern "C" HBITMAP WINAPI CreateDIBSection(HDC, const BITMAPINFO *bmi, UINT, void **bits, HANDLE, DWORD)
{
	// Blank memory: text drawn through GDI stays blank until the port draws its own (port/PLAN.md, later).
	int w = bmi->bmiHeader.biWidth, h = bmi->bmiHeader.biHeight < 0 ? -bmi->bmiHeader.biHeight : bmi->bmiHeader.biHeight;
	int bpp = bmi->bmiHeader.biBitCount ? bmi->bmiHeader.biBitCount : 32;
	Dib *d = (Dib *)calloc(1, sizeof(Dib));
	d->magic = kDibMagic;
	d->bits = calloc((size_t)((w * bpp / 8 + 3) & ~3) * h, 1);
	if (bits)
		*bits = d->bits;
	return (HBITMAP)d;
}

extern "C" int WINAPI GetDIBits(HDC, HBITMAP, UINT, UINT, LPVOID, LPBITMAPINFO, UINT) { return 0; }
extern "C" int WINAPI SetMapMode(HDC, int) { return MM_TEXT; }
extern "C" HFONT WINAPI CreateFontA(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR)
{
	return (HFONT)(intptr_t)0x464f;
}
extern "C" COLORREF WINAPI SetTextColor(HDC, COLORREF c) { return c; }
extern "C" COLORREF WINAPI SetBkColor(HDC, COLORREF c) { return c; }
extern "C" UINT WINAPI SetTextAlign(HDC, UINT a) { return a; }
extern "C" BOOL WINAPI ExtTextOutA(HDC, int, int, UINT, const RECT *, LPCSTR, UINT, const INT *) { return TRUE; }
extern "C" BOOL WINAPI GetTextExtentPoint32A(HDC, LPCSTR s, int c, LPSIZE size)
{
	// A fixed 8x16 cell, so layout code that sizes a font texture from the extents still gets sane numbers.
	size->cx = 8 * c;
	size->cy = 16;
	(void)s;
	return TRUE;
}

// ---- version information: retail's resource says 1.0.0.0 (BEA.exe 74154bfa..., VS_FIXEDFILEINFO) -----------------------
extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR, LPDWORD handle)
{
	if (handle)
		*handle = 0;
	return sizeof(VS_FIXEDFILEINFO);
}

extern "C" BOOL WINAPI GetFileVersionInfoA(LPCSTR, DWORD, DWORD len, LPVOID data)
{
	if (len < sizeof(VS_FIXEDFILEINFO))
		return FALSE;
	VS_FIXEDFILEINFO *v = (VS_FIXEDFILEINFO *)data;
	memset(v, 0, sizeof(*v));
	v->dwSignature = 0xFEEF04BD;
	v->dwStrucVersion = 0x00010000;
	v->dwFileVersionMS = 0x00010000;
	v->dwFileVersionLS = 0;
	v->dwProductVersionMS = 0x00010000;
	v->dwProductVersionLS = 0;
	v->dwFileFlagsMask = 0x3f;
	v->dwFileOS = 0x40004;
	v->dwFileType = 1;
	return TRUE;
}

extern "C" BOOL WINAPI VerQueryValueA(LPCVOID block, LPCSTR sub, LPVOID *out, PUINT len)
{
	if (!sub || strcmp(sub, "\\") != 0)
		return FALSE;
	*out = (LPVOID)block;
	*len = sizeof(VS_FIXEDFILEINFO);
	return TRUE;
}
