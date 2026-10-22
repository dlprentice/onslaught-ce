// FEPWingmen.cpp: the wingman page (reconstructed from the retail PC executable; not in Stuart's release). Its maths
// constants lead the object (frontend.h includes mathsconst.h early): Init copies this TU's ID_FMATRIX (0089bd40).
// The levels come from the same file and format as the configuration page's, and share its entry type.

#include	"Common.h"

#include	"FEPWingmen.h"
#include	"Frontend.h"
#include	"FrontEndThing.h"
#include	"WorldPhysicsManager.h"
#include	"CLIParams.h"
#include	"mesh.h"
#include	"System.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"text.h"
#include	<stdio.h>

#define MEMTYPE_WINGMEN		((EMemoryType)0x80)

// FUNCTION: 0x00521560 init:ZERO_FVECTOR
// FUNCTION: 0x00521580 init:ID_FMATRIX

//*********************************************************************************
// FUNCTION: 0x00521650
SINT	CFEPWingmen::GetNumWingmen()
{
	SBEConfigLevel	*level = FRONTEND.mFEPWingmen.GetCurrentLevel();
	SINT			count = 0;

	if (level)
	{
		if (level->mTara)
			count = 1;
		if (level->mJason)
			count++;
		if (level->mBilly)
			count++;
	}
	return count;
}

//*********************************************************************************
// FUNCTION: 0x005216c0
BOOL	CFEPWingmen::Init()
{
	mTime = 0;
	mWingman = 0;
	mLeftFlash = 0;
	mRightFlash = 0;
	mUnknown24 = -1000.f;

	if (!PLAYABLE_DEMO)
	{
		SFrontEndThingInit	init;

#line 128
		mThing[0] = new(MEMTYPE_FRONTEND) CFrontEndThing;
		if (mThing[0])
		{
			init.mPos = FVector(0, 0, 0);
			init.mName[0] = 0;
			init.mOri = ID_FMATRIX;
			CMESH	*mesh = FED.GetMesh(FEM_WILDCARD_T);
			strcpy(init.mName, mesh ? mesh->mName : "Unknown Mesh");
			mThing[0]->Init(&init);
		}

#line 139
		mThing[1] = new(MEMTYPE_FRONTEND) CFrontEndThing;
		if (mThing[1])
		{
			init.mPos = FVector(0, 0, 0);
			init.mName[0] = 0;
			init.mOri = ID_FMATRIX;
			CMESH	*mesh = FED.GetMesh(FEM_WILDCARD_B);
			strcpy(init.mName, mesh ? mesh->mName : "Unknown Mesh");
			mThing[1]->Init(&init);
		}

#line 150
		mThing[2] = new(MEMTYPE_FRONTEND) CFrontEndThing;
		if (mThing[2])
		{
			SFrontEndThingInit	init2;
			init2.mPos = FVector(0, 0, 0);
			init2.mName[0] = 0;
			init2.mOri = ID_FMATRIX;
			CMESH	*mesh = FED.GetMesh(FEM_WILDCARD_J);
			strcpy(init2.mName, mesh ? mesh->mName : "Unknown Mesh");
			mThing[2]->Init(&init2);
		}
	}

	CMEMBUFFER	mem;
	char		name[200];
	sprintf(name, "data\\WorldHeaders.dat");

	if (!mem.InitFromFile(name))
	{
		mem.Close();
		return FALSE;
	}

	SINT	version, count;
	mem.Read(&version, 4);
	mem.Read(&count, 4);

	for (SINT i = 0; i < count; i++)
		Load(mem);
	mem.Close();

	return TRUE;
}

//*********************************************************************************
// FUNCTION: 0x00521a60
void	CFEPWingmen::Shutdown()
{
	delete mThing[0];
	delete mThing[1];
	delete mThing[2];

	SBEConfigLevel	*level;
	while (level = mLevels.First())
	{
		mLevels.Remove(level);
		delete level;
	}
}

