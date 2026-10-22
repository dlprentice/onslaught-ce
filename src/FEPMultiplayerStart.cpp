// FEPMultiplayerStart.cpp: the PC's two-player start page (reconstructed from the retail PC executable; not in
// Stuart's release). Its maths constants lead the object (frontend.h includes mathsconst.h early): Init copies this
// TU's ID_FMATRIX (0089bbf0).

#include	"Common.h"

#include	"FEPMultiplayerStart.h"
#include	"Frontend.h"
#include	"FrontEndThing.h"
#include	"BattleEngineDataManager.h"
#include	"reconnectinterface.h"
#include	"renderinfo.h"
#include	"mesh.h"
#include	"System.h"
#include	"text.h"
#include	"Platform.h"
#include	"mouse.h"
#include	"TransitionHelpers.h"
#include	"SpriteRenderer.h"

// FUNCTION: 0x0051dab0 init:ZERO_FVECTOR
// FUNCTION: 0x0051dad0 init:ID_FMATRIX

//*********************************************************************************
// FUNCTION: 0x0051dba0
BOOL	CFEPMultiplayerStart::Init()
{
	mPort[0] = 0;
	mPort[1] = 1;
	mTime = 0;
	mConfig[0] = 0;
	mConfig[1] = 0;
	mUpFlash[0] = 0;
	mUpFlash[1] = 0;
	mDownFlash[0] = 0;
	mDownFlash[1] = 0;

	if (!PLAYABLE_DEMO)
	{
#line 56
		mThing[0] = new(MEMTYPE_FRONTEND) CFrontEndThing;
		if (mThing[0])
		{
			SFrontEndThingInit	init;
			init.mPos = FVector(0, 0, 0);
			init.mName[0] = 0;
			init.mOri = ID_FMATRIX;
			CMESH	*mesh = FED.GetMesh(FEM_BE_TRANS);
			strcpy(init.mName, mesh ? mesh->mName : "Unknown Mesh");
			mThing[0]->Init(&init);
			mThing[0]->SetAnimation("transform");
		}
	}

	if (!PLAYABLE_DEMO)
	{
#line 73
		mThing[1] = new(MEMTYPE_FRONTEND) CFrontEndThing;
		if (mThing[1])
		{
			SFrontEndThingInit	init;
			init.mPos = FVector(0, 0, 0);
			init.mName[0] = 0;
			init.mOri = ID_FMATRIX;
			CMESH	*mesh = FED.GetMesh(FEM_BE_TRANSM);
			strcpy(init.mName, mesh ? mesh->mName : "Unknown Mesh");
			mThing[1]->Init(&init);
			mThing[1]->SetAnimation("transform");
		}
	}

	return TRUE;
}

//*********************************************************************************
// FUNCTION: 0x0051dd90
void	CFEPMultiplayerStart::Shutdown()
{
	delete mThing[0];
	delete mThing[1];
}

//*********************************************************************************
// FUNCTION: 0x0051ddd0
void	CFEPMultiplayerStart::ChangeConfig(SINT button, SINT player)
{
	if (player == -1)
		return;

	switch (button)
	{
	case BUTTON_FRONTEND_MENU_DOWN:
		if (CFEPBEConfig::GetNumConfigs() > 1)
		{
			if (--mConfig[player] < 0)
				mConfig[player] = CFEPBEConfig::GetNumConfigs() - 1;
			FRONTEND.PlaySound(FES_MOVE);
			mDownFlash[player] = 1.f;
		}
		break;

	case BUTTON_FRONTEND_MENU_UP:
		if (CFEPBEConfig::GetNumConfigs() > 1)
		{
			mConfig[player]++;
			if (mConfig[player] > CFEPBEConfig::GetNumConfigs() - 1)
				mConfig[player] = 0;
			FRONTEND.PlaySound(FES_MOVE);
			mUpFlash[player] = 1.f;
		}
		break;
	}
}

//*********************************************************************************
// FUNCTION: 0x0051de60
void	CFEPMultiplayerStart::ButtonPressed(SINT button, float val)
{
	if (RECONNECT_INTERFACE[1].GetState() != CReconnectInterface::OK)
		return;

	SINT	player = -1;
	if (FRONTEND.mReceivingButtonActionPort == mPort[0])
		player = 0;
	if (FRONTEND.mReceivingButtonActionPort == mPort[1])
		player = 1;
	if (player == -1)
		return;

	switch (button)
	{
	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_MULTIPLAYER, 30);
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		mReady[0] = TRUE;
		mReady[1] = TRUE;
		break;
	}
}

