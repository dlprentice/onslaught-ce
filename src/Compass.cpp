// Compass.cpp: the HUD's compass, its platform-independent part (00426ff0..00427a8f). Not in Stuart's release:
// reconstructed from retail.

#include "common.h"
#include "DebugLog.h"		// init:ZERO_FVECTOR, init:ID_FMATRIX
#include "Compass.h"
#include "Texture.h"
#include "SpriteRenderer.h"
#include "FastVB.h"
#include "Hud.h"
#include "ltshell.h"
#include "Platform.h"
#include "game.h"
#include "world.h"
#include "eventmanager.h"
#include "BattleEngine.h"
#include "RadarWarningReceiver.h"
#include "player.h"

// FUNCTION: 0x00426ff0 init:ZERO_FVECTOR
// FUNCTION: 0x00427010 init:ID_FMATRIX

// FUNCTION: 0x004270e0
void CCompass::Init()
{
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < COMPASS_MARKERS; i++)
			mMarkers[i][j].mTime = 0;

	PlatformInit();
}

// FUNCTION: 0x00427110
void CCompass::LoadTextures()
{
	mThreatFlash		= CDXTexture::FindTexture("hud\\v2\\ThreatFlash.tga", 0, 0, 1, 1, 1);
	mDamageFlash		= CDXTexture::FindTexture("hud\\v2\\DamageFlash.tga", 0, 0, 1, 1, 1);
	mBarLine			= CDXTexture::FindTexture("hud\\v2\\BarLine.tga", 0, 0, 1, 1, 1);
	mObjectiveMarker	= CDXTexture::FindTexture("hud\\v2\\CompassObjectiveMarker.tga", 0, 0, 1, 1, 1);
}

// FUNCTION: 0x00427190
void CCompass::ReleaseTextures()
{
	SAFE_RELEASE(mThreatFlash);
	SAFE_RELEASE(mDamageFlash);
	SAFE_RELEASE(mBarLine);
	SAFE_RELEASE(mObjectiveMarker);
}

// FUNCTION: 0x00427200
CCompass::CCompass()
{
	mUnknown3c10 = 0;
}

extern UINT	gCompassInnerWidth;							// 0065042c (DXCompass.cpp): the inner ring texture's width

// A threat on the radar warning receiver's list, as the compass reads it (RadarWarningReceiver.cpp's SRadarThreat).
struct SCompassThreat
{
	float	mYaw;				// +0x00
	float	mDistance;			// +0x04
	int		mId;				// +0x08
};

