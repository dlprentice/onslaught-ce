// SPDX-License-Identifier: GPL-3.0-only
// Grand Outdoor/Indoor Museum architecture, procedural coastal sky dome, original
// GPL-3.0-only architectural materials, interactive fixtures, and Screening Room host.
// All 3D exhibits, paintings, films, and audio clips load at runtime from the player's
// installed retail files; a missing texture or asset falls back cleanly to plain colour.
#ifdef CE_VERSION
#include "common.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "ce_museum_hall.h"
#include "ce_museum_exhibition_host.h"
#include "ce_museum_world.h"
#include "ce_museum_textures.h"
#include "ce_museum_tour.h"
#include "ce_museum_turntable.h"
#include "ce_museum_art.h"
#include "ce_museum_audio_kiosk.h"
#include "ce_museum_cinema.h"
#include "ce_museum_cinema_catalogue.h"
#include "ce_museum_cinema_view.h"
#include "ce_museum_music_duck.h"
#include "ce_museum_film.h"
#include "ce_photo.h"
#include "binkapi.h"
#include "game.h"
#include "Controller.h"
#include "ce_tutorial_pad.h"
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
int PortCeGamepadInputs(int id, int pad, SControlInput *out);

namespace {
struct Vertex { float x, y, z; DWORD colour; };
const DWORD kIron = 0xff263641, kSlate = 0xff405563, kEdge = 0xff899aa1;
const DWORD kBrass = 0xffd4a654, kLight = 0xffe6cb88;

enum Material {
    kMatAuto, kMatFlat, kMatFloor, kMatCourtyard, kMatInlay, kMatWall, kMatPier, kMatHeader, kMatDoor,
    kMatPlinth, kMatStand, kMatRail, kMatTrim, kMatNumeral, kMatCeiling,
    kMatStone, kMatCarpet, kMatSeat, kMatPlinthTop, kMatSmallPlinth, kMatSmallTop,
    kMatScreenFrame, kMatCinemaWall, kMatCinemaCeiling, kMatCinemaPost,
    kMatPierCap, kMatPierBase, kMatPierLine, kMatCinemaCap, kMatCinemaBase, kMatScreenIdle,
    kMatWater, kMatForsetiTrim, kMatMuspellTrim,
    kMatCount
};

// Each architectural surface maps to one original GPL-3.0-only texture (CE_MUSEUM_TEX_COUNT = plain colour).
struct MaterialSpec {
    CeMuseumTexId tex;
    float repeat;
    DWORD tint;
    float glow;
    float quiet;
    unsigned lift;
};

const MaterialSpec kMaterials[kMatCount] = {
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0,          0.00f, 0.00f, 0}, // auto
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0,          0.05f, 0.00f, 0}, // flat
    {CE_MUSEUM_TEX_GALLERY_FLOOR,   3.2f, 0xfff5f4f0, 0.00f, 0.00f, 0}, // gallery floor slabs
    {CE_MUSEUM_TEX_COURTYARD_PAVER, 3.2f, 0xfffdf9f0, 0.02f, 0.00f, 0}, // courtyard sandstone pavers
    {CE_MUSEUM_TEX_RUNNER_STONE,    2.6f, 0xffd6dde3, 0.00f, 0.00f, 0}, // basalt wayfinding runner
    {CE_MUSEUM_TEX_LIMESTONE_WALL,  4.0f, 0xfff9f6ef, 0.00f, 0.00f, 0}, // dressed limestone walls
    {CE_MUSEUM_TEX_SLATE_PIER,      2.4f, 0xffdde4ea, 0.00f, 0.00f, 0}, // honed slate piers/columns
    {CE_MUSEUM_TEX_LIMESTONE_WALL,  3.6f, 0xfff1ede4, 0.00f, 0.00f, 0}, // portal header beams
    {CE_MUSEUM_TEX_SLATE_PIER,      2.2f, 0xffc0cad2, 0.00f, 0.00f, 0}, // return arch / doors
    {CE_MUSEUM_TEX_PLINTH_MARBLE,   2.0f, 0xffeff2f4, 0.00f, 0.00f, 0}, // plinth bodies
    {CE_MUSEUM_TEX_SLATE_PIER,      1.6f, 0xffbcc6ce, 0.00f, 0.00f, 0}, // lecterns / flight pylons
    {CE_MUSEUM_TEX_SLATE_PIER,      1.8f, 0xffced6dc, 0.00f, 0.00f, 0}, // balustrades / rails
    {CE_MUSEUM_TEX_BRUSHED_BRASS,   1.4f, 0xfffff1d0, 0.09f, 0.00f, 0}, // brushed museum brass
    {CE_MUSEUM_TEX_BRUSHED_BRASS,   1.0f, 0xfffff2c6, 0.24f, 0.00f, 0}, // glowing brass numerals/bars
    {CE_MUSEUM_TEX_COFFERED_CEILING,4.0f, 0xffe4eaf0, 0.02f, 0.00f, 0}, // coffered dark ceiling
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 2.4f, 0xffb2bcc6, 0.00f, 0.00f, 0}, // cinema proscenium wall
    {CE_MUSEUM_TEX_RUNNER_STONE,    2.2f, 0xff9a8a8e, 0.00f, 0.00f, 0}, // cinema dark carpet
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 1.2f, 0xffd07e80, 0.00f, 0.00f, 0}, // cinema seats upholstery
    {CE_MUSEUM_TEX_SLATE_PIER,      1.6f, 0xffc8d2da, 0.00f, 0.00f, 0}, // plinth top deck
    {CE_MUSEUM_TEX_PLINTH_MARBLE,   1.6f, 0xfff4f6f8, 0.08f, 0.00f, 0}, // small exhibit plinth
    {CE_MUSEUM_TEX_SLATE_PIER,      1.4f, 0xffd2dce4, 0.12f, 0.00f, 0}, // small exhibit plinth top
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff8d97a1, 0.00f, 0.00f, 0}, // cinema screen frame
    {CE_MUSEUM_TEX_CINEMA_ACOUSTIC, 2.6f, 0xffc4ced8, 0.00f, 0.00f, 0}, // cinema side walls
    {CE_MUSEUM_TEX_COFFERED_CEILING,4.0f, 0xffb8c2cc, 0.00f, 0.00f, 0}, // cinema ceiling
    {CE_MUSEUM_TEX_SLATE_PIER,      1.6f, 0xffb4bec8, 0.00f, 0.00f, 0}, // projector post
    {CE_MUSEUM_TEX_LIMESTONE_WALL,  2.0f, 0xfffcf9f0, 0.00f, 0.00f, 0}, // pier cap
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff949ea8, 0.00f, 0.00f, 0}, // pier base course
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff4e5862, 0.00f, 0.00f, 0}, // floor shadow reveal line
    {CE_MUSEUM_TEX_LIMESTONE_WALL,  2.0f, 0xffe4e0d8, 0.00f, 0.00f, 0}, // cinema projector cap
    {CE_MUSEUM_TEX_SLATE_PIER,      2.0f, 0xff848e98, 0.00f, 0.00f, 0}, // cinema projector base
    {CE_MUSEUM_TEX_COUNT,           1.0f, 0,          0.00f, 0.00f, 0}, // idle cinema screen glass
    {CE_MUSEUM_TEX_WATER_BASIN,     3.2f, 0xffe6f6ff, 0.08f, 0.00f, 0}, // calm reflecting water basin
    {CE_MUSEUM_TEX_FORSETI_TRIM,    2.2f, 0xffeef6ff, 0.04f, 0.00f, 0}, // Forseti steel-blue accent
    {CE_MUSEUM_TEX_MUSPELL_TRIM,    2.2f, 0xffffece0, 0.04f, 0.00f, 0}, // Muspell iron-amber accent
};

bool CinemaMaterial(unsigned m) {
    switch (m) {
    case kMatStone: case kMatCarpet: case kMatSeat: case kMatScreenFrame: case kMatCinemaWall:
    case kMatCinemaCeiling: case kMatCinemaPost: case kMatCinemaCap: case kMatCinemaBase: return true;
    default: return false;
    }
}

struct LitVertex { float x, y, z, nx, ny, nz; DWORD colour; float u, v; };
enum { kLitCapacity = 96000 };

struct Gallery {
    CeHallTour tour;
    CeHallGround ground;
    LitVertex vertices[kLitCapacity], sorted[kLitCapacity];
    unsigned char material[kLitCapacity / 3];
    unsigned used, first[kMatCount], count[kMatCount];
    IDirect3DTexture9 *texture[kMatCount];
    Material current;
    bool hasCentre;
    float centre[3];
    int zone;
    bool valid, overflow;
    const char *overflowReason;
} g;

static CeMuseumExhibition::Plan gExhibition = {};
static CeMuseumExhibition::Vec2 gExhibitionVisitor = {};
static CeMuseumExhibition::Grand::RoomGraph gGrandGraph = {};
static bool gGrandMode = false;
static float gGrandFloorZ = 0.0f;
static CeMuseumTour::Wing gCurrentWing = CeMuseumTour::kWingCourtyard;
static bool gWingAnnounced = false;
static double gLastStepClock = 0.0;
static bool gHasStepClock = false;

// Atrium Directory Lectern interaction & quick-jump state
static CeMuseumTour::DirectoryHit gDirHit = {false, -1};
static bool gDirFireHeld = false;
static bool gDirFireCaptured = false;
static bool gPendingJump = false;
static float gJumpX = 0.0f, gJumpY = 0.0f, gJumpYaw = 0.0f;

void Overflow(const char *why) {
    if (!g.overflow) g.overflowReason = why;
    g.overflow = true;
}

Material ColourMaterial(DWORD colour) {
    switch (colour) {
    case kBrass: case 0xffb99262: return kMatTrim;
    case kLight: return kMatNumeral;
    case kIron: case kSlate: case kEdge: return kMatStand;
    case 0xff69767a: case 0xff1c272d: return kMatStone;
    case 0xff39444b: case 0xff4b3539: return kMatCarpet;
    case 0xff913f43: case 0xffb16961: return kMatSeat;
    case 0xfff3f1e9: return kMatWall;
    case 0xffd5d9d6: case 0xffe1e3df: return kMatFloor;
    default: return kMatFlat;
    }
}

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
    auto colour = [&](DWORD source) -> DWORD {
        if (!g.texture[m]) return CeMuseumExhibition::Shade(source, 68);
        if (tint) return tint;
        return source;
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

void ExhibitionShell() {
    using namespace CeMuseumWorld;
    const CeHallPoint origin={kEntranceX,kEntranceY};
    const float span=kBack-kFront;
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
    }
    FittedBox(Offset(origin,0,kBack),kHalfWidth,0.35f,0,8.0f,kWall,kWallCap);
    FittedBox(Offset(origin,0,kFront),kHalfWidth,0.35f,0,6.4f,kWall,kWallCap);
}

