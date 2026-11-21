// Pure test driver for the Commander selection/action panel.
//
// Synthetic data only: no engine, no renderer, no game files.  Run without
// arguments to execute every control; `--list` prints the control names and
// `--dump` prints one layout for independent geometry checks by the runner.

#include "ce_command_panel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace ce_command_panel;

namespace {

// ---------------------------------------------------------------------------
// Check bookkeeping
// ---------------------------------------------------------------------------

int g_checks = 0;
int g_test_failures = 0;

void Fail(const char* expr, int line) {
    ++g_test_failures;
    std::printf("  check failed at line %d: %s\n", line, expr);
}

#define CHECK(cond)                      \
    do {                                 \
        ++g_checks;                      \
        if (!(cond)) Fail(#cond, __LINE__); \
    } while (0)

#define CHECK_INT(actual, expected)                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        long long a_ = static_cast<long long>(actual);                           \
        long long e_ = static_cast<long long>(expected);                         \
        if (a_ != e_) {                                                          \
            Fail(#actual " == " #expected, __LINE__);                            \
            std::printf("    actual %lld, expected %lld\n", a_, e_);             \
        }                                                                        \
    } while (0)

#define CHECK_STR(actual, expected)                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        std::string a_ = (actual);                                               \
        std::string e_ = (expected);                                             \
        if (a_ != e_) {                                                          \
            Fail(#actual " == " #expected, __LINE__);                            \
            std::printf("    actual \"%s\", expected \"%s\"\n", a_.c_str(), e_.c_str()); \
        }                                                                        \
    } while (0)

// ---------------------------------------------------------------------------
// Synthetic font, world and draw recorder
// ---------------------------------------------------------------------------

struct Font {
    float char_w;
    float char_h;
};

TextSize MeasureText(void* user, const char* text, float scale) {
    const Font* font = static_cast<const Font*>(user);
    TextSize s;
    s.w = static_cast<float>(std::strlen(text)) * font->char_w * scale;
    s.h = font->char_h * scale;
    return s;
}

const uint32_t kOrders = ActionBit(Action::Move) | ActionBit(Action::Advance) |
                         ActionBit(Action::Hold) | ActionBit(Action::FallBack);

void SetText(char* dst, const char* src) {
    std::memset(dst, 0, kNameCapacity);
    std::strncpy(dst, src, kNameCapacity - 1);
}

struct World {
    std::vector<EntitySnapshot> entities;
    uint64_t session = 7;
    uint32_t snapshot_generation = 1;
    uint32_t context_generation = 1;
    uint32_t targeted = 0;
    bool has_armed = false;
    Action armed = Action::Move;

    void Add(uint32_t id, const char* name, EntityKind kind, ControlOwner control,
             uint32_t caps) {
        EntitySnapshot e;
        std::memset(&e, 0, sizeof(e));
        e.key.session = session;
        e.key.id = id;
        e.key.generation = 1;
        e.kind = kind;
        e.relation = TeamRelation::Friendly;
        e.control = control;
        e.availability = Availability::Available;
        e.selectable = true;
        e.capabilities = caps;
        SetText(e.name, name);
        entities.push_back(e);
    }

    EntitySnapshot* Find(uint32_t id) {
        for (size_t i = 0; i < entities.size(); ++i) {
            if (entities[i].key.id == id) return &entities[i];
        }
        std::printf("  driver error: no synthetic entity %u\n", static_cast<unsigned>(id));
        std::exit(2);
    }

    EntityKey Key(uint32_t id) { return Find(id)->key; }

    void SetSquad(uint32_t id, uint32_t squad_id, const char* name, int members) {
        EntitySnapshot* e = Find(id);
        e->squad.present = true;
        e->squad.squad_id = squad_id;
        e->squad.squad_generation = 1;
        e->squad.has_member_count = members > 0;
        e->squad.member_count = static_cast<uint16_t>(members > 0 ? members : 0);
        SetText(e->squad.name, name);
    }

    void Remove(uint32_t id) {
        for (size_t i = 0; i < entities.size(); ++i) {
            if (entities[i].key.id == id) {
                entities.erase(entities.begin() + static_cast<long>(i));
                return;
            }
        }
    }

    void SetSession(uint64_t value) {
        session = value;
        for (size_t i = 0; i < entities.size(); ++i) entities[i].key.session = value;
    }

    // Each call is a new host snapshot of the same synthetic world.
    SnapshotResult Apply(CommandPanel& panel) {
        WorldSnapshot ws;
        std::memset(&ws, 0, sizeof(ws));
        ws.session = session;
        ws.snapshot_generation = snapshot_generation;
        ws.context_generation = context_generation;
        ws.targeted_actions = targeted;
        ws.has_armed_target_action = has_armed;
        ws.armed_target_action = armed;
        ws.entities = entities.empty() ? nullptr : &entities[0];
        ws.entity_count = static_cast<int>(entities.size());
        return panel.ApplySnapshot(ws);
    }
};

// The standard synthetic cast.  Some capability masks are deliberately more
// generous than the control state allows, to prove the panel's own refusals.
World MakeWorld() {
    World w;
    const uint32_t ai_caps =
        kOrders | ActionBit(Action::Focus) | ActionBit(Action::TakeControl);
    w.Add(10, "Gunship 1", EntityKind::Unit, ControlOwner::CommandAI, ai_caps);
    w.Add(11, "Gunship 2", EntityKind::Unit, ControlOwner::CommandAI, ai_caps);
    w.Add(12, "Gunship 3", EntityKind::Unit, ControlOwner::CommandAI, ai_caps);
    w.SetSquad(10, 3, "Alpha", 3);
    w.SetSquad(11, 3, "Alpha", 3);
    w.SetSquad(12, 3, "Alpha", 3);
    w.Add(13, "Tank 1", EntityKind::Unit, ControlOwner::CommandAI, ai_caps);
    w.SetSquad(13, 4, "Bravo", 2);
    w.Add(20, "Player Craft", EntityKind::Unit, ControlOwner::LocalHuman,
          kOrders | ActionBit(Action::ReturnToAI) | ActionBit(Action::Focus));
    w.Add(30, "Escort", EntityKind::Unit, ControlOwner::Scripted,
          kOrders | ActionBit(Action::Focus));
    w.Add(40, "Wreck", EntityKind::Unit, ControlOwner::CommandAI, kOrders);
    w.Find(40)->availability = Availability::Dead;
    w.Add(50, "Factory", EntityKind::Structure, ControlOwner::None,
          ActionBit(Action::RequestVehicle) | ActionBit(Action::Focus));
    w.Add(60, "Enemy Tank", EntityKind::Unit, ControlOwner::IndependentAI,
          ActionBit(Action::Focus));
    w.Find(60)->relation = TeamRelation::Hostile;
    return w;
}

LayoutInput MakeInput(float screen_w, float screen_h, float scale, Font* font) {
    LayoutInput in;
    std::memset(&in, 0, sizeof(in));
    const float margin = 8.0f * scale;
    in.safe.x = margin;
    in.safe.y = margin;
    in.safe.w = screen_w - 2.0f * margin;
    in.safe.h = screen_h - 2.0f * margin;
    in.scale = scale;
    in.measure = MeasureText;
    in.measure_user = font;
    return in;
}

void AddReservation(LayoutInput* in, float x, float y, float w, float h) {
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    in->reservations[in->reservation_count++] = r;
}

struct DrawItem {
    char kind;          // 'F' fill, 'L' line, 'T' text
    Rect rect;          // fill rect, or line as (x0, y0, x1, y1)
    std::string text;
    uint32_t color;
};

struct DrawLog {
    std::vector<DrawItem> items;
};

void LogFill(void* user, Rect rect, uint32_t color) {
    DrawItem it;
    it.kind = 'F';
    it.rect = rect;
    it.color = color;
    static_cast<DrawLog*>(user)->items.push_back(it);
}

void LogLine(void* user, float x0, float y0, float x1, float y1, uint32_t color) {
    DrawItem it;
    it.kind = 'L';
    it.rect.x = x0;
    it.rect.y = y0;
    it.rect.w = x1;
    it.rect.h = y1;
    it.color = color;
    static_cast<DrawLog*>(user)->items.push_back(it);
}

void LogText(void* user, float x, float y, const char* text, float scale, uint32_t color) {
    (void)scale;
    DrawItem it;
    it.kind = 'T';
    it.rect.x = x;
    it.rect.y = y;
    it.rect.w = 0.0f;
    it.rect.h = 0.0f;
    it.text = text;
    it.color = color;
    static_cast<DrawLog*>(user)->items.push_back(it);
}

Palette MakePalette() {
    Palette p;
    p.background = 1;
    p.border = 2;
    p.separator = 3;
    p.text = 4;
    p.text_dim = 5;
    p.text_disabled = 6;
    p.button = 7;
    p.button_hover = 8;
    p.button_pressed = 9;
    p.button_disabled = 10;
    p.button_border = 11;
    p.accent = 12;
    p.human = 13;
    p.structure = 14;
    p.muted = 15;
    p.warning = 16;
    return p;
}

void PaintInto(const CommandPanel& panel, DrawLog* log) {
    DrawCallbacks cb;
    cb.user = log;
    cb.fill_rect = LogFill;
    cb.line = LogLine;
    cb.text = LogText;
    log->items.clear();
    panel.Paint(cb, MakePalette());
}

bool LogHasFill(const DrawLog& log, uint32_t color) {
    for (size_t i = 0; i < log.items.size(); ++i) {
        if (log.items[i].kind == 'F' && log.items[i].color == color) return true;
    }
    return false;
}

bool LogHasLine(const DrawLog& log, uint32_t color) {
    for (size_t i = 0; i < log.items.size(); ++i) {
        if (log.items[i].kind == 'L' && log.items[i].color == color) return true;
    }
    return false;
}

bool LogHasText(const DrawLog& log, const char* text) {
    for (size_t i = 0; i < log.items.size(); ++i) {
        if (log.items[i].kind == 'T' && log.items[i].text == text) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Layout inspection helpers
// ---------------------------------------------------------------------------

int FindControl(const PanelLayout& L, Action action) {
    for (int i = 0; i < L.control_count; ++i) {
        if (L.controls[i].action == action) return i;
    }
    return -1;
}

float CenterX(const Rect& r) { return r.x + r.w * 0.5f; }
float CenterY(const Rect& r) { return r.y + r.h * 0.5f; }

bool HasLine(const PanelLayout& L, const char* text) {
    for (int i = 0; i < L.line_count; ++i) {
        if (std::strcmp(L.lines[i].text, text) == 0) return true;
    }
    return false;
}

// True when `needle` appears in any text the layout could ever draw.
bool AnyText(const PanelLayout& L, const char* needle) {
    for (int i = 0; i < L.line_count; ++i) {
        if (std::strstr(L.lines[i].text, needle) != nullptr) return true;
    }
    for (int i = 0; i < L.control_count; ++i) {
        if (std::strstr(L.controls[i].label, needle) != nullptr) return true;
        if (std::strstr(L.controls[i].detail, needle) != nullptr) return true;
    }
    return std::strstr(L.detail_legend, needle) != nullptr;
}

bool Inside(const Rect& inner, const Rect& outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

bool Overlap(const Rect& a, const Rect& b) {
    if (a.w <= 0.0f || a.h <= 0.0f || b.w <= 0.0f || b.h <= 0.0f) return false;
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

float Measured(const char* text, const Font& font, float scale) {
    return static_cast<float>(std::strlen(text)) * font.char_w * scale;
}

// Every drawn element stays inside the panel, the panel stays inside the
// safe rectangle and clear of reservations, every label fits its box, and
// no two controls or text lines share pixels.
bool LayoutFits(const PanelLayout& L, const LayoutInput& in, const Font& font) {
    if (!L.visible) return true;
    bool ok = true;
    ok = ok && Inside(L.panel, in.safe);
    ok = ok && Inside(L.panel, L.available);
    for (int i = 0; i < in.reservation_count; ++i) {
        ok = ok && !Overlap(L.panel, in.reservations[i]);
    }
    ok = ok && Inside(L.stripe, L.panel);
    for (int i = 0; i < L.line_count; ++i) {
        const TextLine& line = L.lines[i];
        ok = ok && Inside(line.bounds, L.panel);
        ok = ok && !Overlap(line.bounds, L.stripe);
        ok = ok && Measured(line.text, font, L.scale) <= line.bounds.w;
        ok = ok && line.text[0] != '\0';
        for (int j = i + 1; j < L.line_count; ++j) {
            ok = ok && !Overlap(line.bounds, L.lines[j].bounds);
        }
        for (int j = 0; j < L.control_count; ++j) {
            ok = ok && !Overlap(line.bounds, L.controls[j].rect);
        }
    }
    for (int i = 0; i < L.control_count; ++i) {
        const Control& c = L.controls[i];
        ok = ok && Inside(c.rect, L.panel);
        ok = ok && !Overlap(c.rect, L.stripe);
        ok = ok && Inside(c.text_bounds, c.rect);
        ok = ok && Measured(c.label, font, L.scale) <= c.text_bounds.w;
        ok = ok && c.label[0] != '\0';
        for (int j = i + 1; j < L.control_count; ++j) {
            ok = ok && !Overlap(c.rect, L.controls[j].rect);
        }
        if (L.has_detail_row) {
            ok = ok && !Overlap(c.rect, L.detail_row);
            ok = ok && Measured(c.detail, font, L.scale) <= L.detail_row.w;
        }
    }
    if (L.has_separator) ok = ok && Inside(L.separator, L.panel);
    if (L.has_detail_row) {
        ok = ok && Inside(L.detail_row, L.panel);
        ok = ok && Measured(L.detail_legend, font, L.scale) <= L.detail_row.w;
    }
    return ok;
}

std::unique_ptr<CommandPanel> NewPanel() {
    return std::unique_ptr<CommandPanel>(new CommandPanel());
}

void SelectIds(CommandPanel& panel, World& w, SelectOp op, const std::vector<uint32_t>& ids) {
    std::vector<EntityKey> keys;
    for (size_t i = 0; i < ids.size(); ++i) keys.push_back(w.Key(ids[i]));
    panel.Select(op, keys.empty() ? nullptr : &keys[0], static_cast<int>(keys.size()));
}

// A press and release on the centre of one control.
PointerResult Click(CommandPanel& panel, Action action, ActionIntent* intent) {
    const PanelLayout& L = panel.Layout();
    int index = FindControl(L, action);
    PointerResult none;
    none.owner = PointerOwner::World;
    none.outcome = PointerOutcome::World;
    if (index < 0) return none;
    const Rect r = L.controls[index].rect;
    panel.PointerDown(CenterX(r), CenterY(r));
    return panel.PointerUp(CenterX(r), CenterY(r), intent);
}

ActionIntent BlankIntent() {
    ActionIntent intent;
    std::memset(&intent, 0, sizeof(intent));
    intent.key_count = 0xFFFF;   // sentinel: stays put unless an intent is written
    return intent;
}

const float kFarX = 600.0f;   // a world point well away from the panel at 640x480
const float kFarY = 60.0f;

// ---------------------------------------------------------------------------
// Controls: identity, selection, saved sets
// ---------------------------------------------------------------------------

void TestIdentityAndGeneration() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;

    SnapshotResult r = w.Apply(panel);
    CHECK_INT(r.accepted_entities, 9);
    CHECK_INT(r.rejected_entities, 0);
    CHECK(!r.session_changed);

    SelectIds(panel, w, SelectOp::Replace, {10, 11});
    CHECK_INT(panel.SelectionCount(), 2);
    const EntityKey old11 = w.Key(11);
    const uint32_t revision = panel.SelectionRevision();

    // Same id, new generation: the old key is obsolete.
    w.Find(11)->key.generation = 2;
    ++w.snapshot_generation;
    r = w.Apply(panel);
    CHECK_INT(r.pruned_selection, 1);
    CHECK_INT(panel.SelectionCount(), 1);
    CHECK(!panel.IsSelected(old11));
    CHECK(panel.IsSelected(w.Key(10)));
    CHECK(panel.SelectionRevision() != revision);

    SelectResult s = panel.Select(SelectOp::Add, &old11, 1);
    CHECK_INT(s.rejected_stale, 1);
    CHECK(!s.changed);
    CHECK_INT(panel.SelectionCount(), 1);

    const EntityKey new11 = w.Key(11);
    s = panel.Select(SelectOp::Add, &new11, 1);
    CHECK_INT(s.added, 1);
    CHECK_INT(panel.SelectionCount(), 2);

    // An entity that leaves the snapshot is pruned.
    w.Remove(10);
    ++w.snapshot_generation;
    r = w.Apply(panel);
    CHECK_INT(r.pruned_selection, 1);
    CHECK_INT(panel.SelectionCount(), 1);

    // An unchanged world prunes nothing.
    ++w.snapshot_generation;
    r = w.Apply(panel);
    CHECK_INT(r.pruned_selection, 0);
    CHECK_INT(panel.SelectionCount(), 1);

    // A key from another session never matches.
    EntityKey foreign = w.Key(12);
    foreign.session = 99;
    s = panel.Select(SelectOp::Add, &foreign, 1);
    CHECK_INT(s.rejected_stale, 1);
    CHECK(!s.changed);

    // A new session invalidates every stored key.
    w.SetSession(8);
    ++w.snapshot_generation;
    r = w.Apply(panel);
    CHECK(r.session_changed);
    CHECK_INT(panel.SelectionCount(), 0);

    // Malformed rows are refused: id 0 and a repeated id.
    w.Add(0, "Zero", EntityKind::Unit, ControlOwner::CommandAI, kOrders);
    w.Add(12, "Twin", EntityKind::Unit, ControlOwner::CommandAI, kOrders);
    ++w.snapshot_generation;
    r = w.Apply(panel);
    CHECK_INT(r.rejected_entities, 2);
    CHECK_INT(r.accepted_entities, 8);
}

void TestSelectOps() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    w.Apply(panel);

    EntityKey keys[3] = {w.Key(12), w.Key(10), w.Key(10)};
    SelectResult s = panel.Select(SelectOp::Replace, keys, 3);
    CHECK_INT(s.added, 2);
    CHECK(s.changed);
    CHECK_INT(panel.SelectionCount(), 2);
    EntityKey k;
    CHECK(panel.SelectionKey(0, &k) && k.id == 10);
    CHECK(panel.SelectionKey(1, &k) && k.id == 12);
    CHECK(!panel.SelectionKey(2, &k));

    SelectIds(panel, w, SelectOp::Add, {11});
    CHECK_INT(panel.SelectionCount(), 3);
    CHECK(panel.SelectionKey(1, &k) && k.id == 11);

    EntityKey toggle[2] = {w.Key(11), w.Key(20)};
    s = panel.Select(SelectOp::Toggle, toggle, 2);
    CHECK_INT(s.removed, 1);
    CHECK_INT(s.added, 1);
    CHECK(!panel.IsSelected(w.Key(11)));
    CHECK(panel.IsSelected(w.Key(20)));
    CHECK_INT(panel.SelectionCount(), 3);

    const EntityKey ten = w.Key(10);
    s = panel.Select(SelectOp::Remove, &ten, 1);
    CHECK_INT(s.removed, 1);
    CHECK_INT(panel.SelectionCount(), 2);
    s = panel.Select(SelectOp::Remove, &ten, 1);
    CHECK(!s.changed);

    // The host decides what may be newly selected.
    w.Find(60)->selectable = false;
    ++w.snapshot_generation;
    w.Apply(panel);
    const EntityKey sixty = w.Key(60);
    s = panel.Select(SelectOp::Add, &sixty, 1);
    CHECK_INT(s.rejected_unselectable, 1);
    CHECK(!s.changed);

    s = panel.Select(SelectOp::Add, nullptr, 5);
    CHECK(!s.changed);

    // Replacing with nothing valid empties the selection and says why.
    EntityKey stale = w.Key(12);
    stale.generation = 77;
    s = panel.Select(SelectOp::Replace, &stale, 1);
    CHECK_INT(s.rejected_stale, 1);
    CHECK_INT(s.removed, 2);
    CHECK_INT(panel.SelectionCount(), 0);

    // The selection is bounded and stays in ascending id order.
    World big;
    std::vector<EntityKey> many;
    for (uint32_t id = 70; id >= 1; --id) {
        big.Add(id, "Unit", EntityKind::Unit, ControlOwner::CommandAI, kOrders);
        many.push_back(big.Key(id));
    }
    std::unique_ptr<CommandPanel> big_holder = NewPanel();
    CommandPanel& big_panel = *big_holder;
    big.Apply(big_panel);
    s = big_panel.Select(SelectOp::Replace, &many[0], static_cast<int>(many.size()));
    CHECK_INT(big_panel.SelectionCount(), kMaxSelection);
    CHECK_INT(s.rejected_capacity, 70 - kMaxSelection);
    bool ascending = true;
    uint32_t previous = 0;
    for (int i = 0; i < big_panel.SelectionCount(); ++i) {
        EntityKey each;
        big_panel.SelectionKey(i, &each);
        if (each.id <= previous) ascending = false;
        previous = each.id;
    }
    CHECK(ascending);
}

void TestSavedSets() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    w.Apply(panel);

    SelectIds(panel, w, SelectOp::Replace, {10, 20});
    CHECK(panel.SaveSelectionSet(0));
    CHECK(!panel.SaveSelectionSet(-1));
    CHECK(!panel.SaveSelectionSet(kMaxSavedSets));
    CHECK_INT(panel.SelectionSetCount(0), 2);

    SelectIds(panel, w, SelectOp::Replace, {50});
    SelectResult s = panel.RecallSelectionSet(0, SelectOp::Replace);
    CHECK(s.changed);
    CHECK_INT(panel.SelectionCount(), 2);
    CHECK(panel.IsSelected(w.Key(10)));
    CHECK(panel.IsSelected(w.Key(20)));

    // A recalled set is still described by the squads the simulation reports;
    // it is never presented as a squad of its own.
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "1 squad, 1 without squad"));
    CHECK(!AnyText(panel.Layout(), "Set"));
    CHECK(!AnyText(panel.Layout(), "Group"));

    SelectIds(panel, w, SelectOp::Replace, {50});
    s = panel.RecallSelectionSet(0, SelectOp::Add);
    CHECK_INT(s.added, 2);
    CHECK_INT(panel.SelectionCount(), 3);

    // Saved keys obey the same generation rule as the live selection.
    w.Find(20)->key.generation = 5;
    ++w.snapshot_generation;
    SnapshotResult r = w.Apply(panel);
    CHECK_INT(r.pruned_saved, 1);
    CHECK_INT(r.pruned_selection, 1);
    CHECK_INT(panel.SelectionSetCount(0), 1);

    const int before = panel.SelectionCount();
    s = panel.RecallSelectionSet(3, SelectOp::Replace);
    CHECK(!s.changed);
    CHECK_INT(panel.SelectionCount(), before);

    panel.ClearSelectionSet(0);
    CHECK_INT(panel.SelectionSetCount(0), 0);

    // The host's squad facts are untouched by any of this.
    CHECK_INT(w.Find(10)->squad.member_count, 3);
    CHECK_INT(w.Find(10)->squad.squad_id, 3);
    CHECK(!w.Find(20)->squad.present);
}

// ---------------------------------------------------------------------------
// Controls: what the panel shows for each kind of selection
// ---------------------------------------------------------------------------

void TestSingleUnit() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    w.Apply(panel);

