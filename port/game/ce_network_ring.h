// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_network_ring.h: the ring a player opens with Escape (or a pad's Start) in a network mission, with Continue
// and Leave Game (port/game/ce_network_ring.cpp). Network builds only.
#ifndef CE_NETWORK_RING_H
#define CE_NETWORK_RING_H

#ifdef __cplusplus
extern "C" {
#endif
union SDL_Event;
// window.cpp's event pump: 1 when the ring took the event (Escape or Start in a running network mission, and its own
// keys and buttons while it is open), so nothing else sees it.
int PortCeNetRingEvent(const union SDL_Event *event);
// While the ring is open (and until the key or button that closed it is up) this player's controls are held still in
// the shared simulation: ce_network_lockstep.cpp sends an empty control sample, as it does while text chat is typed.
int PortCeNetRingCapturing(void);
// The player chose Leave Game, then Yes: the lobby tells them so on the first page (ce_netlobby.cpp).
int PortCeNetRingLeft(void);
#ifdef __cplusplus
}
// DXEngine's interface pass (patch 0231): the ring over the battle, as the game's pause menu is drawn.
void PortCeNetRingRender();
#endif

#endif