void Ceiling(float floor, float x0, float y0, float x1, float y1, float height, Material material = kMatCeiling) {
    const CeHallPoint a = CeMuseumExhibitionHost::World({x0, y0});
    const CeHallPoint c = CeMuseumExhibitionHost::World({x1, y1});
    const float z = floor - height;
    g.current = material; g.hasCentre = true;
    g.centre[0] = (a.x + c.x) * 0.5f; g.centre[1] = (a.y + c.y) * 0.5f; g.centre[2] = z - 1.0f;
    const unsigned across = unsigned(std::ceil((x1 - x0) / 4.0f)), along = unsigned(std::ceil((y1 - y0) / 4.0f));
    if (!across || !along || across > 40 || along > 40) {
        Overflow("a ceiling is empty or too large"); g.hasCentre = false; g.current = kMatAuto; return;
    }
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
}

void Entry() {
    const CeHallPoint c=CeHallEntranceArch(g.tour);
    float z;
    if (!g.ground(c.x,c.y,&z)) return;
    for (int side=-1;side<=1;side+=2) {
        CeHallPoint p=Offset(c,side*kCeHallArchPostOffset,0); float foot;
        if (!g.ground(p.x,p.y,&foot)) return;
        Box(p,kCeHallArchFootHalfWidth,0.66f,foot+0.2f,0.48f,kIron,kEdge);
        Box(p,0.31f,0.46f,foot-0.26f,foot-z+4.55f,kSlate,kEdge);
    }
    Box(c,2.91f,0.48f,z-4.74f,0.49f,kIron,kEdge);
}

// ---- Grand Outdoor/Indoor Multi-Wing Architectural Builder ----
void LocalBox(float x0, float y0, float z0, float x1, float y1, float z1,
              float floorZ, Material mat, DWORD col = 0xffffffff) {
    if (!(x1 > x0 && y1 > y0 && z1 > z0)) return;
    const CeMuseumExhibition::Box b = {
        {x0, y0, z0}, {x1, y1, z1}, col, CeMuseumExhibition::kSolidWall, 0
    };
    CeMuseumExhibition::Vertex authored[CeMuseumExhibition::kBoxVertices];
    const unsigned count = CeMuseumExhibition::EmitBox(b, authored);
    const CeHallPoint middle = CeMuseumExhibitionHost::World({0.5f * (x0 + x1), 0.5f * (y0 + y1)});
    g.hasCentre = true;
    g.current = mat;
    g.centre[0] = middle.x;
    g.centre[1] = middle.y;
    g.centre[2] = floorZ - 0.5f * (z0 + z1);
    for (unsigned i = 0; i + 2 < count; i += 3) {
        Vertex native[3];
        for (unsigned j = 0; j < 3; ++j)
            native[j] = Point(CeMuseumExhibitionHost::World({authored[i + j].x, authored[i + j].y}),
                              floorZ - authored[i + j].z, authored[i + j].colour);
        Triangle(native[0], native[1], native[2]);
    }
    g.hasCentre = false;
    g.current = kMatAuto;
}

void LocalFloorGrid(float x0, float y0, float x1, float y1, float lift,
                    float floorZ, Material mat, float step = 4.0f, DWORD col = 0xffffffff) {
    const unsigned nx = unsigned(std::ceil((x1 - x0) / step));
    const unsigned ny = unsigned(std::ceil((y1 - y0) / step));
    if (!nx || !ny || nx > 32 || ny > 32) return;
    const float z = floorZ - lift;
    g.hasCentre = false;
    g.current = mat;
    for (unsigned ix = 0; ix < nx; ++ix) {
        const float ax = x0 + (x1 - x0) * ix / nx;
        const float bx = x0 + (x1 - x0) * (ix + 1) / nx;
        for (unsigned iy = 0; iy < ny; ++iy) {
            const float ay = y0 + (y1 - y0) * iy / ny;
            const float by = y0 + (y1 - y0) * (iy + 1) / ny;
            Quad(Point(CeMuseumExhibitionHost::World({ax, ay}), z, col),
                 Point(CeMuseumExhibitionHost::World({bx, ay}), z, col),
                 Point(CeMuseumExhibitionHost::World({bx, by}), z, col),
                 Point(CeMuseumExhibitionHost::World({ax, by}), z, col));
        }
    }
    g.current = kMatAuto;
}

void DressedColumn(float cx, float cy, float half, float height, float floorZ) {
    LocalBox(cx - half - 0.14f, cy - half - 0.14f, 0.02f, cx + half + 0.14f, cy + half + 0.14f, 0.08f, floorZ, kMatPierLine, kIron);
    LocalBox(cx - half - 0.10f, cy - half - 0.10f, 0.08f, cx + half + 0.10f, cy + half + 0.10f, 0.62f, floorZ, kMatPierBase, kSlate);
    LocalBox(cx - half, cy - half, 0.62f, cx + half, cy + half, height - 0.45f, floorZ, kMatPier, kSlate);
    LocalBox(cx - half - 0.06f, cy - half - 0.06f, height - 0.56f, cx + half + 0.06f, cy + half + 0.06f, height - 0.45f, floorZ, kMatTrim, kBrass);
    LocalBox(cx - half - 0.12f, cy - half - 0.12f, height - 0.45f, cx + half + 0.12f, cy + half + 0.12f, height, floorZ, kMatPierCap, kIvory);
}

void RunnerStripX(float x0, float x1, float cy, float halfW, float floorZ) {
    LocalFloorGrid(x0, cy - halfW, x1, cy + halfW, 0.055f, floorZ, kMatInlay, 4.0f, 0xffd0d6dc);
    LocalFloorGrid(x0, cy - halfW - 0.10f, x1, cy - halfW, 0.066f, floorZ, kMatTrim, 4.0f, kBrass);
    LocalFloorGrid(x0, cy + halfW, x1, cy + halfW + 0.10f, 0.066f, floorZ, kMatTrim, 4.0f, kBrass);
}

void RunnerStripY(float cx, float y0, float y1, float halfW, float floorZ) {
    LocalFloorGrid(cx - halfW, y0, cx + halfW, y1, 0.055f, floorZ, kMatInlay, 4.0f, 0xffd0d6dc);
    LocalFloorGrid(cx - halfW - 0.10f, y0, cx - halfW, y1, 0.066f, floorZ, kMatTrim, 4.0f, kBrass);
    LocalFloorGrid(cx + halfW, y0, cx + halfW + 0.10f, y1, 0.066f, floorZ, kMatTrim, 4.0f, kBrass);
}

void TurntablePlinth(float cx, float cy, float half, float topZ, float floorZ, bool tiered = false) {
    if (tiered) {
        LocalBox(cx - half, cy - half, 0.02f, cx + half, cy + half, 0.36f, floorZ, kMatWall, 0xffe8e4dc);
        LocalBox(cx - half * 0.86f, cy - half * 0.86f, 0.36f, cx + half * 0.86f, cy + half * 0.86f, 0.86f, floorZ, kMatPier, kSlate);
        LocalBox(cx - half * 0.76f, cy - half * 0.76f, 0.86f, cx + half * 0.76f, cy + half * 0.76f, topZ - 0.06f, floorZ, kMatPlinth, 0xffdce2e6);
        LocalBox(cx - half * 0.79f, cy - half * 0.79f, topZ - 0.12f, cx + half * 0.79f, cy + half * 0.79f, topZ - 0.03f, floorZ, kMatTrim, kBrass);
        LocalBox(cx - half * 0.74f, cy - half * 0.74f, topZ - 0.06f, cx + half * 0.74f, cy + half * 0.74f, topZ, floorZ, kMatPlinthTop, kSlate);
        return;
    }
    LocalBox(cx - half, cy - half, 0.02f, cx + half, cy + half, 0.16f, floorZ, kMatPierBase, kSlate);
    LocalBox(cx - half * 0.94f, cy - half * 0.94f, 0.16f, cx + half * 0.94f, cy + half * 0.94f, topZ - 0.06f, floorZ, kMatPlinth, 0xffdde2e6);
    LocalBox(cx - half * 0.97f, cy - half * 0.97f, topZ - 0.10f, cx + half * 0.97f, cy + half * 0.97f, topZ - 0.04f, floorZ, kMatTrim, kBrass);
    LocalBox(cx - half * 0.92f, cy - half * 0.92f, topZ - 0.06f, cx + half * 0.92f, cy + half * 0.92f, topZ, floorZ, kMatPlinthTop, kSlate);
}

void AudioKioskPedestal(float cx, float cy, float floorZ) {
    LocalBox(cx - 0.44f, cy - 0.36f, 0.02f, cx + 0.44f, cy + 0.36f, 0.16f, floorZ, kMatPierBase, kSlate);
    LocalBox(cx - 0.32f, cy - 0.24f, 0.16f, cx + 0.32f, cy + 0.24f, 1.08f, floorZ, kMatStand, kSlate);
    LocalBox(cx - 0.42f, cy - 0.32f, 1.08f, cx + 0.42f, cy + 0.32f, 1.24f, floorZ, kMatTrim, kBrass);
    LocalBox(cx - 0.28f, cy - 0.20f, 1.24f, cx + 0.28f, cy + 0.20f, 1.32f, floorZ, kMatNumeral, kLight);
}

void PortalFrameX(float y, float x0, float x1, float clearH, float wallH, float floorZ, Material accentMat) {
    const float w = CeMuseumExhibition::kWallHalf;
    // Header lintel above portal + accent frieze + dressed stone jambs
    LocalBox(x0, y - w, clearH, x1, y + w, wallH, floorZ, kMatHeader, 0xffede9e0);
    LocalBox(x0, y - w - 0.06f, clearH, x1, y + w + 0.06f, clearH + 0.42f, floorZ, accentMat, kBrass);
    DressedColumn(x0 - 0.42f, y, 0.42f, wallH, floorZ);
    DressedColumn(x1 + 0.42f, y, 0.42f, wallH, floorZ);
}

