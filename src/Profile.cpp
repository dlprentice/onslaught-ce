// Profile.cpp: the profiler's table (Stuart's release has no copy). The retail PC build keeps only the table here:
// PROFILE_ENTRIES (0082ce84) runs 134 entries of 0x1e0 bytes up to radar.obj's data (0083c9c4..), where this file's
// name falls in the link order between PolyBucket.obj and radar.obj; the highest profile used is 133 (MCBuggy.obj).

#include "common.h"
#define ENABLE_PROFILER 1
#include "profile.h"

SProfileEntry	PROFILE_ENTRIES[134];
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_Profile[8] = { 0 };
