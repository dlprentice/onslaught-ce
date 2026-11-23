// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_ui.h: Native BEA front-end entry points for Online Multiplayer UI.
// Cleanly guarded with #ifndef CE_NETWORK_OFF so default/public builds without network omit it.
#ifndef CE_ONLINE_UI_H
#define CE_ONLINE_UI_H

#include <stdint.h>

#ifdef __cplusplus
class ICeOnlineUiBackend;
extern "C" {
#endif

#ifndef CE_NETWORK_OFF
void PortCeOnlineUiOpen(void);
void PortCeOnlineUiOpenFromMultiplayer(void);
int  PortCeOnlineUiActive(void);
void PortCeOnlineUiUpdate(void);
void PortCeOnlineUiRender(void);
void PortCeOnlineUiButton(int button, float val);
int  PortCeOnlineUiKey(uint16_t key, int eventType);
void PortCeOnlineUiClose(void);

// Internal frontend integration helpers (used by ce_netpages.cpp & patch 0380).
int  PortCeOnlineUiTakePendingEntry(void);
const char *PortCeOnlineUiTitleUtf8(void);
void PortCeOnlineUiMultiplayerProcess(int active);
void PortCeOnlineUiMultiplayerRender(float transition);

#ifdef __cplusplus
void PortCeOnlineUiSetBackend(ICeOnlineUiBackend *backend);
#endif
#endif // !CE_NETWORK_OFF

#ifdef __cplusplus
}
#endif

#endif // CE_ONLINE_UI_H