void PortalFrameY(float x, float y0, float y1, float clearH, float wallH, float floorZ, Material accentMat) {
    const float w = CeMuseumExhibition::kWallHalf;
    LocalBox(x - w, y0, clearH, x + w, y1, wallH, floorZ, kMatHeader, 0xffede9e0);
    LocalBox(x - w - 0.06f, y0, clearH, x + w + 0.06f, y1, clearH + 0.42f, floorZ, accentMat, kBrass);
    DressedColumn(x, y0 - 0.42f, 0.42f, wallH, floorZ);
    DressedColumn(x, y1 + 0.42f, 0.42f, wallH, floorZ);
}

void BuildGrandComplex(float floorZ) {
    using namespace CeMuseumExhibition::Grand;
    const float w = CeMuseumExhibition::kWallHalf;

    // =========================================================================
    // 1. OUTDOOR ARRIVAL COURTYARD (x [-15.5, 15.5], y [-6.0, 20.0], OPEN SKY)
    // =========================================================================
    LocalFloorGrid(-15.5f, -6.0f, 15.5f, 20.0f, 0.02f, floorZ, kMatCourtyard, 4.0f);
    // Coastal cliff foundation apron under courtyard so looking over balustrade reads as elevated stone terrace
    LocalBox(-16.2f, -6.7f, -10.0f, 16.2f, -5.8f, 0.02f, floorZ, kMatPierBase, kSlate);
    LocalBox(-16.2f, -6.7f, -10.0f, -15.3f, 20.2f, 0.02f, floorZ, kMatPierBase, kSlate);
    LocalBox( 15.3f, -6.7f, -10.0f,  16.2f, 20.2f, 0.02f, floorZ, kMatPierBase, kSlate);

    // Ceremonial processional stone spine + non-overlapping perimeter loop around Hero Monument
    RunnerStripY(0.0f, -5.2f, 5.10f, 1.35f, floorZ);
    RunnerStripX(-5.55f, 5.55f, 6.20f, 1.00f, floorZ);
    RunnerStripY(-4.60f, 7.30f, 13.70f, 0.90f, floorZ);
    RunnerStripY( 4.60f, 7.30f, 13.70f, 0.90f, floorZ);
    RunnerStripX(-5.55f, 5.55f, 14.80f, 1.00f, floorZ);
    RunnerStripY(0.0f, 15.90f, 20.00f, 1.55f, floorZ);

    // Coastal stone balustrade (low knee-wall + brass top rail + rhythmic piers — wide open sea vista!)
    auto balustradeY = [&](float x, float y0, float y1) {
        LocalBox(x - 0.28f, y0, 0.02f, x + 0.28f, y1, 0.56f, floorZ, kMatWall, 0xffe4e0d8);
        LocalBox(x - 0.34f, y0, 0.56f, x + 0.34f, y1, 0.68f, floorZ, kMatPierCap, 0xfff2eee5);
        LocalBox(x - 0.14f, y0, 1.06f, x + 0.14f, y1, 1.18f, floorZ, kMatTrim, kBrass);
        for (float y = y0; y <= y1 + 0.1f; y += 4.3f) {
            LocalBox(x - 0.34f, y - 0.24f, 0.02f, x + 0.34f, y + 0.24f, 1.24f, floorZ, kMatPier, kSlate);
            LocalBox(x - 0.38f, y - 0.28f, 1.24f, x + 0.38f, y + 0.28f, 1.34f, floorZ, kMatPierCap, 0xfff2eee5);
        }
    };
    auto balustradeX = [&](float y, float x0, float x1) {
        LocalBox(x0, y - 0.28f, 0.02f, x1, y + 0.28f, 0.56f, floorZ, kMatWall, 0xffe4e0d8);
        LocalBox(x0, y - 0.34f, 0.56f, x1, y + 0.34f, 0.68f, floorZ, kMatPierCap, 0xfff2eee5);
        LocalBox(x0, y - 0.14f, 1.06f, x1, y + 0.14f, 1.18f, floorZ, kMatTrim, kBrass);
        for (float x = x0; x <= x1 + 0.1f; x += 4.0f) {
            LocalBox(x - 0.24f, y - 0.34f, 0.02f, x + 0.24f, y + 0.34f, 1.24f, floorZ, kMatPier, kSlate);
            LocalBox(x - 0.28f, y - 0.38f, 1.24f, x + 0.28f, y + 0.38f, 1.34f, floorZ, kMatPierCap, 0xfff2eee5);
        }
    };
    balustradeY(-15.5f, -6.0f, 9.7f);
    balustradeY( 15.5f, -6.0f, 9.7f);
    balustradeX( -6.0f, -15.5f, -3.5f);
    balustradeX( -6.0f,   3.5f, 15.5f);

    // Return Arch behind visitor arrival (y = -5.4)
    DressedColumn(-3.10f, -5.4f, 0.45f, 4.6f, floorZ);
    DressedColumn( 3.10f, -5.4f, 0.45f, 4.6f, floorZ);
    LocalBox(-3.65f, -5.85f, 4.60f, 3.65f, -4.95f, 5.25f, floorZ, kMatHeader, 0xfff2eee5);
    LocalBox(-3.68f, -5.90f, 4.82f, 3.68f, -4.90f, 5.00f, floorZ, kMatTrim, kBrass);

    // Flanking calm dark-water reflecting pools (East & West of courtyard)
    for (int side = -1; side <= 1; side += 2) {
        const float px0 = side < 0 ? -13.2f : 7.2f;
        const float px1 = side < 0 ?  -7.2f : 13.2f;
        const float py0 = 4.5f, py1 = 15.0f;
        LocalBox(px0, py0, 0.02f, px1, py0 + 0.42f, 0.42f, floorZ, kMatWall, 0xffe4e0d8);
        LocalBox(px0, py1 - 0.42f, 0.02f, px1, py1, 0.42f, floorZ, kMatWall, 0xffe4e0d8);
        LocalBox(px0, py0 + 0.42f, 0.02f, px0 + 0.42f, py1 - 0.42f, 0.42f, floorZ, kMatWall, 0xffe4e0d8);
        LocalBox(px1 - 0.42f, py0 + 0.42f, 0.02f, px1, py1 - 0.42f, 0.42f, floorZ, kMatWall, 0xffe4e0d8);
        LocalBox(px0 + 0.42f, py0 + 0.42f, 0.02f, px1 - 0.42f, py1 - 0.42f, 0.30f, floorZ, kMatWater, 0xff224c64);
    }

    // Hero Battle Engine Aquila Monument Plinth at (0, 10.5)
    TurntablePlinth(0.0f, 10.5f, 2.50f, 1.35f, floorZ, true);

    // Grand Museum South Colonnaded Facade (y = 20.0, height 13.5 m)
    LocalBox(-16.3f, 20.0f - w, 0.0f, -5.5f, 20.0f + w, 13.5f, floorZ, kMatWall, 0xfff5f1e8);
    LocalBox(  5.5f, 20.0f - w, 0.0f, 16.3f, 20.0f + w, 13.5f, floorZ, kMatWall, 0xfff5f1e8);
    PortalFrameX(20.0f, -5.5f, 5.5f, 7.5f, 13.5f, floorZ, kMatTrim);
    // Classical cornice + brass entablature band + decorative facade pilasters
    LocalBox(-16.5f, 19.3f, 12.7f, 16.5f, 20.5f, 13.6f, floorZ, kMatPierCap, 0xfff8f4eb);
    LocalBox(-16.4f, 19.55f, 7.55f, -5.5f, 19.8f, 8.05f, floorZ, kMatTrim, kBrass);
    LocalBox(  5.5f, 19.55f, 7.55f, 16.4f, 19.8f, 8.05f, floorZ, kMatTrim, kBrass);
    DressedColumn(-11.2f, 19.6f, 0.42f, 12.7f, floorZ);
    DressedColumn( 11.2f, 19.6f, 0.42f, 12.7f, floorZ);

    // =========================================================================
    // 2. SKYLIT GRAND ATRIUM (x [-16, 16], y [20, 52], height 13.5 m, OPEN SKYLIGHT)
    // =========================================================================
    LocalFloorGrid(-16.0f, 20.0f, 16.0f, 52.0f, 0.02f, floorZ, kMatFloor, 4.0f);

    // Central Brass Compass Rose Floor Inlay at (0, 36.0) + radial wing runners
    LocalFloorGrid(-4.4f, 31.6f, 4.4f, 40.4f, 0.055f, floorZ, kMatInlay, 2.2f, 0xffd0d6dc);
    LocalFloorGrid(-4.7f, 31.3f, 4.7f, 31.6f, 0.068f, floorZ, kMatTrim, 4.0f, kBrass);
    LocalFloorGrid(-4.7f, 40.4f, 4.7f, 40.7f, 0.068f, floorZ, kMatTrim, 4.0f, kBrass);
    LocalFloorGrid(-4.7f, 31.6f, -4.4f, 40.4f, 0.068f, floorZ, kMatTrim, 4.0f, kBrass);
    LocalFloorGrid( 4.4f, 31.6f,  4.7f, 40.4f, 0.068f, floorZ, kMatTrim, 4.0f, kBrass);
    // Cardinal brass compass crosshairs inside the medallion
    LocalFloorGrid(-4.2f, 35.75f, 4.2f, 36.25f, 0.075f, floorZ, kMatTrim, 4.0f, kBrass);
    LocalFloorGrid(-0.25f, 31.8f, 0.25f, 40.2f, 0.075f, floorZ, kMatTrim, 4.0f, kBrass);
    LocalFloorGrid(-1.1f, 34.9f, 1.1f, 37.1f, 0.082f, floorZ, kMatNumeral, 2.2f, kLight);

    // Floor runners connecting Compass Rose to ALL 6 Atrium portals (clean non-overlapping joins)
    RunnerStripY(0.0f, 20.0f, 31.3f, 1.35f, floorZ);        // South -> Colonnade
    RunnerStripY(0.0f, 40.7f, 52.0f, 1.35f, floorZ);        // North -> Hangar
    RunnerStripX(-16.0f, -4.7f, 41.0f, 1.15f, floorZ);      // West -> Forseti
    RunnerStripX(  4.7f, 16.0f, 41.0f, 1.15f, floorZ);      // East -> Muspell
    RunnerStripX(-16.0f, -1.45f, 24.0f, 1.00f, floorZ);     // Southwest -> Early Designs Vault
    RunnerStripX( 1.45f, 16.0f, 24.0f, 1.00f, floorZ);      // Southeast -> Concept Art Gallery

    // Central Directory Lectern at (0, 31.5)
    LocalBox(-1.60f, 31.20f, 0.02f, 1.60f, 31.80f, 0.20f, floorZ, kMatPierBase, kSlate);
    LocalBox(-1.50f, 31.26f, 0.20f, 1.50f, 31.74f, 1.02f, floorZ, kMatStand, kSlate);
    LocalBox(-1.62f, 31.18f, 1.02f, 1.62f, 31.82f, 1.16f, floorZ, kMatTrim, kBrass);

    // Four monumental clerestory skylight columns
    DressedColumn(-8.2f, 28.2f, 0.52f, 13.5f, floorZ);
    DressedColumn( 8.2f, 28.2f, 0.52f, 13.5f, floorZ);
    DressedColumn(-8.2f, 43.8f, 0.52f, 13.5f, floorZ);
    DressedColumn( 8.2f, 43.8f, 0.52f, 13.5f, floorZ);

    // Atrium Perimeter Ceiling with HUGE 16m x 16m Open Skylit Clerestory Lantern (x [-8,+8], y [28,44])
    Ceiling(floorZ, -16.3f, 20.0f,  -8.0f, 52.3f, 13.5f, kMatCeiling);
    Ceiling(floorZ,   8.0f, 20.0f,  16.3f, 52.3f, 13.5f, kMatCeiling);
    Ceiling(floorZ,  -8.0f, 20.0f,   8.0f, 28.0f, 13.5f, kMatCeiling);
    Ceiling(floorZ,  -8.0f, 44.0f,   8.0f, 52.3f, 13.5f, kMatCeiling);
    // Clerestory lantern curb + slender architectural cross-beams framing open blue daylight overhead
    LocalBox(-8.2f, 27.8f, 13.1f,  8.2f, 28.2f, 14.5f, floorZ, kMatPierCap, 0xfff2eee5);
    LocalBox(-8.2f, 43.8f, 13.1f,  8.2f, 44.2f, 14.5f, floorZ, kMatPierCap, 0xfff2eee5);
    LocalBox(-8.2f, 28.2f, 13.1f, -7.8f, 43.8f, 14.5f, floorZ, kMatPierCap, 0xfff2eee5);
    LocalBox( 7.8f, 28.2f, 13.1f,  8.2f, 43.8f, 14.5f, floorZ, kMatPierCap, 0xfff2eee5);
    LocalBox(-8.0f, 35.8f, 13.5f,  8.0f, 36.2f, 14.1f, floorZ, kMatTrim, kBrass);
    LocalBox(-0.2f, 28.0f, 13.5f,  0.2f, 44.0f, 14.1f, floorZ, kMatTrim, kBrass);

    // Atrium West Wall (x = -16.0): portals to Vault [21.5..26.5] and Forseti [38.0..44.0]
    LocalBox(-16.0f - w, 20.0f, 0.0f, -16.0f + w, 21.5f, 13.5f, floorZ, kMatWall);
    PortalFrameY(-16.0f, 21.5f, 26.5f, 5.0f, 13.5f, floorZ, kMatTrim);
    LocalBox(-16.0f - w, 26.5f, 0.0f, -16.0f + w, 38.0f, 13.5f, floorZ, kMatWall);
    PortalFrameY(-16.0f, 38.0f, 44.0f, 6.0f, 13.5f, floorZ, kMatForsetiTrim);
    LocalBox(-16.0f - w, 44.0f, 0.0f, -16.0f + w, 52.3f, 13.5f, floorZ, kMatWall);

    // Atrium East Wall (x = +16.0): portals to Concept Art [21.5..26.5] and Muspell [38.0..44.0]
    LocalBox(16.0f - w, 20.0f, 0.0f, 16.0f + w, 21.5f, 13.5f, floorZ, kMatWall);
    PortalFrameY(16.0f, 21.5f, 26.5f, 5.0f, 13.5f, floorZ, kMatTrim);
    LocalBox(16.0f - w, 26.5f, 0.0f, 16.0f + w, 38.0f, 13.5f, floorZ, kMatWall);
    PortalFrameY(16.0f, 38.0f, 44.0f, 6.0f, 13.5f, floorZ, kMatMuspellTrim);
    LocalBox(16.0f - w, 44.0f, 0.0f, 16.0f + w, 52.3f, 13.5f, floorZ, kMatWall);

    // Atrium North Wall (y = 52.0): grand 10 m arch to Aviation & Naval Hangar [-5.0..+5.0]
    LocalBox(-20.3f, 52.0f - w, 0.0f, -5.0f, 52.0f + w, 15.5f, floorZ, kMatWall);
    PortalFrameX(52.0f, -5.0f, 5.0f, 8.0f, 15.5f, floorZ, kMatTrim);
    LocalBox(  5.0f, 52.0f - w, 0.0f, 20.3f, 52.0f + w, 15.5f, floorZ, kMatWall);

    // =========================================================================
    // 3. FORSETI FORCES WING (West: x [-44, -16], y [30, 52], height 10.5 m)
    // =========================================================================
    LocalFloorGrid(-44.0f, 30.0f, -16.0f, 52.0f, 0.02f, floorZ, kMatFloor, 4.0f);
    RunnerStripX(-40.0f, -16.0f, 41.0f, 1.15f, floorZ);
    LocalBox(-44.3f, 30.0f - w, 0.0f, -16.0f, 30.0f + w, 10.5f, floorZ, kMatWall);
    LocalBox(-44.3f, 52.0f - w, 0.0f, -16.0f, 52.0f + w, 10.5f, floorZ, kMatWall);
    LocalBox(-44.0f - w, 29.7f, 0.0f, -44.0f + w, 52.3f, 10.5f, floorZ, kMatWall);
    // Cool Forseti steel-blue horizontal frieze & skirting accents
    LocalBox(-43.6f, 30.32f, 6.4f, -16.4f, 30.48f, 7.15f, floorZ, kMatForsetiTrim);
    LocalBox(-43.6f, 51.52f, 6.4f, -16.4f, 51.68f, 7.15f, floorZ, kMatForsetiTrim);
    LocalBox(-43.68f, 30.4f, 6.4f, -43.52f, 51.6f, 7.15f, floorZ, kMatForsetiTrim);
    LocalBox(-43.6f, 30.32f, 0.02f, -16.4f, 30.45f, 0.42f, floorZ, kMatForsetiTrim);
    LocalBox(-43.6f, 51.55f, 0.02f, -16.4f, 51.68f, 0.42f, floorZ, kMatForsetiTrim);
    for (float x = -39.5f; x <= -18.5f; x += 7.0f) {
        DressedColumn(x, 30.55f, 0.34f, 10.5f, floorZ);
        DressedColumn(x, 51.45f, 0.34f, 10.5f, floorZ);
    }
    Ceiling(floorZ, -44.3f, 29.7f, -15.7f, 52.3f, 10.5f, kMatCeiling);
    // Six native bay plinths (+ sleek flight post for any banked aircraft bay raised by RaiseOnPost)
    for (unsigned i = 0; i < kNativeBayCount; ++i) {
        const NativeBaySpot &nb = gGrandGraph.nativeBays[i];
        TurntablePlinth(nb.x, nb.y, nb.halfSize, nb.topZ, floorZ, false);
        float postLift = 0.0f;
        const CeMuseumExhibition::Bay *bay = CeMuseumExhibitionHost::InputBay(gExhibition, i);
        if (bay) {
            for (unsigned b = 0; b < gExhibition.boxCount; ++b) {
                const CeMuseumExhibition::Box &bx = gExhibition.boxes[b];
                if (bx.purpose == CeMuseumExhibition::kSolidStand && bx.bay == bay->number &&
                    (bx.max.x - bx.min.x) <= 0.20f) {
                    postLift = bx.max.z - bx.min.z;
                    break;
                }
            }
        }
        if (postLift > 0.05f) {
            const float postTop = nb.topZ + postLift;
            LocalBox(nb.x - 0.14f, nb.y - 0.14f, nb.topZ, nb.x + 0.14f, nb.y + 0.14f, postTop - 0.06f,
                     floorZ, kMatStand, kSlate);
            LocalBox(nb.x - 0.18f, nb.y - 0.18f, postTop - 0.12f, nb.x + 0.18f, nb.y + 0.18f, postTop - 0.02f,
                     floorZ, kMatTrim, kBrass);
        }
    }
    TurntablePlinth(-40.8f, 37.8f, 1.45f, 0.90f, floorZ, false);
    TurntablePlinth(-40.8f, 44.2f, 1.45f, 0.90f, floorZ, false);
    AudioKioskPedestal(-18.5f, 34.5f, floorZ);

    // =========================================================================
    // 4. MUSPELL DOMINION WING (East: x [16, 44], y [30, 52], height 10.5 m)
    // =========================================================================
    LocalFloorGrid(16.0f, 30.0f, 44.0f, 52.0f, 0.02f, floorZ, kMatFloor, 4.0f);
    RunnerStripX(16.0f, 41.0f, 41.0f, 1.15f, floorZ);
    LocalBox(16.0f, 30.0f - w, 0.0f, 44.3f, 30.0f + w, 10.5f, floorZ, kMatWall);
    LocalBox(16.0f, 52.0f - w, 0.0f, 44.3f, 52.0f + w, 10.5f, floorZ, kMatWall);
    LocalBox(44.0f - w, 29.7f, 0.0f, 44.0f + w, 52.3f, 10.5f, floorZ, kMatWall);
    // Warm Muspell iron-amber horizontal frieze & skirting accents
    LocalBox(16.4f, 30.32f, 6.4f, 43.6f, 30.48f, 7.15f, floorZ, kMatMuspellTrim);
    LocalBox(16.4f, 51.52f, 6.4f, 43.6f, 51.68f, 7.15f, floorZ, kMatMuspellTrim);
    LocalBox(43.52f, 30.4f, 6.4f, 43.68f, 51.6f, 7.15f, floorZ, kMatMuspellTrim);
    LocalBox(16.4f, 30.32f, 0.02f, 43.6f, 30.45f, 0.42f, floorZ, kMatMuspellTrim);
    LocalBox(16.4f, 51.55f, 0.02f, 43.6f, 51.68f, 0.42f, floorZ, kMatMuspellTrim);
    for (float x = 18.5f; x <= 39.5f; x += 7.0f) {
        DressedColumn(x, 30.55f, 0.34f, 10.5f, floorZ);
        DressedColumn(x, 51.45f, 0.34f, 10.5f, floorZ);
    }
    Ceiling(floorZ, 15.7f, 29.7f, 44.3f, 52.3f, 10.5f, kMatCeiling);
    // Six Muspell rotating turntable plinths + Audio Kiosk
    const float mx[3] = {22.0f, 30.0f, 38.0f};
    for (unsigned i = 0; i < 3; ++i) {
        TurntablePlinth(mx[i], 47.8f, 1.85f, 0.90f, floorZ, false);
        TurntablePlinth(mx[i], 34.2f, 1.85f, 0.90f, floorZ, false);
    }
    AudioKioskPedestal(18.5f, 34.5f, floorZ);

    // =========================================================================
    // 5. AVIATION & NAVAL HANGAR (North High-Bay: x [-20, 20], y [52, 78], 15.5 m)
    // =========================================================================
    LocalFloorGrid(-20.0f, 52.0f, 20.0f, 78.0f, 0.02f, floorZ, kMatFloor, 4.0f);
    RunnerStripY(0.0f, 52.0f, 73.2f, 1.45f, floorZ);
    RunnerStripX(-10.5f, -1.55f, 65.0f, 1.05f, floorZ);
    RunnerStripX(  1.55f,  7.6f, 65.0f, 1.05f, floorZ);
    LocalBox(-20.0f - w, 52.0f, 0.0f, -20.0f + w, 78.3f, 15.5f, floorZ, kMatWall);
    LocalBox( 20.0f - w, 52.0f, 0.0f,  20.0f + w, 78.3f, 15.5f, floorZ, kMatWall);
    LocalBox(-20.3f, 78.0f - w, 0.0f,  20.3f, 78.0f + w, 15.5f, floorZ, kMatWall);
    // Twin Forseti/Muspell high-bay accent stripes + heavy vaulted slate roof trusses
    LocalBox(-19.65f, 52.5f, 9.6f, -19.50f, 77.5f, 10.3f, floorZ, kMatForsetiTrim);
    LocalBox( 19.50f, 52.5f, 9.6f,  19.65f, 77.5f, 10.3f, floorZ, kMatMuspellTrim);
    for (float y = 56.0f; y <= 75.0f; y += 6.0f) {
        DressedColumn(-19.45f, y, 0.42f, 15.5f, floorZ);
        DressedColumn( 19.45f, y, 0.42f, 15.5f, floorZ);
        // Overhead high-bay truss beam
        LocalBox(-19.5f, y - 0.30f, 13.8f, 19.5f, y + 0.30f, 15.2f, floorZ, kMatPier, kSlate);
        LocalBox(-19.5f, y - 0.34f, 13.72f, 19.5f, y + 0.34f, 13.86f, floorZ, kMatTrim, kBrass);
    }
    Ceiling(floorZ, -20.3f, 51.7f, 20.3f, 78.3f, 15.5f, kMatCeiling);

    // West Aviation Bay: 3 plinths + tall sleek flight pylons supporting banked fighters/bomber
    const float ay[3] = {58.0f, 65.0f, 72.0f};
    const float aTop[3] = {2.75f, 3.05f, 2.75f};
    for (unsigned i = 0; i < 3; ++i) {
        TurntablePlinth(-14.5f, ay[i], 1.75f, 0.80f, floorZ, false);
        LocalBox(-14.70f, ay[i] - 0.20f, 0.80f, -14.30f, ay[i] + 0.20f, aTop[i] - 0.10f, floorZ, kMatStand, kSlate);
        LocalBox(-14.76f, ay[i] - 0.26f, aTop[i] - 0.22f, -14.24f, ay[i] + 0.26f, aTop[i] - 0.08f, floorZ, kMatTrim, kBrass);
    }

    // East Naval Bay: recessed reflecting-water basin (x [8.8, 17.4], y [55.6, 74.4]) + 3 submerged piers
    LocalBox( 8.8f, 55.6f, 0.02f, 17.4f, 56.05f, 0.44f, floorZ, kMatWall);
    LocalBox( 8.8f, 73.95f, 0.02f, 17.4f, 74.4f, 0.44f, floorZ, kMatWall);
    LocalBox( 8.8f, 56.05f, 0.02f,  9.25f, 73.95f, 0.44f, floorZ, kMatWall);
    LocalBox(16.95f, 56.05f, 0.02f, 17.4f, 73.95f, 0.44f, floorZ, kMatWall);
    LocalBox( 9.25f, 56.05f, 0.02f, 16.95f, 73.95f, 0.28f, floorZ, kMatWater, 0xff204c66);
    const float ny[3] = {58.5f, 65.0f, 71.5f};
    for (unsigned i = 0; i < 3; ++i) {
        LocalBox(11.3f, ny[i] - 1.3f, 0.28f, 14.7f, ny[i] + 1.3f, 0.65f, floorZ, kMatPierBase, kSlate);
        LocalBox(11.26f, ny[i] - 1.34f, 0.58f, 14.74f, ny[i] + 1.34f, 0.65f, floorZ, kMatTrim, kBrass);
    }

    // North Relative Scale Alcove (calibrated elevation wall at y = 77.5 + long comparison plinth)
    LocalBox(-6.6f, 77.35f, 0.02f, 6.6f, 77.68f, 11.2f, floorZ, kMatPierBase, kSlate);
    for (int m = 2; m <= 10; m += 2) {
        const float hz = float(m);
        LocalBox(-6.3f, 77.26f, hz - 0.05f, 6.3f, 77.38f, hz + 0.05f, floorZ, kMatNumeral, kLight);
    }
    LocalBox(-5.4f, 73.9f, 0.02f, 5.4f, 76.7f, 0.55f, floorZ, kMatPlinth, 0xffdde2e6);
    LocalBox(-5.44f, 73.86f, 0.48f, 5.44f, 76.74f, 0.55f, floorZ, kMatTrim, kBrass);
    AudioKioskPedestal(-7.5f, 55.5f, floorZ);

    // =========================================================================
    // 6. CONCEPT ART GALLERY (Southeast: x [16, 40], y [10, 28], height 8.5 m)
    // =========================================================================
    LocalFloorGrid(16.0f, 10.0f, 40.0f, 28.0f, 0.02f, floorZ, kMatFloor, 4.0f);
    RunnerStripX(16.0f, 32.4f, 24.0f, 1.00f, floorZ);
    RunnerStripX(34.6f, 40.0f, 14.5f, 1.15f, floorZ);
    RunnerStripY(33.5f, 13.35f, 25.10f, 1.00f, floorZ);
    // West facade wall sealing Concept Art Gallery from the Outdoor Courtyard (x = 16, y [9.7..20.0])
    LocalBox(16.0f - w, 9.7f, 0.0f, 16.0f + w, 20.0f, 8.5f, floorZ, kMatWall, 0xfff3efe6);
    LocalBox(15.40f, 9.5f, 8.05f, 16.35f, 20.1f, 8.65f, floorZ, kMatPierCap, 0xfff8f4eb);
    LocalBox(15.52f, 9.7f, 5.20f, 15.68f, 20.0f, 5.55f, floorZ, kMatTrim, kBrass);
    DressedColumn(15.62f, 10.1f, 0.34f, 8.05f, floorZ);
    DressedColumn(15.62f, 15.0f, 0.34f, 8.05f, floorZ);
    LocalBox(16.0f, 10.0f - w, 0.0f, 40.3f, 10.0f + w, 8.5f, floorZ, kMatWall);
    LocalBox(16.0f, 28.0f - w, 0.0f, 40.3f, 28.0f + w, 8.5f, floorZ, kMatWall);
    // East wall shared with Screening Room (full 9.5 m height to meet Screening Room ceiling cleanly): portal y [11.7..17.3]
    LocalBox(40.0f - w, 7.0f, 0.0f, 40.0f + w, 11.7f, 9.5f, floorZ, kMatWall);
    PortalFrameY(40.0f, 11.7f, 17.3f, 4.6f, 9.5f, floorZ, kMatTrim);
    LocalBox(40.0f - w, 17.3f, 0.0f, 40.0f + w, 29.0f, 9.5f, floorZ, kMatWall);
    // Brushed-brass picture rail & dado moulding along North & South art walls
    LocalBox(16.4f, 27.60f, 4.65f, 39.6f, 27.72f, 4.78f, floorZ, kMatTrim, kBrass);
    LocalBox(16.4f, 10.28f, 4.65f, 39.6f, 10.40f, 4.78f, floorZ, kMatTrim, kBrass);
    LocalBox(16.4f, 27.62f, 0.02f, 39.6f, 27.72f, 0.36f, floorZ, kMatPierBase, kSlate);
    LocalBox(16.4f, 10.28f, 0.02f, 39.6f, 10.38f, 0.36f, floorZ, kMatPierBase, kSlate);
    // Central gallery viewing bench + Audio Kiosk
    LocalBox(24.0f, 18.4f, 0.02f, 32.0f, 19.6f, 0.42f, floorZ, kMatPierBase, kSlate);
    LocalBox(23.9f, 18.35f, 0.42f, 32.1f, 19.65f, 0.54f, floorZ, kMatPlinth, 0xffe6e4dc);
    AudioKioskPedestal(19.0f, 15.0f, floorZ);
    Ceiling(floorZ, 15.7f, 9.7f, 40.0f, 28.3f, 8.5f, kMatCeiling);

    // =========================================================================
    // 7. EARLY DESIGNS VAULT (Southwest: x [-40, -16], y [10, 28], height 8.5 m)
    // =========================================================================
    LocalFloorGrid(-40.0f, 10.0f, -16.0f, 28.0f, 0.02f, floorZ, kMatFloor, 4.0f);
    RunnerStripX(-36.0f, -16.0f, 24.0f, 1.00f, floorZ);
    // East facade wall sealing Early Designs Vault from the Outdoor Courtyard (x = -16, y [9.7..20.0])
    LocalBox(-16.0f - w, 9.7f, 0.0f, -16.0f + w, 20.0f, 8.5f, floorZ, kMatWall, 0xfff3efe6);
    LocalBox(-16.35f, 9.5f, 8.05f, -15.40f, 20.1f, 8.65f, floorZ, kMatPierCap, 0xfff8f4eb);
    LocalBox(-15.68f, 9.7f, 5.20f, -15.52f, 20.0f, 5.55f, floorZ, kMatTrim, kBrass);
    DressedColumn(-15.62f, 10.1f, 0.34f, 8.05f, floorZ);
    DressedColumn(-15.62f, 15.0f, 0.34f, 8.05f, floorZ);
    LocalBox(-40.3f, 10.0f - w, 0.0f, -16.0f, 10.0f + w, 8.5f, floorZ, kMatWall);
    LocalBox(-40.3f, 28.0f - w, 0.0f, -16.0f, 28.0f + w, 8.5f, floorZ, kMatWall);
    LocalBox(-40.0f - w, 9.7f, 0.0f, -40.0f + w, 28.3f, 8.5f, floorZ, kMatWall);
    LocalBox(-39.6f, 27.60f, 4.65f, -16.4f, 27.72f, 4.78f, floorZ, kMatTrim, kBrass);
    LocalBox(-39.6f, 10.28f, 4.65f, -16.4f, 10.40f, 4.78f, floorZ, kMatTrim, kBrass);
    LocalBox(-39.6f, 27.62f, 0.02f, -16.4f, 27.72f, 0.36f, floorZ, kMatPierBase, kSlate);
    LocalBox(-39.6f, 10.28f, 0.02f, -16.4f, 10.38f, 0.36f, floorZ, kMatPierBase, kSlate);
    // Central archival viewing bench + Audio Kiosk
    LocalBox(-32.0f, 18.4f, 0.02f, -24.0f, 19.6f, 0.42f, floorZ, kMatPierBase, kSlate);
    LocalBox(-32.1f, 18.35f, 0.42f, -23.9f, 19.65f, 0.54f, floorZ, kMatPlinth, 0xffe6e4dc);
    AudioKioskPedestal(-19.0f, 15.0f, floorZ);
    Ceiling(floorZ, -40.3f, 9.7f, -15.7f, 28.3f, 8.5f, kMatCeiling);
}

