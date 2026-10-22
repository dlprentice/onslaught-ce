// FEPMultiplayer.cpp: the PC's two-player level grid (reconstructed from the retail PC executable; not in Stuart's
// release). RenderPreCommon folds with CFEPDirectory's (0051ae50).

#include	"Common.h"

#include	"FEPMultiplayer.h"
#include	"Frontend.h"
#include	"Career.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"cliparams.h"

extern int	gControlPreset;						// 00677d70 (the controls page's ApplyControlPreset)
void		ApplyControlPreset(int preset);		// 00453780
bool		AllControlBindingsSet(int index);	// 00456650 (FEPControls.cpp): FALSE while a configurable control has no input

// FUNCTION: 0x0051c980 init:ZERO_FVECTOR
// FUNCTION: 0x0051c9a0 init:ID_FMATRIX

//*********************************************************************************
// The level in each grid cell, row by row (the switch at 0051ca89, table 0051cb68).
static SINT	get_mp_level(SINT n)
{
	SINT	level;

	switch (n)
	{
	case 0:	level = 850; break;
	case 1:	level = 853; break;
	case 2:	level = 859; break;
	case 3:	level = 864; break;
	case 4:	level = 865; break;
	case 5:	level = 866; break;
	case 6:	level = 851; break;
	case 7:	level = 855; break;
	case 8:	level = 860; break;
	case 9:	level = 861; break;
	case 10:	level = 862; break;
	case 11:	level = 863; break;
	case 12:	level = 852; break;
	case 13:	level = 854; break;
	case 14:	level = 857; break;
	default:	level = -2; break;
	};

	return level;
}

//*********************************************************************************
// Put the cursor on the selected level.
inline void	CFEPMultiplayer::FindSelectedLevel()
{
	for (SINT y = 0; y < 3; y++)
	{
		for (SINT x = 0; x < 6; x++)
		{
			if (get_mp_level(y * 6 + x) == FRONTEND.mSelectedLevel)
			{
				mCX = x;
				mCY = y;
				return;
			}
		}
	}

	mCY = 0;
	mCX = 0;
}

//*********************************************************************************
BOOL	CFEPMultiplayer::Init()	// FUNCTION: 0x0051ca70
{
	FindSelectedLevel();

	mPos = 0;
	// the level-select pages' scroll position (FEPE3LevelSelect's column * 100 less column * slide) with the column fixed
	// at 0: retail calls floor(300.0 / 6) = floor(50.0) for the slide and stores the 0 (here and in ButtonPressed).
	// The fixed column is our reading of a formula that folds to 0; `mPosDest = 0;` loses 0051ca70 and 0051ccb0.
	float	slide = float(floor(300.0 / 6));
	mPosDest = (0 * 100.f) - (0 * slide);

	mXTime = PLATFORM.GetSysTimeFloat();
	mYTime = PLATFORM.GetSysTimeFloat();

	return TRUE;
}

//*********************************************************************************
void	CFEPMultiplayer::Shutdown()	// FUNCTION: 0x0051cbb0
{
	FindSelectedLevel();
}

//*********************************************************************************
void	CFEPMultiplayer::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0051ccb0
{
	SINT	oldcx = mCX;
	SINT	oldcy = mCY;

	switch (button)
	{
	case BUTTON_FRONTEND_MENU_LEFT:
		mCX --;
		if (mCX < 0)
			mCX = 0;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		mCX ++;
		if (mCX > 5)
			mCX = 5;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_UP:
		mCY --;
		if (mCY < 0)
			mCY = 0;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		mCY ++;
		if (mCY > 2)
			mCY = 2;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		// the right half needs one of two levels completed
		if ((mCX < 3) || (CAREER.GetNodeFromWorldNo(800)->mComplete) || (CAREER.GetNodeFromWorldNo(741)->mComplete))
		{
			FRONTEND.SetPage(FEP_MULTIPLAYER_START, 30);
			FRONTEND.mSelectedLevel = get_mp_level(FRONTEND.mFEPMultiplayer.mCY * 6 + FRONTEND.mFEPMultiplayer.mCX);
		}
		FRONTEND.PlaySound(FES_SELECT);
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_MAIN, 70);
		break;
	};

	if (mCX != oldcx)
		mXTime = PLATFORM.GetSysTimeFloat();

	// the bottom row has three levels
	if ((mCX > 2) && (mCY == 2))
		mCY = 1;

	if (mCY != oldcy)
		mYTime = PLATFORM.GetSysTimeFloat();

	float	slide = float(floor(300.0 / 6));
	mPosDest = (0 * 100.f) - (0 * slide);
}

