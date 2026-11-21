// SPDX-License-Identifier: GPL-3.0-only
// Authored positions only: no game object, no installed content.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ce_museum_label_rules.h"
static void Need(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}
int main() {
    // three exhibits in a row along X at Y 100, the visitor south of them looking north (+Y)
    const float at[3][2] = {{0, 100}, {20, 100}, {40, 100}};
    Need(CeMuseumNearestLabel(at, 3, 20, 80, 0, 1, -1) == 1, "the exhibit in front, in range, is shown");
    Need(CeMuseumNearestLabel(at, 3, 20, 20, 0, 1, -1) == -1, "nothing is shown from far away");
    Need(CeMuseumNearestLabel(at, 3, 20, 130, 0, 1, -1) == -1, "an exhibit behind the visitor is not shown");
    Need(CeMuseumNearestLabel(at, 3, 20, 120, 0, -1, -1) == 1, "turning round shows it");
    Need(CeMuseumNearestLabel(at, 3, 3, 80, 0, 1, -1) == 0, "the exhibit in the crosshair is shown, not the one beside it");
    Need(CeMuseumNearestLabel(at, 3, 100, 100, 0, 0, -1) == -1 && CeMuseumNearestLabel(at, 3, 35, 100, 0, 0, -1) == 2,
         "with no heading the nearest in range is shown, and none out of range");
    Need(CeMuseumNearestLabel(at, 0, 0, 0, 0, 1, -1) == -1, "an empty hall shows nothing");
    // the one the visitor looks at wins, not the one that is nearest
    const float pair[2][2] = {{0, 25}, {6, 10}};
    Need(CeMuseumNearestLabel(pair, 2, 0, 0, 0, 1, -1) == 0, "a nearer machine off to the side does not name itself over the one ahead");
    const float cone[2][2] = {{1, 25}, {3, 10}};
    Need(CeMuseumNearestLabel(cone, 2, 0, 0, 0, 1, -1) == 0, "of two in the view the more central wins though it is farther");
    Need(CeMuseumNearestLabel(cone, 2, 0, 0, 1, 0, -1) == -1, "looking sideways away from both shows none");
    Need(CeMuseumNearestLabel(cone, 2, 0, 0, 0, 10, -1) == 0 && CeMuseumNearestLabel(cone, 2, 0, 0, 0, 0.1f, -1) == 0, "the heading's length does not matter");
    // on the same line of sight the nearer machine is named (the exhibits hide one another)
    const float line[2][2] = {{0, 25}, {0, 12}};
    Need(CeMuseumNearestLabel(line, 2, 0, 0, 0, 1, -1) == 1, "of two machines on one line of sight the nearer is named");
    // the width of the view: a machine about 19 degrees to one side is named, one 31 degrees to the side is not
    const float near_axis[1][2] = {{7.0f, 20.0f}}, wide[1][2] = {{12.0f, 20.0f}};		// 19.3 and 31 degrees
    Need(CeMuseumNearestLabel(near_axis, 1, 0, 0, 0, 1, -1) == 0, "a machine within the view is named");
    Need(CeMuseumNearestLabel(wide, 1, 0, 0, 0, 1, -1) == -1, "a machine well outside the view is not");
    // the held label survives a step out of the cone (20 to 28 degrees) and out of range (30 to 36), a new one does not
    const float edge[1][2] = {{0, 100}};
    Need(CeMuseumNearestLabel(edge, 1, 0, 100 - 33, 0, 1, -1) == -1, "just outside the range a label is not taken");
    Need(CeMuseumNearestLabel(edge, 1, 0, 100 - 33, 0, 1, 0) == 0, "a label already shown is kept a little beyond the range");
    Need(CeMuseumNearestLabel(edge, 1, 0, 100 - 40, 0, 1, 0) == -1, "and is dropped well beyond it");
    const float glance[1][2] = {{8.3f, 18.2f}};		// 24.5 degrees off the heading, 20 units away
    Need(CeMuseumNearestLabel(glance, 1, 0, 0, 0, 1, -1) == -1, "a machine a little past the view's edge is not taken");
    Need(CeMuseumNearestLabel(glance, 1, 0, 0, 0, 1, 0) == 0, "but a label already shown is kept a little past it");
    const float sideways[1][2] = {{20, 20}};			// 45 degrees
    Need(CeMuseumNearestLabel(sideways, 1, 0, 0, 0, 1, 0) == -1, "and dropped well past it");
    // two almost equally central machines: the one shown keeps the label
    const float twin[2][2] = {{0, 20}, {1, 20}};
    Need(CeMuseumNearestLabel(twin, 2, 0, 0, 0, 1, -1) == 0, "of two equally central machines the first is taken");
    Need(CeMuseumNearestLabel(twin, 2, 0, 0, 0, 1, 1) == 1, "the label shown stays on its machine when another is barely more central");
    const float own[2][2] = {{5, 25}, {5, 5}};
    Need(CeMuseumNearestLabel(own, 2, 5, 5, 0, 1, -1) == 1, "standing on an exhibit shows it before one ahead");
    const float nan[1][2] = {{__builtin_nanf(""), 0}};
    Need(CeMuseumNearestLabel(nan, 1, 0, 0, 0, 1, -1) == -1, "a position that is not a number is never shown");
    // a capsule grows, holds, shrinks
    Need(CeMuseumCapsuleOpen(100, 200, 5000, 400, true) == 0.0f, "before it starts a capsule is shut");
    Need(CeMuseumCapsuleOpen(200, 200, 5000, 400, true) == 0.0f, "at its first moment it is shut");
    Need(CeMuseumCapsuleOpen(400, 200, 5000, 400, true) == 0.5f, "half way through growing it is half open");
    Need(CeMuseumCapsuleOpen(2000, 200, 5000, 400, true) == 1.0f, "in the middle it is open");
    Need(CeMuseumCapsuleOpen(5000, 200, 5000, 400, true) == 0.5f, "half way through closing it is half open");
    Need(CeMuseumCapsuleOpen(5300, 200, 5000, 400, true) == 0.0f, "after its time it is shut");
    Need(CeMuseumCapsuleOpen(900000, 200, 0, 400, false) == 1.0f, "a capsule with no end stays open");
    // a line that only repeats the name's last word is left off
    Need(CeMuseumLineRepeatsName("Forseti City Building", "Building"), "a kind that is the name's last word is a repeat");
    Need(CeMuseumLineRepeatsName("Forseti City BUILDING", "building"), "without regard to case");
    Need(!CeMuseumLineRepeatsName("Air Trainer", "Fighter aircraft"), "another kind is not a repeat");
    Need(!CeMuseumLineRepeatsName("Forseti City Building", "Build"), "a part of the last word is not a repeat");
    Need(!CeMuseumLineRepeatsName("Forseti City Building", "Building site"), "nor is a longer line that starts with it");
    Need(!CeMuseumLineRepeatsName("Walker", "Walking machine") && CeMuseumLineRepeatsName("Walker", "Walker"), "a one word name is compared whole");
    Need(!CeMuseumLineRepeatsName("", "Building") && !CeMuseumLineRepeatsName("Tank", "") && !CeMuseumLineRepeatsName(NULL, "x") && !CeMuseumLineRepeatsName("x", NULL),
         "an empty or missing text repeats nothing");
    // the sea's reach (Z counts downward: ground above the water has a smaller Z)
    Need(!CeMuseumInSeaReach(-13.0f, -10.0f, 1.5f), "a visitor well above the water is out of the sea's reach");
    Need(CeMuseumInSeaReach(-11.0f, -10.0f, 1.5f), "a visitor within the margin of the water is in reach");
    Need(CeMuseumInSeaReach(-9.0f, -10.0f, 1.5f), "a visitor below the surface is in reach");
    Need(!CeMuseumInSeaReach(-11.5f, -10.0f, 1.5f), "the margin's edge is not in reach");
    Need(!CeMuseumInSeaReach(__builtin_nanf(""), -10.0f, 1.5f), "a position that is not a number is not moved");
    // the hall's edge: a visitor past the radius from where it started, in any direction and at any height over it, is put back
    Need(!CeMuseumPastHallEdge(10, 10, 10, 10, kCeMuseumHallRadius) && !CeMuseumPastHallEdge(10 + 80, 10, 10, 10, kCeMuseumHallRadius), "a visitor inside the hall is not past its edge");
    Need(CeMuseumPastHallEdge(10 + 91, 10, 10, 10, kCeMuseumHallRadius) && CeMuseumPastHallEdge(10, 10 - 91, 10, 10, kCeMuseumHallRadius), "a visitor past the radius along an axis is");
    Need(CeMuseumPastHallEdge(10 + 70, 10 + 70, 10, 10, kCeMuseumHallRadius) && !CeMuseumPastHallEdge(10 + 60, 10 + 60, 10, 10, kCeMuseumHallRadius),
         "the edge is a circle, not a square");
    Need(!CeMuseumPastHallEdge(__builtin_nanf(""), 0, 0, 0, kCeMuseumHallRadius), "a position that is not a number is not moved");
    Need(kCeMuseumHallRadius >= 60.0f && kCeMuseumHallRadius <= 150.0f, "the hall's radius leaves room to walk round the exhibits and is small enough that the sea is near");
    // a label is named only while the nose is level enough
    Need(CeMuseumLookingLevel(0, 1, 0) && CeMuseumLookingLevel(0, 1, 0.5f) && CeMuseumLookingLevel(0, 0, 0), "a level view names what is ahead");
    Need(!CeMuseumLookingLevel(0, 1, 1.0f) && !CeMuseumLookingLevel(0, 0.1f, -1.0f) && !CeMuseumLookingLevel(0, 0, 1.0f), "a nose well up or down names nothing");
    Need(!CeMuseumLookingLevel(__builtin_nanf(""), 1, 0), "a direction that is not a number names nothing");
    // names as a label reads them
    char name[32];
    CeMuseumPrettyName(name, sizeof name, "forseti_tank");
    Need(!std::strcmp(name, "Forseti Tank"), "separators become spaces and words start with a capital");
    CeMuseumPrettyName(name, sizeof name, "FORSETI TANK");
    Need(!std::strcmp(name, "Forseti Tank"), "a word in capitals is lowered after its first letter");
    CeMuseumPrettyName(name, sizeof name, "forseti_city_building_1");
    Need(!std::strcmp(name, "Forseti City Building"), "a number that ends a name is dropped");
    CeMuseumPrettyName(name, sizeof name, "av_14a_sabre");
    Need(!std::strcmp(name, "Av 14a Sabre"), "a number inside a name stays");
    CeMuseumPrettyName(name, sizeof name, "tank_14");
    Need(!std::strcmp(name, "Tank"), "a trailing number goes from a short name too");
    CeMuseumPrettyName(name, sizeof name, "42");
    Need(!std::strcmp(name, "42"), "a name that is only a number stays");
    CeMuseumPrettyName(name, sizeof name, "walker_");
    Need(!std::strcmp(name, "Walker"), "a space left at the end goes");
    CeMuseumPrettyName(name, sizeof name, "ForsetiTank");
    Need(!std::strcmp(name, "ForsetiTank"), "a capital after a lower case letter is kept");
    CeMuseumPrettyName(name, sizeof name, "gun.pod-light");
    Need(!std::strcmp(name, "Gun Pod Light"), "dots and dashes become spaces");
    CeMuseumPrettyName(name, sizeof name, "a$b@c");
    Need(!std::strcmp(name, "A B C"), "characters the small face lacks become spaces");
    CeMuseumPrettyName(name, 6, "muspell fighter");
    Need(!std::strcmp(name, "Muspe") && std::strlen(name) == 5, "a long name is cut to the buffer and terminated");
    CeMuseumPrettyName(name, 1, "x");
    Need(name[0] == 0, "a one byte buffer holds only the terminator");
    CeMuseumPrettyName(NULL, 4, "x"); CeMuseumPrettyName(name, 0, "x");
    std::puts("PASS label choice, capsule timing and names");
    Need(!CeMuseumInspectionNear(10.0f, 0, 2.0f, false), "a distant label is not an inspection");
    Need(CeMuseumInspectionNear(8.0f, 0, 2.0f, false), "reaching the exhibit reveals its card");
    Need(CeMuseumInspectionNear(0, -9.0f, 2.0f, true), "one step back keeps the card");
    Need(!CeMuseumInspectionNear(7.0f, 7.0f, 2.0f, true), "walking away hides the card");
    Need(!CeMuseumInspectionNear(7.0f, 0, 0.0f, false), "large and small exhibits have different reach");
    char note[48];
    Need(CeMuseumScaleNote(note, sizeof note, 3) && !std::strcmp(note, "Shown three times life size."), "an enlarged exhibit says its factor plainly");
    Need(CeMuseumScaleNote(note, sizeof note, 8) && !std::strcmp(note, "Shown eight times life size."), "the largest factor has its word");
    Need(CeMuseumScaleNote(note, sizeof note, 2) && !std::strcmp(note, "Shown two times life size."), "the smallest factor has its word");
    Need(!CeMuseumScaleNote(note, sizeof note, 1) && !note[0], "a machine at its own size says nothing");
    Need(!CeMuseumScaleNote(note, sizeof note, 9) && !note[0], "a factor beyond the words says nothing");
    Need(!CeMuseumScaleNote(note, 10, 3) && !note[0], "a note too long for its buffer is dropped, not cut");
    std::puts("PASS scale note");
    return 0;
}
