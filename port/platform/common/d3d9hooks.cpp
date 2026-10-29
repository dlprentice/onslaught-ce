// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/d3d9hooks.cpp: the game's Direct3D 9 entry, on both targets. port.h renames the game's
// Direct3DCreate9 to PortDirect3DCreate9, which returns the real object (dxvk-native on Linux, the system's d3d9 on
// Windows) with these calls observed through its vtable (HookTable: a private copy on Linux, the object's own table on
// Windows; the object keeps its own implementation):
// - IDirect3D9::GetAdapterModeCount/EnumAdapterModes: the standard 4:3 modes where the desktop offers none, or with
//   widescreen modes on, the desktop's sizes (below).
// - IDirect3D9::CreateDevice: retail creates its device without D3DCREATE_FPU_PRESERVE, so Direct3D leaves the
//   creating thread's x87 unit at single precision, round to nearest, and the whole simulation runs that way. The
//   port sets the same control word itself (dxvk-native may not), and logs the word before and after the real call.
// - IDirect3DDevice9::Present: counts frames (a progress line every 300), and with BEA_PORT_SHOT=FRAME:PATH writes
//   that frame's back buffer as a binary PPM before presenting it; with BEA_PORT_QUIT=FRAME it posts WM_CLOSE to the
//   device's window at that frame (the game's own shutdown path, as when the player closes the window). In public builds,
//   a frame CE photo mode asks for (port/game/ce_photo.cpp) is read back and saved as a PNG first (ce_screenshot.cpp).
// - IDirect3DDevice9::DrawPrimitive and its three siblings: in public builds, while photo mode is on, the draw calls
//   made inside a CePhotoDrawScope (the HUD overlay, the battle line, the cockpit) do nothing (ce_photo.h, patch 0133).
#include <windows.h>
#include <d3d9.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#include <SDL3/SDL.h>

namespace
{
const int kSlots = 64;			// covers IDirect3D9Ex (20) and IDirect3DDevice9Ex (134 would be more; see Copy)
const int kDeviceSlots = 160;	// IDirect3DDevice9Ex has 134 entries

unsigned short ControlWord()
{
	unsigned short cw;
	__asm__ volatile("fnstcw %0" : "=m"(cw));
	return cw;
}

void SetControlWord(unsigned short cw) { __asm__ volatile("fldcw %0" : : "m"(cw)); }

// The vtable whose entries the caller replaces (with SetSlot), after reading the originals from it.
// Linux: the object is pointed at a private copy of its vtable (two slots before it included: offset-to-top and RTTI).
// Windows: the object's own vtable, changed in place. Windows' compatibility layer (apphelp.dll) hooks Direct3D 9
// methods (IDirect3D9::CreateDevice, IDirect3DDevice9::Reset and others) by writing its own entries into the real
// vtable, and each of its hooks finds the method it replaced by the object's current vtable pointer: an object pointed at
// a copy makes the lookup fail and the hook call address 0 (the 0.1.1 and 0.1.2 startup crash). Direct3D's own code also
// calls internal entries past the public interface through the object's vtable pointer, which a copy of the public
// entries does not hold. Keeping the pointer, and changing only the entries the port observes, leaves both working; the
// table is shared by every object of the class, which the guards below (the first original kept) already allow for.
void **HookTable(void *object, int slots)
{
	void ***vptr = (void ***)object;
	void **original = *vptr;
#ifdef _WIN32
	(void)slots;
	return original;
#else
	void **copy = (void **)malloc(sizeof(void *) * (slots + 2));
	memcpy(copy, original - 2, sizeof(void *) * (slots + 2));
	*vptr = copy + 2;
	return copy + 2;
#endif
}

// Replaces one entry of a HookTable (on Windows, a read-only page of d3d9.dll made writable for the one write).
void SetSlot(void **table, int slot, void *fn)
{
#ifdef _WIN32
	if (table[slot] == fn)
		return;
	DWORD old;
	if (!VirtualProtect(&table[slot], sizeof(void *), PAGE_READWRITE, &old))
	{
		fprintf(stderr, PORT_LOG_PREFIX "could not hook Direct3D entry %d (error %lu)\n", slot, (unsigned long)GetLastError());
		return;
	}
	table[slot] = fn;
	VirtualProtect(&table[slot], sizeof(void *), old, &old);
#else
	table[slot] = fn;
#endif
}

typedef HRESULT (STDMETHODCALLTYPE *CreateDeviceFn)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *,
													IDirect3DDevice9 **);
typedef HRESULT (STDMETHODCALLTYPE *PresentFn)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT (STDMETHODCALLTYPE *ResetFn)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
ResetFn gReset;
UINT gBoxW, gBoxH;			// the game's back buffer while it is shown desktop-fullscreen (0: not)
UINT gBackW, gBackH;		// the game's back buffer (its drawing size, LTShell::GetWindowWidth), from CreateDevice and Reset
SDL_Window *gBoxWindow;

// Fullscreen on a modern desktop: the game asks for an exclusive 4:3 mode (640x480 and up), which dxvk on Linux cannot
// switch a widescreen monitor to. As Wine does, the port keeps the desktop's mode: the swapchain becomes windowed
// at the game's resolution and its window covers the desktop, where dxvk scales the image. Offscreen (test) runs are
// left exactly as they were, and Windows keeps its exclusive modes. BEA_PORT_EXCLUSIVE=1 keeps the game's own request.
void DesktopFullscreen(HWND focus, D3DPRESENT_PARAMETERS *pp)
{
	if (!pp || pp->Windowed)
	{
		if (pp)
			gBoxW = gBoxH = 0;
		return;
	}
#ifdef _WIN32
	// Direct3D switches the display mode and expands the native window, but SDL still owns its client rectangle.
	// A borderless, nonresizable SDL window constrains that rectangle to its last SDL size (WM_NCCALCSIZE). The
	// default native mode can be larger than the initial 640x480 window, leaving visible menu buttons outside the
	// client area. Resize through SDL first so its size and the exclusive back buffer agree, on creation and reset.
	SDL_Window *native = PortSdlWindow(pp->hDeviceWindow ? pp->hDeviceWindow : focus);
	if (native && pp->BackBufferWidth && pp->BackBufferHeight)
	{
		int width = 0, height = 0;
		SDL_GetWindowSizeInPixels(native, &width, &height);
		if ((UINT)width != pp->BackBufferWidth || (UINT)height != pp->BackBufferHeight)
		{
			if (!SDL_SetWindowSize(native, (int)pp->BackBufferWidth, (int)pp->BackBufferHeight) || !SDL_SyncWindow(native))
				fprintf(stderr, PORT_LOG_PREFIX "exclusive window resize to %ux%u failed: %s\n",
						pp->BackBufferWidth, pp->BackBufferHeight, SDL_GetError());
		}
	}
	return;						// Windows' own Direct3D 9 still owns the exclusive mode switch
#endif
	const char *driver = SDL_GetCurrentVideoDriver();
	if (!driver || !strcmp(driver, "offscreen"))
		return;
	const char *exclusive = getenv("BEA_PORT_EXCLUSIVE");
	if (exclusive && *exclusive == '1')
		return;
	SDL_Window *w = PortSdlWindow(pp->hDeviceWindow ? pp->hDeviceWindow : focus);
	pp->Windowed = TRUE;
	pp->FullScreen_RefreshRateInHz = 0;
	gBoxW = pp->BackBufferWidth;
	gBoxH = pp->BackBufferHeight;
	gBoxWindow = w;
	if (w)
	{
		SDL_SetWindowFullscreenMode(w, NULL);
		SDL_SetWindowFullscreen(w, true);
		SDL_SyncWindow(w);
		SDL_HideCursor();			// the game draws its own cursor (retail hides Windows')
	}
	fprintf(stderr, PORT_LOG_PREFIX "fullscreen %ux%u shown in a desktop-fullscreen window\n", pp->BackBufferWidth,
			pp->BackBufferHeight);
}

}

