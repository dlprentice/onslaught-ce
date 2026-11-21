// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/replay_cheat.h: whether a cheat replay was asked for (BEA_PORT_REPLAY_CHEAT; replay.cpp describes
// it). Read from the environment alone, so the save writers (save_replace.cpp) can skip career and options writes while it
// is set, whatever replay.cpp decides about the switch itself.
#ifndef PORT_REPLAY_CHEAT_H
#define PORT_REPLAY_CHEAT_H

#include <stdlib.h>

// True when BEA_PORT_REPLAY_CHEAT is 1 and BEA_PORT_TAPE names a tape: a replay, never live play or a recording.
static inline bool PortReplayCheatRequested()
{
	const char *cheat = getenv("BEA_PORT_REPLAY_CHEAT"), *tape = getenv("BEA_PORT_TAPE");
	return cheat && cheat[0] == '1' && !cheat[1] && tape && *tape;
}

#endif
