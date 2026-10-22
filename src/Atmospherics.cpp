// Atmospherics.cpp: reconstructed from Atmospherics.obj (00404910..00404cde); Stuart's release has no copy. The list
// of atmospherics modules and the weather the scripts set. The allocations' __LINE__s are the retail ones; the #lines
// place them.
#include "common.h"
#define ATMOSPHERICS_CPP
#define ATMOSPHERICS_FULL
#include "maths.h"
#include "DX.h"
#include "atmospherics.h"
#include "DXSnow.h"
#include "DXClouds.h"
#include "Texture.h"
#include "console.h"

float			CAtmospherics::mLightningDensity;	// 00660188
float			CAtmospherics::mSnowDensity;		// 0066018c
float			CAtmospherics::mRainDensity;		// 00660190
FVector			CAtmospherics::mWindVector;			// 00660198
CAtmospherics	*CAtmospherics::mFirst;				// 006601a8
CDXTexture		*CAtmospherics::mSnowLayer;			// 006601ac

// FUNCTION: 0x00404910 initorder:1


//******************************************************************************************
CAtmospherics::CAtmospherics()	// FUNCTION: 0x00404920
{
	CAtmospherics	*a = mFirst;
	if (a)
	{
		while (a->mNext)
			a = a->mNext;
		a->mNext = this;
	}
	else
		mFirst = this;
	mNext = NULL;
}

//******************************************************************************************
CAtmospherics::~CAtmospherics()	// FUNCTION: 0x00404960
{
	CAtmospherics	*a = mFirst;
	if (a != this)
	{
		while (a->mNext != this)
			a = a->mNext;
		a->mNext = mNext;
	}
	else
		mFirst = mNext;
}

//******************************************************************************************
void	ListAtmospherics(char *)	// FUNCTION: 0x004049a0
{
	CAtmospherics	*a = CAtmospherics::mFirst;
	CONSOLE.Print("Atmospheric effects : \n");
	while (a)
	{
		CONSOLE.Print("%s\n", a->GetName());
		a = a->mNext;
	}
	CONSOLE.Print("\n");
}

//******************************************************************************************
void	CAtmospherics::InitialiseAll()	// FUNCTION: 0x00404a00
{
	mWindVector.Set(0, 0, 0);
	mLightningDensity = mSnowDensity = mRainDensity = 0;
	mSnowLayer = CDXTexture::FindTexture("Atmospherics\\Snow\\SnowLayer.tga", 4, 0, 1, 1, 0);

#line 112
	new (MEMTYPE_ATMOSPHERICS) CSnow;
#line 115
	new (MEMTYPE_ATMOSPHERICS) CClouds;

	CONSOLE.RegisterCommand("ListAtmospherics", "Displays a list of the atmospheric effect modules", ListAtmospherics);
	CONSOLE.RegisterVariable("atm_windvector", "The prevailing wind vector", CVar_fvector, &mWindVector);
	CONSOLE.RegisterVariable("atm_raindensity", "Rain density (0-1)", CVar_float, &mRainDensity);
	CONSOLE.RegisterVariable("atm_snowdensity", "Snow density (0-1)", CVar_float, &mSnowDensity);
	CONSOLE.RegisterVariable("atm_lightningdensity", "Lightning density (0-1)", CVar_float, &mLightningDensity);
}

//******************************************************************************************
void	CAtmospherics::SetupAll()	// FUNCTION: 0x00404b90
{
	mWindVector.Set(0, 0, 0);
	for (CAtmospherics *a = mFirst; a; a = a->mNext)
		a->Init();
}

//******************************************************************************************
void	CAtmospherics::RenderAll()	// FUNCTION: 0x00404bd0
{
	for (CAtmospherics *a = mFirst; a; a = a->mNext)
		a->Render();
}

//******************************************************************************************
void	CAtmospherics::ProcessAll()	// FUNCTION: 0x00404bf0
{
	for (CAtmospherics *a = mFirst; a; a = a->mNext)
		a->Process();
}

//******************************************************************************************
void	CAtmospherics::ShutdownAll()	// FUNCTION: 0x00404c10
{
	if (mSnowLayer)
	{
		mSnowLayer->CTexture::Release();
		mSnowLayer = NULL;
	}

	CAtmospherics	*a = mFirst;
	while (a)
	{
		CAtmospherics	*next = a->mNext;
		a->Shutdown();
		delete a;
		a = next;
	}
}

//******************************************************************************************
void	CAtmospherics::AlterLightDir(FVector *dir)	// FUNCTION: 0x00404c90
{
	for (CAtmospherics *a = mFirst; a; a = a->mNext)
		a->Unknown5(dir);
}

#include "mathsconst.h"	// the maths constant's include, where retail's initializer falls (placement evidence)	// FUNCTION: 0x00404cc0 init:ZERO_FVECTOR
// STAND-IN (link layout): retail's 136 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_Atmospherics[136] = { 0 };
