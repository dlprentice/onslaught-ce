// SPDX-License-Identifier: GPL-3.0-only
// Museum architecture is built from boxes clad in the game's own textures (the
// Forseti base set the visit has already loaded) and lit by the level's lights.
// A texture that is not loaded leaves its pieces in a plain lit colour.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "ce_museum_hall.h"
#include "ce_museum_exhibition_host.h"
#include "ce_museum_world.h"
#include "ce_museum_cinema.h"
#include "ce_museum_cinema_catalogue.h"
#include "ce_museum_cinema_view.h"
#include "ce_photo.h"
#include "binkapi.h"
#include "game.h"
#include "Controller.h"
#include <cstdio>
#include <cstring>
#include "ce_museum_visit.h"
#include "ce_museum_label.h"
#include "../platform/common/portlog.h"
#include "Texture.h"
#include <cmath>
#include <cstdlib>
extern "C" unsigned PortPresentCount();

extern "C" bool PortCeMuseumVisitActive(void);
extern bool gMirrorPass;

namespace {
struct Vertex { float x, y, z; DWORD colour; };
const DWORD kIron = 0xff263641, kSlate = 0xff405563, kEdge = 0xff899aa1;
const DWORD kBrass = 0xffd4a654, kLight = 0xffe6cb88;

// What a piece is clad in. kMatAuto picks a material from the piece's colour.
enum Material {
    kMatAuto, kMatFlat, kMatFloor, kMatInlay, kMatWall, kMatPier, kMatHeader, kMatDoor,
    kMatPlinth, kMatStand, kMatRail, kMatTrim, kMatNumeral, kMatCeiling,
    kMatStone, kMatCarpet, kMatSeat, kMatPlinthTop, kMatSmallPlinth, kMatSmallTop, kMatCount
};
// texture: a loaded game texture (none: plain colour). repeat: metres per tile.
// tint: the vertex colour, or 0 to keep the piece's own colour. glow: emissive share.
struct MaterialSpec { const char *texture; float repeat; DWORD tint; float glow; };
// Atlas pages (the hangar "bits", building pages, pads) do not tile and are not used.
// The level's ambient light leans green, so the grey chrome's tints hold green back.
const MaterialSpec kMaterials[kMatCount] = {
    {0, 1.0f, 0, 0},                                           // auto (resolved before use)
    {0, 1.0f, 0, 0.05f},                                       // flat
    {"meshtex\\docks_floor.tga", 1.5f, 0xffffffff, 0},         // floor: deck plate
    {"meshtex\\M_panel005.tga", 2.0f, 0, 0},                   // inlay: a runner, tinted
    {"meshtex\\FB_hangertop01.tga", 4.0f, 0xffffffff, 0},      // wall: Forseti hangar band
    {"meshtex\\FB_TChromeForsetti.tga", 2.0f, 0xff9a88ad, 0},  // pier and jamb
    {"meshtex\\FB_hangertop02.tga", 3.0f, 0xffffffff, 0},      // header over the portal
    {"meshtex\\FB_TChromeForsetti.tga", 2.0f, 0xff7f789b, 0},  // door
    {"meshtex\\FB_TChromeForsetti.tga", 0.5f, 0xffd2b3dc, 0},  // plinth sides: fine grain, not broad swirls
    {"meshtex\\FB_TChromeForsetti.tga", 1.0f, 0xff746c8c, 0},  // stand
    {"meshtex\\FB_TChromeForsetti.tga", 1.0f, 0xff5a546d, 0},  // rail
    {"meshtex\\FB_TChromeForsetti.tga", 1.0f, 0, 0.10f},       // trim: brass and skirting, tinted
    {0, 1.0f, 0, 0.25f},                                       // numeral: lit plate that glows
    {"meshtex\\FB_hangertop02b.tga", 5.0f, 0xffa8b0b8, 0},     // ceiling: hangar roof panels
    {"meshtex\\FB_hangertop01.tga", 4.0f, 0, 0},               // theater's dark screen wall, tinted
    {"meshtex\\M_panel005.tga", 2.0f, 0, 0},                   // carpet and dark floor, tinted
    {"meshtex\\M_panel005.tga", 1.0f, 0, 0},                   // upholstery, tinted
    {"meshtex\\docks_floor.tga", 0.75f, 0xffb4c0cc, 0},       // plinth top: a finer, cooler deck plate than the floor
    {"meshtex\\FB_TChromeForsetti.tga", 0.5f, 0xffd2b3dc, 0.12f}, // a small object's plinth: lit a little
    {"meshtex\\docks_floor.tga", 0.75f, 0xffb4c0cc, 0.18f},   // and its top, where the object stands
};
struct LitVertex { float x, y, z, nx, ny, nz; DWORD colour; float u, v; };
enum { kLitCapacity = 9600 + CeMuseumExhibition::kMaxBoxes * CeMuseumExhibition::kBoxVertices + 2 * 40 * 40 * 6 };
struct Gallery {
    CeHallTour tour;
    CeHallGround ground;
    LitVertex vertices[kLitCapacity], sorted[kLitCapacity];
    unsigned char material[kLitCapacity / 3];
    unsigned used, first[kMatCount], count[kMatCount];
    CDXTexture *texture[kMatCount];
    Material current;          // material of the triangles being emitted
    bool hasCentre;            // orient normals away from centre; else face up
    float centre[3];
    int zone;
    bool valid, overflow;
    const char *overflowReason; // the first piece that did not fit
} g;
static CeMuseumExhibition::Plan gExhibition = {};
static CeMuseumExhibition::Vec2 gExhibitionVisitor = {};

void Overflow(const char *why) {
    if (!g.overflow) g.overflowReason = why;
    g.overflow = true;
}

Material ColourMaterial(DWORD colour) {
    switch (colour) {
    case kBrass: case 0xffb99262: return kMatTrim;               // brass, bronze
    case kLight: return kMatNumeral;
    case kIron: case kSlate: case kEdge: return kMatStand;
    case 0xff69767a: case 0xff1c272d: return kMatStone;          // cinema stone, proscenium
    case 0xff39444b: case 0xff4b3539: return kMatCarpet;         // cinema floor, carpet
    case 0xff913f43: case 0xffb16961: return kMatSeat;           // upholstery
    case 0xfff3f1e9: return kMatWall;
    case 0xffd5d9d6: case 0xffe1e3df: return kMatFloor;
    default: return kMatFlat;
    }
}

// Normals point out of the piece (away from its centre) or, for floor pieces,
// up (native Z grows down). Texture coordinates are the world position on the
// face's own plane, so every face tiles at the same scale with no stretching.
void Triangle(const Vertex &a, const Vertex &b, const Vertex &c) {
    if (g.used + 3 > kLitCapacity) { Overflow("the vertex buffer is full"); return; }
    const Material m = g.current == kMatAuto ? ColourMaterial(a.colour) : g.current;
    const float ux=b.x-a.x, uy=b.y-a.y, uz=b.z-a.z, vx=c.x-a.x, vy=c.y-a.y, vz=c.z-a.z;
    float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
    const float length=std::sqrt(nx*nx+ny*ny+nz*nz);
    if (length < 1e-8f) return;
    nx/=length; ny/=length; nz/=length;
    bool flip;
    if (g.hasCentre) {
        const float cx=(a.x+b.x+c.x)/3-g.centre[0], cy=(a.y+b.y+c.y)/3-g.centre[1], cz=(a.z+b.z+c.z)/3-g.centre[2];
        flip = nx*cx+ny*cy+nz*cz < 0;
    } else flip = nz > 0;
    if (flip) { nx=-nx; ny=-ny; nz=-nz; }
    const float ax=std::fabs(nx), ay=std::fabs(ny), az=std::fabs(nz);
    const float scale = 1.0f / kMaterials[m].repeat;
    const DWORD tint = kMaterials[m].tint;
    // A plain colour has no texture to darken it: at full strength the level's
    // lights wash it to white, so it starts at 60%. That includes a material whose
    // texture is not loaded (its tint is meant for a texture, so it is dropped). A
    // piece's own colour under a texture holds green back as the fixed tints do
    // (the ambient leans green).
    auto colour = [&](DWORD source) -> DWORD {
        if (!g.texture[m]) return CeMuseumExhibition::Shade(source, 60);
        if (tint) return tint;
        const DWORD green = ((source >> 8) & 255u) * 82u / 100u;
        return (source & 0xffff00ffu) | (green << 8);
    };
    const Vertex *in[3] = {&a, &b, &c};
    for (const Vertex *p : in) {
        LitVertex &v = g.vertices[g.used++];
        const float x=p->x-CeMuseumWorld::kEntranceX, y=p->y-CeMuseumWorld::kEntranceY;
        v.x=p->x; v.y=p->y; v.z=p->z; v.nx=nx; v.ny=ny; v.nz=nz;
        v.colour = colour(p->colour);
        if (az >= ax && az >= ay) { v.u=x*scale; v.v=y*scale; }
        else if (ax >= ay) { v.u=y*scale; v.v=p->z*scale; }
        else { v.u=x*scale; v.v=p->z*scale; }
    }
    g.material[g.used/3 - 1] = static_cast<unsigned char>(m);
}
void Quad(Vertex a, Vertex b, Vertex c, Vertex d) {
    Triangle(a,b,c); Triangle(a,c,d);
}
CeHallPoint Offset(CeHallPoint p, float side, float depth) {
    return {p.x + side * g.tour.right.x + depth * g.tour.forward.x,
            p.y + side * g.tour.right.y + depth * g.tour.forward.y};
}
Vertex Point(CeHallPoint p, float z, DWORD colour) { return {p.x, p.y, z, colour}; }
// One material for the whole box: its sides' colour picks it. The lights shade the sides.
void Box(CeHallPoint p, float w, float d, float z, float h, DWORD face, DWORD edge) {
    CeHallPoint a = Offset(p,-w,-d), b = Offset(p,w,-d), c = Offset(p,w,d), e = Offset(p,-w,d);
    const float top = z - h;
    const Material was = g.current;
    if (g.current == kMatAuto) g.current = ColourMaterial(face);
    g.hasCentre = true; g.centre[0] = p.x; g.centre[1] = p.y; g.centre[2] = z - h*0.5f;
    Quad(Point(a,top,edge),Point(b,top,edge),Point(c,top,edge),Point(e,top,edge));
    Quad(Point(a,z,face),Point(b,z,face),Point(b,top,face),Point(a,top,face));
    Quad(Point(b,z,face),Point(c,z,face),Point(c,top,face),Point(b,top,face));
    Quad(Point(c,z,face),Point(e,z,face),Point(e,top,face),Point(c,top,face));
    Quad(Point(e,z,face),Point(a,z,face),Point(a,top,face),Point(e,top,face));
    g.hasCentre = false; g.current = was;
}
bool FloorVertex(CeHallPoint p, float lift, DWORD colour, Vertex *v) {
    float z;
    if (!g.ground(p.x, p.y, &z)) return false;
    *v = Point(p, z-lift, colour); return true;
}
void Strip(CeHallPoint a, CeHallPoint b, float side, float width, float lift, DWORD colour) {
    float dx = b.x-a.x, dy = b.y-a.y, length = std::sqrt(dx*dx + dy*dy);
    if (length < 0.02f) return;
    float rx=dy/length, ry=-dx/length;
    const unsigned steps = static_cast<unsigned>(std::ceil(length / 0.9f));
    if (steps > 90) { Overflow("a floor strip is too long"); return; }
    for (unsigned i = 0; i < steps; ++i) {
        const float t = float(i)/steps, u = float(i+1)/steps;
        CeHallPoint v = {a.x+t*dx,a.y+t*dy}, w = {a.x+u*dx,a.y+u*dy};
        Vertex av,bv,cv,dv;
        if (!FloorVertex({v.x+rx*(side-width),v.y+ry*(side-width)},lift,colour,&av) ||
            !FloorVertex({v.x+rx*(side+width),v.y+ry*(side+width)},lift,colour,&bv) ||
            !FloorVertex({w.x+rx*(side+width),w.y+ry*(side+width)},lift,colour,&cv) ||
            !FloorVertex({w.x+rx*(side-width),w.y+ry*(side-width)},lift,colour,&dv)) continue;
        Quad(av,bv,cv,dv);
    }
}
void Walk(CeHallPoint a, CeHallPoint b) {
    Strip(a,b,0,0.85f,0.11f,kSlate);
    Strip(a,b,-0.71f,0.075f,0.16f,kBrass);
    Strip(a,b, 0.71f,0.075f,0.16f,kBrass);
}
void Panel(CeHallPoint p, float z, unsigned number) {
    // The seven-segment numerals are raised plates on the face of the stand.
    static const float segments[][4] = {{0,0.38f,0.24f,0.055f},
        {0.21f,0.19f,0.055f,0.19f},{0.21f,-0.19f,0.055f,0.19f},
        {0,-0.38f,0.24f,0.055f},{-0.21f,-0.19f,0.055f,0.19f},
        {-0.21f,0.19f,0.055f,0.19f},{0,0,0.24f,0.055f}};
    const unsigned mask = number == 1 ? 0x06u : (number == 2 ? 0x5bu : 0x4fu);
    for (unsigned i=0;i<7;++i) if (mask & (1u<<i)) {
        const float *s=segments[i];
        Box(Offset(p,s[0],-0.17f),s[2],0.032f,z-1.1f-s[1]+s[3],2*s[3],kLight,kBrass);
    }
}
void Stand(CeHallPoint stop, unsigned number) {
    const CeHallPoint p=Offset(stop,1.65f,0.62f);
    float z;
    if (!g.ground(p.x,p.y,&z)) return;
    Box(p,0.8f,0.47f,z+0.12f,0.35f,kIron,kSlate);
    Box(p,0.23f,0.20f,z-0.19f,0.9f,kSlate,kEdge);
    Box(p,0.58f,0.14f,z-0.49f,1.18f,kIron,kEdge);
    Box(p,0.60f,0.16f,z-1.62f,0.13f,kBrass,kLight);
    Panel(p,z,number);
}
// A solid-colour exhibition bay, constructed only for a measured rear exhibit.
// The slab, sill and lintel frame the installed machine; the open end meets the outer lane.
const DWORD kIvory = 0xffe6e9df, kCeramic = 0xfff3f2df, kGlassFrame = 0xff9ebeb8;

void FittedBox(CeHallPoint p, float halfWidth, float halfDepth, float lift, float height,
               DWORD face = kIvory, DWORD cap = kCeramic) {
    float z;
    if (g.ground(p.x,p.y,&z)) Box(p,halfWidth,halfDepth,z-lift,height,face,cap);
}

void Tile(CeHallPoint p, float width, float depth, float lift, DWORD colour) {
    Vertex a,b,c,d;
    if (FloorVertex(Offset(p,-width,-depth),lift,colour,&a) &&
        FloorVertex(Offset(p, width,-depth),lift,colour,&b) &&
        FloorVertex(Offset(p, width, depth),lift,colour,&c) &&
        FloorVertex(Offset(p,-width, depth),lift,colour,&d)) Quad(a,b,c,d);
}

void AnnexLane(CeHallPoint a, CeHallPoint b) {
    Strip(a,b,0,1.18f,0.13f,kIvory);
    Strip(a,b,-0.96f,0.07f,0.18f,kBrass);
    Strip(a,b, 0.96f,0.07f,0.18f,kBrass);
}

// Walls and floor use the same authored boundary as visitor collision. The long
// glazing panels, repeated upright ribs and darker skirting establish room scale.
void ExhibitionShell() {
    using namespace CeMuseumWorld;
    const CeHallPoint origin={kEntranceX,kEntranceY};
    const float span=kBack-kFront, middle=(kFront+kBack)*0.5f;
    const unsigned columns=10, rows=9;
    const float tileWidth=2*kHalfWidth/columns, tileDepth=span/rows;
    for (unsigned row=0;row<rows;++row) for (unsigned col=0;col<columns;++col) {
        const CeHallPoint p=Offset(origin,-kHalfWidth+(col+0.5f)*tileWidth,kFront+(row+0.5f)*tileDepth);
        Tile(p,tileWidth*0.5f,tileDepth*0.5f,0.015f,(row+col)%2?kFloorA:kFloorB);
    }
    for (int sign=-1;sign<=1;sign+=2) {
        const float side=sign*kHalfWidth;
        auto wall=[&](float a,float b) {
            FittedBox(Offset(origin,side,(a+b)*0.5f),0.30f,(b-a)*0.5f,0,7.3f,kWall,kWallCap);
            FittedBox(Offset(origin,side-sign*0.32f,(a+b)*0.5f),0.10f,(b-a)*0.5f,0,0.45f,kReveal,kWallCap);
        };
        if (sign<0) wall(kFront,kBack);
        else { wall(kFront,CeCinemaRoom::kDoorY-CeCinemaRoom::kDoorHalf); wall(CeCinemaRoom::kDoorY+CeCinemaRoom::kDoorHalf,kBack); }
        for (int n=0;n<4;++n) {
            const float depth=kFront+(n+0.5f)*span/4;
            if (sign>0 && depth-4.8f<CeCinemaRoom::kDoorY+CeCinemaRoom::kDoorHalf && depth+4.8f>CeCinemaRoom::kDoorY-CeCinemaRoom::kDoorHalf) continue;
            FittedBox(Offset(origin,side-sign*0.32f,depth),0.12f,4.8f,2.0f,3.6f,kGlazing,kWallCap);
            FittedBox(Offset(origin,side-sign*0.51f,depth),0.18f,0.11f,0,7.9f,kWall,kWallCap);
        }
    }
    // Stepped back wall and broad light band anchor the sightline from the entrance.
    FittedBox(Offset(origin,0,kBack),kHalfWidth,0.35f,0,8.0f,kWall,kWallCap);
    FittedBox(Offset(origin,0,kBack-0.40f),kHalfWidth-1,0.14f,4.3f,2.0f,kGlazing,kWallCap);
    FittedBox(Offset(origin,0,kFront),kHalfWidth,0.35f,0,6.4f,kWall,kWallCap);
    FittedBox(Offset(origin,0,kFront+0.4f),3.4f,0.17f,0,5.35f,kReveal,kWallCap);
    for (unsigned i=0;i<g.tour.stops && i<2;++i) {
        const CeHallDisplay &d=g.tour.display[i];
        FittedBox({d.x,d.y},d.radius+0.40f,d.radius+0.40f,0,0.24f,kReveal,kWallCap);
    }
}
// A downward-facing ceiling over a rectangle (local coordinates), height above the floor.
void Ceiling(float floor, float x0, float y0, float x1, float y1, float height) {
    const CeHallPoint a = CeMuseumExhibitionHost::World({x0, y0}), b = CeMuseumExhibitionHost::World({x1, y0});
    const CeHallPoint c = CeMuseumExhibitionHost::World({x1, y1}), d = CeMuseumExhibitionHost::World({x0, y1});
    const float z = floor - height;
    g.current = kMatCeiling; g.hasCentre = true;
    g.centre[0] = (a.x + c.x) * 0.5f; g.centre[1] = (a.y + c.y) * 0.5f; g.centre[2] = z - 1.0f;
    // Panels of about 2 m, so the per-vertex lighting has no long diagonal seam.
    const unsigned across = unsigned(std::ceil((x1 - x0) / 2.0f)), along = unsigned(std::ceil((y1 - y0) / 2.0f));
    if (!across || !along || across > 40 || along > 40) {
        Overflow("a ceiling is empty or too large"); g.hasCentre = false; g.current = kMatAuto; return;
    }
    (void)b; (void)d;
    for (unsigned i = 0; i < across; ++i) for (unsigned j = 0; j < along; ++j) {
        const float u0 = x0 + (x1 - x0) * i / across, u1 = x0 + (x1 - x0) * (i + 1) / across;
        const float v0 = y0 + (y1 - y0) * j / along, v1 = y0 + (y1 - y0) * (j + 1) / along;
        Quad(Point(CeMuseumExhibitionHost::World({u0, v0}), z, 0xffffffff),
             Point(CeMuseumExhibitionHost::World({u1, v0}), z, 0xffffffff),
             Point(CeMuseumExhibitionHost::World({u1, v1}), z, 0xffffffff),
             Point(CeMuseumExhibitionHost::World({u0, v1}), z, 0xffffffff));
    }
    g.hasCentre = false; g.current = kMatAuto;
}

#include "ce_museum_cinema.inc"

void ExhibitRoom() {
    const CeHallTour &t=g.tour;
    const CeHallPoint stop=t.stop[2], model={t.display[2].x,t.display[2].y};
    const float radius=t.display[2].radius;
    const float half=radius+1.25f, front=-radius-0.85f, back=radius+1.05f;
    AnnexLane(t.stop[1],t.annexEntry); AnnexLane(t.annexEntry,t.annexCorner);
    AnnexLane(t.annexCorner,stop);
    // A moulded directional chevron reads by shape, as well as by the two-tone inlay.
    const CeHallPoint arrow=Offset(t.annexEntry,0,0.3f);
    Strip(Offset(arrow,-0.67f,-0.38f),Offset(arrow,0,0.42f),0,0.12f,0.20f,kIron);
    Strip(Offset(arrow, 0.67f,-0.38f),Offset(arrow,0,0.42f),0,0.12f,0.20f,kIron);
    const CeHallPoint turnArrow=Offset(t.annexCorner,-0.20f,0);
    Strip(Offset(turnArrow,0.40f,-0.60f),Offset(turnArrow,-0.45f,0),0,0.12f,0.20f,kIron);
    Strip(Offset(turnArrow,0.40f, 0.60f),Offset(turnArrow,-0.45f,0),0,0.12f,0.20f,kIron);
    // On the opening approach the pale ribs are visible over the intervening front model.
    for (int k=0;k<2;++k) {
        const CeHallPoint pole=Offset(t.annexEntry,k ? 1.65f : -1.65f,1.75f);
        FittedBox(pole,0.23f,0.32f,-0.08f,k ? 3.1f : 2.4f);
        FittedBox(pole,0.25f,0.34f,k ? 2.55f : 1.85f,0.20f,kBrass,kLight);
    }
    // Small ground-following tiles prevent the floor vanishing into a slope or floating at an edge.
    const unsigned columns=unsigned(std::ceil((2*half)/1.30f));
    const unsigned rows=unsigned(std::ceil((back-front+2.8f)/1.30f));
    if (!columns || !rows || columns>30 || rows>30) return;
    const float tileWidth=2*half/columns, tileDepth=(back-front+2.8f)/rows;
    for (unsigned y=0;y<rows;++y) for (unsigned x=0;x<columns;++x) {
        const CeHallPoint at=Offset(model,-half+(x+0.5f)*tileWidth,front-2.8f+(y+0.5f)*tileDepth);
        Tile(at,tileWidth/2,tileDepth/2,0.18f,(x+y)%2 ? kIvory : kCeramic);
    }
    const float backHeight=5.5f, middle=(front+back)*0.5f;
    FittedBox(Offset(model,0,back),half,0.22f,-0.10f,backHeight);
    FittedBox(Offset(model,-half,middle),0.22f,(back-front)/2,-0.10f,backHeight);
    FittedBox(Offset(model,half,middle),0.22f,(back-front)/2,-0.10f,backHeight);
    FittedBox(Offset(model,0,back-0.25f),half,0.08f,3.6f,0.30f,kGlassFrame,kCeramic);
    // A low display pedestal is visible under the machine; front opening is deliberately unglazed.
    FittedBox(model,radius+0.26f,radius+0.18f,-0.05f,0.33f,kGlassFrame,kIvory);
    FittedBox(Offset(model,-half,front),0.27f,0.32f,-0.08f,4.5f);
    FittedBox(Offset(model, half,front),0.27f,0.32f,-0.08f,4.5f);
    FittedBox(Offset(model,0,front),half+0.18f,0.31f,4.0f,0.42f,kGlassFrame,kCeramic);
    FittedBox(Offset(model,0,front),half,0.19f,0.20f,0.34f,kGlassFrame,kCeramic);
}

void Entry() {
    // The visitor can pass between the deep, open uprights. Two illuminated
    // bands and the joined lintel distinguish the exit from a display stand.
    const CeHallPoint c=CeHallEntranceArch(g.tour);
    float z;
    if (!g.ground(c.x,c.y,&z)) return;
    for (int side=-1;side<=1;side+=2) {
        CeHallPoint p=Offset(c,side*kCeHallArchPostOffset,0); float foot;
        if (!g.ground(p.x,p.y,&foot)) return;
        Box(p,kCeHallArchFootHalfWidth,0.66f,foot+0.2f,0.48f,kIron,kEdge);
        Box(p,0.31f,0.46f,foot-0.26f,foot-z+4.55f,kSlate,kEdge);
        Box(p,0.36f,0.49f,z-3.12f,0.21f,kBrass,kLight);
        Box(p,0.36f,0.49f,z-3.55f,0.12f,kBrass,kLight);
    }
    Box(c,2.91f,0.48f,z-4.74f,0.49f,kIron,kEdge);
    Box(c,1.06f,0.51f,z-4.79f,0.13f,kBrass,kLight);
}

} // namespace

