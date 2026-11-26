// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum.cpp: the Museum preview's visitor mode (ce_museum.h, patch 0199).
#include "ce_museum.h"

namespace {
bool visitor = false;	// a visit is under way
bool entered = false;	// and the gallery has been reached from the CE page
bool exiting = false;	// the gallery just initiated an exit transition
bool fadeHold = false;	// holding visitor visuals during the 70-tick exit fade
}

extern "C" bool PortCeMuseumBegin(bool offered) {
    if (!offered || visitor) return false;
    visitor = true;
    entered = false;
    exiting = false;
    fadeHold = false;
    return true;
}

extern "C" void PortCeMuseumEnd(void) {
    visitor = false;
    entered = false;
    exiting = false;
    fadeHold = false;
}

extern "C" void PortCeMuseumHoldExitFade(bool hold) {
    if (hold && exiting) {
        exiting = false;
        fadeHold = true;
    } else {
        exiting = false;
        fadeHold = false;
    }
}

extern "C" bool PortCeMuseumActive(void) {
    return visitor || fadeHold;
}

extern "C" bool PortCeMuseumCanLoad(bool visual) {
    if (fadeHold) return visual;
    return !visitor || visual;
}

extern "C" bool PortCeMuseumCanChangeUnlocks(void) {
    if (fadeHold) return false;
    return !visitor;
}

extern "C" int PortCeMuseumReadUnlock(int ordinary, bool visual,
                                      int viewed, int unavailable) {
    if (!fadeHold) {
        if (!visitor) return ordinary;
    }
    return visual ? viewed : unavailable;
}

extern "C" int PortCeMuseumPage(int current, int requested, int ce,
                                int gallery, int ordinary_back) {
    if (fadeHold) {
        PortCeMuseumEnd();
        return requested;
    }
    if (!visitor) return requested;
    if (!entered) {
        if (current == ce && requested == gallery) {
            entered = true;
            return requested;
        }
        PortCeMuseumEnd();
        return requested;
    }
    if (current != gallery) {
        PortCeMuseumEnd();
        return requested;
    }
    if (requested == gallery) return requested;
    const bool back = requested == ordinary_back;
    PortCeMuseumEnd();
    exiting = true;
    return back ? ce : requested;
}
