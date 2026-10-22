// MotionController.cpp: reconstructed from MotionController.obj (004bad20..004bb289); Stuart's release has no copy.
// The base motion controller does nothing to a mesh's parts.

#include "common.h"
#define MOTIONCONTROLLER_CPP
#define MCDROPSHIP_CPP
#define MCGROUNDVEHICLE_CPP
#define MCWARSPITEDOME_CPP
#define MCDESTRUCTABLESEGMENTS_CPP
#define MCGROUNDATTACK_CPP
#define MCHIVEBOSS_CPP
#include "Motioncontroller.h"
#include "mcbuggy.h"
#include "game.h"
#include "BattleEngine.h"
#include "MCBattleEngine.h"
#include "MCGroundAttack.h"
#include "MCTentacle.h"
#include "DestructableSegmentsController.h"
#include "mesh.h"
#include "MCCannon.h"
#include "DebugLog.h"

// FUNCTION: 0x004bad20 init:ZERO_FVECTOR
// FUNCTION: 0x004bad40 init:ID_FMATRIX

// FUNCTION: 0x004bae10 ??_GCMotionController@@UAEPAXI@Z

//******************************************************************************************
CMotionController::CMotionController()	// FUNCTION: 0x004bae30
{
	mThing = NULL;
}

//******************************************************************************************
CMotionController::~CMotionController()	// FUNCTION: 0x004bae50
{
}

//******************************************************************************************
// Slot 4 is the same empty body; the linker folds GetPartTransform into it.
void CMotionController::Unknown3(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x004bae60
{
}

void CMotionController::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)
{
}

// The hive boss's controller is declared in its own file (MCHiveBoss.cpp); only its mesh tests here.
class CMCHiveBoss
{
public:
	static BOOL		CanMergePart(CMeshPart *part);								// 00498080
	static BOOL		HasCylinders(CMesh *mesh);									// 00498270
};

// Three controllers' merge tests allow every part; the linker folds them into one return-TRUE body (004fdc10). Which
// controllers they are is open: the turret test and part test the linker kept (CMCCannon's, CMCGroundVehicle's) stand
// for the controllers whose identical copies it folded.
BOOL	MergeAnyPart(CMeshPart *part);

//******************************************************************************************
// The mesh optimizer may fold a part into its neighbours only if no motion controller that could drive the mesh moves
// it.
BOOL CMotionController::CanOptimisePart(CMeshPart *part)	// FUNCTION: 0x004bae70
{
	if (CMCBuggy::HasWheelMotion(part->mBaseMesh) && !CMCBuggy::CanMergePart(part))
		return FALSE;
	if (CDestructableSegmentsMotionController::HasCore(part->mBaseMesh) && !CDestructableSegmentsMotionController::CanOptimisePart(part))
		return FALSE;
	if (CMCCannon::HasTurret(part->mBaseMesh) && !CMCGroundVehicle::CanMergePart(part))
		return FALSE;
	if (CMCCannon::HasTurret(part->mBaseMesh) && !CMCGroundVehicle::CanMergePart(part))
		return FALSE;
	if (CMCDropship::HasDoorAnimation(part->mBaseMesh) && !CMCDropship::CanMergePart(part))
		return FALSE;
	if (CMCMech::HasLegMotion(part->mBaseMesh) && !CMCMech::CanMergePart(part))
		return FALSE;
	if (CMCGroundAttack::HasTurret(part->mBaseMesh) && !CMCGroundAttack::CanMergePart(part))
		return FALSE;
	if (CMCCannon::HasTurret(part->mBaseMesh) && !CMCGroundVehicle::CanMergePart(part))
		return FALSE;
	if (CMCHiveBoss::HasCylinders(part->mBaseMesh) && !CMCHiveBoss::CanMergePart(part))
		return FALSE;
	if (CMCMech::HasLegMotion(part->mBaseMesh) && !CMCMech::CanMergePart(part))
		return FALSE;
	if (CMCTentacle::HasTentacle(part->mBaseMesh) && !CMCTentacle::CanMergePart(part))
		return FALSE;
	if (CMCMech::HasLegMotion(part->mBaseMesh) && !CMCMech::CanMergePart(part))
		return FALSE;
	if (CMCWarspiteDome::HasDome(part->mBaseMesh) && !CMCWarspiteDome::CanMergePart(part))
		return FALSE;
	return TRUE;
}

