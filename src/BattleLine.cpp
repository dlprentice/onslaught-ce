// BattleLine.cpp: the platform-independent part of the HUD's battle line (CDXBattleLine::Update, 00414cb0). Not in
// Stuart's release; reconstructed from its own object between BattleEngineWalkerPart.obj and Boat.obj, whose maths
// constants open it (00414bc0, 00414be0).

#include "common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include "mathsconst.h"	// FUNCTION: 0x00414bc0 init:ZERO_FVECTOR
						// FUNCTION: 0x00414be0 init:ID_FMATRIX
#include "BattleLine.h"
#include "world.h"
#include "thing.h"
#include "squad.h"
//******************************************************************************************
// the overlay points: every objective in yellow, and in red the squads that answer Unknown4e6610 with 1
void	CDXBattleLine::Update()	// FUNCTION: 0x00414cb0
{
	mNumOverlayPoints = 0;
	for (CThing *t = WORLD.GetObjectiveThingNB().First(); t; t = WORLD.GetObjectiveThingNB().Next())
		AddOverlayPoint(t->GetPos().X, t->GetPos().Y, 0xffff00);
	for (CSquad *s = WORLD.GetSquadNB().First(); s; s = WORLD.GetSquadNB().Next())
		if (s->Unknown4e6610() == 1)
			AddOverlayPoint(s->GetPos().X, s->GetPos().Y, 0xff0808);
}
