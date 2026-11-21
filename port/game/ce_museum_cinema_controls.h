// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_cinema_controls.h: the screening room's film selector as a
// small state machine. Plain arithmetic with no engine type, so a host test runs
// it (ce_museum_exhibition_tests.py).
//
// One console carries three controls in a row: Previous, Play or Stop, Next. The
// visitor aims at one and presses and releases the Fire action they already have
// mapped; the caller supplies that action's level and its on-screen name. The
// caller also supplies how many films it found and what its player is doing, and
// carries out the commands returned on the room's own screen. Nothing here knows a
// file, a codec, a key or a save.
//
// Fire belongs to the console only for a press that begins while the visitor
// aims at a control of a console that can be used. Such a press is consumed whole,
// press and release, and acts on release if the aim never left that control. Any
// other press, including one already held on arrival, is passed through whole and
// does nothing here, so a weapon never sees half a press.
#ifndef CE_MUSEUM_CINEMA_CONTROLS_H
#define CE_MUSEUM_CINEMA_CONTROLS_H
#include <cmath>

namespace CeMuseumCinemaControls {

enum Region { kRegionNone, kRegionPrevious, kRegionPlay, kRegionNext };
enum Decoder {
    kDecoderStopped,      // nothing on the screen
    kDecoderPlaying,      // a film is running
    kDecoderEnded,        // a film ran to its end and still holds the screen
    kDecoderUnavailable   // films cannot be shown at all
};
enum Command { kCommandNone, kCommandSelect, kCommandPlay, kCommandStop };
// Which line of help to show; the caller owns the words and the action's name.
enum Prompt {
    kPromptNone, kPromptAim, kPromptPrevious, kPromptPlay, kPromptStop, kPromptNext,
    kPromptOnlyFilm, kPromptUnavailable
};

struct Input {
    bool museumActive;   // the visit is running; false in ordinary play
    bool inRoom;         // the visitor is inside the screening room
    bool atConsole;      // inside the console's zone, with the console in view
    Region region;       // the control the visitor aims at, if any
    bool fireDown;       // level of the mapped primary Fire action
    bool paused;         // the game's own pause is up
    bool focused;        // the game has the keyboard and pad
    unsigned films;      // films the caller can show; indices 0 to films-1
    Decoder decoder;
    int playing;         // index of the film on the screen, or -1
};

struct State {
    int selected;        // the chosen film, or -1 when there is none
    bool fireWasDown;    // last level seen
    bool pressOwned;     // the press now held began on a control
    bool pressSpoiled;   // ...and has since left it, or the game paused or lost focus
    Region pressRegion;
    bool wasInRoom;      // inRoom as last seen during a visit
};

struct Output {
    bool consumeFire;    // keep this update's Fire level and edges from the game
    Command command;
    int index;           // the film for Select and Play, otherwise -1
    int selected;        // the chosen film after this update, or -1
    int playing;         // the film on the screen as understood here, or -1
    bool usable;         // the console can do something
    Region pressed;      // the control held down, to draw it pressed
    Prompt prompt;
};

// A new visit. Fire is taken as already held, so whatever level arrives first can
// never be read as a fresh press on a control.
inline void Reset(State *s) {
    if (!s) return;
    s->selected = -1; s->fireWasDown = true; s->pressOwned = false; s->pressSpoiled = false;
    s->pressRegion = kRegionNone; s->wasInRoom = false;
}

namespace detail {
inline int Playing(const Input &in) {
    const bool shown = in.decoder == kDecoderPlaying || in.decoder == kDecoderEnded;
    return shown && in.playing >= 0 && unsigned(in.playing) < in.films ? in.playing : -1;
}
// Keeps the selection inside the list the caller has now.
inline void Settle(State &s, unsigned films, int playing) {
    if (!films) s.selected = -1;
    else if (s.selected < 0) s.selected = playing >= 0 ? playing : 0;
    else if (unsigned(s.selected) >= films) s.selected = int(films) - 1;
}
} // namespace detail

// Chooses a film directly. An index outside the list changes nothing.
inline bool Select(State *s, unsigned films, int index) {
    if (!s || index < 0 || unsigned(index) >= films) return false;
    s->selected = index;
    return true;
}

// One step, called whenever the Fire level or anything else in Input changes and
// at least once a frame during a visit. Previous and Next wrap around the list.
// Play shows the chosen film, replacing another on the screen; aimed at while the
// chosen film itself is running, it stops it. While the visitor is outside the
// room, Stop is returned for as long as anything is on the screen, so carrying
// out Stop twice must be harmless.
//
// The caller holds back Fire, level and edges, on every step consumeFire is set.
// A caller that keeps stepping after the visit ends, with museumActive false,
// until consumeFire is clear never hands the game the tail of a press the
// console took; one that stops stepping at once must drop that tail itself.
// With museumActive false no command is returned: the caller clears its own
// screen when a visit ends.
inline Output Update(State *state, const Input &in) {
    Output out = {false, kCommandNone, -1, -1, -1, false, kRegionNone, kPromptNone};
    if (!state) return out;
    State &s = *state;
    const bool press = in.fireDown && !s.fireWasDown, release = !in.fireDown && s.fireWasDown;
    s.fireWasDown = in.fireDown;
    if (!in.museumActive) {
        // Ordinary play: no press is taken and nothing is asked of the player. Only
        // the rest of a press the console already owned when the visit ended under
        // it is still kept back, spoiled, to its release.
        if (press) s.pressOwned = false;
        s.pressSpoiled = s.pressOwned;
        out.consumeFire = s.pressOwned && (in.fireDown || release);
        if (!in.fireDown) { s.pressOwned = s.pressSpoiled = false; s.pressRegion = kRegionNone; }
        s.wasInRoom = false;
        return out;
    }
    const int playing = detail::Playing(in);
    detail::Settle(s, in.films, playing);
    const bool usable = in.films > 0 && in.decoder != kDecoderUnavailable;
    const bool present = in.inRoom && in.atConsole;
    const Region region = present ? in.region : kRegionNone;
    const bool owning = usable && present && region != kRegionNone && in.focused && !in.paused;

    if (press) { s.pressOwned = owning; s.pressSpoiled = false; s.pressRegion = owning ? region : kRegionNone; }
    if (s.pressOwned && (!owning || region != s.pressRegion)) s.pressSpoiled = true;
    out.consumeFire = s.pressOwned && (in.fireDown || release);
    if (release) {
        if (s.pressOwned && !s.pressSpoiled) {
            const unsigned films = in.films;
            if (region == kRegionPlay) {
                if (in.decoder == kDecoderPlaying && playing == s.selected) out.command = kCommandStop;
                else { out.command = kCommandPlay; out.index = s.selected; }
            } else if (films > 1) {
                const unsigned step = region == kRegionNext ? 1u : films - 1u;
                s.selected = int((unsigned(s.selected) + step) % films);
                out.command = kCommandSelect; out.index = s.selected;
            }
        }
        s.pressOwned = s.pressSpoiled = false; s.pressRegion = kRegionNone;
    }
    // Outside the room nothing stays on the screen. Stop is asked for on the step
    // the visitor walks out and on every later step until the caller reports the
    // screen clear, so a film that was still starting, or one the caller cannot
    // name, is not left running. No fire action can fall on the same step: none
    // is taken away from the console.
    const bool shown = in.decoder == kDecoderPlaying || in.decoder == kDecoderEnded;
    if (!in.inRoom && shown && out.command == kCommandNone) out.command = kCommandStop;
    s.wasInRoom = in.inRoom;

    out.selected = s.selected;
    out.playing = out.command == kCommandStop ? -1 : out.command == kCommandPlay ? out.index : playing;
    out.usable = usable;
    out.pressed = s.pressOwned && !s.pressSpoiled && in.fireDown ? s.pressRegion : kRegionNone;
    if (present) {
        if (!usable) out.prompt = kPromptUnavailable;
        else if (region == kRegionNone) out.prompt = kPromptAim;
        else if (region == kRegionPlay)
            out.prompt = in.decoder == kDecoderPlaying && playing == s.selected ? kPromptStop : kPromptPlay;
        else if (in.films < 2) out.prompt = kPromptOnlyFilm;
        else out.prompt = region == kRegionNext ? kPromptNext : kPromptPrevious;
    }
    return out;
}

// The console as the visitor meets it: the middle of its face on the floor plan,
// the unit direction from the face toward the visitor's side, half its width,
// and how far back its controls can be worked from.
struct Console { float x, y, towardX, towardY, halfWidth, reach; };

// Distance in front of the face and offset along it (to the right of a visitor
// facing the console, who has (fy, -fx) to the right when looking along (fx, fy)).
inline void ConsoleLocal(const Console &c, float x, float y, float *front, float *across) {
    const float dx = x - c.x, dy = y - c.y;
    *front = dx * c.towardX + dy * c.towardY;
    *across = dx * -c.towardY + dy * c.towardX;
}
// Whether a visitor standing at (x, y) is close enough, on the console's front.
inline bool AtConsole(const Console &c, float x, float y) {
    float front, across;
    ConsoleLocal(c, x, y, &front, &across);
    return front >= 0.2f && front <= c.reach && std::fabs(across) <= c.halfWidth + 1.0f;
}
// The middle of a control along the face, and half its width. The three controls
// are separated by gaps, so an aim on a boundary is on neither.
inline float ControlCentre(const Console &c, Region region) {
    return region == kRegionPrevious ? -c.halfWidth * (2.0f / 3.0f) :
           region == kRegionNext ? c.halfWidth * (2.0f / 3.0f) : 0.0f;
}
inline float ControlHalf(const Console &c) { return c.halfWidth * 0.27f; }
// The control the visitor at (x, y) aims at with heading (hx, hy), any length.
inline Region AimRegion(const Console &c, float x, float y, float hx, float hy) {
    float front, across;
    ConsoleLocal(c, x, y, &front, &across);
    const float closing = -(hx * c.towardX + hy * c.towardY), length = std::sqrt(hx * hx + hy * hy);
    if (!AtConsole(c, x, y) || !std::isfinite(length) || !(length > 1.0e-6f) || !(closing > 0.35f * length))
        return kRegionNone;
    const float hit = across + front * (hx * -c.towardY + hy * c.towardX) / closing;
    for (int r = kRegionPrevious; r <= kRegionNext; ++r)
        if (std::fabs(hit - ControlCentre(c, Region(r))) <= ControlHalf(c)) return Region(r);
    return kRegionNone;
}

} // namespace CeMuseumCinemaControls
#endif
