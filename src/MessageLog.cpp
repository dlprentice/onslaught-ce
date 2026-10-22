// MessageLog.cpp: the message log, the messages already shown, listed from the pause menu (reconstructed from
// MessageLog.obj, 004b8ce0..004ba056; Stuart's release has no copy).

#include	"Common.h"
#include	"PauseMenu.h"		// first, so CGameMenu is declared before CMessageLog: the declaration order decides the order of the RTTI records (retail: CMessageLog's locator first)

#include	"DebugLog.h"
#include	"MessageBox.h"
#include	"frontend.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"text.h"
#include	"mouse.h"

// CGameMenu's empty constructor, inlined by this object's constructor (0048f55f, 004b8df3); PauseMenu.cpp keeps the
// out-of-line copy CPauseMenu::ButtonPressed calls (004d0e40), so the definition is repeated inline here
inline CGameMenu::CGameMenu() {}

// FUNCTION: 0x004b8ce0 init:ZERO_FVECTOR
// FUNCTION: 0x004b8d00 init:ID_FMATRIX
// FUNCTION: 0x004b8e50 ??_GCMessageLog@@UAEPAXI@Z

CDXTexture	*CMessageLog::mArrow = NULL;

//*********************************************************************************
CMessageLog::CMessageLog()	// FUNCTION: 0x004b8dd0
	: mActive(FALSE)
{
	mScroll = 0.f;
	mScrollTarget = 0.f;
	mTop = 0;
	mSelected = 0;
	mArrow = NULL;
	mEndCurve = NULL;
	mBlank = NULL;
	mHeadFrame = NULL;
	mHeadFrameMask = NULL;
	mCanScrollDown = FALSE;
}

//*********************************************************************************
void	CMessageLog::InitResources()	// FUNCTION: 0x004b8e70
{
	mEndCurve = CDXTexture::FindTexture("MessageLog\\endcurve.tga", 4, 0, 1, 0, 1);
	mArrow = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Arrow.tga", 4, 0, 1, 0, 1);
	mBlank = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Blank.tga", 4, 0, 1, 0, 1);
	mHeadFrame = CDXTexture::FindTexture("MessageLog\\messge_headframe.tga", 4, 0, 1, 0, 1);
	mHeadFrameMask = CDXTexture::FindTexture("MessageLog\\messge_headframe_mask.tga", 4, 0, 1, 0, 1);
}

//*********************************************************************************
void	CMessageLog::AddMessage(CMessage *message)	// FUNCTION: 0x004b8ef0
{
	mMessages.Add(message);
}

#define SAFE_RELEASE_TEXTURE(t)	if (t) { t->Release(); t = NULL; }

//*********************************************************************************
CMessageLog::~CMessageLog()	// FUNCTION: 0x004b8f00
{
	mMessages.DeleteAll();
	mActive = FALSE;
	mCanScrollDown = FALSE;

	SAFE_RELEASE_TEXTURE(mArrow);
	SAFE_RELEASE_TEXTURE(mEndCurve);
	SAFE_RELEASE_TEXTURE(mBlank);
	SAFE_RELEASE_TEXTURE(mHeadFrame);
	SAFE_RELEASE_TEXTURE(mHeadFrameMask);
}

