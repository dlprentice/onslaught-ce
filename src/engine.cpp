#define ENGINE_CPP
#include	"Common.h"
#include	"Engine.h"
#include	"Console.h"

#include	<stdio.h>
#include	<string.h>
#include	"camera.h"
#include	"platform.h"
#include	"gcgamut.h"
#include	"landscape.h"
#include	"maptex.h"
#include	"eventmanager.h"
#include	"water.h"
#include	"map.h"
#include	"atmospherics.h"
#include	"shadows.h"
#include	"renderinfo.h"
#include	"screenfx.h"
#include	"GameInterface.h"
#include	"shadows.h"
#include	"world.h"
#include	"THING.h"
#include	"tree.h"
#include	"ResourceAccumulator.h"
#include	"chunker.h"
#include	"meshrenderer.h"
#include	"trees.h"
#if TARGET==PS2
#include	"PS2Pinmapper.h"
#include "PS2ClutData.h"
#include "PS2MipmapData.h"
#include "PS2Display.h"

#else
#include	"state.h"
#endif

//******************************************************************************************

CEngine::CEngine()	// FUNCTION: 0x00449820
{
	mNearZ = DEFAULT_Z_NEAR;
	mFarZ = DEFAULT_Z_FAR;
	mGamut = NULL;	
	mRenderLandscape=TRUE;	
	mDrawPolyBuckets=FALSE;
	mMapTexs = NULL;	
	mKempyCube=NULL;
	mWater=NULL;	
	mLights=NULL;	
	mSky=NULL;

	mHudAditive		= TRUE;
	mLandscape	  = NULL;
	mHilightTexture=NULL;
	mHitEffectTexture=NULL;
	mCloakTexture=NULL;

	mCurrentViewpoint = 0;
	for (int i=0;i<VIEWPOINTS;i++)
	{
		mCamera[i]		  = NULL;
	}
}

//******************************************************************************************

void CEngine::Shutdown()	// FUNCTION: 0x00449890
{
	SCREENFX.Shutdown();
	
	SHADOWS.ShutDown();

	TREES.Shutdown();

	SAFE_DELETE(mGamut);
	
	mLandscape->ShutDown();
	SAFE_DELETE(mLandscape);

	for (int i=0;i<VIEWPOINTS;i++)
	{
		SAFE_DELETE(mCamera[i]);		
	}

	SAFE_DELETE(mLights);
	SAFE_DELETE(mWater);
	SAFE_DELETE_ARRAY(mMapTexs);
	SAFE_DELETE(mKempyCube);	

	SAFE_RELEASE(mHilightTexture);
	SAFE_RELEASE(mHitEffectTexture);
	SAFE_RELEASE(mCloakTexture);

#ifdef _DIRECTX
	// The dynamic vbuffers can piss off.
	CVBufTexture::ClearOut();
#endif
}

//******************************************************************************************

