// profile.h: the profiler hooks. They compile away unless a file defines ENABLE_PROFILER as 1 before including this;
// retail's MCBuggy.obj did (CMCBuggy::UpdateWheel 00493513 opens profile 133 and closes it inline at 004942d2, and
// its out-of-line destructor 00494310 closes it when an exception unwinds). Each profile is 0x1e0 bytes of a table
// whose first total is at 0082ce84: the cycles spent at +0 and the times entered at +0x1d0.

#ifndef PROFILE_H
#define PROFILE_H

#if ENABLE_PROFILER == 1

struct SProfileEntry
{
	DWORD	mCycles;							// +0x000
	UBYTE	mUnknown004[0x1d0 - 0x004];
	SINT	mCount;								// +0x1d0
	UBYTE	mUnknown1D4[0x1e0 - 0x1d4];
};

extern SProfileEntry	PROFILE_ENTRIES[];		// 0082ce84

#pragma warning(disable:4035)
inline __int64 ReadTimeStampCounter() { __asm { xor edx, edx
	rdtsc } }
#pragma warning(default:4035)

// Reconstructed counter update ownership; original helper/source spelling is unknown.
// Measured Buggy and Mech profile sites motivate a free destructor (38) and helper (37).
// The constructor remains its previous spelling (44); native 00494310 cleanup stays exact.
// Caller FP/call gates must pass; these sizes do not prove every retail profile site.
inline void ProfileAdd(SINT id, DWORD cycles)
{
	PROFILE_ENTRIES[id].mCycles += cycles;
	PROFILE_ENTRIES[id].mCount++;
}

class CProfileFn
{
public:
	CProfileFn(SINT id) { mId = id; __int64 t = ReadTimeStampCounter(); mStart = (DWORD)t; }
	~CProfileFn() { ProfileAdd(mId, (DWORD)ReadTimeStampCounter() - mStart); }

	SINT	mId;
	DWORD	mStart;
};

// The profiles' numbers (only those retail's code shows).
enum EProfile
{
	PROFILE_MechUpdateBone = 73,				// CMCMech::UpdateBone (00499e56); the name is ours
	PROFILE_MechGuide = 74,						// CMechGuide::Unknown3 (004a0be0); the name is ours
	PROFILE_BuggyUpdateWheel = 133				// CMCBuggy::UpdateWheel (00493513)
};

#define PROFILE_FN(name)	CProfileFn	__profile(PROFILE_##name)
#define PROFILE_START(name)
#define PROFILE_END(name)

#else

#define PROFILE_FN(name)
#define PROFILE_START(name)
#define PROFILE_END(name)

#endif

#endif
