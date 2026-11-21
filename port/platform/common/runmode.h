// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/runmode.h: what kind of run this process is, decided once at start from its environment, so features
// ask this instead of reading test switches themselves (the first user: PortCeNetOffered, ce_netpages.cpp).
//   network test   BEA_NET_HOST, BEA_NET_JOIN, BEA_LOCKSTEP_HOST or BEA_LOCKSTEP_JOIN (the engineering and lockstep roles)
//   record         BEA_PORT_RECORD or BEA_RECORD
//   replay         BEA_PORT_TAPE, BEA_PORT_PILOT or BEA_PORT_AUTOPILOT
//   test           BEA_PORT_PROBES
//   test kit       a journey drives the run: asked of the test kit through testhooks.h, so only a --test-kit build can be in
//                  this mode (its own file reads BEA_PORT_VERBS); a player's build has no code that sets the hook or reads that
//                  variable, so no environment can put it in this mode
//   test           BEA_PORT_CLOCK without any of the above
//   play           none of these
// The first that applies, in this order, decides. The game's own command line (demo record and playback, auto-config) is
// the game's to read and is not part of this.
#ifndef PORT_RUNMODE_H
#define PORT_RUNMODE_H

enum PortRunMode
{
	PORT_RUN_PLAY = 0,
	PORT_RUN_TEST,
	PORT_RUN_TEST_KIT,
	PORT_RUN_REPLAY,
	PORT_RUN_RECORD,
	PORT_RUN_NETWORK_TEST
};

PortRunMode	PortRunModeNow();
const char	*PortRunModeName(PortRunMode mode);
// The online pages (Host and Join) are offered in ordinary play and in test-kit journeys (which drive them by the player's
// input, on any clock), and in no recording, replay, engineering session or other test run.
bool		PortRunOffersNetworkPages();

#endif
