// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_GRAPHICS_POLICY_H
#define CE_GRAPHICS_POLICY_H

enum CeGraphicsResource
{
    CE_GRAPHICS_WATER = 1,
    CE_GRAPHICS_LANDSCAPE = 2,
    CE_GRAPHICS_PARTICLES = 4,
    CE_GRAPHICS_LANDSCAPE_VERTEX = 8,
    CE_GRAPHICS_DEVICE = 16
};

// Ordinary CE only. Faithful clocks, tapes, probes and recordings never consult settings or change a tweak.
bool PortCeGraphicsPolicyActive();
bool PortCeGraphicsFloatTargetRequired();
// Called immediately after the native options open. Only the absence of the profile's own saved options opts a fresh profile into
// the maximum of every Video Options quality row except 3D distant trees: nothing readable at all, or only the file the retail game left in the game folder (the
// profile has none of its own). Taking the fresh defaults also starts the beyond-retail rows unless ce.ini names a choice:
// High-detail units On (1), 3D distant trees Off (0, opt-in), Terrain detail distance Extended (1), Anisotropic filtering 16x (16),
// Water reflections Ultra (1024), and Dynamic shadows Ultra (256). Legacy full_models=1 enables both unit and tree models when neither split key is saved.
void PortCeGraphicsOptionsOpened(bool opened, int error);
bool PortCeGraphicsTakeFreshDefaults();
void PortCeGraphicsApplyFreshDefaults();
void PortCeGraphicsBeginDevice(unsigned pixelVersion, unsigned vertexVersion, bool formats, bool float32);
void PortCeGraphicsResourceOutcome(unsigned resource, bool ready);
void PortCeGraphicsFinishDevice(bool ready);
bool PortCeGraphicsRequested();
// A supported device could not start an optional effect the player enabled.
bool PortCeGraphicsRequestFailed();
bool PortCeGraphicsStoreRequest(int enabled);
// The Overall Detail level of the ordinary CE menus (patch 0213) reads the rows as their setters leave them. The game's presets were
// written for the graphics cards of 2003: the landscape preset names a level beyond the row's last choice on a card with more, and
// the High preset names only the first anti-aliasing level. In faithful runs both answers are the game's own.
// The landscape selection a preset stands for (high: the High preset): the row's last choice for High, and for Medium the preset itself
// or the last choice when it names a level beyond the row.
int PortCeDetailLandscape(int preset, int choices, int high);
// Whether an anti-aliasing selection counts as the High preset: the game's own first level, or a finer one.
bool PortCeDetailAntiAliasingHigh(int selection, int gameHigh);

#endif