BOOL CEngine::Init()	// FUNCTION: 0x004499d0
{
	int i;

	mLandscape=NULL;

	for (i=0;i<VIEWPOINTS;i++)
	{
		mCamera[i]			= NULL;
	}

	// Set the default hit effect factor
	
	mHitEffectFactorR=200;
	mHitEffectFactorG=150;
	mHitEffectFactorB=150;
	
	CONSOLE.RegisterVariable("cg_hiteffectfactorr","The red factor for the hit effect (/100)",CVar_int,&mHitEffectFactorR);
	CONSOLE.RegisterVariable("cg_hiteffectfactorg","The green factor for the hit effect (/100)",CVar_int,&mHitEffectFactorG);
	CONSOLE.RegisterVariable("cg_hiteffectfactorb","The blue factor for the hit effect (/100)",CVar_int,&mHitEffectFactorB);	

	// Landscape rendering

	CONSOLE.RegisterVariable("cg_renderlandscape","Should the landscape be rendered",CVar_bool,&mRenderLandscape);	

	// Debug rendering

	CONSOLE.RegisterVariable("cg_drawpolybuckets","Should polybucket volumes be rendered",CVar_bool,&mDrawPolyBuckets);	

	// Create the gamut
	
	mGamut = new( MEMTYPE_GAMUT ) C3DGamut(32);	

	// Map textures

	if((mMapTexs	= new( MEMTYPE_MAPTEX ) CMapTex[N_MAPMIPS])==NULL)		return	FALSE;		

	// Water

	if((mWater		= new( MEMTYPE_WATER ) CWATER)==NULL)					return	FALSE;	

	// Landscape
	
	if((mLandscape	= new( MEMTYPE_LANDSCAPE ) CLANDSCAPE)==NULL)					
		return FALSE;

#if TARGET == XBOX
	// we DO NOT init the landscape here. We init it after the FMV playing,
	// so that we can use the megabyte of texture RAM used only temporarily
	// by the FMV
#else
	// the old, more sensible method.
	if(!mLandscape->Init(mMapTexs))										
		return FALSE;
#endif


	// Skycube

	mKempyCube		= NULL;
	
	if((mKempyCube	= new( MEMTYPE_KEMPYCUBE ) CKEMPYCUBE)==NULL)				return	FALSE;	

	// Light manager

	mLights			= NULL;

	if((mLights		= new( MEMTYPE_LIGHT ) CLightManager)==NULL)				return	FALSE;	
	if(!mLights->Init())														return FALSE;
	
	// Initialise screen FX
	
	SCREENFX.Init();

	// Shadows
#ifndef EDITORBUILD2	
	SHADOWS.Init();
#endif	

	TREES.Init();

	mCurrentViewpoint = 0;
	
	//mDebugNoParticles = FALSE;
	
	return TRUE;
}

//******************************************************************************************

void	CEngine::InitResources()	// FUNCTION: 0x00449d50
{
	// Initialise screen FX
	SCREENFX.InitResources();

	SHADOWS.InitResources();

	mHilightTexture=CTEXTURE::GetTextureByName("hilight.tga");
	mHitEffectTexture=CTEXTURE::GetTextureByName("hiteffect.tga",TEXFMT_UNKNOWN,TEX_NORMAL,1);
	mCloakTexture=CTEXTURE::GetTextureByName("cloak.tga",TEXFMT_UNKNOWN,TEX_NORMAL,1);

	mLandscape->InitResources();
}

//******************************************************************************************
#ifdef RESBUILDER
void	CEngine::AccumulateResources(CResourceAccumulator *accumulator)
{
#if TARGET==PC
//	mHilightTexture->AccumulateResources(accumulator,RES_BASESET);
	mHilightTexture->AccumulateResources(accumulator);
	mLandscape->AccumulateResources(accumulator);
	SHADOWS.AccumulateResources(accumulator);
	
	// "Random" engine stuff that the PS2 needs that we can't easily accumulate elsewhere
	
	CDXTexture *tex=CTEXTURE::GetTextureByName("landscapelight.tga");
	tex->AccumulateResources(accumulator,RES_NOTONXBOX);
	mHitEffectTexture->AccumulateResources(accumulator,0);
	mCloakTexture->AccumulateResources(accumulator,0);
#endif
}
#endif
//******************************************************************************************

void	CEngine::LoadAllNamedMeshes( CMEMBUFFER &dataFile )	// FUNCTION: 0x00449dc0
{
	// Make sure we restart from 0
	ResetNamedMeshes();

	CONSOLE.Status("Loading named meshes");

	SINT		num_named_meshes;
	dataFile.Read( &num_named_meshes, sizeof(SINT) ) ;
	
	for (int i=0;i < num_named_meshes; i++)
	{
		// Load names 
		unsigned char		stringLength;
		dataFile.Read( &stringLength, sizeof(stringLength) );

		char		MeshName[255];
		dataFile.Read( MeshName, sizeof(char) * stringLength );
		MeshName[stringLength]=0;
		ENGINE.AddNewGlobalNamedMesh(MeshName) ;
	} 

	CONSOLE.StatusDone("Loading named meshes");
}

//******************************************************************************************
void	CEngine::ResetNamedMeshes()
{
	mNumGlobalMeshes=0;
}

