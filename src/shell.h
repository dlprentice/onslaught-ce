// A shell: CActor through +0xe0, a per-move rotation at +0xe0, and a 256-byte resource name at +0x110.
// Both retail vtables (005ded48, 005decd0) inherit CActor; SpawnThing allocates 0x210 bytes at 004bf4d1.
#ifndef SHELL_H
#define SHELL_H

#include "actor.h"

class CShell : public CActor
{
public:
	CShell();
	// FUNCTION: 0x004df4f0
	virtual char *_GetClassName() { return "CShell"; }
	// FUNCTION: 0x004df500
	virtual int _GetClassID() { return 21; }
	// FUNCTION: 0x004df510
	virtual float GetMaxVelocity() { return 18.0f; }
	virtual void Init(CInitThing *init);
	virtual void Move();
	void	Unknown4df530(char *name);	// 004df530: ret 4; copies nonzero bytes to +0x110 without appending NUL

protected:
	FMatrix	mRotationStep;			// +0xe0: name ours; postmultiplies mOrientation in Move
	char	mResourceName[256];		// +0x110: zeroed by the constructor, read by Init
};

#endif