// The back buffer's size while it is shown desktop-fullscreen (the space PortMapMouse maps into), else 0.
extern "C" int PortMouseBox(unsigned *w, unsigned *h)
{
	if (!gBoxW || !gBoxH || !gBoxWindow)
		return 0;
	*w = gBoxW;
	*h = gBoxH;
	return 1;
}

// The game's back buffer: the size the front end divides the window mouse by (LTShell::GetWindowWidth and Height read
// the present parameters the game passed to CreateDevice and Reset), 0 before the device exists.
extern "C" int PortBackBuffer(unsigned *w, unsigned *h)
{
	if (!gBackW || !gBackH)
		return 0;
	*w = gBackW;
	*h = gBackH;
	return 1;
}

namespace
{
// The picture's rectangle in the window, in window pixels (the same box Present draws into).
bool DesktopBox(int &left, int &top, int &width, int &height)
{
	if (!gBoxW || !gBoxH || !gBoxWindow)
		return false;
	int ww = 0, wh = 0;
	SDL_GetWindowSizeInPixels(gBoxWindow, &ww, &wh);
	if (ww <= 0 || wh <= 0)
		return false;
	long bw = ww, bh = (long)((long long)ww * gBoxH / gBoxW);
	if (bh > wh)
	{
		bh = wh;
		bw = (long)((long long)wh * gBoxW / gBoxH);
	}
	left = (int)((ww - bw) / 2);
	top = (int)((wh - bh) / 2);
	width = (int)bw;
	height = (int)bh;
	return true;
}

IDirect3DDevice9 *gDevice;		// the game's device (PortWideViewport)

HRESULT STDMETHODCALLTYPE Reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *pp)
{
	HWND focus = NULL;
	D3DDEVICE_CREATION_PARAMETERS cp;
	if (SUCCEEDED(device->GetCreationParameters(&cp)))
		focus = cp.hFocusWindow;
	DesktopFullscreen(focus, pp);
	HRESULT hr = gReset(device, pp);
	PortSessionLog(PORT_SESSION_VIDEO, FAILED(hr), "device reset result=%08lx size=%ux%u windowed=%d format=%d",
		(unsigned long)hr, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0,
		pp ? (int)pp->Windowed : -1, pp ? (int)pp->BackBufferFormat : -1);
	if (SUCCEEDED(hr) && pp)
	{
		gBackW = pp->BackBufferWidth;
		gBackH = pp->BackBufferHeight;
		PortWideBackBuffer(pp->BackBufferWidth, pp->BackBufferHeight);
	}
	return hr;
}

CreateDeviceFn gCreateDevice;
PresentFn gPresent;

// IDirect3D9::GetAdapterModeCount and EnumAdapterModes. The game lists only 4:3 modes (d3dapp.cpp, unless the
// ALLOW_WIDESCREEN_MODES tweak is set) and stops with "Could not find any compatible Direct3D devices" without one.
// Windows drivers offered the standard 4:3 modes on any monitor; dxvk on a widescreen desktop offers only the
// monitor's own (or none). When an adapter's list for a format holds no 4:3 mode, the port appends the standard ones
// that fit the desktop, at the desktop's refresh rate; a list that has one (the offscreen driver's) is unchanged.
typedef UINT (STDMETHODCALLTYPE *ModeCountFn)(IDirect3D9 *, UINT, D3DFORMAT);
typedef HRESULT (STDMETHODCALLTYPE *EnumModesFn)(IDirect3D9 *, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE *);
ModeCountFn gModeCount;
EnumModesFn gEnumModes;
#ifndef _WIN32
// Compatibility back buffers for Linux's desktop-fullscreen presentation, never native exclusive display modes.
const UINT kStandard[][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 960 }, { 1400, 1050 },
							  { 1600, 1200 } };

// The standard 4:3 modes this adapter and format gain (none when the real list already has a 4:3 mode).
UINT Extra(IDirect3D9 *d3d, UINT adapter, D3DFORMAT format, UINT real, D3DDISPLAYMODE *out, UINT index)
{
	D3DDISPLAYMODE m;
	UINT maxW = 0, maxH = 0;
	for (UINT i = 0; i < real; i++)
	{
		if (FAILED(gEnumModes(d3d, adapter, format, i, &m)))
			continue;
		if (m.Width * 3 == m.Height * 4)
			return 0;
		if (m.Width >= maxW && m.Height >= maxH)
		{
			maxW = m.Width;
			maxH = m.Height;
		}
	}
	// An empty real list (dxvk-native under SDL3 on X11 reports none) is bounded by the desktop mode instead.
	D3DDISPLAYMODE desktop;
	bool haveDesktop = SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &desktop));
	if (haveDesktop && (desktop.Width > maxW || desktop.Height > maxH))
	{
		maxW = desktop.Width;
		maxH = desktop.Height;
	}
	if (!maxW || (format != D3DFMT_X8R8G8B8 && format != D3DFMT_A8R8G8B8 && !real))
		return 0;
	UINT refresh = haveDesktop ? desktop.RefreshRate : 60;
	UINT n = 0;
	for (const auto &s : kStandard)
		if (s[0] <= maxW && s[1] <= maxH)
		{
			if (out && n == index)
			{
				out->Width = s[0];
				out->Height = s[1];
				out->RefreshRate = refresh;
				out->Format = format;
			}
			n++;
		}
	return n;
}
#endif

// With widescreen modes on (widescreen.cpp), the 32-bit formats list one set of sizes instead, which the game no longer
// limits to 4:3 (patch 0045): on Windows the driver's own sizes (refresh rates folded, as the game folds them); on Linux,
// where dxvk shows any back buffer through the desktop-fullscreen window, the display's sizes, the desktop's own, and
// the common 4:3, 16:9 and 16:10 sizes, none larger than the desktop. At most kWideMax: BuildDeviceList collects modes
// in a fixed array of 100 without a bounds check. R8G8B8 lists nothing then (no display offers it; listed first, it
// would take the sizes from X8R8G8B8 and leave the device without a confirmed format).
const int kWideMax = 64;
// The game folds the two 32-bit formats by size into a fixed 100-mode array. Keep at most 64 distinct
// sizes across those formats, but never substitute one format's or adapter's supported list for another's.
struct WideModes
{
	D3DDISPLAYMODE modes[kWideMax];
	UINT count;
	bool built;
};
WideModes gWide[2];
IDirect3D9 *gWideOwner;
UINT gWideAdapter;
UINT gWideSizes[kWideMax][2];
UINT gWideSizeCount;

// A new COM object's storage can reuse an old address. The factory, not pointer equality, marks its lifetime.
void ResetWideModes() { gWideOwner = NULL; }

WideModes &WideList(IDirect3D9 *d3d, UINT adapter, D3DFORMAT format)
{
	if (gWideOwner != d3d || gWideAdapter != adapter)
	{
		gWideOwner = d3d;
		gWideAdapter = adapter;
		gWideSizeCount = 0;
		for (auto &list : gWide)
		{
			list.count = 0;
			list.built = false;
		}
	}
	return gWide[format == D3DFMT_A8R8G8B8 ? 1 : 0];
}

void AddWideMode(WideModes &list, const D3DDISPLAYMODE &mode, UINT maxW, UINT maxH)
{
	if (mode.Width < 640 || mode.Height < 480 || (maxW && (mode.Width > maxW || mode.Height > maxH)))
		return;
	for (UINT i = 0; i < list.count; i++)
		if (list.modes[i].Width == mode.Width && list.modes[i].Height == mode.Height)
			return;
	if (list.count >= kWideMax)
		return;
	UINT size = 0;
	for (; size < gWideSizeCount; size++)
		if (gWideSizes[size][0] == mode.Width && gWideSizes[size][1] == mode.Height)
			break;
	if (size == gWideSizeCount)
	{
		if (gWideSizeCount >= kWideMax)
			return;
		gWideSizes[gWideSizeCount][0] = mode.Width;
		gWideSizes[gWideSizeCount++][1] = mode.Height;
	}
	list.modes[list.count++] = mode;
}

