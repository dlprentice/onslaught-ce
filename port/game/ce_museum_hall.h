// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_MUSEUM_HALL_H
#define CE_MUSEUM_HALL_H

struct CeHallDisplay { float x, y, radius; int label; };
struct CeHallPoint { float x, y; };
struct CeHallTour {
    CeHallPoint entrance, forward, right, stop[3];
    CeHallDisplay display[3];
    unsigned exhibit[3], stops;
    // Dogleg around the outermost exhibit to the open side of the rear viewing room.
    CeHallPoint annexEntry, annexCorner;
};
typedef bool (*CeHallGround)(float x, float y, float *z);

bool CeHallPlan(const CeHallPoint &start, const CeHallDisplay *front, unsigned count, CeHallTour *tour);
// Zones: entrance 0, numbered signs 1 and 2, elsewhere -1.
// The framed collection is approached from the open viewing apron; the display bay is staff-only.
bool CeHallBayAllows(const CeHallTour &tour, CeHallPoint visitor);
bool CeMuseumHallAllowsVisitor(float x, float y);

int CeHallZone(const CeHallTour &tour, CeHallPoint visitor, int previous);
int CeHallSignLabel(const CeHallTour &tour, int zone, CeHallPoint heading, int previousLabel);
// The visitor can pass between the two feet of the arch.
constexpr float kCeHallArchPostOffset = 2.55f;
constexpr float kCeHallArchFootHalfWidth = 0.54f;
// Use the open arch on the path, not the visitor's starting point.
CeHallPoint CeHallEntranceArch(const CeHallTour &tour);
bool CeHallFacesExit(const CeHallTour &tour, CeHallPoint visitor, CeHallPoint heading, bool held);
bool CeMuseumHallFacesExit(float x, float y, float forwardX, float forwardY, bool held);
int CeMuseumHallSignLabel(float forwardX, float forwardY, int previousLabel, int *displayNumber);
void CeMuseumHallSet(const CeHallTour &tour, CeHallGround ground);
void CeMuseumHallClear();
void CeMuseumHallVisitor(float x, float y);
extern "C" void PortCeMuseumHallDraw();


namespace CeMuseumExhibition { struct Plan; }
void CeMuseumHallSetExhibition(const CeHallTour& tour, CeHallGround ground,
                               const CeMuseumExhibition::Plan& exhibition);

#endif