//*********************************************************************************
void	CFEPMultiplayer::Process(EFEPState state)	// FUNCTION: 0x0051d020
{
	if (fabsf(mPos - mPosDest) > 1.f)
	{
		float delta = (mPosDest - mPos) / 4.f;

		mPos += delta;
	}
	else
	{
		if (mPos < mPosDest)
			mPos = mPosDest - 1;
		else
			mPos = mPosDest + 1;
	}

	for (SINT x = 0; x < 6; x++)
	{
		for (SINT y = 0; y < 3; y++)
		{
			if ((state == FEPS_ACTIVE) && (x == mCX) && (y == mCY))
				mTransition[x][y] += 0.1f;
			else
				mTransition[x][y] -= 0.1f;

			if (mTransition[x][y] < 0)
				mTransition[x][y] = 0;
			if (mTransition[x][y] > 1.f)
				mTransition[x][y] = 1.f;
		}
	}

	// the second player needs every control bound
	if ((state == FEPS_ACTIVE) && (!AllControlBindingsSet(1)))
	{
		if (gControlPreset == 1)
		{
			ApplyControlPreset(2);
		}
		else
		{
			FEMESSBOX.Kill();
			FEMESSBOX.Create(320.f, 240.f, 400.f, FO(100), Localization_GetStringById(0xd5), PLATFORM.Font(FONT_SMALL), 60, 10, FALSE, FEMB_OPTION_A_CONTINUE, 0);
			FRONTEND.SetPage(FEP_OPTIONS, 40);
		}
	}
}

