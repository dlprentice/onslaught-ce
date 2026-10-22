// DXEngine.cpp: implementation of the CDXEngine class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"

#ifdef _DIRECTX

#include	"DXEngine.h"
#include	"DX.h"
#include	"State.h"
#include	"Map.h"
#include	"D3DUtils.h"
#include	"Maptex.h"
#include	"Game.h"
#include	"RTMesh.h"
#include	"RenderThing.h"
#include	"Imposter.h"
#include	"Thing.h"
#include	"KempyCube.h"
#include	"Water.h"
#include	"Shadows.h"
#include	"GCGamut.h"
#include	"World.h"
#include	"Camera.h"
#include	"Profile.h"
#include	"ParticleManager.h"
#include	"ParticleSet.h"
#include	"Line.h"
#include	"UMTexture.h"
#include	"DXHud.h"
#include	"Lights.h"
#include	"VBufTexture.h"
#include	"Capture.h"
#include	"Fog.h"
#include	"Smoke.h"
#include	"Atmospherics.h"
#include	"Console.h"
#include	"BattleLine.h"
#include	"EventManager.h"
#include	"landscape.h"
#include	"cockpit.h"
#include	"rendertarget.h"
#include	"spriterenderer.h"
#include	"waterreflection.h"
#include	"cutscene.h"
#include	"debris.h"
#include	"renderqueue.h"
#include	"screenfx.h"
#include	"renderinfo.h"
#include	"membuffer.h"
#include	"gameinterface.h"
#include	"Player.h"
#include	"debugtext.h"
#include	"debuglog.h"
#include	"rain.h"
#include	"cutsceneeditor.h"
#include	"dxpatchmanager.h"
#include	"debugmarker.h"
#include	"visibilitytester.h"
#include    "pausemenu.h"
#include	"trees.h"
#include	"cliparams.h"
#include	"tweak.h"
#include	"MessageLog.h"
#include	"MessageBox.h"
//=============================================-===--==---=---- ---  --   -
CTweakSLONG	OLD_WATER_TIMING("OLD_WATER_TIMING", 0);	// retail PC (0053d310)
// FUNCTION: 0x0053d310 init:OLD_WATER_TIMING
// FUNCTION: 0x0053d330 exit:OLD_WATER_TIMING
CDXEngine		ENGINE;
// FUNCTION: 0x0053d340 init:ENGINE
// FUNCTION: 0x0053d390 exit:ENGINE
bool			gMirrorPass = false;		// 0089d680: a mirrored pass (engine.h; CGame::Render sets it)
    //=============================================-===--==---=---- ---  --   -
   //
  // Engine
 //
//=============================================-===--==---=---- ---  --   -
CDXEngine::CDXEngine()
{	
	mDefaultShader	= NULL;
	mScreenTexture	= NULL;
	
	mCurrentViewpoint = 0;
	mRenderReflections=false;
	mTextureReflections=true;
//#ifdef EDITORBUILD2
	mNavDisplay = FALSE;
//#endif
}
//=============================================-===--==---=---- ---  --   -
CDXEngine::~CDXEngine()
{
//	ShutDown();
}

//=============================================-===--==---=---- ---  --   -
// retail PC: the default texture and mesh are released first, and CSystem::Shutdown also calls this (004f01b0)
void CDXEngine::ReleaseDefaults()	// FUNCTION: 0x0053d3a0
{
	SAFE_RELEASE(mDefaultTexture);
	SAFE_RELEASE(mDefaultMesh);
}

//=============================================-===--==---=---- ---  --   -
void CDXEngine::ShutDown()	// FUNCTION: 0x0053d3e0
{
	CEngine::Shutdown();

	ReleaseDefaults();

	SAFE_RELEASE(mScreenTexture);

	SAFE_RELEASE(mTexOutline);
#if TARGET == PC
	SAFE_RELEASE(mTexEdArrow);
#endif

	PATCHMANAGER.Shutdown();

	mSunPD = NULL;
}

//=============================================-===--==---=---- ---  --   -
// Sets the gamma ramp bias (0=normal, 1=silly contrast!)
//=============================================-===--==---=---- ---  --   -

void con_setgammabias(char *cmd)	// FUNCTION: 0x0053d480
{
	float value;

	if (sscanf(cmd,"%*s %f",&value)!=1)
	{
		CONSOLE.Print("Syntax : SetGammaBias <value> (range 0-1)\n");
		return;
	}

	ENGINE.SetGammaBias(value);
}

void CDXEngine::SetGammaBias(float value)	// FUNCTION: 0x0053d4c0
{
	D3DGAMMARAMP ramp;

	for (int i=0;i<256;i++)
	{
		float scale=(256-i)*value;
		if (scale>1)
			scale=1;
		if (scale<0)
			scale=0;
		scale=1-scale;
		ramp.red[i]=(BYTE) (mDefaultGammaRamp.red[i]*scale);
		ramp.green[i]=(BYTE) (mDefaultGammaRamp.green[i]*scale);
		ramp.blue[i]=(BYTE) (mDefaultGammaRamp.blue[i]*scale);
		ramp.red[i]=(ramp.red[i]<<8)+ramp.red[i];
		ramp.green[i]=(ramp.green[i]<<8)+ramp.green[i];
		ramp.blue[i]=(ramp.blue[i]<<8)+ramp.blue[i];
	}

	LT.D3D_SetGammaRamp(D3DSGR_NO_CALIBRATION,&ramp);
}

