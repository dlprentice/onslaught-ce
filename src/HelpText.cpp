// HelpText.cpp: the help text display (reconstructed from HelpText.obj, 0047f9c0..0047fd32; Stuart's release has no
// copy). The script adds a message; each shows, centred over the HUD's view, for 20 seconds.

#include	"Common.h"

#include	"HelpText.h"
#include	"DebugLog.h"
#include	"eventmanager.h"
#include	"Platform.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"Career.h"
#include	"Hud.h"

// FUNCTION: 0x0047f9c0 init:ZERO_FVECTOR
// FUNCTION: 0x0047f9e0 init:ID_FMATRIX
// FUNCTION: 0x0047fad0 ??_GCHelpTextDisplay@@UAEPAXI@Z

//*********************************************************************************
CHelpTextDisplay::CHelpTextDisplay()	// FUNCTION: 0x0047fab0
{
}

//*********************************************************************************
void	CHelpTextDisplay::AddMessage(WCHAR *text)	// FUNCTION: 0x0047fb00
{
	int i;
	for (i = 0; i < MAX_HELP_TEXT_MESSAGES; i ++)
	{
		if (!mMessages[i].mActive)
		{
			mMessages[i].mText = text;
			mMessages[i].mTime = EVENT_MANAGER.GetTime();
			mMessages[i].mActive = TRUE;
			return;
		}
	}

	LOG.AddMessage("ERROR: Added too many messages to help text display");
}

//*********************************************************************************
void	CHelpTextDisplay::Render()	// FUNCTION: 0x0047fb50
{
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.Apply();

	float y = 50.f;

	for (int i = 0; i < MAX_HELP_TEXT_MESSAGES; i ++)
	{
		if (!mMessages[i].mActive)
			continue;

		float t = EVENT_MANAGER.GetTime() - mMessages[i].mTime;
		if (t > 20.f)
		{
			mMessages[i].mActive = FALSE;
			continue;
		}

		if (t < 18.f)
		{
			if (t > 1.0)
				t = 1.f;
		}
		else
			t = (t - 17.f) * 8.f;

		SWordWrapFill	fill;
		int num_lines = PLATFORM.Font(FONT_NORMAL)->WordWrap(&fill, mMessages[i].mText, 550.f);

		if ((CAREER.GetControllerConfigurationNum(0) == 3) || (CAREER.GetControllerConfigurationNum(0) == 4))
			PLATFORM.Font(FONT_NORMAL)->mUnknown16c = TRUE;

		for (int c0 = 0; c0 < num_lines; c0 ++)
		{
			SIZE s;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(fill.c[c0], &s);
			float w = float(s.cx);
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(HUD.mViewportWidth * 0.5f + HUD.mViewportX - w * 0.5f, y, 1.f, 1.f, 1.f, 0xffffffff, fill.c[c0], t, 1, 0);
			y += 32.f;
		}

		PLATFORM.Font(FONT_NORMAL)->mUnknown16c = FALSE;
	}
}
