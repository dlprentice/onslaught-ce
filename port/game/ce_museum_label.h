// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_label.h: the museum's words in the HUD's own capsule (ce_museum_label.cpp, hooked into CHud::RenderBattleline by patch 0207).
//
// A hall names itself for a few seconds when the visitor arrives, and each exhibit names itself, with one line, as the visitor comes near it. Both are drawn with
// the game's framed HUD bar (CHud::RenderObjectiveBar, the bar the radio messages use) and its small face, at the bottom centre of the view where the radio
// messages appear; a museum has no radio messages, so the two never meet. Only text and positions cross this boundary, set when the hall is generated
// and dropped when the visit ends. Nothing here reads or changes the simulation.
#ifndef CE_MUSEUM_LABEL_H
#define CE_MUSEUM_LABEL_H

// The most exhibits one hall can label.
enum { kCeMuseumLabelsMax = 16 };

// Drops every label and the hall's title.
extern "C" void PortCeMuseumLabelsClear(void);
// The hall's name and one line under it, shown for about five seconds from the first frame of the visit (a null or empty title shows none).
extern "C" void PortCeMuseumLabelsTitle(const char *title, const char *line);
extern "C" void PortCeMuseumLabelsWayfinding(const char *title, const char *guide);
// One exhibit: its name, one line about it, and where it stands on the ground plane (world X and Y). False when the table is full or a text is unusable.
extern "C" bool PortCeMuseumLabelsAdd(const char *name, const char *line, const char *detail, float x, float y, float top, float radius);
// A third line for one labelled exhibit (its index), shown under the others whenever its label is: what a visitor must know to read it
// right (an enlarged exhibit says so). False for no such label or an unusable text.
extern "C" bool PortCeMuseumLabelsNote(unsigned index, const char *note);
// How many exhibits are labelled, and the nearest one to a point within range of it (index, or -1): the rule the capsule uses, kept free of the
// engine so a host test can run it.
extern "C" unsigned PortCeMuseumLabelsCount(void);
extern "C" int PortCeMuseumLabelsNearest(float x, float y, float forwardX, float forwardY);
// Draws the capsules (inside the HUD's bottom-centre scope).
extern "C" void PortCeMuseumLabelsRender(void);

#endif
