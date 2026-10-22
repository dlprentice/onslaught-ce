// DXParticleManager.cpp: reconstructed from DXParticleManager.obj (0054f5c0..0054fa6f); Stuart's release has no copy.
// The PC particle manager: the global, its lifetime, and the render passes' device state.

#include "common.h"
#include "mathsconst.h"
#include "DX.h"
#include "ParticleManager.h"
#include "ParticleDescriptor.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "engine.h"
#include "Camera.h"

// FUNCTION: 0x0054f5c0 init:ZERO_FVECTOR
// FUNCTION: 0x0054f5e0 init:ID_FMATRIX

CParticleManager	PARTICLE_MANAGER;					// 009c63e8
// FUNCTION: 0x0054f6b0 init:PARTICLE_MANAGER
// FUNCTION: 0x0054f6d0 exit:PARTICLE_MANAGER

// The global's initializer and exit thunk call these out of line (0054f6b5, 0054f6d5).
#pragma auto_inline(off)

//******************************************************************************************
CParticleManager::~CParticleManager()	// FUNCTION: 0x0054f6e0
{
	while (mParticles)
	{
		CParticle	*next = mParticles->mNext;

		mParticles->Destroy();
		mParticles->mNext = mFreeParticles;
		mFreeParticles = mParticles;
		mParticles = next;
	}

	ClearOwnerBacklinks();
	PruneDeadOwnerLinks();
	CleanupHandles();

	delete mSystem;
}

//******************************************************************************************
CParticleManager::CParticleManager()	// FUNCTION: 0x0054f740
{
	mParticles = NULL;
	mTime = 0;
	mFrame = 0;
	mSystem = NULL;
	mFreeParticles = NULL;
}

#pragma auto_inline(on)

//******************************************************************************************
void	CParticleManager::SetRenderStates()	// FUNCTION: 0x0054f760
{
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	LT.SetModulate(0);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
}

// STAND-IN (inline budget): these three inline helpers preserve the retail call boundaries; their names and grouping are
// ours. Written into RenderOther, the basis setup is 440 bytes off retail (and Vec3::Cross and the basis transform fall
// out of the file), the descriptor loop 239 and the state restore 281. The basis setup leaves both matrix products, the
// cross product and both normalizations out of line.
static inline void SetParticleCameraBasis(CParticleManager &manager)
{
	manager.mRight = FVector(1.f, 0.f, 0.f);
	manager.mUp = FVector(0.f, 1.f, 0.f);

	FMatrix	ori = ENGINE.GetCamera()->GetOrientation();
	FVector	up = ori * FVector(0.f, 1.f, 0.f);
	FVector	back = ori * FVector(0.f, 0.f, -1.f);
	manager.mRight = up ^ back;
	manager.mRight.Normalise();
	manager.mUp = back;
	manager.mUp.Normalise();
}

static inline void DrawOtherParticleDescriptors()
{
	for (CParticleDescriptor *pd = (CParticleDescriptor *)PARTICLE_SET.mFirstNode; pd; pd = pd->mUnknown40)
		if (pd->GetType() != PD_MESH)
			pd->Unknown23(true);

	CPARTICLETEXTURE::RenderAll();
}

static inline void RestoreOtherParticleStates()
{
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, TRUE);
	RENDERINFO.SetLightingEnabled(true);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	RENDERINFO.SetFogEnabled(true);
	RENDERINFO.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
	RENDERINFO.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

//******************************************************************************************
// The sprites and trails, facing the camera, after the meshes.
void	CParticleManager::RenderOther()	// FUNCTION: 0x0054f7e0
{
	RENDERINFO.SetLightingEnabled(false);
	RENDERINFO.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	RENDERINFO.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	RENDERINFO.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	RENDERINFO.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, FALSE);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, FALSE);

	SetParticleCameraBasis(*this);
	DrawOtherParticleDescriptors();

	RestoreOtherParticleStates();
}
