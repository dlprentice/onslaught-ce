// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum.h: the Museum preview's visitor mode (patch 0199, ce_frontend.cpp's Museum Preview row).
//
// A visitor context for the Goodies gallery, not a playable museum world. It owns no career, unlock, file, asset or simulation
// state: the gallery's own code asks it three questions (what does this exhibit read as, may a load start, may an unlock change)
// and it answers from one flag. It exists only between PortCeMuseumBegin and the visit's end, and the CE page is the only caller of
// Begin (and only while the CE menus are on), so replays, recordings, the harness, -autoconfigtest and the demo never meet it.
#ifndef CE_MUSEUM_H
#define CE_MUSEUM_H

// Starts a visit. offered is whether the caller may offer one (PortCeMenus()); a refused or repeated start returns false and
// changes nothing.
extern "C" bool PortCeMuseumBegin(bool offered);
// Ends the visit (every way out of the gallery, and the gallery's shutdown, end it).
extern "C" void PortCeMuseumEnd(void);
extern "C" bool PortCeMuseumActive(void);
// A visitor may start loading a picture or a model only. visual is whether the selected exhibit is one.
extern "C" bool PortCeMuseumCanLoad(bool visual);
// False during a visit: the gallery writes no unlock.
extern "C" bool PortCeMuseumCanChangeUnlocks(void);
// What an exhibit reads as: the ordinary state outside a visit; during one, viewed for a picture or a model, unavailable for the rest.
extern "C" int PortCeMuseumReadUnlock(int ordinary, bool visual, int viewed, int unavailable);
// Called with every page change request, after the front end has admitted the page. Only the visit's own route is let through: the
// CE page to the gallery. The page the gallery's Back asks for (ordinary_back: the save page, through which the game saves the career
// on its way out of the gallery) is answered with the CE page instead, so that a visit saves nothing; any other request ends the visit
// and is returned unchanged, so the front end carries on exactly as it would have.
extern "C" int PortCeMuseumPage(int current, int requested, int ce, int gallery, int ordinary_back);

#endif
