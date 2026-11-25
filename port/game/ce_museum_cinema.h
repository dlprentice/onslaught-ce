// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_MUSEUM_CINEMA_H
#define CE_MUSEUM_CINEMA_H
#include "ce_museum_cinema_console.h"

namespace CeCinemaRoom {
enum class FireIntent { Ordinary, Consume, Operate };
// Ownership belongs to the original press. A held Fire that began in the
// gallery cannot turn into a projector command by walking through the doorway.
inline FireIntent FireEdge(bool release,bool usable,bool &held,bool &captured) {
    if(!release) {
        if(!held) { held=true; captured=usable; }
        return captured?FireIntent::Consume:FireIntent::Ordinary;
    }
    const bool armed=captured;held=false;captured=false;
    return !armed?FireIntent::Ordinary:usable?FireIntent::Operate:FireIntent::Consume;
}
// The theater stands outside the hall's maxX wall. kWallX is that wall's line:
// every x below (door, room, screen, console, seats) is measured from it, so the
// whole room moves as one when the hall changes size.
constexpr float kWallX=11.0f;
constexpr float kDoorX=kWallX, kDoorY=4.5f, kDoorHalf=2.8f;
constexpr float kLeft=kWallX, kRight=kWallX+16.0f, kFront=-3.0f, kBack=19.0f;
constexpr float kScreenX=kWallX+8.0f, kScreenY=18.42f, kScreenZ=-5.0f;
constexpr float kConsoleX=kWallX+3.5f, kConsoleY=5.1f;
// Seat n (0 to 5) of a row: three on each side of the centre aisle.
inline float SeatX(unsigned n) { return kWallX+1.65f+(n%3)*1.5f+(n/3)*8.0f; }
constexpr float kScreenHalfW=6.1f, kScreenHalfH=3.43f;
inline bool Range(float p,float lo,float hi) { return p>=lo && p<=hi; }
inline bool Interior(float side,float depth) {
    return Range(side,kLeft-0.15f,kRight-0.55f) && Range(depth,kFront+0.55f,kBack-0.55f);
}
inline bool InDoor(float side,float depth) {
    return Range(side,kLeft-0.75f,kLeft+0.75f) && Range(depth,kDoorY-kDoorHalf+0.55f,kDoorY+kDoorHalf-0.55f);
}
inline bool Console(float side,float depth) {
    return Range(side,kWallX+1.20f,kWallX+13.0f) && Range(depth,1.5f,7.3f);
}
// Seats take up their actual space; the central and side aisles remain open.
inline bool Seat(float side,float depth) {
    for (unsigned row=0;row<3;++row) {
        const float y=1.0f-row*1.60f;
        if (!Range(depth,y-0.58f,y+0.58f)) continue;
        for (unsigned n=0;n<6;++n) {
            const float x=SeatX(n);
            if (Range(side,x-0.61f,x+0.61f)) return true;
        }
    }
    return false;
}
enum class InputDevice { None, Kbm, Pad };
// Tracks whether a connected gamepad is actively driving input rather than keyboard and mouse.
inline bool UpdatePadActive(bool padActive, bool anyPadConnected, bool padInput, bool kbmInput) {
    if (!anyPadConnected) return false;
    if (padInput && !kbmInput) return true;
    if (kbmInput && !padInput) return false;
    return padActive;
}
// Picks which binding slot (0 or 1, or -1 for fallback) to show in the projector prompt.
// KBM players never see a pad button when no gamepad is actively driving the cursor.
inline int PickFireSlot(const InputDevice slots[2], bool padActive) {
    if (padActive) {
        for (int i = 0; i < 2; ++i) if (slots[i] == InputDevice::Pad) return i;
        for (int i = 0; i < 2; ++i) if (slots[i] == InputDevice::Kbm) return i;
        return -1;
    }
    for (int i = 0; i < 2; ++i) if (slots[i] == InputDevice::Kbm) return i;
    return -1;
}
// The idle house: three sconces on the east lining and strip lights along both edges of the centre aisle.
// These are the room's own coordinates; the shell builds the fixtures from them and the hall's light bake
// reads the same ones, so the lamps and their light cannot drift apart.
constexpr unsigned kSconceCount = 3u;
inline float SconceY(unsigned index) { return kFront + 4.0f + float(index) * 5.8f; }
constexpr float kSconceHeight = 3.0f;        // lamp centre above the floor
constexpr float kSconceWashHalf = 1.5f;      // the wall wash is 3 m square around the lamp
constexpr float kSconceReach = 1.48f;        // fades smoothly to 0 at the wash panel edge (zero hard seam)
constexpr float kAisleX = kWallX + 7.15f;    // midway between the two seat banks (SeatX(2) and SeatX(3))
constexpr float kAisleHalf = 1.55f;         // clear of both seat-bank platforms
constexpr float kAisleFront = kFront + 1.2f, kAisleBack = kBack - 4.2f;

// Steps the screening room's house lights (1 = full idle lights, 0 = dimmed for a film) by 'steps' Presents.
// Held constant while the game is paused so the room does not darken or brighten behind the pause menu.
inline float StepHouseLight(float current, float target, unsigned steps, bool paused, float stepSize = 1.0f / 45.0f) {
    if (paused) return current;
    if (steps > 90u) steps = 90u;
    const float move = stepSize * float(steps);
    if (current < target) return current + move < target ? current + move : target;
    if (current > target) return current - move > target ? current - move : target;
    return current;
}
}

// A clock that does not run while it is held (the game paused, Photo Mode up).
// Read with the raw running time and whether the hold is on; the returned time
// stops on the read that first sees the hold and continues from there on the
// read that sees it lifted, so nothing after a hold has to catch up.
struct CeMuseumCinemaHeldClock {
    double skipped;   // total raw time spent held
    double heldAt;    // raw time the current hold began
    bool held;
};
inline CeMuseumCinemaHeldClock CeMuseumCinemaHeldClockNew() {
    const CeMuseumCinemaHeldClock c = {0.0, 0.0, false};
    return c;
}
inline double CeMuseumCinemaHeldClockRead(CeMuseumCinemaHeldClock *clock, double raw, bool hold) {
    if (!clock) return raw;
    if (hold && !clock->held) { clock->held = true; clock->heldAt = raw; }
    else if (!hold && clock->held) {
        clock->held = false;
        if (raw > clock->heldAt) clock->skipped += raw - clock->heldAt;
    }
    const double at = clock->held ? clock->heldAt : raw;
    return at - clock->skipped;
}

// What a failed film open leaves the console in. Only the first scan of the
// whole list finding nothing playable means "films cannot be shown" (true: the
// console says so until the visitor leaves). One chosen film failing, or a frame
// that cannot be written mid-film, returns the console to idle (false).
inline bool CeMuseumCinemaAfterOpenFailure(bool scanningWholeList) {
    return scanningWholeList;
}

extern "C" bool PortCeMuseumCinemaFire(bool release,float value);
extern "C" bool PortCeMuseumCinemaFilmName(unsigned index,char *stem,unsigned cap);
// Closes any film on the Museum projector before the sound device is rebuilt:
// Bink's sound track lives on the device's buffers, and a film left open across
// the rebuild would keep a buffer that no longer exists.
extern "C" void PortCeMuseumCinemaBeforeSoundReinit(void);
// Exact main-view sample; only the Museum projector consumes it.
extern "C" void PortCeMuseumCinemaView(const float *view, unsigned present);
extern "C" bool PortCeMuseumCinemaPanel(CeMuseumCinemaConsole::Presentation *out,
    CeMuseumCinemaConsole::MeasureText measure, void *user, float lineHeight);
#endif
