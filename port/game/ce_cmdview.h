// SPDX-License-Identifier: GPL-3.0-only
// The Commander's live view, drawn by CE itself (port/game/ce_cmdview.cpp). The battle's own frame is rendered exactly as it is when the view is
// closed; this picture is then drawn over it from read-only queries of the world, through the device alone. See the notes at the top of the .cpp.
#ifndef PORT_CE_CMDVIEW_H
#define PORT_CE_CMDVIEW_H

#ifdef CE_VERSION

#include "ce_world_view.h"
#include "ce_command_lifetime.h"

// A camera as plain numbers: where it is, its orientation's three rows (the game's FMatrix rows) and its zoom (the half width of the view at
// distance one, as the game's cameras give it).
struct CeCmdViewCamera
{
	float pos[3];
	float rows[3][3];
	float zoom;
};

// The view and projection matrices the picture is drawn with, for a window of the given size, and the viewport. The same construction as the
// game's own render makes from a camera, so a view that starts at the pilot's camera starts on the pilot's picture. False when the numbers are
// not usable.
bool	CeCmdViewMatrices(const CeCmdViewCamera &camera, float width, float height, float nearZ, float farZ, CeWorldViewSample *out);

// One frame of the picture. Begin keeps the device's whole state and sets the picture's; End puts the device's state back. Between them: the
// world (terrain, water, the units by side), then boxes and text over it in window pixels. `fade` is 0 to 1: below 1 the picture is blended over
// the frame behind it. Begin returns false, having drawn and changed nothing, where the picture cannot be made.
bool	CeCmdViewBegin(const CeWorldViewSample &view, float fade);
void	CeCmdViewWorld(int friendlyAllegiance, const void *pilot);
// Collect native-view friendly member hits without drawing replacement scenery.
void CeCmdViewCollectUnits(const CeWorldViewSample& sample, int friendlyAllegiance, const void *pilot);
// Friendly squad members in the last battlefield picture, in viewport pixels.
// A friendly unit's mark as the last picture placed it: its squad, its window point and pick radius, its identity, and (for the map's rings, which
// stand on the ground) where it is in the world, the world radius its kind is drawn with, and the window pixels one world unit spans there.
struct CeCmdViewUnitMark { int squad; float x, y, radius; CeCommandLifetimeKey key; float wx, wy, wz, worldRadius, ppu; };
const CeCmdViewUnitMark *CeCmdViewUnitMarks(unsigned &count);
void	CeCmdViewBox(float x, float y, float w, float h, unsigned colour);
void	CeCmdViewText(float x, float y, unsigned colour, const char *text, float scale);
void	CeCmdViewEnd();

// The small font's metrics as the picture draws it: a line's width at scale one, and the font's height. Zero where the font is not loaded.
float	CeCmdViewTextWidth(const char *text);
float	CeCmdViewTextHeight();

// Frees the picture's device objects and its memory (the view closed, the level ended, the device changed).
void	CeCmdViewRelease();

// Whether a draw is inside Begin and End (the map's drawing helpers send their boxes and text here while it is).
bool	CeCmdViewDrawing();

#ifdef CE_COMMANDER_TEST
// A test build's footprint audit (ce_cmdview_audit.cpp, BEA_PORT_COMMANDER_AUDIT=1): the game's heaps and the executable's data copied before a
// frame of the live view is drawn and compared after it. The live view may change none of it but its own records.
void	CeCmdViewAuditBefore();
void	CeCmdViewAuditAfter();
void	CeCmdViewAuditReport();
// What the last frame drew, for the proof runs' notes.
struct CeCmdViewStats { unsigned frames, terrainChunks, markers, vertices, texels; bool mixer; };
void	CeCmdViewGetStats(CeCmdViewStats *out);
// One frame of the live view drawn over the game's own render of the battle from the Commander's camera (the native picture): counted with the
// frames of the picture this file draws, so the isolation proof can see that the view was open (ce_commander_isolation_run.py).
void	CeCmdViewCountFrame();
#endif

#endif

#endif