//******************************************************************************************
SINT	CEngine::AddNewGlobalNamedMesh(char *name)
{
	// do we have this already
	SINT	c0;

	for (c0 = 0; c0 < mNumGlobalMeshes; c0 ++)
	{
		if (mGlobalMeshes[c0])
			if (!(stricmp(name, mGlobalMeshes[c0]->mName)))
				return c0;
	}

	// nope - need to create a new resource
	if (mNumGlobalMeshes == MAX_GLOBAL_MESHES - 1)
	{
		return -1;
	}

	mGlobalMeshes[mNumGlobalMeshes] = CMESH::GetMesh(name);

	if (!mGlobalMeshes[mNumGlobalMeshes])
	{
		return -1;
	}

	mNumGlobalMeshes ++;

	return mNumGlobalMeshes -1 ;
}

//=============================================-===--==---=---- ---  --   -

void	CEngine::GetViewMatrixFromCamera(CCamera *cam, FMatrix *matview)	// FUNCTION: 0x00449ef0
{
	FMatrix pitch ;
	pitch.MakeRotationPitchF(1.570796f) ;
	FMatrix ori = cam->GetOrientation();
	ori.TransposeInPlace() ;
	*matview = pitch * ori;
}

//=============================================-===--==---=---- ---  --   -

void CEngine::SetViewpoint(int viewpoint,CCamera *camera,CViewport *viewport,CPlayer *player)	// FUNCTION: 0x0044a020
{
	memcpy(&mViewport[viewpoint],viewport,sizeof(mViewport[viewpoint]));
	mPlayer[viewpoint]=player;
	if (mCamera[viewpoint])
		delete mCamera[viewpoint];
	mCamera[viewpoint] = new( MEMTYPE_CAMERA ) CInterpolatedCamera(camera);
}

//=============================================-===--==---=---- ---  --   -

float CEngine::GetRenderTime()	// FUNCTION: 0x0044a0c0
{
	return EVENT_MANAGER.GetTime();
}

//=============================================-===--==---=---- ---  --   -

void CEngine::SetNumViewpoints(int n)
{
	mViewpoints=n; 
}

//=============================================-===--==---=---- ---  --   -

void CEngine::SelectViewpoint(int n)	// FUNCTION: 0x0044a0d0
{
	ASSERT(n<mViewpoints);
	ASSERT(n>=0);
	mCurrentViewpoint=n;
	
	memcpy(&mCurrentViewport,&mViewport[mCurrentViewpoint],sizeof(mCurrentViewport));	
	
	PLATFORM.SetViewport(&mCurrentViewport);
}

//=============================================-===--==---=---- ---  --   -

void CEngine::ResetPos(int x, int y)	// FUNCTION: 0x0044a110
{
	ASSERT(mCurrentViewpoint!=-1);
	mLandscape->ResetPos(x, y);
}

//=============================================-===--==---=---- ---  --   -

void CEngine::InitDamageSystem()
{
	mLandscape->ResetDamage();
// stick in tree shadows with damage markers
	
	CThing* boj;
	ListIterator <CThing>	iterator(&WORLD.GetThingNB());

	for (boj=iterator.First(); boj; boj=iterator.Next())
	{
		if (boj->IsA(THING_TYPE_TREE))
		{
			
			AddDamage(boj->GetPos().X+TREE_SHADOW_XOFF, boj->GetPos().Y+TREE_SHADOW_YOFF, ((CTree*)boj)->GetShadowSize());
		}
	}
	ENGINE.LockCurrentDamage();
}

void CEngine::BuildLevelSpecifics()	// FUNCTION: 0x0044a130
{
#if TARGET != XBOX
	// everyone else inits the damage right now.
	InitDamageSystem();
#endif

	mLandscape->BuildLevelSpecifics();

#if TARGET == PS2
	DWORD lFogCol = MAP.GetFog();
	PS2DISPLAY.SetClearColour( lFogCol );
#endif
}

//=============================================-===--==---=---- ---  --   -

void CEngine::UpdatePos(CCamera* cam)	// FUNCTION: 0x0044a1c0
{
	if (!mRenderLandscape)
		return;
	ASSERT(mCurrentViewpoint!=-1);
	mLandscape->UpdatePos(cam,mCurrentViewpoint);
}

//=============================================-===--==---=---- ---  --   -

