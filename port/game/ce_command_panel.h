// Commander selection/action panel: a bounded, engine-independent UI model.
//
// The panel consumes validated snapshots supplied by the host, keeps a
// selection made of stable keys only, lays itself out inside a host-supplied
// safe rectangle, and turns completed pointer gestures into action INTENTS.
//
// Contract with the host (the native adapter):
//   * The host owns entity lookup, picking, eligibility and every world order.
//     This module never executes an order and holds no engine pointers.
//   * A snapshot lists every entity whose key is still valid.  A key that is
//     absent, or present with another generation, is obsolete and is pruned.
//   * An ActionIntent is a request, not permission.  Immediately before
//     dispatch the host MUST revalidate authority, liveness, target legality,
//     current ownership and each capability for every key in the intent.
//   * The panel never pauses, moves, protects or takes over anything in the
//     simulation; the piloted unit keeps running while the panel is shown.
//   * The panel binds no physical keys and offers no camera controls.
//
// Per-frame order expected from the host:
//   ApplySnapshot -> UpdateLayout -> pointer events -> Paint.
// Painting and hit testing always use the same stored layout snapshot.  While
// that layout is older than the model (LayoutIsCurrent() is false) the panel
// still captures presses that land on it, but emits no intent.
//
// A CommandPanel object is a few tens of kilobytes; keep it off small stacks.

#ifndef CE_COMMAND_PANEL_H
#define CE_COMMAND_PANEL_H

#include <cstddef>
#include <cstdint>

