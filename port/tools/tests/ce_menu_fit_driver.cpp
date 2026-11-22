// SPDX-License-Identifier: GPL-3.0-only
// Run the production value-fit helper against a font that measures nine units a character; substitute only the font, the clock, the gate,
// the front end's page numbers (FrontEndPage.h, written by the test) and the two answers ce_frontend.cpp and ce_quality_options.cpp give.
#include "Platform.h"
#include "FrontEndPage.h"
#include "ce_menu_fit.h"
#include <cstdio>
#include <cstring>
#include <string>

StandInPlatform PLATFORM;
static bool layerOn = true;
static int failures;
bool PortCeQualityOptionsAvailable() { return layerOn; }
static int qualityRow;                          // stands for one of the rows ce_quality_options.cpp adds to Video Options
bool PortCeQualityRow(const void *item) { return item == &qualityRow; }
// Onslaught CE's own page (FEP_LANGUAGE_TEST's slot) counts as an options page, as ce_frontend.cpp answers.
EFrontEndPage PortCeAsOptions(EFrontEndPage page) { return page == FEP_LANGUAGE_TEST ? FEP_OPTIONS : page; }
WCHAR *PortCeValueFit(WCHAR *text, float x, float *scale);
WCHAR *PortCeListFit(WCHAR *text, float x);
int PortCeListWindow(const void *owner, float y, long rowh, int count, int selection, int page, float *top, int *first, int *shown);
void PortCeListHover(int entry);
int PortCeCompactPitch(const void *item);
float PortCeCompactShift(int pitch, bool options_page);

// One frame of an expanded list: what RenderList asks, with the outputs preset to marks a call must overwrite or leave alone.
struct Frame { int laid; float top; int first; int shown; };
static Frame Draw(const void *owner, float y, int count, int selection, int page, float advance = 1.f / 60.f)
{
    PLATFORM.now += advance;
    Frame f = { 0, -999.f, -999, -999 };
    f.laid = PortCeListWindow(owner, y, 16, count, selection, page, &f.top, &f.first, &f.shown);
    return f;
}
// A window lies inside its page's limits, holds the selection and as many entries as fit (never fewer than the list has, if it fits).
static bool Inside(const Frame &f, int count, int selection, float top, float bottom)
{
    const int capacity = int((bottom - top) / 16.f);
    return f.laid == 1 && f.shown == (count < capacity ? count : capacity) && f.top >= top && f.top + 16.f * f.shown <= bottom &&
           f.first >= 0 && f.first + f.shown <= count && selection >= f.first && selection < f.first + f.shown;
}

