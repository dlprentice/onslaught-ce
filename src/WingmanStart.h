// WingmanStart.h: CWingmanStart, where a level's wingman starts (RTTI: a CComplexThing, vtable 005dcb58, 0x440 bytes;
// oids.obj constructs it and emits its inline members). Reconstructed with WingmanStart.obj (0050a2b0..0050a744).

#ifndef WINGMANSTART_H
#define WINGMANSTART_H

#include "thing.h"
#include "InitThing.h"
#include "activereader.h"

class CWingmanStart : public CComplexThing
{
public:
	typedef CComplexThing SUPERTYPE;
	virtual char	*_GetClassName() { return "CWingmanStart"; }		// 7 (004bfc20)
	virtual int		_GetClassID() { return 38; }						// 8 (004bfc30); oids.cpp's OID_CWingmanStart

	virtual void	Init(CInitThing *init);								// 9 (0050a3a0): spawns the wingman the player picked

	CActiveReader<CThing>	mWingman;									// +0x7c
	CUnitInitThing	mInit;												// +0x80: the wingman's, from the start's place
};

#endif