namespace ce_command_panel {

constexpr int kMaxEntities = 1024;     // entities kept from one snapshot
constexpr int kMaxSelection = 64;      // keys in the selection or a saved set
constexpr int kMaxSavedSets = 8;       // saved selection slots
constexpr int kMaxReservations = 4;    // host rectangles the panel must avoid
constexpr int kNameCapacity = 32;      // bytes, including the terminator
constexpr int kTextCapacity = 64;      // bytes, including the terminator
constexpr int kMaxLines = 6;           // text lines above the controls

// ---------------------------------------------------------------------------
// Snapshot input
// ---------------------------------------------------------------------------

// Stable identity.  `session` identifies the world/session instance, `id` is
// the host's stable entity id (never 0) and `generation` changes whenever the
// id is reused or the entity is replaced.
struct EntityKey {
    uint64_t session;
    uint32_t id;
    uint32_t generation;
};

inline bool operator==(const EntityKey& a, const EntityKey& b) {
    return a.session == b.session && a.id == b.id && a.generation == b.generation;
}
inline bool operator!=(const EntityKey& a, const EntityKey& b) { return !(a == b); }

enum class EntityKind : uint8_t { Unit = 0, Structure = 1 };

// Team relation is descriptive only.  A friendly relation never grants
// command authority; authority comes from ControlOwner and capabilities.
enum class TeamRelation : uint8_t { Unknown = 0, Friendly, Neutral, Hostile };

// Who is driving the entity right now, as asserted by the host.
enum class ControlOwner : uint8_t {
    Unknown = 0,
    None,           // nothing drives it (typical for a structure)
    CommandAI,      // AI-driven and under this commander's authority
    IndependentAI,  // AI-driven, not under this commander's authority
    Scripted,       // mission-script driven; takes no orders
    LocalHuman,     // the local player is piloting it
    OtherHuman      // another human is piloting it
};

enum class Availability : uint8_t { Unavailable = 0, Available = 1, Dead = 2 };

enum class Action : uint8_t {
    Move = 0,
    Advance,
    Hold,
    FallBack,
    ReturnToAI,
    TakeControl,
    Focus,
    RequestVehicle,
    CancelRequest,
    Count
};

constexpr int kActionCount = static_cast<int>(Action::Count);

constexpr uint32_t ActionBit(Action a) {
    return static_cast<uint32_t>(1u) << static_cast<unsigned>(a);
}
constexpr uint32_t kAllActionBits = (static_cast<uint32_t>(1u) << kActionCount) - 1u;

// The squad an entity already belongs to in the simulation.  Read only: the
// panel displays it and never writes membership.
struct SquadDescriptor {
    bool present;
    uint32_t squad_id;
    uint32_t squad_generation;
    bool has_member_count;
    uint16_t member_count;
    char name[kNameCapacity];
};

enum class QueueOwner : uint8_t { Unknown = 0, LocalCommander, Other };

// A request/queue state the host has actually observed.  Nothing about a
// queue is displayed unless `present` is set, and each optional part is shown
// only when its own flag is set.
struct QueueState {
    bool present;
    QueueOwner owner;
    bool has_progress;
    uint16_t progress_permille;   // 0..1000
    bool has_pending_count;
    uint16_t pending_count;
    char item[kNameCapacity];     // host-supplied label; may be empty
};

struct EntitySnapshot {
    EntityKey key;
    EntityKind kind;
    TeamRelation relation;
    ControlOwner control;
    Availability availability;
    bool selectable;              // host allows this entity to be newly selected
    uint32_t capabilities;        // ActionBit() mask of actions supported now
    SquadDescriptor squad;
    QueueState queue;
    char name[kNameCapacity];
};

struct WorldSnapshot {
    uint64_t session;
    uint32_t snapshot_generation;   // opaque token, compared for equality only
    uint32_t context_generation;    // changes when the input/command context changes
    uint32_t targeted_actions;      // ActionBit() mask: actions that still need a world target
    bool has_armed_target_action;   // host is currently waiting for a target for...
    Action armed_target_action;     // ...this action (display only)
    const EntitySnapshot* entities; // read during ApplySnapshot only; never retained
    int entity_count;
};

struct SnapshotResult {
    int accepted_entities;
    int rejected_entities;      // wrong session, id 0 or duplicate id
    int truncated_entities;     // beyond kMaxEntities
    int pruned_selection;       // selected keys that became obsolete
    int pruned_saved;           // saved-set keys that became obsolete
    bool session_changed;
    bool context_changed;
    // False means the host did not supply a complete bounded snapshot.
    // Existing selections are retained, but layout/actions are invalid until a valid update.
    bool accepted;
};

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

enum class SelectOp : uint8_t { Replace = 0, Add, Toggle, Remove };

struct SelectResult {
    int added;
    int removed;
    int rejected_stale;         // key does not match the current snapshot exactly
    int rejected_unselectable;  // entity exists but the host marked it unselectable
    int rejected_capacity;      // selection full, or too many keys in one call
    bool changed;
};

// Why selected entities cannot take part in an action.
struct ActionSummary {
    Action action;
    bool shown;                    // at least one selected entity reports the capability
    uint16_t selected;
    uint16_t eligible;
    uint16_t blocked_unavailable;  // dead or unavailable
    uint16_t blocked_human;        // piloted by a human; never given AI orders
    uint16_t blocked_no_authority; // scripted, independent or otherwise not commandable
    uint16_t blocked_unsupported;  // capability not reported for that entity
    uint64_t eligible_hash;        // digest of the eligible keys, in key order
};

struct SelectionSummary {
    uint16_t selected;
    uint16_t units;
    uint16_t structures;
    uint16_t commandable;    // available and CommandAI
    uint16_t human;          // available and piloted by a human
    uint16_t no_authority;   // available, neither commandable nor human
    uint16_t unavailable;    // dead or unavailable
    uint16_t squads;         // distinct native squads among selected units
    uint16_t without_squad;  // selected units with no squad descriptor
};

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

struct Rect {
    float x, y, w, h;
};

struct TextSize {
    float w, h;
};

// Must return the real pixel size of `text` drawn at `scale` with the font
// the draw callbacks will use.
typedef TextSize (*MeasureTextFn)(void* user, const char* text, float scale);

struct LayoutInput {
    Rect safe;                              // host-supplied safe rectangle, pixels
    float scale;                            // UI scale, > 0
    Rect reservations[kMaxReservations];    // host dialogue/portrait rectangles
    int reservation_count;
    MeasureTextFn measure;                  // used during UpdateLayout only
    void* measure_user;
};

enum class LineRole : uint8_t { Title = 0, Kind, Owner, Squad, Queue };

enum class IdentityStyle : uint8_t {
    None = 0,
    Command,       // commandable AI
    HumanLocal,    // the local player pilots it
    HumanOther,    // another human pilots it
    NoAuthority,   // scripted / independent / unknown controller
    Structure,
    Unavailable,   // dead or unavailable
    Mixed,         // several entities, no human among them
    MixedWithHuman // several entities, at least one human-piloted
};

struct TextLine {
    LineRole role;
    Rect bounds;                 // measured text box; text origin is its top-left
    char text[kTextCapacity];
};

struct Control {
    Action action;
    bool enabled;                // eligible > 0
    bool needs_target;           // host listed the action in targeted_actions
    bool host_armed;             // host reports it is waiting for this action's target
    uint16_t selected;
    uint16_t eligible;
    uint16_t blocked;
    uint64_t eligible_hash;
    Rect rect;
    Rect text_bounds;            // measured label box inside `rect`
    char label[kNameCapacity];
    char detail[kTextCapacity];  // blocked breakdown; empty when nothing is blocked
};

// One immutable snapshot used for both painting and hit testing.
struct PanelLayout {
    bool visible;                // false: nothing is drawn and nothing is hit
    bool no_room;                // selection exists but the panel cannot fit at all
    bool clipped;                // optional lines or controls were dropped to fit
    int hidden_controls;         // controls dropped because of `clipped`
    uint32_t generation;         // bumps when anything interactive changes
    uint64_t session;
    uint32_t snapshot_generation;
    uint32_t context_generation;
    uint32_t selection_revision;
    float scale;
    Rect available;              // safe rectangle minus reservations
    Rect panel;
    Rect stripe;                 // identity stripe on the panel's left edge
    IdentityStyle identity;
    int line_count;
    TextLine lines[kMaxLines];
    bool has_separator;
    Rect separator;              // thin rule between text and controls
    int control_count;
    Control controls[kActionCount];
    bool has_detail_row;         // present when some control is partially blocked
    Rect detail_row;
    char detail_legend[kTextCapacity];
};

// ---------------------------------------------------------------------------
// Pointer interaction and intents
// ---------------------------------------------------------------------------

enum class HitKind : uint8_t { None = 0, Padding, Control, DisabledControl };

enum class PointerOwner : uint8_t { World = 0, Panel };

enum class PointerOutcome : uint8_t {
    World = 0,           // not the panel's event
    PanelIdle,           // pointer over the panel with no press in progress
    Armed,               // press is tracking an enabled control
    ConsumedPadding,     // press began on panel padding or text; no effect
    ConsumedDisabled,    // press began on a disabled control; no effect
    ConsumedStale,       // press began while the layout was out of date; no effect
    IntentEmitted,       // released on the control that was pressed
    ReleasedOffControl,  // released somewhere else; no effect
    CancelledByChange    // layout, context, selection or eligibility changed mid-press
};

struct PointerResult {
    PointerOwner owner;      // Panel: the host must not treat the event as a world event
    PointerOutcome outcome;
};

struct ActionIntent {
    Action action;
    bool needs_world_target;      // host must still obtain a target before dispatch
    uint64_t session;
    uint32_t snapshot_generation;
    uint32_t context_generation;
    uint32_t layout_generation;
    uint32_t selection_revision;
    uint16_t selected_count;      // selection size when the intent was built
    uint16_t blocked_count;       // selected entities NOT included in `keys`
    uint16_t key_count;
    EntityKey keys[kMaxSelection]; // eligible entities only, ascending id
};

enum class IntentStatus : uint8_t {
    Current = 0,     // matches the model's present snapshot; host still revalidates
    Malformed,
    StaleSession,
    StaleContext,
    StaleEntity,     // a key is gone or has another generation
    NotEligible,     // a key no longer qualifies for the action
    StaleSnapshot    // keys still qualify, but the intent predates the present snapshot
};

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Colours are opaque values passed straight back to the callbacks.
struct Palette {
    uint32_t background;
    uint32_t border;
    uint32_t separator;
    uint32_t text;
    uint32_t text_dim;
    uint32_t text_disabled;
    uint32_t button;
    uint32_t button_hover;
    uint32_t button_pressed;
    uint32_t button_disabled;
    uint32_t button_border;
    uint32_t accent;       // commandable identity, host-armed control
    uint32_t human;        // human-piloted identity
    uint32_t structure;    // structure identity
    uint32_t muted;        // no-authority and mixed identity
    uint32_t warning;      // dead / unavailable identity
};

struct DrawCallbacks {
    void* user;
    void (*fill_rect)(void* user, Rect rect, uint32_t color);
    void (*line)(void* user, float x0, float y0, float x1, float y1, uint32_t color);
    void (*text)(void* user, float x, float y, const char* text, float scale, uint32_t color);
};

// ---------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------

class CommandPanel {
public:
    CommandPanel();

