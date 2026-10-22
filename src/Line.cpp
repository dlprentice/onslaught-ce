// Line.cpp: CLine's virtual collision interface, reconstructed from vtable 005d8bfc.
// Slots 1, 2, 3 and 5 fold to 004098c0: swap the movement records and ask the other volume to test this line.
// The line-against-line test and type query also share their retail bodies with unrelated classes.
#include "common.h"
#include "Line.h"
#include "MeshCollisionVolume.h"
#include "cylinder.h"
#include "Sphere.h"
// Line.obj's own code is all folded into identical bodies elsewhere (ICF), so the object shows only what follows
// Lights.obj in the link: its maths pair, four empty initializers and the out-of-line FVector::operator-=
// (00490900, which CPDMesh 004c512d and CSphere 004e53c4 call). The file sits between Lights and Map in the link's
// alphabetical order; that placement is an inference from the order, not from a string.
#include "mathsconst.h"	// FUNCTION: 0x004907d0 init:ZERO_FVECTOR
						// FUNCTION: 0x004907f0 init:ID_FMATRIX

// Four statics with empty constructors (a lone ret each, placed by their .CRT$XCU order). Open question: their
// names, and whether each is an FVector or an FMatrix; either type compiles to the same initializer.
static FVector	sUnknown0;
// FUNCTION: 0x004908c0 initorder:3
static FVector	sUnknown1;
// FUNCTION: 0x004908d0 initorder:4
static FVector	sUnknown2;
// FUNCTION: 0x004908e0 initorder:5
static FVector	sUnknown3;
// FUNCTION: 0x004908f0 initorder:6

// FUNCTION: 0x004098c0
BOOL CLine::CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report)
{
	return other->CollideWithLine(move, (CCollisionMove *)a, this, report);
}

// FUNCTION: 0x004098c0
BOOL CLine::CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report)
{
	return other->CollideWithLine(move, (CCollisionMove *)a, this, report);
}

// FUNCTION: 0x004098c0
BOOL CLine::CollideWithSphere(void *a, CCollisionMove *move, CSphere *other, CCollisionReport *report)
{
	return other->CollideWithLine(move, (CCollisionMove *)a, this, report);
}

// FUNCTION: 0x004098b0
BOOL CLine::CollideWithLine(void *a, CCollisionMove *move, CLine *other, CCollisionReport *report)
{
	return FALSE;
}

// FUNCTION: 0x004098c0
BOOL CLine::Collide(void *a, CCollisionMove *move, CGeneralVolume *other, CCollisionReport *report)
{
	return other->CollideWithLine(move, (CCollisionMove *)a, this, report);
}