//******************************************************************************************
BOOL CMotionController::CanMergePart(CMeshPart *part)	// FUNCTION: 0x004bb040
{
	if (CMCBuggy::HasWheelMotion(part->mBaseMesh) && !CMCBuggy::CanMergePart(part))
		return FALSE;
	if (CDestructableSegmentsMotionController::HasCore(part->mBaseMesh) && !CDestructableSegmentsMotionController::CanMergePart(part))
		return FALSE;
	if (CMCCannon::HasTurret(part->mBaseMesh) && !CMCGroundVehicle::CanMergePart(part))
		return FALSE;
	if (CMCCannon::HasTurret(part->mBaseMesh) && !MergeAnyPart(part))
		return FALSE;
	if (CMCDropship::HasDoorAnimation(part->mBaseMesh) && !MergeAnyPart(part))
		return FALSE;
	if (CMCMech::HasLegMotion(part->mBaseMesh) && !CMCMech::CanMergePart(part))
		return FALSE;
	if (CMCGroundAttack::HasTurret(part->mBaseMesh) && !MergeAnyPart(part))
		return FALSE;
	if (CMCCannon::HasTurret(part->mBaseMesh) && !CMCGroundVehicle::CanMergePart(part))
		return FALSE;
	if (CMCHiveBoss::HasCylinders(part->mBaseMesh) && !CMCHiveBoss::CanMergePart(part))
		return FALSE;
	if (CMCMech::HasLegMotion(part->mBaseMesh) && !CMCMech::CanMergePart(part))
		return FALSE;
	if (CMCTentacle::HasTentacle(part->mBaseMesh) && !CMCTentacle::CanMergePart(part))
		return FALSE;
	if (CMCMech::HasLegMotion(part->mBaseMesh) && !CMCMech::CanMergePart(part))
		return FALSE;
	if (CMCWarspiteDome::HasDome(part->mBaseMesh) && !CMCWarspiteDome::CanMergePart(part))
		return FALSE;
	return TRUE;
}

//******************************************************************************************
// Whether any controller that keeps its parts apart could drive the mesh.
BOOL CMotionController::HasMovingParts(CMesh *mesh)	// FUNCTION: 0x004bb210
{
	if (CMCBuggy::HasWheelMotion(mesh))
		return TRUE;
	if (CMCMech::HasLegMotion(mesh))
		return TRUE;
	if (CMCMech::HasLegMotion(mesh))
		return TRUE;
	if (CMCHiveBoss::HasCylinders(mesh))
		return TRUE;
	if (CMCTentacle::HasTentacle(mesh))
		return TRUE;
	if (CMCMech::HasLegMotion(mesh))
		return TRUE;
	return FALSE;
}

//******************************************************************************************
// The motion controller's defaults that no code above defined. Each compiles to a body another function already has, so
// the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address each
// annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x00452da0
void CMotionController::AdjustFrame(CMeshPart *part, float *frame)
{
}

// FUNCTION: 0x00452da0
void CMotionController::Unknown6(CMeshPart *part, SINT *flags)
{
}

// FUNCTION: 0x004d6b20
SINT CMotionController::Unknown7(CMeshPart *part)
{
	return 0;
}

// FUNCTION: 0x005019c0
BOOL CMotionController::Unknown8()
{
	return FALSE;
}

// FUNCTION: 0x005019c0
BOOL CMotionController::Unknown9()
{
	return FALSE;
}

//******************************************************************************************
// Every part may merge on the PC. Returning TRUE compiles to a body others already have, so the linker folds it into
// that one (/OPT:ICF).

#pragma auto_inline(off)	// retail keeps the calls above
// FUNCTION: 0x004fdc10
BOOL	MergeAnyPart(CMeshPart *part)
{
	return TRUE;
}
#pragma auto_inline(on)
