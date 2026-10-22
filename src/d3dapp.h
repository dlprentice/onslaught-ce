// d3dapp.h: the Direct3D sample framework's application class, in the retail build's Direct3D 9 form (only what
// matched files need). Stuart's copy is the Direct3D 8 original; the retail layout is measured from its users:
// an adapter record is 0x516c bytes (the current device at +0x5168, devices from +0x460), a device 0xf68 (D3DCAPS9,
// 150 modes from +0x140, the current mode at +0xf50, the multisample type at +0xf58), a mode 24 bytes
// (CCareer::WriteOptions 00420b10, CD3DApplication::GetAdapterInfo 00420cd0). The application object LT is at
// 00855bb0: its adapters from +0x04, the adapter count +0x32e3c and the current adapter +0x32e40. d3dapp.cpp
// (00528dd0..0052c7dd) fills in the rest: Stuart's members keep his order at their Direct3D 9 sizes.

#ifndef D3DAPP_H
#define D3DAPP_H

#include <d3d9.h>

//-----------------------------------------------------------------------------
// Error codes
//-----------------------------------------------------------------------------
// (APPMSGTYPE is d3dapp.cpp's: an enum here changes CGame::DrawDebugStuff's registers, 00470825)

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
#define D3DAPPERR_NULLREFDEVICE       0x8200000d		// retail (DisplayErrorMsg 0052c661); the Direct3D 9 framework's name


struct D3DModeInfo
{
	DWORD      Width;      // Screen width in this mode
	DWORD      Height;     // Screen height in this mode
	D3DFORMAT  Format;     // Pixel format in this mode
	DWORD      dwBehavior; // Hardware / Software / Mixed vertex processing
	DWORD      mUnknown10;			// retail: the texture and multisample formats this mode can use (BuildDeviceList
									// 00529fcc: 1 DXT1, 2 DXT2, 4 A8R8G8B8, 8 D24X8, 0x10 D16, 0x20.. multisample 2,3,4,6,8)
	D3DFORMAT  DepthStencilFormat; // Which depth/stencil format to use with this mode (Initialize3DEnvironment 0052b099)
};

struct D3DDeviceInfo
{
	// Device data
	D3DDEVTYPE   DeviceType;      // Reference, HAL, etc.
	D3DCAPS9     d3dCaps;         // Capabilities of this device
	const TCHAR* strDesc;         // Name of this device
	BOOL         bCanDoWindowed;  // Whether this device can work in windowed mode

	// Modes for this device
	DWORD        dwNumModes;
	D3DModeInfo  modes[150];

	// Current state
	DWORD        dwCurrentMode;
	BOOL         bWindowed;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	// retail: the modes the detail presets use (CVideoDetailLevel, PauseMenu.cpp); BuildDeviceList picks them
	DWORD        dwLowDetailMode;		// +0xf5c: mode 0 (0052a44b)
	DWORD        dwMediumDetailMode;	// +0xf60: the 800x600 mode (0052a488)
	DWORD        dwHighDetailMode;		// +0xf64: the 1280x1024 mode, else the 1024x768 (0052a4cd)

	D3DDeviceInfo();			// 0052c750: no mode or multisample type chosen (-1)
};

struct D3DAdapterInfo
{
	// Adapter data
	D3DADAPTER_IDENTIFIER9 d3dAdapterIdentifier;
	D3DDISPLAYMODE d3ddmDesktop;      // Desktop display mode for this adapter

	// Devices for this adapter
	DWORD          dwNumDevices;
	D3DDeviceInfo  devices[5];

	// Current state
	DWORD          dwCurrentDevice;
};

// The retail table (005e4ab4, 14 slots; PCLTShell's 005e488c) has no destructor. Slots 2-4 return S_OK in both classes.
class CD3DApplication
{
public:
					CD3DApplication();							// 00528f80
	virtual HRESULT AdjustWindowForChange();									// 0
	virtual HRESULT ConfirmDevice(D3DCAPS9*,DWORD,D3DFORMAT)	{ return S_OK; }	// 1 (00529060)
	virtual HRESULT OneTimeSceneInit()							{ return S_OK; }	// 2
	virtual HRESULT FrameMove()									{ return S_OK; }	// 3
	virtual HRESULT Render()									{ return S_OK; }	// 4
	virtual HRESULT InvalidateDeviceObjects()					{ return S_OK; }	// 5
	virtual HRESULT DeleteDeviceObjects()						{ return S_OK; }	// 6
	virtual HRESULT FinalCleanup()								{ return S_OK; }	// 7
	virtual HRESULT InitDeviceObjects() = 0;									// 8
	virtual HRESULT RestoreDeviceObjects() = 0;									// 9
	virtual HRESULT ReleaseDeviceObjects(bool show_progress) = 0;				// 10 (retail)
	virtual HRESULT Create( HINSTANCE hInstance );								// 11
	virtual LRESULT MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );	// 12
	virtual VOID    Pause( BOOL bPause );										// 13 (0052c3c0)
	// Stuart's accessor (his d3dapp.h:200), never called. DECLARATION-COUNT CARRIER: with
	// CMeshCollisionVolume::CollideWithMesh out of line (MeshCollisionVolume.h) the witness BuildDeviceList (00529350)
	// needs one more declaration; this one keeps DrawDebugStuff, the CPDTimeline rows and CFEPGoodies::Render too.
	HWND	GetHWnd() {return m_hWnd;};

	VOID			Cleanup3DEnvironment();						// 0052c430
	HRESULT			Reset3DEnvironment(BOOL a, bool b);			// 0052bb80: Stuart's UserSelectNewDevice, whose dialog a skips;
																// b goes to ReleaseDeviceObjects
	D3DAdapterInfo	*GetAdapterInfo(int adapter);		// -1 is the current adapter (00420cd0)
	void			SetResolution(int width, int height);	// 0052c730
	void			UpdateScreenShape();					// 0052c780: the aspect SCREEN_SHAPE picks (CScreenShape 004cef87)