    // Nothing selected: no panel at all, the world keeps every pixel.
    panel.UpdateLayout(in);
    CHECK(!panel.Layout().visible);
    CHECK(panel.HitTest(20.0f, 460.0f) == HitKind::None);

    SelectIds(panel, w, SelectOp::Replace, {10});
    panel.UpdateLayout(in);
    const PanelLayout& L = panel.Layout();
    CHECK(L.visible);
    CHECK(!L.clipped);
    CHECK(panel.LayoutIsCurrent());
    CHECK_STR(L.lines[0].text, "Gunship 1");
    CHECK(L.lines[0].role == LineRole::Title);
    CHECK(HasLine(L, "Friendly unit"));
    CHECK(HasLine(L, "AI - your orders"));
    CHECK(HasLine(L, "Squad: Alpha (1 of 3)"));
    CHECK(L.identity == IdentityStyle::Command);
    CHECK(!L.has_detail_row);

    // Exactly the supplied capabilities, in a fixed order, and nothing else.
    const Action expected[6] = {Action::Move,     Action::Advance,     Action::Hold,
                                Action::FallBack, Action::TakeControl, Action::Focus};
    CHECK_INT(L.control_count, 6);
    for (int i = 0; i < 6 && i < L.control_count; ++i) {
        CHECK(L.controls[i].action == expected[i]);
        CHECK(L.controls[i].enabled);
        CHECK_INT(L.controls[i].eligible, 1);
        CHECK_INT(L.controls[i].blocked, 0);
    }
    CHECK_INT(FindControl(L, Action::ReturnToAI), -1);
    CHECK_INT(FindControl(L, Action::RequestVehicle), -1);
    CHECK_INT(FindControl(L, Action::CancelRequest), -1);
    CHECK_STR(L.controls[0].label, "Move");
    CHECK_STR(L.controls[3].label, "Fall Back");
    CHECK_STR(L.controls[4].label, "Take Control");
    CHECK(LayoutFits(L, in, font));

