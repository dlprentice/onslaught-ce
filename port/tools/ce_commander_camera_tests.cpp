// SPDX-FileCopyrightText: Onslaught CE contributors
// SPDX-License-Identifier: GPL-3.0-only

#if !defined(CE_COMMANDER_CAMERA_INTEGRATION)
#include "ce_commander_camera.h"
#include <cstdlib>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace {

void Check(bool condition, int line) {
    if (!condition) {
        std::cerr << "authored lifecycle control failed at line " << line << '\n';
        std::exit(1);
    }
}
#define CHECK(condition) Check((condition), __LINE__)

bool Same(const CeCommanderCameraBinding& a, const CeCommanderCameraBinding& b) {
    return a.mission == b.mission && a.pilot == b.pilot && a.camera == b.camera;
}

CeCommanderCameraContext OrdinaryContext() {
    // Authored identities: no object addresses, player slots or game data.
    CeCommanderCameraContext c;
    c.binding.mission = 11; c.binding.pilot = 22; c.binding.camera = 33;
    c.admitted_ordinary_solo = true; c.live_pilot = true;
    c.camera_available = true; c.competing_camera_owner = false;
    return c;
}

struct Consumer : CeCommanderCameraConsumer {
    std::string view = "pilot view", saved_view;
    CeCommanderCameraBinding saved_binding;
    CeCommanderCameraInput last_input;
    std::vector<std::string> calls;
    bool captured = false, fail_capture = false, fail_begin = false;
    bool fail_update = false, fail_restore = false;

    bool Capture(const CeCommanderCameraBinding& b) noexcept override {
        calls.push_back("capture"); CHECK(!captured);
        if (fail_capture) return false;
        captured = true; saved_view = view; saved_binding = b; return true;
    }
    bool BeginStrategicView(const CeCommanderCameraBinding& b) noexcept override {
        calls.push_back("begin"); CHECK(captured && Same(b, saved_binding));
        view = "strategic view"; return !fail_begin;
    }
    bool UpdateStrategicView(const CeCommanderCameraBinding& b,
                             const CeCommanderCameraInput& input) noexcept override {
        calls.push_back("update"); CHECK(captured && Same(b, saved_binding));
        last_input = input; view = "updated strategic view"; return !fail_update;
    }
    bool Restore(const CeCommanderCameraBinding& b) noexcept override {
        calls.push_back("restore"); CHECK(captured && Same(b, saved_binding));
        if (fail_restore) return false;
        view = saved_view; captured = false; return true;
    }
    void Abandon(const CeCommanderCameraBinding& b) noexcept override {
        calls.push_back("abandon"); CHECK(captured && Same(b, saved_binding));
        captured = false;
    }
};

void Calls(const Consumer& c, std::initializer_list<const char*> expected) {
    std::vector<std::string> wanted;
    for (const char* call : expected) wanted.push_back(call);
    CHECK(c.calls == wanted);
}

void Invalidate(CeCommanderCameraContext& c, int gate) {
    switch (gate) {
    case 0: c.admitted_ordinary_solo = false; break;
    case 1: c.live_pilot = false; break;
    case 2: c.camera_available = false; break;
    case 3: c.competing_camera_owner = true; break;
    case 4: c.binding.mission = 0; break;
    case 5: c.binding.pilot = 0; break;
    case 6: c.binding.camera = 0; break;
    }
}

void AdmissionAndNormalCycle() {
    Consumer c; CeCommanderCameraOwner owner(c);
    CHECK(!owner.Active() && !owner.OwnsCapture() && !owner.RestorePending());
    CHECK(owner.Enter(CeCommanderCameraContext()) == CeCommanderCameraResult::Refused);
    CHECK(owner.Update(OrdinaryContext(), CeCommanderCameraInput()) == CeCommanderCameraResult::Inactive);
    CHECK(owner.Leave(OrdinaryContext()) == CeCommanderCameraResult::Inactive);
    for (int gate = 0; gate != 7; ++gate) {
        CeCommanderCameraContext context = OrdinaryContext(); Invalidate(context, gate);
        CHECK(owner.Enter(context) == CeCommanderCameraResult::Refused);
        CHECK(!owner.OwnsCapture());
    }
    CHECK(c.calls.empty());
    const CeCommanderCameraContext context = OrdinaryContext();
    CHECK(owner.Enter(context) == CeCommanderCameraResult::Entered);
    CHECK(owner.Active() && owner.OwnsCapture() && !owner.RestorePending());
    CHECK(c.view == "strategic view");
    CHECK(owner.Enter(context) == CeCommanderCameraResult::AlreadyActive);
    Calls(c, {"capture", "begin"});
    CeCommanderCameraInput input;
    input.elapsed_seconds = 0.25; input.pan_horizontal = -0.5; input.pan_vertical = 0.75;
    input.orbit_horizontal = 1.25; input.orbit_vertical = -1.5; input.zoom = 2;
    CHECK(owner.Update(context, input) == CeCommanderCameraResult::Updated);
    CHECK(c.last_input.elapsed_seconds == input.elapsed_seconds);
    CHECK(c.last_input.pan_horizontal == input.pan_horizontal);
    CHECK(c.last_input.pan_vertical == input.pan_vertical);
    CHECK(c.last_input.orbit_horizontal == input.orbit_horizontal);
    CHECK(c.last_input.orbit_vertical == input.orbit_vertical);
    CHECK(c.last_input.zoom == input.zoom);
    // Reapply after normal engine camera updates even without user movement.
    CHECK(owner.Update(context, CeCommanderCameraInput()) == CeCommanderCameraResult::Updated);
    CHECK(owner.Leave(context) == CeCommanderCameraResult::Left);
    CHECK(c.view == "pilot view" && !c.captured && !owner.OwnsCapture());
    CHECK(owner.Leave(context) == CeCommanderCameraResult::Inactive);
    Calls(c, {"capture", "begin", "update", "update", "restore"});
}

void InvalidInput() {
    const double bad[] = {std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity()};
    Consumer c; CeCommanderCameraOwner owner(c);
    const CeCommanderCameraContext context = OrdinaryContext();
    CHECK(owner.Enter(context) == CeCommanderCameraResult::Entered);
    for (double value : bad) for (int field = 0; field != 6; ++field) {
        CeCommanderCameraInput input;
        switch (field) {
        case 0: input.elapsed_seconds = value; break;
        case 1: input.pan_horizontal = value; break;
        case 2: input.pan_vertical = value; break;
        case 3: input.orbit_horizontal = value; break;
        case 4: input.orbit_vertical = value; break;
        case 5: input.zoom = value; break;
        }
        CHECK(owner.Update(context, input) == CeCommanderCameraResult::Refused);
        CHECK(owner.Active());
    }
    CeCommanderCameraInput negative; negative.elapsed_seconds = -1;
    CHECK(owner.Update(context, negative) == CeCommanderCameraResult::Refused);
    Calls(c, {"capture", "begin"});
    CHECK(owner.Leave(context) == CeCommanderCameraResult::Left);
}

void LifetimesAndFences() {
    for (int identity = 0; identity != 3; ++identity) {
        // All public transitions must reject a replaced lifetime; none silently
        // enters the replacement or restores an old view over its camera.
        for (int operation = 0; operation != 3; ++operation) {
            Consumer c; CeCommanderCameraOwner owner(c);
            CeCommanderCameraContext context = OrdinaryContext();
            CHECK(owner.Enter(context) == CeCommanderCameraResult::Entered);
            if (identity == 0) ++context.binding.mission;
            if (identity == 1) ++context.binding.pilot;
            if (identity == 2) ++context.binding.camera;
            c.view = "replacement view";
            CeCommanderCameraResult result = CeCommanderCameraResult::Inactive;
            if (operation == 0) result = owner.Enter(context);
            if (operation == 1) result = owner.Update(context, CeCommanderCameraInput());
            if (operation == 2) result = owner.Leave(context);
            CHECK(result == CeCommanderCameraResult::ContextLost);
            CHECK(!owner.OwnsCapture() && !c.captured && c.view == "replacement view");
            Calls(c, {"capture", "begin", "abandon"});
            CHECK(owner.Update(context, CeCommanderCameraInput()) == CeCommanderCameraResult::Inactive);
            CHECK(owner.Enter(context) == CeCommanderCameraResult::Entered);
            CHECK(owner.Leave(context) == CeCommanderCameraResult::Left);
            CHECK(c.view == "replacement view");
        }
    }
    for (int gate = 0; gate != 7; ++gate) {
        Consumer c; CeCommanderCameraOwner owner(c);
        CeCommanderCameraContext context = OrdinaryContext();
        CHECK(owner.Enter(context) == CeCommanderCameraResult::Entered);
        Invalidate(context, gate); c.view = "next view owner";
        CHECK(owner.Update(context, CeCommanderCameraInput()) == CeCommanderCameraResult::ContextLost);
        CHECK(!owner.OwnsCapture() && !c.captured && c.view == "next view owner");
        Calls(c, {"capture", "begin", "abandon"});
    }
}

