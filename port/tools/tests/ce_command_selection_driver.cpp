// Standalone test driver for the selection controller.
//
// Link with ce_command_panel.cpp and ce_command_selection.cpp.  The driver
// plays the host: it builds frames, lays the panel out with a fixed-metric
// font, and drives pointer events and actions.  Every case runs every check.
// On success it prints one line of totals; each failed check prints its case
// name, expression and line.  The exit code is nonzero when anything failed.

#include "ce_command_selection.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace cs = ce_command_selection;
namespace cp = ce_command_panel;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_case = "";

void Check(bool ok, const char* expression, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL [%s] %s (line %d)\n", g_case, expression, line);
    }
}

#define CHECK(condition) Check((condition), #condition, __LINE__)

// ---------------------------------------------------------------------------
// Host stand-in
// ---------------------------------------------------------------------------

constexpr uint64_t kWorld = 7;
constexpr int kSceneUnits = 10;

constexpr uint32_t kOrderCaps =
    cp::ActionBit(cp::Action::Move) | cp::ActionBit(cp::Action::Advance) |
    cp::ActionBit(cp::Action::Hold) | cp::ActionBit(cp::Action::TakeControl) |
    cp::ActionBit(cp::Action::Focus);

cs::SelectionController g_ctl;
cs::UnitSnapshot g_units[cs::kMaxUnits + 1];

cs::NativeKey Key(uint64_t world, uint64_t id, uint64_t generation) {
    cs::NativeKey key;
    key.world = world;
    key.id = id;
    key.generation = generation;
    return key;
}

cs::UnitSnapshot MakeUnit(uint64_t world, uint64_t id, uint64_t generation, float x, float y) {
    cs::UnitSnapshot unit{};
    unit.key = Key(world, id, generation);
    unit.facts.kind = cp::EntityKind::Unit;
    unit.facts.relation = cp::TeamRelation::Friendly;
    unit.facts.control = cp::ControlOwner::CommandAI;
    unit.facts.availability = cp::Availability::Available;
    unit.facts.selectable = true;
    unit.facts.capabilities = kOrderCaps;
    std::snprintf(unit.facts.name, sizeof(unit.facts.name), "Unit %llu",
                  static_cast<unsigned long long>(id));
    unit.screen.x = x;
    unit.screen.y = y;
    unit.screen.radius = 20.0f;
    unit.screen.projected = true;
    return unit;
}

void SetSquad(cs::UnitSnapshot* unit, uint32_t id, uint32_t generation) {
    unit->facts.squad.present = true;
    unit->facts.squad.squad_id = id;
    unit->facts.squad.squad_generation = generation;
    std::snprintf(unit->facts.squad.name, sizeof(unit->facts.squad.name), "Alpha");
}

// The standard scene.  The array index equals the native id.
//   0,1,2  squad (5,1); unit 2 is off screen
//   3      no squad
//   4      piloted by the local player; the host over-reports Move and Hold
//   5      commandable but reports Focus only
//   6      not selectable
//   7,8    squad descriptor present but with id 0 (not a usable squad)
//   9      squad (5,2): same id, another generation
int BuildScene(cs::UnitSnapshot* units, uint64_t world) {
    units[0] = MakeUnit(world, 0, 1, 800.0f, 100.0f);
    units[1] = MakeUnit(world, 1, 1, 900.0f, 100.0f);
    units[2] = MakeUnit(world, 2, 1, 1000.0f, 100.0f);
    units[3] = MakeUnit(world, 3, 1, 800.0f, 200.0f);
    units[4] = MakeUnit(world, 4, 1, 900.0f, 200.0f);
    units[5] = MakeUnit(world, 5, 1, 1000.0f, 200.0f);
    units[6] = MakeUnit(world, 6, 1, 1100.0f, 200.0f);
    units[7] = MakeUnit(world, 7, 1, 800.0f, 300.0f);
    units[8] = MakeUnit(world, 8, 1, 900.0f, 300.0f);
    units[9] = MakeUnit(world, 9, 1, 1000.0f, 300.0f);

    SetSquad(&units[0], 5, 1);
    SetSquad(&units[1], 5, 1);
    SetSquad(&units[2], 5, 1);
    units[2].screen.projected = false;

    units[4].facts.control = cp::ControlOwner::LocalHuman;
    units[4].facts.capabilities =
        cp::ActionBit(cp::Action::Move) | cp::ActionBit(cp::Action::Hold) |
        cp::ActionBit(cp::Action::ReturnToAI) | cp::ActionBit(cp::Action::Focus);

    units[5].facts.capabilities = cp::ActionBit(cp::Action::Focus);

    units[6].facts.selectable = false;
    units[6].facts.relation = cp::TeamRelation::Hostile;
    units[6].facts.control = cp::ControlOwner::IndependentAI;
    units[6].facts.capabilities = 0;

    SetSquad(&units[7], 0, 1);
    SetSquad(&units[8], 0, 1);
    SetSquad(&units[9], 5, 2);
    return kSceneUnits;
}

cs::Frame MakeFrame(uint64_t world, uint32_t snapshot, uint32_t context,
                    const cs::UnitSnapshot* units, int count) {
    cs::Frame frame{};
    frame.world = world;
    frame.snapshot_generation = snapshot;
    frame.context_generation = context;
    frame.targeted_actions = cp::ActionBit(cp::Action::Move);
    frame.complete = true;
    frame.units = units;
    frame.unit_count = count;
    return frame;
}

cp::TextSize Measure(void*, const char* text, float scale) {
    cp::TextSize size;
    size.w = 7.0f * static_cast<float>(std::strlen(text)) * scale;
    size.h = 12.0f * scale;
    return size;
}

// The panel sits at the bottom-left and is at most 640 pixels wide here; all
// world positions used below are to the right of that.
void Layout() {
    cp::LayoutInput input{};
    input.safe.x = 0.0f;
    input.safe.y = 0.0f;
    input.safe.w = 1280.0f;
    input.safe.h = 720.0f;
    input.scale = 1.0f;
    input.measure = Measure;
    g_ctl.UpdateLayout(input);
}

cs::FrameResult Start(const char* name) {
    g_case = name;
    g_ctl.Reset();
    const int count = BuildScene(g_units, kWorld);
    const cs::FrameResult result = g_ctl.BeginFrame(MakeFrame(kWorld, 1, 1, g_units, count));
    Layout();
    return result;
}

// Supply the standard scene again, after `g_units` was optionally edited.
cs::FrameResult Refresh(uint32_t snapshot, uint32_t context, int count) {
    const cs::FrameResult result =
        g_ctl.BeginFrame(MakeFrame(kWorld, snapshot, context, g_units, count));
    Layout();
    return result;
}

cs::GestureResult Click(float x, float y, bool additive = false, bool whole_squad = false) {
    cs::ClickModifiers modifiers{};
    modifiers.additive = additive;
    modifiers.whole_squad = whole_squad;
    g_ctl.PointerDown(x, y);
    return g_ctl.PointerUp(x, y, modifiers);
}

