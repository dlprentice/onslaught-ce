// FEPE3LevelSelect.cpp: the E3 show demo's level select (reconstructed from the retail PC executable; Stuart's file
// is not in his release).

#include	"Common.h"

#include	"FEPE3LevelSelect.h"
#include	"Frontend.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"

// FUNCTION: 0x00459830 init:ZERO_FVECTOR
// FUNCTION: 0x00459850 init:ID_FMATRIX

//*********************************************************************************
CFEPE3LevelSelect::CFEPE3LevelSelect()	// FUNCTION: 0x00459920
{
	int		i, j;

	for (i = 0; i < E3_COLUMNS; i++)
		mColumnLength[i] = 0;

	mColumnLength[0] = 1;
	mColumnLength[1] = 1;
	mColumnLength[2] = 1;
	mColumnLength[3] = 1;

	mLevels[0][0] = 110;
	mLevels[1][0] = 200;
	mLevels[2][0] = 231;
	mLevels[3][0] = 311;

	mNumColumns = 4;

	for (i = 0; i < E3_COLUMNS; i++)
		for (j = 0; j < E3_ROWS; j++)
			mNumLinks[i][j] = 0;
}

//*********************************************************************************
BOOL	CFEPE3LevelSelect::Init()	// FUNCTION: 0x004599a0
{
	mColumn = 0;
	mRow = 0;

	// `goto found` leaves the two-loop search as Stuart's XBoxMemoryCard.cpp:745 does (`goto save_found`); a found flag with a
	// break in each loop is 3 bytes off (the outer loop's closing test comes out inverted), as in
	// CFEPLevelSelect::FindSelectedLevel.
	for (int i = 0; i < E3_COLUMNS; i++)
	{
		for (int j = 0; j < E3_ROWS; j++)
		{
			if (mLevels[i][j] == FRONTEND.mSelectedLevel)
			{
				mColumn = i;
				mRow = j;
				goto found;
			}
		}
	}
found:

	float	slide = floorf(300.f / float(mNumColumns - 1));

	mPos = mPosDest = (float(mColumn) * 100.f) - (float(mColumn) * slide);

	mEpisodeTime = PLATFORM.GetSysTimeFloat();
	mCursorTime = PLATFORM.GetSysTimeFloat();

	return TRUE;
}

//*********************************************************************************
// Both retail level-select tables use 00460490. E3 is a CFrontEndPage sibling,
// not derived from CFEPLevelSelect: it needs its own implementation of this search.
void	CFEPE3LevelSelect::Shutdown()	// FUNCTION: 0x00460490
{
	mColumn = 0;
	mRow = 0;

	for (int i = 0; i < E3_COLUMNS; i++)
	{
		for (int j = 0; j < E3_ROWS; j++)
		{
			if (mLevels[i][j] == FRONTEND.mSelectedLevel)
			{
				mColumn = i;
				mRow = j;
				return;
			}
		}
	}
}

//*********************************************************************************
void	CFEPE3LevelSelect::ActiveNotification(EFrontEndPage from)	// FUNCTION: 0x00459a60
{
	if ((from == FEP_BRIEFING) || (from == FEP_DEBRIEFING))
		mTransition[mColumn][mRow] = 1.f;

	mIdleFrames = 0;
}

//*********************************************************************************
void	CFEPE3LevelSelect::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x00459aa0
{
	mStartTime = PLATFORM.GetSysTimeFloat();

	for (int i = 0; i < E3_COLUMNS; i++)
		for (int j = 0; j < E3_ROWS; j++)
			mTransition[i][j] = 0;

	if ((from == FEP_BRIEFING) || (from == FEP_DEBRIEFING))
		mTransition[mColumn][mRow] = 1.f;
}

//*********************************************************************************
void	CFEPE3LevelSelect::Process(EFEPState state)	// FUNCTION: 0x00459b00
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

	for (SINT i = 0; i < E3_COLUMNS; i++)
	{
		for (SINT j = 0; j < E3_ROWS; j++)
		{
			if ((state == FEPS_ACTIVE) && (i == mColumn) && (j == mRow))
				mTransition[i][j] += 0.1f;
			else
				mTransition[i][j] -= 0.1f;

			if (mTransition[i][j] < 0)
				mTransition[i][j] = 0;
			if (mTransition[i][j] > 1.f)
				mTransition[i][j] = 1.f;
		}
	}

	mIdleFrames++;

	// the show floor: back to the title when nobody plays
	if ((state == FEPS_ACTIVE) && (mIdleFrames > 750))
		FRONTEND.SetPage(FEP_INTRO, 0);
}

