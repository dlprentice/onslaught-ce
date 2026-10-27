// SPDX-License-Identifier: GPL-3.0-only
// Name a port DirectInput key binding by the character it types in SDL's current keyboard layout.
#ifdef CE_VERSION
#include <SDL3/SDL.h>

SDL_Scancode PortScancodeForDik(int dik);	// dinput.cpp: inverts the mapping the game's keyboard input actually uses

// Writes one upper-case letter or digit and returns 1, or returns 0 so the caller uses its generic help text.
extern "C" int PortCeTutorialPrintableKey(int key, char *out, int size)
{
	if (!out || size < 2)
		return 0;
	SDL_Scancode scan = PortScancodeForDik(key);
	if (scan == SDL_SCANCODE_UNKNOWN)
		return 0;

	// The character that physical key types in the player's current layout. Only unambiguous ASCII letters and
	// digits are named; dead keys, symbols, non-Latin layouts and unknown translations use the generic text.
	SDL_Keycode current = SDL_GetKeyFromScancode(scan, SDL_KMOD_NONE, false);
	if (current >= 'a' && current <= 'z')
		current -= 'a' - 'A';
	if (!((current >= 'A' && current <= 'Z') || (current >= '0' && current <= '9')))
		return 0;
	out[0] = (char)current;
	out[1] = 0;
	return 1;
}
#endif
