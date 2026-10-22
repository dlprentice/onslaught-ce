#include "common.h"	// FUNCTION: 0x00528dd0 init:ZERO_FVECTOR
#define D3DAPP_CPP			// d3dapp.h: declare the framework's internal functions
					// FUNCTION: 0x00528df0 init:ID_FMATRIX

#if TARGET == PC

//-----------------------------------------------------------------------------
// File: D3DApp.cpp
//
// Desc: Application class for the Direct3D samples framework library.
//
// Copyright (c) 1998-2000 Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
// Stuart's Direct3D 8 framework as the retail PC build ported it to Direct3D 9 (00528dd0..0052c7dd). His functions
// keep their order and shape; the retail build adds the setup log, the card tweaks, the display-mode choice and the
// device-creation retries, and drops the capture menu.
//#define STRICT
#include <windows.h>
#include <windowsx.h>
#include <basetsd.h>
#include <mmsystem.h>
#include <stdio.h>
#include <tchar.h>
#include <D3D9.h>
#include <dxerr9.h>
#include "d3dapp.h"
#undef new						// MemoryManager.h's new(type) macro would rewrite D3DX's operator new declarations (as in DX.h)
#include "D3DUtil.h"			// the DirectX 9 SDK sample framework's (the build's DX9 SDK samples include path), as Stuart's d3dapp.cpp:21-22
#define new(type) new(type, __FILE__, __LINE__)
#include "DXUtil.h"
#include "DisplayModeKey.h"

// the error message types (Stuart's d3dapp.h)
enum APPMSGTYPE { MSG_NONE, MSGERR_APPMUSTEXIT, MSGWARN_SWITCHEDTOREF };
#include "D3DRes.h"

#include "resource.h"
#include "Capture.h"
#include "cliparams.h"
#include "DX.h"
#include "ltshell.h"
#include "DebugLog.h"
#include "console.h"
#include "tweak.h"
#include "PauseMenu.h"

// the sample framework's cursor helper (d3dutil.cpp; the retail call takes two arguments, 0052b63f)
HRESULT D3DUtil_SetDeviceCursor( LPDIRECT3DDEVICE9 pd3dDevice, HCURSOR hCursor );

// the card's tweaks, read from cardid.txt (005286e0)
void	LoadCardId(char *filename);

extern CTweakSLONG	SCREEN_SHAPE;				// 0082b478
extern CTweakSLONG	TEXTURE_ALLOW_32_BIT;		// 009cc0e8
extern CTweakSLONG	TEXTURE_RES_LOSS_SHIFT;		// 009cc108
extern bool			gDeviceHasLargeTextures;	// 0088907c

CTweakSLONG	ALLOW_WIDESCREEN_MODES("ALLOW_WIDESCREEN_MODES", 0);	// 0089c0a0
// FUNCTION: 0x00528ec0 init:ALLOW_WIDESCREEN_MODES
// FUNCTION: 0x00528ee0 exit:ALLOW_WIDESCREEN_MODES
CTweakSLONG	COPY_NOT_FLIP("COPY_NOT_FLIP", 0);						// 0089c090
// FUNCTION: 0x00528ef0 init:COPY_NOT_FLIP
// FUNCTION: 0x00528f10 exit:COPY_NOT_FLIP
CTweakSLONG	SUGGEST_MULTISAMPLE_TYPE("SUGGEST_MULTISAMPLE_TYPE", 0);	// 0089c080
// FUNCTION: 0x00528f20 init:SUGGEST_MULTISAMPLE_TYPE
// FUNCTION: 0x00528f40 exit:SUGGEST_MULTISAMPLE_TYPE
CTweakSLONG	FORCE_VSYNC("FORCE_VSYNC", 0);							// 0089c070
// FUNCTION: 0x00528f50 init:FORCE_VSYNC
// FUNCTION: 0x00528f70 exit:FORCE_VSYNC

//-----------------------------------------------------------------------------
// Global access to the app (needed for the global WndProc())
//-----------------------------------------------------------------------------
static CD3DApplication* g_pD3DApp = NULL;							// 0089c0f4

// gResourceQuery is defined after TRY_LOCKABLE_BB, below



#define DX_SCREEN_WIDTH		640
#define DX_SCREEN_HEIGHT	480

// the formats a display mode may use (BuildDeviceList 0052969e)
static inline bool Is32BitFormat(D3DFORMAT format)
{
	if ((format == D3DFMT_X8R8G8B8) || (format == D3DFMT_A8R8G8B8) || (format == D3DFMT_R8G8B8))
		return true;
	return false;
}

// a mode of the given size, preferring a 32-bit one (BuildDeviceList 0052a3d2, Initialize3DEnvironment 0052afc0)
static inline DWORD FindMode(D3DDeviceInfo *pDevice, DWORD width, DWORD height)
{
	DWORD	found = 0;
	for (DWORD m = 0; m < pDevice->dwNumModes; m++)
	{
		if (pDevice->modes[m].Width == width && pDevice->modes[m].Height == height)
		{
			found = m;
			if( pDevice->modes[m].Format == D3DFMT_R8G8B8 ||
				pDevice->modes[m].Format == D3DFMT_A8R8G8B8 ||
				pDevice->modes[m].Format == D3DFMT_X8R8G8B8)
			{
				return m;
			}
		}
	}
	return found;
}

// the setup log's name for a format (BuildDeviceList 00529527)
static inline char *FormatName(D3DFORMAT format)
{
	switch (format)
	{
	case D3DFMT_UNKNOWN:	return "D3DFMT_UNKNOWN";
	case D3DFMT_X8R8G8B8:	return "D3DFMT_X8R8G8B8";
	case D3DFMT_A8R8G8B8:	return "D3DFMT_A8R8G8B8";
	case D3DFMT_R5G6B5:		return "D3DFMT_R5G6B5";
	case D3DFMT_A1R5G5B5:	return "D3DFMT_A1R5G5B5";
	case D3DFMT_DXT1:		return "D3DFMT_DXT1";
	case D3DFMT_A4R4G4B4:	return "D3DFMT_A4R4G4B4";
	case D3DFMT_DXT2:		return "D3DFMT_DXT2";
	default:				return "<unknown format>";
	case D3DFMT_DXT4:		return "D3DFMT_DXT4";
	}
}

// a display mode in the setup log (BuildDeviceList 00529519, 005297cf)
static inline void LogDisplayMode(D3DDISPLAYMODE *mode)
{
	SETUP_HISTORY.AddMessage("{%dx%d: %d (%s)}", mode->Width, mode->Height, mode->RefreshRate, FormatName(mode->Format));
}

// the setup log's name for a device type (BuildDeviceList 00529a25)
static inline char *DeviceTypeName(D3DDEVTYPE type)
{
	switch (type)
	{
	case D3DDEVTYPE_HAL:	return "HAL";
	case D3DDEVTYPE_REF:	return "REF";
	case D3DDEVTYPE_SW:		return "SW";
	default:				return "??";
	}
}

//-----------------------------------------------------------------------------
// Name: CD3DApplication()
// Desc: Constructor
//-----------------------------------------------------------------------------
CD3DApplication::CD3DApplication()	// FUNCTION: 0x00528f80
{
    g_pD3DApp           = this;

    m_dwNumAdapters     = 0;
    m_dwAdapter         = 0L;
    m_pD3D              = NULL;
    m_pd3dDevice        = NULL;
    m_hWnd              = NULL;
    m_hWndFocus         = NULL;
    m_bActive           = FALSE;
    m_bReady            = FALSE;
    m_dwCreateFlags     = 0L;

    m_bFrameMoving      = TRUE;
    m_bSingleStep       = FALSE;
    m_fFPS              = 0.0f;
    m_strDeviceStats[0] = '\0';
    m_strFrameStats[0]  = '\0';

    m_strWindowTitle    = ToTCHAR("D3D8 Application");
    m_dwCreationWidth   = DX_SCREEN_WIDTH;
    m_dwCreationHeight  = DX_SCREEN_HEIGHT;
    m_bUseDepthBuffer   = FALSE;
    m_dwMinDepthBits    = 16;
    m_dwMinStencilBits  = 0;
    m_bShowCursorWhenFullscreen = FALSE;
}
// FUNCTION: 0x00529060 ?ConfirmDevice@CD3DApplication@@UAEJPAU_D3DCAPS9@@KW4_D3DFORMAT@@@Z




//-----------------------------------------------------------------------------
// Name: WndProc()
// Desc: Static msg handler which passes messages to the application class.
//-----------------------------------------------------------------------------
LRESULT CALLBACK WndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )	// FUNCTION: 0x00529070
{
    return g_pD3DApp->MsgProc( hWnd, uMsg, wParam, lParam );
}