#ifndef _WIN32
void AddWide(WideModes &list, UINT w, UINT h, UINT maxW, UINT maxH, UINT refresh, D3DFORMAT format)
{
	D3DDISPLAYMODE mode = {w, h, refresh, format};
	AddWideMode(list, mode, maxW, maxH);
}
#endif

void SortWideModes(WideModes &list, UINT adapter, D3DFORMAT format, UINT nativeW, UINT nativeH)
{
	// Ascending by width, then height (the options list reads in that order).
	for (UINT i = 1; i < list.count; i++)
		for (UINT j = i; j > 0 && (list.modes[j - 1].Width > list.modes[j].Width ||
			(list.modes[j - 1].Width == list.modes[j].Width && list.modes[j - 1].Height > list.modes[j].Height)); j--)
		{
			D3DDISPLAYMODE t = list.modes[j];
			list.modes[j] = list.modes[j - 1];
			list.modes[j - 1] = t;
		}
	fprintf(stderr, PORT_LOG_PREFIX "display modes (adapter %u format %d desktop %ux%u):", adapter, (int)format, nativeW, nativeH);
	for (UINT i = 0; i < list.count; i++)
		fprintf(stderr, " %ux%u", list.modes[i].Width, list.modes[i].Height);
	fprintf(stderr, "\n");
}

WideModes &BuildWide(IDirect3D9 *d3d, UINT adapter, D3DFORMAT format)
{
	WideModes &list = WideList(d3d, adapter, format);
	if (list.built)
		return list;
	list.built = true;
	D3DDISPLAYMODE desktop{};
	bool haveDesktop = SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &desktop));
	D3DDISPLAYMODE m;
#ifdef _WIN32
	// Reserve one real usable mode in each format before spending the shared size budget. Prefer its actual
	// desktop mode; a long first-format list must not remove the second format's sole usable desktop.
	const D3DFORMAT formats[] = {D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8};
	UINT counts[2];
	for (UINT f = 0; f < 2; f++)
	{
		gWide[f].built = true;
		counts[f] = gModeCount(d3d, adapter, formats[f]);
		D3DDISPLAYMODE first{};
		bool haveFirst = false, havePreferred = false;
		for (UINT i = 0; i < counts[f]; i++)
			if (SUCCEEDED(gEnumModes(d3d, adapter, formats[f], i, &m)) && m.Width >= 640 && m.Height >= 480)
			{
				if (!haveFirst) { first = m; haveFirst = true; }
				if (haveDesktop && m.Width == desktop.Width && m.Height == desktop.Height)
				{
					AddWideMode(gWide[f], m, 0, 0);
					havePreferred = true;
					break;
				}
			}
		if (!havePreferred && haveFirst)
			AddWideMode(gWide[f], first, 0, 0);
	}
	for (UINT f = 0; f < 2; f++)
		for (UINT i = 0; i < counts[f]; i++)
			if (SUCCEEDED(gEnumModes(d3d, adapter, formats[f], i, &m)))
				AddWideMode(gWide[f], m, 0, 0);
	UINT nativeW = haveDesktop ? desktop.Width : 0, nativeH = haveDesktop ? desktop.Height : 0;
#else
	UINT real = gModeCount(d3d, adapter, format);
	// Linux presents these back buffers through a desktop-fullscreen window; its compatibility sizes are intentional.
	UINT refresh = haveDesktop && desktop.RefreshRate ? desktop.RefreshRate : 60;
	UINT nativeW = 0, nativeH = 0;
	if (!PortWideDesktopSize(&nativeW, &nativeH) && haveDesktop)
	{
		nativeW = desktop.Width;
		nativeH = desktop.Height;
	}
	if (nativeW)
		AddWide(list, nativeW, nativeH, 0, 0, refresh, format);
	AddWide(list, 640, 480, 0, 0, refresh, format);
	for (UINT i = 0; i < real; i++)
		if (SUCCEEDED(gEnumModes(d3d, adapter, format, i, &m)))
			AddWide(list, m.Width, m.Height, nativeW, nativeH, refresh, format);
	unsigned sizes[kWideMax][2];
	int n = PortWideDisplaySizes(sizes, kWideMax);
	for (int i = 0; i < n; i++)
		AddWide(list, sizes[i][0], sizes[i][1], nativeW, nativeH, refresh, format);
	static const UINT common[][2] = { { 1280, 720 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 },
									  { 3840, 2160 }, { 1280, 800 }, { 1440, 900 }, { 1680, 1050 }, { 1920, 1200 },
									  { 2560, 1600 }, { 2560, 1080 }, { 3440, 1440 } };
	for (const auto &s : kStandard)
		AddWide(list, s[0], s[1], nativeW, nativeH, refresh, format);
	for (const auto &s : common)
		AddWide(list, s[0], s[1], nativeW, nativeH, refresh, format);
#endif
#ifdef _WIN32
	for (UINT f = 0; f < 2; f++)
		SortWideModes(gWide[f], adapter, formats[f], nativeW, nativeH);
#else
	SortWideModes(list, adapter, format, nativeW, nativeH);
#endif
	return list;
}

bool WideFormat(D3DFORMAT format)
{
	return format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8 || format == D3DFMT_R8G8B8;
}

#ifdef _WIN32
void CheckNative43Admission(IDirect3D9 *d3d, UINT adapter)
{
	const D3DFORMAT formats[] = {D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8};
	for (D3DFORMAT format : formats)
		for (UINT i = 0, count = gModeCount(d3d, adapter, format); i < count; i++)
		{
			D3DDISPLAYMODE mode;
			if (SUCCEEDED(gEnumModes(d3d, adapter, format, i, &mode)) && mode.Width >= 640 && mode.Height >= 480 &&
				mode.Width * 3 == mode.Height * 4)
				return;
		}
	fprintf(stderr, PORT_LOG_PREFIX "Windows adapter %u has no native 4:3 fullscreen display modes. "
		"Fullscreen 4:3 is unavailable; windowed clients require separate desktop-format validation.\n", adapter);
}
#endif

UINT STDMETHODCALLTYPE ModeCount(IDirect3D9 *d3d, UINT adapter, D3DFORMAT format)
{
	if (WideFormat(format) && PortWideModes())
	{
		return format == D3DFMT_R8G8B8 ? 0 : BuildWide(d3d, adapter, format).count;
	}
	UINT real = gModeCount(d3d, adapter, format);
#ifdef _WIN32
	if (format == D3DFMT_X8R8G8B8) CheckNative43Admission(d3d, adapter);
	return real;
#else
	return real + Extra(d3d, adapter, format, real, NULL, 0);
#endif
}

HRESULT STDMETHODCALLTYPE EnumModes(IDirect3D9 *d3d, UINT adapter, D3DFORMAT format, UINT index, D3DDISPLAYMODE *mode)
{
	if (WideFormat(format) && PortWideModes())
	{
		if (!mode || format == D3DFMT_R8G8B8)
			return D3DERR_INVALIDCALL;
		WideModes &list = BuildWide(d3d, adapter, format);
		if (index >= list.count)
			return D3DERR_INVALIDCALL;
		*mode = list.modes[index];
		return D3D_OK;
	}
#ifdef _WIN32
	return gEnumModes(d3d, adapter, format, index, mode);
#else
	UINT real = gModeCount(d3d, adapter, format);
	if (index < real)
		return gEnumModes(d3d, adapter, format, index, mode);
	if (!mode || index - real >= Extra(d3d, adapter, format, real, NULL, 0))
		return D3DERR_INVALIDCALL;
	Extra(d3d, adapter, format, real, mode, index - real);
	return D3D_OK;
#endif
}
unsigned gFrames;
unsigned gShotFrame;
unsigned gShotEvery;		// BEA_PORT_SHOT_EVERY=N:DIR writes DIR/shot-NNNNN.ppm every N Presents (front-end exploration)
char gShotDir[1024];
unsigned gQuitFrame;
char gShotPath[1024];