void CEngine::LoadMixers(int set)	// FUNCTION: 0x0044a1f0
{
	mMapTexs[0].LoadAll(set,MT_NUMTEX,256);
	mMapTexs[1].LoadAllFromMapTex(&mMapTexs[0]);			// 128
	mMapTexs[2].LoadAllFromMapTex(&mMapTexs[1]);			// 64
	mMapTexs[3].LoadAllFromMapTex(&mMapTexs[2]);			// 32
	mMapTexs[4].LoadAllFromMapTex(&mMapTexs[3]);			// 16
	mMapTexs[5].LoadAllFromMapTex(&mMapTexs[4]);			// 8
	mMapTexs[6].LoadAllFromMapTex(&mMapTexs[5]);			// 4

#if TARGET==PS2
	// Upload textures to IOP
	PINMAPPER.UploadMixTextures();

	// Reinitialise pinmapper
	PINMAPPER.InvalidateLevelData();
#endif
}

//=============================================-===--==---=---- ---  --   -

void CEngine::SetKempyCube(UBYTE number)	// FUNCTION: 0x0044a2a0
{
	mKempyCube->Change(number);
}

//=============================================-===--==---=---- ---  --   -

void CEngine::SetWater(UBYTE number)	// FUNCTION: 0x0044a2c0
{
	mWater->Change(number);
}

//=============================================-===--==---=---- ---  --   -

void CEngine::SetupLights()	// FUNCTION: 0x0044a2d0
{
// D3D lighting
	const SHeightFieldProperties	&hfp=MAP.GetHFProperties();

	FVector		sunlight(hfp.SunPosX,hfp.SunPosY,hfp.SunPosZ);

	sunlight.X=-sunlight.X;
	sunlight.Y=-sunlight.Y;
	sunlight.Z=-sunlight.Z;

	sunlight.Normalise();
	
	CAtmospherics::AlterLightDir(&sunlight); 

	SHADOWS.MakeView(sunlight);//shadowlight);				// the light the shadows use

	ENGINE.EnableLighting();

	CLight	light;
	float r,g,b;
	DWORD col;	

#ifdef OLD_LIGHTING	
	DWORD a=MAP.GetAmbient();
	RENDERINFO.SetAmbient(a);
	
	col=MAP.GetSun() - MAP.GetAmbient();

	CAtmospherics::AlterLightCol(&col);

	r=((float)((col&0xff0000)>>16))*(1.0f/256);
	g=((float)((col&0xff00)>>8))*(1.0f/256);
	b=((float)(col&0xff))*(1.0f/256);

	light.Init( LIGHT_DIRECTIONAL, sunlight, r,g,b);
	RENDERINFO.SetLight( 0, &light );
	RENDERINFO.SetLightEnable( 0, TRUE );

#else

#if TARGET == PS2
	DWORD lSunCol = MAP.GetSun();
	DWORD lAntiSunCol = MAP.GetAntiSun();

	UINT lSunColR = (lSunCol&0xff0000)>>16;
	UINT lSunColG = (lSunCol&0xff00)>>8;
	UINT lSunColB = (lSunCol&0xff);

	/*char buf[256];
	sprintf(buf,"Sun = %d,%d,%d\n",lSunColR,lSunColG,lSunColB);
	TRACE(buf);*/
	
	UINT lAntiSunColR = (lAntiSunCol&0xff0000)>>16;
	UINT lAntiSunColG = (lAntiSunCol&0xff00)>>8;
	UINT lAntiSunColB = (lAntiSunCol&0xff);

	//sprintf(buf,"AntiSun = %d,%d,%d\n",lAntiSunColR,lAntiSunColG,lAntiSunColB);
	//TRACE(buf);

	// Don't allow the antisun to be brighter than the sun
	if (lSunColR<lAntiSunColR)
		lAntiSunColR=lSunColR;
	if (lSunColG<lAntiSunColG)
		lAntiSunColG=lSunColG;
	if (lSunColB<lAntiSunColB)
		lAntiSunColB=lSunColB;

	lSunColR = (( lSunColR - lAntiSunColR )/2);
	lSunColG = (( lSunColG - lAntiSunColG )/2);
	lSunColB = (( lSunColB - lAntiSunColB )/2);

	DWORD lAmbient = ((lAntiSunColR+lSunColR)<<16) | ((lAntiSunColG+lSunColG)<<8) | (lAntiSunColB+lSunColB);

	//printf( "Ambient: %x\n", lAmbient );

	RENDERINFO.SetAmbient( lAmbient );

	r=((float)(lSunColR))*(1.0f/256);
	g=((float)(lSunColG))*(1.0f/256);
	b=((float)(lSunColB))*(1.0f/256);

	//sprintf(buf,"Sunout = %f,%f,%f\n",r,g,b);
	//TRACE(buf);

	//asm("break");
	
	light.Init( LIGHT_DIRECTIONAL, sunlight, r, g, b );
	RENDERINFO.SetLight( 0, &light );
	RENDERINFO.SetLightEnable( 0, TRUE );
#else
	RENDERINFO.SetAmbient(MAP.GetAmbient());			// no real ambient

	col = MAP.GetSun();

	r=((float)((col&0xff0000)>>16))*(1.0f/256);
	g=((float)((col&0xff00)>>8))*(1.0f/256);
	b=((float)(col&0xff))*(1.0f/256);

	light.Init( LIGHT_DIRECTIONAL, sunlight, r, g, b);
	RENDERINFO.SetLight( 0, &light );
	RENDERINFO.SetLightEnable( 0, TRUE );

	FVector alight;
	alight.X = -sunlight.X;
	alight.Y = -sunlight.Y;
	alight.Z = -sunlight.Z;
//	alight.Normalise();

	col = MAP.GetAntiSun();

	r=((float)((col&0xff0000)>>16))*(1.0f/256);
	g=((float)((col&0xff00)>>8))*(1.0f/256);
	b=((float)(col&0xff))*(1.0f/256);
	
	light.Init( LIGHT_DIRECTIONAL, alight, r, g, b);
	RENDERINFO.SetLight( 1, &light );
	RENDERINFO.SetLightEnable( 1, TRUE );
#endif

#endif
	RENDERINFO.SetLightEnable( 2, FALSE );
	RENDERINFO.SetLightEnable( 3, FALSE );
	RENDERINFO.SetLightEnable( 4, FALSE );
	RENDERINFO.SetLightEnable( 5, FALSE );
	RENDERINFO.SetLightEnable( 6, FALSE );
	RENDERINFO.SetLightEnable( 7, FALSE );
	

}