void ConsumerFailuresAndRecovery() {
    const CeCommanderCameraContext context = OrdinaryContext();
    {
        Consumer c; c.fail_capture = true; CeCommanderCameraOwner owner(c);
        CHECK(owner.Enter(context) == CeCommanderCameraResult::CaptureFailed);
        CHECK(!owner.OwnsCapture() && !c.captured && c.view == "pilot view");
        Calls(c, {"capture"});
    }
    for (bool during_begin : {false, true}) for (bool restore_fails : {false, true}) {
        Consumer c; c.fail_begin = during_begin; c.fail_update = !during_begin;
        c.fail_restore = restore_fails; CeCommanderCameraOwner owner(c);
        CeCommanderCameraResult result = owner.Enter(context);
        if (!during_begin) {
            CHECK(result == CeCommanderCameraResult::Entered);
            result = owner.Update(context, CeCommanderCameraInput());
        }
        CHECK(result == (restore_fails ? CeCommanderCameraResult::RestorePending
                                      : CeCommanderCameraResult::ConsumerFailed));
        if (during_begin) Calls(c, {"capture", "begin", "restore"});
        else Calls(c, {"capture", "begin", "update", "restore"});
        if (restore_fails) {
            const std::vector<std::string> before = c.calls;
            CHECK(owner.RestorePending() && owner.OwnsCapture() && !owner.Active());
            CHECK(owner.Enter(context) == CeCommanderCameraResult::RestorePending);
            CHECK(owner.Update(context, CeCommanderCameraInput()) == CeCommanderCameraResult::RestorePending);
            CHECK(c.calls == before);
            c.fail_restore = false;
            CHECK(owner.Leave(context) == CeCommanderCameraResult::Left);
        }
        CHECK(!owner.OwnsCapture() && !c.captured && c.view == "pilot view");
    }
    {
        Consumer c; CeCommanderCameraOwner owner(c); CeCommanderCameraContext current = context;
        CHECK(owner.Enter(current) == CeCommanderCameraResult::Entered);
        c.fail_restore = true;
        CHECK(owner.Leave(current) == CeCommanderCameraResult::RestorePending);
        CHECK(owner.Enter(current) == CeCommanderCameraResult::RestorePending);
        c.fail_restore = false;
        CHECK(owner.Leave(current) == CeCommanderCameraResult::Left);
        CHECK(owner.Enter(current) == CeCommanderCameraResult::Entered);
        c.fail_restore = true;
        CHECK(owner.Leave(current) == CeCommanderCameraResult::RestorePending);
        ++current.binding.camera; c.view = "replacement camera";
        CHECK(owner.Leave(current) == CeCommanderCameraResult::ContextLost);
        CHECK(!owner.OwnsCapture() && !c.captured && c.view == "replacement camera");
        CHECK(c.calls.back() == "abandon");
    }
}

void ResetAndDestruction() {
    const CeCommanderCameraContext context = OrdinaryContext();
    {
        Consumer c; CeCommanderCameraOwner owner(c);
        CHECK(owner.Enter(context) == CeCommanderCameraResult::Entered);
        owner.Reset(); owner.Reset();
        CHECK(!owner.OwnsCapture() && !c.captured && c.view == "strategic view");
        Calls(c, {"capture", "begin", "abandon"});
    }
    for (bool pending : {false, true}) {
        Consumer c;
        {
            CeCommanderCameraOwner owner(c);
            CHECK(owner.Enter(context) == CeCommanderCameraResult::Entered);
            if (pending) {
                c.fail_restore = true;
                CHECK(owner.Leave(context) == CeCommanderCameraResult::RestorePending);
            }
        }
        CHECK(!c.captured && c.view == "strategic view" && c.calls.back() == "abandon");
    }
}

} // namespace

int main() {
    static_assert(!std::is_copy_constructible<CeCommanderCameraOwner>::value, "one capture owner");
    static_assert(!std::is_move_constructible<CeCommanderCameraOwner>::value, "stable consumer owner");
    AdmissionAndNormalCycle(); InvalidInput(); LifetimesAndFences();
    ConsumerFailuresAndRecovery(); ResetAndDestruction();
    std::cout << "authored commander camera lifecycle controls passed\n";
    return 0;
}
#else

#include "ce_commander_camera.h"
#include "ce_photo.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// Authored engine/input doubles. The included fragment is extracted from the
// actual CE consumer, entry, exit, frame, lifecycle and presentation functions.
using WORD = unsigned short;
using DWORD = unsigned long;
using SINT = int;
enum {
    DIK_A = 1, DIK_D, DIK_W, DIK_S, DIK_Q, DIK_E, DIK_R, DIK_F,
    DIK_UP, DIK_DOWN, DIK_ESCAPE, DIK_M, DIK_V, DIK_LEFT, DIK_RIGHT,
    DIK_TAB, DIK_LSHIFT, DIK_RSHIFT, DIK_C, DIK_H, DIK_X, DIK_NEXT, DIK_PRIOR,
    KEY_EVENT_UP = 41, KEY_EVENT_DOWN
};
// The disabled-presentation-hook control still exercises the authored UI
// adapter. Import its real public descriptor types without enabling that hook.
#if !defined(CE_VERSION)
#define CE_CAMERA_TEST_TEMPORARY_VERSION
#define CE_VERSION "authored-types"
#endif
#include "ce_commander.h"
#include "ce_world_view.h"
#if defined(CE_CAMERA_TEST_TEMPORARY_VERSION)
#undef CE_VERSION
#undef CE_CAMERA_TEST_TEMPORARY_VERSION
#endif
void Verify(bool value, int line) {
    if (!value) {
        std::cerr << "authored integration control failed at line " << line << '\n';
        std::exit(1);
    }
}
#define VERIFY(value) Verify((value), __LINE__)

struct FVector {
    float X, Y, Z;
    FVector(float x = 0, float y = 0, float z = 0) : X(x), Y(y), Z(z) {}
    FVector operator-(const FVector& v) const { return FVector(X-v.X, Y-v.Y, Z-v.Z); }
    FVector operator*(float n) const { return FVector(X*n, Y*n, Z*n); }
};
bool SamePosition(const FVector& a, const FVector& b) {
    return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }
