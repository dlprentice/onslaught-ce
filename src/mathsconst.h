// mathsconst.h: the per-file maths constants. Every object that includes this (through debuglog.h or thing.h, or directly)
// emits their two dynamic initializers where the include falls, usually opening the object (ParticleSet 004cbed0,
// triangulate 004f7080); objects that do not include it (chunker.obj) have none.

#ifndef MATHSCONST_H
#define MATHSCONST_H

#include "maths.h"

#if defined(DXLANDSCAPE_CPP) || defined(DXKEMPYCUBE_CPP)
// DXLandscape.obj defines the pair as exported globals (DXLandscape.cpp) and DXKempyCube.obj uses them: retail's sky
// cube reads DXLandscape's matrix (008aa8d8) and has no maths initializers of its own; the pair's initializers
// (00544360/00544380) open DXLandscape.obj, after the cube's code (placement evidence).
extern const FVector ZERO_FVECTOR;
extern const FMatrix ID_FMATRIX;
#else
#ifndef DXSURF_CPP				// DXSurf.obj has neither: 00556b80 is DXTexture's TEXTURE_ZERO initializer
static const FVector ZERO_FVECTOR(0.0f, 0.0f, 0.0f);
#endif
// Twelve retail objects register ZERO_FVECTOR but no ID_FMATRIX (.CRT$XCU), and retail holds no static copy of the
// matrix for them: their defines (each on a line the file already had, so no __LINE__ moves) leave the matrix out.
#if !defined(ATMOSPHERICS_CPP) && !defined(BATTLEENGINECONFIGURATIONS_CPP) && !defined(DXMEMORYMANAGER_CPP) && \
	!defined(DXSURF_CPP) && !defined(LANDSCAPETEXTURE_CPP) && !defined(LANDSCAPEVB_CPP) && !defined(PCCONTROLLER_CPP) && \
	!defined(PCMEMORYCARD_CPP) && !defined(PCPLATFORM_CPP) && !defined(MAPTEX_CPP) && !defined(MIXERMAP_CPP) && \
	!defined(TEXTURE_CPP)
static const FMatrix ID_FMATRIX(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
#endif
#endif

#endif
