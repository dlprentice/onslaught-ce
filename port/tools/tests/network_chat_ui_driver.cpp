// SPDX-License-Identifier: GPL-3.0-only
#include "common.h"
#include <SDL3/SDL.h>
#include "../../game/ce_network_chat_ui.h"
#include "../../network/text_chat_owner.h"
#include <cstdio>
#include <cstring>

std::vector<std::u16string> drawn;
FakePlatform PLATFORM;
FakeFrontend FRONTEND;
namespace {
CeTextChat::Owner owner;
bool active = false, start_ok = true;
unsigned starts = 0, stops = 0, submits = 0, checks = 0, failures = 0;
uint64_t clock_ms = 100;
uint32_t epoch = 1;
SDL_Window window{1};
void Check(bool ok, const char* label) {
    ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
uint32_t Result(CeTextChat::Result result) {
    if (result == CeTextChat::Result::Ok) return CE_CHAT_OK;
    if (result == CeTextChat::Result::RateLimited) return CE_CHAT_RATE_LIMITED;
    if (result == CeTextChat::Result::Backpressure) return CE_CHAT_BUSY;
    return CE_CHAT_INVALID;
}
SDL_Event Key(SDL_Scancode scan, bool down = true, bool repeat = false) {
    SDL_Event event{}; event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = 1; event.key.scancode = scan; event.key.repeat = repeat;
    return event;
}
int SendKey(SDL_Scancode scan, bool down = true, bool repeat = false) {
    const auto event = Key(scan, down, repeat); return PortCeChatEvent(&event);
}
[[maybe_unused]] void Text(const char* text) {
    SDL_Event event{}; event.type = SDL_EVENT_TEXT_INPUT; event.text.windowID = 1;
    event.text.text = text; Check(PortCeChatEvent(&event) != 0, "text-event-owned");
}
[[maybe_unused]] void Remote(uint64_t sequence, const char* text) {
    CeTextChat::Frame frame{};
    Check(CeTextChat::Encode(reinterpret_cast<const uint8_t*>(text), std::strlen(text),
                            sequence, frame) == CeTextChat::Result::Ok, "remote-fixture-valid");
    Check(owner.Receive(1, epoch, frame.bytes, frame.size, clock_ms) == CeTextChat::Result::Ok,
          "remote-fixture-received");
    CeTextChat::Clear(frame);
}
} // namespace

extern "C" SDL_Window* SDL_GetWindowFromID(SDL_WindowID id) { return id == 1 ? &window : nullptr; }
extern "C" bool SDL_StartTextInput(SDL_Window*) { ++starts; return start_ok; }
extern "C" bool SDL_StopTextInput(SDL_Window*) { ++stops; return true; }
extern "C" int CE_LockstepChatAvailable(void) { return active && owner.State().open; }
extern "C" int CE_LockstepChatComposing(void) { return CE_LockstepChatAvailable() && owner.State().composing; }
extern "C" int CE_LockstepChatBegin(void) { return CE_LockstepChatAvailable() && owner.BeginCompose() == CeTextChat::Result::Ok; }
extern "C" uint32_t CE_LockstepChatAppend(const uint8_t* text, uint32_t size) { return Result(owner.Append(text, size)); }
extern "C" uint32_t CE_LockstepChatBackspace(void) { return Result(owner.Backspace()); }
extern "C" uint32_t CE_LockstepChatSubmit(void) {
    ++submits; CeTextChat::Decoded draft{}; owner.CopyDraft(draft);
    const bool oversized = draft.size > 484 - CeTextChat::HeaderBytes;
    CeTextChat::Clear(draft);
    return oversized ? CE_CHAT_TOO_LARGE : Result(owner.SubmitDraft(clock_ms));
}
extern "C" void CE_LockstepChatCancel(void) { owner.CancelCompose(); }
extern "C" void CE_LockstepChatMute(int muted) { owner.SetMuted(muted != 0); }
extern "C" void CE_LockstepChatClear(void) { owner.ClearHistory(); }
extern "C" void CE_LockstepChatCopyView(CE_ChatView* out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out)); if (!CE_LockstepChatAvailable()) return;
    const auto state = owner.State(); out->available = 1; out->composing = state.composing;
    out->muted = state.muted; out->queued = static_cast<uint32_t>(state.outgoing); out->text_limit = 468;
    CeTextChat::Decoded draft{};
    if (owner.CopyDraft(draft) == CeTextChat::Result::Ok) {
        out->draft_size = draft.size; std::memcpy(out->draft, draft.utf8, draft.size);
    }
    CeTextChat::Clear(draft);
    for (unsigned i = 0; i < state.history; ++i) {
        CeTextChat::HistoryEntry entry{}; owner.HistoryAt(i, entry);
        auto& line = out->history[out->history_count++]; line.size = entry.message.text.size;
        line.local_transport_accepted = entry.origin == CeTextChat::Origin::LocalTransportAccepted;
        std::memcpy(line.utf8, entry.message.text.utf8, line.size); CeTextChat::Clear(entry);
    }
}

