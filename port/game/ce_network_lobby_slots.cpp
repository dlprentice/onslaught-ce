// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_network_lobby_slots.cpp: the network lobby's two player slots (port/game/ce_netpages.cpp), drawn as the
// original's Multiplayer page draws its level orbs (CFEPMultiplayer::Render): the orb's mask, its slowly turning blue
// glow and the two ring brackets, from the front end's own textures (CFrontEndData, loaded with the front end). A slot
// with nobody in it is the brackets alone, dimmed as the page dims a level that is not open. Network builds only
// (build.py: a port/game file named ce_network_* is a network module).
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "SpriteRenderer.h"

void	PortCeNetDrawSlot(float x, float y, bool present)
{
	RENDERINFO.SetFogEnabled(false);
	// The mask writes depth (at z 0.31) where the orb is not, without colour; the glow behind it (z 0.315) then shows only
	// inside the orb, as on the Multiplayer page, where the front end draws with the depth test on. The network page
	// draws its words with depth off, so it is on here for the orb only.
	LT.SRS(D3DRS_ZENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	if (present)
	{
		// the mask, then the glow over it, with the page's own blend states
		RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
		RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
		RENDERINFO.Apply();
		CSPRITERENDERER::DrawColouredSprite(x, y, 0.31f, FED.GetTexture(FET_LEVEL_SELECT_MASK), 0xffffffff, 1.f, 1.f, SA_CENTRE);
		RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
		RENDERINFO.Apply();
		const float spin = x * 0.01f + FRONTEND.GetCounter() * 0.02f;
		CSPRITERENDERER::DrawColouredSprite(x, y, 0.315f, FED.GetTexture(FET_LEVEL_SELECT_GLOW_BLUE), 0xffffffff, 1.f, 1.f, SA_CENTRE, SPT_SCROLL_RIGHT, spin);
	}
	const DWORD ring = present ? 0xffffffff : 0x1fffffff;
	CSPRITERENDERER::DrawColouredSprite(x, y, 0.305f, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET02), ring, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, 0.f);
	CSPRITERENDERER::DrawColouredSprite(x, y, 0.305f, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET01), ring, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, 0.f);
	// back to the states the network page draws its words with (ce_netpages.cpp CCeNetMenu::Draw)
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
	RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
	RENDERINFO.Apply();
}

#endif