static Material PurposeMaterial(CeMuseumExhibition::Purpose purpose) {
    using namespace CeMuseumExhibition;
    switch (purpose) {
    case kSolidWall: return kMatWall;
    case kSolidPlinth: return kMatPlinth;
    case kSolidStand: return kMatStand;
    case kSolidDoor: return kMatDoor;
    case kDecorFloor: return kMatFloor;
    case kDecorInlay: return kMatInlay;
    case kDecorTrim: return kMatTrim;
    case kDecorNumeral: return kMatNumeral;
    case kSolidPier: return kMatPier;
    case kSolidHeader: return kMatHeader;
    case kSolidRail: return kMatRail;
    }
    return kMatFlat;
}

static void PlannedExhibition(const CeMuseumExhibition::Plan& plan, float floor) {
    for (unsigned box = 0; box < plan.boxCount; ++box) {
        const CeMuseumExhibition::Box &piece = plan.boxes[box];
        CeMuseumExhibition::Vertex authored[CeMuseumExhibition::kBoxVertices];
        const unsigned count = CeMuseumExhibition::EmitBox(piece, authored);
        const CeHallPoint middle = CeMuseumExhibitionHost::World({(piece.min.x + piece.max.x) * 0.5f,
                                                                  (piece.min.y + piece.max.y) * 0.5f});
        const Material own = PurposeMaterial(piece.purpose);
        // A plinth's top is a calm deck plate; a small object's plinth is lit a little
        // more than the rest, so the eye finds it (the object itself keeps the level's light).
        const CeMuseumExhibition::Bay *bay = piece.bay >= 1 && unsigned(piece.bay) <= plan.bayCount ? &plan.bays[piece.bay - 1] : 0;
        const bool small = own == kMatPlinth && bay && CeMuseumExhibition::IsSmall(bay->radius, bay->height);
        g.hasCentre = true;
        g.centre[0] = middle.x; g.centre[1] = middle.y; g.centre[2] = floor - (piece.min.z + piece.max.z) * 0.5f;
        for (unsigned i = 0; i + 2 < count; i += 3) {
            Vertex native[3];
            for (unsigned j = 0; j < 3; ++j) {
                const CeMuseumExhibition::Vertex& v = authored[i + j];
                // The plan counts height upward; the native world counts Z down. The
                // box's own colour is used on every face: the lights shade the sides.
                native[j] = Point(CeMuseumExhibitionHost::World({v.x, v.y}),
                                  floor - v.z, piece.colour);
            }
            const bool top = own == kMatPlinth && authored[i].z == piece.max.z && authored[i + 1].z == piece.max.z &&
                             authored[i + 2].z == piece.max.z;
            g.current = top ? (small ? kMatSmallTop : kMatPlinthTop) : small ? kMatSmallPlinth : own;
            Triangle(native[0], native[1], native[2]);
        }
    }
    g.hasCentre = false; g.current = kMatAuto;
    // The roof sits on the walls' outer faces; the theater has its own at its height.
    const float wall = CeMuseumExhibition::kWallHalf;
    Ceiling(floor, plan.room.minX - wall, plan.room.minY - wall, plan.room.maxX + wall, plan.room.maxY + wall,
            plan.wallHeight);
}

