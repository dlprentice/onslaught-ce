// SPDX-License-Identifier: GPL-3.0-only
// The map editor's furniture laid out by the production layout code against a stand-in font of nine units a character
// (the game's small face measures about that much) and twenty for the large face, at four window shapes.
#include "ce_editor_ui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static unsigned failures, checks;
static void Check(bool value, const std::string& message) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL %s\n", message.c_str()); }
}
static float StandIn(void*, int face, const char* text) {
    if (!text) return face == CeEditorUiLarge ? 22.f : 16.f;   // the small face is about 16 high, as measured on frames
    return float(std::strlen(text)) * (face == CeEditorUiLarge ? 12.f : 9.f);
}
static bool Inside(const CeEditorUiRect& r, const CeEditorUiModel& m) {
    return r.w <= 0 || (r.x >= 0 && r.y >= 0 && r.x + r.w <= m.width + 0.01f && r.y + r.h <= m.height + 0.01f);
}
static bool Apart(const CeEditorUiRect& a, const CeEditorUiRect& b) {
    return a.w <= 0 || b.w <= 0 || a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
}
static float Centre(const CeEditorUiRect& r, bool y) { return y ? r.y + r.h * 0.5f : r.x + r.w * 0.5f; }
static void PlacementCardChecks(const CeEditorUiModel& source) {
    CeEditorUiModel m = source;
    m.help = false;
    m.dialog = false;
    m.playing = false;
    m.width = 640.f;
    m.height = 480.f;
    m.gameScreen = true;
    m.card = true;
    m.playerStartSelected = true;
    CeEditorUiLayout l = {};
    l.card = {40.f, 80.f, 120.f, 100.f};
    const float x = l.card.x + l.card.w * 0.5f;
    const float y = l.card.y + l.card.h * 0.5f;
    Check(CeEditorUiWhatShows(m).card,
          "selected start keeps its ordinary inspector");
    Check(CeEditorUiOverFurniture(l, m, x, y),
          "ordinary inspector consumes the overlapping point");

    CeEditorUiLayout ordinary;
    CeEditorUiLay(m, StandIn, nullptr, &ordinary);
    Check(ordinary.card.w > 0.f && ordinary.card.h > 0.f,
          "ordinary inspector has visible layout bounds");
    m.placingPlayerStart = true;
    CeEditorUiLayout placing;
    CeEditorUiLay(m, StandIn, nullptr, &placing);
    Check(placing.card.w == 0.f && placing.card.h == 0.f,
          "active placement clears the inspector layout bounds");
    Check(!CeEditorUiWhatShows(m).card,
          "active start placement hides the inspector");
    Check(!CeEditorUiOverFurniture(l, m, x, y),
          "the same overlapping point passes through during start placement");

    m.placingPlayerStart = false;
    CeEditorUiLayout restored;
    CeEditorUiLay(m, StandIn, nullptr, &restored);
    Check(restored.card.x == ordinary.card.x && restored.card.y == ordinary.card.y &&
              restored.card.w == ordinary.card.w && restored.card.h == ordinary.card.h,
          "leaving placement restores the inspector layout bounds");
    Check(CeEditorUiWhatShows(m).card && CeEditorUiOverFurniture(l, m, x, y),
          "completed placement restores the selected start inspector");
    m.playerStartSelected = false;
    m.placingPlayerStart = true;
    Check(!CeEditorUiWhatShows(m).card,
          "start placement also hides a previously selected object card");
    m.placingPlayerStart = false;
    Check(CeEditorUiWhatShows(m).card && CeEditorUiOverFurniture(l, m, x, y),
          "cancelled placement restores the ordinary object inspector");
}