//-----------------------------------------------------------------------------
// Name: Create()
// Desc:
//-----------------------------------------------------------------------------
HRESULT CD3DApplication::Create( HINSTANCE hInstance )	// FUNCTION: 0x005290a0
{
    HRESULT hr;

    // Create the Direct3D object
	SETUP_HISTORY.AddMessage("Direct3DCreate8...");
    m_pD3D = Direct3DCreate9( D3D_SDK_VERSION );
    if( m_pD3D == NULL )
	{
		SETUP_HISTORY.AddMessage("Direct3DCreate8 failed.");
        return DisplayErrorMsg( D3DAPPERR_NODIRECT3D, MSGERR_APPMUSTEXIT );
	}
	SETUP_HISTORY.AddMessage("Direct3DCreate8 succeeded.");

    // Build a list of Direct3D adapters, modes and devices. The
    // ConfirmDevice() callback is used to confirm that only devices that
    // meet the app's requirements are considered.
    if( FAILED( hr = BuildDeviceList() ) )
    {
		SETUP_HISTORY.AddMessage("BuildDeviceList() failed");
        SAFE_RELEASE( m_pD3D );
        return DisplayErrorMsg( hr, MSGERR_APPMUSTEXIT );
    }

    // Unless a substitute hWnd has been specified, create a window to
    // render into
    if( m_hWnd == NULL)
    {
        // Register the windows class
        WNDCLASS wndClass = { 0, WndProc, 0, 0, hInstance,
                              LoadIcon( hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON) ),
                              LoadCursor( NULL, IDC_ARROW ),
                              (HBRUSH)GetStockObject(WHITE_BRUSH),
                              NULL, ToTCHAR("BEA") };
        RegisterClass( &wndClass );

        // Set the window's initial style
        m_dwWindowStyle = WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|
                          WS_MINIMIZEBOX|WS_VISIBLE;

        // Set the window's initial width
        RECT rc;
        SetRect( &rc, 0, 0, m_dwCreationWidth, m_dwCreationHeight );
        AdjustWindowRect( &rc, m_dwWindowStyle, TRUE );

        // Create the render window
        m_hWnd = CreateWindow( ToTCHAR("BEA"), m_strWindowTitle, m_dwWindowStyle,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               (rc.right-rc.left), (rc.bottom-rc.top), 0L,
                               LoadMenu( hInstance, MAKEINTRESOURCE(IDR_MENU) ),
                               hInstance, 0L );
    }

    // The focus window can be a specified to be a different window than the
    // device window.  If not, use the device window as the focus window.
    if( m_hWndFocus == NULL )
        m_hWndFocus = m_hWnd;

    // Save window properties
    m_dwWindowStyle = GetWindowLong( m_hWnd, GWL_STYLE );
    GetWindowRect( m_hWnd, &m_rcWindowBounds );
    GetClientRect( m_hWnd, &m_rcWindowClient );

    // Initialize the 3D environment for the app
    if( FAILED( hr = Initialize3DEnvironment(false) ) )
    {
        SAFE_RELEASE( m_pD3D );
        return DisplayErrorMsg( hr, MSGERR_APPMUSTEXIT );
    }

    // Setup the app so it can support single-stepping
    DXUtil_Timer( TIMER_START );
	SETUP_HISTORY.AddMessage("Started timer");

    // The app is ready to go
    m_bReady = TRUE;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: SortModesCallback()
// Desc: Callback function for sorting display modes (used by BuildDeviceList).
//-----------------------------------------------------------------------------
int SortModesCallback( const VOID* arg1, const VOID* arg2 )	// FUNCTION: 0x00529300
{
    D3DDISPLAYMODE* p1 = (D3DDISPLAYMODE*)arg1;
    D3DDISPLAYMODE* p2 = (D3DDISPLAYMODE*)arg2;

    if( p1->Format > p2->Format )   return -1;
    if( p1->Format < p2->Format )   return +1;
    if( p1->Width  < p2->Width )    return -1;
    if( p1->Width  > p2->Width )    return +1;
    if( p1->Height < p2->Height )   return -1;
    if( p1->Height > p2->Height )   return +1;

    return 0;
}




//-----------------------------------------------------------------------------
// Name: BuildDeviceList()
// Desc:
//-----------------------------------------------------------------------------
// The saved-mode lookup compares a complete packed key for each enumerated mode.
static inline CDisplayModeKey MakeDisplayModeKey(const D3DModeInfo& mode)
{
	CDisplayModeKey key;
	key.mWidth = mode.Width;
	key.mHeight = mode.Height;
	key.m32Bit = Is32BitFormat(mode.Format) ? 1 : 0;
	return key;
}