    // Forget everything: snapshot, selection, saved sets, layout and gesture.
    void Reset();

    // Copy the host's validated facts and prune obsolete keys.  A session or
    // context change, or any pruned selected key, cancels an armed press.
    SnapshotResult ApplySnapshot(const WorldSnapshot& snapshot);

    // Selection.  Keys must match the present snapshot exactly.  The stored
    // selection is kept in ascending id order.
    SelectResult Select(SelectOp op, const EntityKey* keys, int count);
    void ClearSelection();
    int SelectionCount() const;
    bool SelectionKey(int index, EntityKey* out) const;
    bool IsSelected(const EntityKey& key) const;
    uint32_t SelectionRevision() const;

    // Saved selection sets are UI bookmarks only.  They never regroup AI,
    // never write squad membership and never emit an intent.
    bool SaveSelectionSet(int slot);
    SelectResult RecallSelectionSet(int slot, SelectOp op);
    void ClearSelectionSet(int slot);
    int SelectionSetCount(int slot) const;

    // Deterministic facts about the present selection.
    SelectionSummary DescribeSelection() const;
    ActionSummary Summarize(Action action) const;

    // Rebuild the stored layout.  Returns true when something interactive
    // changed (geometry, controls, eligibility, context or selection), which
    // also cancels an armed press.
    bool UpdateLayout(const LayoutInput& input);
    const PanelLayout& Layout() const;
    bool LayoutIsCurrent() const;