// The card never hides what it describes: a selected mark (or the start with its name) where the bottom-right card would
// stand sends the card to the top right, clear of the mark and of every other piece of furniture.
static void CardDockChecks() {
    static const char* tools[8] = {"Save", "Open", "Undo", "Redo", "Play Draft", "Change Side", "Reach Point", "Exit"};
    static const CeEditorUiPrompt chosen[] = {{"Drag", "Move"}, {"Page Up", "Raise"}, {"F5", "Play Draft"}, {"F1", "Keys"}};
    const float shapes[3][2] = {{640, 480}, {1280, 720}, {1920, 1080}};
    for (const auto& shape : shapes) for (int start = 0; start < 2; ++start) {
        CeEditorUiModel m; std::memset(&m, 0, sizeof(m));
        m.width = shape[0]; m.height = shape[1]; m.gameScreen = true;
        std::snprintf(m.level, sizeof(m.level), "%s", "2.21 - Escort Duty");
        std::snprintf(m.project, sizeof(m.project), "%s", "Project 1");
        std::snprintf(m.mode, sizeof(m.mode), "%s", "Select: Marshall");
        m.toolCount = 8; for (int i = 0; i < 8; ++i) { m.tools[i] = tools[i]; m.toolEnabled[i] = true; }
        m.promptCount = 4; for (int i = 0; i < 4; ++i) m.prompts[i] = chosen[i];
        m.card = true;
        std::snprintf(m.cardName, sizeof(m.cardName), "%s", "Marshall");
        std::snprintf(m.cardKind, sizeof(m.cardKind), "%s", "FSV Marshall");
        std::snprintf(m.cardSide, sizeof(m.cardSide), "%s", "Forseti");
        std::snprintf(m.cardPosition, sizeof(m.cardPosition), "%s", "X 335.2  Y 145.1 m");
        std::snprintf(m.cardHeight, sizeof(m.cardHeight), "%s", "-10.0 m");
        std::snprintf(m.cardNote, sizeof(m.cardNote), "%s", "You can move it and change its side.");
        CeEditorUiLayout before; CeEditorUiLay(m, StandIn, nullptr, &before);
        const float x = Centre(before.card, false), y = Centre(before.card, true);
        CeEditorUiMarker mark = {x, y, true};
        if (start) { m.playerStartSelected = true; m.playerStartMarker = true; m.playerStartX = x; m.playerStartY = y; }
        else { m.markers = &mark; m.markerCount = 1; }
        CeEditorUiLayout l; CeEditorUiLay(m, StandIn, nullptr, &l);
        const std::string where = std::to_string(int(m.width)) + (start ? " start" : " object");
        Check(l.card.w == before.card.w && l.card.h == before.card.h && !l.card.Contains(x, y),
              where + ": the card moves off the selected mark it would have hidden");
        Check(l.card.y < before.card.y && Inside(l.card, m) && Apart(l.card, l.mode) && Apart(l.card, l.title) &&
              Apart(l.card, l.project) && Apart(l.card, l.tools) && Apart(l.card, l.prompt),
              where + ": docked at the top right, it touches no other furniture");
        mark.x = m.width * 0.3f; mark.y = m.height * 0.4f; m.playerStartX = mark.x; m.playerStartY = mark.y;
        CeEditorUiLayout clear; CeEditorUiLay(m, StandIn, nullptr, &clear);
        Check(clear.card.y == before.card.y, where + ": with the mark elsewhere the card keeps its bottom-right place");
    }
}