//*********************************************************************************
// Each edge and the middle sit in a block that works out its own size from the panel's (width - 64, height - 64) and
// tests it: retail stores a fresh copy before each of the five tests (004b91a7, 004b9203, 004b927d, 004b92db,
// 004b934c..004b9350) and keeps the two edge widths, the two edge heights and the middle's pair in different slots.
// The scale is a float of its own inside each test: that puts the int positions' adds and stores where retail has them
// (004b91df..004b91e9).
void	CMessageLog::RenderPanel(int x, int y, int width, int height, float alpha)	// FUNCTION: 0x004b9010
{
	int a = int(alpha * 160.f);

	if (width < 64)
		width = 64;
	if (height < 64)
		height = 64;

	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.Apply();

	// the corners
	float fy = float(y);
	CSPRITERENDERER::DrawSprite(float(x), fy, 0.004f, mEndCurve, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, a << 24, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f);
	float rx = width + float(x) - 32.f;
	CSPRITERENDERER::DrawSprite(rx, fy, 0.004f, mEndCurve, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, a << 24, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	float by = height + fy - 32.f;
	CSPRITERENDERER::DrawSprite(rx, by, 0.004f, mEndCurve, SA_TOP_LEFT, SPT_NONE, 1.f, PI, a << 24, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f);
	CSPRITERENDERER::DrawSprite(float(x), by, 0.004f, mEndCurve, SA_TOP_LEFT, SPT_NONE, 1.f, PI, a << 24, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);

	// the edges
	{
		int	w = width - 64;
		if (w)
		{
			float	sw = w * 0.0625f;
			CSPRITERENDERER::DrawSprite(float(x + 32), fy, 0.004f, mBlank, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, a << 24, sw, 2.f, 0.f, 1.f, 0.f, 1.f);
		}
	}
	{
		int	w = width - 64;
		if (w)
		{
			float	sw = w * 0.0625f;
			CSPRITERENDERER::DrawSprite(float(x + 32), float(y + height - 32), 0.004f, mBlank, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, a << 24, sw, 2.f, 0.f, 1.f, 0.f, 1.f);
		}
	}
	{
		int	h = height - 64;
		if (h)
		{
			float	sh = h * 0.0625f;
			CSPRITERENDERER::DrawSprite(float(x), float(y + 32), 0.004f, mBlank, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, a << 24, 2.f, sh, 0.f, 1.f, 0.f, 1.f);
		}
	}
	{
		int	h = height - 64;
		if (h)
		{
			float	sh = h * 0.0625f;
			CSPRITERENDERER::DrawSprite(float(x + width - 32), float(y + 32), 0.004f, mBlank, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, a << 24, 2.f, sh, 0.f, 1.f, 0.f, 1.f);
		}
	}

	// the middle
	{
		int	h = height - 64;
		int	w = width - 64;
		if (w && h)
		{
			float	sw = w * 0.0625f;
			float	sh = h * 0.0625f;
			CSPRITERENDERER::DrawSprite(float(x + 32), float(y + 32), 0.004f, mBlank, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, a << 24, sw, sh, 0.f, 1.f, 0.f, 1.f);
		}
	}

	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	RENDERINFO.Apply();
}

// 00482090, called without a this (defined at the end of this file)
void	SetMessageLogRenderStates();

//*********************************************************************************
void	CMessageLog::Render(CViewport *viewport)	// FUNCTION: 0x004b93f0
{
	if (!mActive)
		return;

	mCanScrollDown = FALSE;
	SetMessageLogRenderStates();
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.Apply();

	// darken the game behind the log
	CSPRITERENDERER::DrawSprite(0, 0, 0.01f, mBlank, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, 0xc0101010, 40.f, 30.f, 0.f, 1.f, 0.f, 1.f);
	CSPRITERENDERER::DrawSprite(100.f, 100.f, 0.01f, mHeadFrame, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, 0, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.Apply();

	SIZE	s;
	WCHAR	*text = TEXT.GetString(0x01f08cea);
	PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &s);
	float	tx = 320.f - s.cx / 2;
	PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(tx, 40.f, 0xffa0a0a0, text, 0, 0.002f, 1.f, 1.f);

	if (!mMessages.Size())
	{
		text = TEXT.GetString(0x002a9338);
		RenderPanel(0xaa, 0xb0, 0x12c, 0x40, 1.f);
		PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &s);
		tx = 320.f - s.cx / 2;
		PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(tx, 200.f, 0xffffffff, text, 0, 0.002f, 1.f, 1.f);
	}

	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	RENDERINFO.Apply();

	// the messages above the first one shown
	int		y = SINT(120.f - mScroll);
	int		i;
	if (mTop >= 1)
		for (i = 0; i < mTop; i ++)
			y -= RenderMessageCard(mMessages.At(i), 100.f, 240.f, 255.f, TRUE) + 8;

	BOOL	more_above = FALSE;
	BOOL	more_below = FALSE;
	int		bottom = y;
	for (CMessage *message = mMessages.First(); message; message = mMessages.Next())
	{
		int		height = RenderMessageCard(message, 100.f, 240.f, 255.f, TRUE);
		if (y <= 50)
			more_above = TRUE;
		float	fy = float(y);
		if (fy >= 315.f)
			more_below = TRUE;
		if ((y > 50) && (y < 350))
		{
			float	alpha = 1.f;
			if (y < 120)
			{
				alpha = 1.f - (120 - y) * (1.f / 70.f);
				if (alpha > 1.f)
					alpha = 1.f;
				else if (alpha < 0.f)
					alpha = 0.f;
			}
			if (y > 280)
			{
				alpha = 1.f - (y - 280) * (1.f / 70.f);
				if (alpha > 1.f)
					alpha = 1.f;
				else if (alpha < 0.f)
					alpha = 0.f;
			}
			RenderMessageCard(message, 100.f, fy, alpha, FALSE);
			bottom = height + y;
		}
		y += height + 8;
	}

	float	ft = 60.f / PLATFORM.GetFPS();
	mScroll += ((mScrollTarget - mScroll) * (1.f / 6.f)) * ft;
	if (fabs(mScroll - mScrollTarget) < 1.0)
	{
		mScroll = 0;
		mScrollTarget = 0;
		if (mTop != mSelected)
			mTop = mSelected;
	}

	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	RENDERINFO.Apply();

	// the scroll arrows bob and pulse
	float	pulse = float(cos(PLATFORM.GetSysTimeFloat() * 10.0)) * 0.05f;
	float	bob = float(cos(PLATFORM.GetSysTimeFloat() * 10.0)) * 5.f;
	float	scale = 0.95f - pulse;
	if (more_above)
	{
		float	ay = bob + 102.f;
		CSPRITERENDERER::DrawSprite(320.f, ay, 0.001f, mArrow, SA_CENTRE, SPT_NONE, 1.f, -PI / 2, 0xffffffff, scale * 0.5f, scale, 0.f, 1.f, 0.f, 1.f);
		if (MouseClickedInRect(304.f, ay - 16.f, 336.f, bob + 118.f))
			ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_UP, 1.f);
	}
	if (more_below)
	{
		float	dy = (bottom + 16) - bob;
		CSPRITERENDERER::DrawSprite(320.f, dy, 0.001f, mArrow, SA_CENTRE, SPT_NONE, 1.f, PI / 2, 0xffffffff, scale * 0.5f, scale, 0.f, 1.f, 0.f, 1.f);
		mCanScrollDown = TRUE;
		if (MouseClickedInRect(304.f, dy - 16.f, 336.f, dy + 16.f))
			ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_DOWN, 1.f);
	}

	// the back arrow
	CSPRITERENDERER::DrawSprite(24.f, 454.f, 0.001f, mArrow, SA_CENTRE, SPT_NONE, 1.f, PI, 0xffffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);
	if (MouseClickedInRect(0, 400, 200, 480))
		ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_BACK, 1.f);
	if (MOUSE_BUTTONS[2].mDown)
	{
		MOUSE_BUTTONS[2].mDown = FALSE;
		ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_BACK, 1.f);
	}
	RenderMouseCursor();
}

