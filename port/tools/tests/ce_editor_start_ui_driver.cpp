// SPDX-License-Identifier: GPL-3.0-only
// The map editor's ten-command rail and the player's own start, measured through the production layout, readout and
// what-shows code against a stand-in font (nine units a character, sixteen high) at three window shapes. Nothing here
// reads the engine: the checks are on rectangles, hit tests, the mode readout and what a frame would draw.
#include "ce_editor_ui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
unsigned checks, failures;
void Check(bool ok, const std::string &why) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", why.c_str());
    }
}
float StandIn(void *, int face, const char *text) {
    if (!text) return face == CeEditorUiLarge ? 22.f : 16.f;
    return float(std::strlen(text)) * (face == CeEditorUiLarge ? 12.f : 9.f);
}
const float kEps = 0.01f;
bool Inside(const CeEditorUiRect &r, const CeEditorUiModel &m) {
    return r.w > 0 && r.h > 0 && r.x >= -kEps && r.y >= -kEps && r.x + r.w <= m.width + kEps && r.y + r.h <= m.height + kEps;
}
bool Within(const CeEditorUiRect &r, const CeEditorUiRect &outer) {
    return r.w > 0 && r.h > 0 && r.x >= outer.x - kEps && r.y >= outer.y - kEps && r.x + r.w <= outer.x + outer.w + kEps &&
           r.y + r.h <= outer.y + outer.h + kEps;
}
// Two rectangles that both show do not overlap.
bool Apart(const CeEditorUiRect &a, const CeEditorUiRect &b) {
    return a.w <= 0 || b.w <= 0 || a.x + a.w <= b.x + kEps || b.x + b.w <= a.x + kEps || a.y + a.h <= b.y + kEps ||
           b.y + b.h <= a.y + kEps;
}
float MidX(const CeEditorUiRect &r) { return r.x + r.w * 0.5f; }
float MidY(const CeEditorUiRect &r) { return r.y + r.h * 0.5f; }
bool Same(const CeEditorUiRect &a, const CeEditorUiRect &b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

// The command set, at its fixed indexes.
const char *const kTools[CeEditorUiToolMax] = {"Save", "Open", "Undo", "Redo", "Play draft", "Change Side",
                                               "Reach Point", "Hold Area", "Player Start", "Exit"};
const CeEditorUiPrompt kIdle[] = {{"Click", "Select"}, {"Tab", "Next Object"}, {"F5", "Play draft"}, {"F1", "Keys"}};
const char *const kShortNote = "Moves with the deck it is on.";
const char *const kLongNote = "Moves with the deck it is on: moving a carrier takes it along.";

void Base(CeEditorUiModel &m, float width, float height) {
    std::memset(&m, 0, sizeof(m));
    m.width = width;
    m.height = height;
    std::snprintf(m.level, sizeof(m.level), "%s", "2.21 - Escort Duty");
    std::snprintf(m.project, sizeof(m.project), "%s", "Project 12");
    m.toolCount = CeEditorUiToolMax;
    for (int i = 0; i < m.toolCount; ++i) {
        m.tools[i] = kTools[i];
        m.toolEnabled[i] = true;
    }
    m.promptCount = 4;
    for (int i = 0; i < 4; ++i) m.prompts[i] = kIdle[i];
}
void ObjectCard(CeEditorUiModel &m) {
    m.card = true;
    m.playerStartSelected = false;
    std::snprintf(m.cardName, sizeof(m.cardName), "%s", "Unit A");
    std::snprintf(m.cardKind, sizeof(m.cardKind), "%s", "Carrier");
    std::snprintf(m.cardSide, sizeof(m.cardSide), "%s", "Forseti");
    std::snprintf(m.cardPosition, sizeof(m.cardPosition), "%s", "X -120.5  Y 42.0 m");
    std::snprintf(m.cardHeight, sizeof(m.cardHeight), "%s", "3.5 m");
    m.cardNote[0] = 0;
}
// The start's inspector, as the editor supplies it through the card's own fields.
void StartCard(CeEditorUiModel &m, const char *note) {
    ObjectCard(m);
    m.playerStartSelected = true;
    std::snprintf(m.cardName, sizeof(m.cardName), "%s", "Player start");
    std::snprintf(m.cardKind, sizeof(m.cardKind), "%s", "Battle Engine walker");
    std::snprintf(m.cardNote, sizeof(m.cardNote), "%s", note);
}
// The readout names what was wanted, tells the draft from the saved file, and writes nothing but its two outputs.
void Describe(CeEditorUiModel &m, bool dirty, bool placing, bool hold, bool moving, const char *wanted, const std::string &where) {
    const CeEditorUiModel before = m;
    CeEditorUiDescribeEditing(m, dirty, placing, hold, moving);
    Check(!std::strcmp(m.mode, wanted), where + ": the readout says \"" + m.mode + "\", not \"" + wanted + "\"");
    Check(m.state && !std::strcmp(m.state, dirty ? " / Draft" : " / Saved"), where + ": the draft is told from the saved file");
    CeEditorUiModel same = m;
    same.state = before.state;
    std::memcpy(same.mode, before.mode, sizeof(same.mode));
    Check(!std::memcmp(&same, &before, sizeof(before)), where + ": the readout changed nothing but the mode and the state");
}
CeEditorUiStartShown StartAt(CeEditorUiModel &m, float x, float y, CeEditorUiLayout *layout = nullptr) {
    m.playerStartMarker = true;
    m.playerStartX = x;
    m.playerStartY = y;
    CeEditorUiLayout l;
    CeEditorUiLay(m, StandIn, nullptr, &l);
    if (layout) *layout = l;
    return CeEditorUiStartShows(l, m);
}

// Ten commands: ten distinct places inside the window, in index order, in their groups.
void RailChecks(float width, float height, const std::string &where) {
    CeEditorUiModel m;
    Base(m, width, height);
    Describe(m, true, false, false, false, "Tool: Select", where);
    CeEditorUiLayout l;
    CeEditorUiLay(m, StandIn, nullptr, &l);
    const float s = l.scale;
    Check(Inside(l.tools, m), where + ": the rail lies inside the window");
    for (int i = 0; i < CeEditorUiToolMax; ++i) {
        const CeEditorUiRect &r = l.tool[i];
        const std::string row = where + ": command " + std::to_string(i);
        Check(Inside(r, m) && Within(r, l.tools), row + " lies inside the rail and the window");
        Check(r.h >= l.lineHeight - kEps && StandIn(nullptr, CeEditorUiSmall, kTools[i]) * s <= r.w - 20.f * s + kEps,
              row + " has room for its word");
        Check(CeEditorUiToolAt(l, m, MidX(r), MidY(r)) == i && CeEditorUiToolAt(l, m, r.x + 1.f, r.y + 1.f) == i &&
              CeEditorUiToolAt(l, m, r.x + r.w - 1.f, r.y + r.h - 1.f) == i, row + " is hit at its own index");
        Check(CeEditorUiOverFurniture(l, m, MidX(r), MidY(r)), row + " is furniture: a click there never reaches the map");
        for (int j = i + 1; j < CeEditorUiToolMax; ++j)
            Check(Apart(r, l.tool[j]), row + " and command " + std::to_string(j) + " have places of their own");
        if (!i) continue;
        const CeEditorUiRect &above = l.tool[i - 1];
        const float gap = r.y - (above.y + above.h);
        const bool group = i == CeEditorUiToolUndo || i == CeEditorUiToolPlay || i == CeEditorUiToolSide ||
                           i == CeEditorUiToolReach || i == CeEditorUiToolExit;
        if (group) {
            Check(gap >= 4.f * s, row + " starts a group, a clear space under the one above");
            const float y = above.y + above.h + gap * 0.5f;
            Check(CeEditorUiToolAt(l, m, MidX(r), y) == -1 && CeEditorUiOverFurniture(l, m, MidX(r), y),
                  row + ": the space above it is no command, and still furniture");
        } else
            Check(std::fabs(gap) < kEps, row + " follows the one above in its group");
    }
    // Behind a list or a question no command is hit.
    m.dialog = CeEditorUiQuestion;
    m.dialogTitle = "Leave without saving?";
    m.dialogText = "Changes since your last save will be lost.";
    CeEditorUiLayout d;
    CeEditorUiLay(m, StandIn, nullptr, &d);
    for (int i = 0; i < CeEditorUiToolMax; ++i)
        Check(CeEditorUiToolAt(d, m, MidX(d.tool[i]), MidY(d.tool[i])) == -1, where + ": no command is hit behind a question");
}

// With everything up at once, no two pieces of furniture touch; the notice and the command's explanation share one place.
void FurnitureChecks(float width, float height, const std::string &where) {
    CeEditorUiModel m;
    Base(m, width, height);
    StartCard(m, kLongNote);
    std::snprintf(m.objective, sizeof(m.objective), "%s", "Reach Point then Hold Area");
    m.toolFocus = CeEditorUiToolPlay;
    Describe(m, true, false, false, false, "Select: Player start", where);
    CeEditorUiLayout l;
    CeEditorUiLay(m, StandIn, nullptr, &l);
    const CeEditorUiShown shown = CeEditorUiWhatShows(m);
    Check(shown.tools && shown.card && shown.prompt && shown.hint && !shown.notice, where + ": with no notice the focused command is explained");
    Check(l.hintTool == CeEditorUiToolPlay, where + ": the explanation is the focused command's");
    const CeEditorUiRect quiet[] = {l.title, l.project, l.mode, l.objective, l.tools, l.card, l.prompt, l.hint};
    const int n = int(sizeof(quiet) / sizeof(*quiet));
    for (int a = 0; a < n; ++a) {
        Check(Inside(quiet[a], m), where + ": furniture " + std::to_string(a) + " lies inside the window");
        for (int b = a + 1; b < n; ++b)
            Check(Apart(quiet[a], quiet[b]), where + ": furniture " + std::to_string(a) + " and " + std::to_string(b) + " do not overlap");
    }
    Check(CeEditorUiOverFurniture(l, m, MidX(l.hint), MidY(l.hint)), where + ": the explanation is furniture while it shows");
    // Every command's explanation fits its capsule and the window.
    for (int focus = 0; focus < CeEditorUiToolMax; ++focus) {
        m.toolFocus = focus;
        CeEditorUiLayout f;
        CeEditorUiLay(m, StandIn, nullptr, &f);
        const float text = (StandIn(nullptr, CeEditorUiSmall, kTools[focus]) + 8.f +
                            StandIn(nullptr, CeEditorUiSmall, kCeEditorUiToolNotes[focus])) * f.scale;
        Check(f.hintTool == focus && Inside(f.hint, m) && text + 2.f * f.capsuleText <= f.hint.w + kEps,
              where + ": command " + std::to_string(focus) + " is explained in a capsule that holds the sentence");
        Check(f.hint.y == l.hint.y && Same(f.card, l.card) && Same(f.tools, l.tools) && Same(f.mode, l.mode),
              where + ": nothing else moves with the focus");
    }
    m.toolFocus = CeEditorUiToolPlay;
    m.notice = "This object is far outside the level, so the view stays here.";
    m.noticeAlpha = 1.f;
    CeEditorUiLayout t;
    CeEditorUiLay(m, StandIn, nullptr, &t);
    const CeEditorUiShown noted = CeEditorUiWhatShows(m);
    Check(noted.notice && !noted.hint, where + ": a notice takes the explanation's place");
    Check(std::fabs(t.notice.y - l.hint.y) < kEps && Same(t.card, l.card), where + ": the notice has that one place, and the card stands still");
    const CeEditorUiRect loud[] = {t.title, t.project, t.mode, t.objective, t.tools, t.card, t.prompt, t.notice};
    for (int a = 0; a < n; ++a) {
        Check(Inside(loud[a], m), where + ": with a notice, furniture " + std::to_string(a) + " lies inside the window");
        for (int b = a + 1; b < n; ++b)
            Check(Apart(loud[a], loud[b]), where + ": with a notice, furniture " + std::to_string(a) + " and " + std::to_string(b) + " do not overlap");
    }
    // A long level name beside the placement readout: neither crosses the other.
    Base(m, width, height);
    std::snprintf(m.level, sizeof(m.level), "%s", "The Longest Level Name of the Game");
    std::snprintf(m.objective, sizeof(m.objective), "%s", "Reach Point then Hold Area");
    m.placingPlayerStart = true;
    Describe(m, true, false, false, false, "Place: Player Start", where);
    CeEditorUiLayout wide;
    CeEditorUiLay(m, StandIn, nullptr, &wide);
    Check(Inside(wide.mode, m) && Apart(wide.mode, wide.title) && Apart(wide.mode, wide.project) && Apart(wide.mode, wide.tools) &&
          Apart(wide.mode, wide.objective) && Apart(wide.objective, wide.tools) && Apart(wide.objective, wide.title) &&
          Inside(wide.objective, m), where + ": the readout and the objective keep clear of a long title");
}

// The focus is not the tool: moving it never arms, disarms or renames what the next click on the map does.
void FocusChecks(float width, float height, const std::string &where) {
    CeEditorUiModel m;
    Base(m, width, height);
    ObjectCard(m);
    m.placingPlayerStart = true;
    CeEditorUiLayout first = {};
    for (int focus = -1; focus < CeEditorUiToolMax; ++focus) {
        m.toolFocus = focus;
        const std::string at = where + ": placing the start, focus " + std::to_string(focus);
        Describe(m, true, false, false, false, "Place: Player Start", at);
        Describe(m, false, true, false, false, "Place: Player Start", at + ", over a reach point");
        Describe(m, true, true, true, true, "Place: Player Start", at + ", over a hold area and a move");
        Check(CeEditorUiActiveTool(m) == CeEditorUiToolPlayerStart, at + ": the armed command is Player Start");
        CeEditorUiLayout l;
        CeEditorUiLay(m, StandIn, nullptr, &l);
        if (focus == -1) first = l;
        Check(l.hintTool == CeEditorUiToolPlayerStart, at + ": the armed command is the one explained");
        Check(Same(l.mode, first.mode) && Same(l.hint, first.hint), at + ": the readout and the explanation stand still");
        for (int i = 0; i < CeEditorUiToolMax; ++i) Check(Same(l.tool[i], first.tool[i]), at + ": no command moves");
    }
    // Not placing: the focus walks the rail and the readout stays on the selection.
    m.placingPlayerStart = false;
    for (int focus = -1; focus < CeEditorUiToolMax; ++focus) {
        m.toolFocus = focus;
        const std::string at = where + ": an object selected, focus " + std::to_string(focus);
        Describe(m, false, false, false, false, "Select: Unit A", at);
        Check(CeEditorUiActiveTool(m) == -1, at + ": no command is armed");
        CeEditorUiLayout l;
        CeEditorUiLay(m, StandIn, nullptr, &l);
        Check(l.hintTool == focus, at + ": the explanation follows the focus only");
    }
    // The start selected: still no command armed, whatever is focused; placing takes the readout back.
    StartCard(m, kShortNote);
    for (int focus = -1; focus < CeEditorUiToolMax; ++focus) {
        m.toolFocus = focus;
        const std::string at = where + ": the start selected, focus " + std::to_string(focus);
        Describe(m, true, false, false, false, "Select: Player start", at);
        Check(CeEditorUiActiveTool(m) == -1, at + ": selecting the start arms nothing");
    }
    m.placingPlayerStart = true;
    Describe(m, true, false, false, false, "Place: Player Start", where + ": placing wins over the selected start");
    m.placingPlayerStart = false;
    // The modes the editor had before still read as they did, and arm the rows they name.
    ObjectCard(m);
    m.toolFocus = CeEditorUiToolSave;
    Describe(m, true, false, false, true, "Move: Unit A", where);
    Describe(m, true, true, false, false, "Place: Reach Point", where);
    Check(CeEditorUiActiveTool(m) == CeEditorUiToolReach, where + ": placing a reach point arms Reach Point");
    Describe(m, true, true, true, false, "Place: Hold Area", where);
    Check(CeEditorUiActiveTool(m) == CeEditorUiToolHold, where + ": placing a hold area arms Hold Area");
    Describe(m, false, false, false, false, "Select: Unit A", where);
    m.card = false;
    Describe(m, false, false, false, false, "Tool: Select", where);
    Check(CeEditorUiActiveTool(m) == -1, where + ": with nothing placed or selected no command is armed");
}

// The selected start's inspector: the card's rows, then the note, readable on one line or two.
void InspectorChecks(float width, float height, const std::string &where) {
    CeEditorUiModel m;
    Base(m, width, height);
    ObjectCard(m);
    Describe(m, false, false, false, false, "Select: Unit A", where);
    CeEditorUiLayout plain;
    CeEditorUiLay(m, StandIn, nullptr, &plain);
    Check(Inside(plain.card, m) && plain.note.w <= 0 && plain.note.h <= 0, where + ": a card without a note has no note's place");
    const char *const notes[2] = {kShortNote, kLongNote};
    for (int v = 0; v < 2; ++v) {
        const std::string at = where + (v ? ": a long note" : ": a short note");
        StartCard(m, notes[v]);
        Describe(m, true, false, false, false, "Select: Player start", at);
        CeEditorUiLayout l;
        CeEditorUiLay(m, StandIn, nullptr, &l);
        const float s = l.scale;
        Check(CeEditorUiWhatShows(m).card && Inside(l.card, m), at + ": the inspector shows inside the window");
        Check(std::fabs(l.card.y + l.card.h - (plain.card.y + plain.card.h)) < kEps && l.card.h > plain.card.h,
              at + ": the card grows upward for it and its foot stays");
        Check(Within(l.note, l.card) && l.note.y >= l.card.y + plain.card.h - 6.f * s - kEps,
              at + ": the note lies in the card, under the rows");
        const int lines = l.noteBreak ? 2 : 1;
        Check(l.note.h >= lines * l.lineHeight - kEps, at + ": the note's place is as tall as its lines");
        if (l.noteBreak) {
            const std::string text = notes[v];
            Check(l.noteBreak > 0 && l.noteBreak < int(text.size()) && text[size_t(l.noteBreak)] == ' ', at + ": it breaks at a space");
            const std::string one = text.substr(0, size_t(l.noteBreak)), two = text.substr(size_t(l.noteBreak) + 1);
            Check(!one.empty() && !two.empty() && StandIn(nullptr, CeEditorUiSmall, one.c_str()) * s <= l.note.w + kEps &&
                  StandIn(nullptr, CeEditorUiSmall, two.c_str()) * s <= l.note.w + kEps, at + ": both lines fit the card");
        } else
            Check(StandIn(nullptr, CeEditorUiSmall, notes[v]) * s <= l.note.w + kEps, at + ": the line fits the card");
        Check(StandIn(nullptr, CeEditorUiSmall, m.cardName) * s <= l.card.w - 20.f * s + kEps, at + ": the name fits the card");
        Check(Apart(l.card, l.tools) && Apart(l.card, l.mode) && Apart(l.card, l.prompt) && Apart(l.card, l.hint) &&
              Apart(l.card, l.title) && Apart(l.card, l.project), at + ": the inspector touches no other furniture");
        Check(CeEditorUiOverFurniture(l, m, MidX(l.note), MidY(l.note)), at + ": the note is part of the card's furniture");
    }
}

// The start's mark: drawn out in the level with its name, never across the furniture, and a click on it reaches the map.
void MarkerChecks(float width, float height, const std::string &where) {
    CeEditorUiModel m;
    Base(m, width, height);
    std::snprintf(m.objective, sizeof(m.objective), "%s", "Reach Point then Hold Area");
    Describe(m, true, false, false, false, "Tool: Select", where);
    CeEditorUiLayout l;
    const float ox = width * 0.45f, oy = height * 0.35f;
    CeEditorUiStartShown shown = StartAt(m, ox, oy, &l);
    const float s = l.scale;
    Check(shown.marker && shown.label, where + ": out in the level the start shows its mark and its name");
    Check(Inside(l.start, m) && l.start.y >= oy && std::fabs(MidX(l.start) - ox) < 0.5f &&
          l.start.w >= StandIn(nullptr, CeEditorUiSmall, kCeEditorUiStartLabel) * s - kEps && l.start.h >= l.lineHeight - kEps,
          where + ": the name sits under the mark, centred, with room for its words");
    Check(!CeEditorUiOverFurniture(l, m, ox, oy) && !CeEditorUiOverFurniture(l, m, MidX(l.start), MidY(l.start)),
          where + ": the mark and its name are not furniture");
    m.playerStartSelected = true;
    shown = StartAt(m, ox, oy);
    Check(shown.marker && shown.label, where + ": selected, the start still shows");
    m.playerStartSelected = false;
    m.placingPlayerStart = true;
    shown = StartAt(m, ox, oy);
    Check(shown.marker && shown.label, where + ": while a new place is chosen the present start still shows");
    m.placingPlayerStart = false;
    m.playerStartMarker = false;
    CeEditorUiLayout none;
    CeEditorUiLay(m, StandIn, nullptr, &none);
    shown = CeEditorUiStartShows(none, m);
    Check(!shown.marker && !shown.label && none.start.w <= 0, where + ": a start that is not in view has no mark");
    // Over each piece of furniture, and where its reach touches one, the mark is not drawn.
    ObjectCard(m);
    Describe(m, true, false, false, false, "Select: Unit A", where);
    (void)StartAt(m, ox, oy, &l);
    const CeEditorUiRect covered[] = {l.title, l.project, l.mode, l.objective, l.tools, l.card, l.prompt, l.hint};
    for (int i = 0; i < int(sizeof(covered) / sizeof(*covered)); ++i) {
        const std::string at = where + ": furniture " + std::to_string(i);
        Check(covered[i].w > 0 && covered[i].h > 0, at + " shows");
        shown = StartAt(m, MidX(covered[i]), MidY(covered[i]));
        Check(!shown.marker && !shown.label, at + ": a start under it is not drawn");
        shown = StartAt(m, covered[i].x + covered[i].w + 8.f * s, MidY(covered[i]));
        Check(!shown.marker && !shown.label, at + ": nor one whose mark reaches its edge");
    }
    m.notice = "Saved as Project 12. Earlier saves are kept.";
    m.noticeAlpha = 1.f;
    CeEditorUiLayout noted;
    (void)StartAt(m, ox, oy, &noted);
    shown = StartAt(m, MidX(noted.notice), MidY(noted.notice));
    Check(noted.notice.w > 0 && !shown.marker && !shown.label, where + ": a start under a notice is not drawn");
    m.notice = nullptr;
    // The name alone is left out where only it would cross the furniture or leave the window.
    m.card = false;
    Describe(m, true, false, false, false, "Tool: Select", where);
    (void)StartAt(m, ox, oy, &l);
    shown = StartAt(m, MidX(l.hint), l.hint.y - 17.5f * s);
    Check(shown.marker && !shown.label, where + ": a name that would cross the explanation is left out, the mark stays");
    shown = StartAt(m, width - 20.f * s, oy);
    Check(shown.marker && !shown.label, where + ": a name that would leave the window is left out, the mark stays");
    shown = StartAt(m, -5.f, oy);
    Check(!shown.marker && !shown.label, where + ": a start outside the window is not drawn");
    shown = StartAt(m, ox, height + 5.f);
    Check(!shown.marker && !shown.label, where + ": nor one below it");
    // Behind a question, a list or the held help, and in a play test, nothing of the start is drawn.
    m.dialog = CeEditorUiQuestion;
    m.dialogTitle = "Leave without saving?";
    m.dialogText = "Changes since your last save will be lost.";
    shown = StartAt(m, ox, oy);
    Check(!shown.marker && !shown.label, where + ": a question hides the start");
    m.dialog = CeEditorUiNoDialog;
    m.help = true;
    shown = StartAt(m, ox, oy);
    Check(!shown.marker && !shown.label, where + ": the held help hides the start");
    m.help = false;
    m.playing = true;
    shown = StartAt(m, ox, oy);
    Check(!shown.marker && !shown.label && CeEditorUiActiveTool(m) == -1, where + ": a play test shows no start and arms nothing");
    m.playing = false;
    shown = StartAt(m, ox, oy);
    Check(shown.marker && shown.label, where + ": back in the editor it shows again");
    // An object's ring keeps the same rule it had.
    const CeEditorUiMarker open = {ox, oy, true}, hidden = {MidX(l.tools), MidY(l.tools), true};
    Check(CeEditorUiMarkerClear(l, m, open) && !CeEditorUiMarkerClear(l, m, hidden), where + ": an object's ring shows in the level and not over the rail");
}
} // namespace

int main() {
    const float shapes[3][2] = {{640, 480}, {1280, 720}, {1920, 1080}};
    for (const auto &shape : shapes) {
        const std::string where = std::to_string(int(shape[0])) + "x" + std::to_string(int(shape[1]));
        RailChecks(shape[0], shape[1], where);
        FurnitureChecks(shape[0], shape[1], where);
        FocusChecks(shape[0], shape[1], where);
        InspectorChecks(shape[0], shape[1], where);
        MarkerChecks(shape[0], shape[1], where);
    }
    // Every command has its sentence, and the help still fits its panel's count.
    for (int i = 0; i < CeEditorUiToolMax; ++i)
        Check(kCeEditorUiToolNotes[i] && kCeEditorUiToolNotes[i][0], "command " + std::to_string(i) + " has an explanation");
    std::printf("map editor player start: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
