#include "ce_editor_ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks = 0;
void Check(bool ok, const char *why) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
float Measure(void *, int face, const char *text) {
    if (!text) return face == CeEditorUiLarge ? 22.f : 16.f;
    float width = 0.f;
    for (; *text; ++text) width += *text == ' ' ? 5.f : (face == CeEditorUiLarge ? 12.f : 9.f);
    return width;
}
bool Apart(const CeEditorUiRect &a, const CeEditorUiRect &b) {
    return a.x + a.w <= b.x || b.x + b.w <= a.x ||
           a.y + a.h <= b.y || b.y + b.h <= a.y;
}
bool Inside(const CeEditorUiRect &r, float w, float h) {
    return r.x >= 0.f && r.y >= 0.f && r.w > 0.f && r.h > 0.f &&
           r.x + r.w <= w && r.y + r.h <= h;
}
void Describe(CeEditorUiModel &m, bool dirty, bool placing, bool hold, bool moving,
              const char *wanted) {
    CeEditorUiModel before = m;
    CeEditorUiDescribeEditing(m, dirty, placing, hold, moving);
    Check(std::strcmp(m.mode, wanted) == 0, "active tool or selection identity was misreported");
    Check(std::strcmp(m.state, dirty ? " / Draft" : " / Saved") == 0,
          "draft was confused with the saved file");
    CeEditorUiModel unchanged = m;
    unchanged.state = before.state;
    std::memcpy(unchanged.mode, before.mode, sizeof(before.mode));
    Check(std::memcmp(&unchanged, &before, sizeof(before)) == 0,
          "readback changed editor model inputs");
}
}

int main() {
    CeEditorUiModel m = {};
    m.width = 640; m.height = 480;
    std::strcpy(m.level, "Practice map");
    std::strcpy(m.project, "Project 2");
    Describe(m, true, false, false, false, "Tool: Select");
    m.card = true;
    std::strcpy(m.cardName, "Unit A");
    std::strcpy(m.cardKind, "Vehicle");
    std::strcpy(m.cardSide, "Friendly");
    std::strcpy(m.cardPosition, "X -120.5  Y 42.0 m");
    std::strcpy(m.cardHeight, "3.5 m");
    for (int focus = -1; focus < CeEditorUiToolMax; ++focus) {
        m.toolFocus = focus;
        Describe(m, false, false, false, false, "Select: Unit A");
        Describe(m, true, false, false, true, "Move: Unit A");
        Describe(m, true, true, false, false, "Place: Reach Point");
        Describe(m, true, true, true, false, "Place: Hold Area");
    }
    // A selected object remains present while placement is armed. The formatter
    // reads that selection; it does not simulate or test the native Play lifecycle.
    m.toolFocus = 0;
    Describe(m, true, true, true, false, "Place: Hold Area");
    Describe(m, false, true, true, false, "Place: Hold Area");
    Describe(m, true, true, true, false, "Place: Hold Area");
    Describe(m, true, false, false, false, "Select: Unit A");
    std::strcpy(m.objective, "Reach Point then Hold Area");
    const float sizes[][2] = {{640,480}, {1280,720}, {1920,1080}};
    for (const auto &size : sizes) {
        m.width=size[0]; m.height=size[1];
        CeEditorUiLayout l = {};
        CeEditorUiLay(m, Measure, nullptr, &l);
        Check(Inside(l.mode,m.width,m.height), "active-tool readout left the viewport");
        Check(Inside(l.objective,m.width,m.height), "objective chain left the viewport");
        Check(Apart(l.mode,l.objective), "active tool obscured the objective chain");
        Check(Apart(l.mode,l.project), "active tool obscured the project state");
        Check(Apart(l.objective,l.project), "objective chain obscured the project state");
        Check(Inside(l.card,m.width,m.height), "axis/height inspector left the viewport");
        Check(l.card.h >= 6.f*l.lineHeight + 8.f*l.scale,
              "height value did not receive its own readable inspector row");
        const float x=l.mode.x+l.mode.w/2.f, y=l.mode.y+l.mode.h/2.f;
        Check(CeEditorUiOverFurniture(l,m,x,y), "tool readout click would leak to the world");
        CeEditorUiMarker marker={x,y,true};
        Check(!CeEditorUiMarkerClear(l,m,marker), "selection marker covered active-tool text");
    }
    std::printf("editor state clarity: %d checks passed\n",checks);
    return 0;
}
