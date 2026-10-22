// DXHud.h: the PC HUD. HUD is a CDXHud (DXHud.obj, 00542600..0054274b), which adds nothing to CHud but its own
// constructor; Hud.h includes this and declares HUD as one.

#ifndef DXHUD_H
#define DXHUD_H

#include "Hud.h"

class CDXHud : public CHud
{
public:
	CDXHud();									// 00542740: out of line (the initializer calls it, 005426f5)
};

#endif
