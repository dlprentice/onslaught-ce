// atmospherics.h: the atmospherics effects (Atmospherics.obj, 00404910..00404cb1): a list of modules (the snow and the
// clouds) that the engine initialises, processes, renders and shuts down together, and the weather the scripts set.

#ifndef ATMOSPHERICS_H
#define ATMOSPHERICS_H

class CDXTexture;

class CAtmospherics
{
public:
#ifdef ATMOSPHERICS_FULL	// the atmospherics' own view (vtable 005d87ec: five pure slots, two empty ones); the
							// others keep the static-only one (game.cpp's header-composition tie)
					CAtmospherics();			// 00404920: appends itself to the list
					~CAtmospherics();			// 00404960: unlinks itself
	virtual char	*GetName() = 0;				// 0
	virtual void	Process() = 0;				// 1
	virtual void	Render() = 0;				// 2
	virtual void	Init() = 0;					// 3
	virtual void	Shutdown() = 0;				// 4
	virtual void	Unknown5(FVector *dir) {}	// 5: each module's turn at the light direction (AlterLightDir 00404ca5)
	virtual void	Unknown6(int a) {}			// 6

	CAtmospherics	*mNext;						// +0x04

	static CAtmospherics	*mFirst;			// 006601a8
	static CDXTexture			*mSnowLayer;		// 006601ac: InitialiseAll finds it, ShutdownAll releases it
#elif defined(DXMESHVB_CPP)	// the mesh buffers draw it as a layer (CDXMeshVB::Render 0054d7ef)
	static CDXTexture			*mSnowLayer;		// 006601ac
#endif
	static void	InitialiseAll();				// 00404a00
	static void	SetupAll();						// 00404b90: needs to be done after level load
	static void	ShutdownAll();					// 00404c10
	static void	ProcessAll();					// 00404bf0
	static void	AlterLightDir(FVector *dir);	// 00404c90 (CEngine::SetupLights 0044a364)
	static void	RenderAll();					// 00404bd0 (CDXEngine::Render 0053e8d2)

	// the weather the scripts set (IScript::SetLightningDensity .. SetWindVector); InitialiseAll clears them
	static float	mLightningDensity;			// 00660188
	static float	mSnowDensity;				// 0066018c
	static float	mRainDensity;				// 00660190: the console's atm_raindensity too
	static FVector	mWindVector;				// 00660198
};

#endif