    // A hostile unit can be inspected; only what the host supplied is offered.
    SelectIds(panel, w, SelectOp::Replace, {60});
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Hostile unit"));
    CHECK(HasLine(panel.Layout(), "AI - not yours"));
    CHECK_INT(panel.Layout().control_count, 1);
    CHECK(panel.Layout().controls[0].action == Action::Focus);
    CHECK(panel.Layout().identity == IdentityStyle::NoAuthority);

    panel.ClearSelection();
    panel.UpdateLayout(in);
    CHECK(!panel.Layout().visible);
}

void TestMixedCounts() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(1024.0f, 768.0f, 1.0f, &font);
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10, 11, 20, 30, 40});
    panel.UpdateLayout(in);
    const PanelLayout& L = panel.Layout();

    ActionSummary move = panel.Summarize(Action::Move);
    CHECK(move.shown);
    CHECK_INT(move.selected, 5);
    CHECK_INT(move.eligible, 2);
    CHECK_INT(move.blocked_human, 1);
    CHECK_INT(move.blocked_no_authority, 1);
    CHECK_INT(move.blocked_unavailable, 1);
    CHECK_INT(move.blocked_unsupported, 0);

    ActionSummary focus = panel.Summarize(Action::Focus);
    CHECK_INT(focus.eligible, 4);
    CHECK_INT(focus.blocked_unavailable, 1);

    ActionSummary back = panel.Summarize(Action::ReturnToAI);
    CHECK_INT(back.eligible, 1);
    CHECK_INT(back.blocked_unsupported, 3);
    CHECK_INT(back.blocked_unavailable, 1);

    ActionSummary take = panel.Summarize(Action::TakeControl);
    CHECK_INT(take.eligible, 0);
    CHECK_INT(take.blocked_human, 1);
    CHECK_INT(take.blocked_unsupported, 3);
    CHECK_INT(take.blocked_unavailable, 1);

    ActionSummary request = panel.Summarize(Action::RequestVehicle);
    CHECK(!request.shown);
    CHECK_INT(request.eligible, 0);

    // The partial result is visible on the control itself and in the detail.
    CHECK_STR(L.lines[0].text, "5 units");
    CHECK(HasLine(L, "2 yours, 1 piloted, 1 other, 1 n/a"));
    CHECK(HasLine(L, "1 squad, 3 without squad"));
    CHECK(L.identity == IdentityStyle::MixedWithHuman);
    CHECK_INT(L.control_count, 7);
    int mi = FindControl(L, Action::Move);
    CHECK(mi >= 0);
    if (mi >= 0) {
        CHECK_STR(L.controls[mi].label, "Move 2/5");
        CHECK_STR(L.controls[mi].detail, "Skips 1 piloted, 1 other, 1 n/a");
        CHECK(L.controls[mi].enabled);
        CHECK_INT(L.controls[mi].blocked, 3);
    }
    int fi = FindControl(L, Action::Focus);
    if (fi >= 0) CHECK_STR(L.controls[fi].label, "Focus 4/5");
    int ri = FindControl(L, Action::ReturnToAI);
    if (ri >= 0) {
        CHECK_STR(L.controls[ri].label, "Return to AI 1/5");
        CHECK_STR(L.controls[ri].detail, "Skips 1 n/a, 3 unable");
    }
    CHECK(L.has_detail_row);
    CHECK_STR(L.detail_legend, "Counts: ready / selected");
    CHECK(LayoutFits(L, in, font));

    // The intent names exactly the entities that can comply.
    ActionIntent intent = BlankIntent();
    PointerResult pr = Click(panel, Action::Move, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    CHECK(intent.action == Action::Move);
    CHECK_INT(intent.key_count, 2);
    CHECK_INT(intent.blocked_count, 3);
    CHECK_INT(intent.selected_count, 5);
    CHECK(intent.keys[0] == w.Key(10));
    CHECK(intent.keys[1] == w.Key(11));

    // The same set selected in another order gives identical facts.
    std::unique_ptr<CommandPanel> other_holder = NewPanel();
    CommandPanel& other = *other_holder;
    w.Apply(other);
    SelectIds(other, w, SelectOp::Replace, {40});
    SelectIds(other, w, SelectOp::Add, {30, 20});
    SelectIds(other, w, SelectOp::Toggle, {11, 10});
    ActionSummary move2 = other.Summarize(Action::Move);
    CHECK_INT(move2.eligible, move.eligible);
    CHECK(move2.eligible_hash == move.eligible_hash);
    ActionIntent intent2 = BlankIntent();
    CHECK(other.BuildIntent(Action::Move, &intent2));
    CHECK_INT(intent2.key_count, 2);
    CHECK(intent2.keys[0] == intent.keys[0]);
    CHECK(intent2.keys[1] == intent.keys[1]);

    // Units and a structure together.
    SelectIds(panel, w, SelectOp::Add, {50});
    panel.UpdateLayout(in);
    const PanelLayout& M = panel.Layout();
    CHECK_STR(M.lines[0].text, "5 units + 1 structure");
    mi = FindControl(M, Action::Move);
    if (mi >= 0) CHECK_STR(M.controls[mi].label, "Move 2/6");
    int qi = FindControl(M, Action::RequestVehicle);
    CHECK(qi >= 0);
    if (qi >= 0) CHECK_STR(M.controls[qi].label, "Request Vehicle 1/6");
    CHECK_INT(panel.Summarize(Action::Move).blocked_unsupported, 1);
    CHECK(LayoutFits(M, in, font));
}

