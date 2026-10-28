// SPDX-License-Identifier: GPL-3.0-only
// A read-only description of the controller currently behind a DirectInput slot. Bits describe the physical SDL
// controls the adapter can actually read; they are not the game's possibly rotated button numbers.
#ifndef CE_TUTORIAL_PAD_H
#define CE_TUTORIAL_PAD_H

enum { CE_TUTORIAL_GAMEPAD = 1, CE_TUTORIAL_STICK = 2 };
enum {
	CE_PAD_X1 = 1 << 0, CE_PAD_Y1 = 1 << 1, CE_PAD_X2 = 1 << 2, CE_PAD_Y2 = 1 << 3,
	CE_PAD_HAT_LEFT = 1 << 4, CE_PAD_HAT_RIGHT = 1 << 5, CE_PAD_HAT_UP = 1 << 6, CE_PAD_HAT_DOWN = 1 << 7
};
struct CeTutorialPadInfo { int kind; unsigned axes, buttons; };

// Returns zero for an empty, unplugged, or non-controller device. Never opens or rebinds a device.
extern "C" int PortCeTutorialPadInfo(void *device, CeTutorialPadInfo *info);
#endif
