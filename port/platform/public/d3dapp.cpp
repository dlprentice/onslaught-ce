// SPDX-License-Identifier: GPL-3.0-only
// Graphics/window lifecycle written for the public CE build. The game shell owns WinMain, input, the main loop and
// quitting. This host owns its device, enumerated options and lifecycle callbacks, using the existing port wrappers.
#ifdef BEA_PUBLIC_D3DAPP_TEST
#include "public_d3dapp_support.h"
#else
#include "common.h"
#include "d3dapp.h"
#include "ltshell.h"
#include "CLIParams.h"
#include "DisplayModeKey.h"
#include "tweak.h"
#include "DebugLog.h"
#include "resource.h"
#include "D3DRes.h"
#include "DXUtil.h"
#endif

extern CTweakSLONG SCREEN_SHAPE, TEXTURE_RES_LOSS_SHIFT, TEXTURE_ALLOW_32_BIT;
void LoadCardId(char *filename);
HRESULT D3DUtil_SetDeviceCursor(LPDIRECT3DDEVICE9 device, HCURSOR cursor);

CTweakSLONG ALLOW_WIDESCREEN_MODES("ALLOW_WIDESCREEN_MODES", 0);
CTweakSLONG COPY_NOT_FLIP("COPY_NOT_FLIP", 0);
CTweakSLONG SUGGEST_MULTISAMPLE_TYPE("SUGGEST_MULTISAMPLE_TYPE", 0);
CTweakSLONG FORCE_VSYNC("FORCE_VSYNC", 0);
CTweakSLONG ZBUFFER_AS_TEXTURE("ZBUFFER_AS_TEXTURE", 0);
CTweakSLONG TRY_LOCKABLE_BB("TRY_LOCKABLE_BB", 0);