void TestStructureSelection() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {50});
    panel.UpdateLayout(in);
    const PanelLayout& L = panel.Layout();

    CHECK(L.visible);
    CHECK_STR(L.lines[0].text, "Factory");
    CHECK(HasLine(L, "Friendly structure"));
    CHECK(HasLine(L, "No controller"));
    CHECK(L.identity == IdentityStyle::Structure);
    for (int i = 0; i < L.line_count; ++i) {
        CHECK(L.lines[i].role != LineRole::Squad);
        CHECK(L.lines[i].role != LineRole::Queue);
    }
    CHECK_INT(L.control_count, 2);
    CHECK(L.controls[0].action == Action::Focus);
    CHECK(L.controls[1].action == Action::RequestVehicle);
    CHECK(L.controls[0].enabled && L.controls[1].enabled);
    CHECK_INT(FindControl(L, Action::Move), -1);
    CHECK_INT(FindControl(L, Action::CancelRequest), -1);
    CHECK(LayoutFits(L, in, font));

    ActionIntent intent = BlankIntent();
    PointerResult pr = Click(panel, Action::RequestVehicle, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    CHECK(intent.action == Action::RequestVehicle);
    CHECK_INT(intent.key_count, 1);
    CHECK(intent.keys[0] == w.Key(50));

    DrawLog log;
    PaintInto(panel, &log);
    CHECK(LogHasFill(log, MakePalette().structure));
    CHECK(!LogHasFill(log, MakePalette().accent));
}

void TestHumanOwned() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    const Palette pal = MakePalette();
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {20});
    panel.UpdateLayout(in);
    const PanelLayout& L = panel.Layout();

    CHECK_STR(L.lines[0].text, "Player Craft");
    CHECK(HasLine(L, "You are piloting"));
    CHECK(L.identity == IdentityStyle::HumanLocal);

    // The snapshot claims order capabilities, but a piloted unit is never
    // offered an AI order: the controls are shown and disabled.
    CHECK_INT(L.control_count, 6);
    const Action orders[4] = {Action::Move, Action::Advance, Action::Hold, Action::FallBack};
    for (int i = 0; i < 4; ++i) {
        int index = FindControl(L, orders[i]);
        CHECK(index >= 0);
        if (index >= 0) CHECK(!L.controls[index].enabled);
        ActionSummary s = panel.Summarize(orders[i]);
        CHECK_INT(s.eligible, 0);
        CHECK_INT(s.blocked_human, 1);
        ActionIntent refused = BlankIntent();
        CHECK(!panel.BuildIntent(orders[i], &refused));
        CHECK_INT(refused.key_count, 0xFFFF);
    }
    int back = FindControl(L, Action::ReturnToAI);
    CHECK(back >= 0);
    if (back >= 0) CHECK(L.controls[back].enabled);

    ActionIntent intent = BlankIntent();
    PointerResult pr = Click(panel, Action::Move, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ConsumedDisabled);
    CHECK_INT(intent.key_count, 0xFFFF);

    CHECK(panel.BuildIntent(Action::ReturnToAI, &intent));
    CHECK_INT(intent.key_count, 1);
    CHECK(intent.keys[0] == w.Key(20));

    // Drawn differently from a commandable unit.
    DrawLog log;
    PaintInto(panel, &log);
    CHECK(LogHasFill(log, pal.human));
    CHECK(LogHasLine(log, pal.human));
    CHECK(!LogHasFill(log, pal.accent));
    CHECK(LogHasText(log, "You are piloting"));

    SelectIds(panel, w, SelectOp::Replace, {10});
    panel.UpdateLayout(in);
    PaintInto(panel, &log);
    CHECK(LogHasFill(log, pal.accent));
    CHECK(!LogHasFill(log, pal.human));
    CHECK(!LogHasLine(log, pal.human));

    // Another human's unit: no orders, no hand-back, no takeover.
    w.Find(20)->control = ControlOwner::OtherHuman;
    w.Find(20)->capabilities |= ActionBit(Action::TakeControl);
    ++w.snapshot_generation;
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {20});
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Human pilot"));
    CHECK(panel.Layout().identity == IdentityStyle::HumanOther);
    CHECK_INT(panel.Summarize(Action::Move).blocked_human, 1);
    CHECK_INT(panel.Summarize(Action::TakeControl).blocked_human, 1);
    CHECK_INT(panel.Summarize(Action::ReturnToAI).blocked_no_authority, 1);
    CHECK_INT(panel.Summarize(Action::ReturnToAI).eligible, 0);
    PaintInto(panel, &log);
    CHECK(LogHasLine(log, pal.human));

    // In a mixed selection the piloted unit is counted, shown and left out.
    SelectIds(panel, w, SelectOp::Replace, {10, 20});
    panel.UpdateLayout(in);
    CHECK(panel.Layout().identity == IdentityStyle::MixedWithHuman);
    int mi = FindControl(panel.Layout(), Action::Move);
    CHECK(mi >= 0);
    if (mi >= 0) CHECK_STR(panel.Layout().controls[mi].label, "Move 1/2");
    intent = BlankIntent();
    pr = Click(panel, Action::Move, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    CHECK_INT(intent.key_count, 1);
    CHECK(intent.keys[0] == w.Key(10));
    CHECK_INT(intent.blocked_count, 1);
}

void TestUnavailableAndScripted() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    w.Apply(panel);

    SelectIds(panel, w, SelectOp::Replace, {40});
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Destroyed"));
    CHECK(panel.Layout().identity == IdentityStyle::Unavailable);
    CHECK_INT(panel.Layout().control_count, 4);
    for (int i = 0; i < panel.Layout().control_count; ++i) {
        CHECK(!panel.Layout().controls[i].enabled);
    }
    CHECK_INT(panel.Summarize(Action::Move).blocked_unavailable, 1);
    ActionIntent intent = BlankIntent();
    CHECK(!panel.BuildIntent(Action::Move, &intent));
    DrawLog log;
    PaintInto(panel, &log);
    CHECK(LogHasFill(log, MakePalette().warning));

    w.Find(40)->availability = Availability::Unavailable;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK_INT(panel.SelectionCount(), 1);
    CHECK(HasLine(panel.Layout(), "Unavailable"));
    CHECK(!HasLine(panel.Layout(), "Destroyed"));

    // Friendly and scripted: on our side, yet not ours to command.
    SelectIds(panel, w, SelectOp::Replace, {30});
    panel.UpdateLayout(in);
    const PanelLayout& L = panel.Layout();
    CHECK(HasLine(L, "Friendly unit"));
    CHECK(HasLine(L, "Scripted - no orders"));
    CHECK(L.identity == IdentityStyle::NoAuthority);
    int mi = FindControl(L, Action::Move);
    CHECK(mi >= 0);
    if (mi >= 0) CHECK(!L.controls[mi].enabled);
    CHECK_INT(panel.Summarize(Action::Move).blocked_no_authority, 1);
    int fi = FindControl(L, Action::Focus);
    CHECK(fi >= 0);
    if (fi >= 0) CHECK(L.controls[fi].enabled);
    CHECK(!panel.BuildIntent(Action::Hold, &intent));
}

// ---------------------------------------------------------------------------
// Controls: intents and pointer gestures
// ---------------------------------------------------------------------------

void TestStaleIntent() {
    World w = MakeWorld();
    w.targeted = ActionBit(Action::Move) | ActionBit(Action::Advance) |
                 ActionBit(Action::FallBack);
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10, 11});
    panel.UpdateLayout(in);

    ActionIntent intent = BlankIntent();
    CHECK(panel.BuildIntent(Action::Move, &intent));
    CHECK(intent.session == w.session);
    CHECK_INT(intent.snapshot_generation, w.snapshot_generation);
    CHECK_INT(intent.context_generation, w.context_generation);
    CHECK_INT(intent.layout_generation, panel.Layout().generation);
    CHECK_INT(intent.selection_revision, panel.SelectionRevision());
    CHECK(intent.needs_world_target);
    CHECK(panel.CheckIntent(intent) == IntentStatus::Current);

    ActionIntent hold = BlankIntent();
    CHECK(panel.BuildIntent(Action::Hold, &hold));
    CHECK(!hold.needs_world_target);

    // A newer snapshot with the same facts: flagged, so the host must look again.
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(panel.CheckIntent(intent) == IntentStatus::StaleSnapshot);

    // Context change.
    CHECK(panel.BuildIntent(Action::Move, &intent));
    CHECK(panel.CheckIntent(intent) == IntentStatus::Current);
    ++w.context_generation;
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(panel.CheckIntent(intent) == IntentStatus::StaleContext);

    // One entity replaced under the same id.
    CHECK(panel.BuildIntent(Action::Move, &intent));
    w.Find(11)->key.generation = 9;
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(panel.CheckIntent(intent) == IntentStatus::StaleEntity);

    // Capability withdrawn.
    CHECK(panel.BuildIntent(Action::Move, &intent));
    CHECK_INT(intent.key_count, 1);
    const uint32_t caps = w.Find(10)->capabilities;
    w.Find(10)->capabilities = caps & ~ActionBit(Action::Move);
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(panel.CheckIntent(intent) == IntentStatus::NotEligible);
    w.Find(10)->capabilities = caps;
    ++w.snapshot_generation;
    w.Apply(panel);

    // A human took the unit over after the intent was made.
    CHECK(panel.BuildIntent(Action::Move, &intent));
    w.Find(10)->control = ControlOwner::LocalHuman;
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(panel.CheckIntent(intent) == IntentStatus::NotEligible);
    w.Find(10)->control = ControlOwner::CommandAI;
    ++w.snapshot_generation;
    w.Apply(panel);

    // The unit died after the intent was made.
    CHECK(panel.BuildIntent(Action::Move, &intent));
    w.Find(10)->availability = Availability::Dead;
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(panel.CheckIntent(intent) == IntentStatus::NotEligible);
    w.Find(10)->availability = Availability::Available;
    ++w.snapshot_generation;
    w.Apply(panel);

    // Another session.
    CHECK(panel.BuildIntent(Action::Move, &intent));
    w.SetSession(21);
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(panel.CheckIntent(intent) == IntentStatus::StaleSession);

    ActionIntent bad;
    std::memset(&bad, 0, sizeof(bad));
    CHECK(panel.CheckIntent(bad) == IntentStatus::Malformed);
    bad.key_count = 1;
    bad.action = Action::Count;
    CHECK(panel.CheckIntent(bad) == IntentStatus::Malformed);
}