//*********************************************************************************
// FUNCTION: 0x00521ae0
void	CFEPWingmen::Load(CMEMBUFFER &file)
{
#line 211
	SBEConfigLevel	*level = new(MEMTYPE_WINGMEN) SBEConfigLevel;

	SINT	version, size, count;
	file.Read(&version, 4);
	file.Read(&size, 4);
	file.Read(&level->mLevel, 4);
	file.Read(&count, 4);

	for (SINT i = 0; i < count; i++)
	{
		char	len;
		file.Read(&len, 1);
#line 231
		char	*name = new(MEMTYPE_WINGMEN) char[len + 1];
		file.Read(name, len);
		name[len] = 0;

		char	**entry = new(MEMTYPE_WINGMEN) char *;
		*entry = name;
		level->mConfigs.Append(entry);
	}

	if (version >= 2)
	{
		file.Read(&level->mTara, 4);
		file.Read(&level->mBilly, 4);
		file.Read(&level->mJason, 4);
	}
	else
	{
		level->mTara = TRUE;
		level->mBilly = FALSE;
		level->mJason = FALSE;
	}

	if (version >= 3)
		file.Read(&level->mUnknown10, 4);
	else
		level->mUnknown10 = 0;

	mLevels.Append(level);
}

//*********************************************************************************
// FUNCTION: 0x00521c80
void	CFEPWingmen::Process(EFEPState state)
{
	mTime += 0.01f;
	if (mThing[0])
		mThing[0]->Process();
	if (mThing[1])
		mThing[1]->Process();
	if (mThing[2])
		mThing[2]->Process();

	mLeftFlash -= 0.1f;
	if (mLeftFlash < 0.f)
		mLeftFlash = 0.f;
	mRightFlash -= 0.1f;
	if (mRightFlash < 0.f)
		mRightFlash = 0.f;

	if ((state == FEPS_ACTIVE) && CLIPARAMS.mAutoConfigTest)
		ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
}

//*********************************************************************************
// FUNCTION: 0x00521d20
void	CFEPWingmen::ButtonPressed(SINT button, float val)
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.SetPage(FEP_BE_CONFIG, 25);
		FRONTEND.PlaySound(FES_BACK);
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
	{
		FRONTEND.mQuit = FRONTEND.mSelectedLevel;
		FRONTEND.PlaySound(FES_SELECT);

		SBEConfigLevel	*level = FRONTEND.mFEPWingmen.GetCurrentLevel();
		SINT			n = mWingman;
		if (level)
		{
			if (level->mTara)
			{
				if (n == 0)
				{
					CUnitData	*unit = UPhysicsManager::GetUnitData("Tara Fighter");
					GAME.GetFrontEndSettings()->mWingman = kTaraWingman;
					strcpy(GAME.GetFrontEndSettings()->mWingmanMesh, unit->mUnknown2c);
				}
				n--;
			}
			if (level->mBilly)
			{
				if (n == 0)
				{
					CUnitData	*unit = UPhysicsManager::GetUnitData("Billy Fighter");
					GAME.GetFrontEndSettings()->mWingman = kBillyWingman;
					strcpy(GAME.GetFrontEndSettings()->mWingmanMesh, unit->mUnknown2c);
				}
				n--;
			}
			if (level->mJason)
			{
				if (n == 0)
				{
					CUnitData	*unit = UPhysicsManager::GetUnitData("Jason Fighter");
					GAME.GetFrontEndSettings()->mWingman = kJasonWingman;
					strcpy(GAME.GetFrontEndSettings()->mWingmanMesh, unit->mUnknown2c);
				}
			}
		}
		break;
	}

	case BUTTON_FRONTEND_MENU_LEFT:
		if (GetNumWingmen() > 0)
		{
			if (--mWingman < 0)
				mWingman = GetNumWingmen() - 1;
			FRONTEND.PlaySound(FES_MOVE);
			mLeftFlash = 1.f;
		}
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		if (GetNumWingmen() > 0)
		{
			mWingman++;
			if (mWingman > GetNumWingmen() - 1)
				mWingman = 0;
			FRONTEND.PlaySound(FES_MOVE);
			mRightFlash = 1.f;
		}
		break;
	}
}

//*********************************************************************************
// FUNCTION: 0x00522160
void	CFEPWingmen::RenderPreCommon(float transition, EFrontEndPage dest)
{
	if (transition == 1.f)
		FRONTEND.GetCommonPage()->RenderSelectionMarker(200.f, 0.f, 1.25f, 255);
}

