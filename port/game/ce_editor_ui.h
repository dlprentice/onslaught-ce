// SPDX-License-Identifier: GPL-3.0-only
// The map editor's screen furniture: what it shows, where, and what a click there means. The editor fills a plain model
// each frame; this file lays it out, answers hit tests and draws it with the game's own fonts, pointer and sprite
// renderer, in the style of the cockpit HUD and the game's message boxes (thin lines, translucent panels with a grey
// edge, capsules with cyan end caps, white text, yellow focus, grey notes). Layout and hit tests are plain C++ so a host
// test can measure them against a stand-in font; only CeEditorUiRender reads the engine.
#ifndef CE_EDITOR_UI_H
#define CE_EDITOR_UI_H

enum { CeEditorUiToolMax = 10, CeEditorUiRowMax = 10, CeEditorUiPromptMax = 5, CeEditorUiTextMax = 96 };
// The editor's one command set. A list of exactly CeEditorUiToolMax tools is this set at these indexes: the rail groups it
// (project, history, play, the selected object, what is placed on the map, leaving) and explains the focused command.
// Hit tests and the keyboard focus use the same indexes. A shorter list is drawn as a plain column.
enum CeEditorUiTool {
    CeEditorUiToolSave = 0, CeEditorUiToolOpen, CeEditorUiToolUndo, CeEditorUiToolRedo, CeEditorUiToolPlay,
    CeEditorUiToolSide, CeEditorUiToolReach, CeEditorUiToolHold, CeEditorUiToolPlayerStart, CeEditorUiToolExit
};
enum CeEditorUiFace { CeEditorUiSmall = 0, CeEditorUiLarge = 1 };
enum CeEditorUiDialog { CeEditorUiNoDialog = 0, CeEditorUiList, CeEditorUiQuestion };

struct CeEditorUiRect {
    float x, y, w, h;
    bool Contains(float px, float py) const { return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h; }
};
struct CeEditorUiMarker { float x, y; bool selected; };
struct CeEditorUiPrompt { const char *key, *action; };

struct CeEditorUiModel {
    float width, height;                 // the window, in pixels
    char objective[CeEditorUiTextMax];
    bool reachMarker; float reachX, reachY;
    bool secondMarker; float secondX, secondY; int routeLength;
    bool reachHold, secondHold;          // whether Stop 1 / Stop 2 is a Hold Area rather than a Reach Point
    bool playing;                        // a play test: only its banner and the notice
    bool gameScreen;                     // in a play test, the game's own pause menu or debrief is up: nothing is drawn
    char level[CeEditorUiTextMax];       // the game's own name for the level (Latin-1), or empty
    char project[CeEditorUiTextMax];     // "New Project", "Project 3"
    const char *state;                   // a grey word or two after the project: "Not Saved", "Saved"
    const char *tools[CeEditorUiToolMax];
    bool toolEnabled[CeEditorUiToolMax];
    int toolCount, toolFocus;
    bool card;                           // the selected object's card
    char cardName[CeEditorUiTextMax], cardKind[CeEditorUiTextMax], cardSide[32], cardPosition[64];
    const CeEditorUiMarker *markers;
    int markerCount;
    CeEditorUiPrompt prompts[CeEditorUiPromptMax];
    int promptCount;
    bool help;                           // every key, while the help key is held
    const char *notice;                  // a passing message, or null
    float noticeAlpha;                   // 0..1
    int dialog;                          // CeEditorUiDialog
    const char *dialogTitle, *dialogText, *dialogNote;
    const char *rows[CeEditorUiRowMax];
    bool rowGrey[CeEditorUiRowMax];      // a row that names another level's project, so it cannot be opened here
    int rowCount, rowFocus;
    bool moreAbove, moreBelow;           // the list holds rows above or below the ones shown
    int buttonFocus;                     // a question: 0 No, 1 Yes (drawn Yes above No, as the game's own question)
    bool pointer;
    float pointerX, pointerY;
    // Editing context is read-only; command focus does not select a tool.
    char mode[CeEditorUiTextMax];
    char cardHeight[32];
    // The player's own start: a place of its own on the map, in window pixels as the reach markers are. It belongs to no
    // object, so nothing here ties it to one. Selected, its inspector comes through the card's fields.
    bool playerStartMarker; float playerStartX, playerStartY;
    bool playerStartSelected;            // the start is what the card describes
    bool placingPlayerStart;             // the next click on the map sets the start
    char cardNote[CeEditorUiTextMax];    // a sentence under the card's rows, or empty
    bool gamepad;                        // true when a connected gamepad drove the editor most recently
};

// Width of text at the 640 x 480 scale (the caller multiplies by the screen's scale).
typedef float (*CeEditorUiMeasure)(void *context, int face, const char *text);

