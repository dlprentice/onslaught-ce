// CPhysicsScript.h: the loaded physics script, a list of statements that define the world's weapons, rounds,
// units and the rest (UPhysicsManager's registries). Reconstructed from CPhysicsScript.obj (0042e790..0042ea9f);
// Stuart's release has no copy. The class's name follows its file's; the retail symbols are not in the image.

#ifndef CPHYSICSSCRIPT_H
#define CPHYSICSSCRIPT_H

#include "SPtrSet.h"
#include "membuffer.h"
#include "DXMemBuffer.h"

class CPhysicsScriptStatement;

class CPhysicsScript
{
public:
	static void		Create();					// 0042e880
	static void		Shutdown();					// 0042e8f0
	static BOOL		Load(CMEMBUFFER &file);		// 0042e950: FALSE unless the file is version 18
	static void		Process();					// 0042ea60: executes every statement in order

	static SPtrSet<CPhysicsScriptStatement>	*mStatements;	// 0066e99c
};

#endif
