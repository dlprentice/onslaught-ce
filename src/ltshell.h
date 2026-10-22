// ltshell.h: the application shell, PCLTShell (LT at 00855bb0; vtable 005e488c). Stuart's ltshell.h is the
// Direct3D 8 original; this is the retail Direct3D 9 port, reconstructed as matched code needs it.

#ifndef LT_SHELL_INCLUDE
#define LT_SHELL_INCLUDE

#include "d3dapp.h"
#include "deviceobject.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <dxerr9.h>


#define MAX_JOYPADS 4

#define	N_RENDERSTATES			172
#define N_TEXTURESTAGESTATES	30
#define INVALID_STATE			0xfedcba98

// retail: a failed call is reported (00513770); Stuart's release build returned the result unchecked
#define SANITY_CHECK_AND_RETURN(CALL) \
{ \
	HRESULT sanity_check_hresult=CALL;\
	if (FAILED(sanity_check_hresult))\
	{\
		TRACE("D3D Error! ");\
		OutputDebugString(DXGetErrorString9(sanity_check_hresult));\
		TRACE("\n");\
	}\
	return(sanity_check_hresult);\
}

#include "Texture.h"

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL	0x020A		// winuser.h defines it only for _WIN32_WINNT >= 0x0400
#endif

#include "keytrap.h"

class CVertexShader;

class PCLTShell : public CD3DApplication
{
public:
	PCLTShell();											// 00512670
	~PCLTShell();

	void			SetRunning(bool running) { mRunning = running; }
	BOOL			WantsToQuit() { return mMsg.message == WM_QUIT; }
	int				GetWindowWidth() { return m_d3dpp.BackBufferWidth; }
	int				GetWindowHeight() { return m_d3dpp.BackBufferHeight; }
	DIJOYSTATE2*	JoyState(int padnumber) { return mJoyState[padnumber]; };
	DIJOYSTATE2*	OldJoyState(int padnumber) { return mOldJoyState[padnumber]; };
	BOOL			JoyButtonOnce(int padnumber, int button) {	if ((!OldJoyState(padnumber)->rgbButtons[button]) &&
														JoyState(padnumber)->rgbButtons[button])
														return TRUE; else return FALSE; }
	BOOL			JoyButtonOn(int padnumber, int button) {	if (JoyState(padnumber)->rgbButtons[button])
														return TRUE; else return FALSE; }
	BOOL			JoyButtonRelease(int padnumber, int button) {	if ((OldJoyState(padnumber)->rgbButtons[button]) &&
														JoyState(padnumber)->rgbButtons[button] == FALSE)
														return TRUE; else return FALSE; }
	LPDIRECT3DDEVICE9	GetDevice() { return m_pd3dDevice; }	// Stuart: "Needed to match XBox"
	int				MainLoop();
	BOOL			SetupSystemMessageHandler();
	int				MsgInfo() { return mMsg.wParam; }
	HWND			GetHWnd() { return m_hWnd; }
	void			ShowGFXCardInfo();
	void			ClearKeyWasUp();									// 00512470
	BOOL			HandlingSystemStuff(bool first);					// 00512490
	void			DisplayScreen();									// 00512630
	HRESULT			UpdateJoystick(int joypad, bool first);				// 00513370
	HRESULT			InitDeviceObjects();
	HRESULT			RestoreDeviceObjects();
	HRESULT			InvalidateDeviceObjects();
	HRESULT			DeleteDeviceObjects();
	HRESULT			ReleaseDeviceObjects(bool show_progress);
	HRESULT			FinalCleanup();
	HRESULT			ConfirmDevice(D3DCAPS9* pCaps, DWORD dwBehavior, D3DFORMAT Format);
	void			AddDeviceObject(DeviceObject* devob);				// 00512ca0 (inline in Stuart's)
	BOOL			RemoveDeviceObject(DeviceObject* devob);			// 00512cc0
	HRESULT			InitDirectInput(HWND hDlg);							// 00513120
	VOID			FreeDirectInput();
	LRESULT			MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
	void			FlushInputBuffers();
	static BOOL CALLBACK EnumJoysticksCallback( const DIDEVICEINSTANCE* pdidInstance, VOID* pContext );
	static BOOL CALLBACK EnumAxesCallback( const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext );
	pKeyTrapper		GetKeytrap() { return mCurrentKeytrap; }