void WriteShot(IDirect3DDevice9 *dev)
{
	IDirect3DSurface9 *back = NULL, *copy = NULL;
	D3DSURFACE_DESC d;
	D3DLOCKED_RECT lr;
	if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		return;
	back->GetDesc(&d);
	if (SUCCEEDED(dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &copy, NULL)) &&
		SUCCEEDED(dev->GetRenderTargetData(back, copy)) && SUCCEEDED(copy->LockRect(&lr, NULL, D3DLOCK_READONLY)))
	{
		if (FILE *f = fopen(gShotPath, "wb"))
		{
			fprintf(f, "P6\n%u %u\n255\n", d.Width, d.Height);
			for (UINT y = 0; y < d.Height; y++)
			{
				const unsigned char *row = (const unsigned char *)lr.pBits + y * lr.Pitch;
				for (UINT x = 0; x < d.Width; x++)
				{
					unsigned char rgb[3];
					if (d.Format == D3DFMT_R5G6B5 || d.Format == D3DFMT_X1R5G5B5 || d.Format == D3DFMT_A1R5G5B5)
					{
						unsigned v = ((const unsigned short *)row)[x];
						bool r565 = d.Format == D3DFMT_R5G6B5;
						unsigned r = r565 ? (v >> 11) & 31 : (v >> 10) & 31, g = r565 ? (v >> 5) & 63 : (v >> 5) & 31, b = v & 31;
						rgb[0] = (unsigned char)(r * 255 / 31);
						rgb[1] = (unsigned char)(g * 255 / (r565 ? 63 : 31));
						rgb[2] = (unsigned char)(b * 255 / 31);
					}
					else
					{
						unsigned v = ((const unsigned *)row)[x];
						rgb[0] = (unsigned char)(v >> 16);
						rgb[1] = (unsigned char)(v >> 8);
						rgb[2] = (unsigned char)v;
					}
					fwrite(rgb, 1, 3, f);
				}
			}
			fclose(f);
			fprintf(stderr, PORT_LOG_PREFIX "frame %u written to %s (%ux%u, format %d)\n", gFrames, gShotPath, d.Width, d.Height,
					(int)d.Format);
		}
		copy->UnlockRect();
	}
	else
		fprintf(stderr, PORT_LOG_PREFIX "frame %u: back buffer read-back failed (format %d)\n", gFrames, (int)d.Format);
	if (copy)
		copy->Release();
	back->Release();
}

#ifdef CE_VERSION
}
#include "../../game/ce_photo.h"
namespace
{
// CE photo mode's screenshot: the back buffer as the player sees it (a multisampled one resolved first), read back
// through a system-memory copy and handed to CE_ScreenshotSave; its outcome goes to the photo mode's note.
void TakeCeShot(IDirect3DDevice9 *dev)
{
	IDirect3DSurface9 *back = NULL, *flat = NULL, *copy = NULL;
	D3DSURFACE_DESC d;
	D3DLOCKED_RECT lr;
	char name[96] = "";
	int status = CE_SHOT_READ;
	if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
	{
		back->GetDesc(&d);
		IDirect3DSurface9 *source = back;
		if (d.MultiSampleType != D3DMULTISAMPLE_NONE &&
			SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &flat, NULL)) &&
			SUCCEEDED(dev->StretchRect(back, NULL, flat, NULL, D3DTEXF_NONE)))
			source = flat;
		if ((source != back || d.MultiSampleType == D3DMULTISAMPLE_NONE) &&
			SUCCEEDED(dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &copy, NULL)) &&
			SUCCEEDED(dev->GetRenderTargetData(source, copy)) && SUCCEEDED(copy->LockRect(&lr, NULL, D3DLOCK_READONLY)))
		{
			status = CE_ScreenshotSave(lr.pBits, lr.Pitch, (int)d.Width, (int)d.Height, (unsigned)d.Format, name, sizeof name);
			copy->UnlockRect();
		}
	}
	if (copy) copy->Release();
	if (flat) flat->Release();
	if (back) back->Release();
	if (status == CE_SHOT_SAVED) fprintf(stderr, PORT_LOG_PREFIX "screenshot %s saved (frame %u)\n", name, gFrames);
	else fprintf(stderr, PORT_LOG_PREFIX "screenshot not saved (status %d, frame %u)\n", status, gFrames);
	PortCePhotoShotDone(status, name);
}
#endif

// BEA_PORT_D3DLOG=FRAME:PATH: one frame's draws as the device saw them, for comparing a renderer with the game's
// (the device is created pure, so Get* is not relied on: the hooks keep a shadow of what the game set). For each
// DrawPrimitive/DrawIndexedPrimitive in frame FRAME (Presents counted from 0): its arguments, stream 0, textures 0-3,
// the vertex shader (each new one's tokens once, as hex), the vertex shader constants set so far (c0..c95), and
// texture stage states and render states set so far, the material and lights 0-7 (with LightEnable). Off: the hooks only forward.
typedef HRESULT (STDMETHODCALLTYPE *SetRSFn)(IDirect3DDevice9 *, D3DRENDERSTATETYPE, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *SetTexFn)(IDirect3DDevice9 *, DWORD, IDirect3DBaseTexture9 *);
typedef HRESULT (STDMETHODCALLTYPE *SetTSSFn)(IDirect3DDevice9 *, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *DrawFn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *DrawIdxFn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *SetVSFn)(IDirect3DDevice9 *, IDirect3DVertexShader9 *);
typedef HRESULT (STDMETHODCALLTYPE *SetVSCFn)(IDirect3DDevice9 *, UINT, const float *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *SetStreamFn)(IDirect3DDevice9 *, UINT, IDirect3DVertexBuffer9 *, UINT, UINT);
SetRSFn gSetRS; SetTexFn gSetTex; SetTSSFn gSetTSS; DrawFn gDraw; DrawIdxFn gDrawIdx; SetVSFn gSetVS; SetVSCFn gSetVSC;
SetStreamFn gSetStream;
typedef HRESULT (STDMETHODCALLTYPE *SetMatFn)(IDirect3DDevice9 *, const D3DMATERIAL9 *);
typedef HRESULT (STDMETHODCALLTYPE *SetLightFn)(IDirect3DDevice9 *, DWORD, const D3DLIGHT9 *);
typedef HRESULT (STDMETHODCALLTYPE *LightEnableFn)(IDirect3DDevice9 *, DWORD, BOOL);
SetMatFn gSetMat; SetLightFn gSetLight; LightEnableFn gLightEnable;
typedef HRESULT (STDMETHODCALLTYPE *SetTransformFn)(IDirect3DDevice9 *, D3DTRANSFORMSTATETYPE, const D3DMATRIX *);
SetTransformFn gSetTransform;
typedef HRESULT (STDMETHODCALLTYPE *SetPSFn)(IDirect3DDevice9 *, IDirect3DPixelShader9 *);
typedef HRESULT (STDMETHODCALLTYPE *SetPSCFn)(IDirect3DDevice9 *, UINT, const float *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *SetRTFn)(IDirect3DDevice9 *, DWORD, IDirect3DSurface9 *);
typedef HRESULT (STDMETHODCALLTYPE *SetDSFn)(IDirect3DDevice9 *, IDirect3DSurface9 *);
SetPSFn gSetPS; SetPSCFn gSetPSC; SetRTFn gSetRT; SetDSFn gSetDS;
IDirect3DPixelShader9 *gPS; float gPSC[32][4]; bool gPSCSet[32];
IDirect3DPixelShader9 *gDumpedPS[256]; int gNumDumpedPS;
D3DMATRIX gWorld, gView, gProj; bool gWorldSet, gViewSet, gProjSet;
D3DMATRIX gTexM[4]; bool gTexMSet[4];
char gLogPath[512];
D3DMATERIAL9 gMat; bool gMatSet;
D3DLIGHT9 gLights[8]; bool gLightSet[8]; BOOL gLightOn[8];
int gLogFrame = -1;
FILE *gLog;
DWORD gRS[256]; bool gRSSet[256];
DWORD gTSS[4][33]; bool gTSSSet[4][33];
float gVSC[96][4]; bool gVSCSet[96];
IDirect3DBaseTexture9 *gTex[4];
IDirect3DVertexShader9 *gVS;
IDirect3DVertexBuffer9 *gVB; UINT gVBOffset, gVBStride;
IDirect3DVertexShader9 *gDumped[256]; int gNumDumped;
unsigned gDrawNo;