//=============================================-===--==---=---- ---  --   -

void	CEngine::LogLight(const FVector &inPos, eLightType lt, float inRadius)	// FUNCTION: 0x0044a610
{
	mLights->AddReq(inPos, mGamut, lt,inRadius);
}

//=============================================-===--==---=---- ---  --   -

void	CEngine::SwitchAllLights(BOOL onoff)	// FUNCTION: 0x0044a640
{
	mLights->SwitchAll(onoff);
}

//=============================================-===--==---=---- ---  --   -


//******************************************************************************************
void CEngine::EnableAlpha()	// FUNCTION: 0x0044a650
{
	RENDERINFO.SRS( RS_ALPHABLENDENABLE, TRUE );
	RENDERINFO.SRS( RS_SRCBLEND,  BLEND_SRCALPHA );
	RENDERINFO.SRS( RS_DESTBLEND, BLEND_INVSRCALPHA);
	RENDERINFO.SRS( RS_ZWRITEENABLE , FALSE );
}

//******************************************************************************************
void CEngine::EnableAdditiveAlpha()
{
	RENDERINFO.SRS( RS_ALPHABLENDENABLE, TRUE );
	RENDERINFO.SRS( RS_SRCBLEND,  BLEND_ONE );
	RENDERINFO.SRS( RS_DESTBLEND, BLEND_ONE );
	RENDERINFO.SRS( RS_ZWRITEENABLE , FALSE );
}


//******************************************************************************************
void CEngine::DisableAlpha()
{
	RENDERINFO.SRS( RS_ALPHABLENDENABLE, FALSE );
	RENDERINFO.SRS( RS_ZWRITEENABLE , TRUE );
}