void DrawDirectoryButtons(IDirect3DDevice9 *dev, float floorZ) {
    if (!gGrandMode || !dev) return;
    if (gCurrentWing != CeMuseumTour::kWingAtrium && gCurrentWing != CeMuseumTour::kWingCourtyard) return;
    Vertex verts[CeMuseumTour::kDirectoryStops * 6];
    unsigned used = 0;
    const float wy = CeMuseumWorld::kEntranceY + CeMuseumTour::kDirectoryY - 0.18f;
    const float wz = floorZ - CeMuseumTour::kDirectoryZ;
    for (int i = 0; i < CeMuseumTour::kDirectoryStops; ++i) {
        const float wx = CeMuseumWorld::kEntranceX + CeMuseumTour::kDirectoryX + CeMuseumTour::kDirectory[i].localOffsetX;
        const bool hovered = gDirHit.valid && gDirHit.index == i;
        const DWORD col = hovered ? 0xffffe580 : 0xffb8944c;
        const float hw = 0.18f, hh = 0.09f;
        const Vertex a = {wx - hw, wy, wz - hh, col};
        const Vertex b = {wx + hw, wy, wz - hh, col};
        const Vertex c = {wx + hw, wy, wz + hh, col};
        const Vertex d = {wx - hw, wy, wz + hh, col};
        verts[used++] = a; verts[used++] = b; verts[used++] = c;
        verts[used++] = a; verts[used++] = c; verts[used++] = d;
    }
    if (used) {
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        dev->SetTexture(0, 0);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, used / 3, verts, sizeof(Vertex));
    }
}

} // namespace

