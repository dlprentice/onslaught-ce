// Selection and command-request controller for a native RTS view.
//
// The controller sits between the host, which owns every native object,
// projection, pick ray and order, and the command panel, which owns the
// selection, layout, painting and its own pointer presses.  It turns pointer
// gestures into selection changes and confirmed actions into Requests.
//
// Contract with the host:
//   * Each frame the host supplies one complete, bounded Frame: every current
//     unit with its lifetime key, descriptive facts and optional screen
//     circle.  The controller keeps values only; it never stores an address.
//   * A Request is intent, never authority.  The host takes it once and, for
//     every key, resolves the lifetime key again and rechecks liveness,
//     allegiance, squad/script ownership, ground legality and capability
//     immediately before acting.  After a refused or failed native command
//     the host re-reads native state; nothing here assumes an order survived.
//   * Action::Focus is a view request.  It is never a simulation order.
//   * Selecting a whole squad is a UI selection of its current members.  The
//     controller never writes squad membership and never regroups AI.  Saved
//     sets are UI bookmarks and are unrelated to native squads.
//   * The controller binds no physical keys, moves no camera and draws
//     nothing.  Modifiers arrive as plain flags chosen by the host.
//
// Per-frame order expected from the host:
//   BeginFrame -> UpdateLayout -> pointer events and host hotkeys ->
//   Panel().Paint -> TakeRequest.
//
// A frame that is incomplete, oversized, null, or carries a key that cannot
// be represented exactly makes picking and every action unavailable.  The
// selection and saved sets are kept as they were, and the next complete valid
// frame restores normal behaviour.
//
// A SelectionController is roughly a hundred kilobytes; give it static or
// long-lived storage rather than a small stack.

#ifndef CE_COMMAND_SELECTION_H
#define CE_COMMAND_SELECTION_H

#include <cstdint>

#include "ce_command_lifetime.h"
#include "ce_command_panel.h"

