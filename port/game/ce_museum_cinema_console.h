// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_cinema_console.h: the screening room's projector console as
// the visitor sees it and aims at it. Plain arithmetic on std types with no engine
// type, so a standalone driver runs it (cinema_console_driver.cpp).
//
// The film selector itself is ce_museum_cinema_controls.h: it owns the Fire press
// and release and returns Select, Play and Stop with a film index. This module
// adds the three things that state machine leaves to its caller:
//
//   Pick     which control the visitor's real view ray meets, for Input.region
//            and Input.atConsole;
//   Build    the console as faces to draw, three of which are exactly the
//            rectangles Pick tests;
//   Present  the on-screen panel: the chosen film, what is on the screen, and the
//            bound Fire label when a press would do something.
//
// Nothing here latches a press, reads a device, opens a file or plays a film.
// Words drawn by Present are in one table in the .cpp; film titles and the Fire
// label come from the caller and are copied, never kept.
#ifndef CE_MUSEUM_CINEMA_CONSOLE_H
#define CE_MUSEUM_CINEMA_CONSOLE_H
#include "ce_museum_cinema_controls.h"

namespace CeMuseumCinemaConsole {

typedef CeMuseumCinemaControls::Region Region;
typedef CeMuseumCinemaControls::Decoder Decoder;
typedef CeMuseumCinemaControls::Input ControlsInput;
typedef CeMuseumCinemaControls::Output ControlsOutput;

// ---------------------------------------------------------------------------
// Placement

struct Vec3 { float x, y, z; };

// Where the console's control plane is. The plane is the one the three control
// faces lie in; everything else is behind it or a hair in front of it.
//
// right, up and front are unit length and at right angles to each other. front
// points out of the panel toward the visitor. right is the direction that is on
// the visitor's right when they face the panel, so Previous is at -right and Next
// at +right. up is whatever the caller calls up along the panel: no world axis is
// assumed, so a world whose Z grows downward passes its own up, and a tilted desk
// passes a tilted up and front. Either handedness is accepted; Chirality reports
// which one was given.
struct Mount {
    Vec3 centre;        // middle of the control plane
    Vec3 right, up, front;
    float halfWidth;    // of the whole panel, along right
    float halfHeight;   // of the whole panel, along up
    float reach;        // farthest the controls can be worked from
};

// Authored proportions, in the caller's world units. A caller with a console of
// another size sets the three numbers itself; every other dimension follows them.
const float kDefaultHalfWidth = 1.0f;
const float kDefaultHalfHeight = 0.36f;
const float kDefaultReach = 8.0f;

// A mount at the caller's placement with the authored proportions.
Mount MakeMount(const Vec3 &centre, const Vec3 &right, const Vec3 &up, const Vec3 &front);

// False for anything that is not finite, an axis that is not unit length or not
// at right angles to the others, a half extent or reach that is not positive and
// sensible, or a reach too short to stand in front of the panel at all. Pick,
// Build and the extent functions refuse the same mounts.
bool ValidMount(const Mount &mount);

// +1 when right x up = front, -1 when it is the mirror image, 0 for a bad mount.
int Chirality(const Mount &mount);

// Rectangles on the control plane, in world units from the centre: u along
// right, v along up. These are the one description of the controls; Pick tests
// them and Build draws them.
struct Extent { float uMin, uMax, vMin, vMax; };

bool PanelExtent(const Mount &mount, Extent *extent);
// Previous, Play and Next only. The three do not touch: a gutter separates each
// pair, and a margin separates them from the panel's edge. Across the panel they
// sit where CeMuseumCinemaControls::ControlCentre and ControlHalf put them.
bool ControlExtent(const Mount &mount, Region region, Extent *extent);
// The world position of a point on the control plane, and of a control's middle.
bool PanelPoint(const Mount &mount, float u, float v, Vec3 *point);
bool ControlCentre(const Mount &mount, Region region, Vec3 *point);
// The least distance in front of the plane the controls can be worked from.
float NearLimit(const Mount &mount);

// ---------------------------------------------------------------------------
// Aiming

// The visitor's real view: where they look from and along what. aim may be any
// length; a zero or non-finite aim picks nothing.
struct View {
    Vec3 origin;
    Vec3 aim;
    bool museumActive;  // the visit is running
    bool inRoom;        // the visitor is inside the screening room
    bool focused;       // the game has the keyboard and pad
    bool paused;        // the game's own pause is up
    bool photo;         // photo mode is up
};

enum PickStatus {
    kPickBadMount,      // the mount was refused
    kPickAway,          // no visit, or not in the room
    kPickSuspended,     // paused, photo mode, or focus lost
    kPickBadView,       // origin or aim unusable
    kPickBehind,        // behind the panel, or closer than NearLimit
    kPickOutOfReach,    // farther from the panel's centre than reach
    kPickLookingAway,   // in range, but the aim does not come near the console
    kPickNear,          // aim lands beside the panel
    kPickPanel,         // aim lands on the panel, on no control
    kPickControl        // aim lands on a control
};

struct Hit {
    PickStatus status;
    bool inRange;       // standing where the controls can be worked
    bool atConsole;     // inRange with the console in view: for Input.atConsole
    bool onPanel;       // the aim meets the panel rectangle
    Region region;      // the control the aim meets, if any: for Input.region
    bool meetsPlane;    // the four fields below mean something
    float distance;     // along the aim to the control plane
    float u, v;         // where on the plane, as in Extent
    Vec3 point;         // the same place in the world
};

Hit NoHit();

// The control under the visitor's aim. A control is picked only from the front,
// from between NearLimit and reach, by a ray that closes on the plane at more than
// a grazing angle and lands inside that control's rectangle. A gutter, the rest of
// the panel, the console's sides and back, and everything around it pick nothing.
// There is no memory between calls, so nothing can stay picked that is not under
// the aim. A visit that is not running, another room, pause, photo mode and lost
// focus all return NoHit with a status.
Hit Pick(const Mount &mount, const View &view);

// Writes the two fields the selector takes from here and leaves the rest alone.
void Feed(const Hit &hit, ControlsInput *input);

// What the caller does with one Update result. index is the unsigned film index
// to hand to the installed-film accessor; it is set only for select and play, and
// only when it is inside the list.
struct HostAction {
    bool select;        // the chosen film changed to index; nothing is opened
    bool play;          // open film index on the room's screen, replacing any other
    bool stop;          // clear the room's screen; harmless when already clear
    bool hasIndex;
    unsigned index;
};
HostAction ResolveCommand(const ControlsOutput &controls, unsigned films);

// Whether the middle control would stop rather than play: the chosen film is the
// one running. The same rule the selector applies.
bool ShowsStop(Decoder decoder, int playing, int selected, unsigned films);

// ---------------------------------------------------------------------------
// The console to draw

enum Material {
    kMaterialPlate,           // the brass face the controls stand on
    kMaterialCasing,          // the console's sides and back
    kMaterialKeySide,         // the walls of a raised control
    kMaterialKey,             // a control at rest
    kMaterialKeyFocus,        // the control under the aim
    kMaterialKeyPressed,      // ...while Fire is held on it
    kMaterialKeyDisabled,     // a control that can do nothing now
    kMaterialGlyph,           // the mark on a control at rest
    kMaterialGlyphLit,        // the mark on a lit control
    kMaterialGlyphDim,        // the mark on a disabled control
    kMaterialFocusFrame,      // border inside the control under the aim
    kMaterialLampIdle,        // the lamp above the middle control
    kMaterialLampPlaying,
    kMaterialLampEnded,
    kMaterialLampUnavailable
};

struct Rgba { unsigned char r, g, b, a; };
// An authored flat colour for each material; a caller may use its own.
Rgba MaterialColour(Material material);

const unsigned kMaxFaces = 40u;

// A flat face of three or four corners. Seen from the side its normal points to,
// the corners run counter-clockwise when Chirality is +1 and clockwise when it is
// -1. With three corners the fourth repeats the third.
struct Face {
    Vec3 vertices[4];
    unsigned vertexCount;
    Vec3 normal;
    Material material;
    Region region;      // the control this face is, when pickable
    bool pickable;      // true for exactly the three faces Pick tests
};

struct Geometry {
    unsigned faceCount;
    Face faces[kMaxFaces];
};

enum Lamp { kLampIdle, kLampPlaying, kLampEnded, kLampUnavailable };

// How the console looks this frame. It changes materials and marks only; no face
// that can be picked ever moves.
struct Display {
    Region focus;         // the control under the aim
    Region pressed;       // the control Fire is held on
    bool usable;          // the console can do something
    bool multipleFilms;   // Previous and Next lead somewhere
    bool showStop;        // the middle control shows Stop instead of Play
    Lamp lamp;
};
Display IdleDisplay();
Display MakeDisplay(const Hit &hit, const ControlsOutput &controls, unsigned films,
                    Decoder decoder, int playing);

// The console: a shallow casing behind a plate, three raised controls in the
// order Previous, Play or Stop, Next, a mark on each, a border inside the one
// under the aim, and a lamp. The three pickable faces come in that order and are
// the rectangles of ControlExtent on the control plane. Everything is within the
// panel's width and height and less than one half height deep. False, with no
// faces, for a bad mount.
bool Build(const Mount &mount, const Display &display, Geometry *geometry);

// ---------------------------------------------------------------------------
// The panel on the screen

// Present lays out in this logical viewport; the caller scales to its own.
const float kViewportWidth = 640.0f;
const float kViewportHeight = 480.0f;

enum Style {
    // rectangles
    // The native painter draws the panel as the HUD's capsules (one for what is
    // chosen, one per control, one for the cue); the backing and the stripe are
    // drawn as rectangles only where the HUD's capsule art is missing.
    kStylePanel,            // the backing behind the text
    kStyleAccent,           // the stripe down the backing's left edge
    kStyleAccentPlaying,    // ...while a film is running
    kStyleKey,              // a control at rest
    kStyleKeyFocus,         // the control under the aim
    kStyleKeyPressed,       // ...while Fire is held on it
    kStyleKeyDisabled,      // a control that can do nothing now
    kStyleBindingCap,       // the backing of the bound Fire label
    // text
    kStyleHeading,
    kStyleFilmCount,        // "Film 2 of 5"
    kStyleTitle,            // the chosen film
    kStyleStatus,           // nothing on the screen
    kStylePlaying,          // what is on the screen
    kStyleWarning,          // films cannot be shown
    kStyleKeyLabel,
    kStyleKeyLabelLit,
    kStyleKeyLabelDim,
    kStyleBinding,          // the bound Fire label
    kStyleAction,           // what pressing it does
    kStyleHint              // help that names no control
};
Rgba StyleColour(Style style);

struct Frame { float x, y, w, h; };
// Top middle of the viewport: clear of the centre of the view, where the console
// itself is, and of the lower caption band.
Frame DefaultBounds();

// The width, in viewport units, of text in the font and size the caller draws
// these lines with. A null callback, or one that returns something that is not a
// finite width, falls back to a fixed advance per byte.
typedef float (*MeasureText)(const char *text, void *user);

struct PresentInput {
    Hit focus;                  // this frame's Pick
    ControlsOutput controls;    // this frame's Update result
    unsigned films;             // as given to Update
    const char *title;          // display title of film titleIndex, or null
    int titleIndex;             // the index title was fetched for
    const char *playingTitle;   // display title of film playing, or null
    const char *fireLabel;      // the name of the control Fire is bound to now
    Decoder decoder;
    int playing;                // index of the film on the screen, or -1
    float lineHeight;           // height of one line of the font, viewport units
    Frame bounds;               // the panel stays inside this
    MeasureText measure;
    void *measureUser;
};
PresentInput MakePresentInput();

const unsigned kMaxLines = 12u;
const unsigned kMaxRects = 8u;
const unsigned kTextCapacity = 96u;

struct TextLine {
    char text[kTextCapacity];   // always terminated
    float x, y;                 // top left of the line
    float width;                // as measured
    float height;
    Style style;
    bool truncated;             // text was cut and ends in "..."
};

struct PanelRect {
    float x, y, w, h;
    Style style;
    Region region;              // the control a chip stands for, else none
};

struct Presentation {
    bool visible;               // false: draw nothing
    bool actionable;            // the bound Fire label is shown
    Region focus;
    Region pressed;
    Frame panel;                // the backing; every rect and line is inside it
    unsigned rectCount;
    PanelRect rects[kMaxRects]; // rects[0] is the backing
    unsigned lineCount;
    TextLine lines[kMaxLines];
};

// The panel for this frame, or nothing. Nothing is drawn unless focus.atConsole,
// so walking off, looking away, pause, photo mode, lost focus and leaving the room
// all clear it at once. The bound Fire label is drawn only when the aim is on a
// control, the console is usable and the selector's own prompt agrees that a press
// there acts. The chosen film is controls.selected; title is used only when
// titleIndex equals it. Long text wraps or is cut with "..." by the caller's
// measure, and when the font is too tall for bounds whole rows are left out
// rather than overlapped. This draws state; it decides nothing about input.
Presentation Present(const PresentInput &input);

} // namespace CeMuseumCinemaConsole
#endif
