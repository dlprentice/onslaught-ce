// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// DirectInput scan code. This CE preference is independent of retail control profiles.
int PortCeDodgeBindingKey();
void PortCeDodgeBindingReload();
bool PortCeDodgeBindingStore(int key);
bool PortCeDodgeBindingName(int key,char *out,int capacity);
bool PortCeDodgeBindingAvailable();
const char *PortCeDodgeBindingConflict(int key);
void PortCeDodgeStrafeNames(char *left,int leftSize,char *right,int rightSize);

// Gameplay shortcuts share the ordinary Controls capture and the CE settings file.
enum class CeControlAction { Dodge, TakeUnit, Commander };
int PortCeActionKey(CeControlAction action);
bool PortCeActionStore(CeControlAction action, int key);
const char *PortCeActionName(CeControlAction action);
const char *PortCeActionLabel(CeControlAction action);
bool PortCeActionEnabled(CeControlAction action);
const char *PortCeActionConflict(CeControlAction action, int key);
