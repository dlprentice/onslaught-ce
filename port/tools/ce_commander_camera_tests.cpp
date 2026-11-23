#include "ce_hud_reservation.h"
#include "ce_commander_transition.h"
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
#include <cstdio>
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
#include "camera_platform_keys.inc"
enum { KEY_EVENT_UP = 41, KEY_EVENT_DOWN };
#include "ce_dodge_binding.h"
#include "ce_cmdproduction.h"
#include "ce_entity_handles.h"
#include "ce_cmd_hud.h"
#include "ce_cmd_voice.h"
void CeCmdVoicePlay(CeCmdVoiceCue, std::uint64_t) {}
void CeCmdVoiceStop() {}
#include "ce_playunit.h"
// Unavailable-delivery fixture declarations also support the CE-off negative build.
bool CePlayUnitCanTakeDelivery();
bool CePlayUnitTakeDelivery();

// Boundaries for the production key preference and conflict readers. The action
// key lookup, default, setting validation and conflict decisions are not simulated.
std::string camera_commander_setting;
extern "C" int CE_SettingGetText(const char* name, char* out, int capacity) {
    if (std::strcmp(name, "commander_key") || camera_commander_setting.empty() ||
            capacity <= static_cast<int>(camera_commander_setting.size())) return 0;
    std::snprintf(out, capacity, "%s", camera_commander_setting.c_str());
    return 1;
}
extern "C" int CE_SettingSetText(const char* name, const char* value) {
    if (std::strcmp(name, "commander_key")) return 0;
    camera_commander_setting = value;
    return 1;
}
extern "C" int PortCeTutorialPrintableKey(int key, char* out, int capacity) {
    if (capacity < 2 || (key != DIK_M && key != DIK_T)) return 0;
    out[0] = key == DIK_T ? 'T' : 'M';
    out[1] = '\0';
    return 1;
}
int CE_SettingGetBool(const char*, int fallback) { return fallback; }
bool PortCeExperimental(const char* mode) { return !std::strcmp(mode, "commander"); }
enum { KEY_ON, KEY_ONCE, KEY_RELEASE, BUTTON_MECH_STRAFE_LEFT, BUTTON_MECH_STRAFE_RIGHT,
    BUTTON_MECH_FORWARD, BUTTON_MECH_BACKWARD, BUTTON_MECH_FIRE_GUN_POD,
    BUTTON_MECH_CHARGE_GUN_POD, BUTTON_MECH_MORPH, BUTTON_MECH_CHANGE_WEAPON };
struct SControlInput { int mPad, mKey, mType; };
struct SControlBinding { int mId; SControlInput mInput[2]; };
SControlBinding CONTROL_BINDINGS[] = {{-1, {}}, {-1, {}}};
#include "camera_binding_conflicts.inc"
void CameraFactoryWorld(bool enabled, bool hasTankMount);
// The disabled-presentation-hook control still exercises the authored UI
// adapter. Import its real public descriptor types without enabling that hook.
#if !defined(CE_VERSION)
#define CE_CAMERA_TEST_TEMPORARY_VERSION
#define CE_VERSION "authored-types"
#endif
#include "ce_commander.h"
#include "ce_cmdview.h"
#include "ce_command_selection.h"
#include "ce_cmdorders.h"
#include "ce_cmdpointer.h"
#include "ce_world_view.h"
#include "ce_cmdsel.h"
#include "ce_cmdgroup.h"
#include "ce_cmdoutcome.h"
#include "ce_cmdmatch.h"
#include "ce_cmdrules.h"
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
    void MakeRotationYawF(float y) { yaw = y; pitch = 0;  SyncRows(); }
    void MakeRotationF(float y, float p, float) { yaw = y; pitch = p;  SyncRows(); }
    FVector operator*(const FVector& p) const {
        const float forward = std::cos(pitch)*p.Y - std::sin(pitch)*p.Z;
        return FVector(std::cos(yaw)*p.X - std::sin(yaw)*forward,
                       std::sin(yaw)*p.X + std::cos(yaw)*forward,
                       std::sin(pitch)*p.Y + std::cos(pitch)*p.Z);
    }

    FVector Row[3] = {FVector(1,0,0), FVector(0,1,0), FVector(0,0,1)};
    void SyncRows() {
        FVector f = (*this) * FVector(0,1,0);
        const float length = sqrtf(f.X*f.X+f.Y*f.Y+f.Z*f.Z);
        if (length > 0.0f) { f.X/=length; f.Y/=length; f.Z/=length; }
        FVector right(f.Y,-f.X,0.0f);
        const float horizontal = sqrtf(right.X*right.X+right.Y*right.Y);
        if (horizontal > 0.00001f) { right.X/=horizontal; right.Y/=horizontal; }
        else right = FVector(cosf(yaw),sinf(yaw),0.0f);
        const FVector up(right.Y*f.Z-right.Z*f.Y, right.Z*f.X-right.X*f.Z, right.X*f.Y-right.Y*f.X);
        Row[0]=FVector(right.X,f.X,up.X); Row[1]=FVector(right.Y,f.Y,up.Y); Row[2]=FVector(right.Z,f.Z,up.Z);
    }
};
struct CCamera {
    virtual ~CCamera() {}
    virtual float GetZoom() { return 1; }
    virtual float GetYaw() { return 0; }

    virtual FVector GetPos() const { return FVector(); }
    virtual FMatrix GetOrientation() const { return FMatrix(); }
};
struct CGenericCamera : CCamera {
    FVector mPos;
    FMatrix mOrientation;
    float mZoom = 1;
    float GetZoom() override { return mZoom; }
    float GetYaw() override { return mOrientation.yaw; }

