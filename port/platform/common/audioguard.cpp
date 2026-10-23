// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/audioguard.cpp: test runs must never play through the desktop's sound. When a run asks for SDL's
// dummy audio driver (SDL_AUDIO_DRIVER=dummy, or BEA_PORT_REQUIRE_DUMMY_AUDIO, which port/tools/run.py and winrun.py
// set), the port selects it itself (an override hint: SDL then tries no other driver), starts SDL audio on it, logs the
// driver and stops if it is not the dummy one, before the game can open any sound: PortAudioGuardCheck runs in Linux's
// main and at the game's first call into the port (PortDirect3DCreate9). The runners check that log line. The static
// object below only reads the environment: an SDL call from a static constructor crashed the Windows build under Wine.
// BEA_PORT_REQUIRE_DUMMY_AUDIO=silent also accepts SDL's disk driver (SDL_AUDIO_DRIVER=disk, which writes the device's
// output to SDL_AUDIO_DISK_OUTPUT_FILE): a test of the record-mode audio monitor opens a real device without sound.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

namespace
{
bool gRequired;
const char *gDriver = "dummy";	// the silent driver required: dummy, or disk with BEA_PORT_REQUIRE_DUMMY_AUDIO=silent

struct AudioGuard
{
	AudioGuard()
	{
		const char *want = getenv("SDL_AUDIO_DRIVER");
		const char *req = getenv("BEA_PORT_REQUIRE_DUMMY_AUDIO");
		gRequired = (req && *req && *req != '0') || (want && strcmp(want, "dummy") == 0);
		if (!gRequired)
			return;
		if (req && strcmp(req, "silent") == 0 && want && strcmp(want, "disk") == 0)
			gDriver = "disk";
		if (want && *want && strcmp(want, gDriver) != 0)
		{
			fprintf(stderr, PORT_LOG_PREFIX "refusing audio: BEA_PORT_REQUIRE_DUMMY_AUDIO with SDL_AUDIO_DRIVER=%s\n", want);
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
	SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, gDriver, SDL_HINT_OVERRIDE);
	// Held for the whole run: port/dsound's own SDL_InitSubSystem then shares this driver.
	const char *driver = SDL_InitSubSystem(SDL_INIT_AUDIO) ? SDL_GetCurrentAudioDriver() : NULL;
	fprintf(stderr, PORT_LOG_PREFIX "SDL audio driver %s\n", driver ? driver : "(none)");
	if (!driver || strcmp(driver, gDriver) != 0)
	{
		fprintf(stderr, PORT_LOG_PREFIX "refusing audio driver %s (%s required)\n", driver ? driver : "(none)", gDriver);
		exit(3);
	}
}