namespace
{
CD3DApplication *windowOwner;
// CE has one application shell. Keep host-only state outside its consumer-visible layout: the platform's optional
// input diagnostic still reads a shell field by its existing offset.
IDirect3DQuery9 *resourceQuery;
unsigned pauseDepth;
bool deviceObjectsLive;
unsigned physicalAdapter[10], mappedAdapterCount;
struct WindowModeState
{
    D3DModeInfo mode, saved;
    DWORD savedIndex;
    bool valid, only, published;
};
WindowModeState windowModes[10][5];
CD3DApplication *modeOwner;
IDirect3D9 *modeFactory;
const DWORD noSelection = ~DWORD(0);
template<class T> void Drop(T *&object) { if (object) { object->Release(); object = NULL; } }
bool Colour32(D3DFORMAT format)
{
    return format == D3DFMT_R8G8B8 || format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8;
}
bool PublicDisplayModeAllowed(const D3DDISPLAYMODE &mode, bool wideModes, bool allowWideModes)
{
    if (mode.Width < 640 || mode.Height < 480) return false;
    if (mode.Format != D3DFMT_R8G8B8 && mode.Format != D3DFMT_X8R8G8B8 && mode.Format != D3DFMT_A8R8G8B8) return false;
    return wideModes || allowWideModes || mode.Width * 3 == mode.Height * 4;
}
DWORD ModeAtSize(const D3DDeviceInfo &device, DWORD width, DWORD height)
{
    DWORD result = 0;
    for (DWORD i = 0; i < device.dwNumModes; ++i)
        if (device.modes[i].Width == width && device.modes[i].Height == height)
        {
            result = i;
            if (Colour32(device.modes[i].Format)) break;
        }
    return result;
}
bool Before(const D3DDISPLAYMODE &left, const D3DDISPLAYMODE &right)
{
    if (left.Format != right.Format) return left.Format > right.Format;
    if (left.Width != right.Width) return left.Width < right.Width;
    return left.Height < right.Height;
}
LRESULT CALLBACK WindowDispatch(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return windowOwner ? windowOwner->MsgProc(window, message, wparam, lparam)
                       : DefWindowProc(window, message, wparam, lparam);
}
DWORD TextureCaps(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, D3DFORMAT colour, D3DFORMAT depth, BOOL windowed)
{
    const D3DFORMAT textures[] = {D3DFMT_DXT1, D3DFMT_DXT2, D3DFMT_A8R8G8B8, D3DFMT_D24X8, D3DFMT_D16};
    DWORD bits = 0;
    for (unsigned i = 0; i < 5; ++i)
        if (SUCCEEDED(d3d->CheckDeviceFormat(adapter, type, colour, 0, D3DRTYPE_TEXTURE, textures[i]))) bits |= 1u << i;
    const unsigned samples[] = {2, 3, 4, 6, 8};
    for (unsigned i = 0; i < 5; ++i)
    {
        DWORD colourQuality = 0, depthQuality = 0;
        D3DMULTISAMPLE_TYPE sample = static_cast<D3DMULTISAMPLE_TYPE>(samples[i]);
        if (SUCCEEDED(d3d->CheckDeviceMultiSampleType(adapter, type, colour, windowed, sample, &colourQuality)) && colourQuality &&
            SUCCEEDED(d3d->CheckDeviceMultiSampleType(adapter, type, depth, windowed, sample, &depthQuality)) && depthQuality)
            bits |= 1u << (i + 5);
    }
    return bits;
}
bool ValidateMode(CD3DApplication &app, UINT physical, D3DDEVTYPE type, D3DCAPS9 &caps, D3DFORMAT colour,
                  BOOL windowed, D3DModeInfo &mode)
{
    if (FAILED(app.m_pD3D->CheckDeviceType(physical, type, colour, colour, windowed))) return false;
    DWORD choices[4]; unsigned n = 0;
    if (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)
    {
        if (caps.DevCaps & D3DDEVCAPS_PUREDEVICE)
            choices[n++] = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE;
        choices[n++] = D3DCREATE_HARDWARE_VERTEXPROCESSING;
        choices[n++] = D3DCREATE_MIXED_VERTEXPROCESSING;
    }
    choices[n++] = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    unsigned c = 0;
    while (c < n && FAILED(app.ConfirmDevice(&caps, choices[c], colour))) ++c;
    D3DFORMAT depth = D3DFMT_UNKNOWN;
    if (c == n || (app.m_bUseDepthBuffer && !app.FindDepthStencilFormat(physical, type, colour, &depth))) return false;
    mode = D3DModeInfo(); mode.Format = colour; mode.dwBehavior = choices[c]; mode.DepthStencilFormat = depth;
    mode.mUnknown10 = TextureCaps(app.m_pD3D, physical, type, colour, depth, windowed);
    return true;
}
WindowModeState *WindowState(CD3DApplication &app, DWORD adapter, DWORD device)
{
    if (modeOwner != &app || modeFactory != app.m_pD3D || adapter >= app.m_dwNumAdapters || adapter >= 10 ||
        device >= app.m_Adapters[adapter].dwNumDevices || device >= 5) return NULL;
    return &windowModes[adapter][device];
}
void RestoreMode(D3DDeviceInfo &device, WindowModeState &state)
{
    if (state.published && state.savedIndex < device.dwNumModes) device.modes[state.savedIndex] = state.saved;
    state.published = false;
}
void DiscardWindowModes(CD3DApplication &app)
{
    if (modeOwner != &app) return;
    for (DWORD a = 0; a < app.m_dwNumAdapters && a < 10; ++a)
        for (DWORD d = 0; d < app.m_Adapters[a].dwNumDevices && d < 5; ++d)
            RestoreMode(app.m_Adapters[a].devices[d], windowModes[a][d]);
    memset(windowModes, 0, sizeof(windowModes)); modeOwner = NULL; modeFactory = NULL;
}
bool ClientSize(unsigned long long width, unsigned long long height)
{
    // Career's packed display key has 16 width bits and 15 height bits. Do not persist a truncated client choice.
    return width && height && width <= 0xffff && height <= 0x7fff;
}
WindowModeState *CurrentWindowState(CD3DApplication &app)
{
    if (app.m_dwAdapter >= app.m_dwNumAdapters || app.m_dwAdapter >= 10) return NULL;
    return WindowState(app, app.m_dwAdapter, app.m_Adapters[app.m_dwAdapter].dwCurrentDevice);
}
bool PublishWindowMode(CD3DApplication &app, WindowModeState &state, bool reset)
{
    D3DAdapterInfo &adapter = app.m_Adapters[app.m_dwAdapter];
    D3DDeviceInfo &device = *app.GetCurrentDevice();
    if (device.dwCurrentMode >= device.dwNumModes) return false;
    D3DDISPLAYMODE desktop;
    D3DCAPS9 caps;
    D3DModeInfo candidate;
    UINT physical = physicalAdapter[app.m_dwAdapter];
    if (FAILED(app.m_pD3D->GetAdapterDisplayMode(physical, &desktop)) || !Colour32(desktop.Format) ||
        FAILED(app.m_pD3D->GetDeviceCaps(physical, device.DeviceType, &caps)) ||
        !ValidateMode(app, physical, device.DeviceType, caps, desktop.Format, TRUE, candidate)) return false;
    // Reset cannot change the device's vertex-processing creation flags. Refuse a format that no longer supports them.
    if (reset && app.m_pd3dDevice)
    {
        if (FAILED(app.ConfirmDevice(&caps, app.m_dwCreateFlags, desktop.Format))) return false;
        candidate.dwBehavior = app.m_dwCreateFlags;
    }
    long long width = static_cast<long long>(app.m_rcWindowClient.right) - app.m_rcWindowClient.left;
    long long height = static_cast<long long>(app.m_rcWindowClient.bottom) - app.m_rcWindowClient.top;
    if (width <= 0 || height <= 0 || !ClientSize(width, height)) return false;
    // Keep the active client record intact until every query and client-size check has succeeded.
    RestoreMode(device, state);
    adapter.d3ddmDesktop = desktop; device.d3dCaps = caps;
    candidate.Width = static_cast<DWORD>(width); candidate.Height = static_cast<DWORD>(height);
    state.mode = candidate;
    state.savedIndex = device.dwCurrentMode; state.saved = device.modes[state.savedIndex]; state.published = true;
    // Legacy texture/options readers use this slot without a separate presentation-domain API. Publish actual client
    // dimensions and desktop-derived metadata while windowed; retain the real fullscreen entry for later restoration.
    device.modes[state.savedIndex] = state.mode;
    return true;
}
struct ModeChange
{
    CD3DApplication &app;
    WindowModeState &state, previous;
    DWORD adapterIndex, deviceIndex, selectedIndex, createFlags;
    D3DDISPLAYMODE desktop;
    D3DCAPS9 caps;
    D3DPRESENT_PARAMETERS parameters;
    BOOL windowed;
    float screenShape;
    bool committed;
    ModeChange(CD3DApplication &owner, WindowModeState &window)
        : app(owner), state(window), previous(window), adapterIndex(owner.m_dwAdapter),
          deviceIndex(owner.m_Adapters[adapterIndex].dwCurrentDevice),
          selectedIndex(window.published ? window.savedIndex : owner.GetCurrentDevice()->dwCurrentMode),
          createFlags(owner.m_dwCreateFlags), desktop(owner.m_Adapters[adapterIndex].d3ddmDesktop),
          caps(owner.GetCurrentDevice()->d3dCaps), parameters(owner.m_d3dpp),
          windowed(window.published ? TRUE : owner.m_pd3dDevice ? owner.m_d3dpp.Windowed : owner.m_bWindowed),
          screenShape(owner.mScreenShapeScale), committed(false)
    {
        // The resize caller has already written its requested dimensions into the presentation parameters.
        // The published record retains the last completed client even if that pending resize fails.
        if (previous.published)
        {
            parameters.BackBufferWidth = previous.mode.Width; parameters.BackBufferHeight = previous.mode.Height;
            parameters.BackBufferFormat = previous.mode.Format; parameters.AutoDepthStencilFormat = previous.mode.DepthStencilFormat;
        }
    }
    ~ModeChange()
    {
        if (committed) return;
        D3DAdapterInfo &adapter = app.m_Adapters[adapterIndex];
        D3DDeviceInfo &device = adapter.devices[deviceIndex];
        RestoreMode(device, state);
        state = previous;
        if (state.published && state.savedIndex < device.dwNumModes) device.modes[state.savedIndex] = state.mode;
        device.dwCurrentMode = selectedIndex; device.d3dCaps = caps; device.bWindowed = windowed;
        adapter.d3ddmDesktop = desktop; adapter.dwCurrentDevice = deviceIndex;
        app.m_dwAdapter = adapterIndex; app.m_bWindowed = windowed; app.m_d3dpp = parameters;
        app.m_dwCreateFlags = createFlags; app.mScreenShapeScale = screenShape;
    }
};
}