void TestPressOutsideCrossRelease() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    const Palette pal = MakePalette();
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10});
    panel.UpdateLayout(in);
    int mi = FindControl(panel.Layout(), Action::Move);
    CHECK(mi >= 0);
    if (mi < 0) return;
    const Rect r = panel.Layout().controls[mi].rect;
    const float cx = CenterX(r);
    const float cy = CenterY(r);

    CHECK(panel.HitTest(kFarX, kFarY) == HitKind::None);
    CHECK(panel.HitTest(cx, cy) == HitKind::Control);

    PointerResult pr = panel.PointerDown(kFarX, kFarY);
    CHECK(pr.owner == PointerOwner::World);
    CHECK(pr.outcome == PointerOutcome::World);

    // Dragging across the panel: still the world's gesture, no hover, no press.
    pr = panel.PointerMove(cx, cy);
    CHECK(pr.owner == PointerOwner::World);
    CHECK(pr.outcome == PointerOutcome::World);
    DrawLog log;
    PaintInto(panel, &log);
    CHECK(!LogHasFill(log, pal.button_hover));
    CHECK(!LogHasFill(log, pal.button_pressed));

    ActionIntent intent = BlankIntent();
    pr = panel.PointerUp(cx, cy, &intent);
    CHECK(pr.owner == PointerOwner::World);
    CHECK(pr.outcome == PointerOutcome::World);
    CHECK_INT(intent.key_count, 0xFFFF);

    // A normal click afterwards works.
    pr = Click(panel, Action::Move, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);

    // A release the panel never saw pressed is not the panel's either.
    pr = panel.PointerMove(cx, cy);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::PanelIdle);
    intent = BlankIntent();
    pr = panel.PointerUp(cx, cy, &intent);
    CHECK(pr.owner == PointerOwner::World);
    CHECK_INT(intent.key_count, 0xFFFF);
}

void TestPressInsideReleaseOutside() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    const Palette pal = MakePalette();
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10});
    panel.UpdateLayout(in);
    int mi = FindControl(panel.Layout(), Action::Move);
    int ai = FindControl(panel.Layout(), Action::Advance);
    CHECK(mi >= 0 && ai >= 0);
    if (mi < 0 || ai < 0) return;
    const Rect move = panel.Layout().controls[mi].rect;
    const Rect advance = panel.Layout().controls[ai].rect;

    PointerResult pr = panel.PointerDown(CenterX(move), CenterY(move));
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::Armed);
    DrawLog log;
    PaintInto(panel, &log);
    CHECK(LogHasFill(log, pal.button_pressed));

    pr = panel.PointerMove(kFarX, kFarY);
    CHECK(pr.owner == PointerOwner::Panel);
    PaintInto(panel, &log);
    CHECK(!LogHasFill(log, pal.button_pressed));

    ActionIntent intent = BlankIntent();
    pr = panel.PointerUp(kFarX, kFarY, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ReleasedOffControl);
    CHECK_INT(intent.key_count, 0xFFFF);

    // Sliding onto a neighbouring control never fires that neighbour.
    panel.PointerDown(CenterX(move), CenterY(move));
    pr = panel.PointerMove(CenterX(advance), CenterY(advance));
    CHECK(pr.owner == PointerOwner::Panel);
    pr = panel.PointerUp(CenterX(advance), CenterY(advance), &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ReleasedOffControl);
    CHECK_INT(intent.key_count, 0xFFFF);

    // Sliding off and back onto the pressed control completes the click.
    panel.PointerDown(CenterX(move), CenterY(move));
    panel.PointerMove(kFarX, kFarY);
    panel.PointerMove(CenterX(move), CenterY(move));
    pr = panel.PointerUp(CenterX(move), CenterY(move), &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    CHECK(intent.action == Action::Move);
    CHECK_INT(intent.key_count, 1);
    CHECK(intent.keys[0] == w.Key(10));
}

void TestDisabledAndPaddingCapture() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {20});
    panel.UpdateLayout(in);
    int mi = FindControl(panel.Layout(), Action::Move);
    int bi = FindControl(panel.Layout(), Action::ReturnToAI);
    CHECK(mi >= 0 && bi >= 0);
    if (mi < 0 || bi < 0) return;
    const Rect move = panel.Layout().controls[mi].rect;       // disabled
    const Rect back = panel.Layout().controls[bi].rect;       // enabled
    const float pad_x = panel.Layout().panel.x + 1.0f;
    const float pad_y = panel.Layout().panel.y + 1.0f;
    CHECK(panel.HitTest(pad_x, pad_y) == HitKind::Padding);
    CHECK(panel.HitTest(CenterX(move), CenterY(move)) == HitKind::DisabledControl);
    CHECK(panel.HitTest(CenterX(back), CenterY(back)) == HitKind::Control);

    // Padding: consumed from press to release, wherever the release lands.
    ActionIntent intent = BlankIntent();
    PointerResult pr = panel.PointerDown(pad_x, pad_y);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ConsumedPadding);
    pr = panel.PointerMove(kFarX, kFarY);
    CHECK(pr.owner == PointerOwner::Panel);
    pr = panel.PointerUp(kFarX, kFarY, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ConsumedPadding);
    CHECK_INT(intent.key_count, 0xFFFF);

    panel.PointerDown(pad_x, pad_y);
    pr = panel.PointerUp(CenterX(back), CenterY(back), &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ConsumedPadding);
    CHECK_INT(intent.key_count, 0xFFFF);

    // Disabled control: the same.
    pr = panel.PointerDown(CenterX(move), CenterY(move));
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ConsumedDisabled);
    pr = panel.PointerMove(kFarX, kFarY);
    CHECK(pr.owner == PointerOwner::Panel);
    pr = panel.PointerUp(kFarX, kFarY, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ConsumedDisabled);
    CHECK_INT(intent.key_count, 0xFFFF);

    panel.PointerDown(CenterX(move), CenterY(move));
    pr = panel.PointerUp(CenterX(back), CenterY(back), &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK_INT(intent.key_count, 0xFFFF);

    // A layout older than the model captures the press but arms nothing.
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(!panel.LayoutIsCurrent());
    pr = panel.PointerDown(CenterX(back), CenterY(back));
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ConsumedStale);
    pr = panel.PointerUp(CenterX(back), CenterY(back), &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::ConsumedStale);
    CHECK_INT(intent.key_count, 0xFFFF);

    panel.UpdateLayout(in);
    pr = Click(panel, Action::ReturnToAI, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);

    // Focus loss mid-press: nothing fires, and the release is still consumed.
    intent = BlankIntent();
    pr = panel.PointerDown(CenterX(back), CenterY(back));
    CHECK(pr.outcome == PointerOutcome::Armed);
    panel.CancelPointer();
    pr = panel.PointerUp(CenterX(back), CenterY(back), &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);
}

// Press the centre of the Move control in the current layout.
bool PressMove(CommandPanel& panel, float* x, float* y) {
    int mi = FindControl(panel.Layout(), Action::Move);
    if (mi < 0) return false;
    const Rect r = panel.Layout().controls[mi].rect;
    *x = CenterX(r);
    *y = CenterY(r);
    PointerResult pr = panel.PointerDown(*x, *y);
    return pr.owner == PointerOwner::Panel && pr.outcome == PointerOutcome::Armed;
}

// Release on the centre of `action` in the current layout.
PointerResult ReleaseOn(CommandPanel& panel, Action action, ActionIntent* intent) {
    int index = FindControl(panel.Layout(), action);
    if (index < 0) return panel.PointerUp(kFarX, kFarY, intent);
    const Rect r = panel.Layout().controls[index].rect;
    return panel.PointerUp(CenterX(r), CenterY(r), intent);
}

