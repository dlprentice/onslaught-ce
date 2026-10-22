// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/audioguard.cpp: test runs must never play through the desktop's sound. When a run asks for SDL's
// dummy audio driver (SDL_AUDIO_DRIVER=dummy, or BEA_PORT_REQUIRE_DUMMY_AUDIO, which port/tools/run.py and winrun.py
// set), the port selects it itself (an override hint: SDL then tries no other driver), starts SDL audio on it, logs the
// driver and stops if it is not the dummy one, before the game can open any sound: PortAudioGuardCheck runs in Linux's
// main and at the game's first call into the port (PortDirect3DCreate9). The runners check that log line. The static
// object below only reads the environment: an SDL call from a static constructor crashed the Windows build under Wine.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

namespace
{
bool gRequired;

struct AudioGuard
{
	AudioGuard()
	{
		const char *want = getenv("SDL_AUDIO_DRIVER");
		const char *req = getenv("BEA_PORT_REQUIRE_DUMMY_AUDIO");
		gRequired = (req && *req && *req != '0') || (want && strcmp(want, "dummy") == 0);
		if (!gRequired)
			return;
		if (want && *want && strcmp(want, "dummy") != 0)
		{
			fprintf(stderr, "bea-port: refusing audio: BEA_PORT_REQUIRE_DUMMY_AUDIO with SDL_AUDIO_DRIVER=%s\n", want);
			exit(3);
		}
	}
} gAudioGuard;
}

void PortAudioGuardCheck()
{
	static bool checked;
	if (!gRequired || checked)
		return;
	checked = true;
	// Set here as well as by the runners: Wine does not pass SDL_AUDIO_DRIVER to Windows programs.
	SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
	// Held for the whole run: port/dsound's own SDL_InitSubSystem then shares this driver.
	const char *driver = SDL_InitSubSystem(SDL_INIT_AUDIO) ? SDL_GetCurrentAudioDriver() : NULL;
	fprintf(stderr, "bea-port: SDL audio driver %s\n", driver ? driver : "(none)");
	if (!driver || strcmp(driver, "dummy") != 0)
	{
		fprintf(stderr, "bea-port: refusing audio driver %s (dummy required)\n", driver ? driver : "(none)");
		exit(3);
	}
}
