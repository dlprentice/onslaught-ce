// SPDX-License-Identifier: GPL-3.0-only
// The public port's graphics host interface. All game consumers are rebuilt against this header; this is not an
// ABI declaration for the original executable. The field names are the interface used by its shell and options.
#ifndef D3DAPP_H
#define D3DAPP_H

#ifndef BEA_PUBLIC_D3DAPP_TEST
#include <d3d9.h>
#endif

// The options tables use compact indices; Direct3D calls require the provider's physical ordinal.
extern "C" unsigned PortPublicAdapterOrdinal(unsigned logicalAdapter);

#define D3DAPPERR_NODIRECT3D          0x82000001
#define D3DAPPERR_NOWINDOW            0x82000002
#define D3DAPPERR_NOCOMPATIBLEDEVICES 0x82000003
#define D3DAPPERR_NOWINDOWABLEDEVICES 0x82000004
#define D3DAPPERR_NOHARDWAREDEVICE    0x82000005
#define D3DAPPERR_HALNOTCOMPATIBLE    0x82000006
#define D3DAPPERR_NOWINDOWEDHAL       0x82000007
#define D3DAPPERR_NODESKTOPHAL        0x82000008
#define D3DAPPERR_NOHALTHISMODE       0x82000009
#define D3DAPPERR_NONZEROREFCOUNT     0x8200000a
#define D3DAPPERR_MEDIANOTFOUND       0x8200000b
#define D3DAPPERR_RESIZEFAILED        0x8200000c
#define D3DAPPERR_NULLREFDEVICE       0x8200000d

struct D3DModeInfo
{
    DWORD Width, Height;
    D3DFORMAT Format;
    DWORD dwBehavior;
    DWORD mUnknown10; // texture/depth-texture/multisample capability bits consumed by the game
    D3DFORMAT DepthStencilFormat;
};

struct D3DDeviceInfo
{
    D3DDEVTYPE DeviceType;
    D3DCAPS9 d3dCaps;
    const TCHAR *strDesc;
    BOOL bCanDoWindowed;
    DWORD dwNumModes;
    D3DModeInfo modes[150];
    DWORD dwCurrentMode;
    BOOL bWindowed;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    DWORD dwLowDetailMode, dwMediumDetailMode, dwHighDetailMode;
    D3DDeviceInfo();
};

struct D3DAdapterInfo
{
    D3DADAPTER_IDENTIFIER9 d3dAdapterIdentifier;
    D3DDISPLAYMODE d3ddmDesktop;
    DWORD dwNumDevices;
    D3DDeviceInfo devices[5];
    DWORD dwCurrentDevice;
};

class CD3DApplication
{
public:
    CD3DApplication();
    virtual HRESULT AdjustWindowForChange();
    virtual HRESULT ConfirmDevice(D3DCAPS9 *, DWORD, D3DFORMAT) { return S_OK; }
    virtual HRESULT OneTimeSceneInit() { return S_OK; }
    virtual HRESULT FrameMove() { return S_OK; }
    virtual HRESULT Render() { return S_OK; }
    virtual HRESULT InvalidateDeviceObjects() { return S_OK; }
    virtual HRESULT DeleteDeviceObjects() { return S_OK; }
    virtual HRESULT FinalCleanup() { return S_OK; }
    virtual HRESULT InitDeviceObjects() = 0;
    virtual HRESULT RestoreDeviceObjects() = 0;
    virtual HRESULT ReleaseDeviceObjects(bool showProgress) = 0;
    virtual HRESULT Create(HINSTANCE instance);
    virtual LRESULT MsgProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    virtual VOID Pause(BOOL paused);

    HWND GetHWnd() { return m_hWnd; }
    VOID Cleanup3DEnvironment();
    HRESULT Reset3DEnvironment(BOOL skipPicker, bool showProgress);
    D3DAdapterInfo *GetAdapterInfo(int adapter); // implemented by Career.cpp
    D3DDeviceInfo *GetCurrentDevice() { return &m_Adapters[m_dwAdapter].devices[m_Adapters[m_dwAdapter].dwCurrentDevice]; }
    void SetResolution(int width, int height);
    void UpdateScreenShape();

    // Internal entry points remain visible because the shell's reference-only fullscreen block names them.
    HRESULT BuildDeviceList();
    BOOL FindDepthStencilFormat(UINT adapter, D3DDEVTYPE device, D3DFORMAT target, D3DFORMAT *depth);
    HRESULT Initialize3DEnvironment(bool reset);
    HRESULT Resize3DEnvironment();
    HRESULT ToggleFullscreen();
    HRESULT ForceWindowed(BOOL windowed);
    HRESULT DisplayErrorMsg(HRESULT error, DWORD kind);

    D3DAdapterInfo m_Adapters[10];
    DWORD m_dwNumAdapters, m_dwAdapter;
    BOOL m_bWindowed, m_bActive, m_bReady, m_bFrameMoving, m_bSingleStep;
    D3DPRESENT_PARAMETERS m_d3dpp;
    float mScreenShapeScale;
    HWND m_hWnd, m_hWndFocus;
    LPDIRECT3D9 m_pD3D;
    LPDIRECT3DDEVICE9 m_pd3dDevice;
    D3DCAPS9 m_d3dCaps;
    D3DSURFACE_DESC m_d3dsdBackBuffer;
    DWORD m_dwCreateFlags, m_dwWindowStyle;
    RECT m_rcWindowBounds, m_rcWindowClient;
    FLOAT m_fTime, m_fElapsedTime, m_fFPS;
    TCHAR m_strDeviceStats[90], m_strFrameStats[40];
    TCHAR *m_strWindowTitle;
    BOOL m_bUseDepthBuffer;
    DWORD m_dwMinDepthBits, m_dwMinStencilBits, m_dwCreationWidth, m_dwCreationHeight;
    BOOL m_bShowCursorWhenFullscreen;

private:
    HRESULT FinishDeviceChange();
};
#endif