void TestChangeWhilePressed() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in640 = MakeInput(640.0f, 480.0f, 1.0f, &font);
    LayoutInput in800 = MakeInput(800.0f, 600.0f, 1.0f, &font);
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10, 11});
    panel.UpdateLayout(in640);
    float x = 0.0f;
    float y = 0.0f;
    ActionIntent intent = BlankIntent();
    PointerResult pr;

    // Resolution change, release where the same control now is.
    CHECK(PressMove(panel, &x, &y));
    CHECK(panel.UpdateLayout(in800));
    pr = panel.PointerMove(x, y);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    pr = ReleaseOn(panel, Action::Move, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);

    // Resolution change, release where a different control now is.
    panel.UpdateLayout(in640);
    CHECK(PressMove(panel, &x, &y));
    CHECK(panel.UpdateLayout(in800));
    pr = ReleaseOn(panel, Action::Advance, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);

    // A fresh press after the change works normally.
    pr = Click(panel, Action::Move, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    intent = BlankIntent();

    // A host reservation appears under the panel: the panel moves, and the
    // release at the old spot is consumed rather than becoming a world click.
    panel.UpdateLayout(in640);
    CHECK(PressMove(panel, &x, &y));
    LayoutInput reserved = in640;
    AddReservation(&reserved, 0.0f, 300.0f, 640.0f, 180.0f);
    CHECK(panel.UpdateLayout(reserved));
    CHECK(LayoutFits(panel.Layout(), reserved, font));
    CHECK(panel.HitTest(x, y) == HitKind::None);
    pr = panel.PointerUp(x, y, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);

    // Scale change.
    panel.UpdateLayout(in640);
    CHECK(PressMove(panel, &x, &y));
    LayoutInput scaled = MakeInput(640.0f, 480.0f, 1.25f, &font);
    CHECK(panel.UpdateLayout(scaled));
    pr = ReleaseOn(panel, Action::Move, &intent);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);

    // Input/command context change with identical geometry.
    panel.UpdateLayout(in640);
    CHECK(PressMove(panel, &x, &y));
    ++w.context_generation;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in640);
    pr = panel.PointerUp(x, y, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);

    // A selected entity is replaced under the same id.
    CHECK(PressMove(panel, &x, &y));
    w.Find(11)->key.generation = 4;
    ++w.snapshot_generation;
    SnapshotResult sr = w.Apply(panel);
    CHECK_INT(sr.pruned_selection, 1);
    panel.UpdateLayout(in640);
    pr = ReleaseOn(panel, Action::Move, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);

    // Eligibility changes while the selection stays the same.
    SelectIds(panel, w, SelectOp::Replace, {10, 11});
    panel.UpdateLayout(in640);
    CHECK(PressMove(panel, &x, &y));
    const uint32_t caps = w.Find(11)->capabilities;
    w.Find(11)->capabilities = caps & ~ActionBit(Action::Move);
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(panel.UpdateLayout(in640));
    pr = ReleaseOn(panel, Action::Move, &intent);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);
    w.Find(11)->capabilities = caps;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in640);

    // The selection itself changes mid-press.
    CHECK(PressMove(panel, &x, &y));
    SelectIds(panel, w, SelectOp::Add, {13});
    panel.UpdateLayout(in640);
    pr = ReleaseOn(panel, Action::Move, &intent);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);

    // A snapshot arrived and the layout was not refreshed before the release.
    CHECK(PressMove(panel, &x, &y));
    ++w.snapshot_generation;
    w.Apply(panel);
    pr = panel.PointerUp(x, y, &intent);
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::CancelledByChange);
    CHECK_INT(intent.key_count, 0xFFFF);

    // An ordinary new snapshot with the same facts does not disturb a press,
    // and the intent carries the generation current at release.
    panel.UpdateLayout(in640);
    CHECK(PressMove(panel, &x, &y));
    ++w.snapshot_generation;
    w.Apply(panel);
    CHECK(!panel.UpdateLayout(in640));
    pr = panel.PointerUp(x, y, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    CHECK_INT(intent.snapshot_generation, w.snapshot_generation);
    CHECK_INT(intent.key_count, 3);
    CHECK(panel.CheckIntent(intent) == IntentStatus::Current);

    // A world gesture stays a world gesture through any change.
    intent = BlankIntent();
    pr = panel.PointerDown(kFarX, kFarY);
    CHECK(pr.owner == PointerOwner::World);
    panel.UpdateLayout(in800);
    pr = ReleaseOn(panel, Action::Move, &intent);
    CHECK(pr.owner == PointerOwner::World);
    CHECK_INT(intent.key_count, 0xFFFF);
}

// ---------------------------------------------------------------------------
// Controls: fitting, truthfulness, painting
// ---------------------------------------------------------------------------

// A selection that makes all nine actions appear, most of them partial.
void SelectEverything(CommandPanel& panel, World& w) {
    w.Find(50)->capabilities |= ActionBit(Action::CancelRequest);
    ++w.snapshot_generation;
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10, 11, 20, 30, 40, 50});
}

bool AnyShortened(const PanelLayout& L) {
    for (int i = 0; i < L.line_count; ++i) {
        if (std::strstr(L.lines[i].text, "..") != nullptr) return true;
    }
    for (int i = 0; i < L.control_count; ++i) {
        if (std::strstr(L.controls[i].label, "..") != nullptr) return true;
        if (std::strstr(L.controls[i].detail, "..") != nullptr) return true;
    }
    return std::strstr(L.detail_legend, "..") != nullptr;
}

void TestBaseResolutionFit() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    w.Apply(panel);
    SelectEverything(panel, w);
    panel.UpdateLayout(in);
    {
        const PanelLayout& L = panel.Layout();
        CHECK(L.visible);
        CHECK(!L.clipped);
        CHECK_INT(L.hidden_controls, 0);
        CHECK_INT(L.control_count, kActionCount);
        CHECK(LayoutFits(L, in, font));
        CHECK(!AnyShortened(L));
        // Compact: a corner of the view, not a full-width command bar.
        CHECK(L.panel.w <= 352.0f);
        CHECK(L.panel.h <= in.safe.h * 0.5f);
        int qi = FindControl(L, Action::RequestVehicle);
        int ci = FindControl(L, Action::CancelRequest);
        CHECK(qi >= 0 && ci >= 0);
        if (qi >= 0) CHECK_STR(L.controls[qi].label, "Request Vehicle 1/6");
        if (ci >= 0) CHECK_STR(L.controls[ci].label, "Cancel Request 1/6");
        int mi = FindControl(L, Action::Move);
        if (mi >= 0) {
            CHECK_STR(L.controls[mi].detail, "Skips 1 piloted, 1 other, 1 n/a, 1 unable");
        }
    }

    // A wider font at the same size still fits without overflow.
    Font wide = {11.0f, 18.0f};
    LayoutInput in_wide = MakeInput(640.0f, 480.0f, 1.0f, &wide);
    panel.UpdateLayout(in_wide);
    CHECK(panel.Layout().visible);
    CHECK_INT(panel.Layout().hidden_controls, 0);
    CHECK(LayoutFits(panel.Layout(), in_wide, wide));

    // Larger sizes and scales.
    const float sizes[4][3] = {{800.0f, 600.0f, 1.0f},
                               {1280.0f, 720.0f, 1.5f},
                               {1920.0f, 1080.0f, 2.0f},
                               {3840.0f, 2160.0f, 4.0f}};
    for (int i = 0; i < 4; ++i) {
        LayoutInput each = MakeInput(sizes[i][0], sizes[i][1], sizes[i][2], &font);
        panel.UpdateLayout(each);
        CHECK(panel.Layout().visible);
        CHECK(!panel.Layout().clipped);
        CHECK_INT(panel.Layout().control_count, kActionCount);
        CHECK(LayoutFits(panel.Layout(), each, font));
        CHECK(!AnyShortened(panel.Layout()));
        CHECK(panel.Layout().panel.w <= each.safe.w * 0.5f + 1.0f);
    }

    // Host reservations (a portrait corner and a dialogue strip) are avoided.
    LayoutInput reserved = MakeInput(640.0f, 480.0f, 1.0f, &font);
    AddReservation(&reserved, 0.0f, 330.0f, 220.0f, 150.0f);
    AddReservation(&reserved, 0.0f, 0.0f, 640.0f, 90.0f);
    panel.UpdateLayout(reserved);
    CHECK(panel.Layout().visible);
    CHECK(!panel.Layout().clipped);
    CHECK(LayoutFits(panel.Layout(), reserved, font));

    // A reservation over the whole view: the panel yields entirely.
    LayoutInput covered = MakeInput(640.0f, 480.0f, 1.0f, &font);
    AddReservation(&covered, 0.0f, 0.0f, 640.0f, 480.0f);
    panel.UpdateLayout(covered);
    CHECK(!panel.Layout().visible);
    CHECK(panel.Layout().no_room);
    CHECK(panel.HitTest(20.0f, 460.0f) == HitKind::None);
    PointerResult pr = panel.PointerDown(20.0f, 460.0f);
    CHECK(pr.owner == PointerOwner::World);
    panel.PointerUp(20.0f, 460.0f, nullptr);

    // A short strip: optional parts go first, and the loss is reported.
    LayoutInput strip = MakeInput(640.0f, 480.0f, 1.0f, &font);
    strip.safe.x = 8.0f;
    strip.safe.y = 400.0f;
    strip.safe.w = 624.0f;
    strip.safe.h = 72.0f;
    panel.UpdateLayout(strip);
    CHECK(panel.Layout().visible);
    CHECK(panel.Layout().clipped);
    CHECK(panel.Layout().hidden_controls > 0);
    CHECK_INT(panel.Layout().control_count + panel.Layout().hidden_controls, kActionCount);
    CHECK(LayoutFits(panel.Layout(), strip, font));

    // No usable room at all.
    LayoutInput tiny = MakeInput(640.0f, 480.0f, 1.0f, &font);
    tiny.safe.w = 30.0f;
    tiny.safe.h = 10.0f;
    panel.UpdateLayout(tiny);
    CHECK(!panel.Layout().visible);
    CHECK(panel.Layout().no_room);

    // A long name in a very wide font is shortened, never drawn past the edge.
    SetText(w.Find(10)->name, "Experimental Long Range Gunship");
    ++w.snapshot_generation;
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10});
    Font huge = {14.0f, 18.0f};
    LayoutInput in_huge = MakeInput(640.0f, 480.0f, 1.0f, &huge);
    panel.UpdateLayout(in_huge);
    CHECK(panel.Layout().visible);
    CHECK(std::strstr(panel.Layout().lines[0].text, "..") != nullptr);
    CHECK(std::strncmp(panel.Layout().lines[0].text, "Experimental", 12) == 0);
    CHECK(LayoutFits(panel.Layout(), in_huge, huge));

    // A nonsense scale is treated as 1 rather than breaking the layout.
    LayoutInput zero = MakeInput(640.0f, 480.0f, 1.0f, &font);
    zero.scale = 0.0f;
    panel.UpdateLayout(zero);
    CHECK(panel.Layout().visible);
    CHECK(panel.Layout().scale == 1.0f);
    CHECK(LayoutFits(panel.Layout(), zero, font));
}

