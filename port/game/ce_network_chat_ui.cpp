// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_chat_ui.h"

#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_TEXT)
#include "ce_network_stl.h"
#include "common.h"
#include "game.h"
#include "frontend.h"
#include "Platform.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>

namespace {
SDL_WindowID text_window = 0;
SDL_Scancode release_key = SDL_SCANCODE_UNKNOWN;
uint32_t last_result = CE_CHAT_OK;

bool CanCompose() { return CE_LockstepChatAvailable() && !FRONTEND.IsRunning(); }

void Wipe(void *value, size_t size) {
    volatile uint8_t *bytes = static_cast<volatile uint8_t*>(value);
    while (size--) *bytes++ = 0;
}

void StopText() {
    if (text_window) {
        if (SDL_Window *window = SDL_GetWindowFromID(text_window))
            SDL_StopTextInput(window);
        text_window = 0;
    }
}

bool Enter(SDL_Scancode key) {
    return key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER;
}

// Presentation fallback only. Wire text and the owner's UTF-8 stay unchanged.
uint32_t Scalar(const uint8_t *text, uint32_t size, uint32_t &offset) {
    if (offset >= size) return 0;
    uint32_t value = text[offset++];
    if (value < 128) return value;
    unsigned more = value < 224 ? 1 : (value < 240 ? 2 : 3);
    value &= more == 1 ? 31 : (more == 2 ? 15 : 7);
    for (unsigned i = 0; i != more; ++i) {
        if (offset >= size || (text[offset] & 192) != 128) return '?';
        value = (value << 6) | (text[offset++] & 63);
    }
    return value;
}

struct Rows {
    WCHAR text[16][257]{};
    unsigned count = 0;
    void Add(const WCHAR *line) {
        if (count == 16) {
            std::memmove(text, text + 1, sizeof(text) - sizeof(text[0]));
            count = 15;
        }
        std::memcpy(text[count++], line, sizeof(text[0]));
    }
};

void Plain(Rows &rows, CDXBitmapFont *font, const char *prefix,
           const uint8_t *text, uint32_t size, float maximum, float scale) {
    WCHAR line[257]{};
    unsigned length = 0;
    for (const char *p = prefix; *p && length != 256; ++p)
        line[length++] = static_cast<WCHAR>(static_cast<unsigned char>(*p));
    uint32_t offset = 0;
    while (offset < size && size <= CE_CHAT_TEXT_BYTES) {
        const uint32_t scalar = Scalar(text, size, offset);
        WCHAR glyph = scalar <= 65535 && font->CanDisplay(static_cast<WCHAR>(scalar))
                      ? static_cast<WCHAR>(scalar) : static_cast<WCHAR>('?');
        if (length == 256) { rows.Add(line); Wipe(line, sizeof(line)); length = 0; }
        line[length++] = glyph; line[length] = 0;
        SIZE measured{}; font->GetTextExtent(line, &measured);
        if (length > 1 && measured.cx * scale > maximum) {
            line[--length] = 0; rows.Add(line); Wipe(line, sizeof(line));
            line[0] = glyph; length = 1;
        }
    }
    if (length) rows.Add(line);
    Wipe(line, sizeof(line));
}
} // namespace

extern "C" void PortCeChatReset(void) {
    CE_LockstepChatCancel(); StopText(); release_key = SDL_SCANCODE_UNKNOWN;
    last_result = CE_CHAT_OK;
}

extern "C" void PortCeChatFrame(void) {
    if (!CanCompose()) { PortCeChatReset(); return; }
    if (!CE_LockstepChatComposing()) StopText();
}

extern "C" int PortCeChatCapturingInput(void) {
    return CanCompose() && (CE_LockstepChatComposing() || release_key != SDL_SCANCODE_UNKNOWN);
}