bool Logging() { return gLog && (int)gFrames == gLogFrame; }

void LogState(const char *what)
{
	fprintf(gLog, "draw %u %s vs %p vb %p off %u stride %u tex0 %p tex1 %p tex2 %p tex3 %p\n", gDrawNo++, what, (void *)gVS,
			(void *)gVB, gVBOffset, gVBStride, (void *)gTex[0], (void *)gTex[1], (void *)gTex[2], (void *)gTex[3]);
	if (gVS)
	{
		int i = 0;
		while (i < gNumDumped && gDumped[i] != gVS)
			i++;
		if (i == gNumDumped && gNumDumped < 256)
		{
			gDumped[gNumDumped++] = gVS;
			UINT size = 0;
			if (SUCCEEDED(gVS->GetFunction(NULL, &size)) && size)
			{
				DWORD *t = (DWORD *)malloc(size);
				if (SUCCEEDED(gVS->GetFunction(t, &size)))
				{
					fprintf(gLog, "  shader %p tokens", (void *)gVS);
					for (UINT k = 0; k < size / 4; k++)
						fprintf(gLog, " %08lx", (unsigned long)t[k]);
					fprintf(gLog, "\n");
				}
				free(t);
			}
		}
	}
	if (gVB && getenv("BEA_PORT_D3DLOG_VB"))
	{
		void *data = NULL;
		UINT bytes = 4 * (gVBStride ? gVBStride : 32);
		if (SUCCEEDED(gVB->Lock(gVBOffset, bytes, &data, D3DLOCK_READONLY)) && data)
		{
			fprintf(gLog, "  vb first 4 vertices (dwords):");
			for (UINT k = 0; k < bytes / 4; k++)
				fprintf(gLog, " %08x", ((const unsigned *)data)[k]);
			fprintf(gLog, "\n");
			gVB->Unlock();
		}
		else
			fprintf(gLog, "  vb lock failed\n");
	}
	auto mat = [](const char *n, const D3DMATRIX &m) {
		fprintf(gLog, "  %s", n);
		for (int k = 0; k < 16; k++)
			fprintf(gLog, " %08x", ((const unsigned *)&m)[k]);
		fprintf(gLog, "\n");
	};
	if (gWorldSet) mat("world", gWorld);
	if (gViewSet) mat("view", gView);
	if (gProjSet) mat("projection", gProj);
	for (int k = 0; k < 4; k++)
		if (gTexMSet[k])
		{
			char n[16];
			snprintf(n, sizeof n, "texture%d", k);
			mat(n, gTexM[k]);
		}
	// Each bound texture's top level: format and size; with BEA_PORT_D3DLOG_TEXDUMP=DRAW, that draw's levels are also
	// written raw (as locked, pitch removed) beside the log as <log>.draw<N>.tex<K>.raw.
	const char *td = getenv("BEA_PORT_D3DLOG_TEXDUMP");
	for (int k = 0; k < 4; k++)
	{
		if (!gTex[k]) continue;
		IDirect3DTexture9 *t2 = NULL;
		if (FAILED(gTex[k]->QueryInterface(IID_IDirect3DTexture9, (void **)&t2)) || !t2) continue;
		D3DSURFACE_DESC dsc;
		if (SUCCEEDED(t2->GetLevelDesc(0, &dsc)))
		{
			fprintf(gLog, "  tex%d format %d size %ux%u levels %lu pool %d\n", k, (int)dsc.Format, dsc.Width, dsc.Height,
					(unsigned long)t2->GetLevelCount(), (int)dsc.Pool);
			if (td && (unsigned)atoi(td) == gDrawNo - 1)
			{
				D3DLOCKED_RECT lr;
				if (SUCCEEDED(t2->LockRect(0, &lr, NULL, D3DLOCK_READONLY)))
				{
					char path[600];
					snprintf(path, sizeof path, "%s.draw%u.tex%d.raw", gLogPath, gDrawNo - 1, k);
					if (FILE *f = fopen(path, "wb"))
					{
						UINT bpp = dsc.Format == D3DFMT_A8R8G8B8 || dsc.Format == D3DFMT_X8R8G8B8 ? 4 : 2;
						bool dxt = dsc.Format == D3DFMT_DXT1 || dsc.Format == D3DFMT_DXT2 || dsc.Format == D3DFMT_DXT3 ||
								   dsc.Format == D3DFMT_DXT4 || dsc.Format == D3DFMT_DXT5;
						UINT rows = dxt ? (dsc.Height + 3) / 4 : dsc.Height;
						UINT rowBytes = dxt ? ((dsc.Width + 3) / 4) * (dsc.Format == D3DFMT_DXT1 ? 8 : 16) : dsc.Width * bpp;
						for (UINT r = 0; r < rows; r++)
							fwrite((const char *)lr.pBits + r * lr.Pitch, 1, rowBytes, f);
						fclose(f);
						fprintf(gLog, "  tex%d dumped to %s\n", k, path);
					}
					t2->UnlockRect(0);
				}
				else
				{
					// A render target (default pool): read it back through a system-memory copy.
					IDirect3DSurface9 *src = NULL, *dst = NULL;
					IDirect3DDevice9 *dev = NULL;
					bool ok = false;
					if (SUCCEEDED(t2->GetSurfaceLevel(0, &src)) && SUCCEEDED(t2->GetDevice(&dev)) &&
						SUCCEEDED(dev->CreateOffscreenPlainSurface(dsc.Width, dsc.Height, dsc.Format, D3DPOOL_SYSTEMMEM, &dst, NULL)) &&
						SUCCEEDED(dev->GetRenderTargetData(src, dst)) && SUCCEEDED(dst->LockRect(&lr, NULL, D3DLOCK_READONLY)))
					{
						char path[600];
						snprintf(path, sizeof path, "%s.draw%u.tex%d.raw", gLogPath, gDrawNo - 1, k);
						if (FILE *f = fopen(path, "wb"))
						{
							UINT bpp = dsc.Format == D3DFMT_A8R8G8B8 || dsc.Format == D3DFMT_X8R8G8B8 ? 4 : 2;
							for (UINT r = 0; r < dsc.Height; r++)
								fwrite((const char *)lr.pBits + r * lr.Pitch, 1, dsc.Width * bpp, f);
							fclose(f);
							fprintf(gLog, "  tex%d (render target) dumped to %s\n", k, path);
							ok = true;
						}
						dst->UnlockRect();
					}
					if (!ok)
						fprintf(gLog, "  tex%d lock and render-target readback failed\n", k);
					if (dst) dst->Release();
					if (src) src->Release();
					if (dev) dev->Release();
				}
			}
		}
		t2->Release();
	}
	auto col = [](const D3DCOLORVALUE &c) { fprintf(gLog, " %08x %08x %08x %08x", *(const unsigned *)&c.r, *(const unsigned *)&c.g,
												 *(const unsigned *)&c.b, *(const unsigned *)&c.a); };
	if (gMatSet)
	{
		fprintf(gLog, "  material diffuse"); col(gMat.Diffuse); fprintf(gLog, " ambient"); col(gMat.Ambient);
		fprintf(gLog, " specular"); col(gMat.Specular); fprintf(gLog, " emissive"); col(gMat.Emissive);
		fprintf(gLog, " power %08x\n", *(unsigned *)&gMat.Power);
	}
	for (int i = 0; i < 8; i++)
		if (gLightSet[i])
		{
			const D3DLIGHT9 &l = gLights[i];
			fprintf(gLog, "  light%d on %d type %d diffuse", i, (int)gLightOn[i], (int)l.Type); col(l.Diffuse);
			fprintf(gLog, " ambient"); col(l.Ambient); fprintf(gLog, " specular"); col(l.Specular);
			fprintf(gLog, " dir %08x %08x %08x pos %08x %08x %08x range %08x\n", *(const unsigned *)&l.Direction.x,
					*(const unsigned *)&l.Direction.y, *(const unsigned *)&l.Direction.z, *(const unsigned *)&l.Position.x,
					*(const unsigned *)&l.Position.y, *(const unsigned *)&l.Position.z, *(const unsigned *)&l.Range);
		}
	if (gPS)
	{
		fprintf(gLog, "  ps %p\n", (void *)gPS);
		int i = 0;
		while (i < gNumDumpedPS && gDumpedPS[i] != gPS)
			i++;
		if (i == gNumDumpedPS && gNumDumpedPS < 256)
		{
			gDumpedPS[gNumDumpedPS++] = gPS;
			UINT size = 0;
			if (SUCCEEDED(gPS->GetFunction(NULL, &size)) && size)
			{
				DWORD *t = (DWORD *)malloc(size);
				if (SUCCEEDED(gPS->GetFunction(t, &size)))
				{
					fprintf(gLog, "  pshader %p tokens", (void *)gPS);
					for (UINT k = 0; k < size / 4; k++)
						fprintf(gLog, " %08lx", (unsigned long)t[k]);
					fprintf(gLog, "\n");
				}
				free(t);
			}
		}
		for (int r = 0; r < 32; r++)
			if (gPSCSet[r])
				fprintf(gLog, "  pc%d %08x %08x %08x %08x\n", r, *(unsigned *)&gPSC[r][0], *(unsigned *)&gPSC[r][1],
						*(unsigned *)&gPSC[r][2], *(unsigned *)&gPSC[r][3]);
	}
	for (int r = 0; r < 96; r++)
		if (gVSCSet[r])
			fprintf(gLog, "  c%d %08x %08x %08x %08x\n", r, *(unsigned *)&gVSC[r][0], *(unsigned *)&gVSC[r][1],
					*(unsigned *)&gVSC[r][2], *(unsigned *)&gVSC[r][3]);
	for (int st = 0; st < 4; st++)
		for (int k = 0; k < 33; k++)
			if (gTSSSet[st][k])
				fprintf(gLog, "  tss%d.%d %08lx\n", st, k, (unsigned long)gTSS[st][k]);
	for (int k = 0; k < 256; k++)
		if (gRSSet[k])
			fprintf(gLog, "  rs%d %08lx\n", k, (unsigned long)gRS[k]);
}