extern "C" unsigned PortPublicAdapterOrdinal(unsigned logicalAdapter)
{
    // Keep invalid indices invalid: never turn a bad logical selection into a valid physical primary adapter.
    return logicalAdapter < mappedAdapterCount ? physicalAdapter[logicalAdapter] : logicalAdapter;
}

D3DDeviceInfo::D3DDeviceInfo()
{
    memset(this, 0, sizeof(*this));
    dwCurrentMode = noSelection;
    MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(-1);
}

CD3DApplication::CD3DApplication()
    : m_dwNumAdapters(0), m_dwAdapter(0), m_bWindowed(FALSE), m_bActive(FALSE), m_bReady(FALSE),
      m_bFrameMoving(TRUE), m_bSingleStep(FALSE), mScreenShapeScale(1.f), m_hWnd(NULL), m_hWndFocus(NULL),
      m_pD3D(NULL), m_pd3dDevice(NULL), m_dwCreateFlags(0), m_dwWindowStyle(0), m_fTime(0), m_fElapsedTime(0), m_fFPS(0),
      m_strWindowTitle(const_cast<TCHAR *>(TEXT("Battle Engine Aquila"))), m_bUseDepthBuffer(FALSE), m_dwMinDepthBits(16),
      m_dwMinStencilBits(0), m_dwCreationWidth(640), m_dwCreationHeight(480), m_bShowCursorWhenFullscreen(FALSE)
{
    windowOwner = this;
    memset(windowModes, 0, sizeof(windowModes)); modeOwner = this; modeFactory = NULL;
    memset(&m_d3dpp, 0, sizeof(m_d3dpp));
    memset(&m_d3dCaps, 0, sizeof(m_d3dCaps));
    memset(&m_d3dsdBackBuffer, 0, sizeof(m_d3dsdBackBuffer));
    memset(&m_rcWindowBounds, 0, sizeof(m_rcWindowBounds));
    memset(&m_rcWindowClient, 0, sizeof(m_rcWindowClient));
    memset(m_strDeviceStats, 0, sizeof(m_strDeviceStats));
    memset(m_strFrameStats, 0, sizeof(m_strFrameStats));
    for (unsigned a = 0; a < 10; ++a)
    {
        memset(&m_Adapters[a].d3dAdapterIdentifier, 0, sizeof(m_Adapters[a].d3dAdapterIdentifier));
        memset(&m_Adapters[a].d3ddmDesktop, 0, sizeof(m_Adapters[a].d3ddmDesktop));
        m_Adapters[a].dwNumDevices = m_Adapters[a].dwCurrentDevice = 0;
    }
}

HRESULT CD3DApplication::Create(HINSTANCE instance)
{
    m_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!m_pD3D) return DisplayErrorMsg(D3DAPPERR_NODIRECT3D, 1);
    HRESULT result = BuildDeviceList();
    if (FAILED(result)) { Drop(m_pD3D); return DisplayErrorMsg(result, 1); }
    if (!m_hWnd)
    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = WindowDispatch;
        wc.hInstance = instance;
        wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDI_MAIN_ICON));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wc.lpszClassName = TEXT("BEA");
        RegisterClass(&wc);
        m_dwWindowStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_VISIBLE;
        RECT size = {0, 0, static_cast<LONG>(m_dwCreationWidth), static_cast<LONG>(m_dwCreationHeight)};
        AdjustWindowRect(&size, m_dwWindowStyle, TRUE);
        m_hWnd = CreateWindow(wc.lpszClassName, m_strWindowTitle, m_dwWindowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                             size.right - size.left, size.bottom - size.top, NULL,
                             LoadMenu(instance, MAKEINTRESOURCE(IDR_MENU)), instance, NULL);
        if (!m_hWnd) { Drop(m_pD3D); return DisplayErrorMsg(D3DAPPERR_NOWINDOW, 1); }
    }
    if (!m_hWndFocus) m_hWndFocus = m_hWnd;
    m_dwWindowStyle = GetWindowLong(m_hWnd, GWL_STYLE);
    GetWindowRect(m_hWnd, &m_rcWindowBounds);
    GetClientRect(m_hWnd, &m_rcWindowClient);
    result = Initialize3DEnvironment(false);
    if (FAILED(result)) { Drop(m_pD3D); return DisplayErrorMsg(result, 1); }
    DXUtil_Timer(TIMER_START);
    m_bReady = TRUE;
    return S_OK;
}