    // Pointer events for the primary button.  A press that starts outside the
    // panel stays a world gesture until its release, even across the panel.
    // A press that starts on the panel is owned by the panel until release.
    // `intent` is written only when the outcome is IntentEmitted.
    HitKind HitTest(float x, float y) const;
    PointerResult PointerDown(float x, float y);
    PointerResult PointerMove(float x, float y);
    PointerResult PointerUp(float x, float y, ActionIntent* intent);
    // Focus loss or similar: a panel press stays consumed until its release;
    // a world gesture is simply forgotten.
    void CancelPointer();
    // The pointer left the window: drop hover feedback.
    void PointerLeft();

    // Build an intent without a pointer (for host-owned input routes).  Fails
    // when no selected entity is eligible.  Uses the present snapshot.
    bool BuildIntent(Action action, ActionIntent* intent) const;

    // Compare an intent with the model's present snapshot.  This is advisory;
    // it never replaces the host's own revalidation against the simulation.
    IntentStatus CheckIntent(const ActionIntent& intent) const;

    // Draw the stored layout.  Missing callbacks are skipped.
    void Paint(const DrawCallbacks& draw, const Palette& palette) const;

private:
    enum class Gesture : uint8_t { Idle = 0, World, PanelArmed, PanelConsumed };

    int FindEntityById(uint32_t id) const;
    int FindEntityExact(const EntityKey& key) const;
    int FindSelected(const EntityKey& key) const;
    bool InsertSelected(const EntityKey& key);
    void RemoveSelectedAt(int index);
    void SelectionChanged();
    void InvalidateInteraction();
    int ControlAt(float x, float y) const;
    void RefreshHover();
    ActionSummary SummarizeInternal(Action action, EntityKey* eligible_out) const;
    void BuildLayout(const LayoutInput& input, PanelLayout* out) const;

    bool have_snapshot_;
    uint64_t session_;
    uint32_t snapshot_generation_;
    uint32_t context_generation_;
    uint32_t targeted_actions_;
    bool has_armed_target_action_;
    Action armed_target_action_;
    int entity_count_;
    EntitySnapshot entities_[kMaxEntities];

    int selection_count_;
    EntityKey selection_[kMaxSelection];
    uint32_t selection_revision_;

    int saved_count_[kMaxSavedSets];
    EntityKey saved_[kMaxSavedSets][kMaxSelection];

    PanelLayout layout_;
    bool layout_dirty_;
    uint32_t layout_generation_;

    Gesture gesture_;
    PointerOutcome consumed_reason_;
    int armed_control_;
    bool armed_inside_;
    uint32_t armed_epoch_;
    uint32_t epoch_;
    int hover_control_;
    bool pointer_known_;
    float pointer_x_;
    float pointer_y_;
};

}  // namespace ce_command_panel

#endif  // CE_COMMAND_PANEL_H