namespace ce_command_selection {

using NativeKey = ::CeCommandLifetimeKey;
using PanelKey = ce_command_panel::EntityKey;
using Action = ce_command_panel::Action;

constexpr int kMaxUnits = ce_command_panel::kMaxEntities;        // rows in one Frame
constexpr int kMaxRequestKeys = ce_command_panel::kMaxSelection; // keys in one Request
constexpr float kDefaultDragThreshold = 4.0f;                    // pixels; see SetDragThreshold

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

inline bool SameKey(const NativeKey& a, const NativeKey& b) {
    return a.world == b.world && a.id == b.id && a.generation == b.generation;
}

// Exact, reversible mapping between a native lifetime key and a panel key:
//   session = world, id = native id + 1, generation = native generation.
// ToPanelKey fails, instead of truncating, unless native id + 1 fits a nonzero
// 32-bit value and the generation is nonzero and fits 32 bits.  ToNativeKey
// fails for a panel id or generation of zero.  On failure `*out` is zeroed.
// `out` may be null when only the verdict is wanted.
bool ToPanelKey(const NativeKey& native, PanelKey* out);
bool ToNativeKey(const PanelKey& panel, NativeKey* out);

// ---------------------------------------------------------------------------
// Frame input
// ---------------------------------------------------------------------------

// The panel's descriptive and capability facts for one unit, as observed by
// the host this frame.  `capabilities` is the only source of action
// availability; the panel may refuse more, never less.
struct UnitFacts {
    ce_command_panel::EntityKind kind;
    ce_command_panel::TeamRelation relation;
    ce_command_panel::ControlOwner control;
    ce_command_panel::Availability availability;
    bool selectable;
    uint32_t capabilities;                    // ce_command_panel::ActionBit() mask
    ce_command_panel::SquadDescriptor squad;  // read only; ids and generations must be nonzero
    ce_command_panel::QueueState queue;
    char name[ce_command_panel::kNameCapacity];
};

// Where the unit is on screen right now, in the same pixel space as pointer
// events.  A circle with `projected` false, a non-finite member or a negative
// radius cannot be clicked or boxed; the unit itself stays in the frame.
struct ScreenCircle {
    float x;
    float y;
    float radius;
    bool projected;
};

struct UnitSnapshot {
    NativeKey key;
    UnitFacts facts;
    ScreenCircle screen;
};

struct Frame {
    uint64_t world;                // world/session; never 0
    uint32_t snapshot_generation;  // opaque token, compared for equality only
    uint32_t context_generation;   // changes when the input/command context changes
    uint32_t targeted_actions;     // ActionBit() mask: actions that need a world point
    bool complete;                 // false when the host could not list every current unit
    const UnitSnapshot* units;     // read during BeginFrame only; never retained
    int unit_count;                // 0..kMaxUnits
};

enum class FrameStatus : uint8_t {
    NoFrame = 0,    // nothing supplied since Reset
    Valid,
    Incomplete,     // host flagged the list as partial
    BadCount,       // negative unit_count
    Oversized,      // more than kMaxUnits rows
    NullUnits,      // rows promised but no array given
    BadWorld,       // world is 0
    BadKey,         // a row key cannot be mapped exactly
    WorldMismatch,  // a row key names another world
    DuplicateId,    // two rows share a native id
    PanelRejected   // the panel did not accept every row
};

// Why armed or queued work was dropped.
enum class CancelReason : uint8_t {
    None = 0,
    HostCancel,
    InvalidFrame,
    WorldChanged,
    ContextChanged,
    SelectionChanged,
    UnitStale,        // an intended key is gone or has another generation
    UnitNotEligible,  // an intended unit no longer qualifies for the action
    TargetKindChanged, // host changed whether this action requires a point
    Replaced          // a newer action took the place of an armed target
};

struct FrameResult {
    FrameStatus status;
    int bad_index;           // first offending row, or -1
    bool world_changed;
    bool context_changed;
    int pruned_selection;    // selected keys that became obsolete
    int pruned_saved;        // saved-set keys that became obsolete
    int rejected_circles;    // rows marked projected whose circle was unusable
    bool pending_cancelled;  // an armed target or queued request was dropped
    CancelReason cancel_reason;
};

// ---------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------

// Host-chosen meaning of the modifiers held when the primary button is
// released.  `additive` toggles on a click and adds on a drag rectangle.
struct ClickModifiers {
    bool additive;
    bool whole_squad;
};

struct SelectionChange {
    ce_command_panel::SelectResult result;
    bool picked;           // a click landed on a unit
    bool whole_squad;      // the click expanded to the unit's native squad
    NativeKey picked_key;  // valid when `picked`
};

enum class ArmStatus : uint8_t {
    None = 0,      // no action was attempted
    Queued,        // a ready Request is waiting in TakeRequest
    Armed,         // waiting for SubmitTarget
    Unavailable,   // no usable frame
    NotEligible,   // no selected unit can take this action
    Stale,         // the intent no longer matches the frame or selection
    QueueFull      // an unconsumed Request is waiting; nothing was changed
};

enum class GestureOutcome : uint8_t {
    None = 0,     // nothing to do: no live press, a cancelled press or no usable frame
    PanelOwned,   // the panel owns this event; see `panel` and `action`
    WorldPress,   // a world press is being tracked and is still a click
    WorldDrag,    // the press became a drag rectangle; see DragRect
    Click,        // a click was applied to the selection; see `selection`
    Box,          // a drag rectangle was applied to the selection; see `selection`
    TargetPoint,  // a click at (x, y) while a target is armed: the host resolves
                  // the world point there and calls SubmitTarget
    Suppressed    // swallowed by SuppressUntilRelease
};

struct GestureResult {
    GestureOutcome outcome;
    ce_command_panel::PointerResult panel;  // the panel's own verdict
    ArmStatus action;                       // set when a panel release emitted an intent
    SelectionChange selection;              // set for Click and Box
    uint64_t target_token;                   // exact arm for TargetPoint; never reuse a point with another token
    float x;                                // set for TargetPoint
    float y;
};

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

struct WorldPoint {
    float x;
    float y;
    float z;
};

// What the user asked for.  Values only: the host resolves every key again
// and revalidates it against the live world before doing anything.
struct Request {
    Action action;
    bool has_point;                // true for a targeted action
    WorldPoint point;
    uint64_t world;
    uint32_t snapshot_generation;  // the frame the keys were last checked against
    uint32_t context_generation;
    uint32_t selection_revision;
    uint16_t selected_count;       // selection size when the action was accepted
    uint16_t blocked_count;        // selected units left out of `keys`
    uint16_t key_count;
    NativeKey keys[kMaxRequestKeys];  // intended units only, ascending native id
};

enum class TargetStatus : uint8_t {
    Submitted = 0,   // `*out` holds the Request and the armed target is cleared
    NoArmedTarget,
    NoOutput,        // `out` was null; the target stays armed
    NonFinitePoint,  // a coordinate was NaN or infinite; the target stays armed
    Stale,           // the armed target was no longer valid and was cancelled
    StaleArm         // token names another arm; newer work stays intact
};

// ---------------------------------------------------------------------------
// The controller
// ---------------------------------------------------------------------------

class SelectionController {
public:
    SelectionController();

    // Forget the frame, selection, saved sets, gesture and pending work.  The
    // drag threshold is configuration and is kept.
    void Reset();

    // Validate and adopt the host's frame.  An unusable frame leaves the
    // selection and saved sets untouched, cancels gestures and pending work,
    // and disables picking and actions until a valid frame arrives.  A valid
    // frame prunes keys that no longer match exactly and rechecks pending work.
    FrameResult BeginFrame(const Frame& frame);
    bool FrameUsable() const;
    FrameStatus LastFrameStatus() const;

    // The panel, for Layout(), Paint(), HitTest() and summaries.  Changes go
    // through the controller so its bookkeeping stays in step.
    const ce_command_panel::CommandPanel& Panel() const;
    bool UpdateLayout(const ce_command_panel::LayoutInput& input);

