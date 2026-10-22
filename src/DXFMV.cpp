// DXFMV.cpp: reconstructed from DXFMV.obj (0053f070..0053f58f); Stuart's release has no copy. The PC's film player: a
// Bink file through the front end's video, full screen, until it ends or a key, button or mouse click skips it.

#include "common.h"
#define FMV_CPP
#define DXFMV_CPP
#include "DX.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "Platform.h"
#include "state.h"
#include "Controller.h"
#include "PCController.h"
#include "frontend.h"
#include "fmv.h"
#include "mouse.h"
#include "CLIParams.h"
#include "Texture.h"
#include "SpriteRenderer.h"

// the PC's player (vtable 005e4fe4, 0x40 bytes)
class CDXFMV : public CFMV
{
public:
					CDXFMV();					// 0053f0f0
	virtual void	Init();						// 6 (0053f180)
	virtual void	Unknown7(int a, int b) {}	// 7 (the shared 00452da0)
	virtual void	Unknown8() {}				// 8 (the shared 0040c640)
	virtual void	Unknown9() {}				// 9 (likewise)
	virtual BOOL	Unknown10() { return FALSE; }	// 10 (the shared 005019c0)
	virtual BOOL	Play(char *name, int language, int b, int c, int d, int e);	// 11 (0053f190)

	CDXFrontEndVideo	mVideo;					// +0x10
};

CDXFMV		FMV;								// 0089d690
// FUNCTION: 0x0053f070 init:FMV
// FUNCTION: 0x0053f090 exit:FMV
SINT		FMV_SKIP_FRAMES = 0x7fffffff;		// 00650610: frames before the splash fades in over a film (System.cpp)

extern CDXTexture	*gFMVSplash;					// 0083d3ec (System.cpp)
void	ResetMouseTransientState();				// 00523db0

// FUNCTION: 0x0053f0a0 ??1CDXFMV@@UAE@XZ

#pragma auto_inline(off)
CDXFMV::CDXFMV()	// FUNCTION: 0x0053f0f0
{
}
#pragma auto_inline(on)

// FUNCTION: 0x0053f140 ??_GCDXFMV@@UAEPAXI@Z

//******************************************************************************************
void	CDXFMV::Init()	// FUNCTION: 0x0053f180
{
	PLATFORM.FlushInputBuffers();
}

//******************************************************************************************
// The Render call's y is a float local (`ypos`), retail's extra 4-byte local. The splash height is read as the member,
// like its width, so it is sign-extended where it is loaded (0053f42b) instead of kept as a 16-bit temporary. alpha is
// declared after frames, just before the loop: the one zero register that serves the file probe, the zero arguments and
// stores before the loop and frames is then also alpha (esi), because `alpha = 0` is the last use of that zero and the
// copy coalesces (declared earlier, the zero and alpha were two registers and this and the controller swapped
// ebp/edi). The placement new names its file the way retail does, with an upper-case .CPP, through #line.
BOOL	CDXFMV::Play(char *name, int language, int b, int c, int d, int e)	// FUNCTION: 0x0053f190
{
	char	path[128];
	sprintf(path, "data\\video\\%s.vid", name);
	FILE	*f = fopen(path, "rb");
	if (!f)
		return FALSE;
	fclose(f);

	mVideo.Init();
	mVideo.Open(path, 640, 480, FALSE, FALSE, language);
#line 53 "C:\\dev\\ONSLAUGHT2\\DXFMV.CPP"
	CController	*controller = new (MEMTYPE_CONTROLLER) CPCController(this, 0, 1);

	STATE.UseDefault();
	RENDERINFO.Reset();
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	RENDERINFO.Apply();
	PLATFORM.ClearScreen(0);
	PLATFORM.Flip(FALSE);
	PLATFORM.ClearScreen(0);
	controller->Flush();
	mQuit = FALSE;
	RENDERINFO.SetFogEnabled(false);

	SINT	frames = 0;
	UINT	alpha = 0;
	while (!mQuit)
	{
		ResetMouseTransientState();
		PLATFORM.Process();
		if (MOUSE_BUTTONS[0].mDown || MOUSE_BUTTONS[1].mDown || MOUSE_BUTTONS[2].mDown)
		{
			ResetMouseTransientState();
			mQuit = TRUE;
			CLIPARAMS.mAttractMode = FALSE;
		}
		controller->Flush();

		int	result = mVideo.NextFrame(true);
		if (result == -1)
		{
			Sleep(1);
			continue;
		}

		if (CLIPARAMS.mAutoConfigTest && frames >= 150)
			ReceiveButtonAction(controller, 7, 1.f);

		PLATFORM.ClearScreen(0);
		if (PLATFORM.BeginScene())
		{
			float	xscale = (float)LT.GetWindowWidth() / (float)mVideo.GetWidth();
			float	yscale = LT.mScreenShapeScale * xscale;
			float	height = (float)LT.GetWindowHeight();
			float	ypos = (height - (float)mVideo.GetHeight() * yscale) * 0.5f;
			bool	drawn = mVideo.Render(0, ypos, 0, xscale, yscale, 0xffffffff, FALSE);

			if (frames >= FMV_SKIP_FRAMES && gFMVSplash)
			{
				SINT	h = gFMVSplash->mHeight;
				SINT	w = gFMVSplash->mWidth;
				CDXSpriteRenderer::DrawColouredSprite(0, 0, 0.5f, gFMVSplash, 0xff000000 + alpha * 0x10101,
					(float)PLATFORM.GetWindowWidth() / w, (float)PLATFORM.GetWindowHeight() / h, SA_TOP_LEFT, SPT_NONE, 1.f, 0);
				alpha += 7;
				if (alpha > 255)
					alpha = 255;
			}

			PLATFORM.EndScene();
			if (drawn)
				PLATFORM.Flip(FALSE);
		}

		if (result)
			break;
		frames++;
	}

	mVideo.CloseVideo();
	mVideo.Shutdown();
	PLATFORM.ClearScreen(0);
	PLATFORM.Flip(FALSE);
	CMonitor::Shutdown();
	if (controller)
		delete controller;
	return mQuit;
}

// The object's maths constants, initialised after FMV (.CRT$XCU 0062299c, 006229a0) and emitted after the code.
#include "mathsconst.h"	// FUNCTION: 0x0053f550 init:ZERO_FVECTOR
						// FUNCTION: 0x0053f570 init:ID_FMATRIX
