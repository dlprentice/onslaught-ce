// SeedUniformRandomNumberStream.cpp: the game's seeded random stream. Not in Stuart's release; reconstructed from the
// retail code (004de8c0..004de959). The modulus is 214783647, a digit short of 2^31 - 1, as retail stores it.

#include "common.h"
#include "SeedUniformRandomNumberStream.h"

int	RANDOM_MULTIPLIER = 48271;					// 006321f0 (names ours)
int	RANDOM_MODULUS = 214783647;					// 006321f4

//******************************************************************************************
CSeedUniformRandomNumberStream::CSeedUniformRandomNumberStream(int seed)	// FUNCTION: 0x004de8c0
{
	mValue = mSeed = seed;
}

//******************************************************************************************
// The magnitude is taken by hand (test, jge, neg at 004de951): abs() is an intrinsic here and compiles branch-free.
int	CSeedUniformRandomNumberStream::Next()	// FUNCTION: 0x004de8d0
{
	static int	q = RANDOM_MODULUS / RANDOM_MULTIPLIER;
	static int	r = RANDOM_MODULUS % RANDOM_MULTIPLIER;

	int	hi = mValue / q;
	int	lo = mValue % q;
	int	t = RANDOM_MULTIPLIER * lo - r * hi;
	if (t > 0)
		mValue = t;
	else
		mValue = t + RANDOM_MODULUS;
	return (mValue < 0) ? -mValue : mValue;
}