//=============================================-===--==---=---- ---  --   -
BOOL CDXEngine::Init()	// FUNCTION: 0x0053d5f0
{
	if (!CEngine::Init())
		return(FALSE);
	
	mDefaultShader	= NULL;
	mScreenTexture	= NULL;
	
	mDrawDebugStuff = DRAW_COCKPIT ;

//	HRESULT hr;	
/*
	if(FAILED(hr = mKempyCube->Init(MAP.GetGenerator()->SV.CubeNo)))	return hr;
	if(FAILED(hr = mSky->Init(MAP.GetGenerator()->SV.CloudNo)))			return hr;
	if(FAILED(hr = mWater->Init(MAP.GetGenerator()->SV.WaterTexture)))	return hr;
*/	
	
	mNumGlobalMeshes = 0;

	// retail PC: no screen capture texture and no capture flag (0053d5fe..0053d63d)
	mCaptureAreaTop=0;
	mCaptureAreaBottom=1000;
	mBlurAlpha=0;
	mNewBlurAlpha=-1;
	mFirstFrame=TRUE;

	// Grab the current gamma ramp

	LT.D3D_GetGammaRamp(&mDefaultGammaRamp);

	// Set up our console commands

	CONSOLE.RegisterCommand("SetGammaBias","Alter the gamma ramp bias (0=normal, 1=black), 0-0.005 is a good range to try",&con_setgammabias);

	CONSOLE.RegisterVariable("cg_renderreflections","Should water reflections be rendered",CVar_bool,&mRenderReflections);
	CONSOLE.RegisterVariable("cg_texturereflections","Should render to texture be used for reflections",CVar_bool,&mTextureReflections);

	PATCHMANAGER.Init(800,300,90);
//	PATCHMANAGER.Init(1000,400,120);
//	PATCHMANAGER.Init(2000,800,300);
	
	return(TRUE);
}

//=============================================-===--==---=---- ---  --   -
void CDXEngine::InitResources()	// FUNCTION: 0x0053d6d0
{
	CEngine::InitResources();

	mDefaultTexture     = CTEXTURE::GetTextureByName("meshtex\\default.tga");
	mTexOutline			= CTEXTURE::GetTextureByName("meshtex\\outline.tga");
#if TARGET == PC
	mTexEdArrow			= CTEXTURE::GetTextureByName("meshtex\\EdArrow.tga");
#endif

	mDefaultMesh = CMESH::GetMesh("default.msh");

	mSunPD = PARTICLE_SET.GetPD("Sun Sprite");
}

//******************************************************************************************
void	CDXEngine::DrawDebugCuboid(DWORD col, FVector &axes, FVector &pos, FMatrix ori, CTEXTURE *for_texture)	// FUNCTION: 0x0053d760
{
	// Draw polys representing cuboid.
	// ** ASSUMES local orientation already set up...

	RENDERINFO.SRS( RS_ALPHABLENDENABLE,	TRUE );
	RENDERINFO.SRS( RS_SRCBLEND,			BLEND_ONE );
	RENDERINFO.SRS( RS_DESTBLEND,			BLEND_ZERO );
	RENDERINFO.Apply() ;

	FVector ax   = ori * FVector(axes.X, 0, 0);
	FVector ay   = ori * FVector(0, axes.Y, 0);
	FVector az   = ori * FVector(0, 0, axes.Z);
	
	SINT		c0;
	CFVF_GTEX	vert;

	CVBufTexture *vb=CVBufTexture::GetOrCreate(for_texture, 0);

	vb->SetVBFormat(FVF_GTEX_FLAGS,D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,sizeof(CFVF_GTEX),D3DPT_TRIANGLELIST,D3DPOOL_DEFAULT);
	vb->SetupI(D3DFMT_INDEX16,D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,2,D3DPOOL_DEFAULT);

	for (c0 = 0; c0 < 6; c0++)
	{
		SINT		i[4];

		for (int c1 = 0;c1 < 4; c1 ++)
		{
			switch (c0)
			{
			case 0:	vert.mPos	=   ax                  + ((c1 & 1) ? ay : -ay) + ((c1 & 2) ? az : -az) ; break;
			case 1:	vert.mPos	= - ax                  + ((c1 & 1) ? ay : -ay) + ((c1 & 2) ? az : -az) ; break;
			case 2:	vert.mPos	= ((c1 & 1) ? ax : -ax) + ay                    + ((c1 & 2) ? az : -az) ; break;
			case 3:	vert.mPos	= ((c1 & 1) ? ax : -ax) - ay                    + ((c1 & 2) ? az : -az) ; break;
			case 4:	vert.mPos	= ((c1 & 1) ? ax : -ax) + ((c1 & 2) ? ay : -ay) + az		   			; break;
			case 5:	vert.mPos	= ((c1 & 1) ? ax : -ax) + ((c1 & 2) ? ay : -ay) - az		   			; break;
			};

			vert.mPos.X  += pos.X;
			vert.mPos.Y  += pos.Y;
			vert.mPos.Z  += pos.Z;

			vert.mU	= (c1&1)?1.f:0.f;
			vert.mV	= (c1&2)?0.f:1.f;

			// Make the front of objects a different colour

			if (c0==2)
				vert.mDiffuse = 0xFFFFFFFF;
			else
				vert.mDiffuse = col;

			i[c1]=vb->AddVertices(&vert,1);
		}

		// double-sided...

		UWORD indices[12];
		
		indices[0]=i[0]; indices[1]=i[3]; indices[2]=i[2];
		indices[3]=i[0]; indices[4]=i[1]; indices[5]=i[3];
		indices[6]=i[0]; indices[7]=i[2]; indices[8]=i[3];
		indices[9]=i[0]; indices[10]=i[3]; indices[11]=i[1];
		
		vb->AddIndices(&indices,12);
	}

	vb->Draw(true);

	SAFE_RELEASE(vb);
	RENDERINFO.SRS( RS_SRCBLEND,			BLEND_SRCALPHA );
	RENDERINFO.SRS( RS_DESTBLEND,			BLEND_INVSRCALPHA );
	RENDERINFO.Apply() ;
}

#ifdef NOT_YET_RECONSTRUCTED	// unreferenced in the retail build
//******************************************************************************************