    // Distance in pixels a press may travel and still count as a click.
    // Ignored unless finite and not negative.
    void SetDragThreshold(float pixels);

    // Primary-button events.  PointerDown must be sent only when the button
    // actually goes down; a release with no live press does nothing, so a
    // button held across a mode boundary never becomes a click.  The panel is
    // consulted first and a press it captures stays its own until release.
    // Events with a non-finite position never select or pick anything.
    GestureResult PointerDown(float x, float y);
    GestureResult PointerMove(float x, float y);
    GestureResult PointerUp(float x, float y, const ClickModifiers& modifiers);
    void PointerLeft();

    // The drag rectangle to draw while the outcome is WorldDrag.
    bool DragRect(ce_command_panel::Rect* out) const;

    // Selection.  Nothing is ever selected implicitly.  SelectAll takes every
    // selectable unit of the present frame, lowest native ids first, up to the
    // panel's selection capacity.
    SelectionChange SelectAll();
    void ClearSelection();
    int SelectionCount() const;
    bool SelectionKey(int index, NativeKey* out) const;
    bool IsSelected(const NativeKey& key) const;

    // Saved sets: UI bookmarks holding exact keys.  Recalling one replaces the
    // selection, or adds to it when `additive` is set.
    bool SaveSet(int slot);
    SelectionChange RecallSet(int slot, bool additive);
    void ClearSet(int slot);
    int SetCount(int slot) const;

    // Start an action for the present selection from a host-owned input route.
    // Uses the same intent and eligibility checks as a panel release, without
    // requiring a visible layout or control. An action the frame lists
    // in targeted_actions is armed and waits for SubmitTarget; any other
    // action becomes the one queued Request.  While a Request is waiting,
    // every new action is refused with QueueFull.
    ArmStatus ArmAction(Action action);

    // The armed target keeps the exact unit keys it was armed with.  It is
    // cancelled, never re-bound, when any of them stops being valid or
    // eligible, or when the world, context or selection changes.
    bool HasArmedTarget() const;
    bool ArmedAction(Action* out) const;
    // Capture this token before resolving a point. It remains unique across Reset.
    uint64_t TargetToken() const;
    // A stale token refuses without cancelling a newer arm or writing output.
    TargetStatus SubmitTarget(uint64_t target_token, const WorldPoint& point, Request* out);
    void CancelArmedTarget();

    // The ready queue holds at most one Request and hands it out once.
    bool HasRequest() const;
    bool TakeRequest(Request* out);

    // Mode, focus or world boundary: drop the armed target, the queued
    // Request and any gesture.  A press the panel captured stays consumed
    // until its release; a world press is forgotten and its release ignored.
    void Cancel();

    // Call when the primary button is already down as command input opens or
    // resumes.  Every pointer press up to and including the next PointerUp is
    // swallowed. Cancel does not lift suppression; forward the real release
    // or a positively observed physical release, even at a non-finite position.
    void SuppressUntilRelease();

    // The reason for the most recent cancellation of pending work.
    CancelReason LastCancelReason() const;

private:
    enum class Gesture : uint8_t { Idle = 0, WorldPress, WorldDrag, Dead, Panel };
    enum class Pending : uint8_t { None = 0, Armed, Queued };

    FrameStatus LoadRows(const Frame& frame, int* bad_index, int* rejected_circles);
    bool PendingStillValid(CancelReason* why) const;
    bool CancelPending(CancelReason why);
    void ReconcilePending();
    void KillGesture();
    bool PastThreshold(float x, float y) const;
    int PickRow(float x, float y) const;
    SelectionChange ApplyClick(float x, float y, const ClickModifiers& modifiers);
    SelectionChange ApplyBox(float x0, float y0, float x1, float y1, bool additive);
    ArmStatus AcceptIntent(const ce_command_panel::ActionIntent& intent);
    bool BuildRequest(Request* out) const;

    ce_command_panel::CommandPanel panel_;

    // The present frame in panel form, plus the usable screen circles.
    ce_command_panel::EntitySnapshot rows_[kMaxUnits];
    ScreenCircle circles_[kMaxUnits];
    int row_count_;
    bool frame_valid_;
    FrameStatus frame_status_;
    bool have_world_;
    uint64_t world_;
    uint32_t snapshot_generation_;
    uint32_t context_generation_;
    uint32_t targeted_actions_;

    // At most one piece of pending work: an armed target or a queued request.
    Pending pending_;
    ce_command_panel::ActionIntent intent_;
    uint64_t arm_serial_;
    CancelReason last_cancel_;

    Gesture gesture_;
    bool suppress_;
    bool press_for_target_;
    uint64_t press_arm_serial_;
    float press_x_;
    float press_y_;
    float cur_x_;
    float cur_y_;
    float last_x_;
    float last_y_;
    float drag_threshold_;
};

}  // namespace ce_command_selection

#endif  // CE_COMMAND_SELECTION_H
