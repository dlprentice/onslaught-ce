// mouse.cpp: the Windows mouse (not in Stuart's release; rebuilt from the retail object after mixermap.obj,
// 005233c0..00523de1). The window's mouse messages (PCLTShell::MsgProc 00512f83) keep the cursor and three buttons;
// the front end reads them through the rectangle tests below. The cursor is in window pixels, rescaled to the
// front end's 640x480 while it runs. An installed key trap sees each button first (key 0..2 for left, middle and
// right, 3 and 4 for the wheel; event 4 down, 3 up) and swallows it by returning true.

#include "common.h"
#include "mouse.h"
#include "Platform.h"
#include "ltshell.h"
#include "CLIParams.h"
#include "frontend.h"
#include "DXEngine.h"
#include "Texture.h"
#include "SpriteRenderer.h"

// The object's initializers are a zero vector (005233c0, data 0089bd88) from a header not yet known, then
// mathsconst.h's pair (reached through frontend.h): ZERO_FVECTOR's data sits just above ID_FMATRIX's, as in
// every object with the pair.
// FUNCTION: 0x005233e0 init:ZERO_FVECTOR
// FUNCTION: 0x00523400 init:ID_FMATRIX

extern bool		gKeyPressed;						// 0083d3f0 (ltshell)

typedef bool (*pMouseTrapper)(WORD key, int event);		// the key trap's retail signature (VKKeyTrap 0051feb0)
#define MOUSE_TRAP	((pMouseTrapper)LT.GetKeytrap())

enum
{
	MOUSE_EVENT_UP = 3,
	MOUSE_EVENT_DOWN = 4
};

int					MOUSE_X;							// 0089bda8
int					MOUSE_Y;							// 0089bda4
static float		sMouseFracX;						// 0089bda0
static float		sMouseFracY;						// 0089bd9c: the cursor as a fraction of the window
static CDXTexture		*sCursor;							// 0089bd98: mouse.tga, or the default texture
static BOOL			sMouseInputActive = FALSE;					// 0089bdf0: DirectInput moves the cursor (InitMouse)
bool				gMouseFlags[4] = {false};					// 0089bdf4: see mouse.h (a stand-in for retail's declaration)
#define sMouseMoved	gMouseFlags[0]				// the cursor moved; the hover test consumes it
SMouseButton		MOUSE_BUTTONS[3] = {0};					// 0089bdf8
static int			sLastMoveX = 0;							// 0089be40
static int			sLastMoveY = 0;							// 0089be44
int					MOUSE_WHEEL = 0;						// 0089be48
UBYTE				MOUSE_CURSOR_ALPHA = 255;			// 00640054

//******************************************************************************************
void	SetMouseInputActive(BOOL active)	// FUNCTION: 0x005234d0
{
	sMouseInputActive = active;
}