int main() {
    Check(owner.Open({1, 1, 0, CeTextChat::Schema}, clock_ms) == CeTextChat::Result::Ok,
          "fixture-owner-open");
    Check(SendKey(SDL_SCANCODE_RETURN) == 0 && starts == 0, "unavailable-input-unchanged");
#if defined(CE_NETWORK_TEXT)
    active = true;
    FRONTEND.running = true;
    Check(SendKey(SDL_SCANCODE_RETURN) == 0, "menu-enter-not-owned");
    drawn.clear(); PortCeChatRender(); Check(drawn.empty(), "menu-has-no-chat-overlay");
    FRONTEND.running = false;
    Check(SendKey(SDL_SCANCODE_RETURN) == 1 && owner.State().composing && starts == 1,
          "enter-opens-real-composer");
    const unsigned before_submit = submits, before_stop = stops;
    Check(SendKey(SDL_SCANCODE_RETURN, true, true) == 1 && submits == before_submit &&
          stops == before_stop && owner.State().composing, "repeat-enter-does-not-submit-or-close");
    Text(u8"Aé🙂");
    Check(SendKey(SDL_SCANCODE_BACKSPACE) == 1, "unicode-backspace-event");
    CeTextChat::Decoded draft{}; owner.CopyDraft(draft);
    Check(draft.size == 3 && draft.scalars == 2, "unicode-edit-reaches-owner");
    CeTextChat::Clear(draft);
    Check(SendKey(SDL_SCANCODE_ESCAPE) == 1 && !owner.State().composing &&
          PortCeChatCapturingInput(), "cancel-retains-key-release-fence");
    Check(SendKey(SDL_SCANCODE_A) == 1, "closing-key-fence-owns-other-key-events");
    Check(SendKey(SDL_SCANCODE_ESCAPE, false) == 1 && !PortCeChatCapturingInput(),
          "cancel-keyup-releases-gameplay-capture");
    Check(SendKey(SDL_SCANCODE_RETURN) == 1, "oversize-composer-open");
    std::string full;
    for (unsigned i = 0; i != 256; ++i) full += u8"é";
    Text(full.c_str());
    const unsigned queued = static_cast<unsigned>(owner.State().outgoing);
    Check(SendKey(SDL_SCANCODE_RETURN) == 1 && owner.State().composing &&
          owner.State().outgoing == queued, "path-size-refusal-keeps-focus-and-queue");
    for (unsigned i = 0; i != 24; ++i) SendKey(SDL_SCANCODE_BACKSPACE);
    Check(SendKey(SDL_SCANCODE_RETURN) == 1 && !owner.State().composing &&
          owner.State().outgoing == queued + 1 && PortCeChatCapturingInput(),
          "corrected-draft-queued-with-key-release-fence");
    SendKey(SDL_SCANCODE_RETURN, false);
    Check(!PortCeChatCapturingInput(), "send-keyup-releases-capture");
    Remote(1, u8"%s <tag> é🙂");
    drawn.clear(); PortCeChatRender();
    std::u16string joined;
    for (const auto& line : drawn) joined += line;
    Check(joined.find(u"%s <tag>") != std::u16string::npos &&
          joined.find(u"??") != std::u16string::npos, "plain-text-and-visible-glyph-fallback");
    Check(drawn.size() <= 16, "render-row-count-bounded");
    SendKey(SDL_SCANCODE_RETURN);
    SendKey(SDL_SCANCODE_F6);
    Check(owner.State().muted && owner.State().history == 0, "mute-control-reaches-owner");
    SendKey(SDL_SCANCODE_F6, true, true);
    Check(owner.State().muted, "mute-key-repeat-does-not-toggle");
    SendKey(SDL_SCANCODE_F6);
    Remote(2, "clear"); SendKey(SDL_SCANCODE_F7);
    Check(!owner.State().muted && owner.State().history == 0, "clear-history-control");
    Text("private draft");
    SDL_Event lost{}; lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    Check(PortCeChatEvent(&lost) == 0 && !owner.State().composing &&
          !PortCeChatCapturingInput(), "focus-loss-cleans-up-and-propagates");
    SendKey(SDL_SCANCODE_RETURN); Text("epoch draft");
    Check(owner.BeginEpoch(1, ++epoch, ++clock_ms) == CeTextChat::Result::Ok, "epoch-reset");
    PortCeChatFrame();
    Check(!PortCeChatCapturingInput() && !owner.State().composing, "epoch-releases-text-input");
    SendKey(SDL_SCANCODE_RETURN); Text("menu draft"); FRONTEND.running = true;
    PortCeChatFrame(); Check(!owner.State().composing && !PortCeChatCapturingInput(),
                             "menu-transition-releases-capture");
    FRONTEND.running = false; start_ok = false;
    Check(SendKey(SDL_SCANCODE_RETURN) == 1 && !owner.State().composing,
          "SDL-input-start-failure-releases-owner");
    start_ok = true; active = false; PortCeChatFrame();
    Check(!PortCeChatCapturingInput(), "departure-releases-capture");
#else
    active = true;
    Check(SendKey(SDL_SCANCODE_RETURN) == 0 && !PortCeChatCapturingInput(),
          "text-disabled-never-captures-input");
    PortCeChatFrame(); PortCeChatRender(); PortCeChatReset();
    Check(starts == 0 && stops == 0 && submits == 0 && drawn.empty(), "text-disabled-no-SDL-or-draw-work");
#endif
    std::printf("checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
