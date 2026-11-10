// SPDX-License-Identifier: GPL-3.0-only
// Long values in the options-style lists (a graphics adapter's marketing name, a folder) fit the 640 x 480 page.
// The game squeezes a label that is too wide for the left half of its row; the value half had no such rule, because the
// values of 2003 ("NVIDIA GeForce4 Ti 4200") were short. patch 0212 asks PortCeValueFit for each value it draws.
#ifndef CE_MENU_FIT_H
#define CE_MENU_FIT_H

// The virtual column where a value must end: the pages' right-hand ornament begins a little before the screen's edge.
static const float kCeValueRight = 600.f;
// How far a value is squeezed before its end is cut off and marked: the narrowest text that still reads.
static const float kCeValueMinScale = 0.75f;
// Where a value that cannot fit even squeezed is cut (characters kept, then "...").
static const int kCeValueMaxChars = 60;

// The horizontal scale that makes text of `width` units, drawn from x, end at `right`: exactly 1 when it already fits (the
// game's own draw call is then unchanged), else the fitting scale, never below `minimum` (the caller cuts what still overflows).
inline float PortCeFitScale(float x, float width, float right, float minimum)
{
    const float room = right - x;
    if (!(width > 0.f) || width <= room) return 1.f;
    if (!(room > 0.f)) return minimum;
    const float scale = room / width;
    return scale < minimum ? minimum : scale;
}

#endif