struct FMatrix {
    float yaw = 0, pitch = 0;
    void MakeRotationYawF(float y) { yaw = y; pitch = 0; }
    void MakeRotationF(float y, float p, float) { yaw = y; pitch = p; }
    FVector operator*(const FVector& p) const {
        const float forward = std::cos(pitch)*p.Y - std::sin(pitch)*p.Z;
        return FVector(std::cos(yaw)*p.X - std::sin(yaw)*forward,
                       std::sin(yaw)*p.X + std::cos(yaw)*forward,
                       std::sin(pitch)*p.Y + std::cos(pitch)*p.Z);
    }
};
struct CCamera {
    virtual ~CCamera() {}
    virtual float GetZoom() { return 1; }
    virtual float GetYaw() { return 0; }
};
struct CGenericCamera : CCamera {
    FVector mPos;
    FMatrix mOrientation;
    float mZoom = 1;
    float GetZoom() override { return mZoom; }
    float GetYaw() override { return mOrientation.yaw; }
};
struct CBattleEngine {
    FVector position;
    float life = 10;
    int creation_number = 1;
    float GetLife() { return life; }
    int GetThingNumber() { return creation_number; }
    FVector GetPos() { return position; }
};
struct CPlayer {
    CBattleEngine* pilot = NULL;
    CBattleEngine* GetBattleEngine() { return pilot; }
};
using KeyTrap = void (*)(WORD, int);
using pKeyTrapper = KeyTrap;
const bool FALSE = false;
const WORD kOpenKey = DIK_M;
const int kMouseUp=3, kMouseDown=4;
int MOUSE_X=400, MOUSE_Y=300;
struct AuthoredMap { int mGridWidth=1000,mGridHeight=1000; float height=0,slopeX=0,slopeY=0; float Collide(const FVector& p){return height+slopeX*p.X+slopeY*p.Y;} } MAP;
struct CCONTROLLER {
    KeyTrap trap = NULL;
    void* target = NULL;
    void* previous = NULL;
    int target_changes = 0;
    KeyTrap GetKeytrap() { return trap; }
    void SetKeytrap(KeyTrap t) { trap = t; }
    void* GetToControl() { return target; }
    void SetToControl(void* t) { previous = target; target = t; ++target_changes; }
    void RelinquishControl() { target = previous; ++target_changes; }
};
struct Game {
    bool paused = false, ordinary = true;
    int pause_calls = 0, level = 7, simulation_ticks = 0;
    CPlayer* player = NULL;
    CCamera* camera = NULL;
    CCONTROLLER controller;
    bool IsPaused() { return paused; }
    void Pause() { ++pause_calls; paused = true; }
    CPlayer* GetPlayer(int player_index) { VERIFY(player_index == 0); return player; }
    CCamera* GetCurrentCamera(int player_index) { VERIFY(player_index == 0); return camera; }
    CCONTROLLER& GetController() { return controller; }
    CCONTROLLER* GetController(int) { return &controller; }
    int GetCurrentLevel() { return level; }
    void Tick() {
        if (!paused) { ++simulation_ticks; player->pilot->life -= 1; }
    }
} GAME;
struct Shell {
    bool KeyDown[256] = {};
    KeyTrap GetKeytrap() { return GAME.controller.trap; }
    void SetKeytrap(KeyTrap trap) { GAME.controller.trap = trap; }
} LT;
struct GameAccess {
    static int pause_field_reads;
    static bool Game::* PauseField() { ++pause_field_reads; return &Game::paused; }
};
int GameAccess::pause_field_reads = 0;
int mouse_starts = 0, mouse_stops = 0, draw_starts = 0, draw_ends = 0;
void InitMouse() { ++mouse_starts; }
void ShutdownMouse() { ++mouse_stops; }
struct Sink {} gSink;
bool gOpen = false, gOpening = false, gShift = false, gLayoutLogged = false;
bool gEscClosed = false, gDown[256] = {};
int gArmed = 0, gSelected = 0, gHover=-1, gCount = 0, gKeptLevel = -1;
unsigned gFrames = 0;
double gMsTotal = 0, gMsMax = 0, fake_ms = 0;
unsigned long long gMapLastNs = 0;
// What the host's clock reads at the start: the nanoseconds since boot of a machine that has been up for two days (the driver turns it up for the
// x87 proof below). The game's x87 unit holds a double to 24 bits, so a clock kept as a double of milliseconds since boot has a 16 ms grain here.
unsigned long long fake_base_ns = 0;
float gViewX = 0, gViewY = 0, gViewSpan = 100;
float gBoundsX0 = 0, gBoundsY0 = 0, gBoundsX1 = 1000, gBoundsY1 = 1000;
bool gTerrainDirty = false;
float gKeptCentreX = 0, gKeptCentreY = 0, gKeptSpan = 0;
char gNote[256] = {};
unsigned gNoteUntil = 0;
bool gNoteNotYet = false;
const unsigned kNoteLength = 240;
const int kMaxSquads = 8;
const DWORD kPick = 0xffd0e8b0, kText=0xffeeeeee, kQuiet=0xff999999, kFriend=0xff00ff00, kEnemy=0xffff0000, kAdvanceColour=0xffeeaa00, kMoveColour=0xff00eeff;
CeCmdSquad gSquads[kMaxSquads];
std::vector<CeCmdSquad> live_squads;
int gUntracked = 0, list_reads = 0, issue_attempts = 0, accepted_orders = 0, last_ordered = -1;
bool refuse_next_order = false;
struct Layout { float s = 1, w = 800, h = 600; } L;
std::vector<std::string> messages;
std::vector<float> text_y;
bool photo_on = false, draw_filter = false;
bool editor_hud=false;
bool PortCeEditorSuppressFlightHud(){return editor_hud;}
int draws_dropped = 0;
bool PortCePhotoOn() { return photo_on; }
void PortCeDrawHooks(bool on) { draw_filter = on; if (!on) draws_dropped = 0; }
void PortCeDrawsDropped(int delta) { draws_dropped += delta; }
void CePlayUnitLevelEnd() {}		// the play-as-a-unit module's call in PortCeCmdLevelEnd: no unit is driven here
CCamera* PortCePlayUnitRenderCamera(CCamera* camera, CPlayer*) { return camera; } // no tank possession in this fixture
unsigned long long PortClockRawNs() {
    return fake_base_ns + static_cast<unsigned long long>(fake_ms * 1000000.0);
}
bool fake_virtual_clock = false;
bool PortClockVirtual() { return fake_virtual_clock; }
// The platform's own record of the keyboard (window.cpp's PortKeyHeldNow): per key 1 held, 0 not, -1 it cannot say (the trap's record then decides).
int fake_platform_keys[256];
extern "C" int PortKeyHeldNow(unsigned dik) { return dik < 256 ? fake_platform_keys[dik] : -1; }
bool Allowed() { return GAME.ordinary; }
bool NotYet(){return false;}
bool CeCmdMapOn(){return GAME.ordinary;}
bool World() { return true; }
void Lay() {}
unsigned PortPresentCount() { return static_cast<unsigned>(fake_ms); }
int CeCmdList(CeCmdSquad* out, int capacity) {
    ++list_reads;
    const int count = static_cast<int>(live_squads.size()) < capacity ? static_cast<int>(live_squads.size()) : capacity;
    for (int i = 0; i < count; ++i) out[i] = live_squads[i];
    return count;
}
int CeCmdUntracked() { return 0; }
float last_order_x=0,last_order_y=0;
int last_order_verb=0;
bool CeCmdIssue(int ordinal, int verb, float x, float y, const char** why) {
    last_order_x=x;last_order_y=y;last_order_verb=verb;
    ++issue_attempts;
    if (refuse_next_order) { refuse_next_order = false; *why = "Squad no longer takes orders."; return false; }
    for (CeCmdSquad& s : live_squads) {
        if (s.ordinal == ordinal && s.commandable) {
            s.verb = verb; ++accepted_orders; last_ordered = ordinal; return true;
        }
    }
    *why = "Squad disappeared."; return false;
}
void Text(float, float y, DWORD, const char* message, float) { messages.push_back(message); text_y.push_back(y); }
void Box(float, float, float width, float height, DWORD) { VERIFY(width > 0 && height > 0); }
void Outline(float,float,float,float,float,DWORD){}
void Dots(float,float,float,float,float,DWORD){}
int cursor_draws=0;
void DrawCursor(){++cursor_draws;}
CeWorldViewSample authored_view={};
bool authored_view_valid=false;
void CeWorldViewInvalidate(CeWorldViewOwner){authored_view_valid=false;}
bool CeWorldViewRead(CeWorldViewOwner owner,std::uint64_t token,CeWorldViewSample* out){
    if(!authored_view_valid || owner!=CeWorldViewOwner::Commander || !token || token!=PortCeCmdWorldViewToken())return false;
    *out=authored_view;return true;
}
void BeginDraw() { ++draw_starts; }
void EndDraw() { ++draw_ends; }
bool Trap(WORD key, int event);

