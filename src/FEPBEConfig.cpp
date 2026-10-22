// FEPBEConfig.cpp: the Battle Engine configuration page (reconstructed from the retail PC executable; not in
// Stuart's release). Its maths constants lead the object (frontend.h includes mathsconst.h early): Init copies this
// TU's ID_FMATRIX (00677738). The configuration helpers are statics the two-player and wingmen pages call too.

#include	"Common.h"

#include	"FEPBEConfig.h"
#include	"Frontend.h"
#include	"FrontEndThing.h"
#include	"BattleEngineDataManager.h"
#include	"WorldPhysicsManager.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"CLIParams.h"
#include	"console.h"
#include	"mesh.h"
#include	"System.h"
#include	<stdio.h>

#define MEMTYPE_BECONFIG	((EMemoryType)0x80)

// FUNCTION: 0x0044e9c0 init:ZERO_FVECTOR
// FUNCTION: 0x0044e9e0 init:ID_FMATRIX

// Render calls these two out of line (00450f1d, 00450da1).
// FUNCTION: 0x00451930 ?GetConfiguration@UBattleEngineDataManager@@SAPAVCBattleEngineData@@PAD@Z

//*********************************************************************************

//*********************************************************************************
// FUNCTION: 0x0044eab0
char	*CFEPBEConfig::GetConfigName(SINT n)
{
	SBEConfigLevel	*level = FRONTEND.mFEPBEConfig.GetCurrentLevel();
	if (level)
	{
		SINT	i = 0;
		for (char **name = level->mConfigs.First(); name; name = level->mConfigs.Next())
		{
			if (i == n)
				return *name;
			i++;
		}
	}
	return NULL;
}

//*********************************************************************************
// FUNCTION: 0x0044eb30
WCHAR	*CFEPBEConfig::GetConfigDescription(SINT n)
{
	char	*name = GetConfigName(n);
	if (name)
	{
		CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(name);
		if (data)
		{
			switch (data->mLanguageName)
			{
			case 1:	return TEXT.GetStringById(0x97d47d6b);
			case 2:	return TEXT.GetStringById(0xe3fcbf7c);
			case 3:	return TEXT.GetStringById(0x3025018d);
			case 4:	return TEXT.GetStringById(0x7c4d439e);
			case 5:	return TEXT.GetStringById(0xc87585af);
			}
		}
	}
	return ToWCHAR("Unknown Configuration");
}

//*********************************************************************************
// FUNCTION: 0x0044ecf0
SINT	CFEPBEConfig::GetNumConfigs()
{
	SBEConfigLevel	*level = FRONTEND.mFEPBEConfig.GetCurrentLevel();
	if (level)
		return level->mConfigs.Size();
	return 0;
}

//*********************************************************************************
// FUNCTION: 0x0044ed40
SINT	CFEPBEConfig::GetNumJetWeapons(SINT n)
{
	if (FRONTEND.mFEPBEConfig.GetCurrentLevel())
	{
		char	*name = GetConfigName(n);
		if (name)
		{
			CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(name);
			if (data)
				return data->mJetWeapons.Size();
		}
	}
	return 0;
}

//*********************************************************************************
// FUNCTION: 0x0044eea0
SINT	CFEPBEConfig::GetNumWalkerWeapons(SINT n)
{
	SINT	count = 0;

	if (FRONTEND.mFEPBEConfig.GetCurrentLevel())
	{
		char	*name = GetConfigName(n);
		if (name)
		{
			CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(name);
			if (data)
			{
				if (data->mPrimaryWeapon)
					count = 1;
				count += data->mWalkerWeapons.Size();
			}
		}
	}
	return count;
}