cs::GestureResult Drag(float x0, float y0, float x1, float y1, bool additive = false) {
    cs::ClickModifiers modifiers{};
    modifiers.additive = additive;
    g_ctl.PointerDown(x0, y0);
    g_ctl.PointerMove((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
    return g_ctl.PointerUp(x1, y1, modifiers);
}

const cp::Control* FindControl(cp::Action action) {
    const cp::PanelLayout& layout = g_ctl.Panel().Layout();
    if (!layout.visible) return nullptr;
    for (int i = 0; i < layout.control_count; ++i) {
        if (layout.controls[i].action == action) return &layout.controls[i];
    }
    return nullptr;
}

bool ControlCentre(cp::Action action, float* x, float* y) {
    *x = 0.0f;
    *y = 0.0f;
    const cp::Control* control = FindControl(action);
    if (control == nullptr) return false;
    *x = control->rect.x + control->rect.w * 0.5f;
    *y = control->rect.y + control->rect.h * 0.5f;
    return true;
}

// Press and release the panel control for `action`.  A missing control gives
// a blank result, which fails the caller's checks by name.
cs::GestureResult PressControl(cp::Action action) {
    Layout();
    float x = 0.0f;
    float y = 0.0f;
    cs::GestureResult blank{};
    if (!ControlCentre(action, &x, &y)) return blank;
    g_ctl.PointerDown(x, y);
    return g_ctl.PointerUp(x, y, cs::ClickModifiers{});
}

bool Sel(uint64_t id) { return g_ctl.IsSelected(Key(kWorld, id, 1)); }

cs::ArmStatus Arm(cp::Action action) { return g_ctl.ArmAction(action); }

// Standard scene with units 0 and 3 selected and Move armed for both.
void StartArmed(const char* name) {
    Start(name);
    Click(800.0f, 100.0f);
    Click(800.0f, 200.0f, true);
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Armed);
    CHECK(g_ctl.HasArmedTarget());
}

cs::WorldPoint Point(float x, float y, float z) {
    cs::WorldPoint point;
    point.x = x;
    point.y = y;
    point.z = z;
    return point;
}

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

void TestIdentity() {
    g_case = "identity round trip and overflow";
    const uint64_t big_world = 0xFEDCBA9876543210ull;

    cp::EntityKey panel_key{9, 9, 9};
    CHECK(cs::ToPanelKey(Key(kWorld, 0, 1), &panel_key));
    CHECK(panel_key.session == kWorld && panel_key.id == 1u && panel_key.generation == 1u);
    cs::NativeKey native = Key(1, 1, 1);
    CHECK(cs::ToNativeKey(panel_key, &native));
    CHECK(cs::SameKey(native, Key(kWorld, 0, 1)));

    // The largest representable key keeps every bit both ways.
    const cs::NativeKey largest = Key(big_world, 0xFFFFFFFEull, 0xFFFFFFFFull);
    CHECK(cs::ToPanelKey(largest, &panel_key));
    CHECK(panel_key.session == big_world);
    CHECK(panel_key.id == 0xFFFFFFFFu);
    CHECK(panel_key.generation == 0xFFFFFFFFu);
    CHECK(cs::ToNativeKey(panel_key, &native));
    CHECK(cs::SameKey(native, largest));

    // Id overflow is refused, and the output is zeroed rather than truncated.
    panel_key = cp::EntityKey{9, 9, 9};
    CHECK(!cs::ToPanelKey(Key(kWorld, 0xFFFFFFFFull, 1), &panel_key));
    CHECK(panel_key.session == 0 && panel_key.id == 0u && panel_key.generation == 0u);
    CHECK(!cs::ToPanelKey(Key(kWorld, 0x100000000ull, 1), &panel_key));
    CHECK(!cs::ToPanelKey(Key(kWorld, 0x100000003ull, 1), &panel_key));
    CHECK(!cs::ToPanelKey(Key(kWorld, 0xFFFFFFFFFFFFFFFFull, 1), &panel_key));

    // Generation zero and generation overflow are refused.
    CHECK(!cs::ToPanelKey(Key(kWorld, 3, 0), &panel_key));
    CHECK(!cs::ToPanelKey(Key(kWorld, 3, 0x100000000ull), &panel_key));
    CHECK(!cs::ToPanelKey(Key(kWorld, 3, 0x100000001ull), &panel_key));
    CHECK(cs::ToPanelKey(Key(kWorld, 3, 0xFFFFFFFFull), &panel_key));

    // Panel keys that no native key maps to are refused on the way back.
    native = Key(5, 5, 5);
    CHECK(!cs::ToNativeKey(cp::EntityKey{kWorld, 0, 1}, &native));
    CHECK(cs::SameKey(native, Key(0, 0, 0)));
    CHECK(!cs::ToNativeKey(cp::EntityKey{kWorld, 1, 0}, &native));

    // A null output still gives the verdict.
    CHECK(cs::ToPanelKey(Key(kWorld, 3, 1), nullptr));
    CHECK(!cs::ToPanelKey(Key(kWorld, 3, 0), nullptr));
    CHECK(cs::ToNativeKey(cp::EntityKey{kWorld, 4, 1}, nullptr));

    // Round trips are exact and distinct inputs stay distinct.
    const uint64_t ids[4] = {0ull, 1ull, 0x7FFFFFFFull, 0xFFFFFFFEull};
    const uint64_t generations[3] = {1ull, 2ull, 0xFFFFFFFFull};
    int exact = 0;
    int distinct = 0;
    for (int i = 0; i < 4; ++i) {
        for (int g = 0; g < 3; ++g) {
            const cs::NativeKey in = Key(big_world, ids[i], generations[g]);
            cp::EntityKey mid{0, 0, 0};
            cs::NativeKey out;
            if (cs::ToPanelKey(in, &mid) && cs::ToNativeKey(mid, &out) && cs::SameKey(in, out)) {
                ++exact;
            }
            cp::EntityKey first{0, 0, 0};
            cs::ToPanelKey(Key(big_world, ids[0], generations[0]), &first);
            if ((i == 0 && g == 0) || mid != first) ++distinct;
        }
    }
    CHECK(exact == 12);
    CHECK(distinct == 12);
}

void TestClick() {
    const cs::FrameResult started = Start("individual and shift selection");
    CHECK(started.status == cs::FrameStatus::Valid);
    CHECK(g_ctl.FrameUsable());
    // Opening selects nothing and shows nothing.
    CHECK(g_ctl.SelectionCount() == 0);
    CHECK(!g_ctl.Panel().Layout().visible);
    CHECK(!g_ctl.HasRequest());
    CHECK(!g_ctl.HasArmedTarget());

    cs::GestureResult g = Click(800.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::Click);
    CHECK(g.selection.picked);
    CHECK(cs::SameKey(g.selection.picked_key, Key(kWorld, 0, 1)));
    CHECK(g.selection.result.added == 1);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(0));
    Layout();
    CHECK(g_ctl.Panel().Layout().visible);

    // A plain click replaces.
    Click(800.0f, 200.0f);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(3) && !Sel(0));

    // Shift-click toggles on, then off.
    Click(800.0f, 100.0f, true);
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(Sel(0) && Sel(3));
    g = Click(800.0f, 100.0f, true);
    CHECK(g.selection.result.removed == 1);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(!Sel(0) && Sel(3));

    // Empty world: kept when additive, cleared otherwise.
    g = Click(1200.0f, 600.0f, true);
    CHECK(g.outcome == cs::GestureOutcome::Click);
    CHECK(!g.selection.picked);
    CHECK(g_ctl.SelectionCount() == 1);
    g = Click(1200.0f, 600.0f);
    CHECK(!g.selection.picked);
    CHECK(g_ctl.SelectionCount() == 0);

    // The circle edge is inside; one pixel beyond is not.
    g = Click(820.0f, 100.0f);
    CHECK(g.selection.picked && Sel(0));
    g = Click(821.0f, 100.0f);
    CHECK(!g.selection.picked);
    CHECK(g_ctl.SelectionCount() == 0);

    // Unselectable and off-screen units cannot be picked.
    g = Click(1100.0f, 200.0f);
    CHECK(!g.selection.picked);
    CHECK(!Sel(6));
    g = Click(1000.0f, 100.0f);
    CHECK(!g.selection.picked);
    CHECK(!Sel(2));

    // A press that barely moves is still a click.
    cs::GestureResult down = g_ctl.PointerDown(800.0f, 200.0f);
    CHECK(down.outcome == cs::GestureOutcome::WorldPress);
    cs::GestureResult move = g_ctl.PointerMove(802.0f, 201.0f);
    CHECK(move.outcome == cs::GestureOutcome::WorldPress);
    g = g_ctl.PointerUp(802.0f, 201.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::Click);
    CHECK(Sel(3));
    CHECK(g_ctl.SelectionCount() == 1);
}

void TestNearestAndTies() {
    g_case = "nearest unit and tie order";
    g_ctl.Reset();
    g_units[0] = MakeUnit(kWorld, 9, 1, 840.0f, 100.0f);
    g_units[1] = MakeUnit(kWorld, 4, 1, 800.0f, 100.0f);
    g_units[0].screen.radius = 50.0f;
    g_units[1].screen.radius = 50.0f;
    CHECK(g_ctl.BeginFrame(MakeFrame(kWorld, 1, 1, g_units, 2)).status ==
          cs::FrameStatus::Valid);

    cs::GestureResult g = Click(825.0f, 100.0f);
    CHECK(g.selection.picked && cs::SameKey(g.selection.picked_key, Key(kWorld, 9, 1)));
    g = Click(805.0f, 100.0f);
    CHECK(g.selection.picked && cs::SameKey(g.selection.picked_key, Key(kWorld, 4, 1)));
    // Exactly between the two: the lowest native id wins.
    g = Click(820.0f, 100.0f);
    CHECK(g.selection.picked && cs::SameKey(g.selection.picked_key, Key(kWorld, 4, 1)));
    CHECK(g_ctl.SelectionCount() == 1);

    // The same tie with the rows in the other order gives the same answer.
    const cs::UnitSnapshot swap = g_units[0];
    g_units[0] = g_units[1];
    g_units[1] = swap;
    CHECK(g_ctl.BeginFrame(MakeFrame(kWorld, 2, 1, g_units, 2)).status ==
          cs::FrameStatus::Valid);
    g = Click(820.0f, 100.0f);
    CHECK(g.selection.picked && cs::SameKey(g.selection.picked_key, Key(kWorld, 4, 1)));
}