// The Linux Windows compatibility headers expose these function macros.
// Exercise the actual consumer under that same preprocessor constraint.
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
// The existing map callback returns bool through the shell's void callback
// cast. Keep that production contract in the double; GCC diagnoses the cast
// even though the shell discards its return value.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#include "ce_commander_strategic_actual.inc"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#undef min
#undef max

CCamera* PresentationCamera(CCamera* camera, CPlayer* player) {
    (void)player;
#include "ce_commander_strategic_hook.inc"
    return camera;
}

CBattleEngine pilot;
CPlayer player;
CGenericCamera original_camera;
void Fixture() {
    gStrategicOwner.Reset(); gOpen = false; gStrategic = false;
    GAME = Game(); pilot = CBattleEngine(); player.pilot = &pilot;
    original_camera = CGenericCamera(); original_camera.mPos = FVector(3, 5, 7);
    pilot.position = FVector(200, 220, 12);
    live_squads.assign(2, CeCmdSquad());
    for (int i = 0; i < 2; ++i) {
        live_squads[i].ordinal = 5 + 12 * i;
        live_squads[i].friendly = live_squads[i].visible = live_squads[i].commandable = true;
        live_squads[i].members = live_squads[i].peak = 3;
        std::snprintf(live_squads[i].unit, sizeof(live_squads[i].unit), "Authored squad %d", i);
    }
    live_squads[0].x = 180; live_squads[0].y = 220;
    live_squads[1].x = 240; live_squads[1].y = 260;
    gSelected = -1; Refresh();
    gNote[0] = 0; gNoteUntil = 0; gNoteNotYet = false;
    list_reads = issue_attempts = accepted_orders = 0; last_ordered = -1; refuse_next_order = false;
    GAME.player = &player; GAME.camera = &original_camera; GAME.controller.target = &pilot;
    gObservedPlayer = NULL; gObservedPilot = NULL; gObservedCamera = NULL;
    AdvanceCameraLifetime(gMissionLifetime);
    std::memset(gDown, 0, sizeof(gDown)); gKeptLevel = -1; fake_ms = 0;
    for (int& key : fake_platform_keys) key = -1;
    gStrategicMessageKeys = false;
    std::memset(LT.KeyDown, 0, sizeof(LT.KeyDown));
    mouse_starts = mouse_stops = draw_starts = draw_ends = 0;
    GameAccess::pause_field_reads = 0; messages.clear(); text_y.clear();
    photo_on = draw_filter = editor_hud = false; draws_dropped = 0;
    MAP=AuthoredMap();authored_view_valid=false;cursor_draws=0;
    last_order_x=last_order_y=0;last_order_verb=0;MOUSE_X=400;MOUSE_Y=300;
}
void OtherTrap(WORD, int) {}
void TapLiveKey(WORD key) {
    Trap(key, KEY_EVENT_DOWN); Trap(key, KEY_EVENT_UP);
}

float MapPanFor(const std::vector<double>& frames_ms) {
    Fixture(); Open(); SetView(500, 500, 100);
    const float start = gViewX;
    gDown[DIK_RIGHT] = true;
    for (double time : frames_ms) { fake_ms = time; MapRenderNavigation(); }
    const float moved = gViewX - start;
    Close();
    return moved;
}

// The live camera moves with every rendered frame, not only with the game's updates (the frame-feel log showed 20 steps a second).
float Travel(const FVector& a, const FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void RenderTimeCamera() {
    Fixture(); Open(true); gDown[kOpenKey] = false;
    VERIFY(gStrategic && gStrategicOwner.Active());
    gStrategicTickOnly = false;
    gStrategicAdvancedAt = ~0u;
    fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
    // a held key and frames only: no game update is called
    Trap(DIK_RIGHT, KEY_EVENT_DOWN);
    FVector last = gStrategicCamera.mPos;
    float steps[10];
    for (int i = 0; i < 10; ++i) {
        fake_ms += 1000.0 / 60;
        VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicCamera);
        steps[i] = Travel(gStrategicCamera.mPos, last);
        last = gStrategicCamera.mPos;
        VERIFY(steps[i] > 0.01f);                                 // it moved on this frame
        VERIFY(std::fabs(steps[i] - steps[0]) < 0.002f * steps[0] + 0.0005f);        // by the same amount: it is time that moves it
    }
    // another view in the same frame (a reflection) keeps the camera the first one used
    const FVector held = gStrategicCamera.mPos;
    fake_ms = std::floor(fake_ms) + 0.999;                          // a little later, still the same Present
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicCamera && SamePosition(gStrategicCamera.mPos, held));
    // an update in the same pass moves it only by the time since the frame's own move, and the next frame carries on from there
    fake_ms += 1000.0 / 60; PortCeCmdFrame();
    const FVector after_update = gStrategicCamera.mPos;
    VERIFY(Travel(after_update, held) > 0.01f);
    fake_ms += 1.0; PortCeCmdRenderCamera(&original_camera, &player);
    VERIFY(Travel(gStrategicCamera.mPos, after_update) < steps[0] * 0.2f);
    // the earlier, update-stepped camera, for the comparison
    gStrategicTickOnly = true;
    fake_ms += 1000.0 / 60;
    const FVector before_frame = gStrategicCamera.mPos;
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicCamera && SamePosition(gStrategicCamera.mPos, before_frame));
    fake_ms += 50; PortCeCmdFrame();
    VERIFY(!SamePosition(gStrategicCamera.mPos, before_frame));
    gStrategicTickOnly = false;
    // keys up: it rests, frame after frame
    Trap(DIK_RIGHT, KEY_EVENT_UP);
    fake_ms += 1000.0 / 60; PortCeCmdRenderCamera(&original_camera, &player);
    const FVector rest = gStrategicCamera.mPos;
    for (int i = 0; i < 5; ++i) { fake_ms += 1000.0 / 60; PortCeCmdRenderCamera(&original_camera, &player); VERIFY(SamePosition(gStrategicCamera.mPos, rest)); }
    // the view that is not the live one is not advanced
    Close();
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    // under a virtual clock the map's time is the frames' own: 1/30 s a Present, whatever the real clock says
    fake_virtual_clock = true;
    fake_ms = 90; VERIFY(Ns() == 90ull * 33333333ull);
    fake_ms = 91; VERIFY(Ns() == 91ull * 33333333ull);
    fake_virtual_clock = false;
    fake_ms = 90; VERIFY(Ns() == fake_base_ns + 90000000ull);
}

// The camera reads the keyboard as it is held now, not as the game's last batch of key messages left it (the messages come once for every update).
void KeysAsHeldNow() {
    Fixture(); Open(true); gDown[kOpenKey] = false;
    gStrategicTickOnly = false; gStrategicAdvancedAt = ~0u;
    fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
    FVector last = gStrategicCamera.mPos;
    auto frame = [&]() { fake_ms += 1000.0 / 60; PortCeCmdRenderCamera(&original_camera, &player);
                         const bool moved = !SamePosition(gStrategicCamera.mPos, last); last = gStrategicCamera.mPos; return moved; };
    VERIFY(!frame());                                   // nothing held: it rests
    // pressed, and the trap has heard nothing yet: it moves on this very frame
    fake_platform_keys[DIK_RIGHT] = 1;
    VERIFY(!gDown[DIK_RIGHT] && frame() && frame());
    // released, and the trap still has it down: it stops on this very frame
    gDown[DIK_RIGHT] = true; fake_platform_keys[DIK_RIGHT] = 0;
    VERIFY(!frame() && !frame());
    // where the platform cannot say, the trap's record decides
    fake_platform_keys[DIK_RIGHT] = -1;
    VERIFY(frame() && frame());
    gDown[DIK_RIGHT] = false;
    VERIFY(!frame());
    // the second pan keys and the orbit, tilt and zoom keys are read the same way
    for (WORD key : {DIK_LEFT, DIK_UP, DIK_W, DIK_DOWN, DIK_S, DIK_E, DIK_Q, DIK_NEXT, DIK_PRIOR, DIK_R, DIK_F}) {
        fake_platform_keys[key] = 1;
        VERIFY(frame());
        fake_platform_keys[key] = 0;
        VERIFY(!frame());
    }
    // BEA_PORT_COMMANDER_VIEW=messages (or =ticks) reads the trap's record instead
    gStrategicMessageKeys = true;
    fake_platform_keys[DIK_RIGHT] = 1;
    VERIFY(!frame());
    gDown[DIK_RIGHT] = true; fake_platform_keys[DIK_RIGHT] = 0;
    VERIFY(frame());
    gDown[DIK_RIGHT] = false; gStrategicMessageKeys = false; fake_platform_keys[DIK_RIGHT] = -1;
    // the paused map's pan, a frame at a time
    Close(); Fixture(); Open(); SetView(500, 500, 100);
    fake_ms = 2000; MapRenderNavigation();
    const float x0 = gViewX;
    fake_platform_keys[DIK_RIGHT] = 1;
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(gViewX > x0);
    const float x1 = gViewX;
    gDown[DIK_RIGHT] = true; fake_platform_keys[DIK_RIGHT] = 0;
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(gViewX == x1);
    fake_platform_keys[DIK_RIGHT] = -1;
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(gViewX > x1);
    gDown[DIK_RIGHT] = false;
    Close();
}

