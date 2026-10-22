// reconnectinterface.cpp: the controller-reconnection prompts (not in Stuart's release; rebuilt from the retail
// object 00527810..00527c7e). A player whose controller is unplugged gets a box over their view asking for it back,
// then for START; START hands control back (Process). RTTI: CReconnectInterface (vtable 005e4a64, 12 bytes).

#include "common.h"
#include "reconnectinterface.h"
#include "Platform.h"
#include "frontend.h"
#include "FrontEndText.h"
#include "text.h"
#include "GameInterface.h"
#include "renderinfo.h"
#include "viewport.h"

// FUNCTION: 0x00527810 init:ZERO_FVECTOR
// FUNCTION: 0x00527830 init:ID_FMATRIX

// FUNCTION: 0x00527900 init:RECONNECT_INTERFACE
// FUNCTION: 0x00527930 exit:RECONNECT_INTERFACE
CReconnectInterface	RECONNECT_INTERFACE[2];		// 0089be50

//******************************************************************************************
void	CReconnectInterface::Reset(int state)	// FUNCTION: 0x00527960
{
	mState = (EState)state;
}

//******************************************************************************************
void	CReconnectInterface::ReceiveButtonAction(CController* from_controller, int button, float val)	// FUNCTION: 0x00527970
{
	if (button == BUTTON_PAUSE)
		mState = RESUMING;
}

BOOL	CReconnectInterface::CanBeControlledWhenInPause()	// the shared return-TRUE (004fdc10)
{
	return TRUE;
}

EControlType	CReconnectInterface::GetControlType()		// the shared return-3 (00457ec0)
{
	return CONTROL_GAMEINTERFACE;
}

//******************************************************************************************
// Keep the measured box dimensions together: retail stores maximum width and total height in adjacent slots.
void	CReconnectInterface::Render(CViewport *viewport, CController *controller, BOOL multiplayer)	// FUNCTION: 0x00527990
{
	int				width, lines, height, x, y;
	SIZE			bounds;
	SIZE			size;
	SWordWrapFill	fill;
	WCHAR			*text;

	if (!viewport)
	{
		x = 0;
		y = 0;
		width = PLATFORM.GetWindowWidth();
		height = PLATFORM.GetWindowHeight();
	}
	else
	{
		x = viewport->X;
		y = viewport->Y;
		width = viewport->Width;
		height = viewport->Height;
	}

	switch (mState)
	{
	case DISCONNECTED:
		switch (controller->GetPadNumber())
		{
		case 0:		text = Localization_GetStringById(0x29);	break;
		case 1:		text = Localization_GetStringById(0x2a);	break;
		default:	text = TEXT.GetStringById(0xcecece9d);		break;
		}
		break;

	case RECONNECTED:
		text = CFrontEndText::GetText(FEX_PRESS_START);
		if (multiplayer)
		{
			if (FRONTEND.GetPlayer0ControllerPort() == controller->GetPadNumber())
				text = CFrontEndText::GetText(FEX_MULTIPLAYER_START_P1);
			else
				text = CFrontEndText::GetText(FEX_MULTIPLAYER_START_P2);
		}
		break;

	default:
		return;
	}

	PLATFORM.Font(FONT_NORMAL)->WordWrap(&fill, text, float(PLATFORM.GetWindowWidth()) * 0.7f);

	for (lines = 0; lines < 40; lines++)
		if (!fill.c[lines][0])
			break;

	bounds.cx = 0;
	int		n;
	for (n = 0; n < lines; n++)
	{
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(fill.c[n], &size);
		bounds.cy = size.cy * lines;
		if (bounds.cx <= size.cx)
			bounds.cx = size.cx;
	}

	float	cx = float(width) * 0.5f + x;
	float	cy = float(height) * 0.5f + y;
	if (multiplayer)
	{
		if (FRONTEND.GetPlayer0ControllerPort() == controller->GetPadNumber())
			cy -= 100.f;
		else
			cy += 100.f;
	}

	GAMEINTERFACE.DrawBackground(cx, cy, float(bounds.cx) + 20.f, float(bounds.cy) + 10.f, 0xff7f7f7f);

	for (n = 0; n < lines; n++)
		PLATFORM.Font(FONT_NORMAL)->DrawText(cx - bounds.cx / 2, cy - (float(lines) * 0.5f - n) * size.cy, 0xffffffff, fill.c[n]);

	RENDERINFO.SetFogEnabled(false);
}

//******************************************************************************************
bool	CReconnectInterface::Process(CController *controller, bool a)	// FUNCTION: 0x00527c50
{
	bool	resumed = false;

	switch (mState)
	{
	case DISCONNECTED:
		mState = RECONNECTED;
		break;

	case RESUMING:
		controller->RelinquishControl();
		mState = OK;
		resumed = true;
		break;
	}
	return resumed;
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00527950 ??0CReconnectInterface@@QAE@XZ
