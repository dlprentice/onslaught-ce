// linklib/provisional.cpp: PROVISIONAL, link-only definitions for symbols the matched source references but whose
// retail owner, layout or body is still open. tools/link.py compiles and links this so the executable runs; nothing
// here is part of the decompilation's score, and each entry's open question is in the link plan's stage-5 notes.

#define MATHSCONST_H	// no per-file maths constants: retail has no initializers for these link-only definitions (.CRT$XC 714)
#include "..\src\common.h"
#include "..\src\maths.h"
extern const FVector ZERO_FVECTOR;		// declared only (headers name them); DXLandscape.obj exports the pair
extern const FMatrix ID_FMATRIX;
#include "..\src\System.h"
#include "..\src\weapon.h"

class CDXTexture;

// SYSTEM (00896ca4): CSystem's object. Retail keeps it between pcsoundmanager.obj's and Credits.obj's data, far from
// System.obj's own (0083d3d0..), so its defining object is open.
CSystem		SYSTEM;

// mesh.cpp's gNoMeshPartOptimise reads 00662f48, which is CLIPARAMS.mNoMeshPartReduction (+0x190). Until mesh.cpp's
// rows settle and it spells the field, this separate flag stays clear: parts are optimised unless the command line
// asks otherwise, which this stand-in does not see.
BOOL		gNoMeshPartOptimise;

// CWeapon::GetReadiness has no retail copy (CBattleEngine::GetWeaponReadiness, its only caller chain, is unreferenced
// and /OPT:REF drops it); this body only satisfies the reference.
float	CWeapon::GetReadiness()
{
	return GetCharge();
}
// STAND-IN (link layout): retail's 1608 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from tools/alignedeq.py --data-events).
char gLinkBss_provisional[1608] = { 0 };