//*********************************************************************************
// FUNCTION: 0x0044f030
SINT	CFEPBEConfig::GetWalkerWeaponRating(SINT n, SINT weapon, SINT rating)
{
	if (FRONTEND.mFEPBEConfig.GetCurrentLevel())
	{
		char	*config = GetConfigName(n);
		if (config)
		{
			CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(config);
			if (data)
			{
				if (data->mPrimaryWeapon)
				{
					if (weapon == 0)
					{
						CWeaponData	*w = UPhysicsManager::GetWeaponData(data->mPrimaryWeapon);
						if (w)
						{
							switch (rating)
							{
							case 0:	return w->mRating[0];
							case 1:	return w->mRating[1];
							case 2:	return w->mRating[2];
							}
						}
						return 0;
					}
					weapon--;
				}
				SINT	i = 0;
				for (char **name = data->mWalkerWeapons.First(); name; name = data->mWalkerWeapons.Next())
				{
					if (i == weapon)
					{
						CWeaponData	*w = UPhysicsManager::GetWeaponData(*name);
						if (w)
						{
							switch (rating)
							{
							case 0:	return w->mRating[0];
							case 1:	return w->mRating[1];
							case 2:	return w->mRating[2];
							}
						}
						return 0;
					}
					i++;
				}
			}
		}
	}
	return 0;
}

//*********************************************************************************
// FUNCTION: 0x0044f300
SINT	CFEPBEConfig::GetJetWeaponRating(SINT n, SINT weapon, SINT rating)
{
	if (FRONTEND.mFEPBEConfig.GetCurrentLevel())
	{
		char	*config = GetConfigName(n);
		if (config)
		{
			CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(config);
			if (data)
			{
				SINT	i = 0;
				for (char **name = data->mJetWeapons.First(); name; name = data->mJetWeapons.Next())
				{
					if (i == weapon)
					{
						CWeaponData	*w = UPhysicsManager::GetWeaponData(*name);
						if (w)
						{
							switch (rating)
							{
							case 0:	return w->mRating[0];
							case 1:	return w->mRating[1];
							case 2:	return w->mRating[2];
							}
						}
						return 0;
					}
					i++;
				}
			}
		}
	}
	return 0;
}

//*********************************************************************************
// FUNCTION: 0x0044f530
WCHAR	*CFEPBEConfig::GetWalkerWeaponName(SINT n, SINT weapon)
{
	if (FRONTEND.mFEPBEConfig.GetCurrentLevel())
	{
		char	*config = GetConfigName(n);
		if (config)
		{
			CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(config);
			if (data)
			{
				if (data->mPrimaryWeapon)
				{
					if (weapon == 0)
					{
						CWeaponData	*w = UPhysicsManager::GetWeaponData(data->mPrimaryWeapon);
						if (w)
							return TEXT.GetStringById(w->mLanguageName);
						return ToWCHAR("Unknown Weapon");
					}
					weapon--;
				}
				SINT	i = 0;
				for (char **name = data->mWalkerWeapons.First(); name; name = data->mWalkerWeapons.Next())
				{
					if (i == weapon)
					{
						CWeaponData	*w = UPhysicsManager::GetWeaponData(*name);
						if (w)
							return TEXT.GetStringById(w->mLanguageName);
						return ToWCHAR("Unknown Weapon");
					}
					i++;
				}
			}
		}
	}
	return ToWCHAR("Unknown Weapon");
}

//*********************************************************************************
// FUNCTION: 0x0044f830
WCHAR	*CFEPBEConfig::GetJetWeaponName(SINT n, SINT weapon)
{
	if (FRONTEND.mFEPBEConfig.GetCurrentLevel())
	{
		char	*config = GetConfigName(n);
		if (config)
		{
			CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(config);
			if (data)
			{
				SINT	i = 0;
				for (char **name = data->mJetWeapons.First(); name; name = data->mJetWeapons.Next())
				{
					if (i == weapon)
					{
						CWeaponData	*w = UPhysicsManager::GetWeaponData(*name);
						if (w)
							return TEXT.GetStringById(w->mLanguageName);
						return ToWCHAR("Unknown Weapon");
					}
					i++;
				}
			}
		}
	}
	return ToWCHAR("Unknown Weapon");
}

