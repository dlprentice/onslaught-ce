// SPDX-License-Identifier: GPL-3.0-only
// The map editor's screen furniture (ce_editor_ui.h). Sizes are in units of the 640 x 480 screen, multiplied by the
// window's scale (the smaller of width / 640 and height / 480) and anchored to the window's corners, as the HUD's
// corner groups are on a wide screen. Text is drawn at the font's own size times that scale and never squeezed.
#include "ce_editor_ui.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

const CeEditorUiPrompt kCeEditorUiHelp[] = {
    {"W A S D", "Move the View"},
    {"Right Drag", "Turn the View"},
    {"Wheel", "Zoom"},
    {"Shift", "Move Faster"},
    {"Click", "Select an Object"},
    {"Drag", "Move the Object"},
    {"Page Up, Page Down", "Raise, Lower"},
    {"T", "Change Side"},
    {"R, H", "Reach Point, Hold Area"},
    {"Delete", "Reset Start or Remove Stop"},
    {"Tab", "Next Object"},
    {"C", "Centre on the Object"},
    {"Home", "See All Objects"},
    {"Up, Down, Enter", "Choose, Use a Tool"},
    {"Ctrl+S, Ctrl+O", "Save, Open"},
    {"Ctrl+Z, Ctrl+Y", "Undo, Redo"},
    {"F5, F6", "Play Draft, Reset Draft"},
    {"Esc", "Cancel or Leave"},
    {0, 0},
};
const char *const kCeEditorUiButtons[2] = {"No", "Yes"};
// One short sentence a command, in the command set's order. Play never saves and Save is the only command that does; the
// player start is a place of its own, which no object carries with it.
const char *const kCeEditorUiToolNotes[CeEditorUiToolMax] = {
    "Save your changes as a new project.",
    "Load a saved project.",
    "Take back the last change.",
    "Restore the change taken back.",
    "Try the draft now. Nothing is saved.",
    "Change the selected unit's side.",
    "A place the player must reach.",
    "An area the player must hold.",
    "Where you begin. It moves with a deck it is on.",
    "Leave the editor.",
};
const char *const kCeEditorUiStartLabel = "Player Start";