    FVector GetPos() const override { return mPos; }
    FMatrix GetOrientation() const override { return mOrientation; }
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
unsigned gCloseKey = 0;
const int kMouseUp=3, kMouseDown=4;
int MOUSE_X=400, MOUSE_Y=300;
struct AuthoredMap { int mGridWidth=1000,mGridHeight=1000; float height=0,slopeX=0,slopeY=0; float water=1000.f; /* Z grows downward: water far below the ground */ float GetWaterLevel(){return water;} float Collide(const FVector& p){return height+slopeX*p.X+slopeY*p.Y;} } MAP;
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
enum { GAME_STATE_PLAYING = 3 };
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
    int GetGameState() { return 3; }		// the game's playing state
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
// The live view's own pointer (ce_cmdmap.cpp, outside the block): its start as the view opens is counted; the scope that holds MOUSE_X and MOUSE_Y
// at that pointer for one entry does nothing here, where the authored pointer is written to them directly.
int live_pointer_starts = 0;
void LivePointerStart() { ++live_pointer_starts; }
struct LivePointer { LivePointer() {} };
// The Commander match's level end (ce_cmdmatch.cpp): counted.
int match_level_ends = 0;
void CeCmdMatchLevelEnd() { ++match_level_ends; }
struct Sink {} gSink;
bool gOpen = false, gOpening = false, gShift = false, gLayoutLogged = false;
bool gEscClosed = false, gDown[256] = {};
int gArmed = 0, gSelected = 0, gHover=-1, gCount = 0, gKeptLevel = -1;
// The selection and the numbered groups (ce_cmdmap.cpp's own records; the functions that keep them are the real ones, extracted).
CeCmdSel gSel = {};
CeCmdGroups gGroups = {};
// The Commander match (ce_cmdmatch.h): the battle as the live view's header shows it, and the match's own step after each frame, counted (it is
// ce_cmdmap.cpp's MatchFrame, outside the extracted text; its rules are the match runner's to show in a game).
CeCmdMatchState gMatch = {};
bool gReopen = false, gLiveLast = false, gPlayerClosed = false;
// The paused tactical map is a tester's page (ce.ini's commander_tactical_map): the fixture says whether it is allowed.
bool tactical_allowed = true;
bool TacticalMapAllowed() { return tactical_allowed; }
bool MatchBase(float*, float*, float*, float*) { return false; }	// no Commander match in this fixture: no base to fit
// When the last order was given (the view draws its line growing from then).
unsigned long long gOrderNs = 0;
int match_frames = 0;
void MatchFrame() { ++match_frames; }
// The guidance line is said on a level's first open only; the live view's bounds are its own (SetView); a match's first view is fitted to the base
// (FitLiveToBase, outside the extracted text: no match here, so the small move up the screen is made). The open key's answer while the engine is down is
// a match's (CeCmdMatch, PilotUp and NoteMay: a fixture's match and engine).
bool gToldInFight = false, gLooseBounds = false;
int fit_calls = 0;
bool FitLiveToBase() { ++fit_calls; return false; }
bool fixture_match = false, fixture_pilot_up = true;
bool CeCmdMatch() { return fixture_match; }
bool PilotUp() { return fixture_pilot_up; }
bool NoteMay() { return GAME.ordinary; }
void SelectOnly(int ordinal);
void SelectToggle(int ordinal);
void SelectAll();
void GroupKey(int number, bool keep, bool add);
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
unsigned gNoteAtUntil = 0;
static bool gCockpitPass = false;
bool StrategicLive();
float gNoteAtX = -1.f, gNoteAtY = -1.f;
bool gNoteNotYet = false;
const unsigned kNoteLength = 240;
const int kMaxSquads = 8;
const DWORD kPick = 0xffd0e8b0, kText=0xffeeeeee, kQuiet=0xff999999, kFriend=0xff00ff00, kEnemy=0xffff0000, kAdvanceColour=0xffeeaa00, kMoveColour=0xff00eeff;
const DWORD kPilot = 0xffffff00;
const DWORD kFriendDim = 0xff445566, kFocus = 0xffffff00, kOrderLine = 0x99ffffff, kPanel = 0xaf000000, kEdge = 0xff7f7f7f;
CeCmdSquad gSquads[kMaxSquads];
std::vector<CeCmdSquad> live_squads;
std::vector<CeCmdViewUnitMark> drawn_members;
const CeCmdViewUnitMark *CeCmdViewUnitMarks(unsigned &count) {
    count = static_cast<unsigned>(drawn_members.size()); return drawn_members.data();
}
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
bool unit_owns_pilot = false;
unsigned unit_map_boundaries = 0;
bool CePlayUnitOwnsPilot() { return unit_owns_pilot; }
void CePlayUnitMapBoundary() { ++unit_map_boundaries; }
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
// Real controller contracts over synthetic current native identities.
static uint64_t fixture_unit_world=1;
static int fixture_mouse[3]={0,0,0};
extern "C" int PortMouseHeldNow(unsigned b){return b<3?fixture_mouse[b]:-1;}
bool PortCeCmdMapOpen(){return gOpen;}
bool CeCmdUnitOrdersEligible(){return GAME.ordinary;}
uint64_t CeCmdUnitWorld(){return fixture_unit_world;}
void CeCmdUnitOrdersWorldEnd(){++fixture_unit_world;}
uint64_t CePlayUnitSelectionWorld(){return fixture_unit_world;}
bool CePlayUnitCanTakeSelection(uint64_t,CUnit*,int,const char**){return false;}
bool CePlayUnitTakeSelection(uint64_t,CUnit*,int,const char**){return false;}
bool CeCmdUnitVisit(const CeCommandLifetimeKey&,bool(*)(CUnit*,int,void*),void*){return false;}
bool CeCmdUnitMove(const CeCommandLifetimeKey&,float,float){return false;}
bool CeCmdUnitHold(const CeCommandLifetimeKey&){return false;}
bool CeCmdUnitReturnAI(const CeCommandLifetimeKey&){return false;}
int CeCmdUnitCollect(CeCmdUnitSnapshot* out,int cap,bool* complete){
 int n=0;*complete=true;
 for(const auto& sq:live_squads){if(!sq.friendly)continue;for(int m=0;m<sq.members;++m){
  if(n==cap){*complete=false;return n;}
  auto& u=out[n++];u=CeCmdUnitSnapshot{};u.key={fixture_unit_world,uint64_t(sq.ordinal*10+m),1};
  u.squadKey={fixture_unit_world,uint64_t(10000+sq.ordinal),1};u.ordinal=sq.ordinal;u.members=sq.members;
  u.friendly=sq.friendly;u.commandable=sq.commandable;u.groundPoint=true;u.position[0]=sq.x;u.position[1]=sq.y;
  std::snprintf(u.name,sizeof(u.name),"Authored unit %d",m);
 }}return n;
}

bool NotYet(){return false;}
bool CeCmdMapOn(){return GAME.ordinary;}
bool World() { return true; }
struct FixtureViewport {
    int width = 640, height = 480;
    int GetWindowWidth() const { return width; }
    int GetWindowHeight() const { return height; }
} PLATFORM;

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
struct IssuedOrder { int ordinal, verb; float x, y; };
std::vector<IssuedOrder> issued;
// Authored coast for checking the group's spread on a shore.
static bool shorelineFixture;
static float wetSlotX, wetSlotY;
bool CeCmdMarchLand(float x, float y) {
    return !shorelineFixture || x != wetSlotX || y != wetSlotY;
}

bool CeCmdIssue(int ordinal, int verb, float x, float y, const char** why) {
    last_order_x=x;last_order_y=y;last_order_verb=verb;issued.push_back(IssuedOrder{ordinal, verb, x, y});
    ++issue_attempts;
    if (refuse_next_order) { refuse_next_order = false; *why = "Squad no longer takes orders."; return false; }
    for (CeCmdSquad& s : live_squads) {
        if (s.ordinal == ordinal && s.commandable) {
            // the goal the order layer keeps: a march's point; a Fall back given no point goes to the authored safe point (300, 300)
            s.verb = verb; ++accepted_orders; last_ordered = ordinal;
            if (verb == CE_CMD_FALLBACK && x == 0 && y == 0) { s.gx = 300; s.gy = 300; } else { s.gx = x; s.gy = y; }
            return true;
        }
    }
    *why = "Squad disappeared."; return false;
}
void Text(float, float y, DWORD, const char* message, float = 0.f, bool = false, float = 0.f) { messages.push_back(message); text_y.push_back(y); }
// The small font's measure: eight units a character, and a line twelve high.
float TextWidth(const char* text, float scale) { return float(std::strlen(text)) * 8.f * scale; }
float LineHeight() { return 12.f * L.s; }
float CeCmdViewTextHeight(){return 12.f;}

void Box(float, float, float width, float height, DWORD) { VERIFY(width > 0 && height > 0); }
void Outline(float,float,float,float,float,DWORD){}
void Dots(float,float,float,float,float,DWORD){}
void Seg(float,float,float,float,float,DWORD){}
void CapsuleBar(float,float,float,float,float = 1.f,bool = false,int = 0){}
bool HasHudCapsuleArt(){return false;}
void DrawBeaCurvedPanel(float x,float y,float w,float h,float,DWORD fill,DWORD capTint,bool,float,bool = true){Box(x,y,w,h,fill);Outline(x,y,w,h,1.f,capTint);}
void CeCmdViewCountFrame(){}
const DWORD kCapsuleCap = 0xff38c8e8;
int cursor_draws=0;
void DrawCursor(){++cursor_draws;}
// The live view's picture (ce_cmdview.cpp behind ce_cmdmap.cpp's StrategicPictureBegin and StrategicPictureEnd): an authored double that
// counts the frames, gives back an authored view, and can be told that no picture can be made. The entry and the leaving are counted too.
CeWorldViewSample authored_view={};
bool picture_available=true;
int entered_calls=0, left_calls=0;
const void* pictured_camera=NULL;
void StrategicEntered() { ++entered_calls; }
void StrategicLeft() { ++left_calls; }
int PilotAllegiance(const void** pilot) { if (pilot) *pilot = nullptr; return 0; }
void CeCmdCountLiveUnits(int, int* outFriendly, int* outTotal) {
    if (outFriendly) *outFriendly = 0;
    if (outTotal) *outTotal = 0;
}
static CeEntityHandleTable gTestEntityHandles;
CeEntityHandle CeEntityHandleFor(const CThing* thing) {
    return thing ? gTestEntityHandles.Register(reinterpret_cast<std::uintptr_t>(thing)) : kCeEntityHandleInvalid;
}
CeEntityHandle CeEntityHandleFor(const CBattleEngine* engine) {
    if (!engine) return kCeEntityHandleInvalid;
    const std::uint64_t stamp = 0x10000ull | static_cast<std::uint64_t>(static_cast<std::uint16_t>(engine->creation_number));
    return gTestEntityHandles.Register(reinterpret_cast<std::uintptr_t>(engine), stamp);
}
CeEntityHandle CeEntityHandleLookup(const CThing* thing) {
    return thing ? gTestEntityHandles.Lookup(reinterpret_cast<std::uintptr_t>(thing)) : kCeEntityHandleInvalid;
}
CThing* CeEntityHandleResolve(CeEntityHandle handle) {
    return reinterpret_cast<CThing*>(gTestEntityHandles.Resolve(handle));
}
CUnit* CeEntityHandleResolveUnit(CeEntityHandle handle) {
    return reinterpret_cast<CUnit*>(gTestEntityHandles.Resolve(handle));
}
bool CeEntityHandleMatches(const CThing* thing, CeEntityHandle handle) {
    return thing && gTestEntityHandles.Matches(reinterpret_cast<std::uintptr_t>(thing), handle);
}
CeEntityHandle CeEntityHandleRegister(CThing* thing) {
    return thing ? gTestEntityHandles.RegisterFresh(reinterpret_cast<std::uintptr_t>(thing)) : kCeEntityHandleInvalid;
}
void CeEntityHandleUnregister(const CThing* thing) {
    if (thing) gTestEntityHandles.Unregister(reinterpret_cast<std::uintptr_t>(thing));
}
void CeEntityHandleUnregisterHandle(CeEntityHandle handle) {
    gTestEntityHandles.UnregisterHandle(handle);
}
void CeEntityHandleResetWorld() { gTestEntityHandles.ResetWorld(); }
bool StrategicPictureBegin(const CGenericCamera& camera, CeWorldViewSample* view) {
    pictured_camera=&camera;
    if(!picture_available)return false;
    ++draw_starts; *view=authored_view; return true;
}
void StrategicPictureEnd() { ++draw_ends; }
void BeginDraw() { VERIFY(false); }		// the paused map's drawing state: the live view never asks for it
void EndDraw() { VERIFY(false); }
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
// Stubs for the map's order-goal and pointer-note reads (the camera block calls them; this driver has no order layer).
bool StrategicLive() { return false; }
bool CeCmdUnitOrderRead(const CeCommandLifetimeKey&, CeCmdUnitOrderView*) { return false; }
#undef min
#undef max
#include "ce_hooks.h"
// Non-camera hook handlers referenced by port/game/ce_hooks.cpp (not exercised by this camera driver).
bool PortCeLayer() { return true; }
void PortCeGamepadBeginCycle() {}
void PortCePaletteClose() {}
void PortCeConsoleLevelEnd() {}
void PortCePhotoLevelEnd() {}
bool PortCePhotoKey(unsigned, bool) { return false; }
bool PortCePaletteSuppressDown(unsigned) { return false; }
bool PortCePaletteOpen(unsigned, bool) { return false; }
void PortCeEditorRender() {}
extern "C" void PortCeEditorFilterSave(CCareer *, void *) {}
void PortCeBonusFilterSave(CCareer *, void *) {}
extern "C" void PortCeMuseumVisitFilterSave(CCareer *, void *) {}

CCamera* PresentationCamera(CCamera* camera, CPlayer* player) {
    (void)player;
#include "ce_commander_strategic_hook.inc"
    return camera;
}

CBattleEngine pilot;
CPlayer player;
CGenericCamera original_camera;
void Fixture() {
    CancelNativeReturn();
    gNativeEntering = gNativeEntrySeeded = gNativePresented = false;
    PLATFORM.width = 640; PLATFORM.height = 480;
    CameraFactoryWorld(false, true);
    camera_commander_setting.clear();
    PortCeDodgeBindingReload();
    CONTROL_BINDINGS[0].mId = -1;
    gCloseKey = 0;
    unit_owns_pilot=false; unit_map_boundaries=0;
    drawn_members.clear(); gUnitUi.Reset(); fixture_unit_world=1;
    fixture_mouse[0]=fixture_mouse[1]=fixture_mouse[2]=0;
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
    MAP=AuthoredMap();StrategicViewInvalidate();cursor_draws=0;
    picture_available=true;entered_calls=left_calls=0;pictured_camera=NULL;L=Layout();
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
    Fixture(); Open(true); gDown[gCloseKey] = false;
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
        VERIFY(StrategicFrameCamera() == &gStrategicCamera);
        steps[i] = Travel(gStrategicCamera.mPos, last);
        last = gStrategicCamera.mPos;
        VERIFY(steps[i] > 0.01f);                                 // it moved on this frame
        VERIFY(std::fabs(steps[i] - steps[0]) < 0.002f * steps[0] + 0.0005f);        // by the same amount: it is time that moves it
    }
    // another view in the same frame (a reflection) keeps the camera the first one used
    const FVector held = gStrategicCamera.mPos;
    fake_ms = std::floor(fake_ms) + 0.999;                          // a little later, still the same Present
    VERIFY(StrategicFrameCamera() == &gStrategicCamera && SamePosition(gStrategicCamera.mPos, held));
    // an update in the same pass moves it only by the time since the frame's own move, and the next frame carries on from there
    fake_ms += 1000.0 / 60; PortCeCmdFrame();
    const FVector after_update = gStrategicCamera.mPos;
    VERIFY(Travel(after_update, held) > 0.01f);
    fake_ms += 1.0; StrategicFrameCamera();
    VERIFY(Travel(gStrategicCamera.mPos, after_update) < steps[0] * 0.2f);
    // the earlier, update-stepped camera, for the comparison
    gStrategicTickOnly = true;
    fake_ms += 1000.0 / 60;
    const FVector before_frame = gStrategicCamera.mPos;
    VERIFY(StrategicFrameCamera() == &gStrategicCamera && SamePosition(gStrategicCamera.mPos, before_frame));
    fake_ms += 50; PortCeCmdFrame();
    VERIFY(!SamePosition(gStrategicCamera.mPos, before_frame));
    gStrategicTickOnly = false;
    // keys up: it rests, frame after frame
    Trap(DIK_RIGHT, KEY_EVENT_UP);
    fake_ms += 1000.0 / 60; StrategicFrameCamera();
    const FVector rest = gStrategicCamera.mPos;
    for (int i = 0; i < 5; ++i) { fake_ms += 1000.0 / 60; StrategicFrameCamera(); VERIFY(SamePosition(gStrategicCamera.mPos, rest)); }
    // the view that is not the live one is not advanced
    Close();
    VERIFY(StrategicFrameCamera() == NULL);
    // under a virtual clock the map's time is the frames' own: 1/30 s a Present, whatever the real clock says
    fake_virtual_clock = true;
    fake_ms = 90; VERIFY(Ns() == 90ull * 33333333ull);
    fake_ms = 91; VERIFY(Ns() == 91ull * 33333333ull);
    fake_virtual_clock = false;
    fake_ms = 90; VERIFY(Ns() == fake_base_ns + 90000000ull);
}

