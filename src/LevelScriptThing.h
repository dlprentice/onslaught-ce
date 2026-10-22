// LevelScriptThing.h: the thing a level's script runs on (LevelScriptThing.obj, 0048ffe0..004900e3; Stuart's header is
// not in his release).

#ifndef LEVELSCRIPTTHING_H
#define LEVELSCRIPTTHING_H

#include "thing.h"

// A level's script holder (0x7c bytes; vtable 005dcfe8).
DECLARE_THING_CLASS(CLevelScriptThing, CComplexThing)
public:
	virtual void	Init(CInitThing *init);								// 004900d0
};

#endif