namespace {
const char *const kHelpTitle = "Keys";
const char *const kCardLabels[4] = {"Kind", "Side", "Position", "Height"};
const char *const kBannerTitle = "Playing draft";
const char *const kResetKey = "F6";
const char *const kResetAction = "Reset";
const char *const kBannerKey = "F5";
const char *const kBannerAction = "Back to editor";
const float kScrollBand = 14.f, kKeyGap = 8.f, kPairGap = 22.f, kHudRight = 290.f;
// The widest position the card shows, so its value column never changes width while an object moves.
const char *const kWidestPosition = "X -9999.9  Y -9999.9 m";
const float kMargin = 12.f, kRowPitch = 20.f, kPanelPadX = 10.f, kPanelPadY = 6.f, kCapsulePad = 6.f, kGap = 8.f;
// Two capsules, one above the other, stand this far apart (the HUD's caps reach a little beyond the bar); the space and
// thin rule between two groups of the command rail; a mark's reach from its centre (the selected ring with its ticks).
const float kCapsuleGap = 12.f, kGroupGap = 6.f, kMarkReach = 16.f;
const char *const kPlacePrefix = "Place: ";

// The command rail is the whole command set and nothing else.
bool Rail(const CeEditorUiModel &m) { return m.toolCount == CeEditorUiToolMax; }
// True for the first command of a group after the first group.
bool GroupStart(int tool) {
    return tool == CeEditorUiToolUndo || tool == CeEditorUiToolPlay || tool == CeEditorUiToolSide ||
           tool == CeEditorUiToolReach || tool == CeEditorUiToolExit;
}
bool Cross(const CeEditorUiRect &a, const CeEditorUiRect &b) {
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
// True when a rectangle crosses a panel or capsule that shows.
bool Covered(const CeEditorUiLayout &l, const CeEditorUiModel &m, const CeEditorUiRect &r) {
    const CeEditorUiShown s = CeEditorUiWhatShows(m);
    return Cross(r, l.title) || Cross(r, l.project) || Cross(r, l.mode) || Cross(r, l.objective) ||
           (s.tools && Cross(r, l.tools)) || (s.card && Cross(r, l.card)) || (s.prompt && Cross(r, l.prompt)) ||
           (s.notice && Cross(r, l.notice)) || (s.hint && Cross(r, l.hint));
}

float Measure(CeEditorUiMeasure measure, void *context, int face, const char *text) {
    return measure && text && text[0] ? measure(context, face, text) : 0.f;
}
float Max(float a, float b) { return a > b ? a : b; }
CeEditorUiRect Rect(float x, float y, float w, float h) {
    CeEditorUiRect r = {x, y, w, h};
    return r;
}
int Lines(const char *text) {
    if (!text || !text[0]) return 0;
    int n = 1;
    for (const char *c = text; *c; ++c) n += *c == '\n';
    return n;
}
// The widest line of a text that may hold line breaks.
float Widest(CeEditorUiMeasure measure, void *context, int face, const char *text) {
    if (!text) return 0.f;
    char line[256];
    float widest = 0.f;
    const char *c = text;
    while (*c) {
        size_t n = 0;
        while (c[n] && c[n] != '\n') ++n;
        const size_t k = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        memcpy(line, c, k);
        line[k] = 0;
        widest = Max(widest, Measure(measure, context, face, line));
        c += n;
        if (*c == '\n') ++c;
    }
    return widest;
}
// Where a note wider than its column breaks: the characters on its first line, at the last space that lets that line fit.
// 0 when the note fits on one line or holds no such space (the column then widens: text is never squeezed).
int NoteBreak(CeEditorUiMeasure measure, void *context, const char *note, float width) {
    if (Measure(measure, context, CeEditorUiSmall, note) <= width) return 0;
    char line[CeEditorUiTextMax];
    int best = 0;
    for (int i = 0; note[i] && i + 1 < CeEditorUiTextMax; ++i) {
        if (note[i] != ' ' || !i) continue;
        memcpy(line, note, size_t(i));
        line[i] = 0;
        if (Measure(measure, context, CeEditorUiSmall, line) > width) break;
        best = i;
    }
    return best;
}
// The two lines of a note that breaks after its first `first` characters.
void NoteLines(const char *note, int first, char *one, char *two) {
    int n = first > 0 && first < CeEditorUiTextMax ? first : 0;
    memcpy(one, note, size_t(n));
    one[n] = 0;
    snprintf(two, CeEditorUiTextMax, "%s", note + n + (n ? 1 : 0));
}
} // namespace

void CeEditorUiDescribeEditing(CeEditorUiModel &m, bool dirty,
                              bool placing, bool holdArea, bool moving) {
    m.state = dirty ? " / Draft" : " / Saved";
    // What the next click on the map does comes first; then what is selected. The focused command is never read here.
    if (m.placingPlayerStart) {
        snprintf(m.mode, sizeof(m.mode), "%sPlayer Start", kPlacePrefix);
    } else if (placing) {
        snprintf(m.mode, sizeof(m.mode), "%s%s", kPlacePrefix, holdArea ? "Hold Area" : "Reach Point");
    } else if (m.playerStartSelected) {
        snprintf(m.mode, sizeof(m.mode), "%s: %s", moving ? "Move" : "Select", kCeEditorUiStartLabel);
    } else if (m.card) {
        snprintf(m.mode, sizeof(m.mode), "%s: %.24s", moving ? "Move" : "Select", m.cardName);
    } else {
        snprintf(m.mode, sizeof(m.mode), "Tool: Select");
    }
}

int CeEditorUiActiveTool(const CeEditorUiModel &m) {
    if (m.playing) return -1;
    if (m.placingPlayerStart) return m.toolCount > CeEditorUiToolPlayerStart ? int(CeEditorUiToolPlayerStart) : -1;
    const size_t n = strlen(kPlacePrefix);
    if (strncmp(m.mode, kPlacePrefix, n)) return -1;
    for (int i = 0; i < m.toolCount && i < CeEditorUiToolMax; ++i)
        if (m.tools[i] && !strcmp(m.mode + n, m.tools[i])) return i;
    return -1;
}

void CeEditorUiLay(const CeEditorUiModel &m, CeEditorUiMeasure measure, void *context, CeEditorUiLayout *out) {
    CeEditorUiLayout l;
    l = CeEditorUiLayout{};
    const float s = m.width / 640.f < m.height / 480.f ? m.width / 640.f : m.height / 480.f;
    l.scale = s > 0.f ? s : 1.f;
    const float k = l.scale;
    l.margin = kMargin * k;
    l.rowPitch = kRowPitch * k;
    float lh = measure ? measure(context, CeEditorUiSmall, 0) : 0.f, large = measure ? measure(context, CeEditorUiLarge, 0) : 0.f;
    if (!(lh > 0.f)) lh = 12.f;
    if (!(large > 0.f)) large = 20.f;
    l.lineHeight = lh * k;
    l.largeHeight = large * k;
    const float capH = (lh + 2.f * kCapsulePad) * k, capEnd = capH * 0.5f + 4.f * k;
    l.capsuleH = capH;
    l.capsuleText = capEnd;
    l.textDrop = (capH - l.lineHeight) * 0.5f;
    // The prompt capsule along the bottom, centred: key then action, pairs a wide space apart.
    float promptW = 0.f;
    for (int i = 0; i < m.promptCount && i < CeEditorUiPromptMax; ++i)
        promptW += (Measure(measure, context, CeEditorUiSmall, m.prompts[i].key) + kKeyGap +
                    Measure(measure, context, CeEditorUiSmall, m.prompts[i].action) + (i ? kPairGap : 0.f)) * k;
    if (m.promptCount > 0) {
        const float w = promptW + 2.f * capEnd;
        l.prompt = Rect((m.width - w) * 0.5f, m.height - l.margin - capH, w, capH);
    }
    // Everything above the prompt line's place, whether or not the line is drawn, so nothing moves when it hides. One
    // capsule's place lies right above the prompt line, across the window's middle: a passing notice's, and the command's
    // explanation when there is no notice. The card stands above that place, so the command rail has the left edge down to
    // it and a notice never crosses the rail or the card.
    const float lane = m.height - l.margin - capH - (kCapsuleGap + lh + 2.f * kCapsulePad) * k;
    const float cardBottom = lane - kGap * k;
    // The card's name and four lines. A note adds its lines under them and the card grows upward: its foot never moves.
    const float cardH = 4.f * (lh + 4.f) * k + 2.f * kPanelPadY * k + 2.f * k + (lh + 2.f) * k;
    l.hintTool = -1;
    if (m.playing) {
        const float actions = (Measure(measure, context, CeEditorUiSmall, kResetKey) + kKeyGap +
            Measure(measure, context, CeEditorUiSmall, kResetAction) + kPairGap +
            Measure(measure, context, CeEditorUiSmall, kBannerKey) + kKeyGap +
            Measure(measure, context, CeEditorUiSmall, kBannerAction)) * k + 2.f * capEnd;
        const float withTitle = actions + (Measure(measure, context, CeEditorUiSmall, kBannerTitle) + kPairGap) * k;
        l.bannerTitle = withTitle <= m.width - kHudRight * k - l.margin;
        const float w = l.bannerTitle ? withTitle : actions;
        float x = (m.width - w) * 0.5f;
        // Clear of the HUD's top-right group (it starts about 270 units from the right edge).
        if (x + w > m.width - kHudRight * k) x = m.width - kHudRight * k - w;
        if (x < l.margin) x = l.margin;
        l.banner = Rect(x, l.margin, w, capH);
        if (m.objective[0]) {
            const float ow = Measure(measure,context,CeEditorUiSmall,m.objective)*k+2.f*capEnd;
            l.objective=Rect(l.margin,l.banner.y+capH+12.f*k,ow,capH);
        }
    } else {
        // Title capsule top left: "Map Editor", the level's own name; the project and its state in a capsule under it.
        const float w = (Measure(measure, context, CeEditorUiSmall, "Map Editor") + kPairGap +
                         Measure(measure, context, CeEditorUiSmall, m.level)) * k + 2.f * capEnd;
        l.title = Rect(l.margin, l.margin, w, capH);
        const float state = m.state && m.state[0] ? Measure(measure, context, CeEditorUiSmall, m.state) : 0.f;
        const float pw = (Measure(measure, context, CeEditorUiSmall, m.project) + state) * k + 2.f * capEnd;
        // The HUD's caps stand a little above and below the bar: the gaps keep two capsules' caps apart.
        l.project = Rect(l.margin, l.title.y + l.title.h + 12.f * k, pw, capH);
        if (m.mode[0]) {
            const float ow=Measure(measure,context,CeEditorUiSmall,m.mode)*k+2.f*capEnd;
            l.mode=Rect(m.width-l.margin-ow,l.margin,ow,capH);
            // A long name beside a long level name: the readout drops to the project's row rather than cross the title.
            CeEditorUiRect reach = Rect(l.mode.x - kGap * k, l.mode.y, l.mode.w + kGap * k, l.mode.h);
            if (Cross(reach, l.title)) {
                reach.y = l.project.y;
                if (!Cross(reach, l.project)) l.mode.y = l.project.y;
            }
        }
    if (m.objective[0]) {
            const float ow=Measure(measure,context,CeEditorUiSmall,m.objective)*k+2.f*capEnd;
            l.objective=Rect(m.width-l.margin-ow,l.margin,ow,capH);
            if (l.mode.h > 0.f) l.objective.y = l.mode.y + l.mode.h + 3.f * k;
    }
        // The tool list: a short column on a panel at the left edge. The command set stands in its groups, a little space
        // and a thin rule between two of them; a row's place is its command's index, whatever the spacing.
        if (m.toolCount > 0) {
            const bool rail = Rail(m);
            float widest = 70.f;
            for (int i = 0; i < m.toolCount && i < CeEditorUiToolMax; ++i)
                widest = Max(widest, Measure(measure, context, CeEditorUiSmall, m.tools[i]));
            const float w = (widest + 2.f * kPanelPadX) * k, top = l.project.y + l.project.h + 12.f * k;
            float y = top + kPanelPadY * k;
            for (int i = 0; i < m.toolCount && i < CeEditorUiToolMax; ++i) {
                if (rail && GroupStart(i)) y += kGroupGap * k;
                l.tool[i] = Rect(l.margin, y, w, l.rowPitch);
                y += l.rowPitch;
            }
            l.tools = Rect(l.margin, top, w, y + kPanelPadY * k - top);
            // One command explained in the notice's place: the armed one while a click on the map would place it, else
            // the focused one. A sentence too long for the window is left out, never squeezed.
            const int armed = CeEditorUiActiveTool(m), told = armed >= 0 ? armed : m.toolFocus;
            if (rail && told >= 0 && told < CeEditorUiToolMax && m.tools[told] && m.tools[told][0]) {
                char keyLabel[CeEditorUiTextMax];
                snprintf(keyLabel, sizeof(keyLabel), "%s:", m.tools[told]);
                const float hw = (Measure(measure, context, CeEditorUiSmall, keyLabel) + kKeyGap +
                                  Measure(measure, context, CeEditorUiSmall, kCeEditorUiToolNotes[told])) * k + 2.f * capEnd;
                if (hw <= m.width - 2.f * l.margin) {
                    l.hint = Rect((m.width - hw) * 0.5f, lane, hw, capH);
                    l.hintTool = told;
                }
            }
        }
        // The selected object's card, bottom right above the notice's place: its name, then Kind, Side, Position and
        // Height. The value column is as wide as the widest position, so the card stands still while the object moves.
        if (m.card && !m.placingPlayerStart) {
            float labels = 0.f, values = Measure(measure, context, CeEditorUiSmall, kWidestPosition);
            const char *v[4] = {m.cardKind, m.cardSide, m.cardPosition, m.cardHeight};
            for (int i = 0; i < 4; ++i) {
                labels = Max(labels, Measure(measure, context, CeEditorUiSmall, kCardLabels[i]));
                values = Max(values, Measure(measure, context, CeEditorUiSmall, v[i]));
            }
            float inner = Max(Measure(measure, context, CeEditorUiSmall, m.cardName), labels + 10.f + values);
            // A note: a sentence under a thin rule, on one line or broken once at a space to the card's own width.
            float noteH = 0.f;
            if (m.cardNote[0]) {
                l.noteBreak = NoteBreak(measure, context, m.cardNote, inner);
                if (l.noteBreak) {
                    char one[CeEditorUiTextMax], two[CeEditorUiTextMax];
                    NoteLines(m.cardNote, l.noteBreak, one, two);
                    inner = Max(inner, Measure(measure, context, CeEditorUiSmall, two));
                } else
                    inner = Max(inner, Measure(measure, context, CeEditorUiSmall, m.cardNote));
                noteH = ((l.noteBreak ? 2.f : 1.f) * (lh + 4.f) + 2.f) * k;
            }
            const float w = (inner + 2.f * kPanelPadX) * k;
            l.card = Rect(m.width - l.margin - w, cardBottom - cardH - noteH, w, cardH + noteH);
            // The card never hides what it describes: when the selected mark (and a start's name under it) would sit
            // under the card at the bottom right, the card docks at the top right instead, under the readouts there.
            float sx = -1.f, sy = -1.f;
            if (m.playerStartSelected && m.playerStartMarker) { sx = m.playerStartX; sy = m.playerStartY + (kMarkReach + 1.f) * k + l.lineHeight; }
            for (int i = 0; m.markers && i < m.markerCount && sx < 0.f; ++i)
                if (m.markers[i].selected) { sx = m.markers[i].x; sy = m.markers[i].y; }
            const float reach = kMarkReach * k;
            const CeEditorUiRect around = Rect(l.card.x - reach, l.card.y - reach, l.card.w + 2.f * reach, l.card.h + 2.f * reach);
            if (sx >= 0.f && around.Contains(sx, sy)) {
                float top = l.margin + capH + 12.f * k;
                if (l.mode.h > 0.f) top = Max(top, l.mode.y + l.mode.h + 12.f * k);
                if (l.objective.h > 0.f) top = Max(top, l.objective.y + l.objective.h + 12.f * k);
                const CeEditorUiRect high = Rect(l.card.x, top, l.card.w, l.card.h);
                const CeEditorUiRect highAround = Rect(high.x - reach, high.y - reach, high.w + 2.f * reach, high.h + 2.f * reach);
                if (!highAround.Contains(sx, sy) && high.y + high.h <= l.card.y) l.card = high;
            }
            if (noteH > 0.f)
                l.note = Rect(l.card.x + kPanelPadX * k, l.card.y + cardH, w - 2.f * kPanelPadX * k, noteH - 2.f * k);
        }
        // The player start's label, under its mark.
        if (m.playerStartMarker) {
            const float lw = Measure(measure, context, CeEditorUiSmall, kCeEditorUiStartLabel) * k;
            l.start = Rect(m.playerStartX - lw * 0.5f, m.playerStartY + (kMarkReach + 1.f) * k, lw, l.lineHeight);
        }
    }
    // A passing notice: one place, a capsule centred right above the prompt line, under the card. In a play test the bottom
    // of the screen belongs to the game's radio messages, so the notice sits under the play-test banner.
    if (m.notice && m.notice[0]) {
        const float w = Measure(measure, context, CeEditorUiSmall, m.notice) * k + 2.f * capEnd;
        float x = (m.width - w) * 0.5f;
        if (x < l.margin) x = l.margin;
        const float y = m.playing ? l.banner.y + l.banner.h + 40.f * k : lane;
        l.notice = Rect(x, y, w, capH);
    }
    // Held help: every key in two columns on a centred panel.
    if (m.help) {
        float keys = 0.f, actions = 0.f;
        int n = 0;
        for (; kCeEditorUiHelp[n].key; ++n) {
            keys = Max(keys, Measure(measure, context, CeEditorUiSmall, kCeEditorUiHelp[n].key));
            actions = Max(actions, Measure(measure, context, CeEditorUiSmall, kCeEditorUiHelp[n].action));
        }
        const float pitch = lh + 2.f;
        const float w = Max(keys + 16.f + actions, Measure(measure, context, CeEditorUiLarge, kHelpTitle)) * k + 4.f * kPanelPadX * k;
        const float h = (2.f * kPanelPadY + large + 8.f + n * pitch) * k;
        const float below = l.project.y + l.project.h + kGap * k;
        l.help = Rect((m.width - w) * 0.5f, below + h <= m.height - l.margin ? below : (m.height - h) * 0.5f, w, h);
    }
    // A list (Open Project) or a question (the game's message box: the question, a line or two, Yes over No).
    if (m.dialog == CeEditorUiList) {
        float widest = Max(Measure(measure, context, CeEditorUiLarge, m.dialogTitle),
                           Measure(measure, context, CeEditorUiSmall, m.dialogNote));
        for (int i = 0; i < m.rowCount && i < CeEditorUiRowMax; ++i)
            widest = Max(widest, Measure(measure, context, CeEditorUiSmall, m.rows[i]) + 2.f * kPanelPadX);
        widest = Max(widest, 280.f);
        const int rows = m.rowCount < CeEditorUiRowMax ? m.rowCount : CeEditorUiRowMax;
        const float w = (widest + 4.f * kPanelPadX) * k;
        const float h = (2.f * kPanelPadY + large + kScrollBand + rows * kRowPitch + kScrollBand + lh) * k;
        l.dialog = Rect((m.width - w) * 0.5f, (m.height - h) * 0.5f, w, h);
        const float top = l.dialog.y + (kPanelPadY + large + kScrollBand) * k;
        for (int i = 0; i < rows; ++i) l.row[i] = Rect(l.dialog.x + kPanelPadX * k, top + i * l.rowPitch, w - 2.f * kPanelPadX * k, l.rowPitch);
        // The bands between the title and the first row and between the last row and the note hold the marks for more
        // rows above and below; a click there scrolls, as the wheel does.
        if (m.moreAbove) l.scroll[0] = Rect(l.dialog.x + kPanelPadX * k, top - kScrollBand * k, w - 2.f * kPanelPadX * k, kScrollBand * k);
        if (m.moreBelow) l.scroll[1] = Rect(l.dialog.x + kPanelPadX * k, top + rows * l.rowPitch, w - 2.f * kPanelPadX * k, kScrollBand * k);
    } else if (m.dialog == CeEditorUiQuestion) {
        // As the game's own question (the main menu's quit box): Yes above No, stacked and centred, the focused answer on a
        // green box fitted to its word. Button 0 is No, button 1 Yes; No, the safe answer, is focused first.
        const float text = Widest(measure, context, CeEditorUiSmall, m.dialogText);
        const float title = Measure(measure, context, CeEditorUiSmall, m.dialogTitle);
        const float w = Max(Max(text, title) + 2.f * 16.f, 260.f) * k;
        // The original's pitch: one large line from answer to answer, the green box as tall as the line (main-quit-question).
        const float answerH = large + 1.f, answerPitch = large + 2.f;
        const float h = (2.f * kPanelPadY + 4.f + (1 + Lines(m.dialogText)) * (lh + 3.f) + 10.f + 2.f * answerPitch) * k;
        l.dialog = Rect((m.width - w) * 0.5f, (m.height - h) * 0.5f, w, h);
        const float first = l.dialog.y + l.dialog.h - (kPanelPadY + 2.f * answerPitch) * k;
        for (int i = 0; i < 2; ++i) {
            const float bw = (Measure(measure, context, CeEditorUiLarge, kCeEditorUiButtons[i]) + 8.f) * k;
            l.button[i] = Rect((m.width - bw) * 0.5f, first + (i == 1 ? 0.f : 1.f) * answerPitch * k, bw, answerH * k);
        }
    }
    *out = l;
}

int CeEditorUiToolAt(const CeEditorUiLayout &l, const CeEditorUiModel &m, float x, float y) {
    if (m.playing || m.dialog != CeEditorUiNoDialog || m.help) return -1;
    for (int i = 0; i < m.toolCount && i < CeEditorUiToolMax; ++i)
        if (l.tool[i].Contains(x, y)) return i;
    return -1;
}
int CeEditorUiRowAt(const CeEditorUiLayout &l, const CeEditorUiModel &m, float x, float y) {
    if (m.dialog != CeEditorUiList) return -1;
    for (int i = 0; i < m.rowCount && i < CeEditorUiRowMax; ++i)
        if (l.row[i].Contains(x, y)) return i;
    return -1;
}
int CeEditorUiButtonAt(const CeEditorUiLayout &l, const CeEditorUiModel &m, float x, float y) {
    if (m.dialog != CeEditorUiQuestion) return -1;
    for (int i = 0; i < 2; ++i)
        if (l.button[i].Contains(x, y)) return i;
    return -1;
}
int CeEditorUiScrollAt(const CeEditorUiLayout &l, const CeEditorUiModel &m, float x, float y) {
    if (m.dialog != CeEditorUiList) return 0;
    if (m.moreAbove && l.scroll[0].Contains(x, y)) return -1;
    if (m.moreBelow && l.scroll[1].Contains(x, y)) return 1;
    return 0;
}
CeEditorUiShown CeEditorUiWhatShows(const CeEditorUiModel &m) {
    CeEditorUiShown s;
    memset(&s, 0, sizeof(s));
    if (m.playing) {
        // The game's own screens (its pause menu, its debrief) are the game's: nothing of the editor's is drawn over them.
        if (m.gameScreen) return s;
        s.banner = true;
        s.notice = m.notice && m.notice[0] && m.noticeAlpha > 0.f;
        return s;
    }
    const bool dialog = m.dialog != CeEditorUiNoDialog, reading = dialog || m.help;
    s.title = true;
    s.dialog = dialog;
    s.help = m.help && !dialog;
    s.tools = m.toolCount > 0 && !s.help;
    s.markers = m.markerCount > 0 && !reading;
    s.card = m.card && !reading && !m.placingPlayerStart;
    s.prompt = m.promptCount > 0 && !reading;
    s.notice = m.notice && m.notice[0] && m.noticeAlpha > 0.f && !reading;
    s.hint = s.tools && Rail(m) && !reading && !s.notice;
    return s;
}
bool CeEditorUiMarkerClear(const CeEditorUiLayout &l, const CeEditorUiModel &m, const CeEditorUiMarker &marker) {
    const float r = kMarkReach * l.scale;   // the selected ring with its ticks
    const CeEditorUiRect ring = {marker.x - r, marker.y - r, 2.f * r, 2.f * r};
    return !Covered(l, m, ring);
}
CeEditorUiStartShown CeEditorUiStartShows(const CeEditorUiLayout &l, const CeEditorUiModel &m) {
    CeEditorUiStartShown s = {false, false};
    if (!m.playerStartMarker || m.playing || m.dialog != CeEditorUiNoDialog || m.help) return s;
    const float x = m.playerStartX, y = m.playerStartY;
    if (!(x >= 0.f && y >= 0.f && x < m.width && y < m.height)) return s;
    const CeEditorUiMarker mark = {x, y, m.playerStartSelected};
    s.marker = CeEditorUiMarkerClear(l, m, mark);
    const CeEditorUiRect &t = l.start;
    s.label = s.marker && t.w > 0.f && t.x >= 0.f && t.y >= 0.f && t.x + t.w <= m.width && t.y + t.h <= m.height &&
              !Covered(l, m, t);
    return s;
}
bool CeEditorUiOverFurniture(const CeEditorUiLayout &l, const CeEditorUiModel &m, float x, float y) {
    if (m.dialog != CeEditorUiNoDialog || m.help) return true;
    return l.title.Contains(x, y) || l.mode.Contains(x, y) || l.objective.Contains(x, y) || l.project.Contains(x, y) || l.tools.Contains(x, y) || (CeEditorUiWhatShows(m).card && l.card.Contains(x, y)) ||
           l.prompt.Contains(x, y) || l.notice.Contains(x, y) || (CeEditorUiWhatShows(m).hint && l.hint.Contains(x, y));
}

#ifdef CE_VERSION
#include "common.h"
#include "Platform.h"
#include "ltshell.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"
#include "mouse.h"

namespace {
// Colours (T4): white content, yellow focus, grey notes, the panel and its edge, the HUD's cyan caps, the box's green.
// Text drawn after a panel goes through the sprite renderer's doubling stage: grey is authored at half, as CE's other
// notes are (0xff545454 shows as about 166, measured at 1920x1080). Text drawn on a capsule follows Prepare(), which
// draws at one times: its grey is authored as it should show (0xffa8a8a8, about 168; at 0x6c it showed 105, too dark on
// the sky). Sprite colours are drawn as written.
const DWORD kWhite = 0xfff4f4f4, kFocus = 0xffffff00, kGrey = 0xff545454, kPanel = 0xd007070f, kEdge = 0xff767879,
            kCapsule = 0xb0040a14, kCap = 0xff38c8e8, kGreen = 0xff2eb030, kRing = 0xd0ffffff,
            kCapsuleGrey = 0xffa8a8a8;   // a note on a capsule (after Prepare, one times): about 168 on screen, as the panels' grey
CDXBitmapFont *gSmall, *gLarge;
CDXTexture *gWhite;
float gScale = 1.f;

void Widen(const char *text, WCHAR *out, int n, CDXBitmapFont *font) {
    int i = 0;
    for (; text && text[i] && i + 1 < n; ++i) {
        const WCHAR c = WCHAR((unsigned char)text[i]);
        out[i] = font->CanDisplay(c) ? c : WCHAR(' ');
    }
    out[i] = 0;
}
float FontMeasure(void *, int face, const char *text) {
    CDXBitmapFont *font = face == CeEditorUiLarge ? gLarge : gSmall;
    if (!text) return float(font->GetHeight());
    WCHAR wide[256];
    Widen(text, wide, 256, font);
    SIZE extent;
    if (!font->GetTextExtent(wide, &extent)) return 0.f;
    return float(extent.cx);
}
void Box(float x, float y, float w, float h, DWORD colour) {
    if (w <= 0 || h <= 0) return;
    CSPRITERENDERER::DrawColouredSprite(x, y, 0.001f, gWhite, colour, w / gWhite->GetRealWidth(), h / gWhite->GetRealHeight());
}
void Line(float x0, float y0, float x1, float y1, float t, DWORD colour) {
    const float dx = x1 - x0, dy = y1 - y0, length = sqrtf(dx * dx + dy * dy);
    if (length <= 0.f) return;
    CSPRITERENDERER::DrawColouredSprite((x0 + x1) * 0.5f - length * 0.5f, (y0 + y1) * 0.5f - t * 0.5f, 0.001f, gWhite, colour,
                                        length / gWhite->GetRealWidth(), t / gWhite->GetRealHeight(), SA_TOP_LEFT, SPT_NONE,
                                        1.f, atan2f(dy, dx));
}
void Arc(float cx, float cy, float r, float from, float to, int segments, float t, DWORD colour) {
    for (int i = 0; i < segments; ++i) {
        const float a = from + (to - from) * i / segments, b = from + (to - from) * (i + 1) / segments;
        Line(cx + r * cosf(a), cy + r * sinf(a), cx + r * cosf(b), cy + r * sinf(b), t, colour);
    }
}
void Panel(const CeEditorUiRect &r) {
    const float e = gScale > 1.5f ? 2.f : 1.f;
    Box(r.x, r.y, r.w, r.h, kPanel);
    Box(r.x, r.y, r.w, e, kEdge);
    Box(r.x, r.y + r.h - e, r.w, e, kEdge);
    Box(r.x, r.y, e, r.h, kEdge);
    Box(r.x + r.w - e, r.y, e, r.h, kEdge);
}
void Prepare();
// The HUD's own capsule, drawn by the HUD's own routine (the objective panel and the radio messages use it): a dark
// translucent body between the cyan end caps, which stand half a height beyond each end of the body. Open below 1 is the
// HUD's own closing: the body goes and the caps turn and fade at the centre. Without the HUD's art, a drawn stand-in.
void Capsule(const CeEditorUiRect &r, float open = 1.f) {
    if (HUD.mObjectiveLeft && HUD.mObjectiveRight && HUD.mObjectiveInnerLeft && HUD.mObjectiveInnerCentre && HUD.mObjectiveInnerRight) {
        if (open <= 0.f) return;
        const float body = r.w - r.h;
        HUD.RenderObjectiveBar(r.x + r.w * 0.5f, r.y + r.h * 0.5f, body > 0.f ? body : 0.f, r.h, open >= 1.f ? 1.f : 0.5f + 0.5f * open);
        Prepare();
        return;
    }
    const float alpha = open;
    const DWORD a = DWORD(alpha * 255.f + 0.5f);
    const DWORD body = ((((kCapsule >> 24) * a) / 255) << 24) | (kCapsule & 0xffffff), cap = (a << 24) | (kCap & 0xffffff);
    const float radius = r.h * 0.5f, t = 2.f * gScale;
    Box(r.x + radius, r.y, r.w - 2.f * radius, r.h, body);
    for (int i = 0; i < 6; ++i) {	// the rounded ends of the body, as thin slices
        const float u = (i + 0.5f) / 6.f, half = radius * sqrtf(1.f - (1.f - u) * (1.f - u)), slice = radius / 6.f;
        Box(r.x + i * slice, r.y + radius - half, slice, 2.f * half, body);
        Box(r.x + r.w - (i + 1) * slice, r.y + radius - half, slice, 2.f * half, body);
    }
    const float pi = 3.14159265f;
    Arc(r.x + radius, r.y + radius, radius - t * 0.5f, pi * 0.62f, pi * 1.38f, 8, t, cap);
    Arc(r.x + r.w - radius, r.y + radius, radius - t * 0.5f, -pi * 0.38f, pi * 0.38f, 8, t, cap);
    Prepare();   // the text on a capsule is drawn at one times on either path
}
float Text(float x, float y, const char *text, DWORD colour, int face = CeEditorUiSmall) {
    CDXBitmapFont *font = face == CeEditorUiLarge ? gLarge : gSmall;
    if (!text || !text[0]) return x;
    WCHAR wide[256];
    Widen(text, wide, 256, font);
    font->DrawTextWithShadow(x, y, colour, wide, 0, 0.f, gScale, gScale);
    return x + FontMeasure(0, face, text) * gScale;
}
void Centred(float cx, float y, const char *text, DWORD colour, int face = CeEditorUiSmall) {
    Text(cx - FontMeasure(0, face, text) * gScale * 0.5f, y, text, colour, face);
}
void PromptPair(float &x, float y, const char *key, const char *action, DWORD alpha = 0xff000000) {
    x = Text(x, y, key, (kWhite & 0xffffff) | alpha);
    x = Text(x + kKeyGap * gScale, y, action, (kCapsuleGrey & 0xffffff) | alpha);
}
// The game's own ship pointer (the texture its pointer routine draws, at that routine's size and alpha), scaled with the
// furniture so it is as large against the editor at 1080 as it is against the menus.
CDXTexture *gPointer;
void Pointer(float x, float y) {
    if (!gPointer) gPointer = CTEXTURE::GetTextureByName((char *)"mouse.tga");   // kept for the process, as the game keeps its own
    if (!gPointer) {
        RenderMouseCursor();
        return;
    }
    CSPRITERENDERER::DrawSprite(x, y, 0.001f, gPointer, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, (DWORD(MOUSE_CURSOR_ALPHA) << 24) | 0xffffff,
                                0.25f * gScale, 0.25f * gScale, 0.f, 0.96875f, 0.f, 0.96875f);
}
void Prepare() {
    RENDERINFO.SetFogEnabled(false);
    RENDERINFO.SetLightingEnabled(false);
    LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
    LT.SRS(D3DRS_ALPHAREF, 8);
    LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
    LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
    LT.SRS(D3DRS_ZENABLE, FALSE);
    LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    RENDERINFO.Apply();
}
} // namespace

bool CeEditorUiRender(const CeEditorUiModel &m, CeEditorUiLayout *out) {
    gSmall = PLATFORM.Font(FONT_SMALL);
    gLarge = PLATFORM.Font(FONT_NORMAL);
    gWhite = HUD.GetWhiteTexture();
    if (!gSmall || !gLarge || gSmall->GetHeight() <= 0 || gLarge->GetHeight() <= 0 || !gWhite || gWhite->GetRealWidth() <= 0 ||
        gWhite->GetRealHeight() <= 0 || m.width <= 0 || m.height <= 0)
        return false;
    CeEditorUiLayout l;
    CeEditorUiLay(m, FontMeasure, 0, &l);
    if (out) *out = l;
    gScale = l.scale;
    // Every capsule's text sits in the middle of the capsule's own height (the title's may be empty in a play test).
    const float k = l.scale, ty = l.textDrop;
    Prepare();
    const float pi = 3.14159265f;
    const CeEditorUiShown shown = CeEditorUiWhatShows(m);
    if (m.playing && !shown.banner) {
        // The game's own screen is up: nothing of the editor's.
    } else if (shown.banner) {
        Capsule(l.banner);
        float x = l.banner.x + l.capsuleText;
        if (l.bannerTitle) {
            x = Text(x, l.banner.y + ty, kBannerTitle, kWhite);
            x += kPairGap * k;
        }
        PromptPair(x, l.banner.y + ty, kResetKey, kResetAction);
        x += kPairGap * k;
        PromptPair(x, l.banner.y + ty, kBannerKey, kBannerAction);
    } else {
        // Markers: thin HUD rings on every object that can be picked; the selected one larger, yellow, with four ticks.
        // A ring that would cross a panel or capsule is not drawn: the furniture never has the level's marks across it.
        for (int i = 0; shown.markers && i < m.markerCount; ++i) {
            const CeEditorUiMarker &marker = m.markers[i];
            if (!CeEditorUiMarkerClear(l, m, marker)) continue;
            if (marker.selected) {
                const float r = 10.f * k, t = 2.f * k;
                Arc(marker.x, marker.y, r, 0.f, 2.f * pi, 24, t, kFocus);
                for (int q = 0; q < 4; ++q) {
                    const float a = q * pi * 0.5f;
                    Line(marker.x + (r + 2.f * k) * cosf(a), marker.y + (r + 2.f * k) * sinf(a), marker.x + (r + 6.f * k) * cosf(a),
                         marker.y + (r + 6.f * k) * sinf(a), t, kFocus);
                }
            } else
                Arc(marker.x, marker.y, 6.f * k, 0.f, 2.f * pi, 16, k > 1.5f ? 2.f : 1.f, kRing);
        }
        // The player start: four corner brackets round a small white pad, cyan as the HUD's caps, with its name under it.
        // Selected it is yellow, larger and ticked, as a selected object's ring. It is a place of its own: nothing is
        // drawn between it and any object. Where it would cross the furniture it is not drawn, as a ring is not.
        const CeEditorUiStartShown start = CeEditorUiStartShows(l, m);
        if (start.marker) {
            const bool chosen = m.playerStartSelected;
            const DWORD colour = chosen ? kFocus : kCap;
            const float cx = m.playerStartX, cy = m.playerStartY, half = (chosen ? 11.f : 9.f) * k, arm = 5.f * k,
                        t = chosen ? 2.f * k : k > 1.5f ? 2.f : 1.5f, pad = 2.f * k;
            for (int q = 0; q < 4; ++q) {
                const float sx = q & 1 ? 1.f : -1.f, sy = q & 2 ? 1.f : -1.f;
                Line(cx + sx * half, cy + sy * half, cx + sx * (half - arm), cy + sy * half, t, colour);
                Line(cx + sx * half, cy + sy * half, cx + sx * half, cy + sy * (half - arm), t, colour);
            }
            Box(cx - pad, cy - pad, 2.f * pad, 2.f * pad, kWhite);
            for (int q = 0; chosen && q < 4; ++q) {
                const float a = q * pi * 0.5f;
                Line(cx + (half + 2.f * k) * cosf(a), cy + (half + 2.f * k) * sinf(a), cx + (kMarkReach * k) * cosf(a),
                     cy + (kMarkReach * k) * sinf(a), t, colour);
            }
            if (start.label) {
                Prepare();   // the name is drawn at one times, as text on a capsule is
                Text(l.start.x, l.start.y, kCeEditorUiStartLabel, chosen ? kFocus : kWhite);
                Prepare();
            }
        }
        Capsule(l.title);
        float x = l.title.x + l.capsuleText;
        x = Text(x, l.title.y + ty, "Map Editor", kWhite);
        Text(x + kPairGap * k, l.title.y + ty, m.level, kWhite);
        Capsule(l.project);
        // The project's name is content (white); its state a note. Grey alone did not read at 3840x2160.
        x = Text(l.project.x + l.capsuleText, l.project.y + ty, m.project, kWhite);
        Text(x, l.project.y + ty, m.state, kCapsuleGrey);
        if (m.mode[0]) {
            Capsule(l.mode); Text(l.mode.x+l.capsuleText,l.mode.y+ty,m.mode,kWhite);
        }
        if (shown.tools) {
            Panel(l.tools);
            // The rail's furniture, before its words: a thin rule between two groups, and a cyan bar beside the armed
            // command (the one the next click on the map places). The bar follows the editing state; the yellow word
            // is only the focus, and the two may stand on different rows.
            const float edge = k > 1.5f ? 2.f : 1.f;
            for (int i = 1; Rail(m) && i < m.toolCount && i < CeEditorUiToolMax; ++i) {
                if (!GroupStart(i)) continue;
                const float mid = (l.tool[i - 1].y + l.tool[i - 1].h + l.tool[i].y) * 0.5f;
                Box(l.tools.x + kPanelPadX * k, mid - edge * 0.5f, l.tools.w - 2.f * kPanelPadX * k, edge, kEdge);
            }
            const int armed = CeEditorUiActiveTool(m);
            if (armed >= 0 && armed < CeEditorUiToolMax && l.tool[armed].w > 0.f)
                Box(l.tool[armed].x + 3.f * k, l.tool[armed].y + (l.rowPitch - l.lineHeight) * 0.5f, 3.f * k, l.lineHeight, kCap);
            for (int i = 0; i < m.toolCount && i < CeEditorUiToolMax; ++i) {
                // The focus always shows, even on a tool that cannot be used just now (it is refused with a sentence).
                const DWORD colour = i == m.toolFocus && !shown.dialog ? kFocus : !m.toolEnabled[i] ? kGrey : kWhite;
                Text(l.tool[i].x + kPanelPadX * k, l.tool[i].y + (l.rowPitch - l.lineHeight) * 0.5f, m.tools[i], colour);
            }
        }
        if (shown.card) {
            Panel(l.card);
            const float pitch = (gSmall->GetHeight() + 4.f) * k, left = l.card.x + kPanelPadX * k;
            float y = l.card.y + kPanelPadY * k;
            Text(left, y, m.cardName, kFocus);
            y += pitch + 2.f * k;
            float labels = 0.f;
            for (int i = 0; i < 4; ++i) labels = labels > FontMeasure(0, CeEditorUiSmall, kCardLabels[i]) ? labels : FontMeasure(0, CeEditorUiSmall, kCardLabels[i]);
            const char *values[4] = {m.cardKind, m.cardSide, m.cardPosition, m.cardHeight};
            for (int i = 0; i < 4; ++i, y += pitch) {
                Text(left, y, kCardLabels[i], kGrey);
                Text(left + (labels + 10.f) * k, y, values[i], kWhite);
            }
            // The note: under a thin rule, white as the values are (it says something about this object), on one line
            // or two as the layout broke it.
            if (l.note.h > 0.f && m.cardNote[0]) {
                const float edge = k > 1.5f ? 2.f : 1.f;
                Box(l.note.x, l.note.y - 3.f * k, l.note.w, edge, kEdge);
                if (l.noteBreak) {
                    char one[CeEditorUiTextMax], two[CeEditorUiTextMax];
                    NoteLines(m.cardNote, l.noteBreak, one, two);
                    Text(l.note.x, l.note.y, one, kWhite);
                    Text(l.note.x, l.note.y + pitch, two, kWhite);
                } else
                    Text(l.note.x, l.note.y, m.cardNote, kWhite);
            }
        }
        if (shown.prompt) {
            Capsule(l.prompt);
            float px = l.prompt.x + l.capsuleText;
            for (int i = 0; i < m.promptCount && i < CeEditorUiPromptMax; ++i) {
                if (i) px += kPairGap * k;
                PromptPair(px, l.prompt.y + ty, m.prompts[i].key, m.prompts[i].action);
            }
        }
        // One command explained, in the notice's place while no notice shows: its name as the rail writes it, then the
        // sentence as a note.
        if (shown.hint && l.hint.w > 0.f && l.hintTool >= 0 && l.hintTool < CeEditorUiToolMax) {
            Capsule(l.hint);
            char keyLabel[CeEditorUiTextMax];
            snprintf(keyLabel, sizeof(keyLabel), "%s:", m.tools[l.hintTool]);
            float hx = l.hint.x + l.capsuleText;
            PromptPair(hx, l.hint.y + ty, keyLabel, kCeEditorUiToolNotes[l.hintTool]);
        }
    }
    if (m.objective[0] && (shown.title || shown.banner) && !shown.dialog && !shown.help) {
        for (int stop = 0; stop < 2; ++stop) {
            const bool visible = stop ? m.secondMarker : m.reachMarker;
            const float cx = stop ? m.secondX : m.reachX, cy = stop ? m.secondY : m.reachY;
            if (!visible || m.playing || !CeEditorUiMarkerClear(l, m, {cx, cy, false})) continue;
            const float r = 11.f*k, t = 2.f*k;
            Line(cx,cy-r,cx+r,cy,t,kCap); Line(cx+r,cy,cx,cy+r,t,kCap);
            Line(cx,cy+r,cx-r,cy,t,kCap); Line(cx-r,cy,cx,cy-r,t,kCap);
            if (m.routeLength == 2) Centred(cx,cy - l.lineHeight*.5f,stop ? "2" : "1",kWhite);
        }
        Capsule(l.objective); Text(l.objective.x+l.capsuleText,l.objective.y+ty,m.objective,kWhite);
    }
    if (shown.notice) {
        const float a = m.noticeAlpha > 1.f ? 1.f : m.noticeAlpha;
        Capsule(l.notice, a);
        if (a >= 1.f) Text(l.notice.x + l.capsuleText, l.notice.y + ty, m.notice, kWhite);   // the HUD shows text on an open bar only
    }
    if (shown.help) {
        Panel(l.help);
        const float cx = l.help.x + l.help.w * 0.5f, pitch = (gSmall->GetHeight() + 2.f) * k;
        Centred(cx, l.help.y + kPanelPadY * k, kHelpTitle, kWhite, CeEditorUiLarge);
        float keys = 0.f;
        for (int i = 0; kCeEditorUiHelp[i].key; ++i)
            keys = keys > FontMeasure(0, CeEditorUiSmall, kCeEditorUiHelp[i].key) ? keys : FontMeasure(0, CeEditorUiSmall, kCeEditorUiHelp[i].key);
        // Keys right-aligned in their own column, what they do left-aligned after it: the panel is sized for both.
        const float split = l.help.x + 2.f * kPanelPadX * k + keys * k;
        float y = l.help.y + (kPanelPadY + gLarge->GetHeight() + 8.f) * k;
        for (int i = 0; kCeEditorUiHelp[i].key; ++i, y += pitch) {
            Text(split - FontMeasure(0, CeEditorUiSmall, kCeEditorUiHelp[i].key) * k, y, kCeEditorUiHelp[i].key, kWhite);
            Text(split + 16.f * k, y, kCeEditorUiHelp[i].action, kGrey);
        }
    }
    if (m.dialog == CeEditorUiList) {
        Panel(l.dialog);
        const float cx = l.dialog.x + l.dialog.w * 0.5f;
        Centred(cx, l.dialog.y + kPanelPadY * k, m.dialogTitle, kWhite, CeEditorUiLarge);
        for (int i = 0; i < m.rowCount && i < CeEditorUiRowMax; ++i)
            Centred(cx, l.row[i].y + (l.rowPitch - l.lineHeight) * 0.5f, m.rows[i], i == m.rowFocus ? kFocus : m.rowGrey[i] ? kGrey : kWhite);
        // More rows: a small chevron in the band above or below, pointing the way the list goes on.
        for (int i = 0; i < 2; ++i) {
            const CeEditorUiRect &r = l.scroll[i];
            if (r.w <= 0) continue;
            const float mx = r.x + r.w * 0.5f, my = r.y + r.h * 0.5f, half = 7.f * k, rise = (i ? 3.f : -3.f) * k,
                        t = k > 1.5f ? 2.f : 1.5f;
            Line(mx - half, my - rise, mx, my + rise, t, kWhite);
            Line(mx, my + rise, mx + half, my - rise, t, kWhite);
        }
        Centred(cx, l.dialog.y + l.dialog.h - (kPanelPadY + 2.f) * k - l.lineHeight, m.dialogNote, kGrey);
    } else if (m.dialog == CeEditorUiQuestion) {
        Panel(l.dialog);
        const float cx = l.dialog.x + l.dialog.w * 0.5f, pitch = (gSmall->GetHeight() + 3.f) * k;
        float y = l.dialog.y + (kPanelPadY + 4.f) * k;
        Centred(cx, y, m.dialogTitle, kWhite);
        y += pitch;
        char line[256];
        for (const char *c = m.dialogText; c && *c;) {
            size_t n = 0;
            while (c[n] && c[n] != '\n') ++n;
            const size_t keep = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
            memcpy(line, c, keep);
            line[keep] = 0;
            Centred(cx, y, line, kGrey);
            y += pitch;
            c += n;
            if (*c == '\n') ++c;
        }
        for (int i = 0; i < 2; ++i) {
            const CeEditorUiRect &b = l.button[i];
            if (i == m.buttonFocus) Box(b.x, b.y, b.w, b.h, kGreen);
            Centred(b.x + b.w * 0.5f, b.y + (b.h - l.largeHeight) * 0.5f, kCeEditorUiButtons[i], kWhite, CeEditorUiLarge);
        }
    }
    // The game's own pointer, drawn by the game's own routine (the pause menu calls it in a level too), once the mouse moved.
    if (m.pointer && !m.playing) Pointer(m.pointerX, m.pointerY);
    LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
    LT.SRS(D3DRS_ZENABLE, TRUE);
    RENDERINFO.Apply();
    return true;
}
#endif
