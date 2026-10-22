// MemoryCard.cpp: the memory card (Stuart's file). The retail PC build keeps only the object itself here: MEMORYCARD
// (00704858) sits alone between MechGuide.obj's and MenuItem.obj's data, where this file's name falls in the link
// order. CPCMemoryCard's methods are PCMemoryCard.cpp's.

#include "common.h"
#include "MemoryCard.h"

CMEMORYCARD MEMORYCARD;
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_MemoryCard[8] = { 0 };