void	CDXEngine::DrawDebugLine(DWORD col, FVector &start, FVector &end)
{
	FVector axes=FVector(0.01f,0.01f,(end-start).Magnitude()/2.0f);

	FMatrix ori;

	ori.BuildLookMatrix(end-start,FVector(0,0,1));

	DrawDebugCuboid(col,axes,(start+end)/2.0f,ori,GetOpaqueTexture());
}

#endif

//******************************************************************************************
void	CDXEngine::RenderArrow(FVector v1, FVector v2, DWORD col)	// FUNCTION: 0x0053df40
{
#if TARGET == PC
	// draw arrow between 2 points

	FMatrix ori = ID_FMATRIX;
	FVector pos = FVector(0, 0, 0);
	RENDERINFO.SetWorld(pos, ori);

	FVector l = v2 - v1;
	FVector s = l ^ FVector(0, 0, -1.f);
	s.Normalise();
	s *= 0.5f;

//	LOG.AddMessage("arrow dif = %08x", col) ;
	SINT		c1;
	CFVF_GTEX	vert;

	CVBufTexture *vb=CVBufTexture::GetOrCreate(mTexEdArrow, 0);
	
	// Call the retail acquisition and format entries directly: forwarding wrappers alter VC6's inline budget,
	// rounding the first vertex X sum before vr is added. Retail keeps X live in x87 (0053e120..0053e149);
	// Y and Z still materialize separately.
	vb->SetVBFormat(FVF_GTEX_FLAGS,D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,sizeof(CFVF_GTEX),D3DPT_TRIANGLELIST,D3DPOOL_DEFAULT);
	vb->SetIBFormat(D3DFMT_INDEX16,D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,2,D3DPOOL_DEFAULT);

	SINT		i[4];

	for (c1 = 0;c1 < 4; c1 ++)
	{
		vert.mPos = ((c1 & 1) ? ZERO_FVECTOR : l) + ((c1 & 2) ? s : -s);

		FVector vr = v1 + FVector(0, 0, -0.5f);

		vert.mPos.X += vr.X ;
		vert.mPos.Y += vr.Y ;
		vert.mPos.Z += vr.Z ;

		vert.mU	= (c1&2)?1.f:0.f;
		vert.mV	= (c1&1)?0.f:1.f;
		vert.mDiffuse = col;
// broken by me :-)		vert.mDiffuse = col;

		i[c1]=vb->AddVertices(&vert,1);
	}

	UWORD indices[6];
	
	indices[0]=i[0]; indices[1]=i[3]; indices[2]=i[2];
	indices[3]=i[0]; indices[1]=i[1]; indices[5]=i[3];
	
	vb->AddIndices(&indices,6);

	vb->Draw(true);
	
	SAFE_RELEASE(vb);
#endif
}



#if TARGET == XBOX
static bool drawing_every_other = true;
static bool is_the_every_other = false;
#endif

//=============================================-===--==---=---- ---  --   -
HRESULT CDXEngine::PreRender(CViewport *viewport)	// FUNCTION: 0x0053e220
{
#if TARGET == XBOX
	if (drawing_every_other && LT.GetFlipMethod() == CXBOXDX::FULL_SYNC_LATE_HUD_MY_COPY)
	{
		// Interesting. We're only drawing every other frame.
		is_the_every_other ^= true;
	}

	if (is_the_every_other) return S_OK;
#endif

	// Do offscreen stuff

	STATE.UseDefault();
	
	SwitchAllLights(FALSE);					// don't want lights showing on imps

#ifdef RESBUILDER
	CIMPOSTER::MakeReady(mFirstFrame);		// Build imposters if needed
#endif

	// Clear whole screen

	// retail PC: the device's scene calls directly (0053e239, 0053e2ab)
	LT.D3D_BeginScene();

	mCurrentViewpoint=0;
	memcpy(&mViewport[mCurrentViewpoint],viewport,sizeof(mViewport[mCurrentViewpoint]));

	PLATFORM.SetViewport(viewport);

	memcpy(&mCurrentViewport,viewport,sizeof(mCurrentViewport));
	
	// Clear entire viewport (the whole screen)

#if TARGET == PC
	// retail PC: the target too in a mirrored pass (0053e287)
	LT.D3D_Clear( 0, NULL, gMirrorPass ? D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET : D3DCLEAR_ZBUFFER, 0x00607080, 1.0f, 0L );	
#else 
	LT.D3D_Clear( 0, NULL, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL , 0x00607080, 1.0f, 0L );	
//	LT.D3D_Clear( 0, NULL, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL | D3DCLEAR_TARGET, 0x00607080, 1.0f, 0L );	
#endif

	LT.D3D_EndScene();

	// Update the ripple effect

	mRipplePhase+=1;

	return(S_OK);
}

//=============================================-===--==---=---- ---  --   -

#if TARGET == XBOX

float SUN_TEST_SIZE = 1.2f;
float SUN_FAR_AWAY = 100.f;
DWORD SUN_COLOUR = 0x00ffffff;

static UINT num_sun_pixels_drawn[4];
static bool test_number_available[8] = {true, true, true, true};
static float OVERALL_SUN_SCALER = 2.f;