HRESULT STDMETHODCALLTYPE SetPS(IDirect3DDevice9 *d, IDirect3DPixelShader9 *ps)
{
	gPS = ps;
	return gSetPS(d, ps);
}
HRESULT STDMETHODCALLTYPE SetPSC(IDirect3DDevice9 *d, UINT r, const float *f, UINT n)
{
	for (UINT i = 0; i < n && r + i < 32; i++)
	{
		memcpy(gPSC[r + i], f + 4 * i, 16);
		gPSCSet[r + i] = true;
	}
	return gSetPSC(d, r, f, n);
}
void LogSurface(const char *what, DWORD index, IDirect3DSurface9 *surface)
{
	if (!Logging())
		return;
	D3DSURFACE_DESC dsc;
	if (surface && SUCCEEDED(surface->GetDesc(&dsc)))
		fprintf(gLog, "%s %lu %p %ux%u format %d (before draw %u)\n", what, (unsigned long)index, (void *)surface, dsc.Width,
				dsc.Height, (int)dsc.Format, gDrawNo);
	else
		fprintf(gLog, "%s %lu %p (before draw %u)\n", what, (unsigned long)index, (void *)surface, gDrawNo);
}
HRESULT STDMETHODCALLTYPE SetRT(IDirect3DDevice9 *d, DWORD i, IDirect3DSurface9 *s)
{
	LogSurface("SetRenderTarget", i, s);
	return gSetRT(d, i, s);
}
HRESULT STDMETHODCALLTYPE SetDS(IDirect3DDevice9 *d, IDirect3DSurface9 *s)
{
	LogSurface("SetDepthStencilSurface", 0, s);
	return gSetDS(d, s);
}
HRESULT STDMETHODCALLTYPE SetTransform(IDirect3DDevice9 *d, D3DTRANSFORMSTATETYPE t, const D3DMATRIX *m)
{
	if (m)
	{
		if (t == D3DTS_WORLD) { gWorld = *m; gWorldSet = true; }
		else if (t == D3DTS_VIEW) { gView = *m; gViewSet = true; }
		else if (t == D3DTS_PROJECTION) { gProj = *m; gProjSet = true; }
		else if (t >= D3DTS_TEXTURE0 && t <= D3DTS_TEXTURE3) { gTexM[t - D3DTS_TEXTURE0] = *m; gTexMSet[t - D3DTS_TEXTURE0] = true; }
	}
	return gSetTransform(d, t, m);
}
HRESULT STDMETHODCALLTYPE SetMat(IDirect3DDevice9 *d, const D3DMATERIAL9 *m)
{
	if (m) { gMat = *m; gMatSet = true; }
	return gSetMat(d, m);
}
HRESULT STDMETHODCALLTYPE SetLight(IDirect3DDevice9 *d, DWORD i, const D3DLIGHT9 *l)
{
	if (i < 8 && l) { gLights[i] = *l; gLightSet[i] = true; }
	return gSetLight(d, i, l);
}
HRESULT STDMETHODCALLTYPE LightEnable(IDirect3DDevice9 *d, DWORD i, BOOL on)
{
	if (i < 8) gLightOn[i] = on;
	return gLightEnable(d, i, on);
}
HRESULT STDMETHODCALLTYPE SetRS(IDirect3DDevice9 *d, D3DRENDERSTATETYPE s, DWORD v)
{
	if ((unsigned)s < 256) { gRS[s] = v; gRSSet[s] = true; }
	return gSetRS(d, s, v);
}
HRESULT STDMETHODCALLTYPE SetTex(IDirect3DDevice9 *d, DWORD st, IDirect3DBaseTexture9 *t)
{
	if (st < 4) gTex[st] = t;
	return gSetTex(d, st, t);
}
HRESULT STDMETHODCALLTYPE SetTSS(IDirect3DDevice9 *d, DWORD st, D3DTEXTURESTAGESTATETYPE k, DWORD v)
{
	if (st < 4 && (unsigned)k < 33) { gTSS[st][k] = v; gTSSSet[st][k] = true; }
	return gSetTSS(d, st, k, v);
}
HRESULT STDMETHODCALLTYPE SetVS(IDirect3DDevice9 *d, IDirect3DVertexShader9 *vs)
{
	gVS = vs;
	return gSetVS(d, vs);
}
HRESULT STDMETHODCALLTYPE SetVSC(IDirect3DDevice9 *d, UINT r, const float *f, UINT n)
{
	for (UINT i = 0; i < n && r + i < 96; i++)
	{
		memcpy(gVSC[r + i], f + 4 * i, 16);
		gVSCSet[r + i] = true;
	}
	return gSetVSC(d, r, f, n);
}
HRESULT STDMETHODCALLTYPE SetStream(IDirect3DDevice9 *d, UINT n, IDirect3DVertexBuffer9 *vb, UINT off, UINT stride)
{
	if (n == 0) { gVB = vb; gVBOffset = off; gVBStride = stride; }
	return gSetStream(d, n, vb, off, stride);
}
HRESULT STDMETHODCALLTYPE Draw(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT start, UINT count)
{
	if (Logging())
	{
		char w[96];
		snprintf(w, sizeof w, "DrawPrimitive type %d start %u count %u", (int)t, start, count);
		LogState(w);
	}
	return gDraw(d, t, start, count);
}
HRESULT STDMETHODCALLTYPE DrawIdx(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, INT base, UINT minv, UINT nv, UINT si, UINT pc)
{
	if (Logging())
	{
		char w[128];
		snprintf(w, sizeof w, "DrawIndexedPrimitive type %d base %d min %u verts %u start %u prims %u", (int)t, base, minv, nv,
				 si, pc);
		LogState(w);
	}
	return gDrawIdx(d, t, base, minv, nv, si, pc);
}

