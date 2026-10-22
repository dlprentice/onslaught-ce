// FEPDebriefing.cpp: the page after a level, where each goodie won flies past as a particle effect (reconstructed
// from the retail PC executable; Stuart's file is not in his release).

#include	"Common.h"

#include	"FEPDebriefing.h"
#include	"Frontend.h"
#include	"ParticleManager.h"
#include	"Career.h"
#include	"EndLevelData.h"
#include	"CLIParams.h"
#include	"FEMessBox.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"mouse.h"


// FUNCTION: 0x00456690 init:ZERO_FVECTOR
// FUNCTION: 0x004566b0 init:ID_FMATRIX

//*********************************************************************************
// The links and positions come from the memory type 0x80, beyond Stuart's list (00456798, 004567e8).
#define MEMTYPE_DEBRIEF		((EMemoryType)0x80)

BOOL	CFEPDebriefing::Init()	// FUNCTION: 0x00456780
{
#line 48
	mLinks = new (MEMTYPE_DEBRIEF) CParticleEffectLink[MAX_DEBRIEF_GOODIES];
	mPositions = new (MEMTYPE_DEBRIEF) FVector[MAX_DEBRIEF_GOODIES];
	mFastUntil = 0;
	mNumShown = 0;
	mNextTime = 0;

	return TRUE;
}
// the array's constructor, out of line for the iterator (004567c9)
// FUNCTION: 0x00456830 ??0CParticleEffectLink@@QAE@XZ

//*********************************************************************************
void	CFEPDebriefing::Shutdown()	// FUNCTION: 0x00456850
{
	delete [] mLinks;
	mLinks = NULL;
	delete [] mPositions;
	mPositions = NULL;
}

//*********************************************************************************
void	CFEPDebriefing::ButtonPressed(SINT button, float val)	// FUNCTION: 0x004568a0
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_SELECT:
	case BUTTON_FRONTEND_MENU_BACK:
		if (mFade > 0.5f)
		{
			if (PLAYABLE_DEMO)
				FRONTEND.SetQuit(-1);
			else
				FRONTEND.SetPage(FEP_LEVEL_SELECT, 30);

			FRONTEND.PlaySound(FES_SELECT);

			for (int i = 0; i < MAX_DEBRIEF_GOODIES; i++)
				mLinks[i].Stop(FALSE);
		}
		break;

	case BUTTON_FRONTEND_CHEAT:
		mFastUntil = PLATFORM.GetSysTimeFloat() + 0.1f;
		break;
	};
}

//*********************************************************************************
static int	sAutoTestBoxTime = 20;				// 00629138: frames the -autoconfigtest run leaves a box up

void	CFEPDebriefing::Process(EFEPState state)	// FUNCTION: 0x00456930
{
	if (FEMESSBOX.BeingDisplayed())
	{
		if (CLIPARAMS.mAutoConfigTest)
		{
			if (--sAutoTestBoxTime == 0)
				FEMESSBOX.Kill();
		}
	}
	else if (state == FEPS_ACTIVE)
	{
		if ((CLIPARAMS.mAutoConfigTest) && (mFade > 0.5f))
			ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);

		if (mLinks)
		{
			for (int i = 0; i < mNumGoodies; i++)
			{
				mPositions[i].X += 0.1f;
				mLinks[i].SetPos(mPositions[i]);
			}

			if ((mFirstGoodie) && (mNumShown > 0) && (mPositions[0].X >= 0.45f))
			{
				mFirstGoodie = FALSE;
				FEMESSBOX.Create(320.f, 270.f, 350.f, FO(10), TEXT.GetStringById(0x37416e9), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_OK, 0);
				FEMESSBOX.SetOnDismiss(FEP_NONE, 0);
			}
		}

		CParticleDescriptor	*pd;

		if (PLATFORM.GetSysTimeFloat() < mFastUntil)
			pd = PARTICLE_SET.FindByName("Goodie Effect 2");
		else
			pd = PARTICLE_SET.FindByName("Goodie Effect");
		FVector	pos(-3.f, 0.f, 1.f);

		if ((mNumShown < mNumGoodies) && (PLATFORM.GetSysTimeFloat() > mNextTime))
		{
			mPositions[mNumShown] = pos;
			PARTICLE_MANAGER.CreateEffect(pd, &mLinks[mNumShown], ZERO_FVECTOR, 0, 0);
			mLinks[mNumShown].SetPos(pos);
			mNumShown++;
			mNextTime = PLATFORM.GetSysTimeFloat() + 0.5f;
		}
	}

	if ((mFade == 0) && (mPositions[mNumGoodies - 1].X > 0))
		mFade = 0.001f;

	if (mFade != 0)
	{
		mFade += 0.02f;
		if (mFade > 1.f)
			mFade = 1.f;
	}
}