// let's draw a little sprite and see if any pixels are visible
static void	draw_sun_test_sprite()
{
	// We actually draw 4 sprites so the sun moves around in some clever way.
	for (DWORD i = 0; i < 4; i++)
	{
		// pick first slot after the vis tester class' slots
		const DWORD TEST_NUMBER = CXBOXVisibilityTester::NUM_SLOTS + i;
			
		// because visibility tests take time to come through, i'll have two available for each section
		if (test_number_available[i] || test_number_available[i + 4])
		{
			LT.GetDevice()->BeginVisibilityTest();

			const SHeightFieldProperties	&hfp=MAP.GetHFProperties();

			FVector		sun(hfp.SunPosX,hfp.SunPosY,hfp.SunPosZ);

			// make the sprite screen-aligned
			FMatrix vCamOrient = ENGINE.GetCamera()->GetOrientation();

			FVector x( vCamOrient.Row[0].X,  vCamOrient.Row[1].X,  vCamOrient.Row[2].X);
			FVector y(-vCamOrient.Row[0].Z, -vCamOrient.Row[1].Z, -vCamOrient.Row[2].Z);
			FVector z( vCamOrient.Row[0].Y,  vCamOrient.Row[1].Y,  vCamOrient.Row[2].Y);

			// work out something incredibly far away.
			sun.Normalise();

			// but we must make sure we're always testing a similar number of pixels. When the sun rotates near to the edge of the screen the number of
			// pixels increases thanks to the marvels of the perspective transform.

			// so we get the projected length of the sun vector along the view z axis
			float projected_sun_length = sun * z;

			sun *= SUN_FAR_AWAY / projected_sun_length;

			x *= SUN_TEST_SIZE;
			y *= SUN_TEST_SIZE;

			FVector centre = ENGINE.GetCamera()->GetPos() + sun;

			// now draw one of 4 sprites.
			if (i & 1) centre -= x; else centre += x;
			if (i & 2) centre -= y; else centre += y;

			CPARTICLEFVF_GTEX verts[4];

			verts[0].mPos = centre - x + y;
			verts[1].mPos = centre + x + y;
			verts[2].mPos = centre - x - y;
			verts[3].mPos = centre + x - y;

			verts[0].mDiffuse = SUN_COLOUR;
			verts[1].mDiffuse = SUN_COLOUR;
			verts[2].mDiffuse = SUN_COLOUR;
			verts[3].mDiffuse = SUN_COLOUR;

			verts[0].mU = 0.f;
			verts[1].mU = 0.f;
			verts[2].mU = 0.f;
			verts[3].mU = 0.f;

			verts[0].mV = 0.f;
			verts[1].mV = 0.f;
			verts[2].mV = 0.f;
			verts[3].mV = 0.f;

			RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);	
		
			LT.SRS(D3DRS_ALPHATESTENABLE,FALSE);
			RENDERINFO.STS(0, TSS_COLOROP, TOP_MODULATE);				
			RENDERINFO.Apply();

			LT.D3D_SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

			ENGINE.GetDefaultTexture()->SelectTexture();
			ENGINE.GetDefaultTexture()->SetupRenderMode();

			RENDERINFO.Apply();

			LT.D3D_DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(verts[0]));

			LT.SRS(D3DRS_ALPHATESTENABLE,TRUE);
			RENDERINFO.STS(0, TSS_COLOROP, TOP_MODULATE2X);				
			RENDERINFO.Apply();

			// that means we've used the test.
			if (test_number_available[i])
			{
				test_number_available[i] = false;
				LT.GetDevice()->EndVisibilityTest(TEST_NUMBER);
			}
			else
			{
				test_number_available[i + 4] = false;
				LT.GetDevice()->EndVisibilityTest(TEST_NUMBER + 4);
			}
		}

		// are any results pending?
		if (!test_number_available[i] && SUCCEEDED(LT.GetDevice()->GetVisibilityTestResult(TEST_NUMBER, &num_sun_pixels_drawn[i], NULL)))
		{
			// the test is done. we can go again.
			test_number_available[i] = true;
		}

		// are any results pending?
		if (!test_number_available[i + 4] && SUCCEEDED(LT.GetDevice()->GetVisibilityTestResult(TEST_NUMBER, &num_sun_pixels_drawn[i], NULL)))
		{
			// the test is done. we can go again.
			test_number_available[i + 4] = true;
		}
	}
}
#endif

//=============================================-===--==---=---- ---  --   -

static CVisibilityTester sea_visibility_tester;	// retail: its empty initializer is 0053e2d0 (placed once Render, which uses it, is back)

//=============================================-===--==---=---- ---  --   -

void CDXEngine::ShutdownRestartLoop()
{
	// We don't want the sea carrying over its visibility.
#if TARGET == XBOX
	sea_visibility_tester.MissATest = 1;
#endif
}


// This is stuff that can be drawn during the VBLANK if you get really silly.
static void draw_fairly_late_stuff(CPlayer *player, int viewpoint)
{
	//***********************************************************************
	//** Backup values for HUD
	RENDERINFO.GetWorld(&ENGINE.mWorldMatrices[viewpoint]);
	RENDERINFO.GetView(&ENGINE.mViewMatrices[viewpoint]);
	RENDERINFO.GetProjection(&ENGINE.mProjectionMatrices[viewpoint]);

	//*****************************************
	//** Render Cockpit

#ifdef DEV_VERSION
	if(!CLIPARAMS.mKillHUD)
#endif
	{
		if (player)
		{
			CBattleEngine *be = player->GetBattleEngine();
			
			if (be && (!be->IsDying()) && (player->ShouldRenderInternalCockpit() == TRUE))
			{
				// Turn the z-buffer back on for the sake of the sun visibility tester.
#if TARGET == XBOX
				// but why doesn't it work? I give up and check in.
#endif
				ENGINE.EnableAlpha();
	//			LT.SRS(D3DRS_LIGHTING,TRUE);
				RENDERINFO.SetLightingEnabled(TRUE);
				
				be->GetCockpit()->Render() ;
				RENDERQUEUE.Render();
				RENDERINFO.SetLightingEnabled(FALSE);
	//			LT.SRS(D3DRS_LIGHTING,FALSE);
			}
		}
	}
	// Render screen FX

	SCREENFX.Render();

	// Debug - draw reflection texture

#if TARGET == XBOX
	// late sun.
	//** SUN

	if (!(GAME.IsPaused()))
	{
		const SHeightFieldProperties	&hfp=MAP.GetHFProperties();
		
		if (hfp.VisibleSun)
		{
			// let's draw a little sprite and see if any pixels are visible
			// but draw it later.
			draw_sun_test_sprite();
		}
	}
#endif
}