static void FindMaterials() {
    CeMuseumTextures::LoadAll(LT.GetDevice());
    for (unsigned m = 0; m < kMatCount; ++m) {
        const CeMuseumTexId tid = kMaterials[m].tex;
        g.texture[m] = tid < CE_MUSEUM_TEX_COUNT ? CeMuseumTextures::Get(tid) : NULL;
    }
}

static void ReleaseMaterials() {
    for (unsigned m = 0; m < kMatCount; ++m) g.texture[m] = NULL;
    CeMuseumTextures::ReleaseAll();
}

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

static int ExhibitionZone(float x, float y, int previous) {
    const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
    const int number = CeMuseumExhibition::StandingBay(gExhibition, local, previous > 0 ? previous : 0);
    if (number > 0) return number;
    const CeMuseumExhibition::Zone* arrival = CeMuseumExhibition::ZoneFor(gExhibition, CeMuseumExhibition::kZoneArrival);
    return arrival && local.x >= arrival->min.x && local.x <= arrival->max.x &&
           local.y >= arrival->min.y && local.y <= arrival->max.y ? 0 : -1;
}

static void HouseLightReset();
void CeMuseumHallClear() {
    gExhibition = CeMuseumExhibition::Plan{};
    gExhibitionVisitor = CeMuseumExhibition::Vec2{};
    gGrandMode = false;
    gCurrentWing = CeMuseumTour::kWingCourtyard;
    gWingAnnounced = false;
    gHasStepClock = false;
    gDirHit = {false, -1};
    gDirFireHeld = false;
    gDirFireCaptured = false;
    gPendingJump = false;
    CeMuseumTurntable::RuntimeShutdown();
    CeMuseumArt::RuntimeShutdown();
    CeMuseumAudioKiosk::RuntimeShutdown();
    ReleaseMaterials();
    HouseLightReset();
    CinemaReset();
    g.valid=false; g.used=0; g.zone=-1; g.ground=0; g.current=kMatAuto; g.hasCentre=false; g.overflowReason=0;
}