//******************************************************************************************
LRESULT __stdcall	HandleMouseMessage(UINT msg, WPARAM wParam, LPARAM lParam)	// FUNCTION: 0x005234e0
{
	if (CLIPARAMS.mAutoConfigTest)
		return FALSE;

	switch (msg)
	{
	case WM_MOUSEMOVE:
		if (!(wParam & MK_LBUTTON))
			MOUSE_HELD[0] = false;
		if (!(wParam & MK_RBUTTON))
			MOUSE_HELD[2] = false;
		if (!(wParam & MK_MBUTTON))
			MOUSE_HELD[1] = false;
		if (!sMouseInputActive)
		{
			MOUSE_X = lParam & 0xffff;
			MOUSE_Y = DWORD(lParam) >> 16;
			sMouseFracX = float(MOUSE_X) / float(PLATFORM.GetWindowWidth());
			sMouseFracY = float(MOUSE_Y) / float(PLATFORM.GetWindowHeight());
		}
		if (FRONTEND.IsRunning())
		{
			MOUSE_X *= 640;
			MOUSE_X /= PLATFORM.GetWindowWidth();
			MOUSE_Y *= 480;
			MOUSE_Y /= PLATFORM.GetWindowHeight();
		}
		sLastMoveX = MOUSE_X;
		sLastMoveY = MOUSE_Y;
		sMouseMoved = true;
		break;

	case WM_RBUTTONUP:
		if (MOUSE_TRAP && MOUSE_TRAP(2, MOUSE_EVENT_UP))
			break;
		MOUSE_HELD[2] = false;
		MOUSE_BUTTONS[2].mReleased = TRUE;
		break;

	case WM_RBUTTONDOWN:
		gKeyPressed = true;
		if (MOUSE_TRAP && MOUSE_TRAP(2, MOUSE_EVENT_DOWN))
			break;
		MOUSE_HELD[2] = true;
		if (!MOUSE_BUTTONS[2].mDown)
		{
			MOUSE_BUTTONS[2].mDown = TRUE;
			MOUSE_BUTTONS[2].mX = lParam & 0xffff;
			MOUSE_BUTTONS[2].mY = DWORD(lParam) >> 16;
			MOUSE_BUTTONS[2].mFracX = float(MOUSE_BUTTONS[2].mX) / float(PLATFORM.GetWindowWidth());
			MOUSE_BUTTONS[2].mFracY = float(MOUSE_BUTTONS[2].mY) / float(PLATFORM.GetWindowHeight());
			if (FRONTEND.IsRunning())
			{
				MOUSE_BUTTONS[2].mX *= 640;
			MOUSE_BUTTONS[2].mX /= PLATFORM.GetWindowWidth();
				MOUSE_BUTTONS[2].mY *= 480;
			MOUSE_BUTTONS[2].mY /= PLATFORM.GetWindowHeight();
			}
		}
		break;

	case WM_LBUTTONUP:
		if (MOUSE_TRAP && MOUSE_TRAP(0, MOUSE_EVENT_UP))
			break;
		MOUSE_HELD[0] = false;
		MOUSE_BUTTONS[0].mReleased = TRUE;
		break;

	case WM_LBUTTONDOWN:
		gKeyPressed = true;
		if (MOUSE_TRAP && MOUSE_TRAP(0, MOUSE_EVENT_DOWN))
			break;
		MOUSE_HELD[0] = true;
		if (!MOUSE_BUTTONS[0].mDown)
		{
			MOUSE_BUTTONS[0].mDown = TRUE;
			MOUSE_BUTTONS[0].mX = lParam & 0xffff;
			MOUSE_BUTTONS[0].mY = DWORD(lParam) >> 16;
			MOUSE_BUTTONS[0].mFracX = float(MOUSE_BUTTONS[0].mX) / float(PLATFORM.GetWindowWidth());
			MOUSE_BUTTONS[0].mFracY = float(MOUSE_BUTTONS[0].mY) / float(PLATFORM.GetWindowHeight());
			if (FRONTEND.IsRunning())
			{
				MOUSE_BUTTONS[0].mX *= 640;
			MOUSE_BUTTONS[0].mX /= PLATFORM.GetWindowWidth();
				MOUSE_BUTTONS[0].mY *= 480;
			MOUSE_BUTTONS[0].mY /= PLATFORM.GetWindowHeight();
			}
		}
		break;

	case WM_MBUTTONUP:
		if (MOUSE_TRAP && MOUSE_TRAP(1, MOUSE_EVENT_UP))
			break;
		MOUSE_BUTTONS[1].mReleased = TRUE;
		MOUSE_HELD[1] = false;
		break;

	case WM_MBUTTONDOWN:
		gKeyPressed = true;
		if (MOUSE_TRAP && MOUSE_TRAP(1, MOUSE_EVENT_DOWN))
			break;
		MOUSE_HELD[1] = true;
		if (!MOUSE_BUTTONS[1].mDown)
		{
			MOUSE_BUTTONS[1].mDown = TRUE;
			MOUSE_BUTTONS[1].mX = lParam & 0xffff;
			MOUSE_BUTTONS[1].mY = DWORD(lParam) >> 16;
			MOUSE_BUTTONS[1].mFracX = float(MOUSE_BUTTONS[1].mX) / float(PLATFORM.GetWindowWidth());
			MOUSE_BUTTONS[1].mFracY = float(MOUSE_BUTTONS[1].mY) / float(PLATFORM.GetWindowHeight());
			if (FRONTEND.IsRunning())
			{
				MOUSE_BUTTONS[1].mX *= 640;
			MOUSE_BUTTONS[1].mX /= PLATFORM.GetWindowWidth();
				MOUSE_BUTTONS[1].mY *= 480;
			MOUSE_BUTTONS[1].mY /= PLATFORM.GetWindowHeight();
			}
		}
		break;

	case WM_MOUSEWHEEL:
	{
		short delta = short(HIWORD(wParam));
		if (MOUSE_TRAP)
		{
			if (delta > 0)
			{
				if (MOUSE_TRAP(3, MOUSE_EVENT_UP))
					break;
			}
			else
			{
				if (MOUSE_TRAP(4, MOUSE_EVENT_UP))
					break;
			}
		}
		MOUSE_WHEEL += delta;
		break;
	}
	}

	return 0;
}