extern "C" int PortCeChatEvent(const SDL_Event *event) {
    if (!event) return 0;
    if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        event->type == SDL_EVENT_WINDOW_DESTROYED) {
        PortCeChatReset(); return 0;
    }
    if (event->type == SDL_EVENT_KEY_UP && event->key.scancode == release_key) {
        release_key = SDL_SCANCODE_UNKNOWN; return 1;
    }
    if (release_key != SDL_SCANCODE_UNKNOWN &&
        (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)) return 1;
    if (!CanCompose()) return 0;
    if (event->type == SDL_EVENT_TEXT_INPUT && CE_LockstepChatComposing()) {
        if (event->text.text) {
            size_t size = 0;
            while (size <= CE_CHAT_TEXT_BYTES && event->text.text[size]) ++size;
            last_result = size <= CE_CHAT_TEXT_BYTES
                          ? CE_LockstepChatAppend(reinterpret_cast<const uint8_t*>(event->text.text),
                                                  static_cast<uint32_t>(size)) : CE_CHAT_TOO_LARGE;
        }
        return 1;
    }
    if (event->type != SDL_EVENT_KEY_DOWN && event->type != SDL_EVENT_KEY_UP) return 0;
    const bool down = event->type == SDL_EVENT_KEY_DOWN;
    const SDL_Scancode key = event->key.scancode;
    if (!CE_LockstepChatComposing()) {
        if (!down || !Enter(key) || event->key.repeat) return 0;
        SDL_Window *window = SDL_GetWindowFromID(event->key.windowID);
        if (!window || !CE_LockstepChatBegin()) return 0;
        if (!SDL_StartTextInput(window)) { CE_LockstepChatCancel(); return 1; }
        text_window = event->key.windowID; last_result = CE_CHAT_OK;
        return 1;
    }
    if (!down) return 1;
    if (key == SDL_SCANCODE_ESCAPE) {
        CE_LockstepChatCancel(); StopText(); release_key = key; last_result = CE_CHAT_OK;
    } else if (Enter(key)) {
        if (!event->key.repeat) {
            last_result = CE_LockstepChatSubmit();
            if (last_result == CE_CHAT_OK) { StopText(); release_key = key; }
        }
    } else if (key == SDL_SCANCODE_BACKSPACE) {
        last_result = CE_LockstepChatBackspace();
    } else if (key == SDL_SCANCODE_F6 && !event->key.repeat) {
        CE_ChatView view{}; CE_LockstepChatCopyView(&view);
        CE_LockstepChatMute(!view.muted); Wipe(&view, sizeof(view));
    } else if (key == SDL_SCANCODE_F7 && !event->key.repeat) {
        CE_LockstepChatClear();
    }
    return 1;
}

void PortCeChatRender(void) {
    CE_ChatView view{}; CE_LockstepChatCopyView(&view);
    if (!view.available || FRONTEND.IsRunning()) { Wipe(&view, sizeof(view)); return; }
    CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
    if (!font) { Wipe(&view, sizeof(view)); return; }
    const float width = float(PLATFORM.GetWindowWidth());
    const float height = float(PLATFORM.GetWindowHeight());
    const float scale = (std::max)(0.65f, (std::min)(1.5f, height / 720.f));
    const float line_height = (std::max)(12.f, float(font->GetHeight()) * scale);
    Rows rows;
    const unsigned first = view.history_count > 6 ? view.history_count - 6 : 0;
    for (unsigned i = first; i < view.history_count && i < CE_CHAT_HISTORY; ++i)
        Plain(rows, font, view.history[i].local_transport_accepted ? "You (local): " : "Peer: ",
              view.history[i].utf8, view.history[i].size, width - 40.f * scale, scale);
    const char *hint = view.composing ? "Enter send | Esc cancel | F6 mute | F7 clear"
                                     : "Enter: text chat (best effort)";
    Plain(rows, font, "", reinterpret_cast<const uint8_t*>(hint),
          static_cast<uint32_t>(std::strlen(hint)), width - 40.f * scale, scale);
    if (view.muted) {
        const char *label = "Peer text muted";
        Plain(rows, font, "", reinterpret_cast<const uint8_t*>(label),
              static_cast<uint32_t>(std::strlen(label)), width - 40.f * scale, scale);
    }
    if (view.composing)
        Plain(rows, font, "> ", view.draft, view.draft_size, width - 40.f * scale, scale);
    const char *notice = last_result == CE_CHAT_TOO_LARGE ? "Message exceeds this connection's limit" :
                         last_result == CE_CHAT_RATE_LIMITED ? "Wait before sending; draft retained" :
                         last_result == CE_CHAT_BUSY ? "Text queue busy; draft retained" :
                         last_result == CE_CHAT_INVALID ? "Unsupported text; draft retained" :
                         view.queued ? "Queued locally (best effort)" : nullptr;
    if (notice)
        Plain(rows, font, "", reinterpret_cast<const uint8_t*>(notice),
              static_cast<uint32_t>(std::strlen(notice)), width - 40.f * scale, scale);
    const float x = 20.f * scale;
    float y = (std::max)(20.f * scale, height - (rows.count + 2) * line_height);
    for (unsigned i = 0; i < rows.count; ++i) {
        font->DrawTextWithShadow(x, y, 0xffffffffu, rows.text[i], 0, 0.f, scale, scale);
        y += line_height;
    }
    Wipe(&rows, sizeof(rows)); Wipe(&view, sizeof(view));
}

#else
extern "C" int PortCeChatEvent(const union SDL_Event *) { return 0; }
extern "C" int PortCeChatCapturingInput(void) { return 0; }
extern "C" void PortCeChatFrame(void) {}
extern "C" void PortCeChatReset(void) {}
void PortCeChatRender(void) {}
#endif
#endif
