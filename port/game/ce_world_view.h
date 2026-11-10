// SPDX-FileCopyrightText: Onslaught CE contributors
// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_WORLD_VIEW_H
#define CE_WORLD_VIEW_H
#include <cstdint>
#include "ce_view_projection.h"

enum class CeWorldViewOwner { Commander = 1, Editor = 2 };
struct CeWorldViewSample {
    float view[16], projection[16];
    CeViewViewport viewport;
    unsigned present;
};

// A token identifies one active entry into the owning tool. Zero is inactive;
// change it on every entry/replacement, and do not recycle wrapped identities.
extern "C" std::uint64_t PortCeCmdWorldViewToken() __attribute__((weak));
extern "C" std::uint64_t PortCeEditorWorldViewToken() __attribute__((weak));

// Called only at the actual world-render projection, before terrain/HUD draw.
// No active owner means no matrix/device reads or writes. Two owners are refused.
// A mirror pass invalidates the old sample without reading its reflected view;
// only a successful following ordinary capture makes the provider readable again.
extern "C" void PortCeWorldViewCapture();
// Reads a copy of the most recently displayed world view. A resize, owner change,
// session change or more than one Present of age refuses it without touching out.
// Reads during a mirror pass are also refused.
bool CeWorldViewRead(CeWorldViewOwner owner, std::uint64_t token, CeWorldViewSample* out);
void CeWorldViewInvalidate(CeWorldViewOwner owner);
#endif