HRESULT CD3DApplication::BuildDeviceList()
{
    DiscardWindowModes(*this);
    m_dwNumAdapters = 0;
    mappedAdapterCount = 0;
    memset(windowModes, 0, sizeof(windowModes)); modeOwner = this; modeFactory = m_pD3D;
    const D3DFORMAT colours[] = {D3DFMT_R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5};
    const D3DDEVTYPE types[] = {D3DDEVTYPE_HAL, D3DDEVTYPE_REF};
    for (UINT physical = 0; physical < m_pD3D->GetAdapterCount() && m_dwNumAdapters < 10; ++physical)
    {
        D3DAdapterInfo &adapter = m_Adapters[m_dwNumAdapters];
        if (FAILED(m_pD3D->GetAdapterIdentifier(physical, 0, &adapter.d3dAdapterIdentifier)) ||
            FAILED(m_pD3D->GetAdapterDisplayMode(physical, &adapter.d3ddmDesktop))) continue;
        adapter.dwNumDevices = adapter.dwCurrentDevice = 0;
        D3DDISPLAYMODE available[150];
        unsigned count = 0;
        for (unsigned f = 0; f < 4; ++f)
            for (UINT i = 0; i < m_pD3D->GetAdapterModeCount(physical, colours[f]); ++i)
            {
                D3DDISPLAYMODE mode;
                if (FAILED(m_pD3D->EnumAdapterModes(physical, colours[f], i, &mode)) ||
                    !PublicDisplayModeAllowed(mode, PortWideModes() != 0, ALLOW_WIDESCREEN_MODES.mValue != 0)) continue;
                unsigned same = 0;
                while (same < count && (available[same].Width != mode.Width || available[same].Height != mode.Height ||
                       Colour32(available[same].Format) != Colour32(mode.Format))) ++same;
                if (same == count && count < 150) { mode.RefreshRate = 0; available[count++] = mode; }
            }
        for (unsigned i = 1; i < count; ++i)
        {
            D3DDISPLAYMODE item = available[i];
            unsigned at = i;
            while (at && Before(item, available[at - 1])) { available[at] = available[at - 1]; --at; }
            available[at] = item;
        }
        for (unsigned t = 0; t < 2; ++t)
        {
            D3DDeviceInfo &device = adapter.devices[adapter.dwNumDevices];
            // Options are loaded before Create: the selected slot may already carry the player's multisampling.
            D3DMULTISAMPLE_TYPE savedSamples = device.MultiSampleType;
            DWORD savedMode = device.dwCurrentMode;
            device = D3DDeviceInfo();
            device.MultiSampleType = savedSamples;
            device.dwCurrentMode = savedMode;
            device.DeviceType = types[t];
            device.strDesc = t ? TEXT("REF") : TEXT("HAL");
            WindowModeState &window = windowModes[m_dwNumAdapters][adapter.dwNumDevices];
            window = WindowModeState();
            if (FAILED(m_pD3D->GetDeviceCaps(physical, types[t], &device.d3dCaps))) continue;
            // Windowed eligibility depends on the real desktop format, independently of fullscreen enumeration.
            window.valid = Colour32(adapter.d3ddmDesktop.Format) &&
                ValidateMode(*this, physical, device.DeviceType, device.d3dCaps, adapter.d3ddmDesktop.Format, TRUE, window.mode);
            device.bCanDoWindowed = device.bWindowed = window.valid;
            // Include the desktop format even if no fullscreen mode uses it: it controls windowed availability.
            D3DFORMAT formats[151];
            unsigned formatCount = 1;
            formats[0] = adapter.d3ddmDesktop.Format;
            for (unsigned i = 0; i < count; ++i)
            {
                unsigned f = 0;
                while (f < formatCount && formats[f] != available[i].Format) ++f;
                if (f == formatCount) formats[formatCount++] = available[i].Format;
            }
            D3DModeInfo accepted[150];
            bool usable[150] = {};
            for (unsigned f = 0; f < formatCount; ++f)
            {
                D3DFORMAT colour = formats[f];
                D3DModeInfo confirmed;
                if (!ValidateMode(*this, physical, device.DeviceType, device.d3dCaps, colour, FALSE, confirmed)) continue;
                for (unsigned i = 0; i < count; ++i)
                    if (available[i].Format == colour)
                    {
                        D3DModeInfo &mode = accepted[i];
                        mode = confirmed; mode.Width = available[i].Width; mode.Height = available[i].Height;
                        usable[i] = true;
                    }
            }
            for (unsigned i = 0; i < count; ++i)
                if (usable[i]) device.modes[device.dwNumModes++] = accepted[i];
            if (!device.dwNumModes)
            {
                if (!CLIPARAMS.mForceWindowed || !window.valid || !ClientSize(m_dwCreationWidth, m_dwCreationHeight)) continue;
                // This is a client choice for legacy current-mode/options readers, not an enumerated fullscreen mode.
                // Keep its domain outside the consumer layout and refuse every fullscreen transition that selects it.
                window.only = true; device.dwNumModes = 1; device.modes[0] = window.mode;
                device.modes[0].Width = m_dwCreationWidth; device.modes[0].Height = m_dwCreationHeight;
            }
            unsigned wideW = 0, wideH = 0;
            bool wideDefault = PortWideModes() && !CLIPARAMS.mResWidth &&
                PortWideStartSize(gDisplayModeKey.mWidth, gDisplayModeKey.mHeight, gDisplayModeKey.m32Bit, &wideW, &wideH);
            if (wideDefault)
            {
                DWORD selected = ModeAtSize(device, wideW, wideH);
                if ((device.modes[selected].Width != wideW || device.modes[selected].Height != wideH) &&
                    PortWideDefaultSize(&wideW, &wideH)) selected = ModeAtSize(device, wideW, wideH);
                device.dwCurrentMode = selected;
            }
            else if (gDisplayModeKey.mWidth)
            {
                device.dwCurrentMode = 0;
                bool found = false;
                for (DWORD i = 0; i < device.dwNumModes; ++i)
                    if (device.modes[i].Width == gDisplayModeKey.mWidth && device.modes[i].Height == gDisplayModeKey.mHeight &&
                        Colour32(device.modes[i].Format) == (gDisplayModeKey.m32Bit != 0)) { device.dwCurrentMode = i; found = true; break; }
                if (!found && PortWideModes() && !CLIPARAMS.mResWidth && PortWideDefaultSize(&wideW, &wideH))
                    device.dwCurrentMode = ModeAtSize(device, wideW, wideH);
            }
            else if (device.dwCurrentMode == noSelection || device.dwCurrentMode >= device.dwNumModes)
                device.dwCurrentMode = ModeAtSize(device, m_dwCreationWidth, m_dwCreationHeight);
            device.dwLowDetailMode = 0;
            device.dwMediumDetailMode = ModeAtSize(device, 800, 600);
            device.dwHighDetailMode = ModeAtSize(device, 1280, 1024);
            if (!device.dwHighDetailMode) device.dwHighDetailMode = ModeAtSize(device, 1024, 768);
            if (PortWideModes() && PortWideDefaultSize(&wideW, &wideH))
                device.dwLowDetailMode = device.dwMediumDetailMode = device.dwHighDetailMode = ModeAtSize(device, wideW, wideH);
            ++adapter.dwNumDevices;
        }
        if (adapter.dwNumDevices)
        {
            physicalAdapter[m_dwNumAdapters++] = physical;
            mappedAdapterCount = m_dwNumAdapters;
        }
    }
    if (!m_dwNumAdapters) return D3DAPPERR_NOCOMPATIBLEDEVICES;
    m_dwAdapter = 0;
    if (CLIPARAMS.mForceWindowed)
    {
        bool found = false;
        for (DWORD a = 0; a < m_dwNumAdapters && !found; ++a)
            for (DWORD d = 0; d < m_Adapters[a].dwNumDevices; ++d)
                if (m_Adapters[a].devices[d].bCanDoWindowed)
                { m_dwAdapter = a; m_Adapters[a].dwCurrentDevice = d; found = true; break; }
        if (!found) return D3DAPPERR_NOWINDOWABLEDEVICES;
    }
    m_bWindowed = CLIPARAMS.mForceWindowed;
    return S_OK;
}