//******************************************************************************************
void	RenderMouseCursor()	// FUNCTION: 0x00523a70
{
	if (!sCursor)
	{
		sCursor = CTEXTURE::GetTextureByName("mouse.tga");
		if (!sCursor)
		{
			sCursor = ENGINE.GetDefaultTexture();
			sCursor->AddRef();
		}
	}

	CSPRITERENDERER::DrawSprite(float(MOUSE_X), float(MOUSE_Y), 0.001f, sCursor, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f,
								(MOUSE_CURSOR_ALPHA << 24) | 0xffffff, 0.25f, 0.25f, 0.f, 0.96875f, 0.f, 0.96875f);
}

//******************************************************************************************
void	ReleaseMouseCursor()	// FUNCTION: 0x00523b30
{
	if (sCursor)
	{
		sCursor->Release();
		sCursor = NULL;
	}
}

//******************************************************************************************
inline BOOL	MouseAt(float tlx, float tly, float brx, float bry)
{
	return (float(MOUSE_X) >= tlx) && (float(MOUSE_X) < brx) && (float(MOUSE_Y) >= tly) && (float(MOUSE_Y) < bry);
}

//******************************************************************************************
bool	MouseInRect(float tlx, float tly, float brx, float bry)	// FUNCTION: 0x00523b50
{
	if (!sMouseMoved)
		return false;
	if (LT.GetKeytrap())
		return false;

	return (!CLIPARAMS.mAutoConfigTest) && MouseAt(tlx, tly, brx, bry);
}

//******************************************************************************************
bool	MouseDispatchClickInRect(float tlx, float tly, float brx, float bry, SINT button)	// FUNCTION: 0x00523bc0
{
	if (MOUSE_TRAP && MOUSE_TRAP(0, MOUSE_EVENT_DOWN))
		return false;

	if (MouseClickedInRect(tlx, tly, brx, bry))
		FRONTEND.ReceiveButtonAction(FRONTEND.GetController(0), button, 1.f);

	return MouseInRect(tlx, tly, brx, bry);
}

//******************************************************************************************
// FUNCTION: 0x00523cc0 ?MouseClickedInRect@@YA_NMMMM@Z
bool	MouseClickedInRect(float tlx, float tly, float brx, float bry)
{
	if (MOUSE_TRAP && MOUSE_TRAP(0, MOUSE_EVENT_DOWN))
		return false;

	if ((!CLIPARAMS.mAutoConfigTest) && MOUSE_BUTTONS[0].mReleased && MouseAt(tlx, tly, brx, bry))
	{
		MOUSE_BUTTONS[0].mReleased = FALSE;
		return true;
	}
	return false;
}

//******************************************************************************************
// FUNCTION: 0x00523d40 ?MouseOverRect@@YA_NMMMM@Z
bool	MouseOverRect(float tlx, float tly, float brx, float bry)
{
	if (MOUSE_TRAP && MOUSE_TRAP(0, MOUSE_EVENT_DOWN))
		return false;

	if ((!CLIPARAMS.mAutoConfigTest) && sMouseMoved && MouseAt(tlx, tly, brx, bry))
	{
		sMouseMoved = false;
		return true;
	}
	return false;
}

//******************************************************************************************
void	ResetMouseTransientState()	// FUNCTION: 0x00523db0
{
	MOUSE_CURSOR_ALPHA = 255;
	MOUSE_BUTTONS[0].mDown = FALSE;
	MOUSE_BUTTONS[2].mDown = FALSE;
	MOUSE_BUTTONS[1].mDown = FALSE;
	MOUSE_BUTTONS[0].mReleased = FALSE;
	MOUSE_BUTTONS[2].mReleased = FALSE;
	MOUSE_BUTTONS[1].mReleased = FALSE;
	sMouseMoved = false;
	MOUSE_WHEEL = 0;
}