// STAND-IN (spelling): retail copies the map's fog density through x87 before it reaches SetFogDensity (0053e5d7); Stuart's
// `float density = MAP.GetFogDensity();` in Render is 1183 bytes off.
static inline float MapFogDensity(CMap& map)
{
	float density = map.GetFogDensity();
	return density;
}

static FMatrix prev_vmatrix;

HRESULT CDXEngine::Render(int viewpoint)	// FUNCTION: 0x0053e2e0
{
	SetNearZ(DEFAULT_Z_NEAR);	// retail PC: every viewpoint starts from the default near plane (0053e307)

	if (!mCamera[viewpoint])
		return S_OK;  //! JCL - er..!!

	SelectViewpoint(viewpoint);

// Create Gamut

	float zoom = mCamera[mCurrentViewpoint]->GetZoom();
	FMatrix	matview;
	FVector	campos = mCamera[mCurrentViewpoint]->GetPos();
	GetViewMatrixFromCamera(mCamera[mCurrentViewpoint], &matview);

	// retail PC: the reflection pass mirrors the camera in the water plane and flips the view for the gamut (0053e365)
	FMatrix	gamutview;
	if (gMirrorPass)
	{
		campos.Z = MAP.GetWaterLevel() - (campos.Z - MAP.GetWaterLevel());
		matview.Row[0].Z = -matview.Row[0].Z;
		matview.Row[1].Z = -matview.Row[1].Z;
		matview.Row[2].Z = -matview.Row[2].Z;
		gamutview = matview;
		gamutview.Row[0] = -matview.Row[0];
	}
	else
	{
		gamutview = matview;
	}

	mGamut->MakeIt(&gamutview,2048,zoom*1.0f,zoom*mCamera[mCurrentViewpoint]->GetAspectRatio(),&campos,false);

// Calculate LODs as early as possible to 'avoid stalls'

	if (GAME.IsMultiplayer())
	{
		static int which_view_lod = 0;

		if (viewpoint == which_view_lod)
		{
			mLandscape->CalcLODs(mGamut, viewpoint);
		}

		which_view_lod ^= 1;
	}
	else
	{
		mLandscape->CalcLODs(mGamut, viewpoint);
	}

	// And then start doing drawing.

	STATE.UseDefault();
	RENDERQUEUE.Reset();
	RENDERINFO.SetView(&matview, &campos);

	// Render shadows
	SwitchAllLights(FALSE);					// don't want lights showing on shadows

	SHADOWS.RenderTextures();			// render shadows picked last frame

	{
		BOOL	bs_ok = PLATFORM.BeginScene();
		if (!bs_ok)
			return S_OK;
	}

	PLATFORM.SetViewport(&mViewport[mCurrentViewpoint]);

    SwitchAllLights(TRUE);

	// Move debris into position around the camera

	CDebris::ShuffleDebris(mCamera[mCurrentViewpoint]->GetPos());

	float wlevel;
	wlevel = MAP.GetWaterLevel();

		FMatrix bbori = mCamera[mCurrentViewpoint]->GetOrientation();

		float angle = atan2f(bbori.Row[1].X,bbori.Row[1].Y);
		CIMPOSTER::SetBillBoardAngle(angle);

		SetupLights();

		mLights->Process();

		// finish setting up the fog
		RENDERINFO.SetFogColour(MAP.GetFog());
		RENDERINFO.SetFogDensity(MapFogDensity(MAP));	// STAND-IN (spelling), see MapFogDensity

		//********************************************************
		//** SKY
		{
			float vfar = 1*(float)sqrt((SKY_HEIGHT*SKY_HEIGHT)+((SKY_CELL_SIZE*SKY_RAD)*(SKY_CELL_SIZE*SKY_RAD)));
			RENDERINFO.SetProjection(1.0f,vfar, zoom ,zoom*mCamera[mCurrentViewpoint]->GetAspectRatio());

			mKempyCube->Render();
		}

		//********************************************************
		//** LANDSCAPE
		float howfar = 700;
		RENDERINFO.SetProjection(mNearZ,howfar,mNearZ*zoom,mNearZ*zoom*mCamera[mCurrentViewpoint]->GetAspectRatio());

		if (mRenderLandscape)
		{
			mLandscape->Render(mGamut, viewpoint);

			SHADOWS.RenderLandscapeShadows();
			ReallySetDefaultMaterial();
		}
		SHADOWS.TestDraw();

		// retail PC: the water draws here unless OLD_WATER_TIMING asks for after the world, and never in the
		// reflection pass (0053e6b4, 0053e7cb)
		if (!OLD_WATER_TIMING.mValue && !gMirrorPass)
		{
			SetModulate2X(FALSE);
			RENDERINFO.SetLightingEnabled(FALSE);
			if (!mRenderReflections)
			{
				mWater->Render(wlevel);
			}
			RENDERINFO.SetLightingEnabled(TRUE);
			SetModulate2X(TRUE);
		}

		//********************************************************
		//** WORLD

		SHADOWS.ResetShads();				// clear shadow list

		LT.SRS(D3DRS_ALPHATESTENABLE,			TRUE );

		{
			SetMipFilterByGlobalToggle(0);										// retail PC (0053e728)
			LT.SSS_NoCache(0, D3DSAMP_MINFILTER,		D3DTEXF_ANISOTROPIC);	// aniso on
			LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY,	4);
			float bias = -1.0f;
			LT.SSS_NoCache(0, D3DSAMP_MIPMAPLODBIAS,	*((LPDWORD) (&bias)));

			WORLD.Render(mGamut,FALSE,SHADOWS.BlobShadowsEnabled());		// render objects

			LT.SSS_NoCache(0, D3DSAMP_MIPMAPLODBIAS,	0);
			SetMipFilterLinear(0);												// retail PC (0053e791)
			LT.SSS_NoCache(0, D3DSAMP_MINFILTER,		D3DTEXF_LINEAR);	// bilinear on
			LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY,	1);
		}

		CIMPOSTER::RenderAllImposters();	// Render cache of all imposter stuff

		// Render fast trees
		TREES.Render();

		// Update RTMesh render tracking stuff, kill redundant FoRs, etc

		CRTMesh::UpdateRenderLists();

		if (OLD_WATER_TIMING.mValue && !gMirrorPass)
		{
			SetModulate2X(FALSE);
			RENDERINFO.SetLightingEnabled(FALSE);
			if (!mRenderReflections)
			{
				mWater->Render(wlevel);
			}
			RENDERINFO.SetLightingEnabled(TRUE);
			SetModulate2X(TRUE);
		}

		DEBUGMARKERS.Render();				// Render debug markers

		if (SHADOWS.BlobShadowsEnabled())
		{
			LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
			SHADOWS.DrawAllBlobShadows();		// Render VB contents out
		}

		LT.SRS(D3DRS_ALPHATESTENABLE,			FALSE );

		// Update particle system
		if (!GAME.IsPaused())
		{
			if (!ENGINE.DebugNoParticles())
				if (viewpoint == 0)
					if (!gMirrorPass)		// retail PC (0053e86d)
						PARTICLE_MANAGER.Process(GAME.GetRenderFrameLength(), GAME.IsFirstFrame());
		}

		if (!DebugNoParticles() && !gMirrorPass)
		{
			if (viewpoint == 0)
			{
				PARTICLE_MANAGER.DoInterpolation(); // Needs to be after things are drawn and before the particles.
			}

			{
				RENDERINFO.SetFogEnabled(true);
				PARTICLE_MANAGER.RenderMeshes();	// needs to be here to share the lighting model..
			}
		}

		RENDERQUEUE.Render();				// Render all our batched stuff