static void HallFailed(const char *why) {
    PortSessionLog(PORT_SESSION_GAME, false, "museum: the hall was not built: %s", why);
}

static void SetHall(const CeHallTour &tour, CeHallGround ground, const CeMuseumExhibition::Plan* exhibition) {
    CeMuseumHallClear();
    if (!ground) { HallFailed("no ground lookup"); return; }
    if (!tour.stops || tour.stops > 3) { HallFailed("the tour has no stops or too many"); return; }
    g.tour=tour; g.ground=ground; g.overflow=false; g.zone=0;
    FindMaterials();
    if (exhibition) {
        if (!exhibition->valid) { HallFailed("the exhibition plan is not valid"); ReleaseMaterials(); return; }
        float floor = 0.0f;
        if (!g.ground(CeMuseumWorld::kEntranceX, CeMuseumWorld::kEntranceY, &floor)) {
            HallFailed("no ground under the entrance"); ReleaseMaterials(); return;
        }
        gExhibition = *exhibition;
        g.zone = -1;
        gGrandFloorZ = floor;
        if (PortCeMuseumAuthoredMap()) {
            gGrandMode = true;
            gGrandGraph = CeMuseumExhibition::Grand::MakeRoomGraph();
            BuildGrandComplex(floor);
            CinemaShell(true, floor, 9.5f);
            CeMuseumTurntable::RuntimeInit();
            CeMuseumArt::RuntimeInit();
            CeMuseumAudioKiosk::RuntimeInit();
        } else {
            ExhibitionShell();
        }
    } else {
        if (PortCeMuseumAuthoredMap()) { ExhibitionShell(); CinemaShell(false, 0, 0); }
        Walk(Offset(tour.entrance,0,-1.5f),tour.stop[0]);
        if(tour.stops >= 2) Walk(tour.stop[0],tour.stop[1]);
        if(tour.stops == 3) ExhibitRoom();
        Entry();
        for (unsigned k=0;k<tour.stops;++k) Stand(tour.stop[k],k+1);
    }
    g.valid=g.used && !g.overflow;
    if (g.valid) {
        SortMaterials();
        PortSessionLog(PORT_SESSION_GAME, false, "museum: built Grand Complex (%u vertices, %u triangles)",
                       g.used, g.used / 3u);
        return;
    }
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
    if (g.valid && gGrandMode) {
        const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
        return CeMuseumExhibition::Grand::WalkAt(gGrandGraph, local) != CeMuseumExhibition::kWalkBlocked;
    }
    if (g.valid && gExhibition.valid) {
        const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
        const CeMuseumExhibition::Walk walk = CeMuseumExhibition::WalkAt(gExhibition, local);
        if (walk == CeMuseumExhibition::kWalkOutside)
            return (CeCinemaRoom::Interior(local.x, local.y) || CeCinemaRoom::InDoor(local.x, local.y)) &&
                   !CeCinemaRoom::Seat(local.x, local.y);
        return walk != CeMuseumExhibition::kWalkBlocked;
    }
    return !g.valid || ((!PortCeMuseumAuthoredMap() || CeMuseumWorld::Inside(x,y)) && CeHallBayAllows(g.tour,{x,y}));
}

static CeMuseumTour::Wing WingFromRoom(CeMuseumExhibition::Grand::RoomId r) {
    using namespace CeMuseumExhibition::Grand;
    switch (r) {
    case kRoomCourtyard:    return CeMuseumTour::kWingCourtyard;
    case kRoomAtrium:       return CeMuseumTour::kWingAtrium;
    case kRoomForseti:      return CeMuseumTour::kWingForseti;
    case kRoomMuspell:      return CeMuseumTour::kWingMuspell;
    case kRoomHangar:       return CeMuseumTour::kWingHangar;
    case kRoomConceptArt:   return CeMuseumTour::kWingConceptArt;
    case kRoomEarlyDesigns: return CeMuseumTour::kWingEarlyDesigns;
    case kRoomCinema:       return CeMuseumTour::kWingScreeningRoom;
    default:                return gCurrentWing;
    }
}

