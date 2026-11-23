// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_online_ui_host.h: Plain-C POD boundary between game-heap qcxx renderer
// (port/game/ce_online_ui.cpp) and host-STL pcxx model/service layer (port/game/ce_online_ui_bridge.cpp).
// Deliberately contains ZERO STL types so operator new/delete never cross MemoryManager.h.
#ifndef PORT_GAME_CE_ONLINE_UI_HOST_H
#define PORT_GAME_CE_ONLINE_UI_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CE_OUI_SND_SELECT = 0,
    CE_OUI_SND_MOVE   = 1,
    CE_OUI_SND_BACK   = 2
};

enum {
    CE_OUI_EXIT_CLOSE_NET  = 1,
    CE_OUI_EXIT_CLOSE_MP   = 2,
    CE_OUI_EXIT_DIRECT_LAN = 3
};

enum {
    CE_OUI_BTN_UP     = 1,
    CE_OUI_BTN_DOWN   = 2,
    CE_OUI_BTN_LEFT   = 3,
    CE_OUI_BTN_RIGHT  = 4,
    CE_OUI_BTN_SELECT = 5,
    CE_OUI_BTN_BACK   = 6
};

enum {
    CE_OUI_KEY_ESC       = 1,
    CE_OUI_KEY_PASTE     = 2,
    CE_OUI_KEY_BACKSPACE = 3,
    CE_OUI_KEY_ENTER     = 4,
    CE_OUI_KEY_UP        = 5,
    CE_OUI_KEY_DOWN      = 6,
    CE_OUI_KEY_LEFT      = 7,
    CE_OUI_KEY_RIGHT     = 8,
    CE_OUI_KEY_PGUP      = 9,
    CE_OUI_KEY_PGDN      = 10,
    CE_OUI_KEY_CHAT      = 11,
    CE_OUI_KEY_CHAR      = 12
};

typedef struct CeOnlineUiHostOps {
    void (*draw_panel)(float x0, float y0, float x1, float y1, uint32_t fill, uint32_t border);
    void (*draw_text_left)(float x, float y, float max_w, uint32_t col, const char *text, int with_cursor);
    void (*draw_text_center)(float x0, float y0, float x1, float y1, uint32_t col, const char *text);
    void (*draw_text_right)(float right_x, float y, float max_w, uint32_t col, const char *text);
    int  (*mouse_clicked)(float x0, float y0, float x1, float y1);
    void (*play_sound)(int kind);
    int  (*copy_clip)(const char *text);
    int  (*paste_clip)(char *out, int cap);
    int  (*get_be_config)(int level, int index, char *out, int cap);
    void (*exit_action)(int kind);
} CeOnlineUiHostOps;

void PortCeOnlineHostInitOps(const CeOnlineUiHostOps *ops);
void PortCeOnlineHostOpen(int from_multiplayer);
void PortCeOnlineHostClose(void);
int  PortCeOnlineHostActive(void);
const char *PortCeOnlineHostTitleUtf8(void);
void PortCeOnlineHostUpdate(uint64_t now_ms);
void PortCeOnlineHostRender(float mouse_x, float mouse_y, int mouse_moved,
                            int *mouse_wheel, int can_click);
void PortCeOnlineHostButton(int btn);
int  PortCeOnlineHostKey(int kind, char ch);

#ifdef __cplusplus
}
#endif

#endif // PORT_GAME_CE_ONLINE_UI_HOST_H
