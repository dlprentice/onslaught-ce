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
}

extern "C" bool PortCeMuseumCinemaFire(bool release,float value);
extern "C" bool PortCeMuseumCinemaFilmName(unsigned index,char *stem,unsigned cap);
// Exact main-view sample; only the Museum projector consumes it.
extern "C" void PortCeMuseumCinemaView(const float *view, unsigned present);
extern "C" bool PortCeMuseumCinemaPanel(CeMuseumCinemaConsole::Presentation *out,
    CeMuseumCinemaConsole::MeasureText measure, void *user, float lineHeight);
#endif