#ifdef CE_VERSION
// CE photo mode's draw filter (ce_photo.h, patch 0133). While photo mode is on, PortCeDrawHooks puts these in the device's
// four draw entries; inside a CePhotoDrawScope they do nothing, so the HUD overlay, battle line and cockpit routines run in
// full and only their pictures are lost. With photo mode off the entries are the ones the device came with.
typedef HRESULT (STDMETHODCALLTYPE *DrawUPFn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *DrawIdxUPFn)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT,
												 const void *, UINT);
void **gDeviceTable;			// the hook table of the game's device (CreateDevice)
bool gDrawsFiltered;			// the filter is in the table
int gDrawsDropped;				// open CePhotoDrawScopes: the render thread's (the game draws from one thread), so no lock
void *gDrawsReal[4];			// the entries the filter replaced
const int kDrawSlots[4] = {81, 82, 83, 84};		// DrawPrimitive, DrawIndexedPrimitive and their UP forms

HRESULT STDMETHODCALLTYPE FilterDraw(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT start, UINT count)
{
	return gDrawsDropped > 0 ? D3D_OK : ((DrawFn)gDrawsReal[0])(d, t, start, count);
}
HRESULT STDMETHODCALLTYPE FilterDrawIdx(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, INT base, UINT minv, UINT nv, UINT si, UINT pc)
{
	return gDrawsDropped > 0 ? D3D_OK : ((DrawIdxFn)gDrawsReal[1])(d, t, base, minv, nv, si, pc);
}
HRESULT STDMETHODCALLTYPE FilterDrawUP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT count, const void *data, UINT stride)
{
	return gDrawsDropped > 0 ? D3D_OK : ((DrawUPFn)gDrawsReal[2])(d, t, count, data, stride);
}
HRESULT STDMETHODCALLTYPE FilterDrawIdxUP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT minv, UINT nv, UINT count, const void *index,
										  D3DFORMAT format, const void *data, UINT stride)
{
	return gDrawsDropped > 0 ? D3D_OK : ((DrawIdxUPFn)gDrawsReal[3])(d, t, minv, nv, count, index, format, data, stride);
}
void *const kDrawFilters[4] = {(void *)&FilterDraw, (void *)&FilterDrawIdx, (void *)&FilterDrawUP, (void *)&FilterDrawIdxUP};

// A device made (CreateDevice): its hook table. Linux gives each device a private copy, which holds the device's own draw
// entries; Windows has one shared table, where the filter, if it is in, stays.
void NoteDeviceTable(void **table)
{
	if (gDeviceTable != table)
		gDrawsFiltered = false;
	gDeviceTable = table;
}
#endif

HRESULT STDMETHODCALLTYPE Present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND w, const RGNDATA *dirty)
{
	if (gLog && (int)gFrames == gLogFrame)
		fflush(gLog);
	gFrames++;
	if (gFrames % 300 == 0 && PortDevLog())
		fprintf(stderr, PORT_LOG_PREFIX "frame %u (x87 control word %04x)\n", gFrames, ControlWord());
	if (gShotFrame && gFrames == gShotFrame)
		WriteShot(dev);
	if (gShotEvery && gFrames % gShotEvery == 0)
	{
		snprintf(gShotPath, sizeof(gShotPath), "%s/shot-%05u.ppm", gShotDir, gFrames);
		WriteShot(dev);
	}
#ifdef CE_VERSION
	if (PortCePhotoShotWanted())
		TakeCeShot(dev);
#endif
	if (gQuitFrame && gFrames == gQuitFrame)
	{
		// As if the player closed the window: the game's own shutdown path (BEA_PORT_QUIT=FRAME).
		D3DDEVICE_CREATION_PARAMETERS cp;
		if (SUCCEEDED(dev->GetCreationParameters(&cp)))
			PortPostMessageA(cp.hFocusWindow, WM_CLOSE, 0, 0);
		fprintf(stderr, PORT_LOG_PREFIX "frame %u: WM_CLOSE posted (BEA_PORT_QUIT)\n", gFrames);
	}
	// Desktop fullscreen keeps the game's aspect: the picture goes to the largest centred rectangle of its shape
	// (pillarboxed on a widescreen desktop) instead of stretching across the window.
	RECT box;
	int bl, bt, bw, bh;
	if (!dst && DesktopBox(bl, bt, bw, bh))
	{
		box.left = bl;
		box.top = bt;
		box.right = bl + bw;
		box.bottom = bt + bh;
		dst = &box;
	}
	HRESULT hr = gPresent(dev, src, dst, w, dirty);
	static HRESULT lastResult = D3D_OK;
	if (hr != lastResult)
	{
		PortSessionLog(PORT_SESSION_VIDEO, FAILED(hr), "Present result=%08lx previous=%08lx", (unsigned long)hr,
			(unsigned long)lastResult);
		lastResult = hr;
	}
	PortSessionPulse(gFrames);
	PortClockFrame();		// after the Present returns, as the retail replay shim counts it
	// Then the tape's probes and events for this frame (replay.cpp; off unless BEA_PORT_TAPE or BEA_PORT_PROBES).
	D3DDEVICE_CREATION_PARAMETERS cp;
	PortReplayFrame(gFrames, SUCCEEDED(dev->GetCreationParameters(&cp)) ? cp.hFocusWindow : w);
	return hr;
}

