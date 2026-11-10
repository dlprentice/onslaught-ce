// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_NETWORK_CHAT_UI_H
#define CE_NETWORK_CHAT_UI_H
#include <stdint.h>

enum { CE_CHAT_TEXT_BYTES = 512, CE_CHAT_HISTORY = 16 };
enum { CE_CHAT_OK = 0, CE_CHAT_CLOSED, CE_CHAT_INVALID, CE_CHAT_TOO_LARGE,
       CE_CHAT_RATE_LIMITED, CE_CHAT_BUSY };
typedef struct CE_ChatLine {
    uint32_t size;
    int local_transport_accepted;
    uint8_t utf8[CE_CHAT_TEXT_BYTES + 1];
} CE_ChatLine;
typedef struct CE_ChatView {
    int available, composing, muted;
    uint32_t queued, text_limit, draft_size, history_count;
    uint8_t draft[CE_CHAT_TEXT_BYTES + 1];
    CE_ChatLine history[CE_CHAT_HISTORY];
} CE_ChatView;

#ifdef __cplusplus
extern "C" {
#endif
int CE_LockstepChatAvailable(void);
int CE_LockstepChatComposing(void);
int CE_LockstepChatBegin(void);
uint32_t CE_LockstepChatAppend(const uint8_t *text, uint32_t size);
uint32_t CE_LockstepChatBackspace(void);
uint32_t CE_LockstepChatSubmit(void);
void CE_LockstepChatCancel(void);
void CE_LockstepChatMute(int muted);
void CE_LockstepChatClear(void);
/* Caller-owned plain-text copies; every unavailable path clears the output. */
void CE_LockstepChatCopyView(CE_ChatView *view);
union SDL_Event;
int PortCeChatEvent(const union SDL_Event *event);
int PortCeChatCapturingInput(void);
void PortCeChatFrame(void);
void PortCeChatReset(void);
#ifdef __cplusplus
}
/* The generated game's overlay hook uses ordinary C++ linkage. */
void PortCeChatRender(void);
#endif
#endif