// The name a material looks up. BEA_PORT_MUSEUM_MISSING_TEXTURE (a test setting,
// unset in play) swaps every name containing its text for one no install has,
// so a run can show the fallback for a texture a level does not load.
static const char *MaterialName(unsigned m) {
    const char *name = kMaterials[m].texture;
    const char *missing = std::getenv("BEA_PORT_MUSEUM_MISSING_TEXTURE");
    if (name && missing && *missing && std::strstr(name, missing)) return "meshtex\\ce_museum_test_missing.tga";
    return name;
}
// Only a texture the level has already loaded is taken, matched by name as
// FindTexture matches (case ignored). FindTexture itself is not called: for a
// name that is not loaded, or loaded with other mip levels, it ends the game.
static CDXTexture *LoadedTexture(const char *name) {
    for (CDXTexture *t = CDXTexture::mFirst; t; t = t->CTexture::mNext)
        if (!stricmp(name, t->GetName())) { t->AddRef(); return t; }
    return 0;
}
// A missing texture leaves its pieces in a plain lit colour; it is named once in
// the log. Each found texture is held (once per material) until the hall is cleared.
static void FindMaterials() {
    for (unsigned m = 0; m < kMatCount; ++m) {
        g.texture[m] = 0;
        const char *name = MaterialName(m);
        if (!name) continue;
        bool earlier = false;
        for (unsigned k = 0; k < m && !earlier; ++k) {
            const char *other = MaterialName(k);
            if (!other || std::strcmp(other, name)) continue;
            earlier = true;
            if (g.texture[k]) { g.texture[m] = g.texture[k]; g.texture[m]->AddRef(); }
        }
        if (earlier) continue;
        g.texture[m] = LoadedTexture(name);
        if (!g.texture[m]) PortSessionLog(PORT_SESSION_GAME, false, "museum: %s is not loaded; its pieces are plain colour", name);
    }
}
// A level's end can free every texture first; release only one still listed.
static void ReleaseMaterials() {
    for (unsigned m = 0; m < kMatCount; ++m) {
        if (!g.texture[m]) continue;
        for (CDXTexture *t = CDXTexture::mFirst; t; t = t->CTexture::mNext)
            if (t == g.texture[m]) { t->Release(); break; }
        g.texture[m] = 0;
    }
}
// Group the triangles by material so each draws in one call.
static void SortMaterials() {
    unsigned triangles = g.used / 3, at = 0;
    for (unsigned m = 0; m < kMatCount; ++m) g.count[m] = 0;
    for (unsigned t = 0; t < triangles; ++t) ++g.count[g.material[t]];
    for (unsigned m = 0; m < kMatCount; ++m) { g.first[m] = at; at += g.count[m] * 3; }
    unsigned next[kMatCount];
    for (unsigned m = 0; m < kMatCount; ++m) next[m] = g.first[m];
    for (unsigned t = 0; t < triangles; ++t) {
        const unsigned m = g.material[t];
        for (unsigned j = 0; j < 3; ++j) g.sorted[next[m]++] = g.vertices[t * 3 + j];
    }
}

