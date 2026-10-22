// imposter.cpp: reconstructed from imposter.obj (00488800..00488abe); Stuart's release has no copy. Finding or making
// a mesh's imposter and keeping the list; DXImposter.cpp draws them. The allocation's __LINE__ is the retail one; the
// #line places it.

#include "common.h"
#define IMPOSTER_FULL
#include "mathsconst.h"
#include "imposter.h"
#include "RenderThing.h"
#include "thing.h"

// FUNCTION: 0x00488800 init:ZERO_FVECTOR
// FUNCTION: 0x00488820 init:ID_FMATRIX

//******************************************************************************************
// The imposter list, the loaded flag and the shared angle (0067a678..80): retail keeps these in the game-group object (between
// Hud.cpp and InfluenceMap.cpp's data); the rest of the class's statics are in DXImposter.cpp (008aa8b8..).
CDXImposter	*CDXImposter::sFirst = NULL;
BOOL		CDXImposter::sLoaded = FALSE;
float		CDXImposter::sAngle = 0;

CDXImposter	*CDXImposter::FindOrCreate(CInitRenderThing *init, CMesh *mesh, SINT views, SINT b, SINT frames, SINT mode, SINT alt)	// FUNCTION: 0x004888f0
{
	for (CDXImposter *i = sFirst; i; i = i->mNext)
	{
		if (stricmp(init->mName, i->mName) == 0)
		{
			if (mesh == i->mMesh && i->mAlt == alt && i->mViews == views && i->mNumFrames == frames &&
				i->mAnimated == mode && i->mUnknown30 == b)
				return i;
			TRACE("strange lack of imposter");
		}
	}

#line 41
	CDXImposter	*imp = new (MEMTYPE_IMPOSTER) CDXImposter;
	if (!imp)
		return NULL;

	imp->Link();
	imp->mMesh = mesh;
	strcpy(imp->mName, init->mName);
	imp->mViews = views;
	imp->mUnknown30 = b;
	imp->mAnimated = mode;
	imp->mNumFrames = frames;
	imp->mAlt = alt;
	imp->mUnknown38 = 0;
	return imp;
}

//******************************************************************************************
void	CDXImposter::Link()	// FUNCTION: 0x00488a70
{
	CDXImposter	*a = sFirst;
	if (a)
	{
		while (a->mNext)
			a = a->mNext;
		a->mNext = this;
	}
	else
		sFirst = this;
	mNext = NULL;
}

//******************************************************************************************
float	CDXImposter::GetFrameWidth(CThing *thing)	// FUNCTION: 0x00488aa0
{
	return mFrames[thing->GetRenderImposterNo()].mWidth;
}
