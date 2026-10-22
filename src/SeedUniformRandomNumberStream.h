// SeedUniformRandomNumberStream.h: the game's seeded random stream (SeedUniformRandomNumberStream.cpp; the class name
// is Stuart's, game.h; the header is not in his release). A Park-Miller generator by Schrage's method.

#ifndef SEEDUNIFORMRANDOMNUMBERSTREAM_H
#define SEEDUNIFORMRANDOMNUMBERSTREAM_H

class CSeedUniformRandomNumberStream
{
public:
	CSeedUniformRandomNumberStream(int seed);	// 004de8c0 (8 bytes: CGame::InitRestartLoop 0046c7be)
	int		Next();								// 004de8d0: the next value, never negative

	int		mValue;								// +0x00
	int		mSeed;								// +0x04
};

#endif
