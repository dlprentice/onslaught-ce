// rtmesh.h: CRTMesh, the mesh render thing (vtable 005deb1c, 28 slots). Reconstructed from rtmesh.obj
// (004dc280..004dd5bf). UpdatePosition is an ordinary out-of-line function with an empty body:
// CThing::UpdatePosition (004f39b0) jumps to it, and the linker folded it into the shared empty function at
// 0040c640.

#ifndef RTMESH_H
#define RTMESH_H

#include "RenderThing.h"
#include "mesh.h"

class CParticleEffectLink;

class CRTMesh : public CRenderThing
{
public:
	virtual			~CRTMesh();										// 004dc950
	virtual void	Init(CInitRenderThing *init);					// 004dc370
	virtual void	Render(DWORD flags);							// 004dcba0
	virtual void	RenderImposter();								// 004dcea0
	virtual CBoundingBox	*GetBoundingBox()	{ return mMesh->mBoundingBox; }	// 004de060 (emitted elsewhere)
	virtual CBoundingBox	*GetPartBoundingBox(int part)	{ return mMesh->mParts[part]->mBoundingBox; }	// 004db8a0 (emitted elsewhere)
	virtual float	GetRTRadius()		{ return mRadius; }			// 004dcaf0
	virtual void	GetRTEmitter(char *name, SINT index, FVector &pos, FMatrix &ori, BOOL a, BOOL b);	// 004dd160
	virtual FVector	GetRandomSurfacePoint(SINT frame);			// 004dd400 (slot 8; returned by value, ret 8)
	virtual CMESH	*GetRTMesh()		{ return mMesh; }		// 004de070 (emitted elsewhere)
	virtual void	CreatePolyBuckets();							// 004dcb90
	virtual float	GetFrameIncrement(EAnimMode am, int* realindex)	{ return mMesh ? mMesh->GetFrameIncrement(am, realindex) : 0.0f; }	// 004db9f0 (emitted elsewhere)
	virtual void	GetMovieCameraPosition(FMatrix *ori, FVector *pos, float *fov, IRenderableThing *thing);	// 004dd1e0
	virtual SINT	GetMovieCameraNumber(IRenderableThing *thing);			// 004dd370
	virtual SINT	FindEffect(CMeshPart *part);							// 004dd510
	virtual bool	GetEffectActive(SINT n);						// 004dd540
	virtual SINT	GetEffectPart(SINT n);							// 004dd550
	virtual SINT	GetEffectId(SINT n);							// 004dd560
	virtual void	*GetEffect(SINT n);								// 004dd570
	virtual void	SetEffectActive(SINT n, bool on);				// 004dd580
	virtual void	SetEffect(SINT n, void *effect);				// 004dd5a0

	virtual BOOL	Unknown27()		{ return TRUE; }		// 27 (the shared 004014a0)

	// the tree and object detail the options set (cdecl; CTreeDetail 004cef50, 004cef60)
	static void		SetQualityLevel(int level);						// 004dd6b0
	static int		GetQualityLevel();								// 004dd770

	void			UpdatePosition();

	static void		UpdateRenderLists();							// 004dd0c0 (Stuart's DXEngine.cpp)

	CMESH			*mMesh;					// +0x14
	class CDXImposter	*mImposter;			// +0x18
	UBYTE			*mImposterState;		// +0x1c
	float			mRadius;				// +0x20
	SINT			mUnknown24;				// +0x24
	SINT			mNumEffects;			// +0x28
	bool			*mEffectActive;			// +0x2c
	CParticleEffectLink	**mEffects;			// +0x30
	class CParticleDescriptor	**mEffectTypes;	// +0x34
	SINT			*mEffectParts;			// +0x38
	CMeshPart		**mEffectPartPointers;		// +0x3c
	SINT			*mEffectIds;			// +0x40
	CRTMesh			*mPrevRendered;			// +0x44
	CRTMesh			*mNextRendered;			// +0x48
	SINT			mUnknown4C;				// +0x4c: CRTMesh is 0x50 bytes (CreateRenderThing 005165b3)

	static CRTMesh	*sRenderedLastFrame;	// 0083cd5c
	static CRTMesh	*sRenderedThisFrame;	// 0083cd60
};

#endif
