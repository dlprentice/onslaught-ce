// mathsconst2.h: a second copy of the maths constants, included after a file's code. Several objects (MapWhoEntry,
// PCRTID, DXMemoryManager, Lights, LandscapeVB, SimpleBuilding, BomberAI,
// GroundAttackAircraft) are followed by a separately stored pair of maths-constant initializers (.CRT$XCU order).
// The pairs after DXClouds, SoundManager and BattleEngineWalkerPart proved to open the next objects (DXCockpit,
// SoundMaterials and BattleLine), whose code follows them; these
// may likewise open objects of their own, the ones followed by unplaced code most of all. Open question; this header
// places the bytes meanwhile.

#ifndef MATHSCONST2_H
#define MATHSCONST2_H

#include "maths.h"

static const FVector ZERO_FVECTOR2(0.0f, 0.0f, 0.0f);
#ifndef PARTICLESET_CPP		// ParticleSet's second pair is the vector alone (.CRT$XCU)
static const FMatrix ID_FMATRIX2(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
#endif

#endif
