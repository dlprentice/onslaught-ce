// meshrenderer.h: the mesh renderer (only what matched files need). The render mode is a static at 00704e48
// (CFrontEnd::RenderStart sets it to R_Normal, 00468695).

#ifndef MESHRENDERER_H
#define MESHRENDERER_H
#include "maths.h"

enum ERenderMode
{
	R_Normal = 0
};

class CMesh;
class CMeshPart;
class CRenderThing;
class IRenderableThing;

extern SINT	MESH_ALPHA;						// 0063012c (255 by default)
extern BOOL	CUTSCENE_RENDERING;				// 00704e60

class CMeshRenderer
{
public:
	static void	SetRenderMode(ERenderMode mode) { mRenderMode = mode; }
	static void	SetRenderAlpha(SINT alpha) { MESH_ALPHA = alpha; }	// Stuart's name; inline (CBattleEngine::Render 0040d560)
	static ERenderMode	GetRenderMode() { return mRenderMode; }
	static SINT	GetRenderAlpha() { return MESH_ALPHA; }
	static BOOL	GetCutsceneRendering() { return CUTSCENE_RENDERING; }

	// A mesh and its lower LODs (when lods): through the render thing's cached pose when it has one, else part by
	// part from the root (CRTMesh::Render calls it, 004dce20).
	static void	Render(FVector *pos, FMatrix *ori, CMesh *mesh, IRenderableThing *thing, CRenderThing *rt, SINT a, bool lods, SINT b);	// 004b6260
#if defined(PDSIMPLESPRITE_CPP) || defined(MESHRENDERER_CPP)	// only these, so the other objects keep their declaration counts
	// one part at a pose (CPDMesh::Unknown11 004c8fe0; cdecl, seven arguments; name ours)
	static void	RenderPart(FVector *pos, FMatrix *ori, class CMeshPart *part, IRenderableThing *thing, CRenderThing *rt, SINT a, SINT b);	// 004b6350
#endif
	static void	RenderPart(FVector pos, FMatrix ori, CMeshPart *part, IRenderableThing *thing, CRenderThing *rt, SINT a, SINT b);	// 004b5ad0
	static void	RenderPose(FVector pos, FMatrix ori, CMesh *mesh, IRenderableThing *thing, CRenderThing *rt, SINT b);	// 004b5e80
#if defined(MESHRENDERER_CPP) || defined(DXMESHRENDERER_CPP)
	// the renderer core: one part drawn (in the object after DXMemoryManager.obj; cdecl, nine arguments; name ours)
	static void	RenderMeshCore(FVector pos, FMatrix *ori, CMeshPart *part, IRenderableThing *thing, CRenderThing *rt, SINT a, SINT b, class CMotionController *mc, BOOL collision);	// 00549570
#endif

	static ERenderMode	mRenderMode;
};

#define CMESHRENDERER CMeshRenderer

#if defined(MESHRENDERER_CPP) || defined(DXMESHRENDERER_CPP)
DWORD	PackNormal(const FVector *n, float alpha);	// 004b52c0: a normal as a colour
#endif

#endif
