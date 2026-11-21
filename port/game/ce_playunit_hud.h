// SPDX-License-Identifier: GPL-3.0-only
// Play as a unit: what the player sees of it on the screen (port/game/ce_playunit_hud.cpp). The module (ce_playunit.cpp) decides; these draw. Nothing here reads or
// changes the game's simulation, the controls or a save.
#ifndef PORT_CE_PLAYUNIT_HUD_H
#define PORT_CE_PLAYUNIT_HUD_H

#ifdef CE_VERSION

// A notice in the HUD's own message capsule for four seconds of drawn time (six when longer): one or two lines (second may be NULL), in the game's small face. A newer
// notice replaces the one shown. The second line is drawn as a note (grey), the first as content (white).
void	CePlayUnitNotice(const char *first, const char *second, bool longer = false);
void	CePlayUnitNoticeClear();

// The one prompt line shown in a small capsule at the bottom while a unit is driven and no notice is up (NULL or "" for none).
void	CePlayUnitPrompt(const char *line);

// One frame of the play-as-a-unit screen: the reticle and the held fire's word while a unit is driven (reticle true, fireState a CePlayUnitFireState), then the notice
// or the prompt, at the top centre while the Battle Engine's HUD or the radio is up (hudShown) and where the radio's capsule sits when neither is.
void	CePlayUnitHudRender(bool reticle, int fireState, bool hudShown);

#endif

#endif