HRESULT CD3DApplication::BuildDeviceList()	// FUNCTION: 0x00529350
{
    const DWORD dwNumDeviceTypes = 2;
    TCHAR strDeviceDescs[2][16];

	_tcscpy(strDeviceDescs[0],ToTCHAR("HAL"));
	_tcscpy(strDeviceDescs[1],ToTCHAR("REF"));

    const D3DDEVTYPE DeviceTypes[] = { D3DDEVTYPE_HAL, D3DDEVTYPE_REF };

    BOOL bHALExists = FALSE;
    BOOL bHALIsWindowedCompatible = FALSE;
    BOOL bHALIsDesktopCompatible = FALSE;
    BOOL bHALIsSampleCompatible = FALSE;

    // Loop through all the adapters on the system (usually, there's just one
    // unless more than one graphics card is present).
	UINT	dwNumAdapters = m_pD3D->GetAdapterCount();
	SETUP_HISTORY.AddMessage("D3D->GetAdapterCount(): %d\n", dwNumAdapters);
    for( UINT iAdapter = 0; iAdapter < dwNumAdapters; iAdapter++ )
    {
        // Fill in adapter info
        D3DAdapterInfo* pAdapter  = &m_Adapters[m_dwNumAdapters];
        m_pD3D->GetAdapterIdentifier( iAdapter, 0, &pAdapter->d3dAdapterIdentifier );
		SETUP_HISTORY.AddMessage("Adapter %d", iAdapter);
		SETUP_HISTORY.AddMessage("Driver : %s", pAdapter->d3dAdapterIdentifier.Driver);
		SETUP_HISTORY.AddMessage("Desc   : %s", pAdapter->d3dAdapterIdentifier.Description);
		SETUP_HISTORY.AddMessage("Version: %08x%08x", pAdapter->d3dAdapterIdentifier.DriverVersion.HighPart,
													  pAdapter->d3dAdapterIdentifier.DriverVersion.LowPart);
		SETUP_HISTORY.AddMessage("IDs    : V: %x, D: %x, SS: %x, R: %x", pAdapter->d3dAdapterIdentifier.VendorId,
																		pAdapter->d3dAdapterIdentifier.DeviceId,
																		pAdapter->d3dAdapterIdentifier.SubSysId,
																		pAdapter->d3dAdapterIdentifier.Revision);
		SETUP_HISTORY.AddMessage("WHQL   : %d", pAdapter->d3dAdapterIdentifier.WHQLLevel);
        m_pD3D->GetAdapterDisplayMode( iAdapter, &pAdapter->d3ddmDesktop );
		LogDisplayMode(&pAdapter->d3ddmDesktop);
        pAdapter->dwNumDevices    = 0;
        pAdapter->dwCurrentDevice = 0;

        // Enumerate all display modes on this adapter
        D3DDISPLAYMODE modes[100];
        D3DFORMAT      formats[20];
        DWORD dwNumFormats      = 0;
        DWORD dwNumModes        = 0;

        // Add the adapter's current desktop format to the list of formats
        formats[dwNumFormats++] = pAdapter->d3ddmDesktop.Format;

		// the adapter formats whose modes are listed
		const D3DFORMAT AdapterFormats[] = { D3DFMT_R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5 };

		for( UINT iFormat = 0; iFormat < 4; iFormat++ )
		{
			D3DFORMAT AdapterFormat = AdapterFormats[iFormat];
			DWORD dwNumAdapterModes = m_pD3D->GetAdapterModeCount( iAdapter, AdapterFormat );

			for( UINT iMode = 0; iMode < dwNumAdapterModes; iMode++ )
			{
				// Get the display mode attributes
				D3DDISPLAYMODE DisplayMode;
				m_pD3D->EnumAdapterModes( iAdapter, AdapterFormat, iMode, &DisplayMode );

				// Filter out low-resolution modes
				if( DisplayMode.Width  < 640 || DisplayMode.Height < 480 )
					continue;

				// and, unless asked, modes that are not 4:3
				if( !ALLOW_WIDESCREEN_MODES.mValue && ( DisplayMode.Width * 3 != DisplayMode.Height * 4 ) )
					continue;

				if( !Is32BitFormat( DisplayMode.Format ) )
					continue;

				// Check if the mode already exists (to filter out refresh rates)
				DWORD m; for (m=0L; m<dwNumModes; m++ )
				{
					if( ( modes[m].Width  == DisplayMode.Width  ) &&
						( modes[m].Height == DisplayMode.Height ) &&
						( Is32BitFormat( modes[m].Format ) == Is32BitFormat( DisplayMode.Format ) ) )
						break;
				}

				// If we found a new mode, add it to the list of modes
				if( m == dwNumModes )
				{
					modes[dwNumModes].Width       = DisplayMode.Width;
					modes[dwNumModes].Height      = DisplayMode.Height;
					modes[dwNumModes].Format      = DisplayMode.Format;
					modes[dwNumModes].RefreshRate = 0;
					dwNumModes++;

					// Check if the mode's format already exists
					DWORD f; for (f=0; f<dwNumFormats; f++ )
					{
						if( DisplayMode.Format == formats[f] )
							break;
					}

					// If the format is new, add it to the list
					if( f== dwNumFormats )
						formats[dwNumFormats++] = DisplayMode.Format;
				}
			}
		}

        // Sort the list of display modes (by format, then width, then height)
        qsort( modes, dwNumModes, sizeof(D3DDISPLAYMODE), SortModesCallback );

		SETUP_HISTORY.AddMessage("Available modes:");
		for( DWORD m=0L; m<dwNumModes; m++ )
			LogDisplayMode(&modes[m]);

        // Add devices to adapter
        for( UINT iDevice = 0; iDevice < dwNumDeviceTypes; iDevice++ )
        {
            // Fill in device info
            D3DDeviceInfo* pDevice;
            pDevice                 = &pAdapter->devices[pAdapter->dwNumDevices];
            pDevice->DeviceType     = DeviceTypes[iDevice];
            m_pD3D->GetDeviceCaps( iAdapter, DeviceTypes[iDevice], &pDevice->d3dCaps );
			TCHAR*	strDesc			= strDeviceDescs[iDevice];
            pDevice->strDesc        = strDesc;
            pDevice->dwNumModes     = 0;
            pDevice->bCanDoWindowed = FALSE;
            pDevice->bWindowed      = FALSE;

			SETUP_HISTORY.AddMessage("%s caps:", strDesc);
			char	text[688];
			char	*p = text;
			for (DWORD n = 0; n < sizeof(D3DCAPS9); n++)
			{
				sprintf(p, "%02x", ((BYTE *)&pDevice->d3dCaps)[n]);
				p += 2;
				if ((n & 3) == 3)
					*p++ = ' ';
			}
			*p = 0;
			SETUP_HISTORY.AddMessage("%s", text);

            // Examine each format supported by the adapter to see if it will
            // work with this device and meets the needs of the application.
            BOOL  bFormatConfirmed[20];
            DWORD dwBehavior[20];
            D3DFORMAT fmtDepthStencil[20];
			DWORD dwFormatCaps[20];

            for( DWORD f=0; f<dwNumFormats; f++ )
            {
                bFormatConfirmed[f] = FALSE;
                fmtDepthStencil[f] = D3DFMT_UNKNOWN;

                // Skip formats that cannot be used as render targets on this device
				HRESULT hr;
                if( FAILED( hr = m_pD3D->CheckDeviceType( iAdapter, pDevice->DeviceType,
                                                     formats[f], formats[f], FALSE ) ) )
				{
					SETUP_HISTORY.AddMessage("CheckDeviceType failed for a: %d, t: %s, f: %s because %s", iAdapter,
											 DeviceTypeName(pDevice->DeviceType), FormatName(formats[f]), DXGetErrorString9(hr));
                    continue;
				}

                if( pDevice->DeviceType == D3DDEVTYPE_HAL )
                {
                    // This system has a HAL device
                    bHALExists = TRUE;

                    // HAL can run in a window for some mode
                    bHALIsWindowedCompatible = TRUE;

                    if( f == 0 )
                    {
                        // HAL can run in a window for the current desktop mode
                        bHALIsDesktopCompatible = TRUE;
                    }
                }

                // Confirm the device/format for HW vertex processing
                if( pDevice->d3dCaps.DevCaps&D3DDEVCAPS_HWTRANSFORMANDLIGHT )
                {
                    if( pDevice->d3dCaps.DevCaps&D3DDEVCAPS_PUREDEVICE )
                    {
                        dwBehavior[f] = D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                        D3DCREATE_PUREDEVICE;

                        if( SUCCEEDED( ConfirmDevice( &pDevice->d3dCaps, dwBehavior[f],
                                                      formats[f] ) ) )
						{
							SETUP_HISTORY.AddMessage("%s: HWTNL + PURE OK", FormatName(formats[f]));
                            bFormatConfirmed[f] = TRUE;
						}
                    }

                    if ( FALSE == bFormatConfirmed[f] )
                    {
                        dwBehavior[f] = D3DCREATE_HARDWARE_VERTEXPROCESSING;

                        if( SUCCEEDED( ConfirmDevice( &pDevice->d3dCaps, dwBehavior[f],
                                                      formats[f] ) ) )
						{
							SETUP_HISTORY.AddMessage("%s: HWTNL OK", FormatName(formats[f]));
                            bFormatConfirmed[f] = TRUE;
						}
                    }

                    if ( FALSE == bFormatConfirmed[f] )
                    {
                        dwBehavior[f] = D3DCREATE_MIXED_VERTEXPROCESSING;

                        if( SUCCEEDED( ConfirmDevice( &pDevice->d3dCaps, dwBehavior[f],
                                                      formats[f] ) ) )
						{
							SETUP_HISTORY.AddMessage("%s: MIXED TNL OK", FormatName(formats[f]));
                            bFormatConfirmed[f] = TRUE;
						}
                    }
                }

                // Confirm the device/format for SW vertex processing
                if( FALSE == bFormatConfirmed[f] )
                {
                    dwBehavior[f] = D3DCREATE_SOFTWARE_VERTEXPROCESSING;

                    if( SUCCEEDED( ConfirmDevice( &pDevice->d3dCaps, dwBehavior[f],
                                                  formats[f] ) ) )
					{
						SETUP_HISTORY.AddMessage("%s: SWVP OK", FormatName(formats[f]));
                        bFormatConfirmed[f] = TRUE;
					}
                }

                // Find a suitable depth/stencil buffer format for this device/format
                if( bFormatConfirmed[f] && m_bUseDepthBuffer )
                {
                    if( !FindDepthStencilFormat( iAdapter, pDevice->DeviceType,
                        formats[f], &fmtDepthStencil[f] ) )
                    {
						SETUP_HISTORY.AddMessage("But no depth/stencil available.");
                        bFormatConfirmed[f] = FALSE;
                    }
                }

				// The texture and multisample formats this format can use
				if( bFormatConfirmed[f] )
				{
					dwFormatCaps[f] = 0;

					if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, pDevice->DeviceType, formats[f], 0,
															  D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8 ) ) )
					{
						dwFormatCaps[f] = 4;
						SETUP_HISTORY.AddMessage("Can do texture A8R8G8B8");
					}
					if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, pDevice->DeviceType, formats[f], 0,
															  D3DRTYPE_TEXTURE, D3DFMT_DXT1 ) ) )
					{
						dwFormatCaps[f] |= 1;
						SETUP_HISTORY.AddMessage("Can do texture DXT1");
					}
					if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, pDevice->DeviceType, formats[f], 0,
															  D3DRTYPE_TEXTURE, D3DFMT_DXT2 ) ) )
					{
						dwFormatCaps[f] |= 2;
						SETUP_HISTORY.AddMessage("Can do texture DXT2");
					}
					if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, pDevice->DeviceType, formats[f], 0,
															  D3DRTYPE_TEXTURE, D3DFMT_D24X8 ) ) )
					{
						dwFormatCaps[f] |= 8;
						SETUP_HISTORY.AddMessage("Can do texture D24X8");
					}
					if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, pDevice->DeviceType, formats[f], 0,
															  D3DRTYPE_TEXTURE, D3DFMT_D16 ) ) )
					{
						dwFormatCaps[f] |= 0x10;
						SETUP_HISTORY.AddMessage("Can do texture D16");
					}

					DWORD	quality;
					if( SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, formats[f], FALSE,
																	   D3DMULTISAMPLE_2_SAMPLES, &quality ) ) && quality &&
						SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, fmtDepthStencil[f], FALSE,
																	   D3DMULTISAMPLE_2_SAMPLES, &quality ) ) && quality )
					{
						dwFormatCaps[f] |= 0x20;
						SETUP_HISTORY.AddMessage("Can do multisample 2");
					}
					if( SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, formats[f], FALSE,
																	   D3DMULTISAMPLE_3_SAMPLES, &quality ) ) && quality &&
						SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, fmtDepthStencil[f], FALSE,
																	   D3DMULTISAMPLE_3_SAMPLES, &quality ) ) && quality )
					{
						dwFormatCaps[f] |= 0x40;
						SETUP_HISTORY.AddMessage("Can do multisample 3");
					}
					if( SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, formats[f], FALSE,
																	   D3DMULTISAMPLE_4_SAMPLES, &quality ) ) && quality &&
						SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, fmtDepthStencil[f], FALSE,
																	   D3DMULTISAMPLE_4_SAMPLES, &quality ) ) && quality )
					{
						dwFormatCaps[f] |= 0x80;
						SETUP_HISTORY.AddMessage("Can do multisample 4");
					}
					if( SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, formats[f], FALSE,
																	   D3DMULTISAMPLE_6_SAMPLES, &quality ) ) && quality &&
						SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, fmtDepthStencil[f], FALSE,
																	   D3DMULTISAMPLE_6_SAMPLES, &quality ) ) && quality )
					{
						dwFormatCaps[f] |= 0x100;
						SETUP_HISTORY.AddMessage("Can do multisample 6");
					}
					if( SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, formats[f], FALSE,
																	   D3DMULTISAMPLE_8_SAMPLES, &quality ) ) && quality &&
						SUCCEEDED( m_pD3D->CheckDeviceMultiSampleType( iAdapter, pDevice->DeviceType, fmtDepthStencil[f], FALSE,
																	   D3DMULTISAMPLE_8_SAMPLES, &quality ) ) && quality )
					{
						dwFormatCaps[f] |= 0x200;
						SETUP_HISTORY.AddMessage("Can do multisample 8");
					}
				}
            }

            // Add all enumerated display modes with confirmed formats to the
            // device's list of valid modes
            DWORD m; for (m=0L; m<dwNumModes; m++ )
            {
                for( DWORD f=0; f<dwNumFormats; f++ )
                {
                    if( modes[m].Format == formats[f] )
                    {
                        if( bFormatConfirmed[f] == TRUE )
                        {
                            // Add this mode to the device's list of valid modes
                            pDevice->modes[pDevice->dwNumModes].Width      = modes[m].Width;
                            pDevice->modes[pDevice->dwNumModes].Height     = modes[m].Height;
                            pDevice->modes[pDevice->dwNumModes].Format     = modes[m].Format;
                            pDevice->modes[pDevice->dwNumModes].dwBehavior = dwBehavior[f];
                            pDevice->modes[pDevice->dwNumModes].mUnknown10 = dwFormatCaps[f];
                            pDevice->modes[pDevice->dwNumModes].DepthStencilFormat = fmtDepthStencil[f];
                            pDevice->dwNumModes++;

                            if( pDevice->DeviceType == D3DDEVTYPE_HAL )
                                bHALIsSampleCompatible = TRUE;
                        }
                    }
                }
            }

			// The mode the options saved
			if( gDisplayModeKey.mWidth )
			{
				CDisplayModeKey	key;
				for( m=0; m<pDevice->dwNumModes; m++ )
				{
					D3DModeInfo	*mode = &pDevice->modes[m];
					key = MakeDisplayModeKey(*mode);
					if( key == gDisplayModeKey )
						goto found;
				}
				m = 0;
found:
				pDevice->dwCurrentMode = m;
			}

            // Else the creation size (but prefer a 32-bit mode)
			else if( pDevice->dwCurrentMode == -1 || pDevice->dwCurrentMode >= pDevice->dwNumModes )
				pDevice->dwCurrentMode = FindMode(pDevice, m_dwCreationWidth, m_dwCreationHeight);

			pDevice->dwLowDetailMode = 0;
			pDevice->dwMediumDetailMode = FindMode(pDevice, 800, 600);
			pDevice->dwHighDetailMode = FindMode(pDevice, 1280, 1024);
			if( !pDevice->dwHighDetailMode )
				pDevice->dwHighDetailMode = FindMode(pDevice, 1024, 768);

            // Check if the device is compatible with the desktop display mode
            // (which was added initially as formats[0])
            if( bFormatConfirmed[0] )
            {
                pDevice->bCanDoWindowed = TRUE;
                pDevice->bWindowed      = TRUE;
            }

            // If valid modes were found, keep this device
            if( pDevice->dwNumModes > 0 )
			{
				SETUP_HISTORY.AddMessage("%s accepted.", strDesc);
                pAdapter->dwNumDevices++;
			}
        }

        // If valid devices were found, keep this adapter
        if( pAdapter->dwNumDevices > 0 )
		{
			SETUP_HISTORY.AddMessage("%s accepted.", pAdapter->d3dAdapterIdentifier.Description);
            m_dwNumAdapters++;
		}
		SETUP_HISTORY.AddMessage("");
    }

    // Return an error if no compatible devices were found
    if( 0L == m_dwNumAdapters )
        return D3DAPPERR_NOCOMPATIBLEDEVICES;

    // Pick a default device
    for( DWORD a=0; a<m_dwNumAdapters; a++ )
    {
        if( m_Adapters[a].dwNumDevices > 0 )
        {
			DWORD d = 0;
            m_Adapters[a].dwCurrentDevice = d;
            m_dwAdapter = a;
            m_bWindowed = ( m_Adapters[a].devices[d].bWindowed && CLIPARAMS.mForceWindowed ) ? TRUE : FALSE;

            // Display a warning message
            if( m_Adapters[a].devices[d].DeviceType == D3DDEVTYPE_REF )
            {
                if( !bHALExists )
                    DisplayErrorMsg( D3DAPPERR_NOHARDWAREDEVICE, MSGWARN_SWITCHEDTOREF );
                else if( !bHALIsSampleCompatible )
                    DisplayErrorMsg( D3DAPPERR_HALNOTCOMPATIBLE, MSGWARN_SWITCHEDTOREF );
                else if( !bHALIsWindowedCompatible )
                    DisplayErrorMsg( D3DAPPERR_NOWINDOWEDHAL, MSGWARN_SWITCHEDTOREF );
                else if( !bHALIsDesktopCompatible )
                    DisplayErrorMsg( D3DAPPERR_NODESKTOPHAL, MSGWARN_SWITCHEDTOREF );
                else // HAL is desktop compatible, but not sample compatible
                    DisplayErrorMsg( D3DAPPERR_NOHALTHISMODE, MSGWARN_SWITCHEDTOREF );
            }

			SETUP_HISTORY.AddMessage("Using %s on %s.", m_Adapters[a].devices[d].strDesc, m_Adapters[a].d3dAdapterIdentifier.Description);
            return S_OK;
        }
    }

    return D3DAPPERR_NOCOMPATIBLEDEVICES;
}