// A planned hall's zone is the bay's own number (1 to 6), 0 inside the return
// door, and -1 anywhere else.
static int ExhibitionZone(float x, float y, int previous) {
    const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
    const int number = CeMuseumExhibition::StandingBay(gExhibition, local, previous > 0 ? previous : 0);
    if (number > 0) return number;
    const CeMuseumExhibition::Zone* arrival = CeMuseumExhibition::ZoneFor(gExhibition, CeMuseumExhibition::kZoneArrival);
    return arrival && local.x >= arrival->min.x && local.x <= arrival->max.x &&
           local.y >= arrival->min.y && local.y <= arrival->max.y ? 0 : -1;
}

void CeMuseumHallClear() {
    gExhibition = CeMuseumExhibition::Plan{};
    gExhibitionVisitor = CeMuseumExhibition::Vec2{};
    ReleaseMaterials();
 CinemaReset(); g.valid=false; g.used=0; g.zone=-1; g.ground=0; g.current=kMatAuto; g.hasCentre=false; g.overflowReason=0; }
static void HallFailed(const char *why) {
    PortSessionLog(PORT_SESSION_GAME, false, "museum: the hall was not built: %s", why);
}
static void SetHall(const CeHallTour &tour, CeHallGround ground, const CeMuseumExhibition::Plan* exhibition) {
    CeMuseumHallClear();
    if (!ground) { HallFailed("no ground lookup"); return; }
    if (!tour.stops || tour.stops > 3) { HallFailed("the tour has no stops or too many"); return; }
    g.tour=tour; g.ground=ground; g.overflow=false; g.zone=0;
    // Materials first: a piece's vertex colour depends on whether its texture is there.
    FindMaterials();
    if (exhibition) {
        // The plan is the whole hall: its enclosure, floor, bays and portal. Only
        // the theater beyond the portal is added, once.
        if (!exhibition->valid) { HallFailed("the exhibition plan is not valid"); ReleaseMaterials(); return; }
        float floor = 0.0f;
        if (!g.ground(CeMuseumWorld::kEntranceX, CeMuseumWorld::kEntranceY, &floor)) {
            HallFailed("no ground under the entrance"); ReleaseMaterials(); return;
        }
        gExhibition = *exhibition;
        g.zone = -1;
        PlannedExhibition(gExhibition, floor);
        if (PortCeMuseumAuthoredMap()) CinemaShell(true, floor, gExhibition.wallHeight);
    } else {
        if (PortCeMuseumAuthoredMap()) { ExhibitionShell(); CinemaShell(false, 0, 0); }
        Walk(Offset(tour.entrance,0,-1.5f),tour.stop[0]);
        if(tour.stops >= 2) Walk(tour.stop[0],tour.stop[1]);
        if(tour.stops == 3) ExhibitRoom();
        Entry();
        for (unsigned k=0;k<tour.stops;++k) Stand(tour.stop[k],k+1);
    }
    g.valid=g.used && !g.overflow;
    if (g.valid) { SortMaterials(); return; }
    HallFailed(g.overflow ? (g.overflowReason ? g.overflowReason : "too many pieces") : "no pieces were built");
    ReleaseMaterials();
}