//*********************************************************************************
// STAND-IN (frame/schedule; review F 2026-10-15; each measured: the plain forms lose 004b9a80). Two things are needed for retail's frame and schedule: the bracket round the floor loop's result (an IL tuple that puts the
// loop in retail's schedule; a `float fl = ...; ly = fl;` temporary does the same) and `lineCount` and `top` as locals of their
// own, so that the first-line position reads copies (readform's by-value copies of n and ty: they change which variables the
// frame slots and registers go to, where reading n and ty directly leaves this, hours, ly and tx in the wrong slots).
int		CMessageLog::RenderMessageCard(CMessage *message, float x, float y, float alpha, BOOL measure)	// FUNCTION: 0x004b9a80
{
	SINT	a = SINT(alpha * 255.f);
	int		width = GAME.GetMessageBox()->mCharsPerLine + 9;
	if (width > 0x55)
		LOG.AddMessage("FATAL ERROR: mnessage log not enough space for message");

	WCHAR	lines[8][0x56];
	message->WordWrap(lines[0], width, 8);

	int		n = 0;
	int i; for (i = 0; i < 8; i ++)
		if (lines[i][0])
			n ++;

	float	h = n * 16.f + 10.f;
	if (h < 74.f)
		h = 74.f;
	if (measure)
		return SINT(h);

	int		height = SINT(h);
	RenderPanel(SINT(x), SINT(y), 0x1b8, height, alpha);

	DWORD	col = (a << 24) + 0xffffff;
	float	px = x + 5.f;
	float	ty = y + 5.f;
	float	py = y + 5.f;

	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
	CSPRITERENDERER::DrawSprite(px, py, 0.002f, mHeadFrameMask, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);

	SetMessageLogRenderStates();
	LT.SRS(D3DRS_ZENABLE, TRUE);
	RENDERINFO.Apply();
	WCHAR		*name = message->mName;
	CMessageBox	*mb = GAME.GetMessageBox();
	CDXTexture	*portrait = mb->GetPortraitTexture(mb->GetPortrait(name));
	CSPRITERENDERER::DrawSprite(px + 8.f, py + 8.f, 0.003f, portrait, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f,
								col, 0.375f, 0.375f, 0.f, 1.f, 0.f, 1.f);
	CSPRITERENDERER::DrawSprite(px, py, 0.001f, mHeadFrame, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, col, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);

	// the time it was shown
	int		hours = SINT(floor(message->mEndTime * (1.f / 3600.f)));
	float	t = message->mEndTime;
	int		minutes = SINT(floor((t - hours * 3600.f) * (1.f / 60.f)));
	char	text[20];
	sprintf(text, "%02d:%02d:%02d", hours, minutes, SINT(message->mEndTime) % 60);
	PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(x + 75.f, y + 30.f, (a << 24) + 0x606060, ToWCHAR(text), 8, 0.002f, 1.f, 1.f);

	float	tx = x + 145.f;
	int		lineCount = n;
	float	top = ty;
	float	ly = (lineCount <= 3) ? top + 24.f - (lineCount * 8 - 8) : top;
	for (i = 0; i < n; i ++)
	{
		ly = (float(floor(ly)));
		PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(tx, i * 16 + ly, col, lines[i], 0, 0, 1.f, 1.f);
	}
	return height;
}

