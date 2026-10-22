// animal.h: CAnimal, the birds that flock over a level (Stuart's header is not in his release). oids.cpp keeps its own
// short declaration for the factory; this is animal.cpp's full view.

#ifndef ANIMAL_H
#define ANIMAL_H

#include	"thing.h"
#include	"oids.h"
#include	"AnimalType.h"

// An animal (0xe0 bytes; vtable 005d8698).
DECLARE_THING_CLASS(CAnimal, CComplexThing)
public:
	virtual			~CAnimal();											// 00404010
	virtual void	HandleEvent(CEvent *event);							// folded by the linker into CExplosion's (0044c140)
	virtual void	Init(CInitThing *init);								// 00403d30
	virtual	FVector	GetVelocity() { return mVelocity; }					// 00404120 (folded with CActor's)
	virtual	FVector	GetOldPos() { return mOldPos; }						// 004041a0
	virtual FMatrix	GetOldOrientation() { return mOldOrientation; }		// 004041d0
	virtual void	Render(DWORD flags);								// 004045d0
	virtual void	Move();												// 66 (00404210)
	virtual void	SetVelocity2(const FVector &v) { mVelocity = v; }	// 67 (00404150)
	virtual void	AddVelocity2(const FVector &v) { mVelocity += v; }	// 68 (00404170)

	FVector			mVelocity;											// +0x7c
	FVector			mOldPos;											// +0x8c
	FMatrix			mOldOrientation;									// +0x9c
	CAnimal			*mNext;												// +0xcc
	CAnimal			*mLeader;											// +0xd0: the one it follows
	AnimalType		mType;												// +0xd4
	float			mHeight;											// +0xd8: where it starts (Z is down)
	float			mClimb;												// +0xdc: the pitch it aims for

	static CAnimal	*sFirst;											// 00660130
	static SINT		sCount;												// 00660134
};

#endif