//*********************************************************************************
// The chosen wingman: 0 Tara, 1 Billy, 2 Jason, or -1 (inlined into Render 00522756..0052279c; name ours).
static inline SINT wm_index(SINT n)
{
	SBEConfigLevel	*level = FRONTEND.mFEPWingmen.GetCurrentLevel();
	if (level)
	{
		if (level->mTara)
		{
			if (n == 0)
				return 0;
			n--;
		}
		if (level->mBilly)
		{
			if (n == 0)
				return 1;
			n--;
		}
		if (level->mJason)
		{
			if (n == 0)
				return 2;
		}
	}
	return -1;
}

// The chosen wingman's name (inlined into Render 00522c8f..00522d00; name ours).
static inline WCHAR *wm_name(SINT n)
{
	SBEConfigLevel	*level = FRONTEND.mFEPWingmen.GetCurrentLevel();
	if (level)
	{
		if (level->mTara)
		{
			if (n == 0)
				return TEXT_DB.GetStringById(0x3c345);
			n--;
		}
		if (level->mBilly)
		{
			if (n == 0)
				return TEXT_DB.GetStringById(0xcb1fd);
			n--;
		}
		if (level->mJason)
		{
			if (n == 0)
				return TEXT_DB.GetStringById(0x19f8cf);
		}
	}
	return ToWCHAR("Unknown Wingman");
}

// maths.h's FMatrix::MakeRotationF as a file-local copy, with two products bracketed: see the stand-in note on
// CFEPWingmen::Render.
static inline void MakeRotF(FMatrix &m, float yaw, float pitch, float roll)
{
	float cy = cosf(yaw), sy = sinf(yaw);
	float cr = cosf(roll), sr = sinf(roll);
	float cp = cosf(pitch), sp = sinf(pitch);
	m.Row[0].X = (((cr * cy - sp * sr * sy)));	m.Row[0].Y = (((-cp * sy)));	m.Row[0].Z = ((sp * cr * sy + (sr * cy)));
	m.Row[1].X = ((sp * sr * cy + (cr * sy)));	m.Row[1].Y = (((cp * cy)));		m.Row[1].Z = (sr * sy - sp * cr * cy);
	m.Row[2].X = (-cp * sr);				m.Row[2].Y = sp;			m.Row[2].Z = (cp * cr);
}

// The page's projection for the fighter, restored after (0089bd00; CFEPBEConfig keeps its own).
static D3DMATRIX	sOldProjection;