#ifdef D3DAPP_CPP
	// Only d3dapp.cpp calls these. Every declaration a file sees shifts some register ties (CGame::DrawDebugStuff,
	// 00470825, flips with the count), so the other files see the class without them; they add no data or vtable slot.
	// Internal error handling function
	HRESULT			DisplayErrorMsg( HRESULT hr, DWORD dwType );	// 0052c4f0

	// Internal functions to manage and render the 3D scene
	HRESULT			BuildDeviceList();							// 00529350
	BOOL			FindDepthStencilFormat( UINT iAdapter, D3DDEVTYPE DeviceType,
						D3DFORMAT TargetFormat, D3DFORMAT* pDepthStencilFormat );	// 0052a830
	HRESULT			Initialize3DEnvironment(bool reset);		// 0052af00: resets the device it has when asked
	HRESULT			Resize3DEnvironment();						// 0052b760
	HRESULT			ToggleFullscreen();							// 0052b840
	HRESULT			ForceWindowed(BOOL windowed);				// 0052ba50: to a device that can (or cannot) run windowed
	static INT_PTR CALLBACK SelectDeviceProc( HWND hDlg, UINT msg,
						WPARAM wParam, LPARAM lParam );			// 0052bc80
#endif
	D3DDeviceInfo	*GetCurrentDevice() { return &m_Adapters[m_dwAdapter].devices[m_Adapters[m_dwAdapter].dwCurrentDevice]; }

	// Internal variables for the state of the app
	D3DAdapterInfo    m_Adapters[10];
	DWORD             m_dwNumAdapters;
	DWORD             m_dwAdapter;
	BOOL              m_bWindowed;         // +0x32e44
	BOOL              m_bActive;           // +0x32e48
	BOOL              m_bReady;            // +0x32e4c

	// Internal variables used for timing
	BOOL              m_bFrameMoving;      // +0x32e50
	BOOL              m_bSingleStep;       // +0x32e54

	// Main objects used for creating and rendering the 3D scene
	D3DPRESENT_PARAMETERS m_d3dpp;         // +0x32e58 Parameters for CreateDevice/Reset
	float             mScreenShapeScale;   // +0x32e90 retail: the aspect scale UpdateScreenShape sets
	HWND              m_hWnd;              // +0x32e94 (00888a44) The main app window
	HWND              m_hWndFocus;         // The D3D focus window (usually same as m_hWnd)
	LPDIRECT3D9       m_pD3D;              // +0x32e9c The main D3D object
	LPDIRECT3DDEVICE9 m_pd3dDevice;        // +0x32ea0 (00888a50) The D3D rendering device
	D3DCAPS9          m_d3dCaps;           // +0x32ea4 Caps for the device
	D3DSURFACE_DESC   m_d3dsdBackBuffer;   // +0x32fd4 Surface desc of the backbuffer
	DWORD             m_dwCreateFlags;     // +0x32ff4 Indicate sw or hw vertex processing
	DWORD             m_dwWindowStyle;     // +0x32ff8 Saved window style for mode switches
	RECT              m_rcWindowBounds;    // +0x32ffc Saved window bounds for mode switches
	RECT              m_rcWindowClient;    // +0x3300c Saved client area size for mode switches

	// Variables for timing
	FLOAT             m_fTime;             // +0x3301c Current time in seconds
	FLOAT             m_fElapsedTime;      // +0x33020 Time elapsed since last frame
	FLOAT             m_fFPS;              // +0x33024 Instanteous frame rate
	TCHAR             m_strDeviceStats[90];// +0x33028 String to hold D3D device stats
	TCHAR             m_strFrameStats[40]; // +0x33082 String to hold frame stats

	// Overridable variables for the app (defaults from CD3DApplication's constructor, 00528f80)
	TCHAR*            m_strWindowTitle;    // +0x330ac Title for the app's window
	BOOL              m_bUseDepthBuffer;   // Whether to autocreate depthbuffer
	DWORD             m_dwMinDepthBits;    // Minimum number of bits needed in depth buffer (16)
	DWORD             m_dwMinStencilBits;  // Minimum number of bits needed in stencil buffer
	DWORD             m_dwCreationWidth;   // +0x330bc Width used to create window (640; -res)
	DWORD             m_dwCreationHeight;  // +0x330c0 Height used to create window (480)
	BOOL              m_bShowCursorWhenFullscreen; // Whether to show cursor when fullscreen
};


#endif