// Render effects

	CAtmospherics::RenderAll();

	//********************************************************
	//** MAPWHO DEBUG
	if (mDrawDebugStuff & DRAW_MAP_WHO)
	{
		EnableAlpha() ;
		MAP_WHO.DrawMapWhoDebug() ;
	}

	//***********************************************************************
	//** SUN

	if (!(GAME.IsPaused()) && !gMirrorPass)
	{
		const SHeightFieldProperties	&hfp=MAP.GetHFProperties();

		if (hfp.VisibleSun)
		{
			float SUN_SCALE = 0.6f;

			FVector		sun(hfp.SunPosX,hfp.SunPosY,hfp.SunPosZ);

			sun *= SUN_SCALE;
			FVector	sunch = sun;
			FVector temp;

			sunch.Normalise();
			sunch *= 200.f;
			CLine line_of_sight(mCamera[mCurrentViewpoint]->GetPos(), mCamera[mCurrentViewpoint]->GetPos() + sunch) ;
			CWorldLineColReport wlcr ;
			CThing* to_ignore = NULL ;

			if (mPlayer[mCurrentViewpoint] && mPlayer[mCurrentViewpoint]->GetBattleEngine())
			{
				to_ignore = mPlayer[mCurrentViewpoint]->GetBattleEngine() ;
			}

			// retail PC: the rest of the arguments are explicit (0053ead2)
			if(WORLD.FindFirstThingToHitLine(line_of_sight,to_ignore, &wlcr, TRUE, TRUE, 1, 4, TRUE)==kCollideNothing)
			{
				PARTICLE_MANAGER.CreateEffect(mSunPD, NULL, mCamera[mCurrentViewpoint]->GetPos() + sun, FALSE, TRUE);
			}
		}
	}

	//***********************************************************************
	//** PARTICLE SYSTEM
	{
		RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);

		LT.D3D_SetMaterial(STATE.GetDefaultMaterial());

		if (!DebugNoParticles() && !gMirrorPass)
			PARTICLE_MANAGER.RenderOther();
	}

	draw_fairly_late_stuff(mPlayer[mCurrentViewpoint], viewpoint);

	PLATFORM.EndScene();

	return S_OK;
}

#ifdef NOT_YET_RECONSTRUCTED	// the Xbox copy-back path
//=============================================-===--==---=---- ---  --   -

#if TARGET == XBOX
float TEXMOVE = -.5f;
float TEXMOVE2 = -.5f;
float TEXMOVE3 = .5f;
float TEXMOVE4 = .5f;

float xaspect = 4.0f / 3.0f;
float yaspect = 1.f;

static FVector where_hit(const FVector &ray, const FMatrix &rot)
{
	// the ray has rotated a bit
	FVector newray = rot * ray;

	// but its destiny is still to intersect with the Z=1 plane.
	newray = newray / newray.Z;

	// but we want to know how far it is in the new world. So take the forward vector.
	FVector forward(0.f, 0.f, 1.f);

	// rotate that one by the same matrix.
	forward = rot * forward;

	// and see how far along that vector we project.
	float project = forward * newray;

	// and bury it in the return.
	newray.Z = project;

	return newray;
}

