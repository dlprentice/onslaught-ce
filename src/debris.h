// debris.h: the debris around the camera (only what matched files need so far).

#ifndef DEBRIS_H
#define DEBRIS_H

#ifdef DEBRIS_CPP	// debris.cpp's view; CDXEngine::Render only calls ShuffleDebris
#include "thing.h"

// a tuft of grass kept near the camera (vtables 005daf10, IRenderableThing 005dae98; the list head 0066eb78)
DECLARE_THING_CLASS(CDebris, CComplexThing)
public:
	virtual			~CDebris();									// 00441320: off the list
	virtual void	Init(CInitThing *init);						// 004411a0
	virtual void	Render(DWORD flags = 0);					// 004413a0: faded by distance
	virtual void	RenderImposter();							// 00441420
	static void		ShuffleDebris(FVector pos);				// 00441490: wraps each tuft around the camera

	CDebris			*mNext;					// +0x7c
	float			mDistance;				// +0x80: from the camera, set by ShuffleDebris

	static CDebris	*sFirst;				// 0066eb78
};
#else
class CDebris
{
public:
	static void	ShuffleDebris(FVector pos);			// 00441490 (CDXEngine::Render 0053e56c: the position by value)
};
#endif

#endif