//-----------------------------------------------------------------------------
// Name: FindDepthStencilFormat()
// Desc: Finds a depth/stencil format for the given device that is compatible
//       with the render target format and meets the needs of the app.
//-----------------------------------------------------------------------------
BOOL CD3DApplication::FindDepthStencilFormat( UINT iAdapter, D3DDEVTYPE DeviceType,	// FUNCTION: 0x0052a830
    D3DFORMAT TargetFormat, D3DFORMAT* pDepthStencilFormat )
{
    if( m_dwMinDepthBits <= 24 && m_dwMinStencilBits == 0 )
    {
        if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, DeviceType,
            TargetFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24X8 ) ) )
        {
            if( SUCCEEDED( m_pD3D->CheckDepthStencilMatch( iAdapter, DeviceType,
                TargetFormat, TargetFormat, D3DFMT_D24X8 ) ) )
            {
                *pDepthStencilFormat = D3DFMT_D24X8;
                return TRUE;
            }
        }

        if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, DeviceType,
            TargetFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24S8 ) ) )
        {
            if( SUCCEEDED( m_pD3D->CheckDepthStencilMatch( iAdapter, DeviceType,
                TargetFormat, TargetFormat, D3DFMT_D24S8 ) ) )
            {
                *pDepthStencilFormat = D3DFMT_D24S8;
                return TRUE;
            }
        }

        if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, DeviceType,
            TargetFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D32 ) ) )
        {
            if( SUCCEEDED( m_pD3D->CheckDepthStencilMatch( iAdapter, DeviceType,
                TargetFormat, TargetFormat, D3DFMT_D32 ) ) )
            {
                *pDepthStencilFormat = D3DFMT_D32;
                return TRUE;
            }
        }
    }

    if( m_dwMinDepthBits <= 16 && m_dwMinStencilBits == 0 )
    {
        if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, DeviceType,
            TargetFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D16 ) ) )
        {
            if( SUCCEEDED( m_pD3D->CheckDepthStencilMatch( iAdapter, DeviceType,
                TargetFormat, TargetFormat, D3DFMT_D16 ) ) )
            {
                *pDepthStencilFormat = D3DFMT_D16;
                return TRUE;
            }
        }
    }

    if( m_dwMinDepthBits <= 15 && m_dwMinStencilBits <= 1 )
    {
        if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, DeviceType,
            TargetFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D15S1 ) ) )
        {
            if( SUCCEEDED( m_pD3D->CheckDepthStencilMatch( iAdapter, DeviceType,
                TargetFormat, TargetFormat, D3DFMT_D15S1 ) ) )
            {
                *pDepthStencilFormat = D3DFMT_D15S1;
                return TRUE;
            }
        }
    }

    if( m_dwMinDepthBits <= 24 && m_dwMinStencilBits <= 8 )
    {
        if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, DeviceType,
            TargetFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24S8 ) ) )
        {
            if( SUCCEEDED( m_pD3D->CheckDepthStencilMatch( iAdapter, DeviceType,
                TargetFormat, TargetFormat, D3DFMT_D24S8 ) ) )
            {
                *pDepthStencilFormat = D3DFMT_D24S8;
                return TRUE;
            }
        }
    }

    if( m_dwMinDepthBits <= 24 && m_dwMinStencilBits <= 4 )
    {
        if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, DeviceType,
            TargetFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24X4S4 ) ) )
        {
            if( SUCCEEDED( m_pD3D->CheckDepthStencilMatch( iAdapter, DeviceType,
                TargetFormat, TargetFormat, D3DFMT_D24X4S4 ) ) )
            {
                *pDepthStencilFormat = D3DFMT_D24X4S4;
                return TRUE;
            }
        }
    }

    if( m_dwMinDepthBits <= 32 && m_dwMinStencilBits == 0 )
    {
        if( SUCCEEDED( m_pD3D->CheckDeviceFormat( iAdapter, DeviceType,
            TargetFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D32 ) ) )
        {
            if( SUCCEEDED( m_pD3D->CheckDepthStencilMatch( iAdapter, DeviceType,
                TargetFormat, TargetFormat, D3DFMT_D32 ) ) )
            {
                *pDepthStencilFormat = D3DFMT_D32;
                return TRUE;
            }
        }
    }

    return FALSE;
}