void CeMuseumHallVisitor(float x, float y) {
    gExhibitionVisitor = CeMuseumExhibitionHost::Local(x, y);
    if (!g.valid) return;
    CinemaVisitor(x,y);

    if (gGrandMode) {
        const CeMuseumExhibition::Grand::RoomId room =
            CeMuseumExhibition::Grand::RoomAt(gGrandGraph, gExhibitionVisitor);
        const CeMuseumTour::Wing wing = WingFromRoom(room);
        const bool wingChanged = (!gWingAnnounced || wing != gCurrentWing);
        gCurrentWing = wing;
        gWingAnnounced = true;

        const double now = CinemaClock();
        float wallDt = 0.0f;
        if (gHasStepClock && now > gLastStepClock) wallDt = float(now - gLastStepClock);
        gLastStepClock = now;
        gHasStepClock = true;
        float dt = GAME.GetRenderFrameLength();
        if (!(dt > 0.0f && dt <= 0.25f)) dt = wallDt;

        // Anticipate the adjacent 3D wing when standing near an Atrium portal so open archways
        // always showcase gleaming exhibits inside while still capping residency to a single wing.
        CeMuseumTour::Wing turntableWing = wing;
        if (wing == CeMuseumTour::kWingAtrium) {
            if (gExhibitionVisitor.y >= 45.0f) turntableWing = CeMuseumTour::kWingHangar;
            else if (gExhibitionVisitor.x >= 9.5f && gExhibitionVisitor.y >= 32.0f) turntableWing = CeMuseumTour::kWingMuspell;
            else if (gExhibitionVisitor.x <= -9.5f && gExhibitionVisitor.y >= 32.0f) turntableWing = CeMuseumTour::kWingForseti;
        }

        // Convert world view ray to visitor-local coordinates (Z positive upward) for ray-pick fixtures
        float eyeX = gExhibitionVisitor.x, eyeY = gExhibitionVisitor.y, eyeZ = 1.70f;
        float aimX = 0.0f, aimY = 1.0f, aimZ = 0.0f;
        if (Cinema::s.viewValid) {
            eyeX = Cinema::s.eye[0] - CeMuseumWorld::kEntranceX;
            eyeY = Cinema::s.eye[1] - CeMuseumWorld::kEntranceY;
            eyeZ = gGrandFloorZ - Cinema::s.eye[2];
            aimX = Cinema::s.aim[0];
            aimY = Cinema::s.aim[1];
            aimZ = -Cinema::s.aim[2];
        }

        const bool paused = GAME.IsPaused();
        const bool cinemaPlaying = Cinema::s.inRoom && Cinema::s.phase == Cinema::Playing;
        CeMuseumTurntable::RuntimeUpdate(turntableWing, dt, paused, eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
        CeMuseumArt::RuntimeUpdate(wing);
        CeMuseumAudioKiosk::RuntimeUpdate(wing, paused, cinemaPlaying, eyeX, eyeY, eyeZ, aimX, aimY, aimZ);
        gDirHit = (wing == CeMuseumTour::kWingAtrium)
            ? CeMuseumTour::PickDirectory(eyeX, eyeY, eyeZ, aimX, aimY, aimZ)
            : CeMuseumTour::DirectoryHit{false, -1};

        if (wingChanged && wing != CeMuseumTour::kWingScreeningRoom) {
            const CeMuseumTour::WingMeta &meta = CeMuseumTour::Meta(wing);
            PortCeMuseumLabelsTitle(meta.title, meta.subtitle);
            PortSessionLog(PORT_SESSION_GAME, false, "museum: visitor entered %s", meta.title);
        }
        return;
    }

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
    if (g.valid && gGrandMode) {
        // In the Forseti Forces Wing, resolve close-inspection cards for the 6 native bay exhibits.
        if (gCurrentWing == CeMuseumTour::kWingForseti && gExhibition.valid) {
            const float flen = std::sqrt(forwardX * forwardX + forwardY * forwardY);
            if (flen > 1e-5f) {
                const float fx = forwardX / flen, fy = forwardY / flen;
                int best = -1;
                float bestScore = -1.0f;
                for (unsigned i = 0; i < gExhibition.bayCount && i < CeMuseumExhibition::Grand::kNativeBayCount; ++i) {
                    const CeMuseumExhibition::Grand::NativeBaySpot &nb = gGrandGraph.nativeBays[i];
                    const float dx = nb.x - gExhibitionVisitor.x, dy = nb.y - gExhibitionVisitor.y;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    if (d < 0.2f || d > 8.5f) continue;
                    const float cosA = (dx * fx + dy * fy) / d;
                    if (cosA < 0.86f) continue;
                    const float score = cosA - 0.015f * d;
                    if (score > bestScore) { bestScore = score; best = static_cast<int>(i); }
                }
                if (best >= 0) {
                    if (displayNumber) *displayNumber = best + 1;
                    return gExhibition.bays[best].stableLabel;
                }
            }
        }
        if (displayNumber) *displayNumber = 0;
        return -1;
    }
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
    if (g.valid && gGrandMode)
        return CeMuseumExhibition::Grand::FacesExit(
            CeMuseumExhibitionHost::Local(x, y), {forwardX, forwardY}, held);
    if (g.valid && gExhibition.valid)
        return CeMuseumExhibition::FacesExit(gExhibition,
            CeMuseumExhibitionHost::Local(x, y), {forwardX, forwardY}, held);
    return g.valid && CeHallFacesExit(g.tour, {x, y}, {forwardX, forwardY}, held);
}

extern "C" void PortCeMuseumCinemaView(const float *view, unsigned present) {
    CinemaCaptureView(view, present);
}

extern "C" bool PortCeMuseumCinemaFire(bool release,float value) {
    if (gGrandMode) {
        const bool cinemaPlaying = Cinema::s.inRoom && Cinema::s.phase == Cinema::Playing;
        // Route Fire cleanly to interactive museum fixtures (Projector, Directory, Turntable, Kiosk).
        if (Cinema::s.inRoom) return CinemaAction(release, value);
        if (gDirHit.valid && gDirHit.index >= 0 && gDirHit.index < CeMuseumTour::kDirectoryStops) {
            const CeCinemaRoom::FireIntent intent = CeCinemaRoom::FireEdge(
                release || value <= 0.0f, true, gDirFireHeld, gDirFireCaptured);
            if (intent == CeCinemaRoom::FireIntent::Operate) {
                const CeMuseumTour::Wing target = CeMuseumTour::kDirectory[gDirHit.index].target;
                const CeMuseumTour::WingMeta &m = CeMuseumTour::Meta(target);
                gJumpX = CeMuseumWorld::kEntranceX + m.jumpX;
                gJumpY = CeMuseumWorld::kEntranceY + m.jumpY;
                gJumpYaw = m.jumpYaw;
                gPendingJump = true;
                PortSessionLog(PORT_SESSION_GAME, false, "museum directory: quick-jump to %s", m.title);
                return true;
            }
            if (intent != CeCinemaRoom::FireIntent::Ordinary) return true;
        } else {
            CeCinemaRoom::FireEdge(release || value <= 0.0f, false, gDirFireHeld, gDirFireCaptured);
        }
        if (CeMuseumTurntable::RuntimeHandleFire(release, value)) return true;
        if (CeMuseumAudioKiosk::RuntimeHandleFire(release, value, cinemaPlaying)) return true;
        // Consume stray Fire clicks in the museum so visitors never fire weapons inside galleries.
        return true;
    }
    return CinemaAction(release,value);
}

extern "C" bool PortCeMuseumCinemaPanel(CeMuseumCinemaConsole::Presentation *out,
    CeMuseumCinemaConsole::MeasureText measure, void *user, float lineHeight) {
    return CinemaPresentation(out, measure, user, lineHeight);
}

extern "C" bool PortCeMuseumCinemaCue(char *title,unsigned titleCap,char *line,unsigned lineCap) {
    return CinemaCue(title,titleCap,line,lineCap);
}

extern "C" bool PortCeMuseumInteractiveAim(void) {
    if (!g.valid || !PortCeMuseumVisitActive()) return false;
    if (Cinema::s.atConsole) return true;
    if (gGrandMode) {
        if (gDirHit.valid) return true;
        if (CeMuseumTurntable::RuntimeIsAimingControl()) return true;
        if (CeMuseumAudioKiosk::RuntimeIsAimingControl()) return true;
    }
    return false;
}

extern "C" bool PortCeMuseumInteractiveCard(float x, float y, float fx, float fy,
                                            char *title, unsigned titleCap,
                                            char *line, unsigned lineCap,
                                            char *note, unsigned noteCap) {
    if (!g.valid || !gGrandMode || !PortCeMuseumVisitActive()) return false;
    if (gDirHit.valid && gDirHit.index >= 0 && gDirHit.index < CeMuseumTour::kDirectoryStops) {
        const CeMuseumTour::DirectoryStop &ds = CeMuseumTour::kDirectory[gDirHit.index];
        const CeMuseumTour::WingMeta &meta = CeMuseumTour::Meta(ds.target);
        std::snprintf(title, titleCap, "Directory - %s", meta.title);
        std::snprintf(line, lineCap, "%s", meta.subtitle);
        if (note && noteCap) std::snprintf(note, noteCap, "Press Fire to move directly to this gallery.");
        return true;
    }
    if (CeMuseumAudioKiosk::RuntimeCue(title, titleCap, line, lineCap, note, noteCap)) return true;
    const CeMuseumExhibition::Vec2 local = CeMuseumExhibitionHost::Local(x, y);
    if (CeMuseumTurntable::RuntimeCue(local.x, local.y, fx, fy, gCurrentWing,
                                      title, titleCap, line, lineCap, note, noteCap))
        return true;
    if (CeMuseumArt::RuntimeCue(local.x, local.y, fx, fy, gCurrentWing,
                                title, titleCap, line, lineCap, note, noteCap))
        return true;
    return false;
}

extern "C" bool PortCeMuseumTakeQuickJump(float *worldX, float *worldY, float *yaw) {
    if (!gPendingJump) return false;
    gPendingJump = false;
    if (worldX) *worldX = gJumpX;
    if (worldY) *worldY = gJumpY;
    if (yaw) *yaw = gJumpYaw;
    return true;
}

// Subtle minimal neutral aim dot rendered ONLY when facing an interactive fixture.
extern "C" void PortCeMuseumDrawAimDot(float cx, float cy) {
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev) return;
    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }
    struct DotVertex { float x, y, z, rhw; DWORD colour; };
    auto drawDiamond = [&](float r, DWORD col) {
        const DotVertex q[6] = {
            {cx, cy - r, 0.0f, 1.0f, col}, {cx + r, cy, 0.0f, 1.0f, col}, {cx, cy + r, 0.0f, 1.0f, col},
            {cx, cy - r, 0.0f, 1.0f, col}, {cx, cy + r, 0.0f, 1.0f, col}, {cx - r, cy, 0.0f, 1.0f, col},
        };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q, sizeof(DotVertex));
    };
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTexture(0, NULL);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    drawDiamond(5.0f, 0xb014181e);
    drawDiamond(3.0f, 0xfff5ebd4);
    saved->Apply();
    saved->Release();
}

