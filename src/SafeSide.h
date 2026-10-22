// SafeSide.h: a side's safe area (SafeSide.obj, 004de0a0..004de1e6; Stuart's header is not in his release). It keeps
// itself on the world's safe-side list (+0xd0), which CUnit::FindNearestSafeSide walks (004fd910).

#ifndef SAFESIDE_H
#define SAFESIDE_H

#include "thing.h"

// 0x80 bytes; vtable 005dccdc.
DECLARE_THING_CLASS(CSafeSide, CComplexThing)
public:
	virtual	void	Shutdown();											// 004de1d0
	virtual void	Init(CInitThing *init);								// 004de190
	virtual SINT	GetAllegiance() { return mAllegiance; }				// 66

	SINT			mAllegiance;										// +0x7c
};

#endif