//*********************************************************************************
// FUNCTION: 0x0051ded0
void	CFEPMultiplayerStart::Process(EFEPState state)
{
	mUpFlash[0] -= 0.1f;
	if (mUpFlash[0] < 0.f)
		mUpFlash[0] = 0.f;
	mUpFlash[1] -= 0.1f;
	if (mUpFlash[1] < 0.f)
		mUpFlash[1] = 0.f;
	mDownFlash[0] -= 0.1f;
	if (mDownFlash[0] < 0.f)
		mDownFlash[0] = 0.f;
	mDownFlash[1] -= 0.1f;
	if (mDownFlash[1] < 0.f)
		mDownFlash[1] = 0.f;

	mTime += 0.01f;

	if (mThing[0])
		mThing[0]->Process();
	if (mThing[1])
		mThing[1]->Process();

	if (state == FEPS_ACTIVE)
	{
		if (mReady[0] && mReady[1])
			mCountdown--;

		if (mCountdown <= 0)
		{
			FRONTEND.PlaySound(FES_SELECT);
			FRONTEND.mQuit = FRONTEND.mSelectedLevel;

			GAME.GetFrontEndSettings()->mPlayer1Configuration = mConfig[0];
			GAME.GetFrontEndSettings()->mPlayer2Configuration = mConfig[1];

			char	*name = CFEPBEConfig::GetConfigName(mConfig[0]);
			if (name)
			{
				CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(name);
				if (data)
					strcpy(GAME.GetFrontEndSettings()->mPlayer1CockpitMesh, data->mCockpit);
			}

			name = CFEPBEConfig::GetConfigName(mConfig[1]);
			if (name)
			{
				CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(name);
				if (data)
					strcpy(GAME.GetFrontEndSettings()->mPlayer2CockpitMesh, data->mCockpit);
			}
		}
	}
}

//*********************************************************************************
// FUNCTION: 0x0051e120
void	CFEPMultiplayerStart::RenderPreCommon(float transition, EFrontEndPage dest)
{
	RENDERINFO.SetFogEnabled(false);

	SINT	alpha = SINT(RangeTransition(transition, 0.25f, 1.f) * 255.f);
	FRONTEND.GetCommonPage()->RenderSelectionMarker(200.f, 0.f, transition * 0.25f + 1.f, alpha);
}

