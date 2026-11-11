// SPDX-License-Identifier: GPL-3.0-only
// Authored stand-ins for what the CE debrief panel (port/game/ce_debrief.cpp and patch 0104's block) reads.
#ifndef CE_DEBRIEF_FAKE_H
#define CE_DEBRIEF_FAKE_H
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
typedef unsigned long DWORD;
typedef wchar_t WCHAR;
struct SIZE { long cx, cy; };
enum { FONT_SMALL, FONT_NORMAL };
enum { TOTAL_MISSION_TIME, TIME_IN_JET_MODE, TIME_IN_WALKER_MODE, ROUNDS_FIRED, ACCURACY, DAMAGE_TAKEN };
enum { PS_ROUNDSFIRED, PS_ROUNDSHIT, PS_CHEATED, PS_TIMEASJET, PS_TIMEASWALKER, PS_DAMAGETAKEN, PS_NUM_PLAYERSTATS };
extern BOOL PLAYABLE_DEMO;
struct FakeCli { bool mAutoConfigTest = false, mRecordDemo = false, mPlaybackDemo = false, mAttractMode = false; int mStressTest = 0; };
extern FakeCli CLIPARAMS;
WCHAR *ToWCHAR(const char *t);
#endif
