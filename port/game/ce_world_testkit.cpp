// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_world_testkit.cpp: the test kit's read-only world queries (ce_testkit.h; port/journeys/README.md). Compiled only
// by build.py --test-kit. Each verb asks the game's own functions a question and keeps the answer as a state key for assert,
// wait and dump; nothing changes game state, so they are INPUT steps (a query, not a write).
// Axes are the game's: x and y across the map, z grows DOWN (a higher point has a smaller z). Distances are the game's units.
//   probe-line X1 Y1 Z1 X2 Y2 Z2   the first thing a line from one point to the other hits (WORLD.FindFirstThingToHitLine):
//                                  world.line = TYPE,DISTANCE,THING,CLASS,HIT_Z (TYPE nothing, ground, water or thing as the
//                                  game reports it; THING the thing number the update dump uses, or -1; HIT_Z the hit's height).
//                                  Measured on level 311: over water the line stops at the water surface and still reports
//                                  ground, so tell water from land by HIT_Z against world.ground's water level.
//   probe-ground X Y               the map's height and the water level there: world.ground = GROUND_Z,WATER_Z
//   world.camera (a key, no verb)  where player 1's current camera is: X,Y,Z
#include "common.h"
#include "game.h"
#include "world.h"
#include "thing.h"
#include "map.h"
#include "Line.h"
#include "Camera.h"
#include "../platform/common/ce_testkit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <typeinfo>

namespace
{
char gLine[256], gGround[128];	// the last answers (plain buffers: the game's headers define min and max)

bool	InLevel() { return GAME.GetGameState() != GAME_STATE_NOT_RUNNING; }

CeTestResult VerbProbeLine(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 7) { CeTestReplyf(r, "usage: probe-line X1 Y1 Z1 X2 Y2 Z2"); return CE_TEST_FAILED; }
	if (!InLevel()) { CeTestReplyf(r, "waiting for a level"); return CE_TEST_PENDING; }
	float v[6];
	for (int i = 0; i < 6; ++i) v[i] = float(atof(a.word[1 + i]));
	CWorldLineColReport report;
	WORLD.FindFirstThingToHitLine(CLine(FVector(v[0], v[1], v[2]), FVector(v[3], v[4], v[5])), NULL, &report, FALSE, TRUE, 2, 0, TRUE);
	static const char *const types[] = {"nothing", "ground", "water", "thing"};
	const int type = int(report.mHitType);
	char text[256];
	const float dist = isfinite(report.mDistToImpact) ? report.mDistToImpact : -1.f;
	// the hit's height along the line (its z), or the line's end when nothing is hit
	const float length = sqrtf((v[3] - v[0]) * (v[3] - v[0]) + (v[4] - v[1]) * (v[4] - v[1]) + (v[5] - v[2]) * (v[5] - v[2]));
	const float hitZ = dist >= 0 && length > 0 ? v[2] + (v[5] - v[2]) * dist / length : v[5];
	snprintf(text, sizeof text, "%s,%.3f,%d,%s,%.3f", type >= 0 && type < 4 ? types[type] : "other", dist,
			 report.mHitThing ? report.mHitThing->GetThingNumber() : -1, report.mHitThing ? typeid(*report.mHitThing).name() : "-", hitZ);
	snprintf(gLine, sizeof gLine, "%s", text);
	CeTestReplyf(r, "%s", text);
	return CE_TEST_DONE;
}
CeTestResult VerbProbeGround(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3) { CeTestReplyf(r, "usage: probe-ground X Y"); return CE_TEST_FAILED; }
	if (!InLevel()) { CeTestReplyf(r, "waiting for a level"); return CE_TEST_PENDING; }
	const FVector at(float(atof(a.word[1])), float(atof(a.word[2])), 0.f);
	char text[128];
	snprintf(text, sizeof text, "%.3f,%.3f", MAP.Collide(at), MAP.GetWaterLevel());
	snprintf(gGround, sizeof gGround, "%s", text);
	CeTestReplyf(r, "%s", text);
	return CE_TEST_DONE;
}
bool	StateLine(char *out, size_t size) { if (!gLine[0]) return false; snprintf(out, size, "%s", gLine); return true; }
bool	StateGround(char *out, size_t size) { if (!gGround[0]) return false; snprintf(out, size, "%s", gGround); return true; }
bool	StateCamera(char *out, size_t size)
{
	if (!InLevel()) return false;
	CCamera *camera = GAME.GetCurrentCamera(0);
	if (!camera) return false;
	const FVector p = camera->GetPos();
	snprintf(out, size, "%.2f,%.2f,%.2f", p.X, p.Y, p.Z);
	return true;
}
}

CE_TEST_VERB("probe-line", "probe-line X1 Y1 Z1 X2 Y2 Z2: what a line hits first (world.line); z grows down", CE_TEST_INPUT, VerbProbeLine);
CE_TEST_VERB("probe-ground", "probe-ground X Y: the map's height and the water level (world.ground)", CE_TEST_INPUT, VerbProbeGround);
CE_TEST_STATE("world.line", StateLine);
CE_TEST_STATE("world.ground", StateGround);
CE_TEST_STATE("world.camera", StateCamera);