//*********************************************************************************
void	CFEPDebriefing::RenderPreCommon(float transition, EFrontEndPage dest)	// FUNCTION: 0x00456d40
{
	FED.mCamera.mPos = FVector(0.f, -2.f, 0.f);
	FED.mCamera.mOrientation = ID_FMATRIX;
	FED.mCamera.mZoom = 1.f;
	FRONTEND.UpdateCamera();

	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
void	CFEPDebriefing::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00456dd0
{
	SINT	i;

	RENDERINFO.SetFogEnabled(false);

	// the level select's ring, shrinking back as the page arrives
	if (transition == 1.f)
	{
		FRONTEND.GetCommonPage()->RenderSmallForsetiText(1.f);
		CSPRITERENDERER::DrawColouredSprite(285.f, 225.f, 0.9f, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), 0xffffffff, 1.6f, 1.6f, SA_CENTRE, SPT_NONE, 1.f, 0.f);
	}
	else if (dest != FEP_LEVEL_SELECT)
	{
		float	r = transition * -1.f;
		float	sc = (r + 2.f) * 1.6f;
		CSPRITERENDERER::DrawColouredSprite(285.f, 225.f, 0.9f, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), 0xffffffff, sc, sc, SA_CENTRE, SPT_NONE, 1.f, r + 1.f);
	}

	if (transition == 1.f)
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(1.f);

	if (dest == FEP_LEVEL_SELECT)
		FRONTEND.GetCommonPage()->RenderHelpText(4, transition);
	else
		FRONTEND.GetCommonPage()->RenderHelpText(4, mFade);

	// the level's name, then the mission status and objectives, sliding in from the left
	float	tr = RangeTransition(transition, 0.75f, 1.f);
	float	d = 1.f - tr;
	float	off = d * d * -800.f;
	SINT	alpha = SINT(tr * 255.f);
	WCHAR	*text = FET.GetLevelName(END_LEVEL_DATA.mWorldFinished);

	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(off + 130.f, 149.f, 0.1f, 1.f, 1.f, (alpha << 24) | 0xffffff, text, 1000.f, 0, 0);

	SIZE	size;
	SINT	maxw = -1;
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(FET.GetText(FEX_MISSION_STATUS), &size);
	if (size.cx > maxw)
		maxw = size.cx;
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(FET.GetText(FEX_PRIMARY_OBJECTIVES), &size);
	if (size.cx > maxw)
		maxw = size.cx;
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(FET.GetText(FEX_SECONDARY_OBJECTIVES), &size);
	if (size.cx > maxw)
		maxw = size.cx;
	float	tab = maxw + 20.f;

	WCHAR	buf[256];
	DWORD	col;

	wcscpy(buf, FET.GetText(FEX_MISSION_STATUS));
	if (TEXT.GetLanguage() == 1)
		wcscat(buf, ToWCHAR(" : "));
	else
		wcscat(buf, ToWCHAR(": "));
	DWORD	lcol = (alpha << 24) | 0xffaf3f;
	PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(off + 130.f, 184.f, 0.1f, 1.f, 1.f, lcol, buf, 1000.f, 0, 0);

	switch (END_LEVEL_DATA.mFinalState)
	{
	case GAME_STATE_LEVEL_LOST:
		text = FET.GetText(FEX_DEFEAT);
		col = 0xffff3f1f;
		break;
	case GAME_STATE_LEVEL_WON:
		text = FET.GetText(FEX_VICTORY);
		col = 0xff3fff2f;
		break;
	default:
		text = FET.GetText(FEX_ABORTED);
		col = 0xff3f3f3f;
		break;
	}
	PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(tab + off + 130.f, 184.f, 0.1f, 1.f, 1.f, (col & 0xffffff) | (alpha << 24), text, 1000.f, 0, 0);

	float	y = 210.f;

	// -1 when the level has no objectives of the kind, 1 when all are complete, 0 otherwise
	SINT	primary = -1;
	for (i = 0; i < MAX_PRIMARY_OBJECTIVES; i++)
	{
		int	state = END_LEVEL_DATA.mPrimaryObjectives[i].GetStatus();
		if (state != MOS_NOT_DEFINED)
		{
			if (state == MOS_COMPLETE)
			{
				if (primary == -1)
					primary = state;
			}
			else
				primary = 0;
		}
	}

	if ((END_LEVEL_DATA.mWorldFinished == 500) && (END_LEVEL_DATA.mFinalState == GAME_STATE_LEVEL_WON))
		primary = 1;

	if (primary != -1)
	{
		wcscpy(buf, FET.GetText(FEX_PRIMARY_OBJECTIVES));
		if (TEXT.GetLanguage() == 1)
			wcscat(buf, ToWCHAR(" : "));
		else
			wcscat(buf, ToWCHAR(": "));
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(off + 130.f, y, 0.1f, 1.f, 1.f, lcol, buf, 1000.f, 0, 0);

		switch (primary)
		{
		case 1:
			text = FET.GetText(FEX_COMPLETE);
			col = 0xff3fff2f;
			break;
		default:
			text = FET.GetText(FEX_INCOMPLETE);
			col = 0xffff3f1f;
			break;
		}
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(tab + off + 130.f, y, 0.1f, 1.f, 1.f, (col & 0xffffff) | (alpha << 24), text, 1000.f, 0, 0);
		y += 16.f;
	}

	SINT	secondary = -1;
	for (i = 0; i < MAX_SECONDARY_OBJECTIVES; i++)
	{
		int	state = END_LEVEL_DATA.mSecondaryObjectives[i].GetStatus();
		if (state != MOS_NOT_DEFINED)
		{
			if (state == MOS_COMPLETE)
			{
				if (secondary == -1)
					secondary = state;
			}
			else
				secondary = 0;
		}
	}

	if (secondary != -1)
	{
		wcscpy(buf, FET.GetText(FEX_SECONDARY_OBJECTIVES));
		if (TEXT.GetLanguage() == 1)
			wcscat(buf, ToWCHAR(" : "));
		else
			wcscat(buf, ToWCHAR(": "));
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(off + 130.f, y, 0.1f, 1.f, 1.f, lcol, buf, 1000.f, 0, 0);

		switch (secondary)
		{
		case 1:
			text = FET.GetText(FEX_COMPLETE);
			col = 0xff3fff2f;
			break;
		default:
			text = FET.GetText(FEX_INCOMPLETE);
			col = 0xffff3f1f;
			break;
		}
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(tab + off + 130.f, y, 0.1f, 1.f, 1.f, (col & 0xffffff) | (alpha << 24), text, 1000.f, 0, 0);
	}

	RENDERINFO.SetFogEnabled(false);

	// a won level's grade: the brackets close in, then the letter
	SINT	a;
	float	scale, ang;
	DWORD	white = 0xffffffff;
	if (END_LEVEL_DATA.mFinalState == GAME_STATE_LEVEL_WON)
	{
		a = 255;
		scale = 1.25f;
		ang = 0.f;
		if (transition < 1.f)
		{
			if (dest != FEP_LEVEL_SELECT)
			{
				if (transition < 0.1f)
					a = 0;
				else if (transition < 0.3f)
				{
					float part = RangeTransition(transition, 0.1f, 0.3f);
					a = MakeAlpha(part);
					scale = part;
					ang = -(part * 0.3f);
				}
				else if (transition < 0.5f)
					scale = 1.f;
				else if (transition < 0.7f)
					scale = RangeTransition(transition, 0.5f, 0.7f) * 0.25f + 1.f;
			}
			else
			{
				float	c = 1.f - RangeTransition(transition, 0.75f, 1.f);
				a = MakeAlpha(1.f - c);
				scale = c * 1.4f + 1.25f;
				ang = -c;
			}
		}

		CSPRITERENDERER::DrawColouredSprite(FRONTEND.GetShadowOffsetX() + 325.f, FRONTEND.GetShadowOffsetY() + 320.f, 0.35f, FED.GetTexture(FET_BEA_TITLE_SYMBOL_BRACKET01), BlendAlpha2(0x3f000000, a), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, ang);
		CSPRITERENDERER::DrawColouredSprite(320.f, 310.f, 0.3f, FED.GetTexture(FET_BEA_TITLE_SYMBOL_BRACKET01), BlendAlpha2(white, a), scale, scale, SA_CENTRE, SPT_NONE, 1.f, ang);

		a = 255;
		ang = 0.f;
		if ((transition < 1.f) && (dest != FEP_LEVEL_SELECT) && (!(transition < 0.1f)))
		{
			BOOL	show = TRUE;
			if (transition < 0.3f)
			{
				float part = RangeTransition(transition, 0.1f, 0.3f);
				a = MakeAlpha(part);
				scale = part;
				ang = -(part * 0.3f);
			}
			else if (transition < 0.7f)
				scale = 1.f;
			else if (transition < 0.9f)
			{
				float part = 1.f - RangeTransition(transition, 0.7f, 0.9f);
				a = MakeAlpha(part);
				scale = part;
				ang = -(part * 0.3f);
			}
			else
				show = FALSE;

			if (show)
			{
				CSPRITERENDERER::DrawColouredSprite(FRONTEND.GetShadowOffsetX() + 325.f, FRONTEND.GetShadowOffsetY() + 320.f, 0.35f, FED.GetTexture(FET_BEA_TITLE_SYMBOL_BRACKET02), BlendAlpha2(0x3f000000, a), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, ang);
				CSPRITERENDERER::DrawColouredSprite(320.f, 310.f, 0.3f, FED.GetTexture(FET_BEA_TITLE_SYMBOL_BRACKET02), BlendAlpha2(white, a), scale, scale, SA_CENTRE, SPT_NONE, 1.f, ang);
			}
		}
	}

	if (END_LEVEL_DATA.mFinalState == GAME_STATE_LEVEL_WON)
	{
		text = FET.GetText(FEX_GRADE);
		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(off + 130.f, 295.f, 0.1f, 1.f, 1.f, (a << 24) | 0xffffff, text, 1000.f, 0, 0);

		RENDERINFO.SetFogEnabled(false);
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);

		float		ranking = END_LEVEL_DATA.mRanking;
		WCHAR		grade = CAREER.GetGradeFromRanking(ranking);
		CTEXTURE	*gtex;
		if (grade == ToWCHAR("S")[0])
			gtex = FED.GetTexture(FET_RANKING_S);
		else
			gtex = FED.GetTexture(grade - ToWCHAR("A")[0] + FET_RANKING_A);

		// the letter lands a second after the page opens, then glints every four seconds
		float	t = PLATFORM.GetSysTimeFloat() - mStartTime;
		if ((t > 1.f) && (transition > 0.75f))
		{
			float	s = ((0.5f - t) + 1.f) * 10.f;
			float	flash = -1.f;
			if (s < 1.f)
			{
				flash = 1.f - s;
				s = 1.f;
			}

			SINT	ga = SINT(1.f / s * 255.f);
			CSPRITERENDERER::DrawColouredSprite(323.f, 313.f, 0.05f, gtex, BlendAlpha2(0x3f000000, ga), s, s, SA_CENTRE, SPT_NONE, 1.f, 0.f);

			RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
			RENDERINFO.Apply();
			CSPRITERENDERER::DrawColouredSprite(320.f, 310.f, 0.999f, gtex, BlendAlpha2(white, ga), s, s, SA_CENTRE, SPT_NONE, 1.f, 0.f);
			RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			RENDERINFO.Apply();

			if ((s == 1.f) && (fmodf(t, 4.f) < 1.f))
			{
				RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
				RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
				RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, TRUE);
				RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
				RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
				RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
				RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
				CSPRITERENDERER::DrawColouredSprite(320.f, 310.f, 0.998f, FED.GetTexture(FET_GLARE_ALPHA), 0xffffffff, 1.f, 1.f, SA_CENTRE, SPT_SCROLL_V, t, 0.f);
				RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
				RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			}

			if ((flash < 3.f) && (flash > 0.f))
			{
				s = flash * 2.f;
				SINT	fa = SINT(1.f / (s * 2.f) * 128.f);
				CSPRITERENDERER::DrawColouredSprite(320.f, 310.f, 0.02f, gtex, BlendAlpha2(white, fa), s, s, SA_CENTRE, SPT_NONE, 1.f, 0.f);
			}
		}
	}

	char	debug[256];
	sprintf(debug, "(Debug information: Score: %d, Time : %.2f, Rating: %.2f)", END_LEVEL_DATA.mScore, END_LEVEL_DATA.mTimeTaken, END_LEVEL_DATA.mRanking);
	ToWCHAR(debug);

	if ((transition == 1.f) && (!FEMESSBOX.IsInteractive()))
		MouseDispatchClickInRect(0.f, 0.f, 640.f, 480.f, BUTTON_FRONTEND_MENU_SELECT);

	RENDERINFO.SetFogEnabled(false);
	FRONTEND.DrawTitleBar(FET.GetText(FEX_DEBRIEFING), transition, dest);
}

//*********************************************************************************
void	CFEPDebriefing::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x00457cf0
{
	char	buf[256];

	CAREER.CountGoodies();

	if (PLAYABLE_DEMO)
		mNumGoodies = 0;
	else
		mNumGoodies = CAREER.GetAndResetGoodieNewCount();

	mFirstGoodie = CAREER.GetAndResetFirstGoodie();

	if (mNumGoodies >= MAX_DEBRIEF_GOODIES)
		mNumGoodies = MAX_DEBRIEF_GOODIES - 1;

	if (mNumGoodies > 0)
		mFade = 0;
	else
		mFade = 0.001f;

	mStartTime = PLATFORM.GetSysTimeFloat() + 0.2f;

	sprintf(buf, "(Debug information: Score: %d, Time : %.2f, Rating: %.2f, Ranking %c)\n", END_LEVEL_DATA.mScore, END_LEVEL_DATA.mTimeTaken, END_LEVEL_DATA.mRanking, CAREER.GetGradeFromRanking(END_LEVEL_DATA.mRanking));
	TRACE(buf);
}