HRESULT STDMETHODCALLTYPE CreateDevice(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
									   D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
#ifdef CE_VERSION
	static bool identityLogged;
	if (!identityLogged && PortSessionEnabled())
	{
		const char *build = getenv("BEA_CE_BUILD");
		PortSessionLog(PORT_SESSION_GAME, false, "compiled_game_version=%s launcher_supplied_build=%.64s",
			CE_VERSION, build && *build ? build : "unavailable");
		identityLogged = true;
	}
#endif
	PortSessionLog(PORT_SESSION_VIDEO, false, "backend=%s SDL=%d adapter=%u device_type=%d widescreen_modes=%d",
#ifdef _WIN32
		"native_direct3d9",
#else
		"dxvk_native",
#endif
		SDL_GetVersion(), adapter, (int)type, PortWideModes());
	{
		// The GPU, for the player's log (the launcher copies this line to the top of onslaught-ce.log); a query only,
		// made before the control word is read so the checks below see whatever it leaves.
		D3DADAPTER_IDENTIFIER9 id;
		memset(&id, 0, sizeof(id));
		if (SUCCEEDED(d3d->GetAdapterIdentifier(adapter, 0, &id)))
			fprintf(stderr, PORT_LOG_PREFIX "adapter %.512s (vendor %04lx, device %04lx, driver %u.%u.%u.%u)\n", id.Description,
					(unsigned long)id.VendorId, (unsigned long)id.DeviceId, (unsigned)HIWORD(id.DriverVersion.HighPart),
					(unsigned)LOWORD(id.DriverVersion.HighPart), (unsigned)HIWORD(id.DriverVersion.LowPart),
					(unsigned)LOWORD(id.DriverVersion.LowPart));
	}
	unsigned short before = ControlWord();
	DesktopFullscreen(focus, pp);
	if (pp)
		fprintf(stderr, PORT_LOG_PREFIX "CreateDevice asks %ux%u format %d windowed %d refresh %u\n", pp->BackBufferWidth,
				pp->BackBufferHeight, (int)pp->BackBufferFormat, (int)pp->Windowed, pp->FullScreen_RefreshRateInHz);
	HRESULT hr = gCreateDevice(d3d, adapter, type, focus, flags, pp, out);
	unsigned short after = ControlWord();
	if (SUCCEEDED(hr) && pp)
	{
		gDevice = *out;
		gBackW = pp->BackBufferWidth;
		gBackH = pp->BackBufferHeight;
		PortWideBackBuffer(pp->BackBufferWidth, pp->BackBufferHeight);
	}
	if (SUCCEEDED(hr) && !(flags & D3DCREATE_FPU_PRESERVE))
		// Precision control 00 (24-bit) and rounding control 00 (nearest); the exception masks stay as they are.
		SetControlWord((unsigned short)(after & ~0x0F00));
	fprintf(stderr, PORT_LOG_PREFIX "CreateDevice %08lx flags %08lx; x87 control word %04x before, %04x after the runtime, %04x now\n",
			(unsigned long)hr, (unsigned long)flags, before, after, ControlWord());
	PortSessionLog(PORT_SESSION_VIDEO, FAILED(hr), "device create result=%08lx actual_size=%ux%u windowed=%d "
		"format=%d multisample=%d quality=%lu interval=%08lx", (unsigned long)hr,
		pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0, pp ? (int)pp->Windowed : -1,
		pp ? (int)pp->BackBufferFormat : -1, pp ? (int)pp->MultiSampleType : -1,
		pp ? (unsigned long)pp->MultiSampleQuality : 0, pp ? (unsigned long)pp->PresentationInterval : 0);
	if (SUCCEEDED(hr) && *out)
	{
		void **table = HookTable(*out, kDeviceSlots);
#ifdef CE_VERSION
		NoteDeviceTable(table);
#endif
		if (!gPresent)
			gPresent = (PresentFn)table[17];
		SetSlot(table, 17, (void *)&Present);
		if (!gReset)
			gReset = (ResetFn)table[16];
		SetSlot(table, 16, (void *)&Reset);
		if (const char *spec = getenv("BEA_PORT_D3DLOG"))
		{
			const char *colon = strchr(spec, ':');
			if (colon && !gLog)
			{
				gLogFrame = atoi(spec);
				gLog = fopen(colon + 1, "w");
				snprintf(gLogPath, sizeof gLogPath, "%s", colon + 1);
			}
		}
		if (gLog)
		{
			if (!gSetRS)
			{
				gSetRS = (SetRSFn)table[57]; gSetTex = (SetTexFn)table[65]; gSetTSS = (SetTSSFn)table[67];
				gDraw = (DrawFn)table[81]; gDrawIdx = (DrawIdxFn)table[82]; gSetVS = (SetVSFn)table[92];
				gSetVSC = (SetVSCFn)table[94]; gSetStream = (SetStreamFn)table[100];
				gSetTransform = (SetTransformFn)table[44];
				gSetPS = (SetPSFn)table[107]; gSetPSC = (SetPSCFn)table[109]; gSetRT = (SetRTFn)table[37]; gSetDS = (SetDSFn)table[39];
				gSetMat = (SetMatFn)table[49]; gSetLight = (SetLightFn)table[51]; gLightEnable = (LightEnableFn)table[53];
			}
			const struct { int slot; void *fn; } logged[] = {
				{57, (void *)&SetRS}, {65, (void *)&SetTex}, {67, (void *)&SetTSS}, {81, (void *)&Draw},
				{82, (void *)&DrawIdx}, {92, (void *)&SetVS}, {94, (void *)&SetVSC}, {100, (void *)&SetStream},
				{44, (void *)&SetTransform}, {107, (void *)&SetPS}, {109, (void *)&SetPSC}, {37, (void *)&SetRT},
				{39, (void *)&SetDS}, {49, (void *)&SetMat}, {51, (void *)&SetLight}, {53, (void *)&LightEnable}};
			for (const auto &l : logged)
				SetSlot(table, l.slot, l.fn);
		}
	}
	return hr;
}
}

extern "C" IDirect3D9 *__stdcall PortDirect3DCreate9(UINT sdk)
{
	PortAudioGuardCheck();
	if (const char *shot = getenv("BEA_PORT_SHOT"))
	{
		const char *colon = strchr(shot, ':');
		if (colon)
		{
			gShotFrame = (unsigned)atoi(shot);
			snprintf(gShotPath, sizeof(gShotPath), "%s", colon + 1);
		}
	}
	if (const char *every = getenv("BEA_PORT_SHOT_EVERY"))
		if (const char *colon = strchr(every, ':'))
		{
			gShotEvery = (unsigned)atoi(every);
			snprintf(gShotDir, sizeof(gShotDir), "%s", colon + 1);
		}
	if (const char *quit = getenv("BEA_PORT_QUIT"))
		gQuitFrame = (unsigned)atoi(quit);
	IDirect3D9 *d3d = Direct3DCreate9(sdk);
	if (d3d)
	{
		ResetWideModes();
		void **table = HookTable(d3d, kSlots);
		if (!gCreateDevice)
			gCreateDevice = (CreateDeviceFn)table[16];
		SetSlot(table, 16, (void *)&CreateDevice);
		if (!gModeCount)
		{
			gModeCount = (ModeCountFn)table[6];
			gEnumModes = (EnumModesFn)table[7];
		}
		SetSlot(table, 6, (void *)&ModeCount);
		SetSlot(table, 7, (void *)&EnumModes);
	}
	return d3d;
}

// A mouse position in the SDL window (points) as the game's client coordinates: while the picture is shown
// desktop-fullscreen, the pillarbox is taken out and the picture's rectangle scaled to the back buffer, so the game
// sees the coordinates its own window would have given. Otherwise unchanged.
extern "C" int PortMapMouse(float *x, float *y)
{
	int bl, bt, bw, bh;
	if (!DesktopBox(bl, bt, bw, bh))
		return 0;
	float density = SDL_GetWindowPixelDensity(gBoxWindow);
	if (density <= 0)
		density = 1;
	float px = *x * density - bl, py = *y * density - bt;
	*x = px * (float)gBoxW / (float)bw;
	*y = py * (float)gBoxH / (float)bh;
	return 1;
}

// The game's viewport over the whole back buffer (0) or the centred 4:3 box of the virtual screen (1), for the wide
// front end (patch 0042) and the screens drawn after it (0044).
void PortWideViewport(int box)
{
	if (!gDevice)
		return;
	D3DSURFACE_DESC d;
	IDirect3DSurface9 *back = NULL;
	if (FAILED(gDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		return;
	back->GetDesc(&d);
	back->Release();
	D3DVIEWPORT9 vp = { 0, 0, d.Width, d.Height, 0.f, 1.f };
	if (box && PortWideActive())
	{
		float s = PortUiScale();
		vp.X = (DWORD)(PortUiLeft() + 0.5f);
		vp.Y = (DWORD)(PortUiTop() + 0.5f);
		vp.Width = (DWORD)(640.f * s + 0.5f);
		vp.Height = (DWORD)(480.f * s + 0.5f);
		if (vp.X + vp.Width > d.Width)
			vp.Width = d.Width - vp.X;
		if (vp.Y + vp.Height > d.Height)
			vp.Height = d.Height - vp.Y;
	}
	gDevice->SetViewport(&vp);
}

extern "C" unsigned PortPresentCount(void) { return gFrames; }

#ifdef CE_VERSION
// Photo mode's draw filter in the device's draw entries (on), or the device's own again (off); asked for again, it does
// nothing, which lets ce_photo.cpp re-apply it every frame for a device made anew. "Own" is whatever each entry held when
// the filter went in, BEA_PORT_D3DLOG's hook included. A hook put in an entry while the filter is in (the log's, when
// Windows' one shared table is filled again by a device made during photo mode) is replaced by the filter, and off then
// writes the older entry back over it: a debug log can miss draws or stop logging them, nothing else changes.
void PortCeDrawHooks(bool on)
{
	if (on == gDrawsFiltered || !gDeviceTable)
		return;
	for (int i = 0; i < 4; i++)
	{
		if (on)
			gDrawsReal[i] = gDeviceTable[kDrawSlots[i]];
		SetSlot(gDeviceTable, kDrawSlots[i], on ? kDrawFilters[i] : gDrawsReal[i]);
	}
	gDrawsFiltered = on;
	if (!on)
		gDrawsDropped = 0;
}

void PortCeDrawsDropped(int delta)
{
	gDrawsDropped += delta;
	if (gDrawsDropped < 0)
		gDrawsDropped = 0;
}
#endif