BOOL CD3DApplication::FindDepthStencilFormat(UINT adapter, D3DDEVTYPE device, D3DFORMAT target, D3DFORMAT *depth)
{
    struct Choice { D3DFORMAT format; unsigned bits, stencil; };
    const Choice choices[] = {{D3DFMT_D24X8,24,0},{D3DFMT_D24S8,24,8},{D3DFMT_D32,32,0},
                              {D3DFMT_D16,16,0},{D3DFMT_D15S1,15,1},{D3DFMT_D24S8,24,8},
                              {D3DFMT_D24X4S4,24,4},{D3DFMT_D32,32,0}};
    for (unsigned i = 0; i < sizeof(choices) / sizeof(choices[0]); ++i)
    {
        const Choice &c = choices[i];
        // The first three choices are the game's preferred no-stencil group (including its D24S8 fallback).
        bool eligible = i < 3 ? m_dwMinDepthBits <= 24 && !m_dwMinStencilBits :
            m_dwMinDepthBits <= c.bits && m_dwMinStencilBits <= c.stencil;
        if (!eligible || (c.stencil == 0 && m_dwMinStencilBits)) continue;
        if (SUCCEEDED(m_pD3D->CheckDeviceFormat(adapter, device, target, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, c.format)) &&
            SUCCEEDED(m_pD3D->CheckDepthStencilMatch(adapter, device, target, target, c.format)))
        { *depth = c.format; return TRUE; }
    }
    return FALSE;
}

HRESULT CD3DApplication::FinishDeviceChange()
{
    HRESULT result = m_pd3dDevice->GetDeviceCaps(&m_d3dCaps);
    if (FAILED(result)) return result;
    IDirect3DSurface9 *surface = NULL;
    result = m_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface);
    if (FAILED(result)) return result;
    result = surface->GetDesc(&m_d3dsdBackBuffer);
    Drop(surface);
    if (FAILED(result)) return result;
    if (m_bShowCursorWhenFullscreen && !m_bWindowed)
    {
        D3DUtil_SetDeviceCursor(m_pd3dDevice, reinterpret_cast<HCURSOR>(GetClassLong(m_hWnd, GCL_HCURSOR)));
        m_pd3dDevice->ShowCursor(TRUE);
    }
    return S_OK;
}

