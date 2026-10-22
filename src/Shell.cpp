// Shell.cpp: CShell's construction, render initialization and movement from retail 004df3d0..004df94e.
// Stuart's release has no shell source and the specimen has no Shell.cpp __FILE__ string to recover its case.

#include "common.h"
#include "shell.h"
#include "eventmanager.h"

// FUNCTION: 0x004df3d0 init:ZERO_FVECTOR
// FUNCTION: 0x004df3f0 init:ID_FMATRIX
// FUNCTION: 0x004bfd00 ??_GCShell@@UAEPAXI@Z
// FUNCTION: 0x004df520 ??1CShell@@UAE@XZ

// FUNCTION: 0x004df4c0
CShell::CShell()
{
	for (int i = 0; i < sizeof(mResourceName); i++)
		mResourceName[i] = 0;
}

// FUNCTION: 0x004df530
void CShell::Unknown4df530(char *name)
{
	if (*name)
	{
		char *dest = mResourceName;
		do
			*dest++ = *name++;
		while (*name);
	}
}

// FUNCTION: 0x004df550
void CShell::Init(CInitThing *init)
{
	CRenderData data;
	data.mInit[0].mThing = this;
	data.mRTID[0] = RTID_CRTMesh;
	strcpy(data.mInit[0].mName, mResourceName);
	mRenderThing = CreateRenderThing(data.mRTID[0]);
	if (mRenderThing)
		mRenderThing->Init(&data.mInit[0]);

	mFlags &= ~TF_IN_MAP_WHO;
	CActor::Init(init);
	float yaw = (rand() % 256) * (1.0f / 512.0f) - 0.25f;
	float pitch = (rand() % 256) * (1.0f / 512.0f) - 0.25f;
	float roll = (rand() % 256) * (1.0f / 512.0f) - 0.25f;
	mRotationStep = FMatrix(yaw, pitch, roll);
	EVENT_MANAGER.AddEvent(SHUTDOWN, this, EVENT_MANAGER.GetTime() + 4.0f);
}

// FUNCTION: 0x004df730
void CShell::Move()
{
	CActor::Move();
	if (IsOnGround())
		mVelocity *= 0.7f;
	else
		mVelocity *= 0.9f;
	mOrientation = mOrientation * mRotationStep;
}
