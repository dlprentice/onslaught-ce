// SPDX-License-Identifier: GPL-3.0-only
// Execute the production CE save error notice (port/game/ce_notice.cpp) against the console's authored fake game boundary
// (port/tools/ce_palette_fake.h): the options failure flag the quiet save sites ask for, the front end's line and who may get it,
// the note in a level (its text, place, size and character set, its clock) and the frames it must stay out of.
#include "ce_palette_fake.h"
#include "ce_notice.h"
#include "ce_overlay_box.h"
#include <cmath>
#include <cstdio>
#include <string>

PCLTShell LT; WORD gCharScanCode = 0; bool gKeyPressed = false; Cli CLIPARAMS; BOOL PLAYABLE_DEMO = FALSE;
FakeGame GAME; FakeFrontend FRONTEND; FakeInterface GAMEINTERFACE; FakeHud HUD; FakePlatform PLATFORM;
FakeRenderInfo RENDERINFO; FakeConsole CONSOLE; std::vector<FakeRect> sprites; std::vector<FakeDraw> draws;
bool fakeRecording = false;
#include "ce_layer.inc"

static unsigned long long ticks = 1000;
extern "C" unsigned long long CE_ScreenshotTicks(void) { return ticks; }
static bool savedNext = false;
bool PortCePhotoSavedNext() { return savedNext; }
// FrontEndText.cpp's PortCeSaveErrorText (patch 0137): the game's own line, which the driver can take away or change.
static std::wstring line = L"An error occurred during saving.";
static bool haveLine = true;
WCHAR *PortCeSaveErrorText()
{
    static WCHAR buffer[512];
    if (!haveLine) return nullptr;
    std::wcsncpy(buffer, line.c_str(), 511);
    buffer[511] = 0;
    return buffer;
}

static void check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}

static void render() { draws.clear(); sprites.clear(); PortCeNoticeRender(); }
// A note fades in over 200 milliseconds (ce_overlay_box.h): a frame at the very moment it came shows none of it.
static void settled() { ticks += 200; render(); }
static bool photoOn = false;
bool PortCePhotoOn() { return photoOn; }
static bool drew(const std::wstring &text) { for (const auto &d : draws) if (d.text == text) return true; return false; }

// The note's colours at a full-strength fade, and at fades a float conversion could round past 1: the alpha byte must never wrap (the
// game's x87 unit rounds to nearest, which once made 255 + 0.5 into 256 and the whole note invisible).
#ifdef CE_VERSION
static void colours() {
    check(PortCeNoteColour(0xffffffff, 1.f) == 0xffffffff && PortCeNoteColour(0xaf000000, 1.f) == 0xaf000000 && PortCeNoteColour(0xff7f7f7f, 1.f) == 0xff7f7f7f,
          "a full-strength note keeps every alpha exactly");
    for (float over : {1.001f, 1.003f, 1.01f, 1.5f, 100.f})
        check(PortCeNoteColour(0xffffffff, over) == 0xffffffff && PortCeNoteColour(0xaf123456, over) == 0xaf123456, "a fade past 1 never wraps the alpha byte");
    check(PortCeNoteColour(0xffffffff, 0.f) == 0x00ffffff && PortCeNoteColour(0xffffffff, -0.5f) == 0x00ffffff && PortCeNoteColour(0xff7f7f7f, 0.f) == 0x007f7f7f,
          "a faded-out note has no alpha and keeps its colour");
    check(PortCeNoteColour(0xffffffff, 0.5f) == 0x7fffffff && PortCeNoteColour(0xaf000000, 0.5f) == 0x57000000, "half is half, rounded down");
}
#endif

