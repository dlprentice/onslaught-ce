// Geometry.obj interfaces recovered from the pristine retail calls and bodies.
// Names are descriptive reconstruction names; the omitted original source is not available.
#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "GeneralVolume.h"

// Six mutable endpoint components, then min X/Y, max X/Y, min/max Z; cdecl, bool in AL.
bool ClipSegmentAgainstAABB3D(float *startX, float *startY, float *startZ,
		float *endX, float *endY, float *endZ, const float *bounds);	// 00478160
BOOL TestSweptSphereAgainstTriangle(const FVector *a, const FVector *b, const FVector *c,
		const FVector *pos, const FVector *move, float radius, CCollisionReport *report);	// 00478510
BOOL TestSegmentAgainstTriangle(const FVector *a, const FVector *b, const FVector *c,
		const FVector *start, const FVector *end, CCollisionReport *report);	// 00478c20
BOOL PointInsideTrianglePrism(const FVector *point, const FVector *a, const FVector *b,
		const FVector *c, const FVector *normal);	// 00479020
FVector SelectClosestPointOnTriangleEdges(const FVector *a, const FVector *b,
		const FVector *c, const FVector *point);	// 00479200
float RaySphereEntryDistance(const FVector *start, const FVector *end, float radius);	// 00479630
float DistanceOutsideBox(const FVector *pos, const FVector *extent);	// 00479770

#endif