struct CeEditorUiLayout {
    bool bannerTitle = false;   // omit only the heading when the pilot HUD needs the room
    float scale, margin, rowPitch, lineHeight, largeHeight;
    float capsuleH, capsuleText, textDrop; // every capsule's height, where its text starts after the left cap and below its top
    CeEditorUiRect title, project, objective, tools, tool[CeEditorUiToolMax], card, prompt, notice, help, dialog,
        row[CeEditorUiRowMax], button[2], banner, scroll[2];   // scroll: the list's marks for more rows above, below
    CeEditorUiRect mode;
    // The line that explains one command (the armed one, else the focused one) in the notice's place, and which command;
    // the card's note and the characters on its first line when it takes two (0: one line); the player start's label.
    CeEditorUiRect hint;
    int hintTool;               // -1: no line
    CeEditorUiRect note;
    int noteBreak;
    CeEditorUiRect start;
    CeEditorUiRect stopLabel[2];         // compact map badges for Reach Point / Hold Area stops
};

// What one frame draws. An open list or question, or the held help, is the only thing the player is reading: the markers,
// the card, the prompt line and any notice behind it are not drawn, so nothing shows through its translucent panel.
// The command's explanation shares the notice's place: a notice that shows takes it.
struct CeEditorUiShown { bool markers, title, tools, card, prompt, notice, help, dialog, banner, hint; };
CeEditorUiShown CeEditorUiWhatShows(const CeEditorUiModel &model);

// The command that is armed (the next click on the map places it), or -1. The focus never arms a command: this reads the
// editing state only.
int CeEditorUiActiveTool(const CeEditorUiModel &model);
// What the focused or armed command does, by the command set's indexes.
extern const char *const kCeEditorUiToolNotes[CeEditorUiToolMax];
// The player start's mark and its label, each drawn only where it crosses no panel or capsule; the label also only where
// it lies inside the window. Neither shows in a play test or behind a list, a question or the help.
struct CeEditorUiStartShown { bool marker, label; };
CeEditorUiStartShown CeEditorUiStartShows(const CeEditorUiLayout &layout, const CeEditorUiModel &model);
extern const char *const kCeEditorUiStartLabel;

// The furniture of one frame. Every rectangle lies inside the window; nothing is squeezed to fit.
void CeEditorUiLay(const CeEditorUiModel &model, CeEditorUiMeasure measure, void *context, CeEditorUiLayout *layout);
// What lies under a point: a tool's index, a list row's, a question button's (0 No, 1 Yes), or -1.
int CeEditorUiToolAt(const CeEditorUiLayout &layout, const CeEditorUiModel &model, float x, float y);
int CeEditorUiRowAt(const CeEditorUiLayout &layout, const CeEditorUiModel &model, float x, float y);
int CeEditorUiButtonAt(const CeEditorUiLayout &layout, const CeEditorUiModel &model, float x, float y);
// The list's scroll marks under a point: -1 the mark for rows above, 1 for rows below, 0 neither (or no rows that way).
int CeEditorUiScrollAt(const CeEditorUiLayout &layout, const CeEditorUiModel &model, float x, float y);
// False for a marker whose ring would cross a panel or capsule that shows: such a ring is not drawn.
bool CeEditorUiMarkerClear(const CeEditorUiLayout &layout, const CeEditorUiModel &model, const CeEditorUiMarker &marker);
// True over any panel, so a click there never picks or drags an object in the level behind it.
bool CeEditorUiOverFurniture(const CeEditorUiLayout &layout, const CeEditorUiModel &model, float x, float y);

// The help panel's lines: key, then what it does. Ends with a null key.
extern const CeEditorUiPrompt kCeEditorUiHelp[];
// The two words of a question's buttons.
extern const char *const kCeEditorUiButtons[2];

// Draw the model in the current frame (after the world, before the end of the scene); the layout drawn is returned for
// the next frame's hit tests (false when nothing could be drawn).
bool CeEditorUiRender(const CeEditorUiModel &model, CeEditorUiLayout *layout);
#endif

// Derive editing labels from the caller's existing state; never changes the draft.
void CeEditorUiDescribeEditing(CeEditorUiModel &model, bool dirty,
                              bool placing, bool holdArea, bool moving);
// Tracks whether a connected gamepad is actively driving the editor rather than keyboard and mouse.
bool CeEditorUiUpdateDevice(bool padActive, bool anyPad, bool padInput, bool kbmInput);
// Fill the bottom prompt bar (at most CeEditorUiPromptMax pairs) for the current editing state and input device.
void CeEditorUiFillPrompts(CeEditorUiModel &model, bool moving, bool placingStart, bool placingReach,
                           bool canRemoveReach, bool startSelected, bool objectSelected);