HRESULT CD3DApplication::Initialize3DEnvironment(bool reset)
{
    WindowModeState *window = CurrentWindowState(*this);
    if (!m_pD3D || !window || (!m_bWindowed && window->only)) return E_FAIL;
    ModeChange change(*this, *window);
    if (!reset && m_pd3dDevice)
    {
        // Reset can fail without releasing the device. A fresh creation (including REF fallback) must relinquish
        // the old ownership first, after its callback-owned objects and query. Reset's caller already released
        // those objects; the live flag also covers a direct fresh-device request without calling callbacks twice.
        if (deviceObjectsLive)
        {
            HRESULT released = ReleaseDeviceObjects(false);
            if (FAILED(released)) return released;
            deviceObjectsLive = false;
        }
        Drop(resourceQuery);
        Drop(m_pd3dDevice);
    }
    TEXTURE_RES_LOSS_SHIFT.Set(-1.f);
    TEXTURE_ALLOW_32_BIT.Set(-1.f);
    LoadCardId(const_cast<char *>(CLIPARAMS.mCardID ? "c:\\cardid.txt" : "cardid.txt"));
    bool lockable = TRY_LOCKABLE_BB.mValue != 0;
    HRESULT result = E_FAIL;
    for (unsigned attempt = 0; attempt < 7; ++attempt)
    {
        D3DAdapterInfo &adapter = m_Adapters[m_dwAdapter];
        D3DDeviceInfo &device = *GetCurrentDevice();
        if (!m_bWindowed) RestoreMode(device, *window);
        if (device.dwCurrentMode >= device.dwNumModes) return E_FAIL;
        if (attempt == 6) { device.MultiSampleType = D3DMULTISAMPLE_NONE; lockable = false;
                            if (!m_bWindowed) device.dwCurrentMode = ModeAtSize(device, m_dwCreationWidth, m_dwCreationHeight); }
        if (m_bWindowed && (!window->valid || !PublishWindowMode(*this, *window, reset))) return E_FAIL;
        D3DModeInfo &mode = device.modes[device.dwCurrentMode];
        if (reset && m_pd3dDevice && !m_bWindowed &&
            FAILED(ConfirmDevice(&device.d3dCaps, m_dwCreateFlags, mode.Format))) return E_FAIL;
        AdjustWindowForChange();
        memset(&m_d3dpp, 0, sizeof(m_d3dpp));
        m_d3dpp.Windowed = m_bWindowed;
        m_d3dpp.BackBufferCount = CLIPARAMS.mBackBuffers;
        m_d3dpp.MultiSampleType = static_cast<int>(device.MultiSampleType) == -1
            ? static_cast<D3DMULTISAMPLE_TYPE>(SUGGEST_MULTISAMPLE_TYPE.mValue) : device.MultiSampleType;
        m_d3dpp.EnableAutoDepthStencil = m_bUseDepthBuffer;
        m_d3dpp.AutoDepthStencilFormat = mode.DepthStencilFormat;
        m_d3dpp.SwapEffect = COPY_NOT_FLIP.mValue ? D3DSWAPEFFECT_COPY : D3DSWAPEFFECT_DISCARD;
        if (COPY_NOT_FLIP.mValue) m_d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
        m_d3dpp.hDeviceWindow = m_hWnd;
        m_d3dpp.BackBufferWidth = m_bWindowed ? m_rcWindowClient.right - m_rcWindowClient.left : mode.Width;
        m_d3dpp.BackBufferHeight = m_bWindowed ? m_rcWindowClient.bottom - m_rcWindowClient.top : mode.Height;
        m_d3dpp.BackBufferFormat = m_bWindowed ? adapter.d3ddmDesktop.Format : mode.Format;
        m_d3dpp.PresentationInterval = m_bWindowed ? D3DPRESENT_INTERVAL_DEFAULT :
            (CLIPARAMS.mVSync || FORCE_VSYNC.mValue ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE);
        if (lockable && m_d3dpp.MultiSampleType == D3DMULTISAMPLE_NONE) m_d3dpp.Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        UpdateScreenShape();
        Drop(resourceQuery);
        bool resetting = reset && m_pd3dDevice;
        if (resetting)
            result = m_pd3dDevice->Reset(&m_d3dpp);
        else
        {
            // Keep the owning member out of an API's output argument: a failed factory call may clear or write it.
            IDirect3DDevice9 *created = NULL;
            result = m_pD3D->CreateDevice(PortPublicAdapterOrdinal(m_dwAdapter), device.DeviceType, m_hWndFocus,
                mode.dwBehavior | (CLIPARAMS.mUnknown185 ? D3DCREATE_DISABLE_DRIVER_MANAGEMENT : 0), &m_d3dpp, &created);
            if (SUCCEEDED(result) && created) m_pd3dDevice = created;
            else
            {
                Drop(created);
                if (SUCCEEDED(result)) result = E_FAIL;
            }
        }
        if (SUCCEEDED(result))
        {
            if (!resetting) m_dwCreateFlags = mode.dwBehavior;
            if (CLIPARAMS.mUnknown185) m_pd3dDevice->CreateQuery(D3DQUERYTYPE_RESOURCEMANAGER, &resourceQuery);
            if (m_bWindowed) SetWindowPos(m_hWnd, HWND_NOTOPMOST, m_rcWindowBounds.left, m_rcWindowBounds.top,
                m_rcWindowBounds.right - m_rcWindowBounds.left, m_rcWindowBounds.bottom - m_rcWindowBounds.top, SWP_SHOWWINDOW);
            result = FinishDeviceChange();
            if (SUCCEEDED(result)) { deviceObjectsLive = true; result = InitDeviceObjects(); }
            if (SUCCEEDED(result)) { m_bActive = TRUE; change.committed = true; return S_OK; }
            if (deviceObjectsLive) { InvalidateDeviceObjects(); DeleteDeviceObjects(); deviceObjectsLive = false; }
            Drop(resourceQuery);
            Drop(m_pd3dDevice);
            break;
        }
        if (result == D3DERR_DEVICELOST) { Sleep(100); continue; }
        if (lockable) { lockable = false; continue; }
        if (m_d3dpp.MultiSampleType != D3DMULTISAMPLE_NONE) { device.MultiSampleType = D3DMULTISAMPLE_NONE; continue; }
        if (m_bWindowed) break;
        DWORD fallback = ModeAtSize(device, m_dwCreationWidth, m_dwCreationHeight);
        if (fallback == device.dwCurrentMode) break;
        device.dwCurrentMode = fallback;
        lockable = true;
    }
    RestoreMode(*GetCurrentDevice(), *window);
    // The reference rasterizer is a final fallback, never a recursion back into HAL.
    if (GetCurrentDevice()->DeviceType == D3DDEVTYPE_HAL)
    {
        D3DAdapterInfo &adapter = m_Adapters[m_dwAdapter];
        for (DWORD i = 0; i < adapter.dwNumDevices; ++i)
            if (adapter.devices[i].DeviceType == D3DDEVTYPE_REF &&
                (m_bWindowed ? windowModes[m_dwAdapter][i].valid : !windowModes[m_dwAdapter][i].only))
            {
                adapter.dwCurrentDevice = i;
                // Preserve the existing real-mode REF recovery policy; a requested window never becomes fullscreen.
                m_bWindowed = adapter.devices[i].bWindowed = m_bWindowed || adapter.devices[i].bWindowed;
                result = Initialize3DEnvironment(false);
                if (SUCCEEDED(result)) change.committed = true;
                return result;
            }
    }
    return result;
}