	void			DumpScreen(int number);								// 005134a0
	void			SetKeytrap(pKeyTrapper trap);						// 005135f0
	void			InvalidateAllStates();								// 00513600
	int				GetBPP(D3DFORMAT fmt);								// 00513640
	BOOL			IsThisAGeForce3();
	void			TriggerQuit();										// 00513730
	ETextureFormat	GetDisplayFormat();									// 00513760
	// Stuart's inline device wrappers (the D3D 9 swap-chain argument is 0; CDXEngine::Init 0053d657,
	// CDXEngine::PreRender 0053e241..0053e2b3)
	void			D3D_GetGammaRamp(D3DGAMMARAMP *ramp) { m_pd3dDevice->GetGammaRamp(0, ramp); }
	HRESULT			D3D_SetMaterial(D3DMATERIAL9 *material) { return m_pd3dDevice->SetMaterial(material); }
	void			D3D_SetGammaRamp(DWORD flags, D3DGAMMARAMP *ramp) { m_pd3dDevice->SetGammaRamp(0, flags, ramp); }
	HRESULT			D3D_BeginScene() { return m_pd3dDevice->BeginScene(); }
	HRESULT			D3D_EndScene() { return m_pd3dDevice->EndScene(); }
	HRESULT			D3D_Clear(DWORD count, CONST D3DRECT *rects, DWORD flags, D3DCOLOR col, float z, DWORD stencil)
						{ return m_pd3dDevice->Clear(count, rects, flags, col, z, stencil); }
	HRESULT			D3D_CreateVertexBuffer( UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer);
	HRESULT			D3D_CreateIndexBuffer( UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer);
	int				D3D_ReleaseVertexBuffer(IDirect3DVertexBuffer9 * pVertexBuffer);

// Encapsulated device state: inline in Stuart's D3D 8 header, out of line in retail (00513820-00514030), and
// written against LT's device rather than this one (every caller passes LT).
	void			STS(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD val);			// 00513820
	void			ForceTS(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD val);		// 00513870
	void			SSS(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD val);			// 005138b0 retail
	void			SSS_NoCache(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD val);	// 00513930 retail
	HRESULT			D3D_CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage,	// 005139a0
						D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture);
	HRESULT			D3D_CreateTextureNoCheck(UINT Width, UINT Height, UINT Levels, DWORD Usage,	// 00513a10 retail
						D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture);
	void			D3D_SetTexture(UINT Name, IDirect3DBaseTexture9* pTexture);			// 00513a50
	BYTE			xKeyUp(int c);														// 00513a80 retail
	BYTE			xKeyOnceThisFrame(int c);											// 00513a90 retail (a key read once stays readable this frame)
	BYTE			xKeyOn(int c) { return KeyDown[c]; };
	BYTE			xKeyOnce(int c) { BYTE a=KeyWasDown[c]; KeyWasDown[c]=0; return a; };
	void			SetModulate(DWORD stage);											// 00513af0 retail: COLOROP, 2x when USE_MODULATE_2X
	void			ForceModulate(DWORD stage);										// 00513b60 retail
	void			SRS(D3DRENDERSTATETYPE state, DWORD value);							// 00513bc0
	void			ForceRS(D3DRENDERSTATETYPE state, DWORD value);						// 00513c20
	HRESULT			D3D_DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, UINT MinIndex, UINT NumVertices,	// 00513c70
						UINT StartIndex, UINT PrimCount);
	HRESULT			SetSoftwareVertexProcessing(bool software);							// 00513ca0 retail
	void			MoveDeviceObjectToFirst(DeviceObject* devob);						// 00513d20 retail
	void			SetAlphaRef(DWORD value);											// 00513d90 retail
	void			ForceAlphaRef(DWORD value);											// 00513dd0 retail
	HRESULT			D3D_ValidateDevice();												// 00513e00 retail
	void			SetVertexShader(CVertexShader* shader);								// 00513e20 retail
	void			SetFVF(DWORD fvf);													// 00513e90 retail
	void			ForceFVF(DWORD fvf);												// 00513ec0 retail
	HRESULT			D3D_CreatePixelShader(char* text, IDirect3DPixelShader9** ppShader);	// 00513f20 retail
	HRESULT			D3D_CreateVertexShader(CONST DWORD* pDeclaration, CONST DWORD* pFunction,	// 00513ff0
						DWORD* pHandle, DWORD Usage);
	void			D3D_Release(IUnknown** ppObject);									// 00514010 retail
	void			SetFogMode(bool enable);											// 00514030 retail

	static DWORD					mRenderStates[N_RENDERSTATES];		// 00855540
	static DWORD					mTextureStageStates[8][N_TEXTURESTAGESTATES];	// 008557f0
	static IDirect3DBaseTexture9*	mCurrentTextures[8];				// 008554d0
	static DWORD					mSoftwareVertexProcessing;			// 00889070 retail (Stuart's mLastVertexShader)
	static CVertexShader*			mCurrentVertexShader;				// 0088906c retail
	static DWORD					mCurrentFVF;						// 00889068 retail
	static DeviceObject*	mDeviceObjects;								// 00889074
	static DeviceObject*	mFirstDeviceObjects;						// 00889078: retail; initialised and restored first

	bool			mRunning;							// +0x330c8 (00888c78)
	BOOL			DodgyJoyX[MAX_JOYPADS];
	BOOL			mD3DActive;
	int				mDeadZone;
	BYTE			KeyDown[256];						// +0x330e4 held, by scan code in retail
	BYTE			KeyWasDown[256];					// +0x331e4 pressed since last read (GetKeyOnce)
	BYTE			KeyWasUp[256];						// +0x332e4 retail: released since last cleared
	DIJOYSTATE2*	mJoyState[MAX_JOYPADS];				// +0x333e4
	DIJOYSTATE2*	mOldJoyState[MAX_JOYPADS];			// +0x333f4
	int				mD3DErrorCount;						// +0x33404 (00888fb4): CGCGamut::RenderThings zeroes it

	LPDIRECTINPUTDEVICE8	pJoystick[MAX_JOYPADS];		// +0x33408
	DIDEVCAPS				diDevCaps;
	bool					mJoypadIsNewtype[MAX_JOYPADS];
	int						mJoypads;					// +0x33448
	int						mMouseX;
	int						mMouseY;
	int						mMouseButtons;

	void					ReleaseSurfaces();			// Stuart's ltshell.h declarations, restored at his position (nothing calls them)
	HRESULT					RestoreSurfaces();

	void					InitialiseRumble(int joypad);
	pKeyTrapper				mCurrentKeytrap;
	BOOL					m_bSmallTexturesDisabled, m_bTextureCompressionDisabled;	// +0x3345c, +0x33460 (00889010): the PC's loaders
											// raise the second around textures that must stay uncompressed (CFrontEndData::Init 00468a7a,
											// CDXBattleLine, CDXFont, CDXLandscape, CDXShadows, CWaterRenderSystem); CDXTexture's slot 5 reads it (00556eef)
	BOOL					mJoypadHasRz[MAX_JOYPADS];	// +0x33464 retail: the pad has an Rz axis (EnumAxesCallback)
	bool					mUnknown33474[MAX_JOYPADS];	// retail: TRUE for each pad; UpdateJoystick tests it
	LPDIRECTINPUT8			pDI;						// +0x33478 (moved in retail)
	LPDIRECTINPUTEFFECT		mRumbleEffect[MAX_JOYPADS];	// +0x3347c
	bool					mRumbleEnabled;				// +0x3348c
	bool					mRumbleInitialised[MAX_JOYPADS];

	MSG				mMsg;								// +0x33494 (00889044)
	HACCEL			mhAccel;							// +0x334b0 (00889060)
	HINSTANCE		mhInstance;							// +0x334b4 (00889064)
};

extern PCLTShell LT;

#endif
