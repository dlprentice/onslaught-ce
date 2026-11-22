// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit (port/game/ce_playunit.cpp): a developer option that hands the local player's controls to a friendly ground tank. Off unless ce.ini holds the line
// play_unit=tank, and never in a replay, a recording, under probes or the virtual clocks, for a test pilot, in a demo, in a network game or with a second player. What a test
// build adds (the probe and the proof switch) exists only with -DCE_PLAYUNIT_TEST (port/build.py --play-unit-test).
#ifndef PORT_CE_PLAYUNIT_H
#define PORT_CE_PLAYUNIT_H

#include <stdint.h>
class CUnit;

#ifdef CE_VERSION

// Once at the top of every CGame::Update (port/game/updatedump.cpp, PortUpdateHook): returns at once unless the option is wanted and allowed here.
void	CePlayUnitUpdate();

// The level is ending (CGame::ShutdownRestartLoop, through ce_cmdmap.cpp's PortCeCmdLevelEnd, patch 0162): the driver gives the controls and the Battle Engine back.
void	CePlayUnitLevelEnd();

// Immediate native-adapter calls only. Keep native pointers out of UI snapshots.
// Resolve the UI entity generation again before calling; these calls independently
// validate the world epoch, current registry membership and creation identity.
uint64_t CePlayUnitSelectionWorld();
bool CePlayUnitCanTakeSelection(uint64_t world, CUnit* candidate,
    int expectedNumber, const char** reason = nullptr);
bool CePlayUnitTakeSelection(uint64_t world, CUnit* candidate,
    int expectedNumber, const char** reason = nullptr);


// A controlled unit or its pending handback owns the pilot until both input and camera are returned.
bool CePlayUnitCanTakeDelivery();
bool CePlayUnitTakeDelivery();
bool CePlayUnitOwnsPilot();
// A map transition consumes the current takeover press, including a key held across the boundary.
void CePlayUnitMapBoundary();

// Every frame, from ce_notice.cpp's render hook (weakly: a program without this module has none): reads the take key, and while a unit is driven draws the state of the
// held fire (READY, ARMING, RECOVERING, OUT OF RANGE, BLOCKED) under the middle of the view, and the module's notice in the HUD's message capsule. Nothing at all while the
// option is not wanted.
void	CePlayUnitRenderState();

// For the frame-feel log (port/platform/common/feel.h): while a unit is driven, its displayed (render) position and heading then its simulation position and heading, as
// twelve floats (three, three, three, three); 0 and nothing written when no unit is driven. Reads the unit; changes nothing.
extern "C" int	CePlayUnitFeelPose(float *out);

// The HUD while a unit is driven with the look camera (patch 0285). Its own camera hides the Battle Engine's overlay, so the HUD draws the driven unit's instead
// (CePlayUnitHudOverlay: the objective panel, the compass ring round the reticle and the radar, as the Battle Engine's view has them), and the compass and the radar
// read the focus: the unit's displayed place, the look's heading (the game's yaw) and the unit's life against its full life, where they would read the Battle
// Engine's. The ring's energy arc and marker are the Battle Engine's alone and are not drawn. False when no unit is driven: the HUD reads its Battle Engine.
// Read-only state for a test kit (every build; no behaviour of their own). Driving: a unit is driven now. Class: its class name, "" when none. Pose: its simulation
// place, heading in degrees (the game's yaw) and speed along the ground in units a second; false when none. Aim: true while the aim point is valid, with the point
// and the reticle's state (0 ready, 1 arming, 2 recovering, 3 out of range, 4 blocked; -1 no weapon). Rounds: how many rounds the driven unit has launched since
// the take, with where the last one struck and the aim point when it was launched; only a test build (--play-unit-test) with its probe on follows rounds, so
// every other build answers 0 with the points not numbers.
bool CePlayUnitDrivingNow();
const char *CePlayUnitDrivenClass();
bool CePlayUnitDrivenPose(float pos[3], float *headingDeg, float *speedUps);
bool CePlayUnitAimNow(float point[3], int *fireState);
unsigned CePlayUnitRoundsNow(float lastImpact[3], float lastAim[3]);
struct CePlayUnitHudFocus { float x, y, z, yaw, life, fullLife; int allegiance; };
bool	CePlayUnitHudFocusOf(CePlayUnitHudFocus *out);
bool	CePlayUnitHudOverlay(class CPlayer *player, int viewpoint);
struct CePlayUnitHudTelemetry;
bool	CePlayUnitHudTelemetryOf(CePlayUnitHudTelemetry *out);

#endif


class CUnit;
class FVector;
// False leaves the native AI aim path untouched.
bool PortCePlayUnitAimPoint(const CUnit* unit, FVector& point);

#endif