void TestNonFinite() {
    g_case = "non-finite positions and radii";
    g_ctl.Reset();
    const float not_number = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    g_units[0] = MakeUnit(kWorld, 0, 1, not_number, 100.0f);
    g_units[1] = MakeUnit(kWorld, 1, 1, 800.0f, 100.0f);
    g_units[1].screen.radius = inf;
    g_units[2] = MakeUnit(kWorld, 2, 1, 900.0f, 100.0f);
    g_units[2].screen.radius = -5.0f;
    g_units[3] = MakeUnit(kWorld, 3, 1, 1000.0f, 100.0f);
    const cs::FrameResult frame = g_ctl.BeginFrame(MakeFrame(kWorld, 1, 1, g_units, 4));
    CHECK(frame.status == cs::FrameStatus::Valid);
    CHECK(frame.rejected_circles == 3);

    // An infinite radius would contain every point; it contains none.
    cs::GestureResult g = Click(800.0f, 100.0f);
    CHECK(!g.selection.picked);
    g = Click(900.0f, 100.0f);
    CHECK(!g.selection.picked);
    CHECK(g_ctl.SelectionCount() == 0);
    g = Click(1000.0f, 100.0f);
    CHECK(g.selection.picked && cs::SameKey(g.selection.picked_key, Key(kWorld, 3, 1)));
    CHECK(g_ctl.SelectionCount() == 1);

    // Pointer events without a usable position neither select nor clear.
    g = g_ctl.PointerDown(not_number, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::None);
    g = g_ctl.PointerUp(not_number, 100.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(g_ctl.SelectionCount() == 1);
    g = g_ctl.PointerDown(1200.0f, 400.0f);
    CHECK(g.outcome == cs::GestureOutcome::WorldPress);
    g = g_ctl.PointerUp(inf, not_number, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(g_ctl.SelectionCount() == 1);
    g = g_ctl.PointerDown(800.0f, inf);
    CHECK(g.outcome == cs::GestureOutcome::None);
    g = g_ctl.PointerUp(1200.0f, 400.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(g_ctl.SelectionCount() == 1);

    // A box over everything takes only the unit with a usable circle.
    Click(1200.0f, 400.0f);
    CHECK(g_ctl.SelectionCount() == 0);
    g = Drag(700.0f, 50.0f, 1100.0f, 150.0f);
    CHECK(g.outcome == cs::GestureOutcome::Box);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(3));

    // Selectability does not depend on being projected.
    g_ctl.SelectAll();
    CHECK(g_ctl.SelectionCount() == 4);
}

void TestWholeSquad() {
    Start("whole-squad selection");
    cs::GestureResult g = Click(800.0f, 100.0f, false, true);
    CHECK(g.outcome == cs::GestureOutcome::Click);
    CHECK(g.selection.whole_squad);
    CHECK(g_ctl.SelectionCount() == 3);
    // Unit 2 is off screen but is a current member.
    CHECK(Sel(0) && Sel(1) && Sel(2));
    // Same squad id with another generation is another squad.
    CHECK(!Sel(9));
    CHECK(!Sel(3));

    // No squad descriptor: just the unit.
    g = Click(800.0f, 200.0f, false, true);
    CHECK(!g.selection.whole_squad);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(3));

    // A descriptor with id 0 is not a squad; its twin is not pulled in.
    g = Click(800.0f, 300.0f, false, true);
    CHECK(!g.selection.whole_squad);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(7) && !Sel(8));

    // Additive whole-squad adds the squad, then removes it again.
    Click(800.0f, 200.0f);
    Click(900.0f, 100.0f, true, true);
    CHECK(g_ctl.SelectionCount() == 4);
    CHECK(Sel(0) && Sel(1) && Sel(2) && Sel(3));
    Click(800.0f, 100.0f, true, true);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(3));

    // Selecting a squad is selection only: nothing is armed or queued.
    Click(800.0f, 100.0f, false, true);
    CHECK(!g_ctl.HasRequest());
    CHECK(!g_ctl.HasArmedTarget());
}

void TestBox() {
    Start("box selection");
    cs::GestureResult g = g_ctl.PointerDown(760.0f, 60.0f);
    CHECK(g.outcome == cs::GestureOutcome::WorldPress);
    cp::Rect rect{1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(!g_ctl.DragRect(&rect));
    g = g_ctl.PointerMove(900.0f, 150.0f);
    CHECK(g.outcome == cs::GestureOutcome::WorldDrag);
    CHECK(g_ctl.DragRect(&rect));
    CHECK(rect.x == 760.0f && rect.y == 60.0f && rect.w == 140.0f && rect.h == 90.0f);
    g = g_ctl.PointerUp(1040.0f, 240.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::Box);
    CHECK(g_ctl.SelectionCount() == 5);
    CHECK(Sel(0) && Sel(1) && Sel(3) && Sel(4) && Sel(5));
    CHECK(!Sel(2) && !Sel(6));
    CHECK(!g_ctl.DragRect(&rect));

    // Dragging up and to the left gives the same rectangle.
    g = Drag(820.0f, 220.0f, 780.0f, 180.0f);
    CHECK(g.outcome == cs::GestureOutcome::Box);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(3));

    // Additive box keeps what was selected.
    Click(800.0f, 100.0f);
    g = Drag(780.0f, 180.0f, 820.0f, 220.0f, true);
    CHECK(g.selection.result.added == 1);
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(Sel(0) && Sel(3));

    // A box that only touches the circle selects; one that misses clears.
    Drag(700.0f, 90.0f, 785.0f, 110.0f);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(0));
    Drag(700.0f, 90.0f, 775.0f, 110.0f);
    CHECK(g_ctl.SelectionCount() == 0);

    // An empty additive box changes nothing.
    Click(800.0f, 100.0f);
    g = Drag(700.0f, 400.0f, 740.0f, 440.0f, true);
    CHECK(g.outcome == cs::GestureOutcome::Box);
    CHECK(!g.selection.result.changed);
    CHECK(g_ctl.SelectionCount() == 1);
}

void TestSavedSetsAndSelectAll() {
    Start("saved sets and select all");
    Click(800.0f, 100.0f);
    Click(800.0f, 200.0f, true);
    CHECK(g_ctl.SaveSet(0));
    CHECK(g_ctl.SetCount(0) == 2);
    CHECK(!g_ctl.SaveSet(-1));
    CHECK(!g_ctl.SaveSet(cp::kMaxSavedSets));

    Click(1000.0f, 200.0f);
    CHECK(g_ctl.SelectionCount() == 1);
    cs::SelectionChange change = g_ctl.RecallSet(0, false);
    CHECK(change.result.changed);
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(Sel(0) && Sel(3));
    // A bookmark holding one squad member does not pull in the squad.
    CHECK(!Sel(1) && !Sel(2));

    Click(1000.0f, 200.0f);
    g_ctl.RecallSet(0, true);
    CHECK(g_ctl.SelectionCount() == 3);
    CHECK(Sel(0) && Sel(3) && Sel(5));

    // Bookmarks hold exact keys: a replaced unit drops out and its successor
    // is not adopted.
    BuildScene(g_units, kWorld);
    g_units[3].key.generation = 2;
    const cs::FrameResult frame = Refresh(2, 1, kSceneUnits);
    CHECK(frame.status == cs::FrameStatus::Valid);
    CHECK(frame.pruned_saved == 1);
    CHECK(frame.pruned_selection == 1);
    CHECK(g_ctl.SetCount(0) == 1);
    g_ctl.RecallSet(0, false);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(0));
    CHECK(!g_ctl.IsSelected(Key(kWorld, 3, 2)));

    g_ctl.ClearSet(0);
    CHECK(g_ctl.SetCount(0) == 0);
    Click(1000.0f, 200.0f);
    change = g_ctl.RecallSet(0, false);
    CHECK(!change.result.changed);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(5));

    // Select All takes every selectable unit and nothing else.
    change = g_ctl.SelectAll();
    CHECK(change.result.changed);
    CHECK(g_ctl.SelectionCount() == 9);
    CHECK(!Sel(6));
    CHECK(Sel(2) && Sel(9));
    g_ctl.ClearSelection();
    CHECK(g_ctl.SelectionCount() == 0);
}