void copy_back_to_front(CCamera *cam)
{
	// start the process. This will set the old front buffer as the render target and the back buffer as texture 0
	LT.GetDevice()->Swap(D3DSWAP_BYPASSCOPY);

	struct	Vert
	{
		float	X, Y, Z, RHW;
		D3DCOLOR C;
		float	U, V;
		static	ULONG	FVF() { return D3DFVF_XYZRHW | D3DFVF_DIFFUSE| D3DFVF_TEX1; };
	};

	// these are the rays to the corners of the screen.
	FVector texture_corners[4];

	texture_corners[0].X = -xaspect;
	texture_corners[0].Y =  yaspect;
	texture_corners[0].Z =  1.f;

	texture_corners[1].X =  xaspect;
	texture_corners[1].Y =  yaspect;
	texture_corners[1].Z =  1.f;

	texture_corners[2].X =  xaspect;
	texture_corners[2].Y = -yaspect;
	texture_corners[2].Z =  1.f;

	texture_corners[3].X = -xaspect;
	texture_corners[3].Y = -yaspect;
	texture_corners[3].Z =  1.f;

	// Ooh ooh it all depends on the previous view matrix doesn't it?
	if (is_the_every_other)
	{
		// We've drawn the back buffer for the previous frame.
		// Now how has it changed for this frame?

		// We need to get the relative rotation.
		FMatrix new_mat;
		ENGINE.GetViewMatrixFromCamera(cam, &new_mat);

		FMatrix movement = prev_vmatrix * new_mat.Transpose();

		for (int i = 0; i < 4; i++)
		{
			texture_corners[i] = where_hit(texture_corners[i], movement);
		}
	}

	float SW = float(PLATFORM.GetScreenWidth ());
	float SH = float(PLATFORM.GetScreenHeight());
	float SW2 = SW * .5f;
	float SH2 = SH * .5f;

	Vert verts[4] = {{ 0,0,0.001f,0.9f,0xfffffff,TEXMOVE,TEXMOVE},
					{SW, 0,0.001f,0.9f,0xffffffff,SW + TEXMOVE2,TEXMOVE},
					{SW,  SH,0.001f,0.9f,0xfffffff,SW + TEXMOVE2,SH + TEXMOVE2},
					{0,SH,0.001f,0.9f,0xffffffff,TEXMOVE,SH + TEXMOVE2}};

	extern float back_buffer_scale;

	for (int i = 0; i < 4; i++)
	{
		verts[i].U = ( texture_corners[i].X * (SW2 / xaspect) + SW2) * back_buffer_scale;
		verts[i].V = (-texture_corners[i].Y * (SH2 / yaspect) + SH2) * back_buffer_scale;
		verts[i].RHW = 1.f / texture_corners[i].Z;
	}

	RENDERINFO.SetFVF(Vert::FVF());				
	LT.STS(0,D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.STS(0,D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	LT.SRS(D3DRS_ALPHABLENDENABLE,FALSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);	
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);	
	RENDERINFO.Apply();

	LT.D3D_DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, verts, sizeof(Vert));


	if (!is_the_every_other)
	{
		if (back_buffer_scale != 1.f)
			LT.GetDevice()->SetBackBufferScale(1.f, 1.f);

		// We've actually drawn this frame. We want to back it up to the other back buffer.
		Vert verts[4] = {{ 0,0,0.001f,0.9f,0xfffffff,TEXMOVE3,TEXMOVE3},
						{SW, 0,0.001f,0.9f,0xffffffff,SW + TEXMOVE4,TEXMOVE3},
						{SW,  SH,0.001f,0.9f,0xfffffff,SW + TEXMOVE4,SH + TEXMOVE4},
						{0,SH,0.001f,0.9f,0xffffffff,TEXMOVE3,SH + TEXMOVE4}};

		// Get the other back buffer (which is the new one now (I think))
		IDirect3DSurface8 *bb;

		LT.GetDevice()->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb);

		LT.D3D_SetRenderTarget(bb, NULL);

		LT.D3D_DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, verts, sizeof(Vert));

		// And release...
		bb->Release();

		// And go back to the front buffer for the HUD.
		LT.GetDevice()->GetBackBuffer(-1, D3DBACKBUFFER_TYPE_MONO, &bb);
		LT.D3D_SetRenderTarget(bb, NULL);

		// but then amusingly decrement the ref count
		bb->Release();

		if (back_buffer_scale != 1.f)
			LT.GetDevice()->SetBackBufferScale(back_buffer_scale, back_buffer_scale);
	}

	LT.STS(0,D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	LT.STS(0,D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);	
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);	
}
#endif

#endif	// NOT_YET_RECONSTRUCTED

bool crazy = false;		// 0089d688

extern bool	gInFrontEnd;			// 00679b40, defined in FrontEnd.cpp