int main(int argc, char** argv) {
    CardDockChecks();
    // argv: notices that must fit the notice capsule at 640 x 480, one per argument.
    const float shapes[4][2] = {{640, 480}, {1280, 720}, {1920, 1080}, {3840, 2160}};
    static const char* tools[8] = {"Save", "Open", "Undo", "Redo", "Test Project", "Change Side", "Reach Point", "Exit"};
    static const CeEditorUiPrompt idle[] = {{"Click", "Select"}, {"Tab", "Next Object"}, {"F5", "Play"}, {"F1", "Keys"}};
    static const CeEditorUiPrompt chosen[] = {{"Drag", "Move"}, {"Page Up", "Raise"}, {"C", "Centre"}, {"F5", "Play"}, {"F1", "Keys"}};
    for (const auto& shape : shapes) {
        for (int variant = 0; variant < 6; ++variant) {
            CeEditorUiModel m; std::memset(&m, 0, sizeof(m));
            m.width = shape[0]; m.height = shape[1];
            const std::string where = std::to_string(int(m.width)) + "x" + std::to_string(int(m.height)) + " variant " + std::to_string(variant);
            std::snprintf(m.level, sizeof(m.level), "%s", "The Longest Level Name of the Game");
            std::snprintf(m.project, sizeof(m.project), "%s", "Project 1234");
            m.state = "Not Saved";
            m.toolCount = 8; for (int i = 0; i < m.toolCount; ++i) { m.tools[i] = tools[i]; m.toolEnabled[i] = true; }
            const CeEditorUiPrompt* set = variant == 0 ? idle : chosen;
            m.promptCount = variant == 0 ? 4 : 5;
            for (int i = 0; i < m.promptCount; ++i) m.prompts[i] = set[i];
            if (variant >= 1) {
                m.card = true;
                std::snprintf(m.cardName, sizeof(m.cardName), "%s", "An Object With a Name of 28c");
                std::snprintf(m.cardKind, sizeof(m.cardKind), "%s", "Muspell Heavy Tank Mk2");
                std::snprintf(m.cardSide, sizeof(m.cardSide), "%s", "Forseti");
                std::snprintf(m.cardPosition, sizeof(m.cardPosition), "%s", "-12345.6, -12345.6, -1234.5");
            }
            m.notice = variant == 2 ? "This project does not match this level, so it was not opened." : nullptr;
            m.noticeAlpha = 1;
            m.help = variant == 3;
            static const char* rows[10] = {"Project 1", "Project 2", "Project 3, The Longest Level Name of", "A Friend's Shared Project Here",
                                           "Project 5", "Project 6", "Project 7", "Project 8", "Project 9", "Project 10"};
            if (variant == 4) {
                m.dialog = CeEditorUiList; m.dialogTitle = "Open Project"; m.dialogNote = "Enter Open      Esc Back";
                m.rowCount = 10; for (int i = 0; i < 10; ++i) m.rows[i] = rows[i]; m.rowFocus = 3;
            }
            if (variant == 5) {
                m.dialog = CeEditorUiQuestion; m.dialogTitle = "Leave without saving?";
                m.dialogText = "Changes since your last save will be lost.";
            }
            CeEditorUiLayout l; CeEditorUiLay(m, StandIn, nullptr, &l);
 PlacementCardChecks(m);
            const float s = m.width / 640.f < m.height / 480.f ? m.width / 640.f : m.height / 480.f;
            Check(l.scale == s, where + ": the scale is the smaller of width / 640 and height / 480");
            const CeEditorUiRect parts[] = {l.title, l.project, l.tools, l.card, l.prompt, l.notice, l.help, l.dialog};
            for (const CeEditorUiRect& r : parts) Check(Inside(r, m), where + ": every panel lies inside the window");
            const CeEditorUiRect corners[] = {l.title, l.project, l.tools, l.card, l.prompt, l.notice};
            for (int a = 0; a < 6; ++a) for (int b = a + 1; b < 6; ++b)
                Check(Apart(corners[a], corners[b]), where + ": the corner panels do not overlap (" + std::to_string(a) + "," + std::to_string(b) + ")");
            Check(l.title.x == l.margin && l.title.y == l.margin, where + ": the title sits in the top left corner");
            Check(l.prompt.y + l.prompt.h <= m.height - l.margin + 0.01f && std::abs(Centre(l.prompt, false) - m.width * 0.5f) < 0.5f,
                  where + ": the prompt line is bottom centre");
            if (m.card) Check(std::abs(l.card.x + l.card.w - (m.width - l.margin)) < 0.5f && l.card.y + l.card.h <= l.prompt.y,
                              where + ": the card sits bottom right, above the prompt line");
            Check(l.tools.x == l.margin && l.tools.y > l.project.y, where + ": the tools are a column at the left edge under the title");
            Check(l.rowPitch == 20.f * s, where + ": rows are 20 units apart");
            for (int i = 0; i < 6; ++i) {
                const bool dialog = m.dialog != CeEditorUiNoDialog || m.help;
                const int hit = CeEditorUiToolAt(l, m, Centre(l.tool[i], false), Centre(l.tool[i], true));
                Check(hit == (dialog ? -1 : i), where + ": a click on a tool finds that tool, and none behind a dialog");
                Check(CeEditorUiOverFurniture(l, m, Centre(l.tool[i], false), Centre(l.tool[i], true)), where + ": the tools are furniture");
            }
            Check(CeEditorUiOverFurniture(l, m, m.width * 0.5f, m.height * 0.4f) == (m.dialog != CeEditorUiNoDialog || m.help),
                  where + ": the middle of the level is not furniture unless a dialog or the help is up");
            if (m.card) Check(CeEditorUiOverFurniture(l, m, Centre(l.card, false), Centre(l.card, true)), where + ": the card is furniture");
            if (m.dialog == CeEditorUiList)
                for (int i = 0; i < 10; ++i) {
                    Check(CeEditorUiRowAt(l, m, Centre(l.row[i], false), Centre(l.row[i], true)) == i, where + ": a click on a row finds it");
                    Check(Inside(l.row[i], m) && l.row[i].x >= l.dialog.x && l.row[i].y + l.row[i].h <= l.dialog.y + l.dialog.h, where + ": rows lie in the list");
                    Check(StandIn(nullptr, 0, rows[i]) * s <= l.row[i].w, where + ": a row's text fits its row");
                }
            if (m.dialog == CeEditorUiQuestion) {
                for (int i = 0; i < 2; ++i) {
                    Check(CeEditorUiButtonAt(l, m, Centre(l.button[i], false), Centre(l.button[i], true)) == i, where + ": a click on a button finds it");
                    Check(l.button[i].x >= l.dialog.x && l.button[i].x + l.button[i].w <= l.dialog.x + l.dialog.w &&
                          l.button[i].y + l.button[i].h <= l.dialog.y + l.dialog.h, where + ": the buttons lie in the box");
                }
                Check(l.button[1].y + l.button[1].h <= l.button[0].y && std::abs(Centre(l.button[0], false) - Centre(l.dialog, false)) < 0.5f &&
                      std::abs(Centre(l.button[1], false) - Centre(l.dialog, false)) < 0.5f, where + ": Yes above No, both centred, as the game's quit question");
                Check(StandIn(nullptr, 0, m.dialogText) * s + 32.f * s <= l.dialog.w + 0.01f, where + ": the question's text fits its box");
            }
            if (m.help) Check(l.help.h <= m.height && l.help.y >= 0 && Apart(l.help, l.title) && Apart(l.help, l.project),
                              where + ": the full key help fits the window under the title");
            // Nothing squeezed: every text's own width fits the panel it is drawn in.
            Check(StandIn(nullptr, 0, "Map Editor") * s + StandIn(nullptr, 0, m.level) * s < l.title.w, where + ": the title fits its capsule");
            if (m.card) Check(StandIn(nullptr, 0, m.cardName) * s <= l.card.w - 20.f * s + 0.01f, where + ": the card's name fits");
        }
    }
    // The Open list's marks for more rows: inside the box, clear of the rows, a click on one scrolls that way; no mark, no
    // scroll. And what a frame draws: an open list, question or the held help hides the markers, the card, the prompt line
    // and any notice, so nothing shows through its panel.
    for (const auto& shape : shapes) {
        CeEditorUiModel m; std::memset(&m, 0, sizeof(m));
        m.width = shape[0]; m.height = shape[1];
        const std::string where = std::to_string(int(m.width)) + "x" + std::to_string(int(m.height));
        static const char* rows[10] = {"Project 14", "Project 13", "Project 12", "Project 11", "Project 10", "Project 9", "Project 8",
                                       "Project 7", "Project 6", "Project 5"};
        m.dialog = CeEditorUiList; m.dialogTitle = "Open Project"; m.dialogNote = "Enter Open      Esc Back";
        m.rowCount = 10; for (int i = 0; i < 10; ++i) m.rows[i] = rows[i];
        m.moreAbove = m.moreBelow = true;
        CeEditorUiMarker marker = {m.width * 0.5f, m.height * 0.5f, true};
        m.markers = &marker; m.markerCount = 1; m.card = true; m.notice = "A notice."; m.noticeAlpha = 1;
        m.promptCount = 1; m.prompts[0] = idle[0]; m.toolCount = 8; for (int i = 0; i < m.toolCount; ++i) m.tools[i] = tools[i];
        CeEditorUiLayout l; CeEditorUiLay(m, StandIn, nullptr, &l);
        for (int i = 0; i < 2; ++i) {
            const CeEditorUiRect& r = l.scroll[i];
            Check(r.w > 0 && r.h >= 14.f * l.scale - 0.01f && r.x >= l.dialog.x && r.x + r.w <= l.dialog.x + l.dialog.w &&
                  r.y >= l.dialog.y && r.y + r.h <= l.dialog.y + l.dialog.h, where + ": a scroll mark lies in the list box");
            for (int row = 0; row < 10; ++row) Check(Apart(r, l.row[row]), where + ": a scroll mark is clear of the rows");
            Check(CeEditorUiScrollAt(l, m, Centre(r, false), Centre(r, true)) == (i ? 1 : -1), where + ": a click on a mark scrolls its way");
        }
        Check(l.scroll[0].y + l.scroll[0].h <= l.row[0].y + 0.01f && l.scroll[1].y >= l.row[9].y + l.row[9].h - 0.01f,
              where + ": the mark for rows above is over the first row, the one for rows below under the last");
        Check(CeEditorUiScrollAt(l, m, Centre(l.row[4], false), Centre(l.row[4], true)) == 0, where + ": a row is not a mark");
        CeEditorUiShown shown = CeEditorUiWhatShows(m);
        Check(shown.dialog && shown.title && !shown.markers && !shown.card && !shown.notice && !shown.prompt,
              where + ": an open list hides the markers, the card, the prompt line and the notice");
        m.dialog = CeEditorUiQuestion; shown = CeEditorUiWhatShows(m);
        Check(shown.dialog && !shown.markers && !shown.card && !shown.notice && !shown.prompt, where + ": so does a question");
        m.dialog = CeEditorUiNoDialog; m.help = true; shown = CeEditorUiWhatShows(m);
        Check(shown.help && !shown.tools && !shown.markers && !shown.card && !shown.notice && !shown.prompt, where + ": so does the held help");
        m.help = false; shown = CeEditorUiWhatShows(m);
        Check(shown.title && shown.tools && shown.markers && shown.card && shown.notice && shown.prompt && !shown.dialog && !shown.help,
              where + ": with nothing open everything shows");
        m.moreAbove = m.moreBelow = false; m.dialog = CeEditorUiList;
        CeEditorUiLay(m, StandIn, nullptr, &l);
        Check(l.scroll[0].w <= 0 && l.scroll[1].w <= 0 && CeEditorUiScrollAt(l, m, Centre(l.dialog, false), l.dialog.y + 1) == 0,
              where + ": a list that holds every row has no marks");
    }
    // Nothing moves as the state changes: the card stands still when the prompt line hides and while its position changes,
    // the notice has one place with or without a card, and every capsule's text lies inside its capsule. The play-test
    // banner keeps clear of the HUD's top-right group, and its text and the notice's lie inside them.
    for (const auto& shape : shapes) {
        CeEditorUiModel m; std::memset(&m, 0, sizeof(m));
        m.width = shape[0]; m.height = shape[1];
        const std::string where = std::to_string(int(m.width)) + "x" + std::to_string(int(m.height));
        std::snprintf(m.level, sizeof(m.level), "%s", "2.21 - Escort Duty"); std::snprintf(m.project, sizeof(m.project), "%s", "Project 12");
        m.toolCount = 8; for (int i = 0; i < m.toolCount; ++i) m.tools[i] = tools[i];
        m.card = true; std::snprintf(m.cardName, sizeof(m.cardName), "%s", "Transport 2");
        std::snprintf(m.cardKind, sizeof(m.cardKind), "%s", "Packhorse"); std::snprintf(m.cardSide, sizeof(m.cardSide), "%s", "Forseti");
        std::snprintf(m.cardPosition, sizeof(m.cardPosition), "%s", "0.0, 0.0, 0.0");
        m.promptCount = 5; for (int i = 0; i < 5; ++i) m.prompts[i] = chosen[i];
        m.notice = "Saved as Project 12. Earlier saves are kept."; m.noticeAlpha = 1;
        CeEditorUiLayout a; CeEditorUiLay(m, StandIn, nullptr, &a);
        m.promptCount = 0; CeEditorUiLayout b; CeEditorUiLay(m, StandIn, nullptr, &b);
        Check(a.card.y == b.card.y && a.card.h == b.card.h, where + ": the card stands still when the prompt line hides");
        std::snprintf(m.cardPosition, sizeof(m.cardPosition), "%s", "-1234.5, 409.7, -12.3"); CeEditorUiLayout c; CeEditorUiLay(m, StandIn, nullptr, &c);
        Check(c.card.x == b.card.x && c.card.w == b.card.w, where + ": the card stands still while its position changes");
        m.card = false; CeEditorUiLayout d; CeEditorUiLay(m, StandIn, nullptr, &d);
        Check(d.notice.y == b.notice.y && d.notice.x == b.notice.x, where + ": a notice has one place, with or without a card");
        Check(Apart(b.notice, b.card) && Apart(b.notice, b.tools), where + ": the notice is clear of the card and the tools");
        const CeEditorUiRect capsules[] = {a.title, a.project, a.prompt, a.notice};
        for (const CeEditorUiRect& r : capsules)
            Check(r.h == a.capsuleH && a.textDrop >= 0.f && a.textDrop + a.lineHeight <= r.h + 0.01f && a.capsuleText < r.w,
                  where + ": a capsule's text lies inside it");
        Check(Apart(a.project, a.title) && Apart(a.project, a.tools) && a.tools.y >= a.project.y + a.project.h,
              where + ": the project's capsule sits between the title and the tools");
        std::snprintf(m.objective, sizeof(m.objective), "%s", "Objective: Reach Point");
        CeEditorUiLayout goal; CeEditorUiLay(m, StandIn, nullptr, &goal);
        Check(Inside(goal.objective,m) && Apart(goal.objective,goal.tools) && goal.objective.h>0 &&
                CeEditorUiOverFurniture(goal,m,Centre(goal.objective,false),Centre(goal.objective,true)),
              where + ": the authoring objective stays visible and owns its input area");
        std::snprintf(m.objective, sizeof(m.objective), "%s", "Reach Point: near the surface  150 m");
        m.playing = true; CeEditorUiLayout e; CeEditorUiLay(m, StandIn, nullptr, &e);
        if (m.width == 640) Check(!e.bannerTitle, where + ": narrow preview leaves room for the Reset and Back keys");
    if (m.width >= 1280 && m.width > m.height * 1.7f) Check(e.bannerTitle, where + ": wide preview identifies its mode with the keys");
    Check(e.banner.w > 0 && e.banner.x + e.banner.w <= m.width - 290.f * e.scale + 0.01f && e.banner.x >= e.margin - 0.01f,
              where + ": the play-test banner keeps clear of the HUD's top-right group");
        Check(e.textDrop >= 0.f && e.textDrop + e.lineHeight <= e.banner.h + 0.01f && e.textDrop + e.lineHeight <= e.notice.h + 0.01f &&
              e.notice.y >= e.banner.y + e.banner.h, where + ": in a play test the banner's and the notice's text lie inside them, the notice under the banner");
        Check(Inside(e.objective,m) && e.objective.h>0 && Apart(e.objective,e.banner) && Apart(e.objective,e.notice),
              where + ": target guidance fits below Back to Editor and clear of the notice");
        CeEditorUiShown inPlay = CeEditorUiWhatShows(m);
        Check(inPlay.banner && inPlay.notice && !inPlay.title && !inPlay.tools && !inPlay.card, where + ": a play test shows its banner and notice only");
        m.gameScreen = true; inPlay = CeEditorUiWhatShows(m);
        Check(!inPlay.banner && !inPlay.notice && !inPlay.title && !inPlay.markers, where + ": over the game's own pause menu or debrief nothing of the editor's shows");
        m.gameScreen = false; m.playing = false;
        // A ring never crosses the furniture: over the title, the project line, the tools, the card, the prompt line or the
        // notice it is not drawn; out in the level it is.
        CeEditorUiLayout f; CeEditorUiLay(m, StandIn, nullptr, &f);
        m.promptCount = 5; m.card = true;
        CeEditorUiLay(m, StandIn, nullptr, &f);
        for (const CeEditorUiRect& r : {f.title, f.project, f.tools, f.card, f.prompt, f.notice}) Check(r.w > 0, where + ": every panel shows");
        const CeEditorUiRect covered[] = {f.title, f.project, f.tools, f.card, f.prompt, f.notice};
        for (int i = 0; i < 6; ++i) {
            const CeEditorUiMarker over = {Centre(covered[i], false), Centre(covered[i], true), false};
            Check(!CeEditorUiMarkerClear(f, m, over), where + ": a marker over a panel or capsule is not drawn (" + std::to_string(i) + ")");
            const CeEditorUiMarker edge = {covered[i].x + covered[i].w + 8.f * f.scale, Centre(covered[i], true), false};
            Check(!CeEditorUiMarkerClear(f, m, edge), where + ": nor one whose ring reaches a panel's edge (" + std::to_string(i) + ")");
        }
        const CeEditorUiMarker open = {m.width * 0.5f, m.height * 0.45f, true};
        Check(CeEditorUiMarkerClear(f, m, open), where + ": a marker out in the level is drawn");
    }
    // Every notice the editor can show while it stays open fits one capsule at 640 x 480.
    for (int i = 1; i < argc; ++i) {
        CeEditorUiModel m; std::memset(&m, 0, sizeof(m)); m.width = 640; m.height = 480; m.notice = argv[i]; m.noticeAlpha = 1;
        CeEditorUiLayout l; CeEditorUiLay(m, StandIn, nullptr, &l);
        Check(l.notice.w > 0 && Inside(l.notice, m) && l.notice.x >= l.margin - 0.01f && l.notice.x + l.notice.w <= 640 - l.margin + 0.01f,
              std::string("the notice fits one capsule: ") + argv[i]);
    }
    // Every help line fits its column at 640 x 480.
    int n = 0; while (kCeEditorUiHelp[n].key) ++n;
    Check(n >= 12 && n <= 22, "the help panel lists every key, and no more than fits");
    std::printf("map editor furniture: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
