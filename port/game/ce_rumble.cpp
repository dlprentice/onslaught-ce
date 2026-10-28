// SPDX-License-Identifier: GPL-3.0-only
// The CE gamepad rumble: the body retail's CPCPlatform::TriggerRumble lost (patch 0105; CBattleEngine::AddShockShake
// calls it on every hit and weapon recoil big enough to shake the view). Stuart's PC shell (PCLTShell::TriggerRumble and
// EnumEffectsProc, ltshell.cpp) played one DirectInput periodic "Sine" effect on the player's pad: magnitude and gain
// DI_FFNOMINALMAX (full), a 0.05 s period, 0.25 s long, restarted at each call. Here it is one SDL rumble of the same
// strength and length on the low-frequency (strong) motor, the one a 20 Hz shake belongs to. His envelope (a 0.5 s
// attack and 1.0 s fade, both longer than the effect) has no SDL equivalent and is left out.
//
// ce.ini rumble=0 turns it off (default on). Never in faithful play: off in a replay, a recording, under probes or the
// virtual clocks, for a test pilot or a network game, or where the CE layer is off. It changes no game state.
#ifdef CE_VERSION

#include "common.h"
#include "CLIParams.h"
#include "ce_rumble.h"

#include <stdlib.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error The CE rumble requires weak symbol support for the network boot boundary
#endif
extern "C" int CE_BootRequested(void) __attribute__((weak));
extern "C" int CE_SettingGetBool(const char *key, int def);
extern "C" int PortRumblePad(void *device, unsigned short low, unsigned short high, unsigned ms);	// dinput.cpp
bool PortCeLayer();
bool PortRecording();

namespace {
const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};
int ini = -1;									// rumble, read once

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}
}

bool PortCeRumbleOn()
{
	if (!PortCeLayer() || PortRecording()) return false;
	for (const char *name : kFenced) if (Present(name)) return false;
	if (CE_BootRequested && CE_BootRequested()) return false;
	if (PLAYABLE_DEMO || CLIPARAMS.mAutoConfigTest || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
		CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode) return false;
	if (ini < 0) ini = CE_SettingGetBool("rumble", 1) ? 1 : 0;
	return ini != 0;
}

void PortCeTriggerRumble(void *const pads[], int count, int pad)
{
	if (pad < 0 || pad >= count || pad >= CE_RUMBLE_MAX_PADS || !pads[pad] || !PortCeRumbleOn()) return;
	PortRumblePad(pads[pad], CE_RUMBLE_LOW, CE_RUMBLE_HIGH, CE_RUMBLE_MS);
}

#endif