void CeMuseumHallSet(const CeHallTour& tour, CeHallGround ground) {
    SetHall(tour, ground, 0);
}

void CeMuseumHallSetExhibition(const CeHallTour& tour, CeHallGround ground,
                               const CeMuseumExhibition::Plan& exhibition) {
    SetHall(tour, ground, &exhibition);
}

bool CeMuseumHallAllowsVisitor(float x,float y) {
    if (g.valid && gExhibition.valid) {
        const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
        const CeMuseumExhibition::Walk walk = CeMuseumExhibition::WalkAt(gExhibition, local);
        if (walk == CeMuseumExhibition::kWalkOutside)
            return (CeCinemaRoom::Interior(local.x, local.y) || CeCinemaRoom::InDoor(local.x, local.y)) &&
                   !CeCinemaRoom::Seat(local.x, local.y);
        return walk != CeMuseumExhibition::kWalkBlocked;
    }
 return !g.valid || ((!PortCeMuseumAuthoredMap() || CeMuseumWorld::Inside(x,y)) && CeHallBayAllows(g.tour,{x,y})); }

void CeMuseumHallVisitor(float x, float y) {
    gExhibitionVisitor = CeMuseumExhibitionHost::Local(x, y);

    if (!g.valid) return;
    CinemaVisitor(x,y);
    if (gExhibition.valid) {
        const int zone = ExhibitionZone(x, y, g.zone);
        if (zone == g.zone) return;
        g.zone = zone;
        if (zone < 0) return;
        const int bays = int(gExhibition.bayCount);
        char title[32], guide[96];
        if (zone == 0) {
            std::snprintf(title, sizeof title, "Gallery Entrance");
            std::snprintf(guide, sizeof guide, "Display 1 is on your %s. The numbers run round the hall.",
                          gExhibition.bays[0].side < 0 ? "left" : "right");
        } else {
            std::snprintf(title, sizeof title, "Display %d", zone);
            if (zone < bays) std::snprintf(guide, sizeof guide, "Display %d is next along the runner.", zone + 1);
            else std::snprintf(guide, sizeof guide, "The last display. The runner leads back to the entrance.");
        }
        PortCeMuseumLabelsWayfinding(title, guide);
        PortSessionLog(PORT_SESSION_GAME, false, "museum: visitor reached %s", title);
        return;
    }
    const int zone = CeHallZone(g.tour, {x, y}, g.zone);
    if (zone==g.zone) return;
    g.zone=zone;
    if (zone<0) return;
    const char *title = zone==0 ? "Gallery Entrance" : zone==1 ? "Display 1" :
                        zone==2 ? "Display 2" : "Collection Annex";
    const char *guide = zone==0 ? "Follow the brass lines to the displays." :
                        zone==1 && g.tour.stops>1 ? "Follow the cross aisle to Display 2." :
                        zone==2 && g.tour.stops==3 ? "The white lane leads to the Collection Annex." :
                        zone==3 ? "Display 3. Follow the white lane back to the entrance." :
                        zone==2 ? "Follow the aisle back to the entrance." : "The entrance is behind you.";
    PortCeMuseumLabelsWayfinding(title,guide);
    PortSessionLog(PORT_SESSION_GAME,false,"museum: visitor reached %s",title);
}