//*********************************************************************************
// FUNCTION: 0x0044fa90
BOOL	CFEPBEConfig::Init()
{
	TRACE("beconf::init() 0\n");

	mChangeTime = -1000.f;
	mTime = 0;
	mConfig = 0;
	mUpFlash = 0;
	mDownFlash = 0;

	if (!PLAYABLE_DEMO)
	{
#line 320
		mThing = new(MEMTYPE_FRONTEND) CFrontEndThing;
		if (mThing)
		{
			SFrontEndThingInit	init;
			init.mPos = FVector(0, 0, 0);
			init.mName[0] = 0;
			init.mOri = ID_FMATRIX;
			CMESH	*mesh = FED.GetMesh(FEM_BE_TRANS);
			strcpy(init.mName, mesh ? mesh->mName : "Unknown Mesh");
			mThing->Init(&init);
			mThing->SetAnimation("transform");
		}
	}

	CMEMBUFFER	mem;
	char		name[200];
	sprintf(name, "data\\WorldHeaders.dat");

	TRACE("beconf::init() 3\n");
	if (!mem.InitFromFile(name))
	{
		TRACE("beconf::init() 1\n");
		CONSOLE.FatalFileError("data\\worldheaders.dat", -1);
		mem.Close();
		return FALSE;
	}

	TRACE("beconf::init() 2\n");
	SINT	version, count;
	mem.Read(&version, 4);
	mem.Read(&count, 4);

	TRACE("beconf::init() 4\n");
	for (SINT i = 0; i < count; i++)
	{
		TRACE("header %d\n", i);
		Load(mem);
	}
	mem.Close();

	TRACE("beconf::init() 5\n");
	return TRUE;
}

//*********************************************************************************
// FUNCTION: 0x0044fda0
void	CFEPBEConfig::Shutdown()
{
	delete mThing;

	SBEConfigLevel	*level;
	while (level = mLevels.First())
	{
		mLevels.Remove(level);
		delete level;
	}
}

//*********************************************************************************
// FUNCTION: 0x0044fdf0
SBEConfigLevel::~SBEConfigLevel()
{
	char	**name;
	while (name = mConfigs.First())
	{
		mConfigs.Remove(name);
		delete *name;
		delete name;
	}
}

//*********************************************************************************
// FUNCTION: 0x0044fe70
void	CFEPBEConfig::Load(CMEMBUFFER &file)
{
#line 399
	SBEConfigLevel	*level = new(MEMTYPE_BECONFIG) SBEConfigLevel;

	SINT	version, size, count;
	file.Read(&version, 4);
	file.Read(&size, 4);
	file.Read(&level->mLevel, 4);
	file.Read(&count, 4);

	for (SINT i = 0; i < count; i++)
	{
		char	len;
		file.Read(&len, 1);
#line 419
		char	*name = new(MEMTYPE_BECONFIG) char[len + 1];
		file.Read(name, len);
		name[len] = 0;

		char	**entry = new(MEMTYPE_BECONFIG) char *;
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
// FUNCTION: 0x00450010
void	CFEPBEConfig::Process(EFEPState state)
{
	if ((state == FEPS_ACTIVE) && CLIPARAMS.mAutoConfigTest)
		ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);

	mTime += 0.01f;
	if (mThing)
		mThing->Process();

	mUpFlash -= 0.1f;
	if (mUpFlash < 0.f)
		mUpFlash = 0.f;
	mDownFlash -= 0.1f;
	if (mDownFlash < 0.f)
		mDownFlash = 0.f;
}

//*********************************************************************************
// FUNCTION: 0x00450090
void	CFEPBEConfig::ButtonPressed(SINT button, float val)
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.SetPage(FEP_BRIEFING, 25);
		FRONTEND.PlaySound(FES_BACK);
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
	{
		GAME.GetFrontEndSettings()->mPlayer1Configuration = mConfig;

		char	*name = GetConfigName(mConfig);
		if (name)
		{
			CBattleEngineData	*data = UBattleEngineDataManager::GetConfiguration(name);
			if (data)
				strcpy(GAME.GetFrontEndSettings()->mPlayer1CockpitMesh, data->mCockpit);
		}

		if ((FRONTEND.mSelectedLevel >= 231) && CFEPWingmen::GetNumWingmen())
		{
			FRONTEND.SetPage(FEP_WINGMEN, 25);
			FRONTEND.PlaySound(FES_SELECT);
		}
		else
		{
			FRONTEND.mQuit = FRONTEND.mSelectedLevel;
			FRONTEND.PlaySound(FES_SELECT);
		}
		break;
	}

	case BUTTON_FRONTEND_MENU_DOWN:
		if (GetNumConfigs() > 1)
		{
			if (--mConfig < 0)
				mConfig = GetNumConfigs() - 1;
			FRONTEND.PlaySound(FES_MOVE);
			mDownFlash = 1.f;
			mChangeTime = PLATFORM.GetSysTimeFloat();
		}
		break;

	case BUTTON_FRONTEND_MENU_UP:
		if (GetNumConfigs() > 1)
		{
			mConfig++;
			if (mConfig > GetNumConfigs() - 1)
				mConfig = 0;
			FRONTEND.PlaySound(FES_MOVE);
			mUpFlash = 1.f;
			mChangeTime = PLATFORM.GetSysTimeFloat();
		}
		break;
	}
}