//-----------------------------------------------------------------------------
// Name: MsgProc()
// Desc: Message handling function.
//-----------------------------------------------------------------------------
LRESULT CD3DApplication::MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam,	// FUNCTION: 0x0052aaf0
                                  LPARAM lParam )
{
    HRESULT hr;

    switch( uMsg )
    {
        case WM_PAINT:
            // Handle paint messages when the app is not ready
            break;

        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 100;
            ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 100;
            break;

        case WM_ENTERSIZEMOVE:
            // Halt frame movement while the app is sizing or moving
            if( m_bFrameMoving )
                DXUtil_Timer( TIMER_STOP );
            break;

        case WM_SIZE:
            // Check to see if we are losing our window...
            if( SIZE_MAXHIDE==wParam || SIZE_MINIMIZED==wParam )
                m_bActive = FALSE;
            else
                m_bActive = TRUE;
            break;

        case WM_EXITSIZEMOVE:
            if( m_bFrameMoving )
                DXUtil_Timer( TIMER_START );

            if( m_bActive && m_bWindowed )
            {
                RECT rcClientOld;
                rcClientOld = m_rcWindowClient;

                // Update window properties
                GetWindowRect( m_hWnd, &m_rcWindowBounds );
                GetClientRect( m_hWnd, &m_rcWindowClient );

                if( rcClientOld.right - rcClientOld.left !=
                    m_rcWindowClient.right - m_rcWindowClient.left ||
                    rcClientOld.bottom - rcClientOld.top !=
                    m_rcWindowClient.bottom - m_rcWindowClient.top)
                {
                    // A new window size will require a new backbuffer
                    // size, so the 3D structures must be changed accordingly.
                    m_bReady = FALSE;

                    m_d3dpp.BackBufferWidth  = m_rcWindowClient.right - m_rcWindowClient.left;
                    m_d3dpp.BackBufferHeight = m_rcWindowClient.bottom - m_rcWindowClient.top;

                    // Resize the 3D environment
                    if( FAILED( hr = Resize3DEnvironment() ) )
                    {
                        DisplayErrorMsg( D3DAPPERR_RESIZEFAILED, MSGERR_APPMUSTEXIT );
                        return 0;
                    }

                    m_bReady = TRUE;
                }
            }

            break;

        case WM_SETCURSOR:
            // Turn off Windows cursor in fullscreen mode
            if( m_bActive && m_bReady && !m_bWindowed )
            {
                SetCursor( NULL );
                if( m_bShowCursorWhenFullscreen )
                    m_pd3dDevice->ShowCursor( TRUE );
                return TRUE; // prevent Windows from setting cursor to window class cursor
            }
            break;

         case WM_MOUSEMOVE:
            if( m_bActive && m_bReady && m_pd3dDevice != NULL )
            {
                POINT ptCursor;
                GetCursorPos( &ptCursor );
                ScreenToClient( m_hWnd, &ptCursor );
                m_pd3dDevice->SetCursorPosition( ptCursor.x, ptCursor.y, 0L );
            }
            break;

       case WM_ENTERMENULOOP:
            // Pause the app when menus are displayed
            Pause(TRUE);
            break;

        case WM_EXITMENULOOP:
            Pause(FALSE);
            break;

        case WM_CONTEXTMENU:
			break;

        case WM_NCHITTEST:
            // Prevent the user from selecting the menu in fullscreen mode
            if( !m_bWindowed )
                return HTCLIENT;

            break;

        case WM_POWERBROADCAST:
            switch( wParam )
            {
                #ifndef PBT_APMQUERYSUSPEND
                    #define PBT_APMQUERYSUSPEND 0x0000
                #endif
                case PBT_APMQUERYSUSPEND:
                    // At this point, the app should save any data for open
                    // network connections, files, etc., and prepare to go into
                    // a suspended mode.
                    return TRUE;

                #ifndef PBT_APMRESUMESUSPEND
                    #define PBT_APMRESUMESUSPEND 0x0007
                #endif
                case PBT_APMRESUMESUSPEND:
                    // At this point, the app should recover any data, network
                    // connections, files, etc., and resume running from when
                    // the app was suspended.
                    return TRUE;
            }
            break;

        case WM_SYSCOMMAND:
            // Prevent moving/sizing and power loss in fullscreen mode
            switch( wParam )
            {
                case SC_MOVE:
                case SC_SIZE:
                case SC_MAXIMIZE:
                case SC_KEYMENU:
                case SC_MONITORPOWER:
                    if( FALSE == m_bWindowed )
                        return 1;
                    break;
            }
            break;

        case WM_COMMAND:
            switch( LOWORD(wParam) )
            {
                case IDM_CHANGEDEVICE:
                    return 0;

                case IDM_EXIT:
                    // Recieved key/menu command to exit app
                    SendMessage( hWnd, WM_CLOSE, 0, 0 );
                    return 0;
            }
            break;

        case WM_CLOSE:
			// Quit
			LT.TriggerQuit();
            return 0;
    }

    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

CTweakSLONG	ZBUFFER_AS_TEXTURE("ZBUFFER_AS_TEXTURE", 0);			// 0089c060
// FUNCTION: 0x0052aea0 init:ZBUFFER_AS_TEXTURE
// FUNCTION: 0x0052aec0 exit:ZBUFFER_AS_TEXTURE
CTweakSLONG	TRY_LOCKABLE_BB("TRY_LOCKABLE_BB", 0);					// 0089c050
// FUNCTION: 0x0052aed0 init:TRY_LOCKABLE_BB
// FUNCTION: 0x0052aef0 exit:TRY_LOCKABLE_BB

static IDirect3DQuery9	*gResourceQuery;						// 0089c04c: the resource manager's query (Initialize3DEnvironment 0052b416); uninitialized and defined here: retail's .bss order (placement evidence)


//-----------------------------------------------------------------------------
// Name: Initialize3DEnvironment()
// Desc:
//-----------------------------------------------------------------------------
// The multisample type is stored in each arm of an if/else, not through a ?:. The ?: result is a web of its own that
// the allocator colours eax before pModeInfo's piece, which was left only edi, and the constant 0 then took eax. With
// the stores in the arms pModeInfo takes eax and the 0 edi, as in retail, and the rest of the function follows.
HRESULT CD3DApplication::Initialize3DEnvironment(bool reset)	// FUNCTION: 0x0052af00
{
    HRESULT hr;

	TEXTURE_RES_LOSS_SHIFT.Set(-1.f);
	TEXTURE_ALLOW_32_BIT.Set(-1.f);
	if (CLIPARAMS.mCardID)
		LoadCardId("c:\\cardid.txt");
	else
		LoadCardId("cardid.txt");

	bool	lockable = (TRY_LOCKABLE_BB.mValue != 0);
	int		attempts = 0;

retry:
    D3DAdapterInfo* pAdapterInfo = &m_Adapters[m_dwAdapter];
    D3DDeviceInfo*  pDeviceInfo  = &pAdapterInfo->devices[pAdapterInfo->dwCurrentDevice];
    D3DModeInfo*    pModeInfo    = &pDeviceInfo->modes[pDeviceInfo->dwCurrentMode];

	if (attempts == 6)
	{
		SETUP_HISTORY.AddMessage("Going to safe mode");
		pDeviceInfo->MultiSampleType = D3DMULTISAMPLE_NONE;
		lockable = false;
		pDeviceInfo->dwCurrentMode = FindMode(pDeviceInfo, m_dwCreationWidth, m_dwCreationHeight);
	}
	else if (attempts == 7)
		CONSOLE.FatalError(FALSE, 0xd2, 10);

    // Prepare window for possible windowed/fullscreen change
    AdjustWindowForChange();

    // Set up the presentation parameters
    ZeroMemory( &m_d3dpp, sizeof(m_d3dpp) );
    m_d3dpp.Windowed               = m_bWindowed;
    m_d3dpp.BackBufferCount        = CLIPARAMS.mBackBuffers;
    if (pDeviceInfo->MultiSampleType == -1)
        m_d3dpp.MultiSampleType    = (D3DMULTISAMPLE_TYPE)SUGGEST_MULTISAMPLE_TYPE.mValue;
    else
        m_d3dpp.MultiSampleType    = pDeviceInfo->MultiSampleType;
    m_d3dpp.EnableAutoDepthStencil = m_bUseDepthBuffer;
    m_d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    m_d3dpp.AutoDepthStencilFormat = pModeInfo->DepthStencilFormat;
    m_d3dpp.hDeviceWindow          = m_hWnd;
	if( COPY_NOT_FLIP.mValue )
	{
		m_d3dpp.SwapEffect         = D3DSWAPEFFECT_COPY;
		m_d3dpp.MultiSampleType    = D3DMULTISAMPLE_NONE;
	}
    if( m_bWindowed )
    {
		m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
        m_d3dpp.BackBufferWidth  = m_rcWindowClient.right - m_rcWindowClient.left;
        m_d3dpp.BackBufferHeight = m_rcWindowClient.bottom - m_rcWindowClient.top;
        m_d3dpp.BackBufferFormat = pAdapterInfo->d3ddmDesktop.Format;

		// the depth format of a mode in the desktop's format, its size if there is one
		for (DWORD m = 0; m < pDeviceInfo->dwNumModes; m++)
		{
			if (pDeviceInfo->modes[m].Format == m_d3dpp.BackBufferFormat)
			{
				m_d3dpp.AutoDepthStencilFormat = pDeviceInfo->modes[m].DepthStencilFormat;
				if (pDeviceInfo->modes[m].Width == pAdapterInfo->d3ddmDesktop.Width &&
					pDeviceInfo->modes[m].Height == pAdapterInfo->d3ddmDesktop.Height)
					break;
			}
		}
    }
    else
    {
		m_d3dpp.PresentationInterval = (CLIPARAMS.mVSync || FORCE_VSYNC.mValue) ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
        m_d3dpp.BackBufferWidth  = pModeInfo->Width;
        m_d3dpp.BackBufferHeight = pModeInfo->Height;
        m_d3dpp.BackBufferFormat = pModeInfo->Format;
    }
	UpdateScreenShape();
	if (lockable && m_d3dpp.MultiSampleType == D3DMULTISAMPLE_NONE)
		m_d3dpp.Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    // Create the device
	SETUP_HISTORY.AddMessage("Creating device...");
	char	text[132];
	char	*p = text;
	for (DWORD n = 0; n < sizeof(m_d3dpp); n++)
	{
		sprintf(p, "%02x", ((BYTE *)&m_d3dpp)[n]);
		p += 2;
		if ((n & 3) == 3)
			*p++ = ' ';
	}
	*p = 0;
	SETUP_HISTORY.AddMessage("%s", text);

	SAFE_RELEASE(gResourceQuery);

	if (reset && m_pd3dDevice)
		hr = m_pd3dDevice->Reset( &m_d3dpp );
	else
		hr = m_pD3D->CreateDevice( m_dwAdapter, pDeviceInfo->DeviceType,
								   m_hWndFocus, pModeInfo->dwBehavior | (CLIPARAMS.mUnknown185 ? D3DCREATE_DISABLE_DRIVER_MANAGEMENT : 0),
								   &m_d3dpp, &m_pd3dDevice );
	attempts++;

	if( FAILED(hr) )
	{
		if (hr == D3DERR_DEVICELOST)
		{
			SETUP_HISTORY.AddMessage("Failed for device lost.");
			Sleep(100);
			goto retry;
		}

		SETUP_HISTORY.AddMessage("Failed for %s.", DXGetErrorString9(hr));
		if (lockable)
		{
			SETUP_HISTORY.AddMessage("Falling back to no lockable backbuffer");
			lockable = false;
			goto retry;
		}
		if (m_d3dpp.MultiSampleType != D3DMULTISAMPLE_NONE)
		{
			SETUP_HISTORY.AddMessage("Falling back to no multisampling");
			SetSetupMessage(0xe5);
			pDeviceInfo->MultiSampleType = D3DMULTISAMPLE_NONE;
			goto retry;
		}
		DWORD	oldmode = pDeviceInfo->dwCurrentMode;
		pDeviceInfo->dwCurrentMode = FindMode(pDeviceInfo, m_dwCreationWidth, m_dwCreationHeight);
		if (pDeviceInfo->dwCurrentMode != oldmode)
		{
			SETUP_HISTORY.AddMessage("Falling back to a friendly mode");
			SetSetupMessage(0xe6);
			lockable = true;
			goto retry;
		}
	}
	else
    {
		if (CLIPARAMS.mUnknown185)
			m_pd3dDevice->CreateQuery(D3DQUERYTYPE_RESOURCEMANAGER, &gResourceQuery);

		SETUP_HISTORY.AddMessage("Succeeded.");
		SETUP_HISTORY.AddMessage("Available texture memory: %d Mb", m_pd3dDevice->GetAvailableTextureMem() >> 20);

        // When moving from fullscreen to windowed mode, it is important to
        // adjust the window size after recreating the device rather than
        // beforehand to ensure that you get the window size you want.  For
        // example, when switching from 640x480 fullscreen to windowed with
        // a 1000x600 window on a 1024x768 desktop, it is impossible to set
        // the window size to 1000x600 until after the display mode has
        // changed to 1024x768, because windows cannot be larger than the
        // desktop.
        if( m_bWindowed )
        {
            SetWindowPos( m_hWnd, HWND_NOTOPMOST,
                          m_rcWindowBounds.left, m_rcWindowBounds.top,
                          ( m_rcWindowBounds.right - m_rcWindowBounds.left ),
                          ( m_rcWindowBounds.bottom - m_rcWindowBounds.top ),
                          SWP_SHOWWINDOW );
        }

        // Store device Caps
        m_pd3dDevice->GetDeviceCaps( &m_d3dCaps );
        m_dwCreateFlags = pModeInfo->dwBehavior;

        // Store device description
        if( pDeviceInfo->DeviceType == D3DDEVTYPE_REF )
            lstrcpy( m_strDeviceStats, TEXT("REF") );
        else if( pDeviceInfo->DeviceType == D3DDEVTYPE_HAL )
            lstrcpy( m_strDeviceStats, TEXT("HAL") );
        else if( pDeviceInfo->DeviceType == D3DDEVTYPE_SW )
            lstrcpy( m_strDeviceStats, TEXT("SW") );

        if( pModeInfo->dwBehavior & D3DCREATE_HARDWARE_VERTEXPROCESSING &&
            pModeInfo->dwBehavior & D3DCREATE_PUREDEVICE )
        {
            if( pDeviceInfo->DeviceType == D3DDEVTYPE_HAL )
                lstrcat( m_strDeviceStats, TEXT(" (pure hw vp)") );
            else
                lstrcat( m_strDeviceStats, TEXT(" (simulated pure hw vp)") );
        }
        else if( pModeInfo->dwBehavior & D3DCREATE_HARDWARE_VERTEXPROCESSING )
        {
            if( pDeviceInfo->DeviceType == D3DDEVTYPE_HAL )
                lstrcat( m_strDeviceStats, TEXT(" (hw vp)") );
            else
                lstrcat( m_strDeviceStats, TEXT(" (simulated hw vp)") );
        }
        else if( pModeInfo->dwBehavior & D3DCREATE_MIXED_VERTEXPROCESSING )
        {
            if( pDeviceInfo->DeviceType == D3DDEVTYPE_HAL )
                lstrcat( m_strDeviceStats, TEXT(" (mixed vp)") );
            else
                lstrcat( m_strDeviceStats, TEXT(" (simulated mixed vp)") );
        }
        else if( pModeInfo->dwBehavior & D3DCREATE_SOFTWARE_VERTEXPROCESSING )
        {
            lstrcat( m_strDeviceStats, TEXT(" (sw vp)") );
        }

        if( pDeviceInfo->DeviceType == D3DDEVTYPE_HAL )
        {
            lstrcat( m_strDeviceStats, TEXT(": ") );
            lstrcat( m_strDeviceStats, ToTCHAR(pAdapterInfo->d3dAdapterIdentifier.Description));
        }

		_tcscat(m_strDeviceStats,ToTCHAR("\n"));
		OutputDebugString(m_strDeviceStats);

        // Store render target surface desc
        LPDIRECT3DSURFACE9 pBackBuffer;
        m_pd3dDevice->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer );
        pBackBuffer->GetDesc( &m_d3dsdBackBuffer );
        pBackBuffer->Release();

        // Set up the fullscreen cursor
        if( m_bShowCursorWhenFullscreen && !m_bWindowed )
        {
            HCURSOR hCursor;
#ifdef _WIN64
            hCursor = (HCURSOR)GetClassLongPtr( m_hWnd, GCLP_HCURSOR );
#else
            hCursor = (HCURSOR)GetClassLong( m_hWnd, GCL_HCURSOR );
#endif
            D3DUtil_SetDeviceCursor( m_pd3dDevice, hCursor );
            m_pd3dDevice->ShowCursor( TRUE );
        }

        // Initialize the app's device-dependent objects
        hr = InitDeviceObjects();
        if(SUCCEEDED(hr))
        {
            m_bActive = TRUE;
			SETUP_HISTORY.AddMessage("D3DA::I3DE OK");
            return S_OK;
        }

        // Cleanup before we try again
        InvalidateDeviceObjects();
        DeleteDeviceObjects();
        SAFE_RELEASE( m_pd3dDevice );
    }

    // If that failed, fall back to the reference rasterizer
    if( pDeviceInfo->DeviceType == D3DDEVTYPE_HAL )
    {
        // Let the user know we are switching from HAL to the reference rasterizer
        DisplayErrorMsg( hr, MSGWARN_SWITCHEDTOREF );

        // Select the default adapter
        m_dwAdapter = 0L;
        pAdapterInfo = &m_Adapters[m_dwAdapter];

        // Look for a software device
        for( UINT i=0L; i<pAdapterInfo->dwNumDevices; i++ )
        {
            if( pAdapterInfo->devices[i].DeviceType == D3DDEVTYPE_REF )
            {
                pAdapterInfo->dwCurrentDevice = i;
                pDeviceInfo = &pAdapterInfo->devices[i];
                m_bWindowed = pDeviceInfo->bWindowed;
                break;
            }
        }

        // Try again, this time with the reference rasterizer
        if( pAdapterInfo->devices[pAdapterInfo->dwCurrentDevice].DeviceType ==
            D3DDEVTYPE_REF )
        {
            hr = Initialize3DEnvironment(false);
        }
    }
    return hr;
}