// Each camera key moves the view the way it did before the camera read the keyboard's state: the five axes the camera is given, the camera's answer to
// three of them, and the paused map's pan (frame-feel review: the sign of the orbit, tilt and vertical keys was not pinned by any check).
void KeyDirections() {
    Fixture(); Open(true); gDown[kOpenKey] = false;
    gStrategicTickOnly = false; gStrategicAdvancedAt = ~0u;
    fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
    struct KeyAxis { WORD key; double CeCommanderCameraInput::*axis; double sign; };
    const KeyAxis table[] = {
        {DIK_RIGHT, &CeCommanderCameraInput::pan_horizontal, +1}, {DIK_LEFT, &CeCommanderCameraInput::pan_horizontal, -1},
        {DIK_UP, &CeCommanderCameraInput::pan_vertical, +1}, {DIK_W, &CeCommanderCameraInput::pan_vertical, +1},
        {DIK_DOWN, &CeCommanderCameraInput::pan_vertical, -1}, {DIK_S, &CeCommanderCameraInput::pan_vertical, -1},
        {DIK_E, &CeCommanderCameraInput::orbit_horizontal, +1}, {DIK_Q, &CeCommanderCameraInput::orbit_horizontal, -1},
        {DIK_NEXT, &CeCommanderCameraInput::orbit_vertical, +1}, {DIK_PRIOR, &CeCommanderCameraInput::orbit_vertical, -1},
        {DIK_R, &CeCommanderCameraInput::zoom, +1}, {DIK_F, &CeCommanderCameraInput::zoom, -1},
    };
    for (const KeyAxis& entry : table) {
        // one key held (the platform's record), one frame later: its axis alone, in its direction
        fake_platform_keys[entry.key] = 1;
        fake_ms += 1000.0 / 60;
        const CeCommanderCameraInput input = StrategicInput();
        fake_platform_keys[entry.key] = -1;
        const double others = std::fabs(input.pan_horizontal) + std::fabs(input.pan_vertical) + std::fabs(input.orbit_horizontal) +
                              std::fabs(input.orbit_vertical) + std::fabs(input.zoom);
        VERIFY(input.*(entry.axis) == entry.sign && others == 1.0);
        // and the same key as the key trap heard it, where the platform cannot say
        gDown[entry.key] = true;
        fake_ms += 1000.0 / 60;
        const CeCommanderCameraInput heard = StrategicInput();
        gDown[entry.key] = false;
        VERIFY(heard.*(entry.axis) == entry.sign);
    }
    // opposite keys cancel
    fake_platform_keys[DIK_E] = fake_platform_keys[DIK_Q] = fake_platform_keys[DIK_NEXT] = fake_platform_keys[DIK_PRIOR] = 1;
    fake_ms += 1000.0 / 60;
    const CeCommanderCameraInput both = StrategicInput();
    fake_platform_keys[DIK_E] = fake_platform_keys[DIK_Q] = fake_platform_keys[DIK_NEXT] = fake_platform_keys[DIK_PRIOR] = -1;
    VERIFY(both.orbit_horizontal == 0 && both.orbit_vertical == 0);
    // the camera's answer, over six frames at 60 a second: E turns it one way and Q the other, PgDn tilts it down and PgUp up, R brings it in
    auto answer = [&](WORD key, const float& quantity) {
        Fixture(); Open(true); gDown[kOpenKey] = false;
        gStrategicTickOnly = false; gStrategicAdvancedAt = ~0u;
        fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
        const float before = quantity;
        fake_platform_keys[key] = 1;
        for (int i = 0; i < 6; ++i) { fake_ms += 1000.0 / 60; PortCeCmdRenderCamera(&original_camera, &player); }
        fake_platform_keys[key] = -1;
        return quantity - before;
    };
    VERIFY(answer(DIK_E, gStrategicYaw) > 0.05f && answer(DIK_Q, gStrategicYaw) < -0.05f);
    VERIFY(answer(DIK_NEXT, gStrategicPitch) > 0.05f && answer(DIK_PRIOR, gStrategicPitch) < -0.05f);
    VERIFY(answer(DIK_R, gViewSpan) < 0.f);
    // the paused map: DOWN pans it toward larger y and UP toward smaller, one frame of 1/60 s moving it 1.2 of a span of 100
    for (int up = 0; up < 2; ++up) {
        const WORD key = up ? DIK_UP : DIK_DOWN;
        Fixture(); Open(); SetView(500, 500, 100);
        fake_ms = 2000; MapRenderNavigation();
        const float y0 = gViewY;
        fake_platform_keys[key] = 1;
        fake_ms += 1000.0 / 60; MapRenderNavigation();
        VERIFY(Near(gViewY - y0, up ? -1.2f : 1.2f));
        fake_platform_keys[key] = -1;
        Close();
    }
}

// A clock reading of 2^k milliseconds has a grain of 2^(k-23) ms once a double of it is rounded to the 24 bits the game's x87 unit keeps: 4 ms after ten hours
// of uptime, 8 ms after nineteen, 16 ms after two days (and 32 ms after three). The camera must still move by the time that has passed, whatever the uptime,
// and so must the paused map's pan.
void TwoDaysOfUptime() {
    const unsigned long long saved = fake_base_ns;
    for (unsigned long long base : {36000000000000ull, 70000123456789ull, 177578900000000ull, 300000000000000ull, 900000123456789ull}) {
        fake_base_ns = base;
        for (double frame_ms : {1000.0 / 144, 1000.0 / 60, 1000.0 / 240}) {
            Fixture(); Open(true); gDown[kOpenKey] = false;
            gStrategicTickOnly = false; gStrategicAdvancedAt = ~0u;
            fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
            Trap(DIK_RIGHT, KEY_EVENT_DOWN);
            FVector last = gStrategicCamera.mPos;
            float first = 0, total = 0;
            for (int i = 0; i < 60; ++i) {
                fake_ms += frame_ms;
                VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicCamera);
                const float step = Travel(gStrategicCamera.mPos, last);
                last = gStrategicCamera.mPos;
                if (i == 0) first = step;
                total += step;
                VERIFY(step > 0.0f);                                                    // a frame in which it did not move is a clock of the wrong grain
                VERIFY(std::fabs(step - first) < 0.004f * first + 0.0005f);              // and every frame moves it by the same amount
            }
            Trap(DIK_RIGHT, KEY_EVENT_UP);
            // the whole run covered the time that passed (the pan is one view span a second, the span unchanged by a pan)
            VERIFY(std::fabs(total - gViewSpan * static_cast<float>(60 * frame_ms * 0.001)) < 0.01f * total + 0.01f);
        }
    }
    // the paused map: the same frames pan it by the same amount each
    for (unsigned long long base : {36000000000000ull, 177578900000000ull, 900000123456789ull}) {
        fake_base_ns = base;
        Fixture(); Open(); SetView(500, 500, 100);
        gDown[DIK_RIGHT] = true;
        fake_ms = 2000; MapRenderNavigation(); // the first call only starts the interval
        float x = gViewX, first = 0;
        for (int i = 0; i < 60; ++i) {
            fake_ms += 1000.0 / 144; MapRenderNavigation();
            const float step = gViewX - x;
            x = gViewX;
            if (i == 0) first = step;
            VERIFY(step > 0.0f && std::fabs(step - first) < 0.004f * first + 0.0005f);
        }
        gDown[DIK_RIGHT] = false;
    }
    fake_base_ns = saved;
}