HRESULT CD3DApplication::Resize3DEnvironment()
{
    WindowModeState *window = CurrentWindowState(*this);
    if (!window || (!m_bWindowed && window->only)) return E_FAIL;
    ModeChange change(*this, *window);
    if (m_bWindowed && (!window->valid || !PublishWindowMode(*this, *window, true))) return E_FAIL;
    if (m_bWindowed)
    {
        m_d3dpp.BackBufferFormat = window->mode.Format;
        m_d3dpp.AutoDepthStencilFormat = window->mode.DepthStencilFormat;
    }
    UpdateScreenShape();
    HRESULT result = InvalidateDeviceObjects();
    if (SUCCEEDED(result)) result = m_pd3dDevice->Reset(&m_d3dpp);
    if (SUCCEEDED(result)) result = FinishDeviceChange();
    if (SUCCEEDED(result)) result = RestoreDeviceObjects();
    if (SUCCEEDED(result) && !m_bFrameMoving) { m_bSingleStep = TRUE; DXUtil_Timer(TIMER_START); DXUtil_Timer(TIMER_STOP); }
    if (SUCCEEDED(result)) change.committed = true;
    return result;
}

HRESULT CD3DApplication::Reset3DEnvironment(BOOL skipPicker, bool showProgress)
{
    // The game's two callers pass TRUE. The retired sample picker is deliberately unsupported by the public host.
    if (!skipPicker) return E_NOTIMPL;
    BOOL wasReady = m_bReady;
    m_bReady = FALSE;
    HRESULT result = ReleaseDeviceObjects(showProgress);
    if (SUCCEEDED(result)) { deviceObjectsLive = false; result = Initialize3DEnvironment(true); }
    if (SUCCEEDED(result)) m_bReady = wasReady;
    if (SUCCEEDED(result) && !m_bFrameMoving) { m_bSingleStep = TRUE; DXUtil_Timer(TIMER_START); DXUtil_Timer(TIMER_STOP); }
    return result;
}

HRESULT CD3DApplication::AdjustWindowForChange()
{
    SetWindowLong(m_hWnd, GWL_STYLE, m_bWindowed ? m_dwWindowStyle : WS_POPUP | WS_SYSMENU | WS_VISIBLE);
    return S_OK;
}

HRESULT CD3DApplication::ForceWindowed(BOOL windowed)
{
    WindowModeState *state = CurrentWindowState(*this);
    if (!state || (windowed ? !state->valid : state->only)) return E_FAIL;
    // Reset stays on the existing adapter/device. Selecting a different provider requires fresh creation.
    m_bWindowed = GetCurrentDevice()->bWindowed = windowed;
    m_bReady = FALSE;
    HRESULT result = Reset3DEnvironment(TRUE, false);
    if (SUCCEEDED(result)) m_bReady = TRUE;
    return result;
}