//*********************************************************************************
void	CFEPE3LevelSelect::ButtonPressed(SINT button, float val)	// FUNCTION: 0x00459c10
{
	SINT	oldcolumn = mColumn;
	SINT	oldrow = mRow;
	SINT	oldepisode = mLevels[mColumn][mRow] / 100;

	switch (button)
	{
	case BUTTON_FRONTEND_MENU_DOWN:
		mRow --;
		if (mRow < 0)
			mRow = 0;
		FRONTEND.PlaySound(FES_MOVE);
		mIdleFrames = 0;
		break;

	case BUTTON_FRONTEND_MENU_UP:
		mRow ++;
		if (mRow >= mColumnLength[mColumn])
			mRow = mColumnLength[mColumn] - 1;
		FRONTEND.PlaySound(FES_MOVE);
		mIdleFrames = 0;
		break;

	case BUTTON_FRONTEND_MENU_LEFT:
		mColumn --;
		if (mColumn < 0)
			mColumn = 0;
		FRONTEND.PlaySound(FES_MOVE);
		mIdleFrames = 0;
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		mColumn ++;
		if (mColumn >= mNumColumns)
			mColumn = mNumColumns - 1;
		FRONTEND.PlaySound(FES_MOVE);
		mIdleFrames = 0;
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		FRONTEND.SetPage(FEP_BRIEFING, 30);
		FRONTEND.PlaySound(FES_SELECT);
		FRONTEND.mSelectedLevel = mLevels[mColumn][mRow];
		mIdleFrames = 0;
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_INTRO, 0);
		mIdleFrames = 0;
		break;
	};

	if (mRow >= mColumnLength[mColumn])
		mRow = mColumnLength[mColumn] - 1;

	float	slide = floorf(300.f / float(mNumColumns - 1));

	mPosDest = (float(mColumn) * 100.f) - (float(mColumn) * slide);

	if ((mColumn != oldcolumn) || (mRow != oldrow))
		mCursorTime = PLATFORM.GetSysTimeFloat();

	if (mLevels[mColumn][mRow] / 100 != oldepisode)
		mEpisodeTime = PLATFORM.GetSysTimeFloat();
}

//*********************************************************************************
void	CFEPE3LevelSelect::RenderPreCommon(float transition, EFrontEndPage dest)	// FUNCTION: 0x00459e50
{
	if (transition > 0.5f)
	{
		float	tr = RangeTransition(transition, 0.75f, 1.f);
		SINT	alpha = SINT(tr * 255.f);

		FRONTEND.GetCommonPage()->RenderVideoBackground(1.f, (alpha * 0x3f0000) | 0xffffff);
	}
}

// The depth of the page's rings; the others sit just behind (CFEPE3LevelSelect::Render 0045a104, 0045a19e, 0045a4b4:
// 0.155, 0x3e23d70b and 0x3e1cac09, the float sums rather than the nearest floats to 0.16 and 0.153).
#define E3_Z	0.15f

