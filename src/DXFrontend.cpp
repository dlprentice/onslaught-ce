#include	"Common.h"

#define DXFRONTEND_CPP
#define ASYNCCACHE_FULL	// ~CDXFrontEnd destroys CFEPGoodies' loader (00540b8b)

#ifdef _DIRECTX

#include	"Frontend.h"
#include	"Platform.h"
#include	"SoundManager.h"

#include	"State.h"
#include	"Engine.h"
#include	"D3DUtils.h"
#include	"SpriteRenderer.h"
#include	"MeshRenderer.h"
#include	"RenderInfo.h"
#include	"ParticleManager.h"

//*********************************************************************************
CDXFrontEnd	FRONTEND;

//*********************************************************************************
void	CDXFrontEnd::RenderParticles()	// FUNCTION: 0x00540c30
{

	// setup lights & camera
	RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);

	CLight		light;
	RENDERINFO.SetAmbient(0x00808080);

	FVector sunlight = FVector( -5000, 5000, 1500.0f ); 
	sunlight.Normalise();

#if TARGET == PS2
	FVector3	col1 = FVector3(0.8f, 0.4f, 0.2f);
	FVector3	col2 = FVector3(0.2f, 0.4f, 0.8f);
	FVector3	col3 = FVector3(0.4f, 0.4f, 0.4f);

	col1 *= 0.6f;
	col2 *= 0.6f;
	col3 *= 0.6f;

	col3 = ( col1 + col2 ) / 2;

	RENDERINFO.SetAmbient( ((int)(col3.X*255))<<16 | ((int)(col3.Y*255))<<8 | ((int)(col3.Z*255)) );

	light.Init( LIGHT_DIRECTIONAL, sunlight, col1.X - col3.X, col1.Y - col3.Y, col1.Z - col3.Z );
	RENDERINFO.SetLight( 0, &light );
	RENDERINFO.SetLightEnable( 0, TRUE );

	RENDERINFO.SetLightingEnabled(TRUE);

	RENDERINFO.SetGlobalAlpha( 255 );
#else
	RENDERINFO.SetAmbient(0);

	FVector3	col1 = FVector3(0.8f, 0.4f, 0.2f);
	FVector3	col2 = FVector3(0.2f, 0.4f, 0.8f);
	FVector3	col3 = FVector3(0.4f, 0.4f, 0.4f);

	col1 *= 0.6f;
	col2 *= 0.6f;
	col3 *= 0.6f;

	light.Init( LIGHT_DIRECTIONAL, sunlight, col1.X, col1.Y, col1.Z);
	RENDERINFO.SetLight( 0, &light );
	RENDERINFO.SetLightEnable( 0, TRUE );
	
	sunlight.X = -sunlight.X;
	light.Init( LIGHT_DIRECTIONAL, sunlight, col2.X, col2.Y, col2.Z);
	RENDERINFO.SetLight( 1, &light );
	RENDERINFO.SetLightEnable( 1, TRUE );

	sunlight = FVector(0, 1000.f, 200.f);
//	sunlight = FVector(0, 0, 1000.f);
	sunlight.Normalise();
	light.Init( LIGHT_DIRECTIONAL, sunlight, col3.X, col3.Y, col3.Z);
	RENDERINFO.SetLight( 2, &light );
	RENDERINFO.SetLightEnable( 2, TRUE );
	RENDERINFO.SetLightingEnabled(TRUE);
#endif

	// retail PC: the fog state is left alone (00540ed2)

	PARTICLE_MANAGER.DoInterpolation();
	PARTICLE_MANAGER.RenderMeshes();

	RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);

	LT.D3D_SetMaterial(STATE.GetDefaultMaterial());
	RENDERINFO.SetLightingEnabled(FALSE);
	LT.SRS( D3DRS_ALPHATESTENABLE, FALSE ); 


//h	PARTICLE_MANAGER.DoInterpolation();
	PARTICLE_MANAGER.RenderOther();



	LT.SRS( D3DRS_ALPHATESTENABLE, TRUE ); 
}

#ifdef NOT_YET_RECONSTRUCTED	// unreferenced in the retail build
//*********************************************************************************
void	CDXFrontEnd::RenderStaticScreen(CTEXTURE *screen,int alpha, DWORD col)
{
	if (!screen)
	{
		PLATFORM.Font( FONT_DEBUG )->DrawText(16,16,0xFFFFFFFF,ToWCHAR("Frontend screen not found..."));
		return;
	}
	
#if TARGET == XBOX
	LT.STS(0,D3DTSS_MINFILTER,D3DTEXF_POINT);	// D3DTEXF_NONE is an invalid state on the xbox
	LT.STS(0,D3DTSS_MAGFILTER,D3DTEXF_POINT);
#else
	LT.STS(0,D3DTSS_MINFILTER,D3DTEXF_NONE);
	LT.STS(0,D3DTSS_MAGFILTER,D3DTEXF_NONE);
#endif
	
	LT.STS(0,D3DTSS_ADDRESSU,D3DTADDRESS_CLAMP);
	LT.STS(0,D3DTSS_ADDRESSV,D3DTADDRESS_CLAMP);	
	
	LT.SRS(D3DRS_ALPHATESTENABLE,FALSE);
	
	CSPRITERENDERER::DrawColouredSprite(0,0,0.1f,screen,(alpha<<24) | (col & 0x00ffffff),1.0f,1.0f);

	LT.STS(0,D3DTSS_MINFILTER,D3DTEXF_LINEAR);
	LT.STS(0,D3DTSS_MAGFILTER,D3DTEXF_LINEAR);

	LT.STS(0,D3DTSS_ADDRESSU,D3DTADDRESS_WRAP);
	LT.STS(0,D3DTSS_ADDRESSV,D3DTADDRESS_WRAP);
}

#endif

//*********************************************************************************

BOOL	CDXFrontEnd::RenderStart()	// FUNCTION: 0x00540f70
{
	STATE.UseDefault();
	RENDERINFO.Reset();

	// Setup Renderstates		(retail PC: no second UseDefault, 00540f82)
	PLATFORM.ClearScreen(0x001f1f3f);

	LT.SRS(D3DRS_ALPHABLENDENABLE,TRUE);	

	return(CFrontEnd::RenderStart());
}

//*********************************************************************************
void	CDXFrontEnd::RenderEnd(BOOL started)	// FUNCTION: 0x00540fb0
{
	if (started)
	{
		// render common stuff
		RenderParticles();		
	}

	CFrontEnd::RenderEnd(started);
}



#endif
// FUNCTION: 0x00540a40 init:ZERO_FVECTOR
// FUNCTION: 0x00540a60 init:ID_FMATRIX
// FUNCTION: 0x00540b30 init:FRONTEND
// FUNCTION: 0x00540b50 exit:FRONTEND
// FUNCTION: 0x00540bf0 ??0CDXFrontEnd@@QAE@XZ
// FUNCTION: 0x00540b60 ??1CDXFrontEnd@@UAE@XZ
// FUNCTION: 0x00540c10 ??_GCDXFrontEnd@@UAEPAXI@Z
