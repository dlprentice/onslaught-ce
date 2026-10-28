// SPDX-License-Identifier: GPL-3.0-only
// The CE gamepad rumble (port/game/ce_rumble.cpp), called by patch 0105 from CPCPlatform::TriggerRumble.
#ifndef PORT_CE_RUMBLE_H
#define PORT_CE_RUMBLE_H

// Stuart's effect (ltshell.cpp EnumEffectsProc): DI_FFNOMINALMAX of DI_FFNOMINALMAX, 0.25 s.
enum { CE_RUMBLE_LOW = 0xffff, CE_RUMBLE_HIGH = 0, CE_RUMBLE_MS = 250, CE_RUMBLE_MAX_PADS = 4 };

bool	PortCeRumbleOn();
// pads: PCLTShell's pJoystick (count of them, mJoypads); pad: the player's number - 1, as CBattleEngine passes it.
void	PortCeTriggerRumble(void *const pads[], int count, int pad);

#endif