//*********************************************************************************
void	CFEPE3LevelSelect::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00459ee0
{
	SINT	x, y, i;

	float	br;
	if ((dest == FEP_BRIEFING) || (dest == FEP_DEBRIEFING))
		br = 1.f;
	else
		br = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallForsetiText(br);

	float	gx = 148.f - mPos;

	FRONTEND.EnableAdditiveAlpha();
	for (x = 0; x < mNumColumns; x++)
	{
		gx += 100.f;
	}
	FRONTEND.EnableModulateAlpha();

	gx = 148.f - mPos;
	SINT	alpha = SINT(RangeTransition(transition, 0.75f, 1.f) * 255.f);

	for (x = 0; x < mNumColumns; x++)
	{
		float	gy = ((mColumnLength[x] - 1) * 60.f) * 0.5f + 300.f;
		for (y = 0; y < mColumnLength[x]; y++)
		{
			RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
			RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
			RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
			RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
			RENDERINFO.Apply();

			CSPRITERENDERER::DrawColouredSprite(gx, gy, E3_Z + 0.005f, FED.GetTexture(FET_LEVEL_SELECT_MASK), BlendAlpha2(0xffffffff, alpha), 1.f, 1.f, SA_CENTRE);

			RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
			RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
			RENDERINFO.Apply();

			float	spin = (float(y * 7) + x * 11) * 0.114f + FRONTEND.GetCounter() * 0.02f;
			CSPRITERENDERER::DrawColouredSprite(gx, gy, E3_Z + 0.01f, FED.GetTexture(FET_LEVEL_SELECT_GLOW_BLUE), BlendAlpha2(0xffffffff, alpha), 1.f, 1.f, SA_CENTRE, SPT_SCROLL_RIGHT, spin);

			if (mTransition[x][y] > 0.f)
			{
				SINT	a = SINT(RangeTransition(mTransition[x][y], 0.f, 0.5f) * 255.f);
				CSPRITERENDERER::DrawColouredSprite(gx, gy, E3_Z + 0.01f, FED.GetTexture(FET_LEVEL_SELECT_GLOW_ORANGE), BlendAlpha2(0xffffffff, (a * alpha) / 255), 1.f, 1.f, SA_CENTRE, SPT_SCROLL_RIGHT, spin);
			}

			RENDERINFO.SetFogEnabled(false);

			DWORD	ring = 0xffffffff;
			float ang, sc;
			if ((transition != 1.f) && (mColumn == x) && (mRow == y) && ((dest == FEP_BRIEFING) || (dest == FEP_DEBRIEFING)))
			{
				// the chosen level's ring grows into the briefing page's
				if (transition > 0.f)
				{
					float	in = 1.f - transition;
					ang = (PI * 0.67f * transition);
					sc = in * 1.5f + 0.1f;
					float	px = (285.f - gx) * in + gx;
					float	py = (225.f - gy) * in + gy;
					CSPRITERENDERER::DrawColouredSprite(px, py, E3_Z, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), ring, sc, sc, SA_CENTRE, SPT_NONE, 1.f, -ang);
					CSPRITERENDERER::DrawColouredSprite(px, py, E3_Z, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL1), ring, sc * 1.75f, sc * 1.75f, SA_CENTRE, SPT_NONE, 1.f, ang);
				}
			}
			else if (alpha > 0)
			{
				DWORD	col = BlendAlpha2(ring, alpha);
				ang = mTransition[x][y] * (PI / 4);
				sc = mTransition[x][y] * 0.25f;
				sc += 1.f;
				CSPRITERENDERER::DrawColouredSprite(gx, gy, E3_Z, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET02), col, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, ang);
				CSPRITERENDERER::DrawColouredSprite(gx, gy, E3_Z, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET01), col, sc, sc, SA_CENTRE, SPT_NONE, 1.f, -ang);
			}

			// the lines to later levels
			for (i = 0; i < mNumLinks[x][y]; i++)
			{
				SINT lx = mLinks[x][y][i].mColumn;
				SINT ly = mLinks[x][y][i].mRow;
				const float &ex = (lx - x) * 100.f + gx;	// STAND-IN (labelled in review 2026-10-15): const references to temporaries; as plain floats 146/382/1750
				const float &ey = ((mColumnLength[lx] - 1) * 60.f) * 0.5f + 300.f - ly * 60.f;	// STAND-IN (same; see above)
				DWORD	lc = 0xafffffff;
				if (alpha > 0)
					FRONTEND.DrawLine(gx, gy, ex, ey, BlendAlpha2(lc, alpha), 10.f, E3_Z + 0.003f, 0.4f);
			}

			gy -= 60.f;
		}
		gx += 100.f;
	}

	if ((dest == FEP_BRIEFING) || (dest == FEP_DEBRIEFING))
		br = 1.f;
	else
		br = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(br);

	// the selected level's name and episode, and the build line
	SINT	ta = MakeAlpha(transition * 4.f - 2.f);
	float	d = 1.f - transition - 0.25f;
	if (d < 0.f)
		d = 0.f;
	float	off = d * d * 3000.f;
	DWORD	tcol = (ta << 24) | 0x7f7f7f;

	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(125.f - off, 160.f, 0.15f, 0.7f, 1.f, tcol, FET.GetLevelName(mLevels[mColumn][mRow]), PLATFORM.GetSysTimeFloat() - mCursorTime, 0, 0);
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(125.f - off, 130.f, 0.15f, 0.7f, 1.f, tcol, FET.GetEpisodeName(mLevels[mColumn][mRow] / 100), PLATFORM.GetSysTimeFloat() - mEpisodeTime, 0, 0);

	SIZE	size;
	WCHAR	*build = ToWCHAR("E3 2002 Build - Beta work-in-progress");
	PLATFORM.Font(FONT_NORMAL)->GetTextExtent(build, &size);
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(320.f - size.cx * 0.5f, 350.f, 0.15f, 1.f, 1.f, (ta << 24) | 0x7f7f3f, build, PLATFORM.GetSysTimeFloat() - mStartTime, 0, 0);

	FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
	FRONTEND.DrawTitleBar(FET.GetText(FEX_SELECT_LEVEL), transition, dest);
}