int CeMuseumHallSignLabel(float forwardX, float forwardY, int previousLabel, int *displayNumber) {
    if (g.valid && gExhibition.valid) {
        int previousBay = -1;
        for (unsigned i = 0; i < gExhibition.bayCount; ++i)
            if (gExhibition.bays[i].stableLabel == previousLabel)
                previousBay = gExhibition.bays[i].number;
        const int number = CeMuseumExhibition::FocusBay(gExhibition, gExhibitionVisitor,
            {forwardX, forwardY}, previousBay);
        if (displayNumber) *displayNumber = number;
        return CeMuseumExhibition::LabelForBay(gExhibition, number);
    }

    const int label = g.valid ? CeHallSignLabel(g.tour, g.zone, {forwardX, forwardY}, previousLabel) : -1;
    if (displayNumber) *displayNumber = label >= 0 ? g.zone : 0;
    return label;
}

bool CeMuseumHallFacesExit(float x, float y, float forwardX, float forwardY, bool held) {
    if (g.valid && gExhibition.valid)
        return CeMuseumExhibition::FacesExit(gExhibition,
            CeMuseumExhibitionHost::Local(x, y), {forwardX, forwardY}, held);

    return g.valid && CeHallFacesExit(g.tour, {x, y}, {forwardX, forwardY}, held);
}

