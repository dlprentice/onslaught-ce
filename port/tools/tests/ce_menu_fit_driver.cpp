// SPDX-License-Identifier: GPL-3.0-only
// Run the production value-fit helper against a font that measures nine units a character; substitute only the font and the gate.
#include "Platform.h"
#include "ce_menu_fit.h"
#include <cstdio>
#include <cstring>
#include <string>

StandInPlatform PLATFORM;
static bool layerOn = true;
static int failures;
bool PortCeQualityOptionsAvailable() { return layerOn; }
WCHAR *PortCeValueFit(WCHAR *text, float x, float *scale);
WCHAR *PortCeListFit(WCHAR *text, float x);

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
    else return 2;
    std::printf("menu-fit %s failures=%d\n", name.c_str(), failures);
    return failures ? 1 : 0;
}
