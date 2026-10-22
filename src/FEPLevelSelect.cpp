// FEPLevelSelect.cpp: the level select (reconstructed from the retail PC executable; Stuart's file is not in his
// release). RenderPreCommon folds with CFEPDirectory's (0051ae50).

#include	"Common.h"

#include	"FEPLevelSelect.h"
#include	"Frontend.h"
#include	"Career.h"
#include	"CLIParams.h"
#include	"FEPgoodies.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"

// FUNCTION: 0x00460050 init:ZERO_FVECTOR
// FUNCTION: 0x00460070 init:ID_FMATRIX

//*********************************************************************************
CFEPLevelSelect::CFEPLevelSelect()	// FUNCTION: 0x00460140
{
	int		i, a, b, k;

	for (i = 0; i < LS_COLUMNS; i++)
		mColumnLength[i] = 0;

	// each level ending in 0 or 1 starts an episode's column
	int		column = -1;

	for (i = 0; i < NUM_LEVELS; i++)
	{
		int		level = CAREER.GetLevelStructure()->mData[i][0];

		if (((level % 10) == 0) || ((level % 10) == 1))
			column++;

		mLevels[column][mColumnLength[column]] = level;
		mColumnLength[column]++;
	}

	mNumColumns = column + 1;

	for (a = 0; a < LS_COLUMNS; a++)
		for (b = 0; b < LS_ROWS; b++)
			mNumLinks[a][b] = 0;

	// and the lines from each level to the ones it opens
	for (i = 0; i < NUM_LEVELS; i++)
	{
		int		level = CAREER.GetLevelStructure()->mData[i][0];
		int		c = -1, r = -1;

		for (a = 0; a < LS_COLUMNS; a++)
			for (b = 0; b < LS_ROWS; b++)
				if (mLevels[a][b] == level)
				{
					c = a;
					r = b;
				}

		if ((c != -1) && (r != -1))
		{
			for (k = 1; k <= 2; k++)
			{
				int		next = CAREER.GetLevelStructure()->mData[i][k];

				if (next != -1)
				{
					int		nlevel = CAREER.GetLevelStructure()->mData[next][0];
					int		c2 = -1, r2 = -1;

					for (a = 0; a < LS_COLUMNS; a++)
						for (b = 0; b < LS_ROWS; b++)
							if (mLevels[a][b] == nlevel)
							{
								c2 = a;
								r2 = b;
							}

					if ((c2 != -1) && (r2 != -1))
					{
						mLinks[c][r][mNumLinks[c][r]].mColumn = c2;
						mLinks[c][r][mNumLinks[c][r]].mRow = r2;
						mNumLinks[c][r]++;
					}
				}
			}
		}
	}
}