HRESULT CD3DApplication::ToggleFullscreen()
{
    WindowModeState *state = CurrentWindowState(*this);
    if (!state || (m_bWindowed && state->only)) return E_FAIL;
    D3DDeviceInfo &device = *GetCurrentDevice();
    if (!m_bWindowed && !device.bCanDoWindowed) return ForceWindowed(TRUE);
    m_bReady = FALSE;
    device.bWindowed = m_bWindowed = !m_bWindowed;
    // No live caller currently uses this path. Reuse the same complete reset contract as the options menu.
    HRESULT result = Reset3DEnvironment(TRUE, false);
    if (SUCCEEDED(result)) m_bReady = TRUE;
    return result;
}

LRESULT CD3DApplication::MsgProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CLOSE: LT.TriggerQuit(); return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDM_EXIT) { SendMessage(window, WM_CLOSE, 0, 0); return 0; }
        if (LOWORD(wparam) == IDM_CHANGEDEVICE) return 0;
        break;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO *>(lparam)->ptMinTrackSize.x = 100;
        reinterpret_cast<MINMAXINFO *>(lparam)->ptMinTrackSize.y = 100;
        break;
    case WM_SIZE: m_bActive = wparam != SIZE_MAXHIDE && wparam != SIZE_MINIMIZED; break;
    case WM_ENTERSIZEMOVE: if (m_bFrameMoving) DXUtil_Timer(TIMER_STOP); break;
    case WM_EXITSIZEMOVE:
        if (m_bFrameMoving) DXUtil_Timer(TIMER_START);
        if (m_bActive && m_bWindowed)
        {
            RECT previous = m_rcWindowClient;
            GetWindowRect(m_hWnd, &m_rcWindowBounds);
            GetClientRect(m_hWnd, &m_rcWindowClient);
            if (previous.right - previous.left != m_rcWindowClient.right - m_rcWindowClient.left ||
                previous.bottom - previous.top != m_rcWindowClient.bottom - m_rcWindowClient.top)
            {
                m_bReady = FALSE;
                m_d3dpp.BackBufferWidth = m_rcWindowClient.right - m_rcWindowClient.left;
                m_d3dpp.BackBufferHeight = m_rcWindowClient.bottom - m_rcWindowClient.top;
                HRESULT result = Resize3DEnvironment();
                if (FAILED(result)) return DisplayErrorMsg(result, 1), 0;
                m_bReady = TRUE;
            }
        }
        break;
    case WM_ENTERMENULOOP: Pause(TRUE); break;
    case WM_EXITMENULOOP: Pause(FALSE); break;
    case WM_SETCURSOR:
        if (m_bActive && m_bReady && !m_bWindowed)
        {
            SetCursor(NULL);
            if (m_bShowCursorWhenFullscreen && m_pd3dDevice) m_pd3dDevice->ShowCursor(TRUE);
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (m_bActive && m_bReady && m_pd3dDevice)
        {
            POINT point;
            GetCursorPos(&point); ScreenToClient(window, &point);
            m_pd3dDevice->SetCursorPosition(point.x, point.y, 0);
        }
        break;
    case WM_NCHITTEST: if (!m_bWindowed) return HTCLIENT; break;
    case WM_POWERBROADCAST: if (wparam == 0 || wparam == 7) return TRUE; break;
    case WM_SYSCOMMAND:
        if (!m_bWindowed && (wparam == SC_MOVE || wparam == SC_SIZE || wparam == SC_MAXIMIZE ||
                            wparam == SC_KEYMENU || wparam == SC_MONITORPOWER)) return 1;
        break;
    }
    return DefWindowProc(window, message, wparam, lparam);
}

VOID CD3DApplication::Pause(BOOL paused)
{
    if (paused)
    {
        if (!pauseDepth++ && m_bFrameMoving) DXUtil_Timer(TIMER_STOP);
    }
    else if (pauseDepth && !--pauseDepth && m_bFrameMoving) DXUtil_Timer(TIMER_START);
    m_bReady = pauseDepth == 0;
}

VOID CD3DApplication::Cleanup3DEnvironment()
{
    m_bActive = m_bReady = FALSE;
    if (m_pd3dDevice && deviceObjectsLive) { ReleaseDeviceObjects(false); deviceObjectsLive = false; }
    DiscardWindowModes(*this);
    Drop(resourceQuery);
    Drop(m_pd3dDevice);
    Drop(m_pD3D);
    FinalCleanup();
}

HRESULT CD3DApplication::DisplayErrorMsg(HRESULT error, DWORD kind)
{
    char text[128];
    snprintf(text, sizeof(text), "Graphics operation failed (0x%08lx).", static_cast<unsigned long>(error));
    SETUP_HISTORY.AddMessage("%s", text);
    MessageBox(m_hWnd, text, TEXT("Onslaught CE"), MB_OK | MB_ICONERROR);
    if (kind == 1) SendMessage(m_hWnd, WM_CLOSE, 0, 0);
    return error;
}

void CD3DApplication::SetResolution(int width, int height)
{
    m_dwCreationWidth = width; m_dwCreationHeight = height;
}

void CD3DApplication::UpdateScreenShape()
{
    if (PortWideModes() && m_d3dpp.BackBufferWidth * 3 != m_d3dpp.BackBufferHeight * 4) mScreenShapeScale = 1.f;
    else if (SCREEN_SHAPE.mValue == 2) mScreenShapeScale = 1.f;
    else mScreenShapeScale = (SCREEN_SHAPE.mValue == 1 ? 16.f / 9.f : 4.f / 3.f) *
        (static_cast<float>(m_d3dpp.BackBufferHeight) / m_d3dpp.BackBufferWidth);
}