void MapPanTiming() {
    // The real render call and pan body must give the same one-second hold
    // at different refresh rates, retaining the previous speed at 60 FPS.
    for (int rate : {30, 60, 144}) {
        std::vector<double> frames;
        for (int i = 1; i <= rate; ++i) frames.push_back(1000.0 * i / rate);
        VERIFY(Near(MapPanFor(frames), 72.0f));
    }
    std::vector<double> irregular;
    const double gaps[] = {5, 17, 33, 11, 90, 10, 44, 75, 23, 92};
    for (double time = 0; time < 1000;) {
        for (double gap : gaps) {
            time = std::min(1000.0, time + gap); irregular.push_back(time);
            if (time == 1000) break;
        }
    }
    VERIFY(Near(MapPanFor(irregular), 72.0f));

    Fixture(); Open(); SetView(500, 500, 100);
    const float start = gViewX;
    fake_ms = 2000; MapRenderNavigation(); // idle time is not owed to a later press
    VERIFY(gViewX == start);
    gDown[DIK_RIGHT] = true;
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(Near(gViewX - start, 1.2f));
    const float released = gViewX;
    VERIFY(Trap(DIK_RIGHT, KEY_EVENT_UP)); // actual paused-map release path
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(gViewX == released);

    gDown[DIK_RIGHT] = gDown[DIK_LEFT] = true;
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(gViewX == released); // opposing inputs cancel
    gDown[DIK_RIGHT] = false;
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(Near(gViewX - released, -1.2f));

    gDown[DIK_LEFT] = false; gDown[DIK_RIGHT] = true;
    const float before_stall = gViewX;
    fake_ms += 5000; MapRenderNavigation();
    VERIFY(Near(gViewX - before_stall, 7.2f)); // bounded after a suspended or stalled frame
    const float before_backwards = gViewX;
    fake_ms -= 10; MapRenderNavigation();
    VERIFY(gViewX == before_backwards);
    Close();
    const float closed = gViewX;
    fake_ms += 5000; MapRenderNavigation();
    VERIFY(gViewX == closed);
    Open(); SetView(500, 500, 100); gDown[DIK_RIGHT] = true;
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(Near(gViewX - 450, 1.2f)); // reopening does not reuse the closed interval
    Close();

    Fixture(); Open(true); SetView(500, 500, 100); gDown[DIK_RIGHT] = true;
    fake_ms += 1000.0 / 60; MapRenderNavigation();
    VERIFY(gViewX == 450); // this path never moves the live strategic view
    Close();
}

void AuthoredWorldView(){
    authored_view=CeWorldViewSample();
    for(int i=0;i<4;++i)authored_view.view[i*4+i]=1;
    authored_view.view[12]=-200;authored_view.view[13]=-220;authored_view.view[14]=100;
    authored_view.projection[0]=.01f;authored_view.projection[5]=-1.f/75.f;
    authored_view.projection[10]=.005f;authored_view.projection[15]=1;
    authored_view.viewport={0,0,800,600,0,1};authored_view_valid=true;
}
void ClickWorld(int button,int x,int y){MOUSE_X=x;MOUSE_Y=y;Trap(WORD(button),kMouseDown);Trap(WORD(button),kMouseUp);}
void LivePointerInteractions(){
    Fixture();VERIFY(PortCeCmdKey(DIK_M,false));
    VERIFY(gOpen && gStrategic && !GAME.paused && GAME.pause_calls==0);
    AuthoredWorldView();
    ClickWorld(0,320,300);VERIFY(gSelected==5);
    ClickWorld(2,480,340);VERIFY(accepted_orders==1 && last_order_verb==CE_CMD_MOVE);
    VERIFY(Near(last_order_x,220) && Near(last_order_y,230));
    TapLiveKey(DIK_A);VERIFY(gArmed);
    ClickWorld(0,500,360);VERIFY(accepted_orders==2 && last_order_verb==CE_CMD_ADVANCE && !gArmed);
    const int before=accepted_orders;
    ClickWorld(2,480,570);VERIFY(accepted_orders==before); // panel cannot issue a world order
    ClickWorld(0,12+2*112+20,600-43+10);VERIFY(accepted_orders==before+1 && last_order_verb==CE_CMD_HOLD);
    ClickWorld(0,12+20,600-43+10);VERIFY(gArmed && gStrategicClickOrder==CE_CMD_MOVE);
    ClickWorld(0,520,350);VERIFY(last_order_verb==CE_CMD_MOVE && !gArmed);
    // Armed commands and pointer selection must revalidate after world changes.
    live_squads.erase(live_squads.begin());const int attempts=issue_attempts;
    ClickWorld(2,480,340);VERIFY(gSelected==-1 && issue_attempts==attempts);
    ClickWorld(0,560,460);VERIFY(gSelected==17);
    authored_view_valid=false;ClickWorld(2,480,340);VERIFY(issue_attempts==attempts);
    AuthoredWorldView();live_squads[0].ordinal=90;ClickWorld(2,480,340);
    VERIFY(gSelected==-1 && issue_attempts==attempts);
    ClickWorld(0,560,460);VERIFY(gSelected==90);
    live_squads[0].friendly=live_squads[0].commandable=false;ClickWorld(2,480,340);
    VERIFY(gSelected==-1 && issue_attempts==attempts);

    Fixture();Open(true);AuthoredWorldView();ClickWorld(0,320,300);
    pilot.life=0;ClickWorld(2,480,340);VERIFY(!gOpen && accepted_orders==0);
    Fixture();Open(true);AuthoredWorldView();ClickWorld(0,320,300);
    ++pilot.creation_number;ClickWorld(2,480,340);VERIFY(!gOpen && accepted_orders==0);
    Fixture();Open(true);AuthoredWorldView();
    CeViewPoint hit={};
    MAP.slopeX=.1f;MAP.height=-20;
    VERIFY(StrategicGround(authored_view,480,340,&hit));
    VERIFY(Near(hit.x,220) && Near(hit.y,230) && Near(hit.z,2));
    MAP.height=-200;VERIFY(!StrategicGround(authored_view,480,340,&hit)); // ray starts underground
    MAP=AuthoredMap();
    CeWorldViewSample sky=authored_view;sky.view[14]=-100;
    VERIFY(!StrategicGround(sky,480,340,&hit)); // forward ray stays below surface
    VERIFY(!StrategicGround(authored_view,-1,340,&hit));
    const std::uint64_t oldSession=gStrategicViewSession;
    Close();Open(true);VERIFY(gStrategicViewSession!=oldSession && !authored_view_valid);
    TapLiveKey(DIK_ESCAPE);VERIFY(!gOpen && !GAME.paused);
}