//=============================================-===--==---=---- ---  --   -
void CEngine::SetTreeAlphaMode( const bool aSet )
{
	if( aSet )
	{
//		RENDERINFO.SRS(RS_ALPHABLENDENABLE,TRUE);
//		RENDERINFO.SRS(RS_ALPHATESTENABLE,TRUE);
//		RENDERINFO.SRS(RS_ALPHAREF,0x08);
	}
	else
	{
//		RENDERINFO.SRS(RS_ALPHABLENDENABLE,TRUE);
//		RENDERINFO.SRS(RS_ALPHAREF,0x08);
	}
}

//=============================================-===--==---=---- ---  --   -
void CEngine::DisableLighting() 	// FUNCTION: 0x0044a690
{ 
	RENDERINFO.SRS(RS_LIGHTING,FALSE) ; 
}

//=============================================-===--==---=---- ---  --   -
void CEngine::EnableLighting() 
{ 
//	RENDERINFO.SRS(RS_LIGHTING,TRUE) ; 
	RENDERINFO.SetLightingEnabled(TRUE);
}

//=============================================-===--==---=---- ---  --   -
void CEngine::SetSky(UBYTE number)
{
}

//=============================================-===--==---=---- ---  --   -
void CEngine::UpdateArea(SINT inStartX, SINT inStartY, SINT inFinishX, SINT inFinishY, BOOL updategeometryinstead)	// FUNCTION: 0x0044a6b0
{
//	mLandscape->ResetPos(0,0);
	mLandscape->UpdateArea(inStartX, inStartY, inFinishX, inFinishY, updategeometryinstead);
}

//=============================================-===--==---=---- ---  --   -
#ifdef RESBUILDER
void CEngine::Serialize(class CChunker *c,CResourceAccumulator *ra)
{	
	c->Start(MKID("ENGN"));

	int nmaptex=N_MAPMIPS;

	c->Write(&nmaptex,sizeof(nmaptex),1);

	for (int i=0;i<nmaptex;i++)
	{
		mMapTexs[i].Serialize(c,ra);
	}

	MAP.Serialize(c,ra);

	c->End();
}
#endif
//=============================================-===--==---=---- ---  --   -

void CEngine::Deserialize(class CChunkReader *c)	// FUNCTION: 0x0044a6e0
{
	if (c->GetNext()!=MKID("ENGN"))
	{
		SASSERT(0,"Engine deserialize format failure!");
	}

	int nmaptex;

	c->Read(&nmaptex,sizeof(nmaptex),1);

	SASSERT(nmaptex==N_MAPMIPS,"Deserialize format failure in engine! (wrong number of mixer textures)");

	for (int i=0;i<nmaptex;i++)
	{
		mMapTexs[i].Deserialize(c,i);
	}

	MAP.Deserialize(c);

#if TARGET==PS2
	// Upload textures to IOP
	PINMAPPER.UploadMixTextures();
	
	// Reinitialise pinmapper
	PINMAPPER.InvalidateLevelData();
#endif
}

//=============================================-===--==---=---- ---  --   -

#ifdef EDITORBUILD2
FVector	CEngine::GetCursorOffsetMatrix(float x,float y)
{
	float		sx=(float)PLATFORM.GetScreenWidth();
	float		sy=(float)PLATFORM.GetScreenHeight();

	if (x < 0)		x = 0;
	if (y < 0)		y = 0;
	if (x > sx - 1)	x = sx - 1;
	if (y > sy - 1)	y = sy - 1;

	x-=sx/2;
	y-=sy/2;

	FVector		vector(x/(sx/2),1.0f,y/(sx/2));

	vector.Normalise();

	return vector;
}
#endif
#include	"mathsconst.h"	// retail: engine.obj ends with the maths constants' initializers
// FUNCTION: 0x00449730 init:ZERO_FVECTOR
// FUNCTION: 0x00449750 init:ID_FMATRIX

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x0044a5f0 ?Set@FVector@@QAEXMMM@Z

// the global operator delete, out of line in this object (MemoryManager.h)
// FUNCTION: 0x00449d40 ??3@YAXPAX@Z