// The camera reads the keyboard as it is held now, not as the game's last batch of key messages left it (the messages come once for every update).
void KeysAsHeldNow() {
    Fixture(); Open(true); gDown[gCloseKey] = false;
    gStrategicTickOnly = false; gStrategicAdvancedAt = ~0u;
    fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
    FVector last = gStrategicCamera.mPos;
    auto frame = [&]() { fake_ms += 1000.0 / 60; StrategicFrameCamera();
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
    Fixture(); Open(true); gDown[gCloseKey] = false;
    gStrategicTickOnly = false; gStrategicAdvancedAt = ~0u;
    fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
    struct KeyAxis { WORD key; double CeCommanderCameraInput::*axis; double sign; };
    const KeyAxis table[] = {
        {DIK_RIGHT, &CeCommanderCameraInput::pan_horizontal, +1}, {DIK_LEFT, &CeCommanderCameraInput::pan_horizontal, -1},
        {DIK_D, &CeCommanderCameraInput::pan_horizontal, +1}, {DIK_A, &CeCommanderCameraInput::pan_horizontal, -1},
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
        Fixture(); Open(true); gDown[gCloseKey] = false;
        gStrategicTickOnly = false; gStrategicAdvancedAt = ~0u;
        fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
        const float before = quantity;
        fake_platform_keys[key] = 1;
        for (int i = 0; i < 6; ++i) { fake_ms += 1000.0 / 60; StrategicFrameCamera(); }
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
            Fixture(); Open(true); gDown[gCloseKey] = false;
            gStrategicTickOnly = false; gStrategicAdvancedAt = ~0u;
            fake_ms = 1000; gStrategicLastNs = PortClockRawNs();
            Trap(DIK_RIGHT, KEY_EVENT_DOWN);
            FVector last = gStrategicCamera.mPos;
            float first = 0, total = 0;
            for (int i = 0; i < 60; ++i) {
                fake_ms += frame_ms;
                VERIFY(StrategicFrameCamera() == &gStrategicCamera);
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
    PLATFORM.width = L.w; PLATFORM.height = L.h;
    authored_view=CeWorldViewSample();
    for(int i=0;i<4;++i)authored_view.view[i*4+i]=1;
    authored_view.view[12]=-200;authored_view.view[13]=-220;authored_view.view[14]=100;
    authored_view.projection[0]=.01f;authored_view.projection[5]=-1.f/75.f;
    authored_view.projection[10]=.005f;authored_view.projection[15]=1;
    authored_view.viewport={0,0,800,600,0,1};StrategicViewSet(authored_view);
}
void ClickWorld(int button,int x,int y){MOUSE_X=x;MOUSE_Y=y;Trap(WORD(button),kMouseDown);Trap(WORD(button),kMouseUp);}
// Several squads: a Shift click adds and takes out, a drag picks a box, Ctrl and A picks every squad that takes orders, a digit keeps and picks a group,
// one order reaches every picked squad at places of their own, and B, Delete and H go to each of them.
void PressWorld(int button,int x,int y){MOUSE_X=x;MOUSE_Y=y;Trap(WORD(button),kMouseDown);}
void ReleaseWorld(int button,int x,int y){MOUSE_X=x;MOUSE_Y=y;Trap(WORD(button),kMouseUp);}
void ReboundCommanderInput() {
    Fixture();
    VERIFY(OpenKey() == DIK_M && PortCeActionStore(CeControlAction::Commander, DIK_T));
    PortCeDodgeBindingReload();  // the game reopens the saved preference
    VERIFY(OpenKey() == DIK_T && !PortCeCmdKey(DIK_M, false) && !gOpen);
    CONTROL_BINDINGS[0] = {BUTTON_MECH_MORPH, {{0, DIK_T, KEY_ONCE}, {-1, 0, KEY_ON}}};
    VERIFY(!PortCeCmdKey(DIK_T, false) && !gOpen);
    CONTROL_BINDINGS[0].mId = -1;
    VERIFY(PortCeCmdKey(DIK_T, false) && gOpen && gStrategic && gCloseKey == DIK_T);
    VERIFY(Trap(DIK_T, KEY_EVENT_DOWN) && gOpen); // held opener cannot immediately close
    Trap(DIK_T, KEY_EVENT_UP);
    TapLiveKey(DIK_M);
    VERIFY(gOpen);
    // A preference reload cannot strand a map already open under T.
    VERIFY(PortCeActionStore(CeControlAction::Commander, DIK_M));
    TapLiveKey(DIK_M);
    VERIFY(gOpen);
    TapLiveKey(DIK_T);
    VERIFY(!gOpen && !PortCeCmdKey(DIK_T, false));

    VERIFY(PortCeActionStore(CeControlAction::Commander, DIK_T));
    Open(false);
    VERIFY(gOpen && !gStrategic && GAME.paused);
    Trap(DIK_T, KEY_EVENT_UP);
    TapLiveKey(DIK_M);
    VERIFY(gOpen);
    TapLiveKey(DIK_T);
    VERIFY(!gOpen && !GAME.paused);
}

void FactoryCardInput() {
    Fixture();
    Open(true);
    AuthoredWorldView();
    L.s = 1;
    CeFactoryView view = {};
    VERIFY(!CeFactoryRead(view) && FactoryCardAt(20, 62) == -1);
    PressWorld(0, 20, 62); ReleaseWorld(0, 20, 62);
    VERIFY(!CeFactorySelect() && !CeFactoryQueue());
    CameraFactoryWorld(true, false);  // enabled, but this building has no compatible mount
    VERIFY(!CeFactoryRead(view) && FactoryCardAt(20, 62) == -1);
    CameraFactoryWorld(true, true);
    VERIFY(CeFactoryRead(view) && view.available && !view.selected && view.stage == CE_FACTORY_IDLE);
    VERIFY(FactoryCardAt(20, 62) == 1 && FactoryCardAt(20, 116) == -1);
    PressWorld(0, 20, 62); ReleaseWorld(0, 20, 62);
    VERIFY(CeFactoryRead(view) && view.selected && !gSel.n && gFocusTrip.squad == kFactoryFocus);
    VERIFY(FactoryCardAt(20, 116) == 2);
    PressWorld(0, 20, 116); ReleaseWorld(0, 20, 116);
    VERIFY(CeFactoryRead(view) && view.stage == CE_FACTORY_QUEUED && FactoryCardAt(20, 138) == 3);
    PressWorld(0, 20, 138); ReleaseWorld(0, 20, 138);
    VERIFY(CeFactoryRead(view) && view.stage == CE_FACTORY_CANCELLED && FactoryCardAt(20, 138) == 4);
    PressWorld(0, 20, 138); ReleaseWorld(0, 20, 138);
    VERIFY(CeFactoryRead(view) && !view.selected && FactoryCardAt(20, 116) == -1);
    PressWorld(0, 20, 62); ReleaseWorld(0, 20, 62);
    Close();
    VERIFY(!gOpen && CeFactoryRead(view) && !view.selected);
}
void VisibleRouteBearing() {
    Fixture(); Open(); AuthoredWorldView(); Refresh();
    CeCmdSquad squad = {};
    squad.x = 200; squad.y = 220;
    for (int small = 0; small != 2; ++small) {
        const int width = small ? 640 : 1280, height = small ? 480 : 720;
        L.w = float(width); L.h = float(height); L.s = float(height)/480;
        authored_view.viewport.width = float(width); authored_view.viewport.height = float(height);
        CeViewPoint origin = {}, tip = {}, onScreen = {};
        VERIFY(StrategicPixel(authored_view,squad.x,squad.y,&origin));
        squad.gx = 330; squad.gy = squad.y;
        VERIFY(!StrategicPixel(authored_view,squad.gx,squad.gy,&onScreen));
        VERIFY(StrategicRouteBearing(authored_view,squad,origin,1,tip));
        VERIFY(tip.x > width-13 && tip.x < width-9 && fabs(tip.y-origin.y)<1);
        squad.gx = 70;
        VERIFY(StrategicRouteBearing(authored_view,squad,origin,1,tip));
        VERIFY(tip.x > 9 && tip.x < 13 && fabs(tip.y-origin.y)<1);
        squad.gx = squad.x; squad.gy = 345;
        VERIFY(StrategicRouteBearing(authored_view,squad,origin,1,tip));
        VERIFY(tip.y > height*.80f && tip.y < height-42*L.s && fabs(tip.x-origin.x)<1);
        // A route leaving beside the squad must not draw an arrow on top of it.
        squad.x = 294; squad.y=220; squad.gx=390; squad.gy=squad.y;
        VERIFY(StrategicPixel(authored_view,squad.x,squad.y,&origin));
        if (small) VERIFY(!StrategicRouteBearing(authored_view,squad,origin,1,tip));
        squad.x=200; squad.y=220;
        squad.gx=330; squad.gy=220;
        VERIFY(StrategicPixel(authored_view,squad.x,squad.y,&origin));
        authored_view.projection[2]=0.02f;
        VERIFY(!StrategicRouteBearing(authored_view,squad,origin,1,tip));
        authored_view.projection[2]=0;
        CeViewPoint outside = {-1,origin.y,origin.z};
        VERIFY(!StrategicRouteBearing(authored_view,squad,outside,1,tip));
    }
}

void CameraReacquiresSquad() {
    Fixture(); Open(true); AuthoredWorldView(); Refresh();
    TapLiveKey(DIK_TAB); VERIFY(gSelected == 5);
    const float span = gViewSpan, yaw = gStrategicYaw, pitch = gStrategicPitch;
    const float originalHeight = gStrategicFocusZ;
    MAP.height = originalHeight + 24;
    SetView(live_squads[0].x + 50, live_squads[0].y + 55, span);
    const float fromX = gViewX + gViewSpan * 0.5f;
    const float fromY = gViewY + gViewSpan * 0.5f;
    TapLiveKey(DIK_C);
    for (int i=1; i<=4; ++i) {
        fake_ms += 80; StrategicFrameCamera();
        const float phase = i / 4.f, ease = phase * phase * (3.f - 2.f * phase);
        VERIFY(Near(gViewX + gViewSpan * 0.5f, fromX + (live_squads[0].x-fromX)*ease));
        VERIFY(Near(gViewY + gViewSpan * 0.5f, fromY + (live_squads[0].y-fromY)*ease));
        VERIFY(Near(gStrategicFocusZ, originalHeight + 24*ease));
    }
    VERIFY(Near(gViewSpan,span) && Near(gStrategicYaw,yaw) && Near(gStrategicPitch,pitch));
    live_squads[0].x += 20; Refresh();
    fake_ms += 100; StrategicFrameCamera();
    VERIFY(!Near(gViewX + gViewSpan * 0.5f, live_squads[0].x)); // this is not a permanent chase
    TapLiveKey(DIK_C); fake_ms += 50; StrategicFrameCamera();
    Trap(DIK_RIGHT,KEY_EVENT_DOWN); fake_ms += 33; StrategicFrameCamera(); Trap(DIK_RIGHT,KEY_EVENT_UP);
    const float afterPan = gViewX;
    fake_ms += 400; StrategicFrameCamera();
    VERIFY(Near(gViewX,afterPan) && gFocusTrip.squad < 0 && accepted_orders==0);
    TapLiveKey(DIK_C); fake_ms += 60; StrategicFrameCamera();
    TapLiveKey(DIK_ESCAPE); VERIFY(!gOpen && GAME.controller.target==&pilot);
}

void DrawnMemberInteractions() {
    Fixture();
    VERIFY(PortCeCmdKey(DIK_M, false) && gOpen && gStrategic);
    AuthoredWorldView(); Refresh();
    drawn_members = {{5,670,200,5,{1,50,1},0,0,0,0,0}, {5,674,206,4,{1,51,1},0,0,0,0,0},
                     {17,691,205,6,{1,170,1},0,0,0,0,0}, {999,725,200,5,{1,9990,1},0,0,0,0,0}};
    ClickWorld(0,670,200); VERIFY(gUnitUi.SelectionCount()==1 && gUnitUi.IsSelected({1,50,1}) && gSel.n==0);
    ClickWorld(0,655,200); VERIFY(gUnitUi.SelectionCount()==0);
    ClickWorld(0,691,205); VERIFY(gUnitUi.IsSelected({1,170,1}));
    ClickWorld(0,725,200); VERIFY(gUnitUi.SelectionCount()==0);
    PressWorld(0,661,190); ReleaseWorld(0,703,219);
    VERIFY(gUnitUi.SelectionCount()==3);
    live_squads[1].commandable=false; Refresh();
    PressWorld(0,661,190); ReleaseWorld(0,703,219);
    VERIFY(gUnitUi.SelectionCount()==3); // Inspection keeps units whose orders are unavailable.
    live_squads.erase(live_squads.begin()); Refresh();
    ClickWorld(0,670,200); VERIFY(gUnitUi.SelectionCount()==0);
    TapLiveKey(DIK_ESCAPE); VERIFY(!gOpen);
}

void LiveSelectionInteractions(){
    Fixture();
    live_squads.push_back(live_squads[0]);live_squads[2].ordinal=30;live_squads[2].x=200;live_squads[2].y=300;
    live_squads.push_back(live_squads[0]);live_squads[3].ordinal=40;live_squads[3].commandable=false;live_squads[3].why=CE_WHY_SPAWNED;
    VERIFY(PortCeCmdKey(DIK_M,false) && gOpen && gStrategic);
    AuthoredWorldView();Refresh();
    // Legacy squad selection remains available through Tab and the tactical map.
    // Unit pointer selection is exercised separately through the real controller.
    SelectOnly(5);VERIFY(gSelected==5 && gSel.n==1);
    SelectToggle(17);VERIFY(gSel.n==2 && gSelected==5 && CeSelHas(gSel,17));
    SelectToggle(5);VERIFY(gSel.n==1 && gSelected==17);SelectToggle(5);VERIFY(gSel.n==2);
    SelectOnly(17);VERIFY(gSel.n==1 && gSelected==17);
    SelectOnly(-1);VERIFY(gSel.n==0 && gSelected==-1);
    SelectOnly(5);SelectToggle(17);
    issued.clear();ClickWorld(2,480,340);
    VERIFY(issued.size()==2 && issued[0].verb==CE_CMD_ADVANCE && issued[1].verb==CE_CMD_ADVANCE && issued[0].ordinal!=issued[1].ordinal);
    VERIFY(std::fabs(issued[0].x-issued[1].x)+std::fabs(issued[0].y-issued[1].y)>10.f);
    for(const IssuedOrder& o:issued)VERIFY(std::fabs(o.x-220)<=21.f && std::fabs(o.y-230)<=21.f);
    const float coastX = 220, coastY = 230;
    issued.clear(); Order(CE_CMD_ADVANCE, coastX, coastY);
    const std::vector<IssuedOrder> inland = issued;
    VERIFY(inland.size() == 2);
    wetSlotX = inland[0].x; wetSlotY = inland[0].y;
    VERIFY(wetSlotX != coastX || wetSlotY != coastY);
    shorelineFixture = true;
    issued.clear(); Order(CE_CMD_ADVANCE, coastX, coastY);
    VERIFY(issued.size() == inland.size());
    bool keptDry = false, usedCentre = false;
    for (size_t i = 0; i < issued.size() && i < inland.size(); ++i) {
        if (inland[i].x == wetSlotX && inland[i].y == wetSlotY) {
            usedCentre = true; VERIFY(issued[i].x == coastX && issued[i].y == coastY);
        } else {
            keptDry = true; VERIFY(issued[i].x == inland[i].x && issued[i].y == inland[i].y);
        }
    }
    VERIFY(usedCentre && keptDry);
    // An offshore click must reach the order layer as offshore for every squad, never as a spread onto land.
    wetSlotX = coastX + 1.f; wetSlotY = coastY;
    issued.clear(); Order(CE_CMD_MOVE, wetSlotX, wetSlotY);
    VERIFY(issued.size() == inland.size());
    for (const IssuedOrder& o : issued) VERIFY(o.x == coastX + 1.f && o.y == coastY);

    for (auto &sq : live_squads)
        if (sq.ordinal == inland[0].ordinal) sq.crossesWater = true;
    Refresh();
    issued.clear(); Order(CE_CMD_ADVANCE, wetSlotX, wetSlotY);
    VERIFY(issued.size() == 2);
    for (const auto &o : issued) {
        const bool atClick = o.x == wetSlotX && o.y == wetSlotY;
        VERIFY(atClick == (o.ordinal != inland[0].ordinal));
    }
    for (auto &sq : live_squads) sq.crossesWater = false;
    Refresh();
    shorelineFixture = false;

    // Hold, Fall back and Release reach each picked squad.
    issued.clear();TapLiveKey(DIK_H);VERIFY(issued.size()==2 && issued[0].verb==CE_CMD_HOLD && issued[1].verb==CE_CMD_HOLD);
    // A Fall back: each squad to its own safe point first; the two that chose the same one are then given places of their own round it.
    issued.clear();TapLiveKey(DIK_B);
    VERIFY(issued.size()==4 && issued[0].verb==CE_CMD_FALLBACK && issued[1].verb==CE_CMD_FALLBACK && issued[0].x==0 && issued[1].y==0);
    VERIFY(issued[2].verb==CE_CMD_FALLBACK && issued[3].verb==CE_CMD_FALLBACK && issued[2].ordinal!=issued[3].ordinal);
    VERIFY(std::hypot(issued[2].x-issued[3].x, issued[2].y-issued[3].y)>=15.f);
    for(size_t k=2;k<4;++k)VERIFY(std::fabs(issued[k].x-300)<=21.f && std::fabs(issued[k].y-300)<=21.f);
    issued.clear();TapLiveKey(DIK_DELETE);VERIFY(issued.size()==2 && issued[0].verb==CE_CMD_RELEASE);
    // Tactical-map group operations keep the legacy squad bookmarks.
    GroupKey(1,true,false);SelectAll();VERIFY(gSel.n==3 && CeSelHas(gSel,30) && !CeSelHas(gSel,40));
    GroupKey(1,false,false);VERIFY(gSel.n==2 && !CeSelHas(gSel,30));
    GroupKey(2,false,false);VERIFY(gSel.n==2);
    live_squads[1].commandable=false;Refresh();VERIFY(gSel.n==1 && gSelected==5);
    SelectOnly(-1);GroupKey(1,false,false);VERIFY(gSel.n==1 && gSelected==5);
    // Tab picks one squad alone; a squad picked by setting the lead (the paused map's way) is the whole selection.
    live_squads[1].commandable=true;Refresh();GroupKey(1,false,false);VERIFY(gSel.n==2);
    TapLiveKey(DIK_TAB);VERIFY(gSel.n==1);
    gSelected=30;Refresh();VERIFY(gSel.n==1 && gSelected==30);
    // the level's end forgets the selection and the groups
    Trap(DIK_LCONTROL,KEY_EVENT_DOWN);TapLiveKey(DIK_3);Trap(DIK_LCONTROL,KEY_EVENT_UP);
    PortCeCmdLevelEnd();VERIFY(gSel.n==0 && gSelected==-1 && gGroups.group[3].n==0);
}

void LivePointerInteractions(){
    Fixture();VERIFY(PortCeCmdKey(DIK_M,false));
    VERIFY(gOpen && gStrategic && !GAME.paused && GAME.pause_calls==0);
    AuthoredWorldView();
    SelectOnly(5);VERIFY(gSelected==5);

    // Camera letters must be consumed before action shortcuts or the pilot key path.
    const int camera_orders_before = accepted_orders;
    const int camera_selected_before = gSelected;
    const int camera_selection_before = gSel.n;
    const bool camera_arm_before = gArmed;
    VERIFY(Trap(DIK_A, KEY_EVENT_DOWN));
    VERIFY(StrategicInput().pan_horizontal < 0);
    VERIFY(Trap(DIK_A, KEY_EVENT_UP));
    VERIFY(StrategicInput().pan_horizontal == 0);
    VERIFY(Trap(DIK_D, KEY_EVENT_DOWN));
    VERIFY(StrategicInput().pan_horizontal > 0);
    VERIFY(Trap(DIK_D, KEY_EVENT_UP));
    VERIFY(StrategicInput().pan_horizontal == 0);
    VERIFY(gOpen && gStrategic && GAME.controller.target == &pilot);
    VERIFY(accepted_orders == camera_orders_before && gArmed == camera_arm_before);
    VERIFY(gSelected == camera_selected_before && gSel.n == camera_selection_before);
    // A right click sends the picked squad to fight its way there; Ctrl and a right click is the quiet march.
    ClickWorld(2,480,340);VERIFY(accepted_orders==1 && last_order_verb==CE_CMD_ADVANCE);
    VERIFY(Near(last_order_x,220) && Near(last_order_y,230));
    Trap(DIK_LCONTROL,KEY_EVENT_DOWN);ClickWorld(2,480,340);Trap(DIK_LCONTROL,KEY_EVENT_UP);
    VERIFY(accepted_orders==2 && last_order_verb==CE_CMD_MOVE);accepted_orders=1;
    StrategicArm(CE_CMD_ADVANCE);VERIFY(gArmed);
    ClickWorld(0,500,360);VERIFY(accepted_orders==2 && last_order_verb==CE_CMD_ADVANCE && !gArmed);
    const int before=accepted_orders;
    ClickWorld(2,480,570);VERIFY(accepted_orders==before); // panel cannot issue a world order
    // the RTS Command Card buttons (Move = 0, Attack = 1, Hold = 2): clicking Hold issues Hold, clicking Move arms Move, and gaps hit nothing
    const CeCmdHudLayout hudLay = CeCmdHudComputeLayout(L.w, L.h, L.s, StrategicUiBottom(), gHudPlacardCount, gHudPage);
    VERIFY(hudLay.valid);
    const CeCmdHudRect& moveBtn = hudLay.buttonRects[0];
    const CeCmdHudRect& atkBtn = hudLay.buttonRects[1];
    const CeCmdHudRect& holdBtn = hudLay.buttonRects[2];
    ClickWorld(0, int(holdBtn.x + holdBtn.w * 0.5f), int(holdBtn.y + holdBtn.h * 0.5f));
    VERIFY(accepted_orders == before + 1 && last_order_verb == CE_CMD_HOLD);
    VERIFY(StrategicButtonAt(moveBtn.x + moveBtn.w + (atkBtn.x - (moveBtn.x + moveBtn.w)) * 0.5f, moveBtn.y + moveBtn.h * 0.5f) < 0 &&
           StrategicButtonAt(moveBtn.x + 3.f, moveBtn.y + 3.f) == 0);
    ClickWorld(0, int(moveBtn.x + moveBtn.w * 0.5f), int(moveBtn.y + moveBtn.h * 0.5f));
    VERIFY(gArmed && gStrategicClickOrder == CE_CMD_MOVE);
    ClickWorld(0,520,350);VERIFY(last_order_verb==CE_CMD_MOVE && !gArmed);
    // Armed commands and pointer selection must revalidate after world changes.
    live_squads.erase(live_squads.begin());const int attempts=issue_attempts;
    ClickWorld(2,480,340);VERIFY(gSelected==-1 && issue_attempts==attempts);
    SelectOnly(17);VERIFY(gSelected==17);
    StrategicViewInvalidate();ClickWorld(2,480,340);VERIFY(issue_attempts==attempts);
    // The view the pointer picks from is the one the picture was last drawn with: not one a frame older than the last, not one from a window
    // of another size, not one from an earlier entry into the view.
    AuthoredWorldView();{CeWorldViewSample read;VERIFY(StrategicViewRead(&read) && read.viewport.width==800);}
    fake_ms+=1;{CeWorldViewSample read;VERIFY(StrategicViewRead(&read));}
    fake_ms+=1;{CeWorldViewSample read;VERIFY(!StrategicViewRead(&read));}ClickWorld(2,480,340);VERIFY(issue_attempts==attempts);
    AuthoredWorldView();L.w=1024;{CeWorldViewSample read;VERIFY(!StrategicViewRead(&read));}L.w=800;
    {CeWorldViewSample read;VERIFY(StrategicViewRead(&read));}
    ++gStrategicViewSession;{CeWorldViewSample read;VERIFY(!StrategicViewRead(&read));}--gStrategicViewSession;
    {CeWorldViewSample read;VERIFY(StrategicViewRead(&read));VERIFY(!StrategicViewRead(NULL));}
    AuthoredWorldView();live_squads[0].ordinal=90;ClickWorld(2,480,340);
    VERIFY(gSelected==-1 && issue_attempts==attempts);
    SelectOnly(90);VERIFY(gSelected==90);
    live_squads[0].friendly=live_squads[0].commandable=false;ClickWorld(2,480,340);
    VERIFY(gSelected==-1 && issue_attempts==attempts);

    Fixture();Open(true);AuthoredWorldView();SelectOnly(5);
    pilot.life=0;ClickWorld(2,480,340);VERIFY(!gOpen && accepted_orders==0);
    Fixture();Open(true);AuthoredWorldView();SelectOnly(5);
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
    Close();Open(true);VERIFY(gStrategicViewSession!=oldSession && !gStrategicSampleValid);
    TapLiveKey(DIK_ESCAPE);VERIFY(!gOpen && !GAME.paused);
}

void LiveSquadInteractions() {
    Fixture();
    VERIFY(!Trap(DIK_TAB, KEY_EVENT_DOWN) && accepted_orders == 0 && list_reads == 0);
    Open(true); AuthoredWorldView();
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
    const float start_x = gViewX + gViewSpan * 0.5f;
    TapLiveKey(DIK_C);
    VERIFY(Near(gViewX + gViewSpan * 0.5f, start_x));
    fake_ms += 160; StrategicFrameCamera();
    const float middle_x = gViewX + gViewSpan * 0.5f;
    VERIFY(Near(middle_x, (start_x + live_squads[1].x) * 0.5f));
    fake_ms += 180; StrategicFrameCamera();
    VERIFY(Near(gViewX + gViewSpan * 0.5f, live_squads[1].x));
    VERIFY(Near(gViewY + gViewSpan * 0.5f, live_squads[1].y));
    VERIFY(!GAME.paused && GAME.controller.target == &pilot);
    AuthoredWorldView(); // Refresh the displayed input surface after simulated camera frames.
    Trap(DIK_H, KEY_EVENT_DOWN);
    VERIFY(accepted_orders == 1 && last_ordered == 17 && live_squads[1].verb == CE_CMD_HOLD);
    AuthoredWorldView(); // Refresh the displayed input surface after simulated camera frames.
    Trap(DIK_H, KEY_EVENT_DOWN);
    VERIFY(issue_attempts == 1 && accepted_orders == 1);
    Trap(DIK_H, KEY_EVENT_UP); TapLiveKey(DIK_X);
    VERIFY(accepted_orders == 2 && last_ordered == 17);
    live_squads[1].members = 2;
    PortCeCmdFrame(); messages.clear(); text_y.clear(); StrategicRenderPrefix();
    VERIFY(std::any_of(messages.begin(), messages.end(), [](const std::string& s) {
        return s.find("Authored squad 1   2 units") != std::string::npos;
    }));
    VERIFY(std::any_of(messages.begin(), messages.end(), [](const std::string& s) {
        return s.find("Order: Holding") != std::string::npos;
    }));
    live_squads.pop_back(); PortCeCmdFrame();
    VERIFY(gSelected == -1);
    messages.clear(); StrategicRenderPrefix();
    VERIFY(std::find(messages.begin(), messages.end(), "No Unit or Squad Picked") != messages.end());

    // The display is stale on purpose. Every action must obtain a new list,
    // not rely on a render or frame happening between world change and input.
    Fixture(); Open(true); AuthoredWorldView(); TapLiveKey(DIK_TAB);
    live_squads.erase(live_squads.begin()); TapLiveKey(DIK_H);
    VERIFY(gSelected == -1 && issue_attempts == 0 && accepted_orders == 0);
    VERIFY(std::string(gNote) == "Pick a squad first.");

    Fixture(); Open(true); AuthoredWorldView(); TapLiveKey(DIK_TAB);
    // A new squad in the same list slot has a new lifetime ordinal. The
    // adapter must not silently transfer the previous selection to it.
    live_squads[0].ordinal = 89; TapLiveKey(DIK_H);
    VERIFY(gSelected == -1 && issue_attempts == 0 && accepted_orders == 0);

    Fixture(); Open(true); AuthoredWorldView(); TapLiveKey(DIK_TAB);
    live_squads[0].friendly = live_squads[0].commandable = false;
    TapLiveKey(DIK_H);
    VERIFY(gSelected == -1 && issue_attempts == 0);
    TapLiveKey(DIK_TAB); VERIFY(gSelected == 17);
    live_squads[1].commandable = false; live_squads[1].why = CE_WHY_SCRIPTED;
    const float before_x = gViewX, before_y = gViewY;
    TapLiveKey(DIK_C);
    VERIFY(gSelected == -1 && Near(gViewX, before_x) && Near(gViewY, before_y));
    TapLiveKey(DIK_TAB); VERIFY(gSelected == -1 && issue_attempts == 0);

    Fixture(); Open(true); AuthoredWorldView(); TapLiveKey(DIK_TAB); refuse_next_order = true;
    TapLiveKey(DIK_H);
    VERIFY(issue_attempts == 1 && accepted_orders == 0);
    VERIFY(std::string(gNote) == "Squad no longer takes orders.");
    VERIFY(live_squads[0].verb == CE_CMD_NONE);

    Fixture(); Open(true); AuthoredWorldView(); TapLiveKey(DIK_TAB); pilot.life = 0;
    TapLiveKey(DIK_H);
    VERIFY(!gOpen && accepted_orders == 0 && issue_attempts == 0);
    Fixture(); Open(true); AuthoredWorldView(); TapLiveKey(DIK_TAB); GAME.ordinary = false;
    TapLiveKey(DIK_H);
    VERIFY(!gOpen && accepted_orders == 0 && issue_attempts == 0);
    Fixture(); Open(true); AuthoredWorldView(); TapLiveKey(DIK_TAB); LT.SetKeytrap(OtherTrap);
    TapLiveKey(DIK_H);
    VERIFY(!gOpen && LT.GetKeytrap() == OtherTrap && issue_attempts == 0);
    Fixture(); Open(true); AuthoredWorldView(); TapLiveKey(DIK_TAB); ++pilot.creation_number;
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

void SessionGestureCleanup() {
    Fixture(); Open(true); VERIFY(gOpen && gStrategic);
    const auto dirty = [] {
        gBoxing = true; gBoxX = 11; gBoxY = 17;
        gStrategicDragging = true; gStrategicDragX = 7; gStrategicDragY = -9;
        gStrategicWheel = 2; gArmed = true;
    };
    const auto clean = [] {
        VERIFY(!gBoxing && !gStrategicDragging && gStrategicWheel == 0);
        VERIFY(gBoxX == 0 && gBoxY == 0 && gStrategicDragX == 0 && gStrategicDragY == 0);
        VERIFY(!gArmed);
    };
    dirty(); Close(); VERIFY(!gOpen); clean();
    dirty(); Open(true); VERIFY(gOpen && gStrategic); clean();
    AuthoredWorldView();
    const int before = accepted_orders;
    VERIFY(Trap(WORD(0), KEY_EVENT_UP));
    VERIFY(Trap(WORD(1), KEY_EVENT_UP));
    VERIFY(accepted_orders == before && !gBoxing && !gStrategicDragging);
    dirty(); SwitchView(false); VERIFY(gOpen && !gStrategic); clean();
    dirty(); SwitchView(true); VERIFY(gOpen && gStrategic); clean();
    dirty(); PortCeCmdLevelEnd(); VERIFY(!gOpen); clean();
}

void DialogueLayoutAndInput() {
    Fixture(); Open(true);
    const int before_ready = accepted_orders;
    TapLiveKey(DIK_H);
    VERIFY(accepted_orders == before_ready && !StrategicUiReady());
    AuthoredWorldView(); SelectOnly(gSquads[0].ordinal);
    VERIFY(StrategicUiReady() && StrategicUiBottom() == L.h);
    const auto hold_point = [](int &x, int &y) {
        for (y = 0; y < L.h; y += 2)
            for (x = 0; x < L.w; x += 2)
                if (StrategicButtonAt(float(x), float(y)) == 2) return true;
        return false;
    };
    int old_x = 0, old_y = 0;
    VERIFY(hold_point(old_x, old_y));
    const float caption_top = float(L.h) * 0.72f;
    {
        CeHudReservationScope caption(gStrategicViewSession, PortPresentCount(), L.w, L.h);
        CeHudReservationQuad(0, caption_top, float(L.w), caption_top,
            0, float(L.h), float(L.w), float(L.h), 0xffffffffu);
    }
    AuthoredWorldView();
    VERIFY(gStrategicUi.hasDialogue && StrategicUiBottom() < caption_top);
    // FORCES stays cleanly bottom-docked (never pushed into the center of the battlefield),
    // while subtitleSafeRect provides a clean non-overlapping lane above FORCES.
    const CeCmdHudLayout dlgLay = CeCmdHudComputeLayout(L.w, L.h, L.s, StrategicUiBottom(), gHudPlacardCount, gHudPage);
    VERIFY(dlgLay.valid);
    VERIFY(dlgLay.subtitleSafeRect.y + dlgLay.subtitleSafeRect.h < dlgLay.barRect.y);
    int new_x = 0, new_y = 0;
    VERIFY(hold_point(new_x, new_y) && new_y == old_y);
    VERIFY(StrategicButtonAt(float(old_x), float(old_y)) == 2);
    VERIFY(!StrategicWorldArea(float(old_x), float(old_y)));
    const int before = accepted_orders;
    ClickWorld(0, new_x, new_y);
    VERIFY(accepted_orders == before + 1 && last_order_verb == CE_CMD_HOLD);
    StrategicViewInvalidate();
    ClickWorld(0, new_x, new_y); TapLiveKey(DIK_H);
    VERIFY(accepted_orders == before + 1 && !StrategicUiReady());
    AuthoredWorldView();
    ++PLATFORM.width; VERIFY(!StrategicUiReady());
    ClickWorld(0, new_x, new_y); VERIFY(accepted_orders == before + 1);
    --PLATFORM.width;
    ++L.w; VERIFY(!StrategicUiReady());
    ClickWorld(0, new_x, new_y); VERIFY(accepted_orders == before + 1);
    --L.w;
    fake_ms += 1000; AuthoredWorldView();
    VERIFY(!gStrategicUi.hasDialogue && StrategicUiBottom() == L.h);
    Close(); VERIFY(!StrategicUiReady());
}


static void NativePresentationJourney() {
    Fixture();
    const FVector pilot_before = pilot.position;
    const float life_before = pilot.life;
    const FVector cockpit_before = original_camera.mPos;
    Open(true);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicPresentedCamera);
    VERIFY(SamePosition(gStrategicPresentedCamera.mPos, cockpit_before));
    fake_ms += 100;
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicPresentedCamera);
    VERIFY(!SamePosition(gStrategicPresentedCamera.mPos, cockpit_before));
    VERIFY(!SamePosition(gStrategicPresentedCamera.mPos, gStrategicCamera.mPos));
    const FVector intermediate = gStrategicPresentedCamera.mPos;
    PortCeCmdRenderCamera(&original_camera, &player);
    VERIFY(SamePosition(gStrategicPresentedCamera.mPos, intermediate));
    fake_ms += 400;
    PortCeCmdRenderCamera(&original_camera, &player);
    VERIFY(SamePosition(gStrategicPresentedCamera.mPos, gStrategicCamera.mPos));
    VERIFY(SamePosition(pilot.position, pilot_before) && pilot.life == life_before);
    VERIFY(SamePosition(original_camera.mPos, cockpit_before));
    const FVector strategic = gStrategicPresentedCamera.mPos;
    const int orders_before = accepted_orders;
    Close();
    VERIFY(!gOpen && !gStrategicOwner.Active() && GAME.controller.target == &pilot);
    VERIFY(gNativeReturning);
    const CeCommanderCameraContext return_context = StrategicContext();
    VERIFY(return_context.admitted_ordinary_solo);
    VERIFY(return_context.live_pilot);
    VERIFY(return_context.camera_available);
    VERIFY(LT.GetKeytrap() == NULL);
    VERIFY(SameCameraBinding(return_context.binding, gNativeReturnBinding));
    VERIFY(GAME.GetCurrentCamera(0) == gNativeReturnOriginal);
    VERIFY(PortCeCmdHideCockpit());
    VERIFY(PortCeCmdWorldViewToken() == 0);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicPresentedCamera);
    VERIFY(SamePosition(gStrategicPresentedCamera.mPos, strategic));
    CGenericCamera secondary;
    VERIFY(PortCeCmdRenderCamera(&secondary, &player) == &secondary && gNativeReturning);
    original_camera.mPos.X += 12.0f;
    pilot.life -= 1.0f;
    fake_ms += 100;
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicPresentedCamera);
    VERIFY(!SamePosition(gStrategicPresentedCamera.mPos, strategic));
    VERIFY(!SamePosition(gStrategicPresentedCamera.mPos, original_camera.mPos));
    VERIFY(pilot.life == life_before - 1.0f && SamePosition(pilot.position, pilot_before));
    fake_ms += 400;
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    VERIFY(!gNativeReturning && !PortCeCmdHideCockpit());
    VERIFY(accepted_orders == orders_before);

    Fixture(); Open(true); PortCeCmdRenderCamera(&original_camera, &player);
    fake_ms += 400; PortCeCmdRenderCamera(&original_camera, &player); Close();
    fake_ms += 100; PortCeCmdRenderCamera(&original_camera, &player);
    const FVector returning = gStrategicPresentedCamera.mPos;
    Open(true);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicPresentedCamera);
    VERIFY(SamePosition(gStrategicPresentedCamera.mPos, returning) && !gNativeReturning);
    Close();

    Fixture();
    original_camera.mOrientation.Row[0] = FVector(0,0,0);
    Open(true);
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicPresentedCamera);
    VERIFY(SamePosition(gStrategicPresentedCamera.mPos, gStrategicCamera.mPos));
    VERIFY(!gNativeEntering && gStrategicOwner.Active());
    gStrategicCamera.mOrientation.Row[0] = FVector(0,0,0);
    VERIFY(NativeStrategicPresentation(&original_camera) == &original_camera);
    VERIFY(!gStrategicOwner.Active() && !gNativePresented && !gStrategicRender);
    Close();

    for (int invalidation = 0; invalidation < 6; ++invalidation) {
        Fixture(); Open(true); PortCeCmdRenderCamera(&original_camera, &player);
        fake_ms += 400; PortCeCmdRenderCamera(&original_camera, &player); Close();
        VERIFY(gNativeReturning);
        CGenericCamera replacement;
        if (invalidation == 0) unit_owns_pilot = true;
        if (invalidation == 1) ++gMissionLifetime;
        if (invalidation == 2) pilot.life = 0.0f;
        if (invalidation == 3) GAME.camera = &replacement;
        if (invalidation == 4) { PortCeCmdLevelEnd(); VERIFY(!gNativeReturning); }
        if (invalidation == 5) LT.SetKeytrap(OtherTrap);
        CCamera* current = GAME.camera;
        VERIFY(PortCeCmdRenderCamera(current, &player) == current);
        VERIFY(!gNativeReturning && !PortCeCmdHideCockpit());
    }
}