extern "C" void PortCeMuseumCinemaView(const float *view, unsigned present) {
    CinemaCaptureView(view, present);
}
extern "C" bool PortCeMuseumCinemaFire(bool release,float value) {
    return CinemaAction(release,value);
}
extern "C" bool PortCeMuseumCinemaPanel(CeMuseumCinemaConsole::Presentation *out,
    CeMuseumCinemaConsole::MeasureText measure, void *user, float lineHeight) {
    return CinemaPresentation(out, measure, user, lineHeight);
}
extern "C" bool PortCeMuseumCinemaCue(char *title,unsigned titleCap,char *line,unsigned lineCap) {
    return CinemaCue(title,titleCap,line,lineCap);
}
// The hall and the theater are roofed: only a crack at a roof's edge would show
// the backdrop, so it is the colour of shadow under the roof, not a sky.
static const DWORD kRoofShadow = 0xff20262a;
extern "C" void PortCeMuseumAuthoredBackdrop() {
    IDirect3DDevice9 *dev=LT.GetDevice();
    if (dev && !gMirrorPass && PortCeMuseumAuthoredMap())
        dev->Clear(0,0,D3DCLEAR_TARGET,kRoofShadow,1.0f,0);
}

extern "C" void PortCeMuseumHallDraw() {
    if (!g.valid || gMirrorPass || !PortCeMuseumVisitActive()) return;
    IDirect3DDevice9 *dev=LT.GetDevice();
    if (!dev) return;
    IDirect3DStateBlock9 *saved=0;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL,&saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }
    D3DMATRIX world={},view,projection;
    world._11=world._22=world._33=world._44=1;
    RENDERINFO.GetView(&view); RENDERINFO.GetProjection(&projection);
    dev->SetVertexShader(0); dev->SetPixelShader(0);
    dev->SetTransform(D3DTS_WORLD,&world); dev->SetTransform(D3DTS_VIEW,&view); dev->SetTransform(D3DTS_PROJECTION,&projection);
    dev->SetTexture(0,0); dev->SetTexture(1,0);
    // The level's own lights (enabled by the world pass) shade every piece; the
    // vertex colour is the material's colour and the texture modulates it.
    D3DMATERIAL9 material = {};
    material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1.0f;
    material.Ambient = material.Diffuse;
    dev->SetRenderState(D3DRS_LIGHTING,TRUE); dev->SetRenderState(D3DRS_COLORVERTEX,TRUE);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE,D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE,D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE,D3DMCS_MATERIAL);
    dev->SetRenderState(D3DRS_SPECULARENABLE,FALSE); dev->SetRenderState(D3DRS_NORMALIZENORMALS,TRUE);
    dev->SetRenderState(D3DRS_FOGENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE); dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE); dev->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
    dev->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL); dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID); dev->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    dev->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
    dev->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP); dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR); dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    dev->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    dev->SetTexture(1,0);
    dev->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1);
    for (unsigned m = 0; m < kMatCount; ++m) {
        if (!g.count[m]) continue;
        IDirect3DTexture9 *texture = g.texture[m] ? g.texture[m]->GetTexture() : 0;
        dev->SetTexture(0,texture);
        dev->SetTextureStageState(0,D3DTSS_COLOROP,texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0,D3DTSS_COLORARG1,texture ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
        material.Emissive.r = material.Emissive.g = material.Emissive.b = kMaterials[m].glow;
        dev->SetMaterial(&material);
        // Inlays lie 12 mm over the floor: pull them forward so far ones never flicker.
        const float bias = m == kMatInlay ? -0.00002f : 0.0f, slope = m == kMatInlay ? -1.0f : 0.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS,*reinterpret_cast<const DWORD *>(&bias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,*reinterpret_cast<const DWORD *>(&slope));
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,g.count[m],g.sorted + g.first[m],sizeof(LitVertex));
    }
    const float none = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS,*reinterpret_cast<const DWORD *>(&none));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,*reinterpret_cast<const DWORD *>(&none));
    // The theater's console and screen keep their own unlit looks.
    dev->SetRenderState(D3DRS_LIGHTING,FALSE);
    dev->SetTexture(0,0);
    dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
    CinemaConsoleGeometry(dev);
    CinemaScreen(dev);
    saved->Apply(); saved->Release();
}
#endif