void TestBoundedSelection() {
    g_case = "bounded stores";
    g_ctl.Reset();
    // 100 selectable units, listed with the highest id first.
    for (int i = 0; i < 100; ++i) {
        g_units[i] = MakeUnit(kWorld, static_cast<uint64_t>(99 - i), 1, 0.0f, 0.0f);
        g_units[i].screen.projected = false;
    }
    CHECK(g_ctl.BeginFrame(MakeFrame(kWorld, 1, 1, g_units, 100)).status ==
          cs::FrameStatus::Valid);
    const cs::SelectionChange change = g_ctl.SelectAll();
    CHECK(g_ctl.SelectionCount() == cp::kMaxSelection);
    CHECK(change.result.rejected_capacity == 100 - cp::kMaxSelection);
    // The lowest ids are kept, whatever the row order.
    CHECK(Sel(0) && Sel(63));
    CHECK(!Sel(64) && !Sel(99));

    // A frame exactly at capacity is valid; one more row is not.
    for (int i = 0; i < cs::kMaxUnits + 1; ++i) {
        g_units[i] = MakeUnit(kWorld, static_cast<uint64_t>(i), 1, 0.0f, 0.0f);
        g_units[i].screen.projected = false;
    }
    CHECK(g_ctl.BeginFrame(MakeFrame(kWorld, 2, 1, g_units, cs::kMaxUnits)).status ==
          cs::FrameStatus::Valid);
    CHECK(g_ctl.SelectionCount() == cp::kMaxSelection);
    CHECK(g_ctl.BeginFrame(MakeFrame(kWorld, 3, 1, g_units, cs::kMaxUnits + 1)).status ==
          cs::FrameStatus::Oversized);
    CHECK(g_ctl.SelectionCount() == cp::kMaxSelection);
}

void TestEligibility() {
    Start("human and unsupported eligibility");
    Click(800.0f, 200.0f);
    Click(900.0f, 200.0f, true);
    Click(1000.0f, 200.0f, true);
    CHECK(g_ctl.SelectionCount() == 3);

    // Only the commandable unit that reports Move is included.
    const cp::ActionSummary move = g_ctl.Panel().Summarize(cp::Action::Move);
    CHECK(move.eligible == 1);
    CHECK(move.blocked_human == 1);
    CHECK(move.blocked_unsupported == 1);
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Armed);
    cs::Request request{};
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 2.0f, 3.0f), &request) ==
          cs::TargetStatus::Submitted);
    CHECK(request.key_count == 1);
    CHECK(cs::SameKey(request.keys[0], Key(kWorld, 3, 1)));
    CHECK(request.selected_count == 3);
    CHECK(request.blocked_count == 2);

    // A piloted unit takes no AI order even though the host listed Move/Hold.
    Click(900.0f, 200.0f);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(4));
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::NotEligible);
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::NotEligible);
    CHECK(Arm(cp::Action::TakeControl) == cs::ArmStatus::NotEligible);
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(!g_ctl.HasRequest());
    CHECK(Arm(cp::Action::ReturnToAI) == cs::ArmStatus::Queued);
    request = cs::Request{};
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.action == cp::Action::ReturnToAI);
    CHECK(request.key_count == 1 && cs::SameKey(request.keys[0], Key(kWorld, 4, 1)));

    // An action the host did not report is neither shown nor accepted.
    Click(1000.0f, 200.0f);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(5));
    CHECK(!g_ctl.Panel().Summarize(cp::Action::Move).shown);
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::NotEligible);
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::NotEligible);
    // Focus is passed through as a request without a point.
    CHECK(Arm(cp::Action::Focus) == cs::ArmStatus::Queued);
    request = cs::Request{};
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.action == cp::Action::Focus);
    CHECK(!request.has_point);

    // Nothing is invented: unreported and out-of-range actions are refused,
    // while a reported generic action passes through.
    Click(800.0f, 200.0f);
    CHECK(!g_ctl.Panel().Summarize(cp::Action::RequestVehicle).shown);
    CHECK(Arm(cp::Action::RequestVehicle) == cs::ArmStatus::NotEligible);
    CHECK(Arm(cp::Action::ReturnToAI) == cs::ArmStatus::NotEligible);
    CHECK(Arm(cp::Action::Count) == cs::ArmStatus::NotEligible);
    CHECK(Arm(cp::Action::Advance) == cs::ArmStatus::Queued);
    request = cs::Request{};
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.action == cp::Action::Advance);
}

void TestTakeControl() {
    Start("one-target take control");
    Click(800.0f, 100.0f);
    Click(800.0f, 200.0f, true);
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(Arm(cp::Action::TakeControl) == cs::ArmStatus::NotEligible);
    Layout();
    const cp::Control* control = FindControl(cp::Action::TakeControl);
    CHECK(control != nullptr);
    CHECK(control != nullptr && !control->enabled);
    cs::GestureResult g = PressControl(cp::Action::TakeControl);
    CHECK(g.outcome == cs::GestureOutcome::PanelOwned);
    CHECK(g.panel.outcome == cp::PointerOutcome::ConsumedDisabled);
    CHECK(g.action == cs::ArmStatus::None);
    CHECK(!g_ctl.HasRequest());

    Click(800.0f, 200.0f);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Arm(cp::Action::TakeControl) == cs::ArmStatus::Queued);
    cs::Request request{};
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.action == cp::Action::TakeControl);
    CHECK(request.key_count == 1);
    CHECK(request.selected_count == 1);
    CHECK(cs::SameKey(request.keys[0], Key(kWorld, 3, 1)));

    // Widening the selection drops a queued take instead of picking a survivor.
    g = PressControl(cp::Action::TakeControl);
    CHECK(g.action == cs::ArmStatus::Queued);
    CHECK(g_ctl.HasRequest());
    Click(800.0f, 100.0f, true);
    CHECK(!g_ctl.HasRequest());
    CHECK(g_ctl.LastCancelReason() == cs::CancelReason::SelectionChanged);
}

void ExpectUnusable(const cs::FrameResult& frame, cs::FrameStatus want) {
    CHECK(frame.status == want);
    CHECK(g_ctl.LastFrameStatus() == want);
    CHECK(!g_ctl.FrameUsable());
    // Nothing was pruned.
    CHECK(frame.pruned_selection == 0 && frame.pruned_saved == 0);
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(Sel(0) && Sel(3));
    CHECK(g_ctl.SetCount(1) == 2);
    // Nothing can be started.
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Unavailable);
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Unavailable);
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(!g_ctl.HasRequest());
}