//******************************************************************************************
// The compass's sprites round the ring, batched: incoming threats (a marker per threat, fading over 20 s), recent
// damage (over 2 s), the four bar lines (the life and energy ends and their fixed starts) and the objectives. In split
// screen the ring is smaller and sits in the viewer's half.
// The angle is one variable for all four groups: retail keeps every group's angle and the bar index in the dead
// parameter's home (esp+0x60: 004273f3, 00427522, 004276a9, 004278a8, 00427980), which only a heavy symbol takes
// ahead of the position.
// Cases 2 and 3 share one /360 (00427840 jumps to 00427851) because the source shares it: C2's branch pass
// (0x10735042) merges a block ending in a jump only with the block that falls through into the same label, and the
// allocator's reload of the width on the switch's default edge (retail has it too, 00427867) takes case 3's
// fall-through away before that pass runs, so VC6 cannot have merged two separate divisions here.
// STAND-IN: the shared division is written twice, as the two arms of `if (bar == 2)`. The arms are
// identical, so the test and the second copy emit no code: they survive to the register allocator, and the pass at C2
// 0x10758522 merges the arms and drops the compare after it. In between, the frame packer counts both stores of pos.
// Retail's frame needs that count: retail creates pos's class (esp+0x18, shared with the bars' conversion temporaries)
// before cx's (esp+0x28), and the packer takes 4-byte locals by memory references, the earlier to reach a count first;
// cx has five (the last in the objective loop), pos four with one division (three stores and the read), five with the
// split. The plain form (one division) differs in 18 bytes, all this slot order. Which source Stuart wrote is unknown:
// two separate divisions give the slot order but keep both divisions (shape 16); the width products stored into pos,
// or pos assigned before the division, change nothing.
// The threat fade's two constants are bracketed apart (00427471 multiplies by each) and each
// sprite's height is (cos * radius) * the screen shape, bracketed so VC6 keeps retail's order. The three discarded
// calls before the bars (0042768e..004276a0) are retail's.
// FUNCTION: 0x00427210
void CCompass::Render(CBattleEngine *be)
{
	HUD.SetOverlayRenderStates();
	HUD.SetAdditive();
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);

	float	cx = PLATFORM.GetWindowWidth() * 0.5f;
	float	cy = PLATFORM.GetWindowHeight() * 0.5f;
	int		view = 0;
	float	a;
	if (GAME.IsMultiplayer())
	{
		if (be->GetPlayer()->GetNumber() == 1)
			cy *= 0.5f;
		else
		{
			cy *= 1.5f;
			view = 1;
		}
	}

	CDXSpriteRenderer::mBatch = TRUE;

	CRadarWarningReceiver	*rwr = be->GetRadarWarningReceiver();
	if (rwr)
	{
		int		i;
		for (i = 0; i < COMPASS_MARKERS; i++)
		{
			if (mMarkers[i][view].mTime > 0)
				mMarkers[i][view].mTime -= GAME.GetRenderFrameLength();
		}

		for (SCompassThreat *t = (SCompassThreat *)rwr->mUnknown1C.First(); t; t = (SCompassThreat *)rwr->mUnknown1C.Next())
		{
			BOOL	found = FALSE;
			for (i = 0; i < COMPASS_MARKERS; i++)
			{
				if (mMarkers[i][view].mId == t->mId)
				{
					mMarkers[i][view].mYaw = t->mYaw;
					found = TRUE;
					break;
				}
			}
			if (found)
				continue;
			for (i = 0; i < COMPASS_MARKERS; i++)
			{
				if (!(mMarkers[i][view].mTime > 0))
				{
					mMarkers[i][view].mId = t->mId;
					mMarkers[i][view].mYaw = t->mYaw;
					mMarkers[i][view].mTime = 20.0f;
					break;
				}
			}
		}

		float	yaw = be->GetInterpolatedEulerOrientation().mYaw;
		for (i = 0; i < COMPASS_MARKERS; i++)
		{
			SCompassMarker	*m = &mMarkers[i][view];
			if (m->mTime > 0)
			{
				a = yaw - m->mYaw;
				while (a < 0)
					a += PI_M2;
				while (a > PI_M2)
					a -= PI_M2;
				float	radius = GAME.IsMultiplayer() ? 95.0f : 111.5f;
				CDXSpriteRenderer::DrawSprite((float)sin(a) * radius + cx, cy - ((float)cos(a) * radius) * LT.mScreenShapeScale, 0.001f,
											  mThreatFlash, SA_CENTRE, SPT_NONE, 1.0f, a, 0xff000000 - SINT((m->mTime / 20.0f) * -255.0f) * 0x10101,
											  1.0f, 1.0f, 0, 1.0f, 0, 1.0f);
			}
		}
	}
	FASTVB.Render();

	ListIterator<CDamageFlash>	flashes(be->GetDamageFlashList());
	for (CDamageFlash *d = flashes.First(); d; d = flashes.Next())
	{
		a = d->mYaw;
		while (a < 0)
			a += PI_M2;
		while (a > PI_M2)
			a -= PI_M2;
		float	radius = GAME.IsMultiplayer() ? 80.0f : 96.0f;
		float	t = EVENT_MANAGER.GetTime() - d->mStartTime;
		if (t < 0)
			continue;
		t = (2.0f - t) * 0.5f;
		if (!(t > 0))
			continue;
		CDXSpriteRenderer::DrawSprite((float)sin(a) * radius + cx, cy - ((float)cos(a) * radius) * LT.mScreenShapeScale, 0.001f,
									  mDamageFlash, SA_CENTRE, SPT_NONE, 1.0f, a, 0xff000000 - SINT(t * -255.0f) * 0x10101,
									  1.0f, 1.0f, 0, 1.0f, 0, 1.0f);
	}
	FASTVB.Render();

	float	radius = GAME.IsMultiplayer() ? 100.0f : 110.0f;
	float	life = be->GetLife();
	float	initialLife = be->GetInitialLife();
	float	maxEnergy = be->GetMaxEnergy();
	for (int bar = 0; bar < 4; bar++)
	{
		if ((!HUD.IsComponentVisible(0)) && ((bar == 0) || (bar == 2)))
			continue;
		if ((!HUD.IsComponentVisible(1)) && ((bar == 1) || (bar == 3)))
			continue;

		int		pos;
		UINT	n;
		switch (bar)
		{
		case 0:
			pos = (gCompassInnerWidth * 150) / 360 - SINT(be->GetLife() * ((gCompassInnerWidth * 150) / 360 - (gCompassInnerWidth * 60) / 360) / be->GetInitialLife());
			break;
		case 1:
			pos = SINT(be->GetEnergy() * (gCompassInnerWidth * 360 / 360 - (gCompassInnerWidth * 225) / 360) / be->GetMaxEnergy()) + (gCompassInnerWidth * 225) / 360;
			break;
		// STAND-IN (layout): cases 2 and 3 share one division through `goto divide` into case 3, and the shared tail tests
		// bar although both arms are alike; retail has exactly one division sequence. Written out in each case (no goto) the
		// function is 468 bytes off.
		case 2:
			n = gCompassInnerWidth * 150;
			goto divide;
		case 3:
			n = gCompassInnerWidth * 225;
divide:
			if (bar == 2)
				pos = n / 360;
			else
				pos = n / 360;
			break;
		}
		a = (float)pos / gCompassInnerWidth * PI_M2;
		CDXSpriteRenderer::DrawSprite((float)sin(a) * radius + cx, cy - ((float)cos(a) * radius) * LT.mScreenShapeScale, 0.001f,
									  mBarLine, SA_CENTRE, SPT_NONE, 1.0f, a, 0xff1f1f1f, 1.0f, 1.0f, 0, 1.0f, 0, 1.0f);
	}
	FASTVB.Render();

	HUD.SetAlphaBlend();
	for (CThing *t = WORLD.GetObjectiveThingNB().First(); t; t = WORLD.GetObjectiveThingNB().Next())
	{
		FVector	tp = t->GetRenderPos();
		FVector	d = tp - be->GetRenderPos();
		a = be->GetInterpolatedEulerOrientation().mYaw - d.Azimuth();
		float	r = GAME.IsMultiplayer() ? 82.0f : 98.0f;
		while (a < 0)
			a += PI_M2;
		while (a > PI_M2)
			a -= PI_M2;
		CDXSpriteRenderer::DrawSprite((float)sin(a) * r + cx, cy - ((float)cos(a) * r) * LT.mScreenShapeScale, 0.001f,
									  mObjectiveMarker, SA_CENTRE, SPT_NONE, 1.0f, 0, 0xffffef1f, 1.0f, 1.0f, 0, 1.0f, 0, 1.0f);
	}
	FASTVB.Render();

	CDXSpriteRenderer::mBatch = FALSE;
}