static void ordinary() {
#ifdef CE_VERSION
    colours();
#endif
    render();
    check(draws.empty() && sprites.empty(), "nothing is drawn before a save has failed");
    // The options failure flag: set where the file was not written, taken (and cleared) by the site that asks.
    check(!PortCeTakeOptionsFailure(), "no options failure to begin with");
    PortCeOptionsWriteFailed();
    check(PortCeTakeOptionsFailure(), "an options write that failed is reported to the site that asks");
    check(!PortCeTakeOptionsFailure(), "and only once");
    PortCeOptionsWriteFailed(); PortCeOptionsWriteFailed();
    check(PortCeTakeOptionsFailure() && !PortCeTakeOptionsFailure(), "two failures before the ask are one report");
    // The front end's line, for the language change's message box and for the note.
    WCHAR out[256];
    check(PortCeSaveErrorMessage(out, 256) && std::wstring(out) == L"An error occurred during saving.", "the front end's own save error line");
    WCHAR small[8];
    check(PortCeSaveErrorMessage(small, 8) && std::wcslen(small) == 7 && std::wstring(small) == L"An erro", "a short buffer gets a whole, terminated, shorter line");
    check(!PortCeSaveErrorMessage(small, 0) && !PortCeSaveErrorMessage(nullptr, 8), "no buffer, no line");
    haveLine = false;
    check(!PortCeSaveErrorMessage(out, 256) && out[0] == 0, "without the line there is no message");
    PortCeSaveFailedNotice(); settled();
    check(draws.empty(), "and no note");
    haveLine = true;
    line = L"";
    check(!PortCeSaveErrorMessage(out, 256) && out[0] == 0, "an empty line is no message");
    PortCeSaveFailedNotice(); settled();
    check(draws.empty() && sprites.empty(), "and no note, not an empty backing");
    line = L"An error occurred during saving.";
    // The note in a level: the line, bottom centre, in the message box's look (its panel 0xaf000000, a two-pixel outline in 0xff7f7f7f, 8
    // pixels of padding), above the HUD's message bar (75 pixels up from the bottom of 640 x 480), inside the window.
    PortCeSaveFailedNotice(); render();
    check(draws.empty() && sprites.empty(), "a note has faded in by nothing at the moment it comes");
    settled();
    check(draws.size() == 1 && draws[0].colour == 0xffffffff, "the text is white at full strength");
    check(drew(L"An error occurred during saving.") && draws.size() == 1 && sprites.size() == 5, "the note says the line, on one panel with its outline");
    const FakeDraw &d = draws[0];
    check(d.x > 0 && d.x + float(std::wcslen(L"An error occurred during saving.") * 8) * d.sx <= PLATFORM.w + 0.01f, "the note fits the window");
    check(d.y > PLATFORM.h * 0.5f && d.y + 16 * d.sy < PLATFORM.h, "and sits in the lower half");
    check(sprites[0].x < d.x && sprites[0].y < d.y && sprites[0].x + sprites[0].w > d.x + 8 && sprites[0].colour == 0xaf000000, "the panel is behind the text");
    for (int i = 1; i < 5; ++i) check(sprites[i].colour == 0xff7f7f7f, "the outline is the message box's grey");
    check(std::abs(sprites[1].h - 2.f) < 0.01f && std::abs(sprites[2].h - 2.f) < 0.01f && std::abs(sprites[3].w - 2.f) < 0.01f && std::abs(sprites[4].w - 2.f) < 0.01f,
          "two pixels wide");
    check(std::abs(sprites[0].x + 8.f - d.x) < 0.01f && std::abs(sprites[0].y + 8.f - d.y) < 0.01f, "with 8 pixels of padding");
    check(sprites[0].y + sprites[0].h <= PLATFORM.h - 75.f - 4.f, "the note clears the HUD's message bar");
    photoOn = true; render();
    check(std::abs(sprites[0].y + sprites[0].h - (PLATFORM.h - 40.f + 8.f)) < 0.01f, "in photo mode, with the HUD hidden, it keeps the low place");
    photoOn = false;
    check(std::abs((d.x + float(std::wcslen(L"An error occurred during saving.") * 8) * d.sx * 0.5f) - PLATFORM.w * 0.5f) < 1.f, "centred");
    // Its clock is SDL's milliseconds: six seconds from the failure.
    ticks += 5600; render(); check(!draws.empty() && sprites[0].colour == 0xaf000000, "the note stays six seconds");
    ticks += 100; render(); check(!draws.empty() && sprites[0].colour == 0x57000000 && sprites[1].colour == 0x7f7f7f7f && draws[0].colour == 0x7fffffff, "fading out in its last 200 milliseconds, panel, outline and text");
    ticks += 200; render(); check(draws.empty() && sprites.empty(), "and then goes");
    PortCeSaveFailedNotice(); settled();
    check(!draws.empty(), "a later failure shows it again");
    // Never in the frame a screenshot saves.
    savedNext = true; render();
    check(draws.empty() && sprites.empty(), "nothing of the note is in the frame being saved");
    savedNext = false; render();
    check(!draws.empty(), "and it is back in the next frame");
    // Characters the font has no cell for are drawn as hyphens, never passed to it.
    ticks += 10000;
    line = L"Ungültig $ @ ~ Ж";
    PortCeSaveFailedNotice(); settled();
    check(drew(L"Ungültig - - - -"), "undrawable characters become hyphens");
    // A long line in a narrow window is scaled down to fit.
    ticks += 10000;
    line = std::wstring(100, L'W');
    PLATFORM.w = 320; PLATFORM.h = 240;
    PortCeSaveFailedNotice(); settled();
    check(!draws.empty() && draws[0].x >= 0 && draws[0].x + 100 * 8 * draws[0].sx <= PLATFORM.w + 0.01f && draws[0].sx < 0.5f, "a long line is scaled to fit a narrow window");
    PLATFORM.w = 640; PLATFORM.h = 480;
    // Without a font, a backing texture or a window, nothing is drawn and nothing breaks.
    ticks += 10000;
    line = L"An error occurred during saving.";
    PortCeSaveFailedNotice();
    ticks += 200;
    PLATFORM.present = false; render(); check(draws.empty(), "no font, no note"); PLATFORM.present = true;
    HUD.present = false; render(); check(draws.empty(), "no backing texture, no note"); HUD.present = true;
    PLATFORM.w = 0; render(); check(draws.empty(), "no window, no note"); PLATFORM.w = 640;
    render(); check(!draws.empty(), "and the note is still there when they are");
}

// Where the CE layer is off (a flag in the environment): nothing is kept, asked or drawn.
static void denied() {
    WCHAR out[256] = L"stale";
    check(!PortCeSaveErrorMessage(out, 256) && out[0] == 0, "no line where the CE layer is off");
    PortCeSaveFailedNotice(); render();
    check(draws.empty() && sprites.empty(), "no note where the CE layer is off");
    ticks += 100; render();
    check(draws.empty(), "and none a moment later");
}

int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "ordinary";
    if (mode == "ordinary") { ordinary(); std::puts("save error notice passed"); return 0; }
    if (mode == "denied") { denied(); std::puts("save error notice denied passed"); return 0; }
    std::fprintf(stderr, "unknown mode\n");
    return 2;
}