void TestInvalidFrames() {
    Start("invalid frame: incomplete");
    Click(800.0f, 100.0f);
    Click(800.0f, 200.0f, true);
    CHECK(g_ctl.SaveSet(1));
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Armed);

    cs::Frame frame = MakeFrame(kWorld, 2, 1, g_units, kSceneUnits);
    frame.complete = false;
    cs::FrameResult result = g_ctl.BeginFrame(frame);
    CHECK(result.pending_cancelled);
    CHECK(result.cancel_reason == cs::CancelReason::InvalidFrame);
    ExpectUnusable(result, cs::FrameStatus::Incomplete);

    // Picking and every selection route are off; the selection stays as it was.
    Layout();
    CHECK(!g_ctl.Panel().Layout().visible);
    cs::GestureResult g = Click(900.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::None);
    g = Click(1200.0f, 600.0f);
    CHECK(g.outcome == cs::GestureOutcome::None);
    g = Drag(760.0f, 60.0f, 1040.0f, 240.0f);
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(!g_ctl.SelectAll().result.changed);
    CHECK(!g_ctl.RecallSet(1, false).result.changed);
    cs::Request request{};
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 2.0f, 3.0f), &request) ==
          cs::TargetStatus::NoArmedTarget);
    cs::NativeKey first;
    CHECK(g_ctl.SelectionKey(0, &first) && cs::SameKey(first, Key(kWorld, 0, 1)));
    ExpectUnusable(result, cs::FrameStatus::Incomplete);

    g_case = "invalid frame: oversized";
    frame = MakeFrame(kWorld, 3, 1, g_units, cs::kMaxUnits + 1);
    result = g_ctl.BeginFrame(frame);
    ExpectUnusable(result, cs::FrameStatus::Oversized);

    g_case = "invalid frame: negative count";
    frame = MakeFrame(kWorld, 4, 1, g_units, -1);
    result = g_ctl.BeginFrame(frame);
    ExpectUnusable(result, cs::FrameStatus::BadCount);

    g_case = "invalid frame: null rows";
    frame = MakeFrame(kWorld, 5, 1, nullptr, 3);
    result = g_ctl.BeginFrame(frame);
    ExpectUnusable(result, cs::FrameStatus::NullUnits);

    g_case = "invalid frame: world zero";
    BuildScene(g_units, kWorld);
    frame = MakeFrame(0, 6, 1, g_units, kSceneUnits);
    result = g_ctl.BeginFrame(frame);
    ExpectUnusable(result, cs::FrameStatus::BadWorld);

    g_case = "invalid frame: id overflow";
    BuildScene(g_units, kWorld);
    g_units[4].key.id = 0xFFFFFFFFull;
    result = g_ctl.BeginFrame(MakeFrame(kWorld, 7, 1, g_units, kSceneUnits));
    CHECK(result.bad_index == 4);
    ExpectUnusable(result, cs::FrameStatus::BadKey);

    g_case = "invalid frame: generation overflow";
    BuildScene(g_units, kWorld);
    g_units[2].key.generation = 0x100000001ull;
    result = g_ctl.BeginFrame(MakeFrame(kWorld, 8, 1, g_units, kSceneUnits));
    CHECK(result.bad_index == 2);
    ExpectUnusable(result, cs::FrameStatus::BadKey);

    g_case = "invalid frame: generation zero";
    BuildScene(g_units, kWorld);
    g_units[8].key.generation = 0;
    result = g_ctl.BeginFrame(MakeFrame(kWorld, 9, 1, g_units, kSceneUnits));
    CHECK(result.bad_index == 8);
    ExpectUnusable(result, cs::FrameStatus::BadKey);

    g_case = "invalid frame: row from another world";
    BuildScene(g_units, kWorld);
    g_units[5].key.world = kWorld + 1;
    result = g_ctl.BeginFrame(MakeFrame(kWorld, 10, 1, g_units, kSceneUnits));
    CHECK(result.bad_index == 5);
    ExpectUnusable(result, cs::FrameStatus::WorldMismatch);

    g_case = "invalid frame: duplicate native id";
    BuildScene(g_units, kWorld);
    g_units[6].key.id = 3;
    g_units[6].key.generation = 2;
    result = g_ctl.BeginFrame(MakeFrame(kWorld, 11, 1, g_units, kSceneUnits));
    CHECK(result.bad_index == 6);
    ExpectUnusable(result, cs::FrameStatus::DuplicateId);

    // A complete valid frame restores everything that was kept.
    g_case = "invalid frame: recovery";
    BuildScene(g_units, kWorld);
    result = Refresh(12, 1, kSceneUnits);
    CHECK(result.status == cs::FrameStatus::Valid);
    CHECK(g_ctl.FrameUsable());
    CHECK(result.pruned_selection == 0 && result.pruned_saved == 0);
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(Sel(0) && Sel(3));
    CHECK(g_ctl.SetCount(1) == 2);
    CHECK(g_ctl.Panel().Layout().visible);
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Armed);
    g_ctl.CancelArmedTarget();
    g = Click(900.0f, 100.0f, true);
    CHECK(g.outcome == cs::GestureOutcome::Click);
    CHECK(g_ctl.SelectionCount() == 3);

    // Pruning still happens, but only on the word of a valid frame.
    BuildScene(g_units, kWorld);
    g_units[3] = g_units[kSceneUnits - 1];
    result = Refresh(13, 1, kSceneUnits - 1);
    CHECK(result.status == cs::FrameStatus::Valid);
    CHECK(result.pruned_selection == 1);
    CHECK(result.pruned_saved == 1);
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(g_ctl.SetCount(1) == 1);
}

void TestArmSurvivesFrame() {
    StartArmed("ordinary frame increment keeps the arm");
    cp::Action armed = cp::Action::Hold;
    CHECK(g_ctl.ArmedAction(&armed) && armed == cp::Action::Move);

    BuildScene(g_units, kWorld);
    cs::FrameResult frame = Refresh(2, 1, kSceneUnits);
    CHECK(frame.status == cs::FrameStatus::Valid);
    CHECK(!frame.pending_cancelled);
    CHECK(g_ctl.HasArmedTarget());
    const cp::Control* control = FindControl(cp::Action::Move);
    CHECK(control != nullptr);
    CHECK(control != nullptr && control->host_armed);

    frame = Refresh(3, 1, kSceneUnits);
    CHECK(!frame.pending_cancelled);
    CHECK(g_ctl.HasArmedTarget());

    cs::Request request{};
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(10.5f, -3.25f, 2.0f), &request) ==
          cs::TargetStatus::Submitted);
    CHECK(request.action == cp::Action::Move);
    CHECK(request.has_point);
    CHECK(request.point.x == 10.5f && request.point.y == -3.25f && request.point.z == 2.0f);
    CHECK(request.world == kWorld);
    CHECK(request.snapshot_generation == 3u);
    CHECK(request.context_generation == 1u);
    CHECK(request.key_count == 2);
    CHECK(cs::SameKey(request.keys[0], Key(kWorld, 0, 1)));
    CHECK(cs::SameKey(request.keys[1], Key(kWorld, 3, 1)));

    // Submitting clears the arm: it cannot be submitted twice or taken later.
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 1.0f, 1.0f), &request) ==
          cs::TargetStatus::NoArmedTarget);
    CHECK(!g_ctl.HasRequest());
    Refresh(4, 1, kSceneUnits);
    control = FindControl(cp::Action::Move);
    CHECK(control != nullptr);
    CHECK(control != nullptr && !control->host_armed);
}

void TestArmCancelled() {
    cs::Request request{};

    // While a target is armed a world click is a target point, so selection
    // changes reach an arm through the other routes.
    StartArmed("arm cancelled: select all");
    g_ctl.SelectAll();
    CHECK(g_ctl.SelectionCount() == 9);
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.LastCancelReason() == cs::CancelReason::SelectionChanged);
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 2.0f, 3.0f), &request) ==
          cs::TargetStatus::NoArmedTarget);

    Start("arm cancelled: saved set recalled");
    Click(900.0f, 100.0f);
    CHECK(g_ctl.SaveSet(2));
    Click(800.0f, 100.0f);
    Click(800.0f, 200.0f, true);
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Armed);
    g_ctl.RecallSet(2, false);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(1));
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.LastCancelReason() == cs::CancelReason::SelectionChanged);

    // Once the host drops the arm, the next click selects again.
    StartArmed("arm cancelled: host drops the arm, then selects");
    g_ctl.CancelArmedTarget();
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(Click(900.0f, 100.0f, true).outcome == cs::GestureOutcome::Click);
    CHECK(g_ctl.SelectionCount() == 3);

    StartArmed("arm cancelled: selection cleared");
    g_ctl.ClearSelection();
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.LastCancelReason() == cs::CancelReason::SelectionChanged);

    StartArmed("arm cancelled: unit became piloted");
    BuildScene(g_units, kWorld);
    g_units[3].facts.control = cp::ControlOwner::LocalHuman;
    cs::FrameResult frame = Refresh(2, 1, kSceneUnits);
    CHECK(frame.pending_cancelled);
    CHECK(frame.cancel_reason == cs::CancelReason::UnitNotEligible);
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.SelectionCount() == 2);
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 2.0f, 3.0f), &request) ==
          cs::TargetStatus::NoArmedTarget);

    StartArmed("arm cancelled: capability withdrawn");
    BuildScene(g_units, kWorld);
    g_units[0].facts.capabilities = cp::ActionBit(cp::Action::Focus);
    frame = Refresh(2, 1, kSceneUnits);
    CHECK(frame.cancel_reason == cs::CancelReason::UnitNotEligible);
    CHECK(!g_ctl.HasArmedTarget());

    StartArmed("arm cancelled: context changed");
    BuildScene(g_units, kWorld);
    frame = Refresh(2, 2, kSceneUnits);
    CHECK(frame.context_changed);
    CHECK(frame.pending_cancelled);
    CHECK(frame.cancel_reason == cs::CancelReason::ContextChanged);
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.SelectionCount() == 2);
    const cp::Control* control = FindControl(cp::Action::Move);
    CHECK(control != nullptr);
    CHECK(control != nullptr && !control->host_armed);

    StartArmed("arm cancelled: unit removed");
    BuildScene(g_units, kWorld);
    g_units[3] = g_units[kSceneUnits - 1];
    frame = Refresh(2, 1, kSceneUnits - 1);
    CHECK(frame.pending_cancelled);
    CHECK(frame.cancel_reason == cs::CancelReason::UnitStale);
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.SelectionCount() == 1);

    StartArmed("arm cancelled: unit reported dead");
    BuildScene(g_units, kWorld);
    g_units[3].facts.availability = cp::Availability::Dead;
    frame = Refresh(2, 1, kSceneUnits);
    CHECK(frame.cancel_reason == cs::CancelReason::UnitNotEligible);
    CHECK(!g_ctl.HasArmedTarget());

    StartArmed("arm cancelled: world changed");
    BuildScene(g_units, kWorld + 1);
    frame = g_ctl.BeginFrame(MakeFrame(kWorld + 1, 2, 1, g_units, kSceneUnits));
    CHECK(frame.status == cs::FrameStatus::Valid);
    CHECK(frame.world_changed);
    CHECK(frame.pending_cancelled);
    CHECK(frame.cancel_reason == cs::CancelReason::WorldChanged);
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.SelectionCount() == 0);

    StartArmed("arm cancelled: host cancel");
    g_ctl.Cancel();
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.LastCancelReason() == cs::CancelReason::HostCancel);
    CHECK(g_ctl.SelectionCount() == 2);

    StartArmed("arm cancelled: target cancelled");
    g_ctl.CancelArmedTarget();
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(!g_ctl.HasRequest());

    // A selected unit that was left out stays left out, even once it
    // qualifies: the arm keeps its own keys and is not rebuilt.
    Start("arm keeps its exact keys");
    Click(800.0f, 200.0f);
    Click(900.0f, 200.0f, true);
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Armed);
    BuildScene(g_units, kWorld);
    g_units[4].facts.control = cp::ControlOwner::CommandAI;
    frame = Refresh(2, 1, kSceneUnits);
    CHECK(!frame.pending_cancelled);
    CHECK(g_ctl.HasArmedTarget());
    CHECK(g_ctl.Panel().Summarize(cp::Action::Move).eligible == 2);
    request = cs::Request{};
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 2.0f, 3.0f), &request) ==
          cs::TargetStatus::Submitted);
    CHECK(request.key_count == 1);
    CHECK(cs::SameKey(request.keys[0], Key(kWorld, 3, 1)));
}