//-----------------------------------------------------------------------------
// Name:
// Desc:
//-----------------------------------------------------------------------------
HRESULT CD3DApplication::Resize3DEnvironment()	// FUNCTION: 0x0052b760
{
    HRESULT hr;

    // Release all vidmem objects
    if( FAILED( hr = InvalidateDeviceObjects() ) )
        return hr;

    // Reset the device
    if( FAILED( hr = m_pd3dDevice->Reset( &m_d3dpp ) ) )
        return hr;

    // Store render target surface desc
    LPDIRECT3DSURFACE9 pBackBuffer;
    m_pd3dDevice->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer );
    pBackBuffer->GetDesc( &m_d3dsdBackBuffer );
    pBackBuffer->Release();

    // Set up the fullscreen cursor
    if( m_bShowCursorWhenFullscreen && !m_bWindowed )
    {
        HCURSOR hCursor;
#ifdef _WIN64
        hCursor = (HCURSOR)GetClassLongPtr( m_hWnd, GCLP_HCURSOR );
#else
        hCursor = (HCURSOR)GetClassLong( m_hWnd, GCL_HCURSOR );
#endif
        D3DUtil_SetDeviceCursor( m_pd3dDevice, hCursor );
        m_pd3dDevice->ShowCursor( TRUE );
    }

    // Initialize the app's device-dependent objects
    hr = RestoreDeviceObjects();
    if( FAILED(hr) )
        return hr;

    // If the app is paused, trigger the rendering of the current frame
    if( FALSE == m_bFrameMoving )
    {
        m_bSingleStep = TRUE;
        DXUtil_Timer( TIMER_START );
        DXUtil_Timer( TIMER_STOP );
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: ToggleFullScreen()
// Desc: Called when user toggles between fullscreen mode and windowed mode
//-----------------------------------------------------------------------------
HRESULT CD3DApplication::ToggleFullscreen()	// FUNCTION: 0x0052b840
{
    // Get access to current adapter, device, and mode
    D3DAdapterInfo* pAdapterInfo = &m_Adapters[m_dwAdapter];
    D3DDeviceInfo*  pDeviceInfo  = &pAdapterInfo->devices[pAdapterInfo->dwCurrentDevice];
    D3DModeInfo*    pModeInfo    = &pDeviceInfo->modes[pDeviceInfo->dwCurrentMode];

    // Need device change if going windowed and the current device
    // can only be fullscreen
    if( !m_bWindowed && !pDeviceInfo->bCanDoWindowed )
        return ForceWindowed(TRUE);

    m_bReady = FALSE;

    // Toggle the windowed state
    m_bWindowed = !m_bWindowed;
    pDeviceInfo->bWindowed = m_bWindowed;

    // Prepare window for windowed/fullscreen change
    AdjustWindowForChange();

    // Set up the presentation parameters
    m_d3dpp.Windowed               = pDeviceInfo->bWindowed;
    m_d3dpp.MultiSampleType        = pDeviceInfo->MultiSampleType;
    m_d3dpp.AutoDepthStencilFormat = pModeInfo->DepthStencilFormat;
    m_d3dpp.hDeviceWindow          = m_hWnd;
    if( m_bWindowed )
    {
        m_d3dpp.BackBufferWidth  = m_rcWindowClient.right - m_rcWindowClient.left;
        m_d3dpp.BackBufferHeight = m_rcWindowClient.bottom - m_rcWindowClient.top;
        m_d3dpp.BackBufferFormat = pAdapterInfo->d3ddmDesktop.Format;
		m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    }
    else
    {
        m_d3dpp.BackBufferWidth  = pModeInfo->Width;
        m_d3dpp.BackBufferHeight = pModeInfo->Height;
        m_d3dpp.BackBufferFormat = pModeInfo->Format;
		m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    }
	UpdateScreenShape();
	m_d3dpp.Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    // Resize the 3D device
    if( FAILED( Resize3DEnvironment() ) )
    {
        if( m_bWindowed )
            return ForceWindowed(TRUE);
        else
            return E_FAIL;
    }

    // When moving from fullscreen to windowed mode, it is important to
    // adjust the window size after resetting the device rather than
    // beforehand to ensure that you get the window size you want.  For
    // example, when switching from 640x480 fullscreen to windowed with
    // a 1000x600 window on a 1024x768 desktop, it is impossible to set
    // the window size to 1000x600 until after the display mode has
    // changed to 1024x768, because windows cannot be larger than the
    // desktop.
    if( m_bWindowed )
    {
        SetWindowPos( m_hWnd, HWND_NOTOPMOST,
                      m_rcWindowBounds.left, m_rcWindowBounds.top,
                      ( m_rcWindowBounds.right - m_rcWindowBounds.left ),
                      ( m_rcWindowBounds.bottom - m_rcWindowBounds.top ),
                      SWP_SHOWWINDOW );
    }

    m_bReady = TRUE;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: ForceWindowed()
// Desc: Switch to a windowed mode, even if that means picking a new device
//       and/or adapter
//-----------------------------------------------------------------------------
HRESULT CD3DApplication::ForceWindowed(BOOL windowed)	// FUNCTION: 0x0052ba50
{
    HRESULT hr;
    D3DAdapterInfo* pAdapterInfoCur = &m_Adapters[m_dwAdapter];
    D3DDeviceInfo*  pDeviceInfoCur  = &pAdapterInfoCur->devices[pAdapterInfoCur->dwCurrentDevice];
    BOOL bFoundDevice = FALSE;

    if( pDeviceInfoCur->bCanDoWindowed == windowed )
    {
        bFoundDevice = TRUE;
    }
    else
    {
        // Look for a windowable device on any adapter
        D3DAdapterInfo* pAdapterInfo;
        DWORD dwAdapter;
        D3DDeviceInfo* pDeviceInfo;
        DWORD dwDevice;
        for( dwAdapter = 0; dwAdapter < m_dwNumAdapters; dwAdapter++ )
        {
            pAdapterInfo = &m_Adapters[dwAdapter];
            for( dwDevice = 0; dwDevice < pAdapterInfo->dwNumDevices; dwDevice++ )
            {
                pDeviceInfo = &pAdapterInfo->devices[dwDevice];
                if( pDeviceInfo->bCanDoWindowed == windowed )
                {
                    m_dwAdapter = dwAdapter;
                    pDeviceInfoCur = pDeviceInfo;
                    pAdapterInfo->dwCurrentDevice = dwDevice;
                    bFoundDevice = TRUE;
                    break;
                }
            }
            if( bFoundDevice )
                break;
        }
    }

    if( !bFoundDevice )
        return E_FAIL;

    pDeviceInfoCur->bWindowed = windowed;
    m_bWindowed = windowed;

    // Now destroy the current 3D device objects, then reinitialize

    m_bReady = FALSE;

    // Release all scene objects that will be re-created for the new device
    ReleaseDeviceObjects(false);

    // Create the new device
    if( FAILED( hr = Initialize3DEnvironment(true) ) )
        return DisplayErrorMsg( hr, MSGERR_APPMUSTEXIT );
    m_bReady = TRUE;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: AdjustWindowForChange()
// Desc: Prepare the window for a possible change between windowed mode and
//       fullscreen mode.  This function is virtual and thus can be overridden
//       to provide different behavior, such as switching to an entirely
//       different window for fullscreen mode (as in the MFC sample apps).
//-----------------------------------------------------------------------------
HRESULT CD3DApplication::AdjustWindowForChange()	// FUNCTION: 0x0052bb40
{
    if( m_bWindowed )
    {
        // Set windowed-mode style
        SetWindowLong( m_hWnd, GWL_STYLE, m_dwWindowStyle );
    }
    else
    {
        // Set fullscreen-mode style
        SetWindowLong( m_hWnd, GWL_STYLE, WS_POPUP|WS_SYSMENU|WS_VISIBLE );
    }
    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: UserSelectNewDevice()
// Desc: Displays a dialog so the user can select a new adapter, device, or
//       display mode, and then recreates the 3D environment if needed
//-----------------------------------------------------------------------------
HRESULT CD3DApplication::Reset3DEnvironment(BOOL a, bool b)	// FUNCTION: 0x0052bb80
{
    HRESULT hr;

	if (!a)
	{
		// Can't display dialogs in fullscreen mode
		if( m_bWindowed == FALSE )
		{
			if( FAILED( ToggleFullscreen() ) )
			{
				DisplayErrorMsg( D3DAPPERR_RESIZEFAILED, MSGERR_APPMUSTEXIT );
				return E_FAIL;
			}
		}

		// Prompt the user to change the mode
		if( IDOK != DialogBoxParam( (HINSTANCE)GetModuleHandle(NULL),
									MAKEINTRESOURCE(IDD_SELECTDEVICE), m_hWnd,
									SelectDeviceProc, (LPARAM)this ) )
			return S_OK;

		// Get access to the newly selected adapter, device, and mode
		DWORD dwDevice;
		dwDevice  = m_Adapters[m_dwAdapter].dwCurrentDevice;
		m_bWindowed = m_Adapters[m_dwAdapter].devices[dwDevice].bWindowed;
	}

    // Release all scene objects that will be re-created for the new device
    ReleaseDeviceObjects(b);

    // Inform the display class of the change. It will internally
    // re-create valid surfaces, a d3ddevice, etc.
    if( FAILED( hr = Initialize3DEnvironment(true) ) )
        return DisplayErrorMsg( hr, MSGERR_APPMUSTEXIT );

    // If the app is paused, trigger the rendering of the current frame
    if( FALSE == m_bFrameMoving )
    {
        m_bSingleStep = TRUE;
        DXUtil_Timer( TIMER_START );
        DXUtil_Timer( TIMER_STOP );
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: SelectDeviceProc()
// Desc: Windows message handling function for the device select dialog
//-----------------------------------------------------------------------------
INT_PTR CALLBACK CD3DApplication::SelectDeviceProc( HWND hDlg, UINT msg,	// FUNCTION: 0x0052bc80
                                                    WPARAM wParam, LPARAM lParam )
{
    // Get access to the UI controls
    HWND hwndAdapterList        = GetDlgItem( hDlg, IDC_ADAPTER_COMBO );
    HWND hwndDeviceList         = GetDlgItem( hDlg, IDC_DEVICE_COMBO );
    HWND hwndFullscreenModeList = GetDlgItem( hDlg, IDC_FULLSCREENMODES_COMBO );
    HWND hwndWindowedRadio      = GetDlgItem( hDlg, IDC_WINDOW );
    HWND hwndFullscreenRadio    = GetDlgItem( hDlg, IDC_FULLSCREEN );
    HWND hwndMultiSampleList    = GetDlgItem( hDlg, IDC_MULTISAMPLE_COMBO );
    BOOL bUpdateDlgControls     = FALSE;

    // Static state for adapter/device/mode selection
    static CD3DApplication* pd3dApp;
    static DWORD  dwOldAdapter, dwNewAdapter;
    static DWORD  dwOldDevice,  dwNewDevice;
    static DWORD  dwOldMode,    dwNewMode;
    static BOOL   bOldWindowed, bNewWindowed;
    static D3DMULTISAMPLE_TYPE OldMultiSampleType, NewMultiSampleType;

    // Working variables
    D3DAdapterInfo* pAdapter;
    D3DDeviceInfo*  pDevice;

    // Handle the initialization message
    if( WM_INITDIALOG == msg )
    {
        // Old state
        pd3dApp      = (CD3DApplication*)lParam;
        dwOldAdapter = pd3dApp->m_dwAdapter;
        pAdapter     = &pd3dApp->m_Adapters[dwOldAdapter];

        dwOldDevice  = pAdapter->dwCurrentDevice;
        pDevice      = &pAdapter->devices[dwOldDevice];

        dwOldMode    = pDevice->dwCurrentMode;
        bOldWindowed = pDevice->bWindowed;
        OldMultiSampleType = pDevice->MultiSampleType;

        // New state is initially the same as the old state
        dwNewAdapter = dwOldAdapter;
        dwNewDevice  = dwOldDevice;
        dwNewMode    = dwOldMode;
        bNewWindowed = bOldWindowed;
        NewMultiSampleType = OldMultiSampleType;

        // Set flag to update dialog controls below
        bUpdateDlgControls = TRUE;
    }

    if( WM_COMMAND == msg )
    {
        // Get current UI state
        bNewWindowed  = Button_GetCheck( hwndWindowedRadio );

        if( IDOK == LOWORD(wParam) )
        {
            // Handle the case when the user hits the OK button. Check if any
            // of the options were changed
            if( dwNewAdapter != dwOldAdapter || dwNewDevice  != dwOldDevice  ||
                dwNewMode    != dwOldMode    || bNewWindowed != bOldWindowed ||
                NewMultiSampleType != OldMultiSampleType )
            {
                pd3dApp->m_dwAdapter = dwNewAdapter;

                pAdapter = &pd3dApp->m_Adapters[dwNewAdapter];
                pAdapter->dwCurrentDevice = dwNewDevice;

                pAdapter->devices[dwNewDevice].dwCurrentMode = dwNewMode;
                pAdapter->devices[dwNewDevice].bWindowed     = bNewWindowed;
                pAdapter->devices[dwNewDevice].MultiSampleType = NewMultiSampleType;

                EndDialog( hDlg, IDOK );
            }
            else
                EndDialog( hDlg, IDCANCEL );

            return TRUE;
        }
        else if( IDCANCEL == LOWORD(wParam) )
        {
            // Handle the case when the user hits the Cancel button
            EndDialog( hDlg, IDCANCEL );
            return TRUE;
        }
        else if( CBN_SELENDOK == HIWORD(wParam) )
        {
            if( LOWORD(wParam) == IDC_ADAPTER_COMBO )
            {
                dwNewAdapter = ComboBox_GetCurSel( hwndAdapterList );
                pAdapter     = &pd3dApp->m_Adapters[dwNewAdapter];

                dwNewDevice  = pAdapter->dwCurrentDevice;
                dwNewMode    = pAdapter->devices[dwNewDevice].dwCurrentMode;
                bNewWindowed = pAdapter->devices[dwNewDevice].bWindowed;
            }
            else if( LOWORD(wParam) == IDC_DEVICE_COMBO )
            {
                pAdapter     = &pd3dApp->m_Adapters[dwNewAdapter];

                dwNewDevice  = ComboBox_GetCurSel( hwndDeviceList );
                dwNewMode    = pAdapter->devices[dwNewDevice].dwCurrentMode;
                bNewWindowed = pAdapter->devices[dwNewDevice].bWindowed;
            }
            else if( LOWORD(wParam) == IDC_FULLSCREENMODES_COMBO )
            {
                dwNewMode = ComboBox_GetCurSel( hwndFullscreenModeList );
            }
            else if( LOWORD(wParam) == IDC_MULTISAMPLE_COMBO )
            {
                DWORD dwItem = ComboBox_GetCurSel( hwndMultiSampleList );
                NewMultiSampleType = (D3DMULTISAMPLE_TYPE)ComboBox_GetItemData( hwndMultiSampleList, dwItem );
            }
        }
        // Keep the UI current
        bUpdateDlgControls = TRUE;
    }

    // Update the dialog controls
    if( bUpdateDlgControls )
    {
        // Reset the content in each of the combo boxes
        ComboBox_ResetContent( hwndAdapterList );
        ComboBox_ResetContent( hwndDeviceList );
        ComboBox_ResetContent( hwndFullscreenModeList );
        ComboBox_ResetContent( hwndMultiSampleList );

        pAdapter = &pd3dApp->m_Adapters[dwNewAdapter];
        pDevice  = &pAdapter->devices[dwNewDevice];

        // Add a list of adapters to the adapter combo box
        for( DWORD a=0; a < pd3dApp->m_dwNumAdapters; a++ )
        {
            // Add device name to the combo box
            DWORD dwItem = ComboBox_AddString( hwndAdapterList,
                             pd3dApp->m_Adapters[a].d3dAdapterIdentifier.Description );

            // Set the item data to identify this adapter
            ComboBox_SetItemData( hwndAdapterList, dwItem, a );

            // Set the combobox selection on the current adapater
            if( a == dwNewAdapter )
                ComboBox_SetCurSel( hwndAdapterList, dwItem );
        }

        // Add a list of devices to the device combo box
        for( DWORD d=0; d < pAdapter->dwNumDevices; d++ )
        {
            // Add device name to the combo box
            DWORD dwItem = ComboBox_AddString( hwndDeviceList,
                                               pAdapter->devices[d].strDesc );

            // Set the item data to identify this device
            ComboBox_SetItemData( hwndDeviceList, dwItem, d );

            // Set the combobox selection on the current device
            if( d == dwNewDevice )
                ComboBox_SetCurSel( hwndDeviceList, dwItem );
        }

        // Add a list of modes to the mode combo box
        DWORD m; for (m=0; m < pDevice->dwNumModes; m++ )
        {
            DWORD BitDepth = 16;
            if( pDevice->modes[m].Format == D3DFMT_X8R8G8B8 ||
                pDevice->modes[m].Format == D3DFMT_A8R8G8B8 ||
                pDevice->modes[m].Format == D3DFMT_R8G8B8 )
            {
                BitDepth = 32;
            }

            // Add mode desc to the combo box
            TCHAR strMode[80];
            _stprintf( strMode, ToTCHAR("%ld x %ld x %ld"), pDevice->modes[m].Width,
                                                       pDevice->modes[m].Height,
                                                       BitDepth );
            DWORD dwItem = ComboBox_AddString( hwndFullscreenModeList, strMode );

            // Set the item data to identify this mode
            ComboBox_SetItemData( hwndFullscreenModeList, dwItem, m );

            // Set the combobox selection on the current mode
            if( m == dwNewMode )
                ComboBox_SetCurSel( hwndFullscreenModeList, dwItem );
        }

        // Add a list of multisample modes to the multisample combo box
        for( m=0; m <= 16; m++ )
        {
            TCHAR strDesc[50];

            D3DFORMAT fmt;
            if( bNewWindowed )
                fmt = pd3dApp->m_Adapters[dwNewAdapter].d3ddmDesktop.Format;
            else
                fmt = pDevice->modes[dwNewMode].Format;

            if ( m == 1 ) // 1 is not a valid multisample type
                continue;

            if( SUCCEEDED( pd3dApp->m_pD3D->CheckDeviceMultiSampleType( dwNewAdapter,
                pDevice->DeviceType, fmt, bNewWindowed, (D3DMULTISAMPLE_TYPE)m, NULL ) ) )
            {
                if( m == 0 )
                    lstrcpy( strDesc, ToTCHAR("none") );
                else
                    wsprintf( strDesc, ToTCHAR("%d samples"), m );

                // Add device name to the combo box
                DWORD dwItem = ComboBox_AddString( hwndMultiSampleList, strDesc );

                // Set the item data to identify this multisample type
                ComboBox_SetItemData( hwndMultiSampleList, dwItem, m );

                // Set the combobox selection on the current multisample type
                if( (D3DMULTISAMPLE_TYPE)m == NewMultiSampleType || m == 0 )
                    ComboBox_SetCurSel( hwndMultiSampleList, dwItem );
            }
        }
        DWORD dwItem = ComboBox_GetCurSel( hwndMultiSampleList );
        NewMultiSampleType = (D3DMULTISAMPLE_TYPE)ComboBox_GetItemData( hwndMultiSampleList, dwItem );
        EnableWindow( hwndMultiSampleList, ComboBox_GetCount( hwndMultiSampleList ) > 1);
        EnableWindow( hwndWindowedRadio, pDevice->bCanDoWindowed );

        if( bNewWindowed )
        {
            Button_SetCheck( hwndWindowedRadio,   TRUE );
            Button_SetCheck( hwndFullscreenRadio, FALSE );
            EnableWindow( hwndFullscreenModeList, FALSE );
        }
        else
        {
            Button_SetCheck( hwndWindowedRadio,   FALSE );
            Button_SetCheck( hwndFullscreenRadio, TRUE );
            EnableWindow( hwndFullscreenModeList, TRUE );
        }
        return TRUE;
    }

    return FALSE;
}




//-----------------------------------------------------------------------------
// Name: Pause()
// Desc: Called in to toggle the pause state of the app.
//-----------------------------------------------------------------------------
VOID CD3DApplication::Pause( BOOL bPause )	// FUNCTION: 0x0052c3c0
{
    static DWORD dwAppPausedCount = 0L;								// 0089c0f8

    dwAppPausedCount += ( bPause ? +1 : -1 );
    m_bReady          = ( dwAppPausedCount ? FALSE : TRUE );

    // Handle the first pause request (of many, nestable pause requests)
    if( bPause && ( 1 == dwAppPausedCount ) )
    {
        // Stop the scene from animating
        if( m_bFrameMoving )
            DXUtil_Timer( TIMER_STOP );
    }

    if( 0 == dwAppPausedCount )
    {
        // Restart the timers
        if( m_bFrameMoving )
            DXUtil_Timer( TIMER_START );
    }
}




//-----------------------------------------------------------------------------
// Name: Cleanup3DEnvironment()
// Desc: Cleanup scene objects
//-----------------------------------------------------------------------------
VOID CD3DApplication::Cleanup3DEnvironment()	// FUNCTION: 0x0052c430
{
    m_bActive = FALSE;
    m_bReady  = FALSE;

    if( m_pd3dDevice )
    {
        ReleaseDeviceObjects(false);
		SAFE_RELEASE(gResourceQuery);

        int drc=m_pd3dDevice->Release();
        int d3rc=m_pD3D->Release();
		char buf[256];
		sprintf(buf,"d3ddev refcount=%d\n",drc);
		TRACE(buf);
		sprintf(buf,"d3d refcount=%d\n",d3rc);
		TRACE(buf);

        m_pd3dDevice = NULL;
        m_pD3D       = NULL;
    }

    FinalCleanup();
}




//-----------------------------------------------------------------------------
// Name: DisplayErrorMsg()
// Desc: Displays error messages in a message box
//-----------------------------------------------------------------------------
HRESULT CD3DApplication::DisplayErrorMsg( HRESULT hr, DWORD dwType )	// FUNCTION: 0x0052c4f0
{
	// retail: each error is a localized fatal error (CConsole::FatalError's strings 0xb6..0xc5)
    switch( hr )
    {
        case D3DAPPERR_NULLREFDEVICE:
			CONSOLE.FatalError(FALSE, 0xb6, -1);
            break;

        case D3DAPPERR_NODIRECT3D:
			CONSOLE.FatalError(FALSE, 0xb7, -1);
            break;

        case D3DAPPERR_NOCOMPATIBLEDEVICES:
			if (!gDeviceHasLargeTextures)
				CONSOLE.FatalError(FALSE, 0xc5, -1);
			else
				CONSOLE.FatalError(FALSE, 0xb8, -1);
            break;

        case D3DAPPERR_NOWINDOWABLEDEVICES:
			CONSOLE.FatalError(FALSE, 0xb9, -1);
            break;

        case D3DAPPERR_NOHARDWAREDEVICE:
			CONSOLE.FatalError(FALSE, 0xba, -1);
            break;

        case D3DAPPERR_HALNOTCOMPATIBLE:
			CONSOLE.FatalError(FALSE, 0xbb, -1);
            break;

        case D3DAPPERR_NOWINDOWEDHAL:
			CONSOLE.FatalError(FALSE, 0xbc, -1);
            break;

        case D3DAPPERR_NODESKTOPHAL:
			CONSOLE.FatalError(FALSE, 0xbd, -1);
            break;

        case D3DAPPERR_NOHALTHISMODE:
			CONSOLE.FatalError(FALSE, 0xbe, -1);
            break;

        case D3DAPPERR_MEDIANOTFOUND:
			CONSOLE.FatalError(FALSE, 0xbf, -1);
            break;

        case D3DAPPERR_RESIZEFAILED:
			CONSOLE.FatalError(FALSE, 0xc0, -1);
            break;

        case D3DAPPERR_NONZEROREFCOUNT:
			CONSOLE.FatalError(FALSE, 0xc1, -1);
            break;

        case E_OUTOFMEMORY:
			CONSOLE.FatalError(FALSE, 0xc2, -1);
            break;

        case D3DERR_OUTOFVIDEOMEMORY:
			CONSOLE.FatalError(FALSE, 0xc3, -1);
            break;

        default:
			CONSOLE.FatalError(FALSE, 0xc4, -1);
    }

    return hr;
}

//-----------------------------------------------------------------------------
void CD3DApplication::SetResolution(int width, int height)	// FUNCTION: 0x0052c730
{
	m_dwCreationWidth = width;
	m_dwCreationHeight = height;
}

//-----------------------------------------------------------------------------
D3DDeviceInfo::D3DDeviceInfo()	// FUNCTION: 0x0052c750
{
	dwCurrentMode = -1;
	MultiSampleType = (D3DMULTISAMPLE_TYPE)-1;
	dwLowDetailMode = 0;
	dwMediumDetailMode = 0;
	dwHighDetailMode = 0;
}

//-----------------------------------------------------------------------------
void CD3DApplication::UpdateScreenShape()	// FUNCTION: 0x0052c780
{
	float	aspect;
	switch (SCREEN_SHAPE.mValue)
	{
	case 1:
		aspect = 16.f / 9.f;
		break;
	case 2:
		mScreenShapeScale = 1.f;
		return;
	default:
		aspect = 4.f / 3.f;
		break;
	}
	mScreenShapeScale = aspect * ((float)m_d3dpp.BackBufferHeight / m_d3dpp.BackBufferWidth);
}

#endif
// STAND-IN (link layout): retail's 320 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_d3dapp[320] = { 0 };