int main() {
#ifdef CE_X87_SINGLE_PRECISION
    SetX87SinglePrecision();
#endif
    ReboundCommanderInput();
    FactoryCardInput();
    Fixture();
    // Possession and the return-to-pilot transition both keep the map from stealing control.
    unit_owns_pilot=true;
    VERIFY(!PortCeCmdKey(DIK_M,false) && !gOpen && !gStrategic && !unit_map_boundaries);
    VERIFY(std::string(gNote) == "Return to your Battle Engine to command.");
    gNote[0] = 0;
    VERIFY(!PortCeCmdKey(DIK_M,true) && !gOpen && !gStrategic && !unit_map_boundaries);
    VERIFY(!gNote[0]);
    Open(true);
    VERIFY(!gOpen && !unit_map_boundaries && GAME.controller.target == &pilot);
    unit_owns_pilot=false;
    VERIFY(PortCeCmdKey(DIK_M,true) && !gOpen && !unit_map_boundaries);
    VERIFY(PortCeCmdKey(DIK_M,false) && gOpen && gStrategic && unit_map_boundaries==1);
    Close();
    VERIFY(!gOpen && unit_map_boundaries==2 && GAME.controller.target==&pilot);
    Close();
    VERIFY(unit_map_boundaries==2);
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
    VERIFY(StrategicFrameCamera() == NULL && PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    VERIFY(PresentationCamera(&original_camera, &player) == &original_camera);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter);
    VERIFY(!Trap(DIK_V, KEY_EVENT_DOWN) && !gOpen);
    GAME.ordinary = false;
    VERIFY(StrategicFrameCamera() == NULL && PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    GAME.ordinary = true;
    Open();
    VERIFY(gOpen && !gStrategic && GAME.paused && GAME.pause_calls == 1);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter);
    VERIFY(GAME.controller.target == &gSink);
    VERIFY(StrategicFrameCamera() == NULL && PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    VERIFY(entered_calls == 0);
    const FVector before = original_camera.mPos;
    const float fitted_x = gViewX, fitted_y = gViewY, fitted_span = gViewSpan;
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN));
    VERIFY(gOpen && gStrategic && gStrategicOwner.Active());
    VERIFY(!GAME.paused && GAME.pause_calls == 1);
    VERIFY(GAME.controller.target == &pilot && pilot.life == 10);
    VERIFY(SamePosition(original_camera.mPos, before));
    // The live view leaves the game's frame alone: it asks for no draw filter and drops no draw (its own picture covers a frame the game drew whole).
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter && !PortCePhotoOn() && entered_calls == 1 && left_calls == 0);
    VERIFY(PortCeCmdWorldViewToken() == gStrategicViewSession && PortCeCmdWorldViewToken() != 0);
    {
        CePhotoDrawScope cockpit_or_flight_overlay;
        VERIFY(draws_dropped == 0);
    }
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
    VERIFY(StrategicFrameCamera() == &gStrategicCamera);
    // The engine is never given the strategic camera: with the view live, the presentation hook (patch 0200's lines, as the engine calls them)
    // hands back the camera it was given, in the CE build and in the faithful one alike.
    VERIFY(PortCeCmdRenderCamera(&original_camera, &player) == &gStrategicPresentedCamera);