void LiveSquadInteractions() {
    Fixture();
    VERIFY(!Trap(DIK_TAB, KEY_EVENT_DOWN) && accepted_orders == 0 && list_reads == 0);
    Open(true);
    Trap(DIK_TAB, KEY_EVENT_DOWN);
    VERIFY(gSelected == 5);
    const int read_after_pick = list_reads;
    Trap(DIK_TAB, KEY_EVENT_DOWN);
    VERIFY(gSelected == 5 && list_reads == read_after_pick);
    Trap(DIK_TAB, KEY_EVENT_UP); TapLiveKey(DIK_TAB);
    VERIFY(gSelected == 17);
    Trap(DIK_LSHIFT, KEY_EVENT_DOWN); TapLiveKey(DIK_TAB); Trap(DIK_LSHIFT, KEY_EVENT_UP);
    VERIFY(gSelected == 5);
    Trap(DIK_RSHIFT, KEY_EVENT_DOWN); TapLiveKey(DIK_TAB); Trap(DIK_RSHIFT, KEY_EVENT_UP);
    VERIFY(gSelected == 17);
    TapLiveKey(DIK_C);
    VERIFY(Near(gViewX + gViewSpan * 0.5f, live_squads[1].x));
    VERIFY(Near(gViewY + gViewSpan * 0.5f, live_squads[1].y));
    VERIFY(!GAME.paused && GAME.controller.target == &pilot);
    Trap(DIK_H, KEY_EVENT_DOWN);
    VERIFY(accepted_orders == 1 && last_ordered == 17 && live_squads[1].verb == CE_CMD_HOLD);
    Trap(DIK_H, KEY_EVENT_DOWN);
    VERIFY(issue_attempts == 1 && accepted_orders == 1);
    Trap(DIK_H, KEY_EVENT_UP); TapLiveKey(DIK_X);
    VERIFY(accepted_orders == 2 && last_ordered == 17);
    live_squads[1].members = 2;
    PortCeCmdFrame(); messages.clear(); text_y.clear(); StrategicRenderPrefix();
    VERIFY(std::any_of(messages.begin(), messages.end(), [](const std::string& s) {
        return s.find("Squad 17 | Authored squad 1 | 2 units") != std::string::npos;
    }));
    VERIFY(std::any_of(messages.begin(), messages.end(), [](const std::string& s) {
        return s.find("Order: Holding") != std::string::npos;
    }));
    live_squads.pop_back(); PortCeCmdFrame();
    VERIFY(gSelected == -1);
    messages.clear(); StrategicRenderPrefix();
    VERIFY(std::find(messages.begin(), messages.end(), "No squad selected") != messages.end());

    // The display is stale on purpose. Every action must obtain a new list,
    // not rely on a render or frame happening between world change and input.
    Fixture(); Open(true); TapLiveKey(DIK_TAB);
    live_squads.erase(live_squads.begin()); TapLiveKey(DIK_H);
    VERIFY(gSelected == -1 && issue_attempts == 0 && accepted_orders == 0);
    VERIFY(std::string(gNote) == "Pick a squad first.");

    Fixture(); Open(true); TapLiveKey(DIK_TAB);
    // A new squad in the same list slot has a new lifetime ordinal. The
    // adapter must not silently transfer the previous selection to it.
    live_squads[0].ordinal = 89; TapLiveKey(DIK_H);
    VERIFY(gSelected == -1 && issue_attempts == 0 && accepted_orders == 0);

    Fixture(); Open(true); TapLiveKey(DIK_TAB);
    live_squads[0].friendly = live_squads[0].commandable = false;
    TapLiveKey(DIK_H);
    VERIFY(gSelected == -1 && issue_attempts == 0);
    TapLiveKey(DIK_TAB); VERIFY(gSelected == 17);
    live_squads[1].commandable = false; live_squads[1].why = CE_WHY_SCRIPTED;
    const float before_x = gViewX, before_y = gViewY;
    TapLiveKey(DIK_C);
    VERIFY(gSelected == -1 && Near(gViewX, before_x) && Near(gViewY, before_y));
    TapLiveKey(DIK_TAB); VERIFY(gSelected == -1 && issue_attempts == 0);

    Fixture(); Open(true); TapLiveKey(DIK_TAB); refuse_next_order = true;
    TapLiveKey(DIK_H);
    VERIFY(issue_attempts == 1 && accepted_orders == 0);
    VERIFY(std::string(gNote) == "Squad no longer takes orders.");
    VERIFY(live_squads[0].verb == CE_CMD_NONE);

    Fixture(); Open(true); TapLiveKey(DIK_TAB); pilot.life = 0;
    TapLiveKey(DIK_H);
    VERIFY(!gOpen && accepted_orders == 0 && issue_attempts == 0);
    Fixture(); Open(true); TapLiveKey(DIK_TAB); GAME.ordinary = false;
    TapLiveKey(DIK_H);
    VERIFY(!gOpen && accepted_orders == 0 && issue_attempts == 0);
    Fixture(); Open(true); TapLiveKey(DIK_TAB); LT.SetKeytrap(OtherTrap);
    TapLiveKey(DIK_H);
    VERIFY(!gOpen && LT.GetKeytrap() == OtherTrap && issue_attempts == 0);
    Fixture(); Open(true); TapLiveKey(DIK_TAB); ++pilot.creation_number;
    TapLiveKey(DIK_H);
    VERIFY(!gOpen && accepted_orders == 0 && issue_attempts == 0);
}

// Direct3D leaves the game's x87 unit at single precision (port/platform/common/d3d9hooks.cpp); the host build for it does the same, so the
// map's arithmetic runs under the numbers it has in the game.
#if defined(__i386__) && !defined(__SSE_MATH__)
#define CE_X87_SINGLE_PRECISION 1
void SetX87SinglePrecision() {
    unsigned short control;
    __asm__ volatile("fnstcw %0" : "=m"(control));
    control = static_cast<unsigned short>(control & ~0x0300);
    __asm__ volatile("fldcw %0" : : "m"(control));
}
#endif

int main() {
#ifdef CE_X87_SINGLE_PRECISION
    SetX87SinglePrecision();
#endif
    Fixture();
#ifdef CE_CAMERA_EDITOR_HUD_QUERY
    VERIFY(!PortCePhotoOn() && !PortCeCmdSuppressFlightHud());
    { CePhotoDrawScope ordinary; VERIFY(draws_dropped == 0); }
    editor_hud = true;
    {
        CePhotoDrawScope editor_only;
        VERIFY(draws_dropped == 1);
        { CePhotoDrawScope nested; VERIFY(draws_dropped == 2); }
        VERIFY(draws_dropped == 1);
    }
    VERIFY(draws_dropped == 0);
    editor_hud = false;
    { CePhotoDrawScope restored; VERIFY(draws_dropped == 0); }
#endif
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    VERIFY(PresentationCamera(&original_camera, &player) == &original_camera);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter);
    VERIFY(!Trap(DIK_V, KEY_EVENT_DOWN) && !gOpen);
    GAME.ordinary = false;
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    GAME.ordinary = true;
    Open();
    VERIFY(gOpen && !gStrategic && GAME.paused && GAME.pause_calls == 1);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter);
    VERIFY(GAME.controller.target == &gSink);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    const FVector before = original_camera.mPos;
    const float fitted_x = gViewX, fitted_y = gViewY, fitted_span = gViewSpan;
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN));
    VERIFY(gOpen && gStrategic && gStrategicOwner.Active());
    VERIFY(!GAME.paused && GAME.pause_calls == 1);
    VERIFY(GAME.controller.target == &pilot && pilot.life == 10);
    VERIFY(SamePosition(original_camera.mPos, before));
    VERIFY(PortCeCmdSuppressFlightHud() && draw_filter && !PortCePhotoOn());
    {
        CePhotoDrawScope cockpit_or_flight_overlay;
        VERIFY(draws_dropped == 1);
        { CePhotoDrawScope nested; VERIFY(draws_dropped == 2); }
        VERIFY(draws_dropped == 1);
    }
    VERIFY(draws_dropped == 0); // Message-box draws outside the flight scope remain visible.
    VERIFY(Near(gViewX, fitted_x) && Near(gViewY, fitted_y) && Near(gViewSpan, fitted_span));
    const FVector initial_view = gStrategicCamera.mPos;
    for (int i = 0; i < 120; ++i) { fake_ms += 16; PortCeCmdFrame(); }
    VERIFY(SamePosition(gStrategicCamera.mPos, initial_view));
    VERIFY(Near(gViewX, fitted_x) && Near(gViewY, fitted_y));
    const FVector forward = gStrategicCamera.mOrientation * FVector(0, 1, 0);
    VERIFY(Near(gStrategicCamera.mPos.X + forward.X * gViewSpan * 0.8f,
                fitted_x + fitted_span * 0.5f));
    VERIFY(Near(gStrategicCamera.mPos.Y + forward.Y * gViewSpan * 0.8f,
                fitted_y + fitted_span * 0.5f));
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicCamera);
#if defined(CE_VERSION)
    VERIFY(PresentationCamera(&original_camera, &player) == &gStrategicCamera);