void TestStaleAlias() {
    StartArmed("stale alias: id reused with a new generation");
    CHECK(g_ctl.SaveSet(0));
    BuildScene(g_units, kWorld);
    g_units[3].key.generation = 2;
    cs::FrameResult frame = Refresh(2, 1, kSceneUnits);
    CHECK(frame.status == cs::FrameStatus::Valid);
    CHECK(frame.pending_cancelled);
    CHECK(frame.cancel_reason == cs::CancelReason::UnitStale);
    CHECK(!g_ctl.HasArmedTarget());
    // The old key is gone and the new lifetime was not adopted anywhere.
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(0));
    CHECK(!g_ctl.IsSelected(Key(kWorld, 3, 1)));
    CHECK(!g_ctl.IsSelected(Key(kWorld, 3, 2)));
    CHECK(g_ctl.SetCount(0) == 1);
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Armed);
    cs::Request request{};
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 2.0f, 3.0f), &request) ==
          cs::TargetStatus::Submitted);
    CHECK(request.key_count == 1);
    CHECK(cs::SameKey(request.keys[0], Key(kWorld, 0, 1)));

    Start("stale alias: queued request");
    Click(800.0f, 200.0f);
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    BuildScene(g_units, kWorld);
    g_units[3].key.generation = 2;
    frame = Refresh(2, 1, kSceneUnits);
    CHECK(frame.pending_cancelled);
    CHECK(!g_ctl.HasRequest());
    request = cs::Request{};
    request.key_count = 99;
    CHECK(!g_ctl.TakeRequest(&request));
    CHECK(request.key_count == 99);

    // An id whose low 32 bits match a selected unit is refused outright; the
    // selection is neither re-bound to it nor pruned by that frame.
    Start("stale alias: truncating id");
    Click(800.0f, 200.0f);
    CHECK(Sel(3));
    BuildScene(g_units, kWorld);
    g_units[3].key.id = 0x100000003ull;
    frame = g_ctl.BeginFrame(MakeFrame(kWorld, 2, 1, g_units, kSceneUnits));
    CHECK(frame.status == cs::FrameStatus::BadKey);
    CHECK(frame.bad_index == 3);
    CHECK(g_ctl.SelectionCount() == 1);
    CHECK(Sel(3));
    CHECK(!g_ctl.IsSelected(Key(kWorld, 0x100000003ull, 1)));
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Unavailable);
    BuildScene(g_units, kWorld);
    g_units[3] = g_units[kSceneUnits - 1];
    frame = Refresh(3, 1, kSceneUnits - 1);
    CHECK(frame.status == cs::FrameStatus::Valid);
    CHECK(g_ctl.SelectionCount() == 0);
}

void TestTargetCoordinates() {
    StartArmed("finite target coordinates");
    const float not_number = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    cs::Request request{};
    request.key_count = 99;
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(not_number, 0.0f, 0.0f), &request) ==
          cs::TargetStatus::NonFinitePoint);
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(0.0f, inf, 0.0f), &request) ==
          cs::TargetStatus::NonFinitePoint);
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(0.0f, 0.0f, -inf), &request) ==
          cs::TargetStatus::NonFinitePoint);
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 2.0f, 3.0f), nullptr) ==
          cs::TargetStatus::NoOutput);
    // Refusals write nothing and leave the target armed for a usable point.
    CHECK(request.key_count == 99);
    CHECK(g_ctl.HasArmedTarget());
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(-4096.0f, 0.0f, 512.5f), &request) ==
          cs::TargetStatus::Submitted);
    CHECK(request.key_count == 2);
    CHECK(request.has_point);
    CHECK(request.point.x == -4096.0f && request.point.y == 0.0f &&
          request.point.z == 512.5f);
    CHECK(!g_ctl.HasArmedTarget());
}

void TestPanelPress() {
    Start("panel-owned press, drag and release");
    Click(800.0f, 200.0f);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3));
    Layout();
    float hold_x = 0.0f;
    float hold_y = 0.0f;
    CHECK(ControlCentre(cp::Action::Hold, &hold_x, &hold_y));

    // Press and release on a ready action: one queued request, no selection
    // change, nothing sent to the world.
    cs::GestureResult g = g_ctl.PointerDown(hold_x, hold_y);
    CHECK(g.outcome == cs::GestureOutcome::PanelOwned);
    CHECK(g.panel.owner == cp::PointerOwner::Panel);
    CHECK(g.panel.outcome == cp::PointerOutcome::Armed);
    g = g_ctl.PointerUp(hold_x, hold_y, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::PanelOwned);
    CHECK(g.panel.outcome == cp::PointerOutcome::IntentEmitted);
    CHECK(g.action == cs::ArmStatus::Queued);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3));
    CHECK(g_ctl.HasRequest());
    cs::Request request{};
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.action == cp::Action::Hold);
    CHECK(!request.has_point);
    CHECK(request.key_count == 1 && cs::SameKey(request.keys[0], Key(kWorld, 3, 1)));

    // A targeted action arms instead of queueing.
    g = PressControl(cp::Action::Move);
    CHECK(g.panel.outcome == cp::PointerOutcome::IntentEmitted);
    CHECK(g.action == cs::ArmStatus::Armed);
    CHECK(g_ctl.HasArmedTarget());
    CHECK(!g_ctl.HasRequest());

    // While armed, a world click is a target point and never a selection:
    // not on empty ground and not on another unit.
    g = g_ctl.PointerDown(1200.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::WorldPress);
    g = g_ctl.PointerUp(1200.0f, 100.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::TargetPoint);
    CHECK(g.x == 1200.0f && g.y == 100.0f);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3));
    g = Click(800.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::TargetPoint);
    CHECK(!Sel(0) && Sel(3));
    // A drag while armed is not a target and not a box.
    g = Drag(1200.0f, 100.0f, 1100.0f, 200.0f);
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(g_ctl.HasArmedTarget());
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3));
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(5.0f, 6.0f, 7.0f), &request) ==
          cs::TargetStatus::Submitted);
    CHECK(request.action == cp::Action::Move);

    // Press on a control, drag out over a unit, release there: the panel
    // keeps the press, so there is no intent and no click on the unit.
    Layout();
    g = g_ctl.PointerDown(hold_x, hold_y);
    CHECK(g.panel.outcome == cp::PointerOutcome::Armed);
    g = g_ctl.PointerMove(800.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::PanelOwned);
    cp::Rect rect{0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(!g_ctl.DragRect(&rect));
    g = g_ctl.PointerUp(800.0f, 100.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::PanelOwned);
    CHECK(g.panel.outcome == cp::PointerOutcome::ReleasedOffControl);
    CHECK(g.action == cs::ArmStatus::None);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3) && !Sel(0));
    CHECK(!g_ctl.HasRequest() && !g_ctl.HasArmedTarget());

    // A press on panel padding is swallowed; a world click there would have
    // cleared the selection.
    const cp::PanelLayout& layout = g_ctl.Panel().Layout();
    CHECK(layout.visible);
    const float pad_x = layout.panel.x + layout.panel.w - 2.0f;
    const float pad_y = layout.panel.y + 2.0f;
    CHECK(g_ctl.Panel().HitTest(pad_x, pad_y) == cp::HitKind::Padding);
    g = g_ctl.PointerDown(pad_x, pad_y);
    CHECK(g.outcome == cs::GestureOutcome::PanelOwned);
    g = g_ctl.PointerUp(pad_x, pad_y, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::PanelOwned);
    CHECK(g.panel.outcome == cp::PointerOutcome::ConsumedPadding);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3));

    // A press that starts in the world stays a world gesture even when it is
    // released on a control: no intent is emitted.
    g = g_ctl.PointerDown(1200.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::WorldPress);
    g = g_ctl.PointerMove(hold_x, hold_y);
    CHECK(g.outcome == cs::GestureOutcome::WorldDrag);
    g = g_ctl.PointerUp(hold_x, hold_y, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::Box);
    CHECK(g.panel.owner == cp::PointerOwner::World);
    CHECK(g.action == cs::ArmStatus::None);
    CHECK(!g_ctl.HasRequest() && !g_ctl.HasArmedTarget());
}