//*********************************************************************************
// Each player's panel: a header (green once they are ready), their Battle Engine turning while they choose, the
// configuration's name between arrows that step through it (flashing when it changes), and its three ratings.
// The forms retail's code fixes:
// - The turning Battle Engines: `FMatrix turn; turn.MakeRotationYawF(t); mThing[n]->mOri = turn;`, the form of
//   CFEPBEConfig::Render 004505b0: fcos first, the cosine stored to the first row before fsin (0051eac9), the sine
//   negated on the FPU with fld st(0)/fchs before it is stored (0051eaf2, 0051ec4b).
// - The configuration's name measures its text into its own SIZE: retail's extent is esp+0x28 at 0051efb0, the slot it
//   shares with dir and the camera position's FVector temporary, while the page titles' SIZE has a slot of its own.
// - The unit icons' columns are bracketed sums, `(((i == 0) ? -182.f : 115.f) + 310.f) - 15.f`: VC6 reorders an
//   unbracketed chain's IL, and only the bracketed form leaves `fsub 15` after the depth's push (0051f068, 0051f0b9,
//   0051f10a); the association is the same.
// - The column x is a float kept on the FPU (both constants loaded as floats, fst to esp+0x3c at the join, 0051ed29)
//   that each region stores into a float of its own at its first use (fstp esp+0x10 in the up arrows' block, 0051ed6b;
//   fst at the merge, 0051ee3a); the pushes and x - 32 read that float (0051ed74, 0051ee4d). Below, that float is the
//   `(float)dx` conversion temporary (see dx's STAND-IN note); every float spelling tried folds it into x.
// - Both arrows' flash scales are one variable, scale. C2's stack-slot packer (0x1074b617) then sees one symbol with
//   both blocks' four references, packed before x - 32's temporary into the slot of i, total0 and the fistp QWORD
//   temporaries; its down-arrow part overlaps that temporary, which therefore shares esp+0x14 with cr and float(count)
//   instead (0051ee61, 0051ef7d, 0051f22a), as in retail. Two expressions or two variables leave a 0x168 frame against
//   retail's 0x164.
// Limit: this is shown for valid output only. GetTextExtent leaves the SIZE untouched when it fails (a null text
// returns FALSE), so on that path the column's SIZE is uninitialized here; equivalence on that failure path is not
// claimed.
// FUNCTION: 0x0051e1b0
void	CFEPMultiplayerStart::Render(float transition, EFrontEndPage dest)
{
	if (transition == 1.f)
	{
		FRONTEND.GetCommonPage()->RenderSmallForsetiText(1.f);
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(1.f);
		CSPRITERENDERER::DrawColouredSprite(285.f, 225.f, FO(900), FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), 0xffffffff, 1.6f, 1.6f, SA_CENTRE);
	}

	SINT	alpha = MakeAlpha(RangeTransition(transition, 0.75f, 1.f));
	DWORD	white = 0xffffffff;
	BlendAlpha(white, alpha);

	DWORD	col = mReady[0] ? 0xff3fff3f : 0xff037fa0;
	CSPRITERENDERER::DrawColouredSprite(162 + 5 + FRONTEND.GetShadowOffsetX(), 262 + 5 + FRONTEND.GetShadowOffsetX(), FO(890), FED.GetTexture(FET3_HEADER_BRACKET2), BlendAlpha2(0x3f000000, alpha), 0.7f * 1.05f, 0.7f * 1.05f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(162.f, 262.f, FO(890), FED.GetTexture(FET_BEA_HEADER_BRACKET03), BlendAlpha2(col, alpha), 0.7f, 0.7f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(162.f, 262.f, FO(890), FED.GetTexture(FET3_HEADER_BRACKET2), white, 0.7f, 0.7f, SA_CENTRE);

	col = mReady[1] ? 0xff3fff3f : 0xff037fa0;
	CSPRITERENDERER::DrawColouredSprite(462 + 5 + FRONTEND.GetShadowOffsetX(), 262 + 5 + FRONTEND.GetShadowOffsetX(), FO(890), FED.GetTexture(FET3_HEADER_BRACKET2), BlendAlpha2(0x3f000000, alpha), 0.7f * 1.05f, 0.7f * 1.05f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(462.f, 262.f, FO(890), FED.GetTexture(FET_BEA_HEADER_BRACKET03), BlendAlpha2(col, alpha), 0.7f, 0.7f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(462.f, 262.f, FO(890), FED.GetTexture(FET3_HEADER_BRACKET2), white, 0.7f, 0.7f, SA_CENTRE);

	SIZE	size;
	PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT.GetString(0x700283), &size);
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(162.f - float(size.cx) * 0.5f, 125.f, FO(100), 1.f, 1.f, BlendAlpha2(0xffffffff, alpha), TEXT.GetString(0x700283), 1000.f, 0, 0);
	PLATFORM.Font(FONT_NORMAL)->GetTextExtent(TEXT.GetString(0x70caa4), &size);
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(462.f - float(size.cx) * 0.5f, 125.f, FO(100), 1.f, 1.f, BlendAlpha2(0xffffffff, alpha), TEXT.GetString(0x70caa4), 1000.f, 0, 0);

	// the second player's prompt to join, blinking
	if (mPort[1] == -1)
	{
		if ((SINT(FRONTEND.GetCounter()) % 64) < 50)
		{
			SIZE	size;
			WCHAR	*text = Localization_GetStringById(0x75);
			PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
			PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(462.f - float(size.cx) * 0.5f, 254.f, FO(100), 1.f, 1.f, BlendAlpha2(0xffffffff, alpha), text, 1000.f, 0, 0);
		}
	}

	// the Battle Engines, lit by three lights
	RENDERINFO.SetFogEnabled(false);
	CFEPBEConfig::PushProjection();

	CLight	light;
	FVector	dir(-5000.f, 5000.f, 1500.f);
	dir.Normalise();

	RENDERINFO.SetAmbient(0);
	FVector3 col1(0.8f, 0.4f, 0.2f);
	FVector3 col2(0.2f, 0.4f, 0.8f);
	FVector3 col3(0.4f, 0.4f, 0.4f);
	col1 *= 0.6f;
	col2 *= 0.6f;
	col3 *= 0.6f;
	light.Init(LIGHT_DIRECTIONAL, dir, col1.X, col1.Y, col1.Z);
	RENDERINFO.SetLight(0, &light);
	RENDERINFO.SetLightEnable(0, TRUE);

	dir.X = -dir.X;
	light.Init(LIGHT_DIRECTIONAL, dir, col2.X, col2.Y, col2.Z);
	RENDERINFO.SetLight(1, &light);
	RENDERINFO.SetLightEnable(1, TRUE);

	dir = FVector(0.f, 1000.f, 200.f);
	dir.Normalise();
	light.Init(LIGHT_DIRECTIONAL, dir, col3.X, col3.Y, col3.Z);
	RENDERINFO.SetLight(2, &light);
	RENDERINFO.SetLightEnable(2, TRUE);

	RENDERINFO.EnableLighting(true);
	RENDERINFO.Apply();

	SINT	i;
	for (i = 0; i < 2; i++)
	{
		if (transition != 1.f)
			continue;
		if ((i == 1) && (mPort[1] < 0))
			continue;
		if (mReady[i])
			continue;

		float	yaw = -0.47f;
		if (i == 1)
			yaw = 0.43f;
		FED.mCamera.mOrientation.MakeRotationF(yaw, 0.19f, 0.f);
		FED.mCamera.mPos = FVector(0.f, -7.f, -1.4f);
		FRONTEND.UpdateCamera();

		if ((i == 1) && (FRONTEND.mFEPMultiplayer.mCY > 0))
		{
			if (!mThing[1])
				continue;
			FMatrix	turn;
			turn.MakeRotationYawF(-mTime);
			mThing[1]->mOri = turn;
			mThing[1]->Render();
		}
		else
		{
			if (!mThing[0])
				continue;
			float	t = mTime;
			if (i)
				t = -t;
			FMatrix	turn;
			turn.MakeRotationYawF(t);
			mThing[0]->mOri = turn;
			mThing[0]->Render();
		}
	}

	RENDERINFO.EnableLighting(false);
	RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	CFEPBEConfig::PopProjection();

	for (i = 0; i < 2; i++)
	{
		if ((mPort[i] <= -1) || (mReady[i]))
			continue;

		float	x = (i == 0) ? 162.f : 462.f;
		// STAND-IN: dx is a double only to steer VC6 into retail's per-region float: retail stores x into a separate float
		// at its first use in each region (0051ed6b, 0051ee3a) and the pushes and x - 32 read it, which is what `(float)dx`
		// gives; every float spelling folds that value back into x, and `double x` spills a QWORD where retail's join
		// spill is a DWORD (0051ed29). Stuart's GPL text never uses double, so the type is compiler-specific; dx's value is
		// consumed by every use.
		double	dx;
		float	scale;

		// the arrows, which flash when the configuration changes
		if (((SINT(FRONTEND.GetCounter()) % 64) < 50) || (mUpFlash[i] > 0.f))
		{
			dx = x;
			CSPRITERENDERER::DrawColouredSprite((float)dx, 171.f, FO(100), FED.GetTexture(FET_HELP_ARROW), white, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, -PI / 2);
			DWORD	flash = 0xffffffff;
			BlendAlpha(flash, SINT(mUpFlash[i] * 255.f));
			BlendAlpha(flash, alpha);
			scale = (1.f - mUpFlash[i]) + 1.f;
			CSPRITERENDERER::DrawColouredSprite((float)dx, 171.f, FO(100), FED.GetTexture(FET_HELP_ARROW), flash, scale, scale, SA_CENTRE, SPT_NONE, 1.f, -PI / 2);
		}
		dx = x;
		if (::MouseClickedInRect((float)dx - 32.f, 139.f, (float)dx + 32.f, 203.f))
			ChangeConfig(BUTTON_FRONTEND_MENU_UP, i);

		if (((SINT(FRONTEND.GetCounter()) % 64) < 50) || (mDownFlash[i] > 0.f))
		{
			CSPRITERENDERER::DrawColouredSprite((float)dx, 384.f, FO(100), FED.GetTexture(FET_HELP_ARROW), white, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, PI / 2);
			DWORD	flash = 0xffffffff;
			BlendAlpha(flash, SINT(mDownFlash[i] * 255.f));
			BlendAlpha(flash, alpha);
			scale = (1.f - mDownFlash[i]) + 1.f;
			CSPRITERENDERER::DrawColouredSprite((float)dx, 384.f, FO(100), FED.GetTexture(FET_HELP_ARROW), flash, scale, scale, SA_CENTRE, SPT_NONE, 1.f, PI / 2);
		}
		if (::MouseClickedInRect((float)dx - 32.f, 352.f, (float)dx + 32.f, 416.f))
			ChangeConfig(BUTTON_FRONTEND_MENU_DOWN, i);

		// the configuration's name
		SIZE	size;
		WCHAR	*text = CFEPBEConfig::GetConfigDescription(mConfig[i]);
		PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(((i == 0) ? 162.f : 462.f) - float(size.cx) * 0.5f, 190.f, FO(100), 1.f, 1.f, white, text, 1000.f, 0, 0);

		// its ratings, averaged over its weapons, each headed by its unit
		RENDERINFO.SetFogEnabled(false);
		CSPRITERENDERER::DrawColouredSprite((((i == 0) ? -182.f : 115.f) + 310.f) - 15.f, 320.f, FO(100), FED.GetTexture(FET_I_TROOP), white, 1.f, 1.f);
		CSPRITERENDERER::DrawColouredSprite((((i == 0) ? -182.f : 115.f) + 345.f) - 15.f, 320.f, FO(100), FED.GetTexture(FET_I_TANK), white, 1.f, 1.f);
		CSPRITERENDERER::DrawColouredSprite((((i == 0) ? -182.f : 115.f) + 380.f) - 15.f, 320.f, FO(100), FED.GetTexture(FET_I_PLANE), white, 1.f, 1.f);

		SINT	total0 = 0, total1 = 0, total2 = 0, count = 0;
		SINT	j;
		for (j = 0; j < CFEPBEConfig::GetNumWalkerWeapons(mConfig[i]); j++)
		{
			total0 += CFEPBEConfig::GetWalkerWeaponRating(mConfig[i], j, 0);
			total1 += CFEPBEConfig::GetWalkerWeaponRating(mConfig[i], j, 1);
			total2 += CFEPBEConfig::GetWalkerWeaponRating(mConfig[i], j, 2);
			count++;
		}
		for (j = 0; j < CFEPBEConfig::GetNumJetWeapons(mConfig[i]); j++)
		{
			total0 += CFEPBEConfig::GetJetWeaponRating(mConfig[i], j, 0);
			total1 += CFEPBEConfig::GetJetWeaponRating(mConfig[i], j, 1);
			total2 += CFEPBEConfig::GetJetWeaponRating(mConfig[i], j, 2);
			count++;
		}

		CFEPBEConfig::RenderRatingPips(((i == 0) ? -182.f : 115.f) + 310.f, 360.f, float(total0) / float(count) / 5.0f, white);
		CFEPBEConfig::RenderRatingPips(((i == 0) ? -182.f : 115.f) + 345.f, 360.f, float(total1) / float(count) / 5.0f, white);
		CFEPBEConfig::RenderRatingPips(((i == 0) ? -182.f : 115.f) + 380.f, 360.f, float(total2) / float(count) / 5.0f, white);
	}

	if (mPort[1] != -1)
		RECONNECT_INTERFACE[1].Render(NULL, FRONTEND.GetController(mPort[1]), TRUE);

	FRONTEND.DrawTitleBar(FET.GetText(FEX_BE_CONFIG), transition, dest);
	FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
}

//*********************************************************************************
// FUNCTION: 0x0051f350
void	CFEPMultiplayerStart::TransitionNotification(EFrontEndPage from)
{
	mPort[0] = 0;
	mPort[1] = 1;
	mReady[1] = FALSE;
	mReady[0] = FALSE;
	mPort[0] = FRONTEND.mReceivingButtonActionPort;
	mCountdown = 3;
}

//*********************************************************************************
// FUNCTION: 0x0051f370
int		CFEPMultiplayerStart::GetPlayer1ControllerPort()
{
	return mPort[1];
}