HRESULT	CDXEngine::PostRender(CViewport *viewport)	// FUNCTION: 0x0053ecc0
{
	PLATFORM.BeginScene();

	PLATFORM.SetViewport(viewport);
	memcpy(&mCurrentViewport,viewport,sizeof(mCurrentViewpoint));

	// Render HUD

	if (!gMirrorPass)	// retail PC: no HUD in a mirrored pass (0053ecee)
	{
		HUD.Render();

		PLATFORM.SetViewport(viewport);
		memcpy(&mCurrentViewport,viewport,sizeof(mCurrentViewpoint));	

		// Draw the shared hud
		SINT	i,viewpoints=ENGINE.GetNumViewpoints();
		BOOL	render=FALSE;

		for (i=0;i<viewpoints;i++)
		{
			CPlayer *player=ENGINE.GetPlayerForViewpoint(i);

			if (player)
			{
				if ((GAME.GetCamera(i)->GetShowHUD()))
					render=TRUE;
			}
		}

		if (render)
			HUD.RenderBattleline(viewport);

		// retail PC: no point filtering here (0053ed7e)
		RENDERINFO.SetFogEnabled(false);
		RENDERINFO.SetLightingEnabled(false);
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_COLOROP,		TOP_MODULATE);
		RENDERINFO.STS(0, TSS_COLORARG2,	TA_DIFFUSE);
		RENDERINFO.SRS(RS_SRCBLEND,   BLEND_SRCALPHA );
		RENDERINFO.SRS(RS_DESTBLEND,  BLEND_INVSRCALPHA );
		RENDERINFO.Apply() ;
		
		gInFrontEnd = true;		// retail PC (0053ee07)
		GAME.GetMessageLog()->Render(viewport) ;
		GAME.GetLevelBriefingLog()->Render(viewport) ;
		GAME.GetPauseMenu()->Render() ;
		gInFrontEnd = false;

 		RENDERINFO.SetFogEnabled(false);
		RENDERINFO.SetLightingEnabled(false);
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
		SetMipFilterNone(0);				// retail PC (0053ee6b); no point filtering
		RENDERINFO.STS(0, TSS_MINFILTER, TEXF_LINEAR);
		RENDERINFO.STS(0, TSS_MAGFILTER, TEXF_LINEAR);
		RENDERINFO.SRS(RS_ZFUNC, ZFUNC_LEQUAL);
		RENDERINFO.Apply() ;

	}
	if (RENDERQUEUE.mUseQueue)
		PLATFORM.Font( FONT_DEBUG )->DrawText(0,40,0xFFFFFFFF,ToWCHAR("Render queue active"));

	RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
	RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
	RENDERINFO.SetFogEnabled(TRUE);
	RENDERINFO.SetLightingEnabled(TRUE);
	
	// Render screen overlays/blur
	
	HUD.RenderOverlay();
	
	if (mBlurAlpha>0)
	{
		// retail PC: no blur drawn here (0053ef2b)
		if (mBlurAlpha>0)
			mBlurAlpha-=15;
	}

	// Apply any delayed changes to blur/overlays
	
	if (mNewBlurAlpha!=-1)
	{
		mBlurAlpha=mNewBlurAlpha;
		mNewBlurAlpha=-1;
	}
	
	HUD.SwitchInOverlay();
	
	// Write our movie file if necessary
	
	CCAPTURE::Capture();


	// Finally, reset all the VBuffers
	
	CVBufTexture::ResetAll();
	
	RENDERINFO.SetLightingEnabled(TRUE);
	
	{
		// draw memory manager
		if (mDrawDebugStuff & DRAW_MEM_MANAGER)
		{
			MEM_MANAGER.PrintStats() ;	
		}

		GAME.DrawDebugStuff() ;

		// Draw Misc Game Stuff

		GAME.DrawGameStuff();
		// Draw console
		
		RENDERINFO.SetFogEnabled(false);
		CONSOLE.Render();
		RENDERINFO.SetFogEnabled(true);
	}

	// Draw the interface (help, pause menu, etc)
	if (!gMirrorPass)		// retail PC (0053efc4)
		GAMEINTERFACE.Render();

	PLATFORM.EndScene();

	mFirstFrame=FALSE;

	// Most things will be drawn by now.
	// Let's get rid of some vbuffer memory.
	CVBufTexture::DownSizeSome();

	if (crazy)
	{
		// let's lose even more
		CVBufTexture::ClearOut();
	}

	return(S_OK);
}



//=============================================-===--==---=---- ---  --   -
void	CDXEngine::SetBlurAlpha(int a,bool thisframe)	// FUNCTION: 0x0053f010
{
	if (thisframe)
		mBlurAlpha=a;
	else
		mNewBlurAlpha=a;
}
//=============================================-===--==---=---- ---  --   -
#ifdef NOT_YET_RECONSTRUCTED	// unreferenced in the retail build
CDetailLevel* CDXEngine::GetDetailLevel(int lev) 
{
	return mLandscape->GetDetailLevel(lev);
}
#endif	// NOT_YET_RECONSTRUCTED
//=============================================-===--==---=---- ---  --   -
void CDXEngine::ReallySetDefaultMaterial()
{
	LT.D3D_SetMaterial(STATE.GetDefaultMaterial());
}


//=============================================-===--==---=---- ---  --   -
void CDXEngine::SetModulate2X( const bool aSet )	// FUNCTION: 0x0053f040
{
	// retail PC (0053f040)
	if( aSet )
	{
		LT.ForceModulate(0);
	}
	else
	{
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	}
}
//=============================================-===--==---=---- ---  --   -
#ifdef RESBUILDER
void CDXEngine::AccumulateResources( CResourceAccumulator * accumulator )
{
	accumulator->AddTexture(mLandscape->GetDetailTexture(),RES_DONTFIXALPHA | RES_DOWNSAMPLE);
	accumulator->AddTexture(mDefaultTexture,RES_DOWNSAMPLE);
	accumulator->AddTexture(mTexOutline, RES_NOTONPS2);
	accumulator->AddTexture(mTexOpaque, RES_NOTONPS2);
#if TARGET == PC
	accumulator->AddTexture(mTexEdArrow, RES_NOTONPS2);
#endif

	mDefaultMesh->AccumulateResources(accumulator,RES_BASESET);

	mKempyCube->AccumulateResources(accumulator);
	mWater->AccumulateResources(accumulator);
	SCREENFX.AccumulateResources(accumulator);
	GAMEINTERFACE.AccumulateResources(accumulator);
	PARTICLE_SET.AccumulateResources(accumulator);
	CEngine::AccumulateResources(accumulator);
}
#endif

//******************************************************************************************
// The render-target swap's constructor (engine.h) for its callers outside DXLandscape.obj, which inlines its own
// (CGame::Render). It zeroes both surfaces, the body CMapWhoNode's constructor already has, so the linker folds it into
// that one (/OPT:ICF) and the annotation names the survivor.

// FUNCTION: 0x00491900
CRenderTargetSwap::CRenderTargetSwap()
{
	mRenderTarget = NULL;
	mDepthStencil = NULL;
}
//=============================================-===--==---=---- ---  --   -
#endif
// FUNCTION: 0x0053d220 init:ZERO_FVECTOR
// FUNCTION: 0x0053d240 init:ID_FMATRIX

// The empty initializer this object emits (a lone ret), placed by its .CRT$XCU order.
// FUNCTION: 0x0053e2d0 initorder:5
