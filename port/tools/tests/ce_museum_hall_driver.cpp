// SPDX-License-Identifier: GPL-3.0-only
#include "ce_museum_hall.h"
#include "ce_museum_label_rules.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void Need(bool v, const char *why) { if (!v) { std::fprintf(stderr,"%s\n",why); std::exit(2); } }
bool Close(float a,float b) { return std::fabs(a-b)<0.001f; }
}

int main() {
    const CeHallDisplay front[] = {{-6,10,1,4},{0,12,2,7},{8,11,1,10}};
    CeHallTour t;
    Need(CeHallPlan({0,0},front,3,&t),"usable hall");
    Need(t.stops==2 && t.exhibit[0]==1 && t.exhibit[1]==2,"two distinct stops on the visitor's right");
    Need(Close(t.stop[0].x,0) && Close(t.stop[1].x,8) &&
         Close(t.stop[0].y,t.stop[1].y),"a straight cross aisle faces both exhibits");
    Need(t.stop[0].y<=12-2-2 && t.stop[1].y<=13-3-2,"the route clears both machines");
    // From one shoulder of display 2 the reticle falls on the distant object.
    const CeHallPoint second = {t.stop[1].x - 1.0f, t.stop[1].y};
    const float sight[][2] = {{8,11},{second.x,20}};
    Need(CeMuseumNearestLabel(sight, 2, second.x, second.y, 0, 1, -1) == 1,
         "ordinary aim selects the object behind the sign");
    int zone = CeHallZone(t, second, -1);
    Need(zone == 2 && CeHallSignLabel(t, zone, {0,1}, -1) == 10, "second sign opens its own registered card");
    Need(CeHallSignLabel(t, zone, {0,-1}, 10) < 0 &&
         CeHallSignLabel(t, zone, {1,0}, 10) < 0, "look back or sideways to browse freely");
    Need(CeHallSignLabel(t, zone, {0.9f,0.43f}, -1) < 0 &&
         CeHallSignLabel(t, zone, {0.9f,0.43f}, 10) == 10, "small aim changes keep the open card");
    Need(CeHallZone(t, {t.stop[1].x - 2.7f, t.stop[1].y}, zone) == 2 &&
         CeHallZone(t, {t.stop[1].x - 3.4f, t.stop[1].y}, zone) < 0, "step beyond the sign to release it");
    CeHallTour adjacent = t;
    adjacent.stop[1] = {adjacent.stop[0].x + 3.5f, adjacent.stop[0].y};
    Need(CeHallZone(adjacent, adjacent.stop[0], 2) == 1 &&
         CeHallZone(adjacent, {adjacent.stop[0].x + 1.77f, adjacent.stop[0].y}, 1) == 1 &&
         CeHallZone(adjacent, adjacent.stop[1], 1) == 2, "cross to the next sign without chattering at the seam");
    Need(CeHallSignLabel(t, 1, {0,1}, 10) == 7 && CeHallSignLabel(t, 0, {0,1}, 7) < 0 &&
         CeHallSignLabel(t, -1, {0,1}, 7) < 0, "each sign owns its card; the entrance and aisle own none");
    CeHallTour empty = t; empty.display[1].label = -1;
    Need(CeHallSignLabel(empty, 2, {0,1}, 10) < 0, "an unregistered display cannot borrow its neighbour's card");
    CeHallDisplay turned[3];
    for (unsigned i=0;i<3;++i)
        turned[i]={20+front[i].y,30-front[i].x,front[i].radius, front[i].label};
    CeHallTour u;
    Need(CeHallPlan({20,30},turned,3,&u),"rotated hall");
    Need(u.exhibit[1]==t.exhibit[1] && Close(u.stop[1].x,20+t.stop[1].y) &&
         Close(u.stop[1].y,30-t.stop[1].x),"same walking route after rotating the site");
    const CeHallPoint gate = CeHallEntranceArch(t);
    Need(gate.y > t.entrance.y && gate.y < t.stop[0].y, "the physical entrance arch precedes the first stand");
    for (int sign = -1; sign <= 1; sign += 2) {
        const CeHallPoint side = {gate.x + 1.5f * sign, gate.y + 0.44f};
        Need(!CeHallFacesExit(t, side, {-1.0f * sign, 0.0f}, false) &&
             !CeHallFacesExit(t, side, {-1.0f * sign, 0.087f}, false), "sideways and inward views never start an exit cue");
        Need(!CeHallFacesExit(t, side, {-1.0f * sign, -0.01f}, false) &&
             !CeHallFacesExit(t, side, {-1.0f * sign, -0.01f}, true), "a nearly parallel view misses the doorway on both sides");
        Need(CeHallFacesExit(t, side, {-1.5f * sign, -0.44f}, false) &&
             !CeHallFacesExit(t, side, {-1.0f * sign, 0.087f}, true), "aiming through the opening works; turning inward clears it");
        const CeHallPoint centered = {gate.x, gate.y + 4.8f};
        const float opening = kCeHallArchPostOffset - kCeHallArchFootHalfWidth;
        Need(CeHallFacesExit(t, centered, {sign * (opening - 0.2f), -4.8f}, false) &&
             !CeHallFacesExit(t, centered, {sign * (opening + 0.05f), -4.8f}, false) &&
             !CeHallFacesExit(t, centered, {sign * (opening + 0.05f), -4.8f}, true),
             "both fresh and held cues require a ray inside the physical opening");
        Need(!CeHallFacesExit(t, centered, {sign * (opening - 0.03f), -4.8f}, false) &&
             CeHallFacesExit(t, centered, {sign * (opening - 0.03f), -4.8f}, true),
             "a held cue tolerates only a small edge band inside the opening");
    }
    Need(CeHallFacesExit(t, t.stop[0], {0,-1}, false), "look back along the first aisle to find the exit");
    Need(!CeHallFacesExit(t, t.stop[0], {0,1}, true) &&
         !CeHallFacesExit(t, t.stop[0], {1,0}, true), "facing a display or another aisle is not an exit");
    Need(!CeHallFacesExit(t, t.entrance, {0,1}, false) &&
         !CeHallFacesExit(t, t.stop[1], {0,-1}, false), "entering the hall and the other stand have no exit prompt");
    Need(CeHallFacesExit(u, u.stop[0], {-1,0}, false) &&
         !CeHallFacesExit(u, u.stop[0], {1,0}, false), "the cue rotates with the room");
    const CeHallDisplay single[]={{0,8,1,0}};
    Need(CeHallPlan({0,0},single,1,&u) && u.stops==1,"smaller hall has no phantom stop");
    Need(!CeHallPlan({0,8},single,1,&u),"no aisle when the visitor overlaps the display");
    const CeHallDisplay crowded[]={{0,4,3,0}};
    Need(!CeHallPlan({0,0},crowded,1,&u),"no route through an exhibit");
    // The annex is a separate rear collection, never a third sign for a front-row machine.
    const CeHallDisplay whole[]={{-8,12,1,10},{0,12,1,11},{8,12,1,12},
                                 {-8,28,2,13},{0,28,2,14},{8,28,2,15}};
    CeHallTour wing = {};
    Need(CeHallPlan({0,0},whole,6,&wing) && wing.stops==3 && wing.exhibit[2]==5,
         "the outer rear showcase receives its own real exhibit");
    Need(wing.display[2].label==15 && wing.annexEntry.x>whole[2].x+whole[2].radius+2 &&
         Close(wing.annexEntry.x,wing.annexCorner.x) && wing.annexCorner.y>wing.annexEntry.y+4 &&
         Close(wing.annexCorner.y,wing.stop[2].y),"outer and rear aisles physically bypass the front machines");
    Need(CeHallZone(wing,wing.stop[2],0)==3 && CeHallSignLabel(wing,3,{0,1},-1)==15 &&
         CeHallSignLabel(wing,3,{0,-1},15)<0 && CeHallSignLabel(wing,2,{0,1},15)==12,
         "only facing the rear showcase issues its own card; returning changes card");
    Need(CeHallBayAllows(wing,wing.stop[2]) && CeHallBayAllows(wing,wing.annexCorner) &&
         !CeHallBayAllows(wing,{whole[5].x,whole[5].y}) &&
         !CeHallBayAllows(wing,{whole[5].x,whole[5].y-whole[5].radius-0.85f}) &&
         CeHallBayAllows(wing,{whole[5].x+whole[5].radius+2.0f,whole[5].y}),
         "the visitor can reach the window but cannot pass its sill, walls, or pedestal");
    Need(CeHallZone(wing,wing.entrance,3)==0,"the annex shares the existing entrance exit");
    const CeHallDisplay occluded[]={{-8,12,1,10},{0,12,1,11},{8,12,5,12},{8,17,4,13}};
    Need(CeHallPlan({0,0},occluded,4,&wing) && wing.stops==2,
         "a path pinched behind the front row does not claim a viewing room");
    const CeHallDisplay noCard[]={{-8,12,1,10},{0,12,1,11},{8,12,1,12},{8,28,2,-1}};
    Need(CeHallPlan({0,0},noCard,4,&wing) && wing.stops==2,"unknown rear exhibit cannot open an empty gallery");
    CeHallDisplay rotatedWing[6];
    for(unsigned k=0;k<6;++k) rotatedWing[k]={-whole[k].y,whole[k].x,whole[k].radius,whole[k].label};
    Need(CeHallPlan({0,0},rotatedWing,6,&wing) && wing.stops==3 &&
         CeHallSignLabel(wing,3,{-1,0},-1)==15,"annex and display card turn together with the room");
    return 0;
}