//*********************************************************************************
void	CMessageLog::Activate()	// FUNCTION: 0x004b9ea0
{
	mActive = TRUE;
	mScroll = 0.f;
	mScrollTarget = 0.f;
	mTop = 0;
	mSelected = 0;
}

//*********************************************************************************
// Distance to the selected card, negative when it is above the top. Establish the cursor after the already-at-top
// case; both directions share this query.
static inline float ScrollDistance(CMessageLog *log)
{
	if (log->mSelected == log->mTop)
		return 0.f;
	int c0 = log->mTop;
	float	total = 0.f;
	if (c0 != log->mSelected)
	{
		do
		{
			if (c0 > log->mSelected)
				c0 --;
			total += log->RenderMessageCard(log->mMessages.At(c0), 100.f, 240.f, 255.f, TRUE) + 8;
			if (c0 < log->mSelected)
				c0 ++;
		} while (c0 != log->mSelected);
	}
	return (log->mSelected > log->mTop) ? total : -total;
}

void	CMessageLog::ReceiveButtonAction(CController* from_controller, int button, float val)	// FUNCTION: 0x004b9ec0
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_BACK:
		mActive = FALSE;
		GAME.GetPauseMenu()->Activate(FALSE);
		RelinquishControl();
		CFrontEnd::PlaySound(FES_BACK);
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		if ((mSelected < mMessages.Size() - 1) && mCanScrollDown)
		{
			mSelected ++;
			mScrollTarget = ScrollDistance(this);
			CFrontEnd::PlaySound(FES_MOVE);
		}
		break;

	case BUTTON_FRONTEND_MENU_UP:
		if (mSelected > 0)
		{
			mSelected --;
			mScrollTarget = ScrollDistance(this);
			CFrontEnd::PlaySound(FES_MOVE);
		}
		break;
	}
}

//******************************************************************************************
// The log's overlay states, called without a this (Render, RenderMessageCard). The body is CHud::SetOverlayRenderStates's,
// so the linker folds the two (/OPT:ICF).

// FUNCTION: 0x00482090
void	SetMessageLogRenderStates()
{
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	SetMipFilterNone(0);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ALPHAREF, 8);
	RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, FALSE);
	RENDERINFO.SRS(D3DRS_ZENABLE, FALSE);
	RENDERINFO.Apply();
}

// Out of line: retail folds this `return 5` body into 004dbc20 with CInfantryUnit::GetImposterFrames and
// CRTCutscene::Unknown27, and LINK folds an out-of-line body only with out-of-line ones.
EControlType CMessageLog::GetControlType()
{
	return CONTROL_MESSAGE_LOG;
}
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_MessageLog[8] = { 0 };
