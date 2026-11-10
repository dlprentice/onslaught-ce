// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit (port/game/ce_playunit.cpp): a developer option that hands the local player's controls to a friendly ground tank. Off unless ce.ini holds the line
// play_unit=tank, and never in a replay, a recording, under probes or the virtual clocks, for a test pilot, in a demo, in a network game or with a second player. What a test
// build adds (the probe and the proof switch) exists only with -DCE_PLAYUNIT_TEST (port/build.py --play-unit-test).
#ifndef PORT_CE_PLAYUNIT_H
#define PORT_CE_PLAYUNIT_H

#ifdef CE_VERSION

// Once at the top of every CGame::Update (port/game/updatedump.cpp, PortUpdateHook): returns at once unless the option is wanted and allowed here.
void	CePlayUnitUpdate();

// The level is ending (CGame::ShutdownRestartLoop, through ce_cmdmap.cpp's PortCeCmdLevelEnd, patch 0162): the driver gives the controls and the Battle Engine back.
void	CePlayUnitLevelEnd();

// For the frame-feel log (port/platform/common/feel.h): while a unit is driven, its displayed (render) position and heading then its simulation position and heading, as
// twelve floats (three, three, three, three); 0 and nothing written when no unit is driven. Reads the unit; changes nothing.
extern "C" int	CePlayUnitFeelPose(float *out);

#endif

#endif