static void Check(bool ok, const char *label) { if (!ok) { ++failures; std::printf("FAIL %s\n", label); } }
static void Wide(WCHAR *out, const char *in) { while ((*out++ = (WCHAR)(unsigned char)*in++)) {} }
static long Width(const WCHAR *text) { long n = 0; while (text[n]) ++n; return n * 9; }
static std::string Narrow(const WCHAR *text) { std::string s; while (*text) s += (char)*text++; return s; }

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const std::string name = argv[1];
    // The page's edge is pinned here, apart from the header's constant, so a changed edge cannot move its own expectations.
    const float edge = 600.f;
    Check(kCeValueRight == edge && kCeValueMinScale == 0.75f, "the column where a value ends and the smallest squeeze are the reviewed ones");
    const float x = 322.f;                       // where a value starts after its label (the middle of the page)
    const float room = edge - x;
    WCHAR text[160];
    float scale = -1.f;
    if (name == "fits")
    {
        Wide(text, "High (Recommended)");
        WCHAR *drawn = PortCeValueFit(text, x, &scale);
        Check(drawn == text && scale == 1.f, "a value that fits is drawn by the game's own call: same text, scale exactly 1");
    }
    else if (name == "exact")
    {
        // 30 characters is exactly 270 units: room is 278 here, so move x so that the text ends exactly on the edge.
        Wide(text, "123456789012345678901234567890");
        WCHAR *drawn = PortCeValueFit(text, edge - 270.f, &scale);
        Check(drawn == text && scale == 1.f, "a value ending exactly on the edge is not squeezed");
        drawn = PortCeValueFit(text, edge - 269.f, &scale);
        Check(drawn == text && scale < 1.f && scale >= kCeValueMinScale && 270.f * scale <= 269.f + 0.01f, "one unit less room squeezes it to fit");
    }
    else if (name == "adapter")
    {
        Wide(text, "NVIDIA GeForce RTX 4060 Laptop GPU");
        WCHAR *drawn = PortCeValueFit(text, x, &scale);
        Check(drawn == text, "a long adapter name that fits squeezed keeps every character");
        Check(scale < 1.f && scale >= kCeValueMinScale, "the squeeze stays within the readable range");
        Check((float)Width(text) * scale <= room + 0.01f && (float)Width(text) * scale > room - 2.f, "the squeezed name ends at the edge");
    }
    else if (name == "squeeze-limit")
    {
        Wide(text, "123456789012345678901234567890123456789012345678901");     // 51 characters: 459 units, 278 / 459 is below the limit
        WCHAR *drawn = PortCeValueFit(text, x, &scale);
        Check(drawn != text, "a value narrower than the limit would need is cut, not squeezed further");
        std::string cut = Narrow(drawn);
        Check(cut.size() >= 4 && cut.compare(cut.size() - 3, 3, "...") == 0, "the cut is marked with an ellipsis");
        Check(cut.size() < 51 + 3 && std::strncmp(cut.c_str(), "1234", 4) == 0, "the cut keeps the start of the text");
        Check(scale >= kCeValueMinScale && scale <= 1.f && (float)Width(drawn) * scale <= room + 0.01f, "the cut text fits at its scale");
        Check(Width(text) == 459 && text[50] == (WCHAR)'1' && text[51] == 0, "the original text is not modified");
        // the longest prefix that fits at the smallest scale is kept: one more character would not
        const long keep = (long)cut.size() - 3;
        std::string longer = cut.substr(0, keep) + (char)text[keep] + "...";
        Check((float)(longer.size() * 9) * kCeValueMinScale > room + 0.01f, "one more character would not fit");
    }
    else if (name == "just-inside-limit")
    {
        Wide(text, "12345678901234567890123456789012345678901");                // 41 characters: 369 units, 278 / 369 = 0.7534
        WCHAR *drawn = PortCeValueFit(text, x, &scale);
        Check(drawn == text && scale >= kCeValueMinScale && scale < 1.f, "a value that fits squeezed to just above the limit is squeezed, not cut");
    }
    else if (name == "no-room")
    {
        Wide(text, "NVIDIA GeForce RTX 4060 Laptop GPU");
        WCHAR *drawn = PortCeValueFit(text, edge + 10.f, &scale);
        Check(std::string("...") == Narrow(drawn) && scale == kCeValueMinScale, "no room at all draws only the ellipsis at the smallest scale");
    }
    else if (name == "layer-off")
    {
        layerOn = false;
        Wide(text, "NVIDIA GeForce RTX 4060 Laptop GPU with a name much longer than any page can hold at all");
        WCHAR *drawn = PortCeValueFit(text, x, &scale);
        Check(drawn == text && scale == 1.f, "outside the ordinary CE menus (faithful runs) the value is drawn as the game draws it");
    }
    else if (name == "extent-fails")
    {
        PLATFORM.font.fails = true;
        Wide(text, "NVIDIA GeForce RTX 4060 Laptop GPU");
        WCHAR *drawn = PortCeValueFit(text, x, &scale);
        Check(drawn == text && scale == 1.f, "a font that cannot measure leaves the draw alone");
    }
    else if (name == "null")
    {
        Check(PortCeValueFit(nullptr, x, &scale) == nullptr && scale == 1.f, "no text draws as before");
    }
    else if (name == "repeat")
    {
        Wide(text, "NVIDIA GeForce RTX 4060 Laptop GPU with Max-Q Design and more");
        WCHAR *first = PortCeValueFit(text, x, &scale);
        std::string a = Narrow(first);
        Wide(text, "Another Very Long Adapter Name That Needs To Be Cut Down To Fit");
        WCHAR *second = PortCeValueFit(text, x, &scale);
        Check(a != Narrow(second) && Narrow(second).compare(0, 7, "Another") == 0, "each call cuts its own text");
    }
    else if (name == "list-fits")
    {
        Wide(text, "NVIDIA GeForce RTX 4060 Lap");                      // 26 characters: 234 units
        Check(PortCeListFit(text, x) == text, "a list entry that fits is drawn and measured as before: the same text");
        Wide(text, "123456789012345678901234567890123456789012345");       // 45 characters: 405 units: too long
        Check(PortCeListFit(text, edge - 405.f) == text, "an entry ending exactly on the edge is not cut");
    }
    else if (name == "list-cut")
    {
        Wide(text, "NVIDIA GeForce RTX 4060 Laptop GPU");                // 34 characters: 306 units, room 278
        WCHAR *drawn = PortCeListFit(text, x);
        std::string cut = Narrow(drawn);
        Check(drawn != text && cut.size() >= 4 && cut.compare(cut.size() - 3, 3, "...") == 0, "a list entry that does not fit is cut and marked");
        Check(std::strncmp(cut.c_str(), "NVIDIA", 6) == 0, "the cut keeps the start of the entry");
        Check((float)Width(drawn) <= room + 0.01f, "the cut entry fits at scale 1, so the list's own measure of its widest entry fits too");
        Check(Width(text) == 306, "the original entry is not modified");
        const long keep = (long)cut.size() - 3;
        std::string longer = cut.substr(0, keep) + (char)text[keep] + "...";
        Check((float)(longer.size() * 9) > room + 0.01f, "one more character would not fit");
    }
    else if (name == "list-layer-off")
    {
        layerOn = false;
        Wide(text, "NVIDIA GeForce RTX 4060 Laptop GPU with a name much longer than any page can hold at all");
        Check(PortCeListFit(text, x) == text, "outside the ordinary CE menus a list entry is drawn as the game draws it");
    }
    else if (name == "list-extent-fails")
    {
        PLATFORM.font.fails = true;
        Wide(text, "NVIDIA GeForce RTX 4060 Laptop GPU");
        Check(PortCeListFit(text, x) == text && PortCeListFit(nullptr, x) == nullptr, "a font that cannot measure, or no text, leaves the entry alone");
    }
    // The limits are pinned here apart from the header's constants, as the value's edge is.
    else if (name == "window-limits")
    {
        Check(kCeListFrontTop == 104.f && kCeListFrontBottom == 452.f && kCeListPauseTop == 8.f && kCeListPauseBottom == 472.f,
              "the front end's list stays between its title bar and help line, the Pause Menu's within the screen");
        Check(kCeListScrollEvery == 0.15f && kCeListForget == 0.5f, "the reviewed pointer scroll rate and forgetting time");
        Check(kCeListEntryRoom == 16.f, "a window gives each entry the original's 16-unit row");
    }
    else if (name == "window-fits")
    {
        // The Shadow detail list (three entries) on its row, and a two-entry CE row: inside the limits, so the original lays them out.
        static int owner;
        for (int page : {FEP_OPTIONS, FEP_MAIN})
            for (int count : {2, 3})
            {
                Frame f = Draw(&owner, 240.f, count, 1, page);
                Check(f.laid == 0 && f.top == -999.f && f.first == -999 && f.shown == -999, "a list that fits is left to the original's calls, its outputs untouched");
            }
        SCeListPlace place = { -5, -5, -5.f };
        Check(!PortCeListPlace(181.f, 16, 3, 0, -1, 104.f, 452.f, &place) && place.first == -5 && place.top == -5.f, "the pure placement agrees");
        // the edges exactly: a list whose top is on the limit, and one whose end is
        Check(!PortCeListPlace(104.f + 16.f, 16, 3, 0, -1, 104.f, 452.f, &place), "a top exactly on the limit fits");
        Check(PortCeListPlace(104.f + 15.f, 16, 3, 0, -1, 104.f, 452.f, &place) && place.top == 104.f && place.shown == 3 && place.first == 0,
              "one unit higher is moved down to the limit, every entry shown");
        Check(!PortCeListPlace(452.f - 32.f, 16, 3, 0, -1, 104.f, 452.f, &place), "an end exactly on the limit fits");
        Check(PortCeListPlace(452.f - 31.f, 16, 3, 0, -1, 104.f, 452.f, &place) && place.top == 452.f - 48.f, "one unit lower is moved up to end on it");
    }
    else if (name == "window-sizes")
    {
        // 3, 14, 30 and 64 entries with the first, middle and last selected, on the Video Options page's Screen mode row (y 181 with
        // CE's rows) and near the top and bottom of the page, in the front end and in a level's Pause Menu.
        static int owners[4 * 3 * 4 * 2];
        int next = 0;
        for (int count : {3, 14, 30, 64})
            for (int selection : {0, count / 2, count - 1})
                for (float y : {121.f, 181.f, 300.f, 440.f})
                    for (int front = 0; front < 2; ++front)
                    {
                        const void *owner = &owners[next++];		// each a list just opened
                        const float top = front ? 104.f : 8.f, bottom = front ? 452.f : 472.f;
                        Frame f = Draw(owner, y, count, selection, front ? FEP_OPTIONS : FEP_MAIN);
                        const int original = int(y - ((count - 1) * 16L) / 2);
                        const bool fits = original >= top && original + 16 * count <= bottom;
                        if (fits) Check(f.laid == 0, "an original placement inside the limits is kept");
                        else Check(Inside(f, count, selection, top, bottom), "a list that does not fit is a window inside the limits");
                        if (!fits && count <= int((bottom - top) / 16.f)) Check(f.first == 0 && f.shown == count, "a list that fits once moved shows every entry");
                        if (!fits && f.shown < count && selection > f.shown && selection < count - f.shown)
                            Check(f.first == selection - f.shown / 2, "a window opens centred on the highlighted entry");
                    }
        static int screenMode;
        Frame f = Draw(&screenMode, 181.f, 14, 13, FEP_OPTIONS);
        Check(f.laid == 1 && f.top == 104.f && f.shown == 14 && f.first == 0, "the 1080p Screen mode list (14 sizes) starts under the title bar, whole");
        f = Draw(&screenMode, 181.f, 64, 63, FEP_LANGUAGE_TEST, 1.f);
        Check(f.laid == 1 && f.top == 104.f && f.shown == 21 && f.first == 43, "Onslaught CE's own page has the options page's limits: 21 of 64 entries");
        f = Draw(&screenMode, 120.f, 64, 0, FEP_MAIN, 1.f);
        Check(f.laid == 1 && f.top == 8.f && f.shown == 29 && f.first == 0, "the Pause Menu shows 29");
        // The small font measures rows of 15: a window is still sized and placed at 16 a row, so its panel stays on the page.
        SCeListPlace fifteen = { 0, 0, 0.f };
        Check(PortCeListPlace(181.f, 15, 22, 0, -1, 104.f, 452.f, &fifteen) && fifteen.shown == 21 && fifteen.top == 104.f,
              "22 sizes of 15 units: 21 shown under the title bar, as at 16");
        Check(PortCeListPlace(440.f, 15, 21, 20, -1, 104.f, 452.f, &fifteen) && fifteen.shown == 21 && fifteen.top == 452.f - 16.f * 21,
              "a window moved up ends 16 units an entry above the help line");
        f = Draw(&screenMode, 120.f, 0, 0, FEP_MAIN, 1.f);
        Check(f.laid == 0, "an empty list draws nothing new");
    }
    else if (name == "window-follows")
    {
        static int owner;
        Frame f = Draw(&owner, 181.f, 64, 40, FEP_OPTIONS);
        Check(f.laid == 1 && f.first == 30 && f.shown == 21, "opens centred on the highlight");
        f = Draw(&owner, 181.f, 64, 45, FEP_OPTIONS);
        Check(f.first == 30, "a highlight still in view does not move the window (the pointer can pick any entry it sees)");
        f = Draw(&owner, 181.f, 64, 52, FEP_OPTIONS);
        Check(f.first == 32, "a highlight below the window moves it the least that shows it");
        f = Draw(&owner, 181.f, 64, 31, FEP_OPTIONS);
        Check(f.first == 31, "and above it");
        f = Draw(&owner, 181.f, 64, 63, FEP_OPTIONS);
        Check(f.first == 43 && f.first + f.shown == 64, "the last entry ends the window");
        f = Draw(&owner, 181.f, 64, 0, FEP_OPTIONS);
        Check(f.first == 0, "the first starts it");
        f = Draw(&owner, 181.f, 64, -1, FEP_OPTIONS);
        Check(f.first == 0 && f.shown == 21, "no selection (an unreadable value) keeps a valid window");
    }
    else if (name == "window-forget")
    {
        static int one, two;
        Frame f = Draw(&one, 181.f, 64, 40, FEP_OPTIONS);
        f = Draw(&one, 181.f, 64, 52, FEP_OPTIONS);
        Check(f.first == 32, "moved by the highlight");
        f = Draw(&one, 181.f, 64, 52, FEP_OPTIONS, 0.6f);
        Check(f.first == 42, "a list not drawn for a while was closed: it opens centred again");
        f = Draw(&two, 181.f, 64, 52, FEP_OPTIONS);
        f = Draw(&two, 181.f, 64, 53, FEP_OPTIONS);
        Check(f.first == 42, "another list starts its own window");
        f = Draw(&one, 181.f, 64, 20, FEP_OPTIONS);
        Check(f.first == 10, "and the first list, drawn again after another, opens centred");
        PLATFORM.now = 3.f;
        f = Draw(&one, 181.f, 64, 20, FEP_OPTIONS, 0.f);
        f = Draw(&one, 181.f, 64, 30, FEP_OPTIONS, 0.f);
        Check(f.first == 10, "the same instant is still the same window");
        PLATFORM.now = 1.f;
        f = Draw(&one, 181.f, 64, 30, FEP_OPTIONS, 0.f);
        Check(f.first == 20, "a clock that went back is a list opened again");
    }
    else if (name == "window-edge")
    {
        static int owner;
        Frame f = Draw(&owner, 181.f, 64, 40, FEP_OPTIONS);
        Check(f.first == 30, "opened");
        PortCeListHover(40);
        f = Draw(&owner, 181.f, 64, 40, FEP_OPTIONS);
        Check(f.first == 30, "the pointer inside the window does not move it");
        PortCeListHover(50);
        f = Draw(&owner, 181.f, 64, 50, FEP_OPTIONS);
        Check(f.first == 31, "resting on the last entry moves it one down");
        PortCeListHover(51);
        f = Draw(&owner, 181.f, 64, 51, FEP_OPTIONS, 0.05f);
        Check(f.first == 31, "no faster than the scroll rate");
        PortCeListHover(51);
        f = Draw(&owner, 181.f, 64, 51, FEP_OPTIONS, 0.11f);
        Check(f.first == 32, "and again after it");
        PortCeListHover(52);
        f = Draw(&owner, 181.f, 64, 52, FEP_OPTIONS, 0.05f);
        Check(f.first == 32, "held back by the rate");
        f = Draw(&owner, 181.f, 64, 52, FEP_OPTIONS, 0.2f);
        Check(f.first == 32, "a frame without the pointer on an edge leaves it: the pointer of an earlier frame is forgotten");
        PortCeListHover(32);
        f = Draw(&owner, 181.f, 64, 32, FEP_OPTIONS, 0.2f);
        Check(f.first == 31, "resting on the first entry moves it one up");
        Draw(&owner, 181.f, 64, 63, FEP_OPTIONS, 0.2f);
        PortCeListHover(63);
        f = Draw(&owner, 181.f, 64, 63, FEP_OPTIONS, 0.2f);
        Check(f.first == 43, "nothing beyond the last entry: it stays");
        Draw(&owner, 181.f, 64, 0, FEP_OPTIONS, 0.2f);
        PortCeListHover(0);
        f = Draw(&owner, 181.f, 64, 0, FEP_OPTIONS, 0.2f);
        Check(f.first == 0, "nor before the first");
        Check(PortCeListEdgeStep(10, 21, 64, -1) == 0 && PortCeListEdgeStep(10, 21, 64, 9) == 0 && PortCeListEdgeStep(10, 21, 64, 31) == 0,
              "a pointer outside the window moves nothing");
    }
    else if (name == "window-page")
    {
        static int owner;
        // a row at y 130 with 30 entries: the front end's options pages keep it under the title bar, the Pause Menu does not need to
        for (int page : {FEP_OPTIONS, FEP_LANGUAGE_TEST})
        {
            Frame f = Draw(&owner, 130.f, 30, 0, page, 1.f);
            Check(f.laid == 1 && f.top == 104.f && f.shown == 21, "an options page holds 21 under its title bar");
        }
        Frame f = Draw(&owner, 130.f, 30, 0, FEP_MAIN, 1.f);
        Check(f.laid == 1 && f.top == 8.f && f.shown == 29, "any other page is the Pause Menu's: 29 from the top of the screen");
    }
    else if (name == "window-layer-off")
    {
        layerOn = false;
        static int owner;
        Frame f = Draw(&owner, 181.f, 64, 40, FEP_OPTIONS);
        Check(f.laid == 0 && f.top == -999.f && f.first == -999 && f.shown == -999, "outside the ordinary CE menus every list is the original's");
        PortCeListHover(3);
        Check(Draw(nullptr, 181.f, 64, 40, FEP_OPTIONS).laid == 0, "and no owner is no window");
    }
    else if (name == "pitch")
    {
        static int other;
        Check(kCeCompactPitch == 15 && kCeCompactFrontShift == 8.f && kCeCompactPauseShift == -8.f,
              "the reviewed pitch and front-end/pause shifts");
        Check(PortCeCompactPitch(&qualityRow) == 15 && PortCeCompactPitch(&other) == 0 && PortCeCompactPitch(nullptr) == 0,
              "only a list holding CE's Video Options rows is drawn at CE's pitch");
        Check(PortCeCompactShift(15, true) == 8.f && PortCeCompactShift(15, false) == -8.f && PortCeCompactShift(0, true) == 0.f && PortCeCompactShift(0, false) == 0.f,
              "it shifts down in the front end and up in pause, and only at CE's pitch");
        layerOn = false;
        Check(PortCeCompactPitch(&qualityRow) == 0, "outside the ordinary CE menus the list keeps its items' heights");
    }
    else return 2;
    std::printf("menu-fit %s failures=%d\n", name.c_str(), failures);
    return failures ? 1 : 0;
}