#else
    VERIFY(PresentationCamera(&original_camera, &player) == &original_camera);
#endif
    CGenericCamera another_camera; CPlayer another_player;
    VERIFY(PortCeCmdRenderCamera(&another_camera, &player) == &another_camera);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &another_player) == &original_camera);
    VERIFY(PresentationCamera(&another_camera, &player) == &another_camera);
    VERIFY(PresentationCamera(&original_camera, &another_player) == &original_camera);
    GAME.Tick();
    VERIFY(GAME.simulation_ticks == 1 && pilot.life == 9);
    Trap(DIK_V, KEY_EVENT_DOWN); // held entry key does not bounce back to the map
    VERIFY(gStrategic);
    Trap(DIK_W, KEY_EVENT_DOWN); fake_ms += 50;
    const FVector strategic_before = gStrategicCamera.mPos;
    PortCeCmdFrame();
    VERIFY(!SamePosition(strategic_before, gStrategicCamera.mPos));
    VERIFY(gViewY > fitted_y && Near(gViewX, fitted_x));
    VERIFY(SamePosition(original_camera.mPos, before));
    VERIFY(GAME.controller.target == &pilot && !GAME.paused);
    StrategicRenderPrefix();
    VERIFY(draw_starts == 1 && draw_ends == 1);
    VERIFY(std::any_of(messages.begin(), messages.end(), [](const std::string& m) {
        return m.find("Esc pilot") != std::string::npos;
    }));
    VERIFY(cursor_draws==1 && !text_y.empty() && text_y[0]==8*L.s);
    Trap(DIK_W, KEY_EVENT_UP);
    const float pan_centre_x = gViewX + gViewSpan * 0.5f;
    const float pan_centre_y = gViewY + gViewSpan * 0.5f;
    const float span_before_zoom = gViewSpan;
    Trap(DIK_R, KEY_EVENT_DOWN); fake_ms += 20; PortCeCmdFrame();
    Trap(DIK_R, KEY_EVENT_UP);
    VERIFY(gViewSpan < span_before_zoom);
    VERIFY(Near(gViewX + gViewSpan * 0.5f, pan_centre_x));
    VERIFY(Near(gViewY + gViewSpan * 0.5f, pan_centre_y));
    const float panned_x = gViewX, panned_y = gViewY;
    const FVector panned_view = gStrategicCamera.mPos;
    for (int i = 0; i < 20; ++i) { fake_ms += 16; PortCeCmdFrame(); }
    VERIFY(SamePosition(gStrategicCamera.mPos, panned_view));
    Trap(DIK_V, KEY_EVENT_UP); Trap(DIK_V, KEY_EVENT_DOWN);
    VERIFY(gOpen && !gStrategic && GAME.paused && GAME.pause_calls == 2);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    VERIFY(PresentationCamera(&original_camera, &player) == &original_camera);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter && draws_dropped == 0);
    VERIFY(Near(gViewX, panned_x) && Near(gViewY, panned_y));
    Close(); Open(true); // The map's key-up handler is outside this extracted input fragment.
    VERIFY(gStrategic && Near(gViewX, panned_x) && Near(gViewY, panned_y));
    Close();
    VERIFY(!gOpen && !GAME.paused && GAME.controller.target == &pilot);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter);

    // M after V: a switch keeps the trap's record of held keys, so the first M after a V closes the view the V opened,
    // while an M still held from the press that opened the map stays a repeat.
    Fixture(); Open();
    VERIFY(gOpen && !gStrategic && gDown[kOpenKey]); // the M that opened the map from the battle may repeat
    gDown[kOpenKey] = false; // its release: the paused map's key-up handler is outside this extracted fragment
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN) && gOpen && gStrategic && gDown[DIK_V]);
    Trap(DIK_V, KEY_EVENT_UP);
    VERIFY(Trap(DIK_M, KEY_EVENT_DOWN)); // the first M after the V
    VERIFY(!gOpen && !gStrategic && !gStrategicOwner.OwnsCapture() && !GAME.paused);
    VERIFY(GAME.controller.target == &pilot);

    Fixture(); Open(); gDown[kOpenKey] = false;
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN) && gStrategic);
    Trap(DIK_V, KEY_EVENT_UP);
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN) && gOpen && !gStrategic && GAME.paused);
    // Back on the paused map, its own key handler (outside this fragment) takes a key-down as a press when this record
    // says the key is up: the next M closes.
    VERIFY(gDown[DIK_V] && !gDown[kOpenKey]);

    Fixture(); Open(); // M still held from the press that opened the map
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN) && gStrategic);
    Trap(DIK_M, KEY_EVENT_DOWN); // that M's auto-repeat
    VERIFY(gOpen && gStrategic);
    Trap(DIK_M, KEY_EVENT_UP); Trap(DIK_M, KEY_EVENT_DOWN);
    VERIFY(!gOpen && !gStrategic);

    Fixture(); Open(true);
    VERIFY(!GAME.paused && GAME.pause_calls == 0 && GAME.controller.target_changes == 0);
    Trap(DIK_ESCAPE, KEY_EVENT_DOWN);
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture() && gEscClosed);
    VERIFY(GameAccess::pause_field_reads == 0 && GAME.controller.target_changes == 0);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);

    Fixture(); Open(true); GAME.ordinary = false;
    PortCeCmdFrame();
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture());
    VERIFY(!GAME.paused && GAME.controller.target == &pilot);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);

    Fixture(); Open(true); photo_on = true;
    Close();
    VERIFY(!PortCeCmdSuppressFlightHud() && draw_filter && PortCePhotoOn());

    Fixture(); Open(true);
    CBattleEngine replacement_pilot; CPlayer replacement_player;
    replacement_player.pilot = &replacement_pilot;
    CGenericCamera replacement_camera;
    GAME.player = &replacement_player; GAME.camera = &replacement_camera;
    GAME.controller.target = &replacement_pilot;
    VERIFY(PortCeCmdRenderCamera(&replacement_camera, &replacement_player) == &replacement_camera);
    StrategicRenderPrefix();
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture());
    VERIFY(GAME.controller.target == &replacement_pilot && !GAME.paused);

    Fixture(); Open(true); ++pilot.creation_number;
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    StrategicRenderPrefix();
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture());
    VERIFY(GAME.controller.target == &pilot && !GAME.paused);

    Fixture(); Open(true); pilot.life = 0;
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    StrategicRenderPrefix();
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture());
    VERIFY(GAME.controller.target == &pilot && !GAME.paused);

    Fixture(); Open(true); GAME.controller.trap = OtherTrap;
    const int starts_before = mouse_starts;
    PortCeCmdFrame();
    VERIFY(!gOpen && GAME.controller.trap == OtherTrap && mouse_starts == starts_before);
    VERIFY(GAME.controller.target == &pilot);

    Fixture(); Open(true); const std::uint64_t mission_before = gMissionLifetime;
    PortCeCmdLevelEnd();
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture() && gMissionLifetime != mission_before);
    VERIFY(!GAME.paused && GAME.controller.target == &pilot);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    LiveSquadInteractions();
    LivePointerInteractions();
    MapPanTiming();
    RenderTimeCamera();
    TwoDaysOfUptime();
    KeysAsHeldNow();
    KeyDirections();
    std::cout << "source-bound commander camera integration controls passed"
#ifdef CE_X87_SINGLE_PRECISION
              << " (x87 unit at single precision)"
#endif
              << "\n";
    return 0;
}
#endif
