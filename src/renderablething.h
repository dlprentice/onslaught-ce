// renderablething.h: IRenderableThing (vtable 005d9540: 29 pure slots, no destructor). Slot order from the
// retail tables of CThing, CComplexThing, CActor, CUnit and CBattleEngine (their overrides name the slots).
// Slot 21 is GetBoundingBox: CThing's override is thing.obj's 004f3d20, which Stuart's thing.cpp defines out of
// line, and CThing::DrawDebugCuboid (004f37c0) calls it through this table.
// Slot 24 is snow density (CRTMesh calls +60 at 004dce40/004dce4c); slot 27 selects
// the imposter frame (00488aad, 0055a66a). Earlier reconstructed names were swapped.
// These roles are instruction-backed; the missing original interface header is not recovered.

#ifndef RENDERABLETHING_H
#define RENDERABLETHING_H
#include "maths.h"

class CMesh;
class CBoundingBox;
#define CMESH CMesh
class CMotionController;

// The animation modes the mesh loader names (CMesh::SetAnimMode 004a5500: NONE, WALK, RUN, STAND, SHOOT, SHOOT1,
// SHOOT2, SHOOTWALK, FLY, HOVER, IDLE); GetAnimModeByName returns -1 for an unknown name.
enum EAnimMode
{
	AM_INVALID = -1,
	AM_NONE = 0,
	AM_WALK,
	AM_RUN,
	AM_STAND,
	AM_SHOOT,
	AM_SHOOT1,
	AM_SHOOT2,
	AM_SHOOTWALK,
	AM_FLY,
	AM_HOVER,
	AM_IDLE
};

class IRenderableThing
{
public:
	virtual	FVector			GetRenderPos() = 0;						// 0
	virtual	FMatrix			GetRenderOrientation() = 0;				// 1
	virtual	BOOL			GetRenderSelected() = 0;				// 2
	virtual	SINT			GetRenderColourOffset() = 0;			// 3
	virtual	DWORD			GetRenderColour() = 0;					// 4
	virtual float			GetRenderTurn() = 0;					// 5
	virtual float			GetRenderFrame() = 0;					// 6
	virtual SINT			GetRealAnimIndex() = 0;					// 7
	virtual	float			GetRenderRadius() = 0;					// 8
	virtual	FVector			GetRenderStartPos() = 0;				// 9
	virtual	FVector			GetRenderEndPos() = 0;					// 10
	virtual	SINT			GetRenderMesh() = 0;					// 11
	virtual	BOOL			GetRenderRotateShadow() = 0;			// 12
	virtual BOOL			GetRenderCanGoFlatShaded() = 0;			// 13
	virtual	BOOL			RenderUseHierarchy(SINT pn) = 0;		// 14
	virtual	float			GetRenderYaw() = 0;						// 15
	virtual float			GetRenderScale() = 0;					// 16
	virtual BOOL			GetRenderWithTransforms() = 0;			// 17
	virtual bool			GetCanBeImpostered() = 0;				// 18
	virtual int				GetImposterFrames() = 0;				// 19
	virtual EAnimMode		GetImposterAnimMode() = 0;				// 20
	virtual CBoundingBox	*GetBoundingBox() = 0;					// 21
	virtual EAnimMode		GetRenderAnimation() = 0;				// 22
	virtual float			GetRenderImposterBias() = 0;			// 23
	virtual	SINT			GetSnowDensity() = 0;				// 24
	virtual	BOOL			GetRequiresPolyBucket() = 0;			// 25
	virtual int				GetShouldDoHitEffect(SINT mesh_part_no) = 0;	// 26
	virtual int				GetRenderImposterNo() = 0;					// 27
	virtual CMotionController	*GetMotionController() = 0;			// 28
};

#endif
