// SPDX-License-Identifier: GPL-3.0-only
// The Dodge Button (port/game/ce_dodge_assist.cpp), called by patch 0220 from CController::DoMappings and
// CController::SendButtonAction, and by the tutorial cue and the controls help.
#ifndef PORT_CE_DODGE_ASSIST_H
#define PORT_CE_DODGE_ASSIST_H

#include "ce_dodge_assist_rules.h"

class CController;

// The Dodge Button's keys and buttons: Left Ctrl on the keyboard (controller 0 only, as the keyboard bindings are), game button 0 on a
// gamepad (B on an Xbox-style pad, the right of the four face buttons; the tutorial cue names buttons by position and calls it the
// right button). Flight sticks have none.
enum { CE_DODGE_PAD_BUTTON = 0 };

// The keyboard key (a DIK_ code) that is the Dodge Button for the controller on this pad now, or 0: the assist is not working
// for it, or the player has bound the key to a control of their own.
int PortCeDodgeAssistKey(int pad);

// A strafe action the player's own inputs sent through CController::SendButtonAction. 1: do not pass it on.
int PortCeDodgeAssistFilter(CController *controller, int pad, int button, float value);

// The end of a controller update. keyDown: whether the key PortCeDodgeAssistKey named is held. Writes the strafe actions to send
// now (ids: BUTTON_MECH_STRAFE_LEFT or _RIGHT; hard: 1 a full push as a key sends it, 0 a neutral 0) and returns how many, at
// most CeDodge::kMaxTaps.
int PortCeDodgeAssistEnd(CController *controller, int pad, int keyDown, int *ids, int *hard);

// Whether the Dodge Button is on in this session (ce.ini dodge_button, the CE layer, none of the faithful fences, no network
// game), for the tutorial cue and the controls help. Not a promise that a given controller drives a walker now.
bool PortCeDodgeAssistOn();
// Whether the player's keyboard can use it (Left Ctrl is not bound to another control), and whether the gamepad on this pad can
// (a gamepad, not a flight stick, with button B not bound to another control).
bool PortCeDodgeAssistKeyFree();
bool PortCeDodgeAssistPadFree(int pad);

#endif