// Sunlit coastal sky dome + distant sea horizon rendered outdoors & through the Grand Atrium skylight.
static DWORD LerpColour(DWORD a, DWORD b, float t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    auto ch = [&](unsigned sh) -> DWORD {
        const float va = float((a >> sh) & 255u), vb = float((b >> sh) & 255u);
        return static_cast<DWORD>(va + (vb - va) * t + 0.5f) << sh;
    };
    return 0xff000000u | ch(16) | ch(8) | ch(0);
}

extern "C" void PortCeMuseumAuthoredBackdrop() {
    IDirect3DDevice9 *dev = LT.GetDevice();
    if (!dev || gMirrorPass || !PortCeMuseumAuthoredMap()) return;
    dev->Clear(0, 0, D3DCLEAR_TARGET, CeMuseumWorld::kGrandSkyHorizon, 1.0f, 0);

    IDirect3DStateBlock9 *saved = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) return;
    if (FAILED(saved->Capture())) { saved->Release(); return; }

    D3DMATRIX world = {}, view = {}, proj = {};
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    RENDERINFO.GetView(&view);
    RENDERINFO.GetProjection(&proj);
    // Zero translation so the procedural coastal sky dome stays infinitely distant around the camera
    view._41 = 0.0f; view._42 = 0.0f; view._43 = 0.0f;

    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetTexture(0, NULL);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

    // Elevation rings (radians: -PI/2 zenith [native -Z], 0 horizon, +PI/2 deep sea nadir [native +Z])
    const float elev[] = {-1.5708f, -0.95f, -0.45f, -0.14f, -0.02f, 0.02f, 0.14f, 0.45f, 1.5708f};
    const DWORD ringCol[] = {
        CeMuseumWorld::kGrandSkyZenith,
        0xff3c78b8,
        CeMuseumWorld::kGrandSkyMid,
        0xffa7d0eb,
        CeMuseumWorld::kGrandSkyHorizon,
        CeMuseumWorld::kGrandSeaHorizon,
        0xff3c7aa3,
        0xff21567d,
        CeMuseumWorld::kGrandSeaDeep,
    };
    // Sun direction in upper Northeast-East daylight sky (native Z down -> negative Z is up)
    const float sunX = 0.56f, sunY = 0.48f, sunZ = -0.675f;
    const unsigned kSegs = 24;
    const float R = 120.0f;
    Vertex strip[(kSegs + 1) * 2];

    for (unsigned r = 0; r < 8; ++r) {
        const float e0 = elev[r], e1 = elev[r + 1];
        const float c0 = std::cos(e0), s0 = std::sin(e0);
        const float c1 = std::cos(e1), s1 = std::sin(e1);
        for (unsigned i = 0; i <= kSegs; ++i) {
            const float a = float(i) * (6.2831853f / float(kSegs));
            const float ca = std::cos(a), sa = std::sin(a);
            auto evalCol = [&](float dx, float dy, float dz, DWORD base) -> DWORD {
                const float dot = dx * sunX + dy * sunY + dz * sunZ;
                if (dot > 0.72f && dz <= 0.05f) {
                    const float g = (dot - 0.72f) / 0.28f;
                    return LerpColour(base, CeMuseumWorld::kGrandSunGlow, g * g * 0.65f);
                }
                return base;
            };
            const float x0 = c0 * ca, y0 = c0 * sa, z0 = s0;
            const float x1 = c1 * ca, y1 = c1 * sa, z1 = s1;
            strip[i * 2 + 0] = Vertex{x0 * R, y0 * R, z0 * R, evalCol(x0, y0, z0, ringCol[r])};
            strip[i * 2 + 1] = Vertex{x1 * R, y1 * R, z1 * R, evalCol(x1, y1, z1, ringCol[r + 1])};
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, kSegs * 2, strip, sizeof(Vertex));
    }

    // Soft glowing solar disc in the upper coastal sky
    {
        const float sDist = 110.0f;
        const float sx = sunX * sDist, sy = sunY * sDist, sz = sunZ * sDist;
        // Orthonormal basis around sun direction
        float ux = -sunY, uy = sunX, uz = 0.0f;
        const float ul = std::sqrt(ux * ux + uy * uy);
        ux /= ul; uy /= ul;
        const float vx = sunY * uz - sunZ * uy;
        const float vy = sunZ * ux - sunX * uz;
        const float vz = sunX * uy - sunY * ux;
        const float rad = 7.5f;
        Vertex fan[18];
        fan[0] = Vertex{sx, sy, sz, CeMuseumWorld::kGrandSunCore};
        for (unsigned i = 0; i <= 16; ++i) {
            const float a = float(i) * (6.2831853f / 16.0f);
            const float ca = std::cos(a) * rad, sa = std::sin(a) * rad;
            fan[i + 1] = Vertex{sx + ux * ca + vx * sa, sy + uy * ca + vy * sa, sz + uz * ca + vz * sa,
                                CeMuseumWorld::kGrandSunGlow};
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 16, fan, sizeof(Vertex));
    }

    saved->Apply();
    saved->Release();
}

static const float kHouseStep = 1.0f / 45.0f;
static float gHouseLight = 1.0f;
static float HouseShade(unsigned m, float house) {
    const float down = m == kMatCinemaWall || m == kMatCinemaCeiling || m == kMatStone ? 0.34f : 0.62f;
    return down + (1.0f - down) * house;
}
static unsigned gHousePresent = 0;
static bool gHouseStarted = false;
static void HouseLightReset() {
    gHouseLight = 1.0f;
    gHousePresent = 0;
    gHouseStarted = false;
}
static float HouseLight() {
    const bool film = Cinema::s.inRoom && Cinema::s.phase == Cinema::Playing;
    const float target = film ? 0.0f : 1.0f;
    const unsigned present = PortPresentCount();
    if (!gHouseStarted) { gHouseLight = target; gHousePresent = present; gHouseStarted = true; return gHouseLight; }
    const unsigned steps = present - gHousePresent;
    gHousePresent = present;
    gHouseLight = CeCinemaRoom::StepHouseLight(gHouseLight, target, steps, GAME.IsPaused(), kHouseStep);
    return gHouseLight;
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
    const float house = HouseLight();
    for (unsigned m = 0; m < kMatCount; ++m) {
        if (!g.count[m]) continue;
        IDirect3DTexture9 *texture = g.texture[m];
        dev->SetTexture(0,texture);
        dev->SetTextureStageState(0,D3DTSS_COLOROP,texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0,D3DTSS_COLORARG1,texture ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
        const float quiet = texture ? kMaterials[m].quiet : 0.0f;
        const float light = CinemaMaterial(m) ? HouseShade(m, house) : 1.0f;
        const unsigned shade = unsigned(light * 255.0f + 0.5f), keep = unsigned((1.0f - quiet) * 255.0f + 0.5f);
        dev->SetRenderState(D3DRS_TEXTUREFACTOR,D3DCOLOR_ARGB(keep,shade,shade,shade));
        unsigned stage = 1;
        if (quiet > 0.0f) {
            dev->SetTextureStageState(stage,D3DTSS_COLOROP,D3DTOP_BLENDFACTORALPHA);
            dev->SetTextureStageState(stage,D3DTSS_COLORARG1,D3DTA_CURRENT);
            dev->SetTextureStageState(stage,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
            dev->SetTextureStageState(stage,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
            dev->SetTextureStageState(stage,D3DTSS_ALPHAARG1,D3DTA_CURRENT);
            ++stage;
        }
        if (shade < 255) {
            dev->SetTextureStageState(stage,D3DTSS_COLOROP,D3DTOP_MODULATE);
            dev->SetTextureStageState(stage,D3DTSS_COLORARG1,D3DTA_CURRENT);
            dev->SetTextureStageState(stage,D3DTSS_COLORARG2,D3DTA_TFACTOR);
            dev->SetTextureStageState(stage,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
            dev->SetTextureStageState(stage,D3DTSS_ALPHAARG1,D3DTA_CURRENT);
            ++stage;
        }
        dev->SetTextureStageState(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
        dev->SetTextureStageState(stage,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        material.Emissive.r = material.Emissive.g = material.Emissive.b = kMaterials[m].glow;
        dev->SetMaterial(&material);
        const bool isInlay = (m == kMatInlay || m == kMatTrim || m == kMatNumeral);
        const float bias = isInlay ? -0.00002f : 0.0f, slope = isInlay ? -1.0f : 0.0f;
        dev->SetRenderState(D3DRS_DEPTHBIAS,*reinterpret_cast<const DWORD *>(&bias));
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,*reinterpret_cast<const DWORD *>(&slope));
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,g.count[m],g.sorted + g.first[m],sizeof(LitVertex));
    }
    dev->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    dev->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    const float none = 0.0f;
    dev->SetRenderState(D3DRS_DEPTHBIAS,*reinterpret_cast<const DWORD *>(&none));
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,*reinterpret_cast<const DWORD *>(&none));
    dev->SetRenderState(D3DRS_LIGHTING,FALSE);
    dev->SetTexture(0,0);
    dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
    DrawDirectoryButtons(dev, gGrandFloorZ);
    CinemaConsoleGeometry(dev);
    CinemaScreen(dev);
    saved->Apply(); saved->Release();

    // Draw wing-scoped framed concept art canvases and rotating 3D Goodie exhibits with full state guards.
    if (gGrandMode) {
        CeMuseumArt::RuntimeDraw(gGrandFloorZ);
        CeMuseumTurntable::RuntimeDraw(gGrandFloorZ);
    }
}
#endif