void TestCancelAndHeldButton() {
    Start("cancel and held button");
    Click(800.0f, 200.0f);
    Layout();
    float hold_x = 0.0f;
    float hold_y = 0.0f;
    CHECK(ControlCentre(cp::Action::Hold, &hold_x, &hold_y));

    // Cancel during a panel press: the press stays the panel's until release
    // and produces nothing.
    cs::GestureResult g = g_ctl.PointerDown(hold_x, hold_y);
    CHECK(g.panel.outcome == cp::PointerOutcome::Armed);
    g_ctl.Cancel();
    g = g_ctl.PointerMove(hold_x, hold_y);
    CHECK(g.panel.owner == cp::PointerOwner::Panel);
    g = g_ctl.PointerUp(hold_x, hold_y, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::PanelOwned);
    CHECK(g.panel.owner == cp::PointerOwner::Panel);
    CHECK(g.panel.outcome == cp::PointerOutcome::CancelledByChange);
    CHECK(g.action == cs::ArmStatus::None);
    CHECK(!g_ctl.HasRequest());
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3));

    // Cancel during a world press: the release is not a click.
    g = g_ctl.PointerDown(800.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::WorldPress);
    g_ctl.Cancel();
    g = g_ctl.PointerUp(800.0f, 100.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3) && !Sel(0));

    // Cancel during a drag: no box either.
    g_ctl.PointerDown(760.0f, 60.0f);
    g = g_ctl.PointerMove(900.0f, 150.0f);
    CHECK(g.outcome == cs::GestureOutcome::WorldDrag);
    g_ctl.Cancel();
    cp::Rect rect{0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(!g_ctl.DragRect(&rect));
    g = g_ctl.PointerUp(1040.0f, 240.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3));

    // A release with no press, as when the button was already held while
    // command input opened, does nothing.
    g = g_ctl.PointerMove(800.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::None);
    g = g_ctl.PointerUp(800.0f, 100.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3) && !Sel(0));

    // Explicit suppression swallows even a repeated press until the release.
    g_ctl.SuppressUntilRelease();
    g = g_ctl.PointerDown(800.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::Suppressed);
    g = g_ctl.PointerMove(900.0f, 150.0f);
    CHECK(g.outcome == cs::GestureOutcome::Suppressed);
    g = g_ctl.PointerDown(hold_x, hold_y);
    CHECK(g.outcome == cs::GestureOutcome::Suppressed);
    g = g_ctl.PointerUp(hold_x, hold_y, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::Suppressed);
    CHECK(g.action == cs::ArmStatus::None);
    CHECK(!g_ctl.HasRequest());
    CHECK(g_ctl.SelectionCount() == 1 && Sel(3) && !Sel(0));
    // The next real press works normally.
    g = Click(800.0f, 100.0f);
    CHECK(g.outcome == cs::GestureOutcome::Click);
    CHECK(Sel(0));

    // An unusable frame during a press kills it the same way.
    g = g_ctl.PointerDown(800.0f, 200.0f);
    CHECK(g.outcome == cs::GestureOutcome::WorldPress);
    cs::Frame frame = MakeFrame(kWorld, 2, 1, g_units, kSceneUnits);
    frame.complete = false;
    g_ctl.BeginFrame(frame);
    BuildScene(g_units, kWorld);
    Refresh(3, 1, kSceneUnits);
    g = g_ctl.PointerUp(800.0f, 200.0f, cs::ClickModifiers{});
    CHECK(g.outcome == cs::GestureOutcome::None);
    CHECK(g_ctl.SelectionCount() == 1 && Sel(0));
}

void TestQueue() {
    Start("one-shot queue without overwrite");
    Click(800.0f, 200.0f);
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    CHECK(g_ctl.HasRequest());

    // Nothing replaces an unconsumed request: not a hotkey, not a targeted
    // action, not a panel click.
    CHECK(Arm(cp::Action::Advance) == cs::ArmStatus::QueueFull);
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::QueueFull);
    CHECK(!g_ctl.HasArmedTarget());
    cs::GestureResult g = PressControl(cp::Action::Focus);
    CHECK(g.panel.outcome == cp::PointerOutcome::IntentEmitted);
    CHECK(g.action == cs::ArmStatus::QueueFull);
    cs::Request request{};
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.action == cp::Action::Hold);

    // Taken once, gone.
    CHECK(!g_ctl.HasRequest());
    cs::Request second{};
    second.key_count = 99;
    CHECK(!g_ctl.TakeRequest(&second));
    CHECK(second.key_count == 99);

    // A null output does not consume the request.
    CHECK(Arm(cp::Action::Advance) == cs::ArmStatus::Queued);
    CHECK(!g_ctl.TakeRequest(nullptr));
    CHECK(g_ctl.HasRequest());
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.action == cp::Action::Advance);

    // A click that leaves the selection as it was does not cancel.
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    Click(800.0f, 200.0f);
    CHECK(g_ctl.HasRequest());
    // A real selection change does.
    Click(800.0f, 100.0f);
    CHECK(!g_ctl.HasRequest());
    CHECK(g_ctl.LastCancelReason() == cs::CancelReason::SelectionChanged);
    CHECK(!g_ctl.TakeRequest(&request));

    // An ordinary new frame keeps the request and restamps it.
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    BuildScene(g_units, kWorld);
    cs::FrameResult frame = Refresh(2, 1, kSceneUnits);
    CHECK(!frame.pending_cancelled);
    CHECK(g_ctl.HasRequest());
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.snapshot_generation == 2u);
    CHECK(request.key_count == 1 && cs::SameKey(request.keys[0], Key(kWorld, 0, 1)));

    // A context change drops it.
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    frame = Refresh(3, 2, kSceneUnits);
    CHECK(frame.pending_cancelled);
    CHECK(frame.cancel_reason == cs::CancelReason::ContextChanged);
    CHECK(!g_ctl.HasRequest());

    // An unusable frame drops it.
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    cs::Frame partial = MakeFrame(kWorld, 4, 2, g_units, kSceneUnits);
    partial.complete = false;
    frame = g_ctl.BeginFrame(partial);
    CHECK(frame.pending_cancelled);
    CHECK(!g_ctl.HasRequest());
    CHECK(!g_ctl.TakeRequest(&request));
    frame = Refresh(5, 2, kSceneUnits);
    CHECK(frame.status == cs::FrameStatus::Valid);

    // The host's cancel drops it.
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    g_ctl.Cancel();
    CHECK(!g_ctl.HasRequest());
    CHECK(g_ctl.LastCancelReason() == cs::CancelReason::HostCancel);

    // A ready action takes the place of an armed target, never the reverse.
    CHECK(Arm(cp::Action::Move) == cs::ArmStatus::Armed);
    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    CHECK(!g_ctl.HasArmedTarget());
    CHECK(g_ctl.LastCancelReason() == cs::CancelReason::Replaced);
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(), Point(1.0f, 2.0f, 3.0f), &request) ==
          cs::TargetStatus::NoArmedTarget);
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.action == cp::Action::Hold);
    CHECK(!request.has_point);
}

void TestExactKeysEndToEnd() {
    g_case = "exact keys end to end";
    g_ctl.Reset();
    const uint64_t big_world = 0xFEDCBA9876543210ull;
    const cs::NativeKey key = Key(big_world, 0xFFFFFFFEull, 0xFFFFFFFFull);
    g_units[0] = MakeUnit(key.world, key.id, key.generation, 800.0f, 100.0f);
    g_units[1] = MakeUnit(big_world, 0, 1, 900.0f, 100.0f);
    CHECK(g_ctl.BeginFrame(MakeFrame(big_world, 1, 1, g_units, 2)).status ==
          cs::FrameStatus::Valid);

    const cs::GestureResult g = Click(800.0f, 100.0f);
    CHECK(g.selection.picked && cs::SameKey(g.selection.picked_key, key));
    cs::NativeKey selected;
    CHECK(g_ctl.SelectionKey(0, &selected));
    CHECK(cs::SameKey(selected, key));
    CHECK(!g_ctl.SelectionKey(1, &selected));
    CHECK(g_ctl.IsSelected(key));
    CHECK(!g_ctl.IsSelected(Key(big_world, 0xFFFFFFFEull, 0xFFFFFFFEull)));
    CHECK(!g_ctl.IsSelected(Key(big_world + 1, 0xFFFFFFFEull, 0xFFFFFFFFull)));
    CHECK(!g_ctl.IsSelected(Key(big_world, 0x1FFFFFFFEull, 0xFFFFFFFFull)));
    CHECK(!g_ctl.IsSelected(Key(big_world, 0xFFFFFFFEull, 0x1FFFFFFFFull)));

    CHECK(Arm(cp::Action::Hold) == cs::ArmStatus::Queued);
    cs::Request request{};
    CHECK(g_ctl.TakeRequest(&request));
    CHECK(request.world == big_world);
    CHECK(request.key_count == 1);
    CHECK(cs::SameKey(request.keys[0], key));
}