//*********************************************************************************
inline void	CFEPLevelSelect::FindSelectedLevel()
{
	mColumn = 0;
	mRow = 0;

	// `goto found` leaves the two-loop search as Stuart's XBoxMemoryCard.cpp:745 does (`goto save_found`). Goto-free forms are
	// not better: a found flag with a break in each loop is 3 bytes off (the outer loop's closing test comes out inverted), a
	// flag in the loop conditions 155 and `i = LS_COLUMNS` 160.
	for (int i = 0; i < LS_COLUMNS; i++)
	{
		for (int j = 0; j < LS_ROWS; j++)
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

	float	slide = floorf(360.f / float(mNumColumns - 1));

	mPos = mPosDest = (float(mColumn) * 60.f) - (float(mColumn) * slide);

	mEpisodeTime = PLATFORM.GetSysTimeFloat();
	mCursorTime = PLATFORM.GetSysTimeFloat();
}

void	CFEPLevelSelect::SyncToSelectedLevel()	// FUNCTION: 0x00460310
{
	FindSelectedLevel();
}

//*********************************************************************************
BOOL	CFEPLevelSelect::Init()	// FUNCTION: 0x004603d0
{
	FindSelectedLevel();

	return TRUE;
}

//*********************************************************************************
void	CFEPLevelSelect::Shutdown()	// FUNCTION: 0x00460490
{
	mColumn = 0;
	mRow = 0;

	for (int i = 0; i < LS_COLUMNS; i++)
	{
		for (int j = 0; j < LS_ROWS; j++)
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
void	CFEPLevelSelect::ActiveNotification(EFrontEndPage from)	// FUNCTION: 0x004604f0
{
	if ((from == FEP_BRIEFING) || (from == FEP_DEBRIEFING))
		mTransition[mColumn][mRow] = 1.f;
}

//*********************************************************************************
void	CFEPLevelSelect::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x00460520
{
	mStartTime = PLATFORM.GetSysTimeFloat();

	// (only the first six columns' first three rows)
	for (int i = 0; i < 6; i++)
		for (int j = 0; j < 3; j++)
			mTransition[i][j] = 0;

	if ((from == FEP_BRIEFING) || (from == FEP_DEBRIEFING))
		mTransition[mColumn][mRow] = 1.f;
}

//*********************************************************************************
static int	sAutoTestLevels = 0;				// 006798f8: -autoconfigtest plays two levels

void	CFEPLevelSelect::Process(EFEPState state)	// FUNCTION: 0x00460590
{
	SINT	row = mRow;

	if (row >= mColumnLength[mColumn])
		row = mColumnLength[mColumn] - 1;
	else if (row < 0)
		row = 0;

	for (SINT i = 0; i < LS_COLUMNS; i++)
	{
		for (SINT j = 0; j < LS_ROWS; j++)
		{
			if ((state == FEPS_ACTIVE) && (i == mColumn) && (j == row))
				mTransition[i][j] += 0.1f;
			else
				mTransition[i][j] -= 0.1f;

			if (mTransition[i][j] < 0)
				mTransition[i][j] = 0;
			if (mTransition[i][j] > 1.f)
				mTransition[i][j] = 1.f;
		}
	}

	if ((state == FEPS_ACTIVE) && (CLIPARAMS.mAutoConfigTest))
	{
		if (sAutoTestLevels == 0)
		{
			mRow = 0;
			mColumn = 1;
			ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
		}
		else if (sAutoTestLevels == 1)
		{
			mRow = 0;
			mColumn = 16;
			ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
		}
		sAutoTestLevels++;
	}

	FEMouseEdgeSlide(state, &mPos, 1100.f);
}

//*********************************************************************************
// The grid scrolls a column at a time when the cursor passes these screen positions (00460800, 004608b9).
float	gLevelSelectScrollLeft = 90.f;		// 00629390
float	gLevelSelectScrollRight = 570.f;	// 00629394

void	CFEPLevelSelect::ButtonPressed(SINT button, float val)	// FUNCTION: 0x004606b0
{
	if ((button >= BUTTON_FRONTEND_MENU_UP) && (button <= BUTTON_FRONTEND_MENU_DOWN))
	{
		if (mRow >= mColumnLength[mColumn])
			mRow = mColumnLength[mColumn] - 1;
		else if (mRow < 0)
			mRow = 0;
	}

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
		break;

	case BUTTON_FRONTEND_MENU_UP:
		mRow ++;
		if (mRow >= mColumnLength[mColumn])
			mRow = mColumnLength[mColumn] - 1;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_LEFT:
		// column 16 has four rows; its neighbours have two
		if (mColumn == 16)
		{
			if ((mRow == 2) || (mRow == 3))
				mRow = 1;
			else
				mRow = 0;
		}
		mColumn --;
		if (mColumn < 0)
			mColumn = 0;
		FRONTEND.PlaySound(FES_MOVE);
		if ((mColumn == 16) && (mRow == 1))
			mRow = 2;
		if ((float(mColumn) * 60.f) + (148.f - mPos) < gLevelSelectScrollLeft)
			mPos -= 60.f;
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		if (mColumn == 16)
		{
			if ((mRow == 2) || (mRow == 3))
				mRow = 1;
			else
				mRow = 0;
		}
		mColumn ++;
		if (mColumn >= mNumColumns)
			mColumn = mNumColumns - 1;
		FRONTEND.PlaySound(FES_MOVE);
		if ((mColumn == 16) && (mRow == 1))
			mRow = 2;
		if ((float(mColumn) * 60.f) + (148.f - mPos) > gLevelSelectScrollRight)
			mPos += 60.f;
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		{
			SINT	row = mRow;

			if (row >= mColumnLength[mColumn])
				row = mColumnLength[mColumn] - 1;
			else if (row < 0)
				row = 0;

			if (IsWorldUnlocked(mLevels[mColumn][row]) == TRUE)
			{
				FRONTEND.SetPage(FEP_BRIEFING, 30);
				FRONTEND.PlaySound(FES_SELECT);
				FRONTEND.mSelectedLevel = mLevels[mColumn][row];
			}
		}
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_MAIN, 70);
		break;
	};

	float	slide = floorf(360.f / float(mNumColumns - 1));

	mPosDest = (float(mColumn) * 60.f) - (float(mColumn) * slide);

	if ((mColumn != oldcolumn) || (mRow != oldrow))
		mCursorTime = PLATFORM.GetSysTimeFloat();

	if (mLevels[mColumn][mRow] / 100 != oldepisode)
		mEpisodeTime = PLATFORM.GetSysTimeFloat();
}

//*********************************************************************************
void	CFEPLevelSelect::SetCurrentLevelToHighestAvailable()	// FUNCTION: 0x00460a40
{
	for (SINT c = mNumColumns - 1; c >= 0; c--)
	{
		for (SINT r = mColumnLength[c] - 1; r >= 0; r--)
		{
			if (IsWorldUnlocked(mLevels[c][r]))
			{
				mRow = r;
				mColumn = c;

				float	slide = floorf(360.f / float(mNumColumns - 1));

				mPos = mPosDest = (float(mColumn) * 60.f) - (float(mColumn) * slide);

				mEpisodeTime = PLATFORM.GetSysTimeFloat();
				mCursorTime = PLATFORM.GetSysTimeFloat();

				FRONTEND.mSelectedLevel = mLevels[c][r];
				return;
			}
		}
	}
}

//*********************************************************************************
void	CFEPLevelSelect::RenderPreCommon(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
// Each channel of the colour scaled by a / 256, the alpha kept (name ours).
// The channels are shifted and masked out of the value: retail takes the green one from bh (00460c88), which keeps
// the colour in ebx and leaves edi to the alpha.
static inline DWORD ls_scale(DWORD c, DWORD a)
{
	return (((((((c >> 16) & 0xff) * a) / 256) * 256) + ((((c >> 8) & 0xff) * a) / 256)) * 256) + (((c & 0xff) * a) / 256) + (c & 0xff000000);
}

// The level's grade letter: '-' before it has one, ' ' while it is locked (name ours).
static inline WCHAR ls_grade(CFEPLevelSelect *page, SINT level)
{
	CCareerNode	*node = CAREER.GetNodeFromWorldNo(level);
	if (node && (page->IsWorldUnlocked(level) == TRUE))
	{
		if (node->mRanking != -1.f)
			return CAREER.GetGradeFromRanking(node->mRanking);
		return ToWCHAR("-")[0];
	}
	return ToWCHAR(" ")[0];
}

// The level's ranking, or -1 while it has none or is locked (name ours).
static inline float ls_ranking(CFEPLevelSelect *page, SINT level)
{
	CCareerNode	*node = CAREER.GetNodeFromWorldNo(level);
	if (node && (page->IsWorldUnlocked(level) == TRUE))
		return node->mRanking;
	return -1.f;
}

//*********************************************************************************
void	CFEPLevelSelect::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00460b40
{
	SINT	x, y, i;
	float	yaw;		// both ring blocks' angle (Stuart's goodies page names each one yaw): retail keeps them and the glow's spin in one slot, esp+0x2c
	char	buf[50];
	SIZE	size;

	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	// the episode numbers over the columns where each episode starts
	float	gx = 148.f - mPos;
	float	t4 = transition * 4.f;
	SINT	ea = SINT((t4 - 3.f) * 255.f);
	if (ea < 0)
		ea = 0;
	SINT	row = ClampedRow();

	for (x = 0; (x < mNumColumns) && (gx < 610.f); x++, gx += 60.f)
	{
		if ((!(gx < 0.f)) && ((mLevels[x][0] % 100) == 0))
		{
			DWORD	c = ((mLevels[mColumn][row] / 100) != (mLevels[x][0] / 100)) ? 0xff2f2f2f : 0xff8f8f8f;

			FRONTEND.EnableAdditiveAlpha();
			CSPRITERENDERER::DrawColouredSprite(gx - 10.f, 322.f, 0.955f, FED.GetTexture(FET_SELECT_LEVEL_EPISODE_CURVE),
				ls_scale(c, ea),
				1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, 0.f);
			FRONTEND.EnableModulateAlpha();

			sprintf(buf, "%d", mLevels[x][0] / 100);
			c = 0xff1f4f7f;
			if ((mLevels[mColumn][row] / 100) != (mLevels[x][0] / 100))
				c = 0x3e1f4f7f;
			float	tx = gx + 15.f;
			PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(tx, 179.f, 0.345f, 1.f, 1.f, BlendAlpha2(c, ea), ToWCHAR(buf), 1000.f, 0, 8);
		}
	}
	FRONTEND.EnableModulateAlpha();

	gx = 148.f - mPos;
	SINT	alpha = SINT(RangeTransition(transition, 0.75f, 1.f) * 255.f);

	for (x = 0; x < mNumColumns; x++)
	{
		// the grid scrolls when the cursor's column leaves the middle of the screen
		bool	onscreen = (!(gx < gLevelSelectScrollLeft)) && (!(gx > gLevelSelectScrollRight));
		if ((gx < gLevelSelectScrollLeft) && (mColumn <= x))
		{
			mColumn = x + 1;
			mRow = 0;
			row = ClampedRow();
		}
		else if ((gx > gLevelSelectScrollRight) && (mColumn >= x))
		{
			mColumn = x - 1;
			mRow = 0;
			row = ClampedRow();
		}

		if (!(gx < 610.f))
			break;

		if (!(gx < 0.f))
		{
			float	gy = ((mColumnLength[x] - 1) * 60.f) * 0.5f + 320.f;
			for (y = 0; y < mColumnLength[x]; y++)
			{
				if (IsWorldUnlocked(mLevels[x][y]))
				{
					if (onscreen && (transition == 1.f) && FRONTEND.MouseClickedInRect(gx - 20.f, gy - 20.f, gx + 20.f, gy + 20.f))
					{
						mColumn = x;
						mRow = y;
						row = ClampedRow();
						ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
					}

					RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
					RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
					RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
					RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
					RENDERINFO.Apply();

					// the mask shows the level's grade once it has one
					if (ls_ranking(this, mLevels[x][y]) != -1.f)
					{
						WCHAR		grade;
						CTEXTURE	*mask;

						grade = ls_grade(this, mLevels[x][y]);

						switch ((char)grade)
						{
						case 'S':	mask = FED.GetTexture(FET_RANKING_SMALL_MASK_S);	break;
						case 'A':	mask = FED.GetTexture(FET_RANKING_SMALL_MASK_A);	break;
						case 'B':	mask = FED.GetTexture(FET_RANKING_SMALL_MASK_B);	break;
						case 'C':	mask = FED.GetTexture(FET_RANKING_SMALL_MASK_C);	break;
						case 'D':	mask = FED.GetTexture(FET_RANKING_SMALL_MASK_D);	break;
						default:	mask = FED.GetTexture(FET_RANKING_SMALL_MASK_E);	break;
						}
						CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.31f, mask, BlendAlpha2(0xffffffff, alpha), 1.f, 1.f, SA_CENTRE);
					}
					else
						CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.31f, FED.GetTexture(FET_LEVEL_SELECT_MASK), BlendAlpha2(0xffffffff, alpha), 1.f, 1.f, SA_CENTRE);

					RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
					RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
					RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
					RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
					RENDERINFO.Apply();

					float	spin = (float(x * 11) + y * 7) * 0.114f + FRONTEND.GetCounter() * 0.02f;
					CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.335f, FED.GetTexture(FET_LEVEL_SELECT_GLOW_BLUE), BlendAlpha2(0xffffffff, alpha), 1.f, 1.f, SA_CENTRE, SPT_SCROLL_RIGHT, spin);

					if (mTransition[x][y] > 0.f)
					{
						SINT	a = SINT(RangeTransition(mTransition[x][y], 0.f, 0.5f) * 255.f);
						CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.335f, FED.GetTexture(FET_LEVEL_SELECT_GLOW_ORANGE), BlendAlpha2(0xffffffff, (a * alpha) / 255), 1.f, 1.f, SA_CENTRE, SPT_SCROLL_RIGHT, spin);
					}
				}

				RENDERINFO.SetFogEnabled(false);

				BOOL	unlocked = IsWorldUnlocked(mLevels[x][y]);
				DWORD	ring = unlocked ? 0xffffffff : 0x1fffffff;

				if (onscreen && FRONTEND.GetCursorStateInRect(gx - 20.f, gy - 20.f, gx + 20.f, gy + 20.f) && unlocked)
				{
					mColumn = x;
					mRow = y;
					row = ClampedRow();
				}

				if ((transition != 1.f) && (mColumn == x) && (row == y) && ((dest == FEP_BRIEFING) || (dest == FEP_DEBRIEFING)))
				{
					// the chosen level's ring grows into the briefing page's
					if (transition > 0.f)
					{
						float	in = 1.f - transition;
						yaw = (PI * 0.67f * transition);
						float	sc = in * 1.5f + 0.1f;
						float	px = (285.f - gx) * in + gx;
						float	py = (225.f - gy) * in + gy;
						CSPRITERENDERER::DrawColouredSprite(px, py, 0.305f, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), ring, sc, sc, SA_CENTRE, SPT_NONE, 1.f, -yaw);
						CSPRITERENDERER::DrawColouredSprite(px, py, 0.305f, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL1), ring, sc * 1.75f, sc * 1.75f, SA_CENTRE, SPT_NONE, 1.f, yaw);
					}
				}
				else if (alpha > 0)
				{
					DWORD	col = BlendAlpha2(ring, alpha);
					yaw = mTransition[x][y] * PI * 0.25f;
					float	sc = 1.f + (mTransition[x][y] * 0.25f);
					CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.305f, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET02), col, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, yaw);
					CSPRITERENDERER::DrawColouredSprite(gx, gy, 0.305f, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET01), col, sc, sc, SA_CENTRE, SPT_NONE, 1.f, -yaw);
				}

				// the lines to later levels, bright where both ends are open
				for (i = 0; i < mNumLinks[x][y]; i++)
				{
					SINT	lx = mLinks[x][y][i].mColumn;
					SINT	ly = mLinks[x][y][i].mRow;
					float	ex = (lx - x) * 60.f + gx;
					float	ey = ((mColumnLength[lx] - 1) * 60.f) * 0.5f + 320.f - ly * 60.f;
					BOOL	open = IsWorldUnlocked(mLevels[lx][ly]);
					DWORD	lc = (unlocked && open) ? 0xafffffff : 0x1fffffff;
					if (alpha > 0)
						FRONTEND.DrawLine(gx, gy, ex, ey, BlendAlpha2(lc, alpha), 10.f, 0.308f, 0.4f);
				}

				gy -= 60.f;
			}
		}
		gx += 60.f;
	}

	float	logo;
	if ((dest == FEP_BRIEFING) || (dest == FEP_DEBRIEFING))
		logo = 1.f;
	else
		logo = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(logo);

	// the selected level's name and episode
	SINT	ta = MakeAlpha(t4 - 2.f);
	float	d = 1.f - transition - 0.25f;
	if (d < 0.f)
		d = 0.f;
	float	off = d * d * 3000.f;

	WCHAR	*name;
	if (IsWorldUnlocked(mLevels[mColumn][row]))
		name = FET.GetLevelName(mLevels[mColumn][row]);
	else
		name = FET.GetText(FEX_GOODY_UNKNOWN);
	PLATFORM.Font(FONT_NORMAL)->GetTextExtent(name, &size);
	float	sx = 1.f;
	if (size.cx > 450.f)
		sx = 0.8f;

	float	lx = 130.f - off;
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(lx, 153.f, 0.305f, sx, 1.f, (ta << 24) | 0x7f6f3f, name, PLATFORM.GetSysTimeFloat() - mCursorTime, 0, 0);
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(lx, 125.f, 0.305f, 1.f, 1.f, (ta << 24) | 0x7f7f7f, FET.GetEpisodeName(mLevels[mColumn][row] / 100), PLATFORM.GetSysTimeFloat() - mEpisodeTime, 0, 0);

	FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
	FRONTEND.DrawTitleBar(FET.GetText(FEX_SELECT_LEVEL), transition, dest);
}

//*********************************************************************************
// A level is open once a completed link leads to it.
BOOL	CFEPLevelSelect::IsWorldUnlocked(SINT level)	// FUNCTION: 0x00461a50
{
	if (FRONTEND.mFEPSaveGame.IsCheatActive(1))
		return TRUE;

	if (level == 100)
		return TRUE;

	CCareerNode	*node = CAREER.GetNodeFromWorldNo(level);

	if (node)
	{
		SPtrSet<CCareerNodeLink>	links = node->GetParentLinks();
		CCareerNodeLink				*link;

		FOR_ALL_ITEMS_IN(links, link)
		{
			if (link->mLinkType == CN_COMPLETE)
				return TRUE;
		}
	}

	return FALSE;
}