bool NoQueueText(const PanelLayout& L) {
    const char* words[] = {"Request:", "Request active", "%",        "waiting", "Yours",
                           "Not yours", "Queued",        "Building", "Ready",   "Cost"};
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (AnyText(L, words[i])) return false;
    }
    for (int i = 0; i < L.line_count; ++i) {
        if (L.lines[i].role == LineRole::Queue) return false;
    }
    return true;
}

void TestNoInventedProduction() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {50});
    panel.UpdateLayout(in);
    CHECK(NoQueueText(panel.Layout()));

    // Asking for a vehicle is only an intent.  Nothing on the panel claims
    // the request was accepted, queued or progressing.
    ActionIntent intent = BlankIntent();
    PointerResult pr = Click(panel, Action::RequestVehicle, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    panel.UpdateLayout(in);
    CHECK(NoQueueText(panel.Layout()));
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(NoQueueText(panel.Layout()));
    CHECK_INT(FindControl(panel.Layout(), Action::CancelRequest), -1);
    DrawLog log;
    PaintInto(panel, &log);
    bool drew_percent = false;
    for (size_t i = 0; i < log.items.size(); ++i) {
        if (log.items[i].kind == 'T' && log.items[i].text.find('%') != std::string::npos) {
            drew_percent = true;
        }
    }
    CHECK(!drew_percent);

    // Only explicit host fields appear, one at a time as they are supplied.
    EntitySnapshot* factory = w.Find(50);
    factory->queue.present = true;
    SetText(factory->queue.item, "Gunship");
    factory->capabilities |= ActionBit(Action::CancelRequest);
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Request: Gunship"));
    CHECK(!AnyText(panel.Layout(), "%"));
    CHECK(!AnyText(panel.Layout(), "Yours"));
    CHECK(!AnyText(panel.Layout(), "waiting"));
    CHECK(FindControl(panel.Layout(), Action::CancelRequest) >= 0);

    factory->queue.owner = QueueOwner::LocalCommander;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Yours"));
    CHECK(!AnyText(panel.Layout(), "%"));

    factory->queue.has_progress = true;
    factory->queue.progress_permille = 450;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Yours, 45%"));

    factory->queue.has_pending_count = true;
    factory->queue.pending_count = 2;
    factory->queue.owner = QueueOwner::Other;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Not yours, 45%, 2 waiting"));
    CHECK(LayoutFits(panel.Layout(), in, font));

    // A progress number without its flag is not shown.
    factory->queue.has_progress = false;
    factory->queue.progress_permille = 900;
    factory->queue.item[0] = '\0';
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Request active"));
    CHECK(!AnyText(panel.Layout(), "%"));

    // Without `present`, leftover fields mean nothing.
    factory->queue.present = false;
    factory->queue.has_progress = true;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(NoQueueText(panel.Layout()));
}

void TestPaintMatchesLayout() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    const Palette pal = MakePalette();
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10, 11, 20, 30, 40});
    panel.UpdateLayout(in);
    const PanelLayout& L = panel.Layout();
    CHECK(L.has_detail_row);

    DrawLog log;
    PaintInto(panel, &log);
    CHECK(!log.items.empty());
    if (log.items.empty()) return;
    CHECK(log.items[0].kind == 'F' && log.items[0].color == pal.background);

    // Everything painted lies inside the panel rectangle used for hit tests.
    const float px0 = L.panel.x;
    const float py0 = L.panel.y;
    const float px1 = L.panel.x + L.panel.w;
    const float py1 = L.panel.y + L.panel.h;
    int texts = 0;
    bool inside = true;
    for (size_t i = 0; i < log.items.size(); ++i) {
        const DrawItem& it = log.items[i];
        if (it.kind == 'F') {
            inside = inside && Inside(it.rect, L.panel);
        } else if (it.kind == 'L') {
            inside = inside && it.rect.x >= px0 && it.rect.x <= px1 && it.rect.w >= px0 &&
                     it.rect.w <= px1 && it.rect.y >= py0 && it.rect.y <= py1 &&
                     it.rect.h >= py0 && it.rect.h <= py1;
        } else {
            ++texts;
            inside = inside && it.rect.x >= px0 && it.rect.x < px1 && it.rect.y >= py0 &&
                     it.rect.y < py1;
        }
    }
    CHECK(inside);
    CHECK_INT(texts, L.line_count + L.control_count + 1);
    for (int i = 0; i < L.line_count; ++i) CHECK(LogHasText(log, L.lines[i].text));
    for (int i = 0; i < L.control_count; ++i) CHECK(LogHasText(log, L.controls[i].label));
    CHECK(LogHasText(log, L.detail_legend));

    // Hovering a partial control shows who it leaves out.
    int mi = FindControl(L, Action::Move);
    CHECK(mi >= 0);
    if (mi < 0) return;
    const Rect move = L.controls[mi].rect;
    PointerResult pr = panel.PointerMove(CenterX(move), CenterY(move));
    CHECK(pr.owner == PointerOwner::Panel);
    CHECK(pr.outcome == PointerOutcome::PanelIdle);
    PaintInto(panel, &log);
    CHECK(LogHasFill(log, pal.button_hover));
    CHECK(LogHasText(log, L.controls[mi].detail));
    CHECK(!LogHasText(log, L.detail_legend));
    CHECK(LogHasFill(log, pal.button_disabled) == true);

    panel.PointerLeft();
    PaintInto(panel, &log);
    CHECK(!LogHasFill(log, pal.button_hover));
    CHECK(LogHasText(log, L.detail_legend));

    // Missing callbacks are skipped, not called.
    DrawCallbacks none;
    std::memset(&none, 0, sizeof(none));
    panel.Paint(none, pal);

    // No selection: nothing is painted.
    panel.ClearSelection();
    panel.UpdateLayout(in);
    PaintInto(panel, &log);
    CHECK(log.items.empty());
}

void TestTargetsAndHostState() {
    World w = MakeWorld();
    w.targeted = ActionBit(Action::Move);
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    const Palette pal = MakePalette();
    w.Apply(panel);
    SelectIds(panel, w, SelectOp::Replace, {10});
    panel.UpdateLayout(in);
    int mi = FindControl(panel.Layout(), Action::Move);
    int hi = FindControl(panel.Layout(), Action::Hold);
    CHECK(mi >= 0 && hi >= 0);
    if (mi < 0 || hi < 0) return;
    CHECK(panel.Layout().controls[mi].needs_target);
    CHECK(!panel.Layout().controls[hi].needs_target);
    CHECK(!panel.Layout().controls[mi].host_armed);

    // Clicking Move hands the host an intent that still needs a target.  The
    // panel does not mark anything as armed on its own.
    ActionIntent intent = BlankIntent();
    PointerResult pr = Click(panel, Action::Move, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    CHECK(intent.needs_world_target);
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(!panel.Layout().controls[mi].host_armed);
    DrawLog log;
    PaintInto(panel, &log);
    CHECK(!LogHasLine(log, pal.accent));

    // Only the host's own report lights the control.
    w.has_armed = true;
    w.armed = Action::Move;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(panel.Layout().controls[mi].host_armed);
    CHECK(!panel.Layout().controls[hi].host_armed);
    PaintInto(panel, &log);
    CHECK(LogHasLine(log, pal.accent));

    pr = Click(panel, Action::Hold, &intent);
    CHECK(pr.outcome == PointerOutcome::IntentEmitted);
    CHECK(intent.action == Action::Hold);
    CHECK(!intent.needs_world_target);
}

void TestSquadVersusSelection() {
    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    Font font = {8.0f, 14.0f};
    LayoutInput in = MakeInput(640.0f, 480.0f, 1.0f, &font);
    w.Apply(panel);

    SelectIds(panel, w, SelectOp::Replace, {10, 11, 12});
    panel.UpdateLayout(in);
    CHECK_STR(panel.Layout().lines[0].text, "3 units");
    CHECK(HasLine(panel.Layout(), "3 yours"));
    CHECK(HasLine(panel.Layout(), "Squad: Alpha (all 3)"));
    CHECK(panel.Layout().identity == IdentityStyle::Command);

    // Part of a squad is shown as part of it.
    SelectIds(panel, w, SelectOp::Replace, {10, 11});
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Squad: Alpha (2 of 3)"));

    SelectIds(panel, w, SelectOp::Add, {13});
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "2 squads"));
    SelectionSummary sum = panel.DescribeSelection();
    CHECK_INT(sum.squads, 2);
    CHECK_INT(sum.units, 3);
    CHECK_INT(sum.without_squad, 0);

    SelectIds(panel, w, SelectOp::Replace, {13, 20});
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "1 squad, 1 without squad"));

    SelectIds(panel, w, SelectOp::Replace, {13});
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Squad: Bravo (1 of 2)"));

    // No member count supplied: none is shown.
    w.Find(13)->squad.has_member_count = false;
    ++w.snapshot_generation;
    w.Apply(panel);
    panel.UpdateLayout(in);
    CHECK(HasLine(panel.Layout(), "Squad: Bravo"));

    // No squad descriptor supplied: no squad line.
    SelectIds(panel, w, SelectOp::Replace, {20});
    panel.UpdateLayout(in);
    bool squad_line = false;
    for (int i = 0; i < panel.Layout().line_count; ++i) {
        if (panel.Layout().lines[i].role == LineRole::Squad) squad_line = true;
    }
    CHECK(!squad_line);
}

// ---------------------------------------------------------------------------
// Layout dump for the runner's independent geometry checks
// ---------------------------------------------------------------------------