// STAND-IN (concession): the camera's rotation goes through MakeRotF above, a file-local copy of maths.h's
// MakeRotationF whose Row[0].Z and Row[1].X bracket their unshared products, `(sr * cy)` and `(cr * sy)`.
// Why: retail spills sp * cr and takes sr * cy and cr * sy first in those two sums (0052237b..005223a1), then
// interleaves the position temporary's stores two float stores earlier. VC6 orders each sum's products by rank key,
// and the CSE'd products (sp * cr, sp * sr) take their keys from their ids, handed out in 32-id blocks over the whole
// function: ours are 0x554/0x55f (keys 0x5500/0x57c0) against cr/sr (0x4520/0x4540), so ours takes the shared
// product first. Natural source would need the temporaries' block at 0x4e0 or lower: three blocks fewer after the
// camera, five fewer before it, or 22 more (wrap). Measured by deletion, as diagnostics only: a sprite call, an
// arrow's BlendAlpha or the position assignment is one block; a wingman switch, the name lookup, a Normalise or
// MakeRotationPitchF two. No change to the ids: function-scope thing, icon, text, lx/rx, col, yaw/pitch; a named
// camera angle; `FVector dir = FVector(...)`. BlendAlpha2 for the arrows is one block fewer but changes their code
// (shape 60); Stuart's three colour vectors scaled by 0.6f (FEPGoodies.cpp) change pitch * yaw's scheduling
// (shape 26). How: a bracket is one more IL node, so the bracketed product's key is larger and it sorts first; the
// brackets emit no code. Plain form (the copy with maths.h's body, which compiles exactly as the member call):
// shape 16. Kept file-local because maths.h's MakeRotationF body carries its own bracket stand-in for 13 matches;
// the same brackets there were not tried. FEPWingmen.cpp is not in Stuart's release, so his spelling is unknown.
// FUNCTION: 0x00522190
void	CFEPWingmen::Render(float transition, EFrontEndPage dest)
{
	// the panel slides in from the configuration page
	float	slide = ((-cosf(transition * PI) - 1.f) * 0.5f) * 600.f;
	float	x = 162.f - slide;

	CSPRITERENDERER::DrawColouredSprite(FRONTEND.GetShadowOffsetX() + x + 5.f, FRONTEND.GetShadowOffsetX() + 267.f, 0.95f, FED.GetTexture(FET3_HEADER_BRACKET2), 0x3e000000, 0.7f * 1.05f, 0.7f * 1.05f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(x, 262.f, 0.95f, FED.GetTexture(FET_BEA_HEADER_BRACKET03), 0xff037fa0, 0.7f, 0.7f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(x, 262.f, 0.95f, FED.GetTexture(FET3_HEADER_BRACKET2), 0xffffffff, 0.7f, 0.7f, SA_CENTRE);

	// the wingman's fighter, lit by three lights
	RENDERINFO.GetProjection(&sOldProjection);
	RENDERINFO.SetProjection(1.f, 7000.f, 1.f, 0.75f);

	float	t = 1.f - Clamped(transition, 0.f, 1.f);
	MakeRotF(FED.mCamera.mOrientation, t * t * 3.f + 0.3f, 0.19f, 0.f);	// stand-in: see above
	FED.mCamera.mPos = FVector(0.f, -5.f, -1.6f);
	FRONTEND.UpdateCamera();

	CLight	light;
	FVector	dir(-5000.f, 5000.f, 1500.f);
	dir.Normalise();

	RENDERINFO.SetAmbient(0);
	// Keep the float colour scale; the decimal literals round one step lower.
	light.Init(LIGHT_DIRECTIONAL, dir, 0.8f * 0.6f, 0.4f * 0.6f, 0.2f * 0.6f);
	RENDERINFO.SetLight(0, &light);
	RENDERINFO.SetLightEnable(0, TRUE);

	dir.X = -dir.X;
	light.Init(LIGHT_DIRECTIONAL, dir, 0.2f * 0.6f, 0.4f * 0.6f, 0.8f * 0.6f);
	RENDERINFO.SetLight(1, &light);
	RENDERINFO.SetLightEnable(1, TRUE);

	dir = FVector(0.f, 1000.f, 200.f);
	dir.Normalise();
	light.Init(LIGHT_DIRECTIONAL, dir, 0.4f * 0.6f, 0.4f * 0.6f, 0.4f * 0.6f);
	RENDERINFO.SetLight(2, &light);
	RENDERINFO.SetLightEnable(2, TRUE);

	RENDERINFO.EnableLighting(true);
	RENDERINFO.Apply();

	if (GetNumWingmen() > 0)
	{
		CFrontEndThing	*thing;
		switch (wm_index(mWingman))
		{
		case 0:		thing = mThing[0];	break;
		case 1:		thing = mThing[1];	break;
		case 2:		thing = mThing[2];	break;
		default:	thing = NULL;		break;
		}

		if (thing)
		{
			FMatrix	yaw;
			yaw.MakeRotationYawF(mTime);
			FMatrix	pitch;
			pitch.MakeRotationPitchF(-0.1f);
			// Retail 005228c6..00522945 multiplies pitch rows by yaw columns.
			thing->mOri = pitch * yaw;
			thing->Render();
		}
	}

	RENDERINFO.EnableLighting(false);
	RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	RENDERINFO.mProjection = *(SRenderMatrix *)&sOldProjection;
	RENDERINFO.mProjectionChanged = true;

	// the wingman's ratings against tanks and aircraft
	CSPRITERENDERER::DrawColouredSprite(480.f - slide, 180.f, 0.1f, FED.GetTexture(FET_I_TANK), 0xffffffff, 1.f, 1.f);
	CSPRITERENDERER::DrawColouredSprite(515.f - slide, 180.f, 0.1f, FED.GetTexture(FET_I_PLANE), 0xffffffff, 1.f, 1.f);

	float	tanks, planes;
	float	scale;
	SIZE	size;
	switch (wm_index(mWingman))
	{
	case 0:
		tanks = 0.61f;
		planes = 0.61f;
		break;
	case 1:
		tanks = 1.f;
		planes = 0.21f;
		break;
	case 2:
		tanks = 0.21f;
		planes = 1.f;
		break;
	}
	CFEPBEConfig::RenderRatingPips(495.f - slide, 218.f, tanks, 0xff6fff4f);
	CFEPBEConfig::RenderRatingPips(530.f - slide, 218.f, planes, 0xff4f6fff);

	if (transition == 1.f)
	{
		FRONTEND.GetCommonPage()->RenderSmallForsetiText(1.f);
		CSPRITERENDERER::DrawColouredSprite(285.f, 225.f, 0.9f, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), 0xffffffff, 1.6f, 1.6f, SA_CENTRE);
	}

	if (GetNumWingmen() > 1)
		FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	else
		FRONTEND.GetCommonPage()->RenderHelpText(0, transition);
	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);

	// the wingman's name and portrait, and the arrows when there is a choice
	if (GetNumWingmen() > 0)
	{
		WCHAR	*text = wm_name(mWingman);
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(360.f - size.cx * 0.5f - slide, 150.f, 0.1f, 1.f, 1.f, 0xffffffff, text, 1000.f, 0, 0);

		RENDERINFO.SetFogEnabled(false);

		CTEXTURE	*icon;
		switch (wm_index(mWingman))
		{
		case 0:		icon = FED.GetTexture(FET_I_TARA);		break;
		case 1:		icon = FED.GetTexture(FET_I_BILLY);		break;
		case 2:		icon = FED.GetTexture(FET_I_LORENZO);	break;
		}
		CSPRITERENDERER::DrawColouredSprite(x, 262.f, 0.1f, icon, 0xffffffff, 1.f, 1.f, SA_CENTRE);

		if (GetNumWingmen() > 1)
		{
			float	lx = x - 90.f;
			FRONTEND.ProcessMouseClickInRect(lx - 40.f, 222.f, lx + 40.f, 302.f, BUTTON_FRONTEND_MENU_LEFT);
			float	rx = x + 90.f;
			FRONTEND.ProcessMouseClickInRect(rx - 40.f, 222.f, x + 90.f + 40.f, 302.f, BUTTON_FRONTEND_MENU_RIGHT);

			if (((SINT(FRONTEND.GetCounter()) % 64) < 50) || (mLeftFlash > 0.f))
			{
				CSPRITERENDERER::DrawColouredSprite(lx, 262.f, 0.1f, FED.GetTexture(FET_HELP_ARROW), 0xffffffff, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, -PI);
				DWORD	col = 0xffffffff;
				BlendAlpha(col, SINT(mLeftFlash * 255.f));
				scale = (1.f - mLeftFlash) + 1.f;
				CSPRITERENDERER::DrawColouredSprite(lx, 262.f, 0.1f, FED.GetTexture(FET_HELP_ARROW), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, -PI);
			}
			if (((SINT(FRONTEND.GetCounter()) % 64) < 50) || (mRightFlash > 0.f))
			{
				CSPRITERENDERER::DrawColouredSprite(rx, 262.f, 0.1f, FED.GetTexture(FET_HELP_ARROW), 0xffffffff, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, 0.f);
				DWORD	col = 0xffffffff;
				BlendAlpha(col, SINT(mRightFlash * 255.f));
				scale = (1.f - mRightFlash) + 1.f;
				CSPRITERENDERER::DrawColouredSprite(rx, 262.f, 0.1f, FED.GetTexture(FET_HELP_ARROW), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, 0.f);
			}
		}
	}

	if (transition == 1.f)
	{
		FRONTEND.DrawTitleBar(FET.GetText(FEX_BE_CONFIG), 1.f, dest);
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(1.f);
	}
}

//*********************************************************************************
// FUNCTION: 0x005230c0
void	CFEPWingmen::TransitionNotification(EFrontEndPage from)
{
	mStartTime = PLATFORM.GetSysTimeFloat();
	mWingman = 0;
}

//*********************************************************************************
// FUNCTION: 0x005230e0
// The object's last function in retail: defined last, out of line (its callers call it).
SBEConfigLevel	*CFEPWingmen::GetCurrentLevel()
{
	for (SBEConfigLevel *level = mLevels.First(); level; level = mLevels.Next())
	{
		if (FRONTEND.mSelectedLevel == level->mLevel)
			return level;
	}
	return NULL;
}