void TestPressAcrossFrames() {
    Start("world click across ordinary frame");
    g_ctl.PointerDown(800.f, 100.f);
    Refresh(2, 1, kSceneUnits);
    auto gesture = g_ctl.PointerUp(800.f, 100.f, cs::ClickModifiers{});
    CHECK(gesture.outcome == cs::GestureOutcome::Click && Sel(0));

    Start("world box across ordinary frame");
    g_ctl.PointerDown(760.f, 60.f);
    g_ctl.PointerMove(940.f, 240.f);
    Refresh(2, 1, kSceneUnits);
    gesture = g_ctl.PointerUp(940.f, 240.f, cs::ClickModifiers{});
    CHECK(gesture.outcome == cs::GestureOutcome::Box && Sel(0) && Sel(1) && Sel(3));

    Start("panel press across ordinary frame");
    Click(800.f, 200.f); Layout();
    float x=0,y=0;CHECK(ControlCentre(cp::Action::Hold,&x,&y));
    g_ctl.PointerDown(x,y);
    Refresh(2,1,kSceneUnits);
    gesture=g_ctl.PointerUp(x,y,cs::ClickModifiers{});
    CHECK(gesture.panel.outcome==cp::PointerOutcome::IntentEmitted && gesture.action==cs::ArmStatus::Queued);
    cs::Request request{};CHECK(g_ctl.TakeRequest(&request)&&request.action==cp::Action::Hold);

    Start("panel press requires updated layout");
    Click(800.f,200.f);Layout();CHECK(ControlCentre(cp::Action::Hold,&x,&y));
    g_ctl.PointerDown(x,y);
    g_ctl.BeginFrame(MakeFrame(kWorld,2,1,g_units,kSceneUnits));
    gesture=g_ctl.PointerUp(x,y,cs::ClickModifiers{});
    CHECK(gesture.panel.outcome==cp::PointerOutcome::CancelledByChange&&!g_ctl.HasRequest());

    StartArmed("target press across ordinary frame");
    const auto token=g_ctl.TargetToken();g_ctl.PointerDown(1200.f,100.f);
    Refresh(2,1,kSceneUnits);
    gesture=g_ctl.PointerUp(1200.f,100.f,cs::ClickModifiers{});
    CHECK(gesture.outcome==cs::GestureOutcome::TargetPoint&&gesture.target_token==token);
    CHECK(g_ctl.SubmitTarget(gesture.target_token,Point(2,3,4),&request)==cs::TargetStatus::Submitted);

    Start("world press across context change");
    Click(800.f,200.f);g_ctl.PointerDown(800.f,100.f);
    Refresh(2,2,kSceneUnits);
    gesture=g_ctl.PointerUp(800.f,100.f,cs::ClickModifiers{});
    CHECK(gesture.outcome==cs::GestureOutcome::None&&g_ctl.SelectionCount()==1&&Sel(3));

    Start("world press across world change");
    g_ctl.PointerDown(800.f,100.f);BuildScene(g_units,kWorld+1);
    g_ctl.BeginFrame(MakeFrame(kWorld+1,2,1,g_units,kSceneUnits));Layout();
    gesture=g_ctl.PointerUp(800.f,100.f,cs::ClickModifiers{});
    CHECK(gesture.outcome==cs::GestureOutcome::None&&g_ctl.SelectionCount()==0);
}

void TestTargetTickets() {
    StartArmed("delayed target keeps exact arm");
    auto gesture=Click(1200.f,100.f);
    CHECK(gesture.outcome==cs::GestureOutcome::TargetPoint&&gesture.target_token!=0);
    const auto old=gesture.target_token;
    g_ctl.CancelArmedTarget();Click(900.f,100.f);
    CHECK(Arm(cp::Action::Move)==cs::ArmStatus::Armed);
    cs::Request request{};request.key_count=99;
    CHECK(g_ctl.SubmitTarget(old,Point(2,3,4),&request)==cs::TargetStatus::StaleArm);
    CHECK(g_ctl.HasArmedTarget()&&request.key_count==99);
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(),Point(5,6,7),&request)==cs::TargetStatus::Submitted);
    CHECK(request.key_count==1&&cs::SameKey(request.keys[0],Key(kWorld,1,1)));

    StartArmed("target token survives controller reset without reuse");
    const auto beforeReset=g_ctl.TargetToken();
    StartArmed("target token survives controller reset without reuse");
    CHECK(g_ctl.TargetToken()!=beforeReset);
    request.key_count=99;
    CHECK(g_ctl.SubmitTarget(beforeReset,Point(1,2,3),&request)==cs::TargetStatus::StaleArm&&request.key_count==99&&g_ctl.HasArmedTarget());

    StartArmed("target arm replaced during press");
    g_ctl.PointerDown(1200.f,100.f);g_ctl.CancelArmedTarget();
    CHECK(Arm(cp::Action::Move)==cs::ArmStatus::Armed);
    gesture=g_ctl.PointerUp(1200.f,100.f,cs::ClickModifiers{});
    CHECK(gesture.outcome==cs::GestureOutcome::None&&g_ctl.HasArmedTarget());

    Start("target armed after unarmed press");
    Click(800.f,200.f);g_ctl.PointerDown(800.f,100.f);
    CHECK(Arm(cp::Action::Move)==cs::ArmStatus::Armed);
    gesture=g_ctl.PointerUp(800.f,100.f,cs::ClickModifiers{});
    CHECK(gesture.outcome==cs::GestureOutcome::Click&&Sel(0)&&!g_ctl.HasArmedTarget());

    StartArmed("delayed target cannot change action");
    cs::Frame frame=MakeFrame(kWorld,2,1,g_units,kSceneUnits);
    frame.targeted_actions |= cp::ActionBit(cp::Action::Advance);
    g_ctl.BeginFrame(frame);Layout();const auto moveToken=g_ctl.TargetToken();
    CHECK(g_ctl.ArmAction(cp::Action::Advance)==cs::ArmStatus::Armed);
    request.key_count=99;
    CHECK(g_ctl.SubmitTarget(moveToken,Point(3,4,5),&request)==cs::TargetStatus::StaleArm&&request.key_count==99);
    CHECK(g_ctl.SubmitTarget(g_ctl.TargetToken(),Point(3,4,5),&request)==cs::TargetStatus::Submitted&&request.action==cp::Action::Advance);

    StartArmed("target policy change cancels pending work");
    frame=MakeFrame(kWorld,2,1,g_units,kSceneUnits);frame.targeted_actions=0;
    auto result=g_ctl.BeginFrame(frame);
    CHECK(result.pending_cancelled&&result.cancel_reason==cs::CancelReason::TargetKindChanged&&!g_ctl.HasArmedTarget());
    Layout();CHECK(g_ctl.ArmAction(cp::Action::Move)==cs::ArmStatus::Queued);
    frame.snapshot_generation=3;frame.targeted_actions=cp::ActionBit(cp::Action::Move);
    result=g_ctl.BeginFrame(frame);
    CHECK(result.pending_cancelled&&result.cancel_reason==cs::CancelReason::TargetKindChanged&&!g_ctl.HasRequest());
}

}  // namespace

int main() {
    TestIdentity();
    TestClick();
    TestNearestAndTies();
    TestNonFinite();
    TestWholeSquad();
    TestBox();
    TestSavedSetsAndSelectAll();
    TestBoundedSelection();
    TestEligibility();
    TestTakeControl();
    TestInvalidFrames();
    TestArmSurvivesFrame();
    TestArmCancelled();
    TestStaleAlias();
    TestTargetCoordinates();
    TestPanelPress();
    TestCancelAndHeldButton();
    TestQueue();
    TestExactKeysEndToEnd();
    TestPressAcrossFrames();
    TestTargetTickets();

    std::printf("ce_command_selection_driver: %d assertions, %d failed\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