//*********************************************************************************
// FUNCTION: 0x00450390
void	CFEPBEConfig::RenderPreCommon(float transition, EFrontEndPage dest)
{
	if (dest == FEP_WINGMEN)
		FRONTEND.GetCommonPage()->RenderSelectionMarker((cosf(transition * PI) + 1.f) * 100.f, 0.f, 1.25f, 255);
	else if (transition == 1.f)
		FRONTEND.GetCommonPage()->RenderSelectionMarker(0.f, 0.f, 1.25f, 255);
}

//*********************************************************************************
static D3DMATRIX	sOldProjection;				// 006776f8

// FUNCTION: 0x00450400
void	CFEPBEConfig::PushProjection()
{
	RENDERINFO.GetProjection(&sOldProjection);
	RENDERINFO.SetProjection(1.f, 7000.f, 1.f, 0.75f);
}

// FUNCTION: 0x00450440
void	CFEPBEConfig::PopProjection()
{
	RENDERINFO.mProjection = *(SRenderMatrix *)&sOldProjection;
	RENDERINFO.mProjectionChanged = true;
}

//*********************************************************************************
// FUNCTION: 0x00450460
void	CFEPBEConfig::RenderRatingPips(float x, float y, float value, DWORD colour)
{
	SINT	n = SINT(value * 5.01f);
	SINT	alpha = colour >> 24;

	x -= (float(n - 1) * 6.f) * 0.5f;

	switch (n)
	{
	case 1:	colour = 0xffff6f4f; break;
	case 2:
	case 3:	colour = 0xff4f6fff; break;
	case 4:
	case 5:	colour = 0xff6fff5f; break;
	}
	colour = BlendAlpha2(colour, alpha);

	for (SINT i = 0; i < n; i++)
	{
		CSPRITERENDERER::DrawColouredSprite(x + 2.f, y + 2.f, FO(100), FED.GetTexture(FET_SMALL_STAR), BlendAlpha2(0x7f000000, alpha), 0.7f, 0.7f, SA_CENTRE);
		CSPRITERENDERER::DrawColouredSprite(x, y, FO(100), FED.GetTexture(FET_SMALL_STAR), colour, 0.7f, 0.7f, SA_CENTRE);
		x += 6.f;
	}
}