void TestIncompleteSnapshotPreservesSets() {
    auto panel = std::make_unique<CommandPanel>();
    std::vector<EntitySnapshot> entities(kMaxEntities + 1);
    for (int i = 0; i <= kMaxEntities; ++i) {
        auto &e = entities[i];
        e.key = EntityKey{71, static_cast<uint32_t>(i + 1), 1};
        e.kind = EntityKind::Unit; e.relation = TeamRelation::Friendly;
        e.control = ControlOwner::CommandAI; e.availability = Availability::Available;
        e.selectable = true; e.capabilities = ActionBit(Action::Hold);
    }
    WorldSnapshot world{};
    world.session = 71; world.snapshot_generation = 1; world.context_generation = 1;
    world.entities = entities.data(); world.entity_count = kMaxEntities;
    CHECK(panel->ApplySnapshot(world).accepted);
    const EntityKey kept = entities[kMaxEntities - 1].key;
    panel->Select(SelectOp::Replace, &kept, 1); panel->SaveSelectionSet(0);
    ActionIntent intent{}; CHECK(panel->BuildIntent(Action::Hold, &intent));
    world.entity_count = kMaxEntities + 1; ++world.snapshot_generation;
    const auto refused = panel->ApplySnapshot(world);
    CHECK(!refused.accepted && refused.truncated_entities == 1);
    CHECK(refused.pruned_selection == 0 && refused.pruned_saved == 0);
    CHECK(panel->SelectionCount() == 1 && panel->SelectionSetCount(0) == 1);
    CHECK(!panel->BuildIntent(Action::Hold, &intent));
    CHECK(!panel->Layout().visible && !panel->LayoutIsCurrent());
    world.entity_count = kMaxEntities; ++world.snapshot_generation;
    CHECK(panel->ApplySnapshot(world).accepted);
    CHECK(panel->IsSelected(kept));
    panel->ClearSelection(); panel->RecallSelectionSet(0, SelectOp::Replace);
    CHECK(panel->IsSelected(kept));
    CHECK(panel->BuildIntent(Action::Hold, &intent));
    world.entities = nullptr; world.entity_count = 1;
    CHECK(!panel->ApplySnapshot(world).accepted);
    CHECK(panel->SelectionCount() == 1 && panel->SelectionSetCount(0) == 1);
}

void TestTakeRequiresOneSelection() {
    auto panel = std::make_unique<CommandPanel>();
    EntitySnapshot entities[2]{};
    for (int i = 0; i < 2; ++i) {
        auto &e = entities[i]; e.key = EntityKey{83, static_cast<uint32_t>(i + 1), 1};
        e.kind = EntityKind::Unit; e.relation = TeamRelation::Friendly;
        e.control = ControlOwner::CommandAI; e.availability = Availability::Available;
        e.selectable = true; e.capabilities = ActionBit(Action::TakeControl) | ActionBit(Action::Focus);
    }
    WorldSnapshot world{}; world.session = 83; world.snapshot_generation = 1;
    world.context_generation = 1; world.entities = entities; world.entity_count = 2;
    CHECK(panel->ApplySnapshot(world).accepted);
    EntityKey keys[2]{entities[0].key, entities[1].key};
    panel->Select(SelectOp::Replace, keys, 2);
    ActionIntent intent{};
    CHECK(panel->Summarize(Action::TakeControl).shown);
    CHECK(panel->Summarize(Action::TakeControl).eligible == 0);
    CHECK(!panel->BuildIntent(Action::TakeControl, &intent));
    CHECK(panel->BuildIntent(Action::Focus, &intent));
    panel->Select(SelectOp::Replace, keys, 1);
    CHECK(panel->BuildIntent(Action::TakeControl, &intent) && intent.key_count == 1);
    CHECK(panel->CheckIntent(intent) == IntentStatus::Current);
    intent.key_count = 2; intent.selected_count = 2; intent.keys[1] = keys[1];
    CHECK(panel->CheckIntent(intent) == IntentStatus::Malformed);
}

void PrintRect(const char* tag, const Rect& r) {
    std::printf("%s %.3f %.3f %.3f %.3f", tag, static_cast<double>(r.x),
                static_cast<double>(r.y), static_cast<double>(r.w), static_cast<double>(r.h));
}

// --dump SCENARIO WIDTH HEIGHT SCALE CHAR_W CHAR_H [RX RY RW RH]...
int Dump(int argc, char** argv) {
    if (argc < 8) {
        std::printf("usage: --dump SCENARIO WIDTH HEIGHT SCALE CHAR_W CHAR_H [RX RY RW RH]...\n");
        return 2;
    }
    const std::string scenario = argv[2];
    const float width = static_cast<float>(std::atof(argv[3]));
    const float height = static_cast<float>(std::atof(argv[4]));
    const float scale = static_cast<float>(std::atof(argv[5]));
    Font font;
    font.char_w = static_cast<float>(std::atof(argv[6]));
    font.char_h = static_cast<float>(std::atof(argv[7]));

    World w = MakeWorld();
    std::unique_ptr<CommandPanel> holder = NewPanel();
    CommandPanel& panel = *holder;
    w.Apply(panel);
    if (scenario == "single") {
        SelectIds(panel, w, SelectOp::Replace, {10});
    } else if (scenario == "human") {
        SelectIds(panel, w, SelectOp::Replace, {20});
    } else if (scenario == "structure") {
        SelectIds(panel, w, SelectOp::Replace, {50});
    } else if (scenario == "mixed") {
        SelectIds(panel, w, SelectOp::Replace, {10, 11, 20, 30, 40});
    } else if (scenario == "all") {
        SelectEverything(panel, w);
    } else {
        std::printf("unknown scenario %s\n", scenario.c_str());
        return 2;
    }

    LayoutInput in = MakeInput(width, height, scale, &font);
    for (int i = 8; i + 3 < argc && in.reservation_count < kMaxReservations; i += 4) {
        AddReservation(&in, static_cast<float>(std::atof(argv[i])),
                       static_cast<float>(std::atof(argv[i + 1])),
                       static_cast<float>(std::atof(argv[i + 2])),
                       static_cast<float>(std::atof(argv[i + 3])));
    }
    panel.UpdateLayout(in);
    const PanelLayout& L = panel.Layout();

    PrintRect("SAFE", in.safe);
    std::printf("\n");
    for (int i = 0; i < in.reservation_count; ++i) {
        PrintRect("RESERVED", in.reservations[i]);
        std::printf("\n");
    }
    PrintRect("PANEL", L.panel);
    std::printf(" %d %d %d %d\n", L.visible ? 1 : 0, L.no_room ? 1 : 0, L.clipped ? 1 : 0,
                L.hidden_controls);
    if (!L.visible) return 0;
    for (int i = 0; i < L.line_count; ++i) {
        PrintRect("LINE", L.lines[i].bounds);
        std::printf(" %.3f|%s\n",
                    static_cast<double>(Measured(L.lines[i].text, font, L.scale)),
                    L.lines[i].text);
    }
    for (int i = 0; i < L.control_count; ++i) {
        const Control& c = L.controls[i];
        PrintRect("CONTROL", c.rect);
        std::printf(" ");
        PrintRect("TEXT", c.text_bounds);
        std::printf(" %d %d %.3f|%s\n", static_cast<int>(c.action), c.enabled ? 1 : 0,
                    static_cast<double>(Measured(c.label, font, L.scale)), c.label);
    }
    if (L.has_detail_row) {
        float widest = Measured(L.detail_legend, font, L.scale);
        for (int i = 0; i < L.control_count; ++i) {
            float each = Measured(L.controls[i].detail, font, L.scale);
            if (each > widest) widest = each;
        }
        PrintRect("DETAIL", L.detail_row);
        std::printf(" %.3f\n", static_cast<double>(widest));
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

struct TestCase {
    const char* name;
    void (*run)();
};

const TestCase kTests[] = {
    {"identity_and_generation", TestIdentityAndGeneration},
    {"incomplete_snapshot_preserves_sets", TestIncompleteSnapshotPreservesSets},
    {"take_requires_one_selection", TestTakeRequiresOneSelection},
    {"select_ops", TestSelectOps},
    {"saved_sets", TestSavedSets},
    {"single_unit", TestSingleUnit},
    {"mixed_counts", TestMixedCounts},
    {"structure_selection", TestStructureSelection},
    {"human_owned", TestHumanOwned},
    {"unavailable_and_scripted", TestUnavailableAndScripted},
    {"stale_intent", TestStaleIntent},
    {"press_outside_cross_release", TestPressOutsideCrossRelease},
    {"press_inside_release_outside", TestPressInsideReleaseOutside},
    {"disabled_and_padding_capture", TestDisabledAndPaddingCapture},
    {"change_while_pressed", TestChangeWhilePressed},
    {"base_resolution_fit", TestBaseResolutionFit},
    {"no_invented_production", TestNoInventedProduction},
    {"paint_matches_layout", TestPaintMatchesLayout},
    {"targets_and_host_state", TestTargetsAndHostState},
    {"squad_versus_selection", TestSquadVersusSelection},
};

const int kTestCount = static_cast<int>(sizeof(kTests) / sizeof(kTests[0]));

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--list") == 0) {
        for (int i = 0; i < kTestCount; ++i) std::printf("%s\n", kTests[i].name);
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--dump") == 0) {
        return Dump(argc, argv);
    }

    // Optional: run only the named controls.
    int ran = 0;
    int failed = 0;
    for (int i = 0; i < kTestCount; ++i) {
        bool wanted = argc < 2;
        for (int a = 1; a < argc; ++a) {
            if (std::strcmp(argv[a], kTests[i].name) == 0) wanted = true;
        }
        if (!wanted) continue;
        g_test_failures = 0;
        kTests[i].run();
        ++ran;
        if (g_test_failures == 0) {
            std::printf("PASS %s\n", kTests[i].name);
        } else {
            ++failed;
            std::printf("FAIL %s (%d failed checks)\n", kTests[i].name, g_test_failures);
        }
    }
    std::printf("SUMMARY ran=%d failed=%d checks=%d\n", ran, failed, g_checks);
    if (ran == 0) return 2;
    return failed == 0 ? 0 : 1;
}
