// common.h: the base definitions every Battle Engine Aquila source file includes.
// Reconstructed from the retail executable (see README.md); only what matched files need so far.

#ifndef COMMON_H
#define COMMON_H

// Build targets, as the source's #if TARGET == ... blocks expect; this is the PC build.
#define PC		1
#define PS2		3		// retail: CResourceAccumulator::GetFileName compares 1, 3, 2 for "PC", "PS2", "XBOX" (004d6f77)
#define XBOX	2
#define PSX		4
#define TARGET	PC

#define _DIRECTX

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char	UBYTE;
typedef signed int	SINT;
typedef signed short	SWORD;
typedef unsigned short	UWORD;		// Stuart's (BattleEngine.h)
typedef signed long	SLONG;		// Stuart's (BattleEngineConfigurations); his typedef header is not in the release

#include "MemoryManager.h"

// Release builds compile assertions away.
#define ASSERT(x)
#define SASSERT(x, msg)

// A printf-style trace (varargs: VC6 never merges the caller's stack pops around a varargs call).
void	DebugTrace(char *fmt, ...);
#define TRACE DebugTrace

extern BOOL	PLAYABLE_DEMO;					// 0083d448: the playable-demo build's switch (set by -playabledemo)

#endif