//*********************************************************************************
// STAND-IN: the up arrow reads GetCounter() and the down arrow reads mCounter: an inline-budget fit, not
// read from retail's code. Retail calls GetNumWalkerWeapons' GetConfigName (00450fb1) where one offered site fewer
// after it would expand it; the supplying accessor is unknown (Stuart's files use GetCounter()). Plain direct reads expand that call; replace only with a full sibling-preserving match.
// FUNCTION: 0x004505b0
void	CFEPBEConfig::Render(float transition, EFrontEndPage dest)
{
	// the panel slides in from the left, and out to the right towards the wingman page
	float	slide = ((-cosf(transition * PI) - 1.f) * 0.5f) * 600.f;
	float	offset = (dest == FEP_WINGMEN) ? -slide : slide;

	float	x = 162.f - offset;

	CSPRITERENDERER::DrawColouredSprite(x + 5 + FRONTEND.GetShadowOffsetX(), 262 + 5 + FRONTEND.GetShadowOffsetX(), FO(890), FED.GetTexture(FET3_HEADER_BRACKET2), 0x3e000000, 0.7f * 1.05f, 0.7f * 1.05f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(x, 262.f, FO(890), FED.GetTexture(FET_BEA_HEADER_BRACKET03), 0xff037fa0, 0.7f, 0.7f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(x, 262.f, FO(890), FED.GetTexture(FET3_HEADER_BRACKET2), 0xffffffff, 0.7f, 0.7f, SA_CENTRE);

	// the Battle Engine, lit by three lights
	PushProjection();

	float	t = Clamped(transition, 0.f, 1.f);
	float	yaw = (1.f - t) * (1.f - t) * 3.f;
	if (dest == FEP_WINGMEN)
		yaw = -yaw;
	FED.mCamera.mOrientation.MakeRotationF(yaw - 0.47f, 0.19f, 0.f);
	FED.mCamera.mPos = FVector(0.f, -7.f, -1.4f);
	FRONTEND.UpdateCamera();

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

	if (mThing)
	{
		FMatrix	yaw;
		yaw.MakeRotationYawF(mTime);
		mThing->mOri = yaw;
		mThing->Render();
	}

	RENDERINFO.EnableLighting(false);
	RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	PopProjection();

	if ((transition == 1.f) || (dest == FEP_WINGMEN))
		FRONTEND.GetCommonPage()->RenderSmallForsetiText(1.f);
	if ((transition == 1.f) || (dest == FEP_WINGMEN))
		CSPRITERENDERER::DrawColouredSprite(285.f, 225.f, FO(900), FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), 0xffffffff, 1.6f, 1.6f, SA_CENTRE);
	if ((transition == 1.f) || (dest == FEP_WINGMEN))
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(1.f);

	if (GetNumConfigs() > 1)
		FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	else if ((FRONTEND.mSelectedLevel >= 231) && CFEPWingmen::GetNumWingmen())
		FRONTEND.GetCommonPage()->RenderHelpText(4, transition);
	else
		FRONTEND.GetCommonPage()->RenderHelpText(0, transition);
	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);

	// the configuration's name, then its weapons
	WCHAR	*text;
	SIZE	size;

	text = GetConfigDescription(mConfig);
	PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(400.f - float(size.cx) * 0.5f - offset, 100.f, FO(100), 1.f, 1.f, 0xffffffff, text, 1000.f, 0, 0);

	float	reveal = PLATFORM.GetSysTimeFloat() - mChangeTime;

	text = FET.GetText(FEX_GROUND_WEAPONS);
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
	float	tx = 280.f - offset;
	PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(tx, 210.f, FO(100), 1.f, 1.f, 0xffff6f1f, text, 1000.f, 0, 0);

	float	y = 226.f;
	SINT	i;
	for (i = 0; i < GetNumWalkerWeapons(mConfig); i++)
	{
		text = GetWalkerWeaponName(mConfig, i);
		PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(tx, y, FO(100), 1.f, 1.f, 0xffffffff, text, reveal, 0, 0);
		y += 16.f;
	}

	y += 16.f;
	text = FET.GetText(FEX_AIR_WEAPONS);
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
	PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(tx, y, FO(100), 1.f, 1.f, 0xffff6f1f, text, 1000.f, 0, 0);
	y += 16.f;

	for (i = 0; i < GetNumJetWeapons(mConfig); i++)
	{
		text = GetJetWeaponName(mConfig, i);
		PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(tx, y, FO(100), 1.f, 1.f, 0xffffffff, text, reveal, 0, 0);
		y += 16.f;
	}


	// the ratings: a column for each, headed by its unit
	RENDERINFO.SetFogEnabled(false);
	PLATFORM.GetSysTimeFloat();

	CSPRITERENDERER::DrawColouredSprite(445.f - offset, 180.f, FO(100), FED.GetTexture(FET_I_TROOP), 0xffffffff, 1.f, 1.f);
	CSPRITERENDERER::DrawColouredSprite(480.f - offset, 180.f, FO(100), FED.GetTexture(FET_I_TANK), 0xffffffff, 1.f, 1.f);
	CSPRITERENDERER::DrawColouredSprite(515.f - offset, 180.f, FO(100), FED.GetTexture(FET_I_PLANE), 0xffffffff, 1.f, 1.f);

	float	py = 234.f;
	SINT	total0 = 0, total1 = 0, total2 = 0;
	SINT j; for (j = 0; j < GetNumWalkerWeapons(mConfig); j++)
	{
		SINT	r;
		r = GetWalkerWeaponRating(mConfig, j, 0);
		RenderRatingPips(460.f - offset, py, float(r) / 5.0f, 0xffff6f4f);
		total0 += r;
		r = GetWalkerWeaponRating(mConfig, j, 1);
		RenderRatingPips(495.f - offset, py, float(r) / 5.0f, 0xff6fff4f);
		total1 += r;
		r = GetWalkerWeaponRating(mConfig, j, 2);
		RenderRatingPips(530.f - offset, py, float(r) / 5.0f, 0xff4f6fff);
		total2 += r;
		py += 16.f;
	}

	py += 16.f;
	py += 16.f;
	for (j = 0; j < GetNumJetWeapons(mConfig); j++)
	{
		SINT	r;
		r = GetJetWeaponRating(mConfig, j, 0);
		RenderRatingPips(460.f - offset, py, float(r) / 5.0f, 0xffff6f4f);
		total0 += r;
		r = GetJetWeaponRating(mConfig, j, 1);
		RenderRatingPips(495.f - offset, py, float(r) / 5.0f, 0xff6fff4f);
		total1 += r;
		r = GetJetWeaponRating(mConfig, j, 2);
		RenderRatingPips(530.f - offset, py, float(r) / 5.0f, 0xff4f6fff);
		total2 += r;
		py += 16.f;
	}

	// the arrows, which flash when the configuration changes
	if (GetNumConfigs() > 1)
	{
		float	scale;
		FRONTEND.ProcessMouseClickInRect(x - 40.f, 101.f, x + 40.f, 181.f, BUTTON_FRONTEND_MENU_UP);
		if (((SINT(FRONTEND.GetCounter()) % 64) < 50) || (mUpFlash > 0.f))
		{
			CSPRITERENDERER::DrawColouredSprite(x, 141.f, FO(100), FED.GetTexture(FET_HELP_ARROW), 0xffffffff, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, -PI / 2);
			DWORD	col = 0xffffffff;
			BlendAlpha(col, SINT(mUpFlash * 255.f));
			scale = (1.f - mUpFlash) + 1.f;
			CSPRITERENDERER::DrawColouredSprite(x, 141.f, FO(100), FED.GetTexture(FET_HELP_ARROW), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, -PI / 2);
		}

		if (((SINT(FRONTEND.mCounter) % 64) < 50) || (mDownFlash > 0.f))
		{
			FRONTEND.ProcessMouseClickInRect(x - 40.f, 344.f, x + 40.f, 424.f, BUTTON_FRONTEND_MENU_DOWN);
			CSPRITERENDERER::DrawColouredSprite(x, 384.f, FO(100), FED.GetTexture(FET_HELP_ARROW), 0xffffffff, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, PI / 2);
			DWORD	col = 0xffffffff;
			BlendAlpha(col, SINT(mDownFlash * 255.f));
			scale = (1.f - mDownFlash) + 1.f;
			CSPRITERENDERER::DrawColouredSprite(x, 384.f, FO(100), FED.GetTexture(FET_HELP_ARROW), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, PI / 2);
		}
	}

	if (dest != FEP_WINGMEN)
		FRONTEND.DrawTitleBar(FET.GetText(FEX_BE_CONFIG), transition, dest);
	else
		FRONTEND.DrawTitleBar(FET.GetText(FEX_BE_CONFIG), 1.f, FEP_WINGMEN);
}

//*********************************************************************************
// FUNCTION: 0x004519c0
void	CFEPBEConfig::TransitionNotification(EFrontEndPage from)
{
	mStartTime = PLATFORM.GetSysTimeFloat();
	mConfig = 0;

	if (FRONTEND.mSelectedLevel == 710)
	{
		if (GetNumConfigs() > 3)
			mConfig = 1;
	}
}

//*********************************************************************************
// FUNCTION: 0x00451a40
inline SBEConfigLevel	*CFEPBEConfig::GetCurrentLevel()
{
	for (SBEConfigLevel *level = mLevels.First(); level; level = mLevels.Next())
	{
		if (FRONTEND.mSelectedLevel == level->mLevel)
			return level;
	}
	return NULL;
}