#if defined(CE_VERSION)
    VERIFY(PresentationCamera(&original_camera, &player) == &gStrategicPresentedCamera);
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
    // The paused map is a tester's page: without its ce.ini switch V does nothing in the live view.
    Trap(DIK_V, KEY_EVENT_UP); tactical_allowed = false; Trap(DIK_V, KEY_EVENT_DOWN); Trap(DIK_V, KEY_EVENT_UP);
    VERIFY(gOpen && gStrategic && gStrategicOwner.Active() && !GAME.paused);
    tactical_allowed = true;		// and with it V goes to the paused map (below)
    Trap(DIK_W, KEY_EVENT_DOWN); fake_ms += 50;
    const FVector strategic_before = gStrategicCamera.mPos;
    PortCeCmdFrame();
    VERIFY(!SamePosition(strategic_before, gStrategicCamera.mPos));
    VERIFY(gViewY > fitted_y && Near(gViewX, fitted_x));
    VERIFY(SamePosition(original_camera.mPos, before));
    VERIFY(GAME.controller.target == &pilot && !GAME.paused);
    StrategicViewInvalidate(); authored_view.viewport = {0, 0, 800, 600, 0, 1};
    StrategicRenderPrefix();
    // One frame of the picture: drawn from the strategic camera, its view kept for the pointer, the map's text and cursor over it.
    VERIFY(draw_starts == 1 && draw_ends == 1 && pictured_camera == &gStrategicCamera && gStrategicSampleValid);
    // The prompt line names the way back to the cockpit, and the title panel's "Commander" is drawn in the top left group.
    // (each prompt's key and what it does are drawn as two words: the key white, the action grey)
    {
        const auto esc = std::find(messages.begin(), messages.end(), std::string("Esc"));
        VERIFY(esc != messages.end() && std::find(esc, messages.end(), std::string("Cockpit")) != messages.end());
    }
    {
        const auto title = std::find(messages.begin(), messages.end(), std::string("Commander"));
        VERIFY(title != messages.end() && text_y[title - messages.begin()] == 10 * L.s);
    }
    VERIFY(cursor_draws==1 && !text_y.empty());
    // F1 held shows the full list of keys, and only while it is held
    VERIFY(std::find(messages.begin(), messages.end(), std::string("Delete")) == messages.end());
    Trap(DIK_F1, KEY_EVENT_DOWN); messages.clear(); StrategicViewInvalidate(); StrategicRenderPrefix();
    VERIFY(std::find(messages.begin(), messages.end(), std::string("Delete")) != messages.end() &&
           std::find(messages.begin(), messages.end(), std::string("Back to their own orders")) != messages.end());
    Trap(DIK_F1, KEY_EVENT_UP); messages.clear(); StrategicViewInvalidate(); StrategicRenderPrefix();
    VERIFY(std::find(messages.begin(), messages.end(), std::string("Delete")) == messages.end());
    draw_starts = draw_ends = 1; cursor_draws = 1;		// as the frame above left them, for the checks that follow
    // A faded colour keeps its full alpha at full strength, half at half, none at none, whatever way the unit rounds 255.5.
    VERIFY((Faded(0xffeef2f6, 1.f) >> 24) == 0xff && (Faded(0xffeef2f6, 1.f) & 0xffffff) == 0xeef2f6);
    VERIFY((Faded(0xff000000, 0.5f) >> 24) == 0x80 && (Faded(0xaf000000, 0.f) >> 24) == 0 && (Faded(0xaf000000, 2.f) >> 24) == 0xaf);
    // Where no picture can be made nothing is drawn, and the pointer is left without a view to pick from.
    picture_available = false; messages.clear();
    StrategicRenderPrefix();
    VERIFY(draw_starts == 1 && draw_ends == 1 && !gStrategicSampleValid && messages.empty() && cursor_draws == 1);
    picture_available = true;
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
    VERIFY(StrategicFrameCamera() == NULL && PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    VERIFY(PresentationCamera(&original_camera, &player) == &original_camera);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter && draws_dropped == 0 && left_calls == 1);
    VERIFY(PortCeCmdWorldViewToken() == 0);
    VERIFY(Near(gViewX, panned_x) && Near(gViewY, panned_y));
    Close(); Open(true); // The map's key-up handler is outside this extracted input fragment.
    VERIFY(gStrategic && Near(gViewX, panned_x) && Near(gViewY, panned_y));
    Close();
    VERIFY(!gOpen && !GAME.paused && GAME.controller.target == &pilot);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter);

    // M after V: a switch keeps the trap's record of held keys, so the first M after a V closes the view the V opened,
    // while an M still held from the press that opened the map stays a repeat.
    Fixture(); Open();
    VERIFY(gOpen && !gStrategic && gDown[gCloseKey]); // the M that opened the map from the battle may repeat
    gDown[gCloseKey] = false; // its release: this camera-switch case starts from an unheld opener
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN) && gOpen && gStrategic && gDown[DIK_V]);
    Trap(DIK_V, KEY_EVENT_UP);
    VERIFY(Trap(DIK_M, KEY_EVENT_DOWN)); // the first M after the V
    VERIFY(!gOpen && !gStrategic && !gStrategicOwner.OwnsCapture() && !GAME.paused);
    VERIFY(GAME.controller.target == &pilot);

    Fixture(); Open(); gDown[gCloseKey] = false;
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN) && gStrategic);
    Trap(DIK_V, KEY_EVENT_UP);
    VERIFY(Trap(DIK_V, KEY_EVENT_DOWN) && gOpen && !gStrategic && GAME.paused);
    // Back on the paused map, its own key handler (outside this fragment) takes a key-down as a press when this record
    // says the key is up: the next M closes.
    VERIFY(gDown[DIK_V] && !gDown[gCloseKey]);

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
    VERIFY(StrategicFrameCamera() == NULL && PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    VERIFY(entered_calls == 1 && left_calls == 1);

    Fixture(); Open(true); GAME.ordinary = false;
    PortCeCmdFrame();
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture());
    VERIFY(!GAME.paused && GAME.controller.target == &pilot);
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter);
    VERIFY(StrategicFrameCamera() == NULL && PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);

    // Photo mode's draw filter is photo mode's alone: the live view neither takes it nor gives it back.
    Fixture(); Open(true); photo_on = true;
    Close();
    VERIFY(!PortCeCmdSuppressFlightHud() && !draw_filter && PortCePhotoOn());

    Fixture(); Open(true);
    CBattleEngine replacement_pilot; CPlayer replacement_player;
    replacement_player.pilot = &replacement_pilot;
    CGenericCamera replacement_camera;
    GAME.player = &replacement_player; GAME.camera = &replacement_camera;
    GAME.controller.target = &replacement_pilot;
    VERIFY(StrategicFrameCamera() == NULL && PortCeCmdRenderCamera(&replacement_camera, &replacement_player) == &replacement_camera);
    StrategicRenderPrefix();
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture());
    VERIFY(GAME.controller.target == &replacement_pilot && !GAME.paused);

    Fixture(); Open(true); ++pilot.creation_number;
    VERIFY(StrategicFrameCamera() == NULL);
    StrategicRenderPrefix();
    VERIFY(!gOpen && !gStrategicOwner.OwnsCapture());
    VERIFY(GAME.controller.target == &pilot && !GAME.paused);

    Fixture(); Open(true); pilot.life = 0;
    VERIFY(StrategicFrameCamera() == NULL);
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
    VERIFY(StrategicFrameCamera() == NULL && PortCeCmdRenderCamera(&original_camera, &player) == &original_camera);
    LiveSquadInteractions();
    LivePointerInteractions();
    SessionGestureCleanup();
    DialogueLayoutAndInput();
    VisibleRouteBearing();
    CameraReacquiresSquad();
    DrawnMemberInteractions();
    LiveSelectionInteractions();
    MapPanTiming();
    RenderTimeCamera();
    TwoDaysOfUptime();
    KeysAsHeldNow();
    KeyDirections();
    NativePresentationJourney();
    std::cout << "source-bound commander camera integration controls passed"
#ifdef CE_X87_SINGLE_PRECISION
              << " (x87 unit at single precision)"
#endif
              << "\n";
    return 0;
}
#endif