//*********************************************************************************
void	CFEPMultiplayer::RenderPreCommon(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
// Whether a grid cell's level can be played: past the third column only once level 800 or 741 is complete; in the
// E3 build the third column's lower two only (inlined twice into Render).
static inline BOOL	mp_cell_open(SINT x, SINT y)
{
	if (E3_BUILD && (x == 2))
	{
		if (y)
			return TRUE;
		return FALSE;
	}
	if (x < 3)
		return TRUE;
	if (CAREER.GetNodeFromWorldNo(800)->mComplete)
		return TRUE;
	if (CAREER.GetNodeFromWorldNo(741)->mComplete)
		return TRUE;
	return FALSE;
}

// The wrapped text owns its line buffer; the page keeps the measured extent.
static inline void render_mp_description(CFEPMultiplayer &page, SINT ta, float off, SIZE &size)
{
	SWordWrapFill	fill;
	WCHAR *description = FET.GetMultiplayerLevelDescription(page.mCY);
	SINT	lines = PLATFORM.Font(FONT_SMALL)->WordWrap(&fill, description, 420.f);
	float	ty = 163.f;
	float	reveal = PLATFORM.GetSysTimeFloat() - page.mYTime;
	DWORD	tcol = BlendAlpha2(0xff7f6f5f, ta);
	if (lines > 0)
	{
		// STAND-IN (review F 2026-10-15): the offset through a const reference; `125.f - off` loses CFEPMultiplayer::Render's match.
		const float &storedOffset = off;
		float lx = 125.f - storedOffset;
		for (SINT i = 0; i < lines; i++)
		{
			PLATFORM.Font(FONT_SMALL)->GetTextExtent(fill.c[i], &size);
			PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(lx, ty, 0.01f, 1.f, 1.f, tcol, fill.c[i], reveal * 2.f, 0, 0);
			reveal -= 0.025f;
			ty += 20.f;
		}
	}
}

// FUNCTION: 0x0051d160
void	CFEPMultiplayer::Render(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);
	SIZE size;

	float	gx = 148.f - mPos;
	SINT	alpha = SINT(RangeTransition(transition, 0.75f, 1.f) * 255.f);

	for (SINT x = 0; x < 6; x++)
	{
		float	gy = 260.f;
		for (SINT y = 0; y < 3; y++)
		{
			if ((x > 2) && (y == 2))
				continue;

			if (transition == 1.f)
			{
				if (FRONTEND.GetCursorStateInRect(gx - 20.f, gy - 20.f, gx + 20.f, gy + 20.f))
				{
					mCX = x;
					mCY = y;
				}

				if (mp_cell_open(x, y))
				{
					if (FRONTEND.MouseClickedInRect(gx - 20.f, gy - 20.f, gx + 20.f, gy + 20.f))
					{
						mCX = x;
						mCY = y;
						ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
					}

					RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
					RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
					RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
					RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
					RENDERINFO.Apply();

					CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.31f, FED.GetTexture(FET_LEVEL_SELECT_MASK), BlendAlpha2(0xffffffff, alpha), 1.f, 1.f, SA_CENTRE);

					RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
					RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
					RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
					RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
					RENDERINFO.Apply();

					float	spin = (float(y * 7) + x * 11) * 0.114f + FRONTEND.GetCounter() * 0.02f;
					CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.315f, FED.GetTexture(FET_LEVEL_SELECT_GLOW_BLUE), BlendAlpha2(0xffffffff, alpha), 1.f, 1.f, SA_CENTRE, SPT_SCROLL_RIGHT, spin);

					if (mTransition[x][y] > 0.f)
					{
						SINT	a = SINT(RangeTransition(mTransition[x][y], 0.f, 0.5f) * 255.f);
						CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.315f, FED.GetTexture(FET_LEVEL_SELECT_GLOW_ORANGE), BlendAlpha2(0xffffffff, (a * alpha) / 255), 1.f, 1.f, SA_CENTRE, SPT_SCROLL_RIGHT, spin);
					}
				}
			}

			RENDERINFO.SetFogEnabled(false);

			DWORD	ring;
			float ang;
			if (!mp_cell_open(x, y))
				ring = 0x1fffffff;
			else
				ring = 0xffffffff;
			if ((transition != 1.f) && (mCX == x) && (mCY == y) && (dest == FEP_MULTIPLAYER_START))
			{
				// the chosen level's ring grows into the start page's
				if (transition > 0.f)
				{
					float	in = 1.f - transition;
					ang = (PI * 0.67f * transition);
					float	sc = in * 1.5f + 0.1f;
					float	px = (285.f - gx) * in + gx;
					float	py = (225.f - gy) * in + gy;
					CSPRITERENDERER::DrawColouredSprite(px, py, 0.305f, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), ring, sc, sc, SA_CENTRE, SPT_NONE, 1.f, -ang);
					CSPRITERENDERER::DrawColouredSprite(px, py, 0.305f, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL1), ring, sc * 1.75f, sc * 1.75f, SA_CENTRE, SPT_NONE, 1.f, ang);
				}
			}
			else if (alpha > 0)
			{
				DWORD	col = BlendAlpha2(ring, alpha);
				ang = mTransition[x][y] * (PI / 4);
				float sc = Range(mTransition[x][y], 1.f, 1.25f);
				CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.305f, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET02), col, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, ang);
				CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.305f, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET01), col, sc, sc, SA_CENTRE, SPT_NONE, 1.f, -ang);
			}

			// the link to the next column: 1 closed, 2 open (retail sets 2 at 0051d7c1 and tests the state against 1,
			// 0051d7d0)
			if ((x <= 4) && ((y != 2) || (x < y)))
			{
				SINT	link;
				if (y == 2)
					link = 2;
				else if (x < 2)
					link = 2;
				else if (CAREER.GetNodeFromWorldNo(800)->mComplete || CAREER.GetNodeFromWorldNo(741)->mComplete)
					link = 2;
				else
					link = 1;

				{
					// STAND-IN (review F 2026-10-15): retail materializes the endpoint before testing the line alpha (0051d7d5); a float local loses the match.
					const float &ex = gx + 80.f;
					DWORD	lc = (link == 1) ? 0x1fffffff : 0xafffffff;
					if (alpha > 0)
						FRONTEND.DrawLine(gx, gy, ex, gy, BlendAlpha2(lc, alpha), 10.f, 0.308f, 0.4f);
				}
			}
			gy += 60.f;
		}
		gx += 80.f;
	}

	// the selected level's description
	SINT	ta = MakeAlpha(transition * 4.f - 2.f);
	float	d = 1.f - transition - 0.25f;
	if (d < 0.f)
		d = 0.f;
	float	off = d * d * 3000.f;

	render_mp_description(*this, ta, off, size);
	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
	FRONTEND.DrawTitleBar(FET.GetText(FEX_MULTIPLAYER2), transition, dest);

	float	logo = RangeTransition(transition, 0.75f, 1.f);
	if (dest == FEP_MULTIPLAYER_START)
		logo = 1.f;
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(logo);
}

//*********************************************************************************
void	CFEPMultiplayer::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x0051da60
{
	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;

	for (SINT x = 0; x < 6; x++)
		for (SINT y = 0; y < 3; y++)
			mTransition[x][y] = 0;

	if (from == FEP_MULTIPLAYER_START)
		mTransition[mCX][mCY] = 1.f;
}
