// Selection and command-request controller: implementation.
//
// Standard C++17 only.  Fixed-size storage, no allocation, no engine headers.
// See ce_command_selection.h for the host contract.

#include "ce_command_selection.h"

#include <cmath>
#include <cstring>

namespace ce_command_selection {

namespace cp = ce_command_panel;

namespace {

constexpr uint64_t kMaxU32 = 0xFFFFFFFFull;

bool Finite(float v) { return std::isfinite(v); }

// Squares are taken in double so that no finite float input can overflow.
double DistanceSquared(double ax, double ay, double bx, double by) {
    const double dx = ax - bx;
    const double dy = ay - by;
    return dx * dx + dy * dy;
}

double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool UsableCircle(const ScreenCircle& c) {
    return c.projected && Finite(c.x) && Finite(c.y) && Finite(c.radius) && c.radius >= 0.0f;
}

bool ValidSquad(const cp::SquadDescriptor& s) {
    return s.present && s.squad_id != 0u && s.squad_generation != 0u;
}

void SortById(PanelKey* keys, int count) {
    for (int i = 1; i < count; ++i) {
        const PanelKey key = keys[i];
        int at = i;
        while (at > 0 && keys[at - 1].id > key.id) {
            keys[at] = keys[at - 1];
            --at;
        }
        keys[at] = key;
    }
}

GestureResult BlankResult() {
    GestureResult r{};
    r.outcome = GestureOutcome::None;
    r.panel.owner = cp::PointerOwner::World;
    r.panel.outcome = cp::PointerOutcome::World;
    r.action = ArmStatus::None;
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

bool ToPanelKey(const NativeKey& native, PanelKey* out) {
    if (out != nullptr) {
        out->session = 0;
        out->id = 0;
        out->generation = 0;
    }
    // id + 1 must be a nonzero 32-bit value, so the largest usable id is one
    // below the 32-bit maximum.  Larger ids would wrap onto another unit.
    if (native.id >= kMaxU32) return false;
    if (native.generation == 0 || native.generation > kMaxU32) return false;
    if (out != nullptr) {
        out->session = native.world;
        out->id = static_cast<uint32_t>(native.id + 1);
        out->generation = static_cast<uint32_t>(native.generation);
    }
    return true;
}

bool ToNativeKey(const PanelKey& panel, NativeKey* out) {
    if (out != nullptr) *out = NativeKey{};
    if (panel.id == 0u || panel.generation == 0u) return false;
    if (out != nullptr) {
        out->world = panel.session;
        out->id = static_cast<uint64_t>(panel.id) - 1u;
        out->generation = panel.generation;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SelectionController::SelectionController() : arm_serial_(0), drag_threshold_(kDefaultDragThreshold) {
    Reset();
}

void SelectionController::Reset() {
    panel_.Reset();
    std::memset(rows_, 0, sizeof(rows_));
    std::memset(circles_, 0, sizeof(circles_));
    row_count_ = 0;
    frame_valid_ = false;
    frame_status_ = FrameStatus::NoFrame;
    have_world_ = false;
    world_ = 0;
    snapshot_generation_ = 0;
    context_generation_ = 0;
    targeted_actions_ = 0;

    pending_ = Pending::None;
    std::memset(&intent_, 0, sizeof(intent_));
    last_cancel_ = CancelReason::None;

    gesture_ = Gesture::Idle;
    suppress_ = false;
    press_for_target_ = false;
    press_arm_serial_ = 0;
    press_x_ = 0.0f;
    press_y_ = 0.0f;
    cur_x_ = 0.0f;
    cur_y_ = 0.0f;
    last_x_ = 0.0f;
    last_y_ = 0.0f;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

// Checks the whole frame and converts it to panel rows.  Any defect rejects
// the frame as a whole, so the rows used for picking always match the panel's
// own entity list exactly.
FrameStatus SelectionController::LoadRows(const Frame& frame, int* bad_index,
                                          int* rejected_circles) {
    row_count_ = 0;
    if (!frame.complete) return FrameStatus::Incomplete;
    if (frame.unit_count < 0) return FrameStatus::BadCount;
    if (frame.unit_count > kMaxUnits) return FrameStatus::Oversized;
    if (frame.unit_count > 0 && frame.units == nullptr) return FrameStatus::NullUnits;
    if (frame.world == 0) return FrameStatus::BadWorld;

    for (int i = 0; i < frame.unit_count; ++i) {
        const UnitSnapshot& unit = frame.units[i];
        PanelKey key;
        if (!ToPanelKey(unit.key, &key)) {
            *bad_index = i;
            return FrameStatus::BadKey;
        }
        if (unit.key.world != frame.world) {
            *bad_index = i;
            return FrameStatus::WorldMismatch;
        }
        // Same rule as the panel: one row per id, whatever the generation.
        for (int j = 0; j < i; ++j) {
            if (rows_[j].key.id == key.id) {
                *bad_index = i;
                return FrameStatus::DuplicateId;
            }
        }

        cp::EntitySnapshot& row = rows_[i];
        row.key = key;
        row.kind = unit.facts.kind;
        row.relation = unit.facts.relation;
        row.control = unit.facts.control;
        row.availability = unit.facts.availability;
        row.selectable = unit.facts.selectable;
        row.capabilities = unit.facts.capabilities;
        row.squad = unit.facts.squad;
        row.queue = unit.facts.queue;
        std::memcpy(row.name, unit.facts.name, sizeof(row.name));

        if (UsableCircle(unit.screen)) {
            circles_[i] = unit.screen;
        } else {
            if (unit.screen.projected) ++*rejected_circles;
            circles_[i].x = 0.0f;
            circles_[i].y = 0.0f;
            circles_[i].radius = 0.0f;
            circles_[i].projected = false;
        }
    }
    row_count_ = frame.unit_count;
    return FrameStatus::Valid;
}

FrameResult SelectionController::BeginFrame(const Frame& frame) {
    FrameResult r{};
    r.bad_index = -1;
    int rejected_circles = 0;
    FrameStatus status = LoadRows(frame, &r.bad_index, &rejected_circles);

    if (status == FrameStatus::Valid) {
        const bool was_armed = pending_ == Pending::Armed;
        cp::WorldSnapshot snapshot{};
        snapshot.session = frame.world;
        snapshot.snapshot_generation = frame.snapshot_generation;
        snapshot.context_generation = frame.context_generation;
        snapshot.targeted_actions = frame.targeted_actions;
        snapshot.has_armed_target_action = was_armed;
        snapshot.armed_target_action = was_armed ? intent_.action : Action::Move;
        snapshot.entities = rows_;
        snapshot.entity_count = row_count_;
        const cp::SnapshotResult applied = panel_.ApplySnapshot(snapshot);

        if (!applied.accepted || applied.rejected_entities != 0 ||
            applied.truncated_entities != 0 || applied.accepted_entities != row_count_) {
            status = FrameStatus::PanelRejected;
        } else {
            r.world_changed = have_world_ && frame.world != world_;
            r.context_changed = have_world_ && !r.world_changed &&
                                frame.context_generation != context_generation_;
            r.pruned_selection = applied.pruned_selection;
            r.pruned_saved = applied.pruned_saved;
            r.rejected_circles = rejected_circles;

            have_world_ = true;
            world_ = frame.world;
            snapshot_generation_ = frame.snapshot_generation;
            context_generation_ = frame.context_generation;
            targeted_actions_ = frame.targeted_actions;
            frame_valid_ = true;

            if (r.world_changed || r.context_changed) KillGesture();

            if (pending_ != Pending::None) {
                CancelReason why = CancelReason::None;
                if (!PendingStillValid(&why)) {
                    r.pending_cancelled = CancelPending(why);
                    r.cancel_reason = why;
                }
            }
            // The panel was told a target was armed; correct it if that just ended.
            if (was_armed && pending_ != Pending::Armed) {
                snapshot.has_armed_target_action = false;
                snapshot.armed_target_action = Action::Move;
                panel_.ApplySnapshot(snapshot);
            }
        }
    }

    if (status != FrameStatus::Valid) {
        row_count_ = 0;
        frame_valid_ = false;
        // A deliberately unusable snapshot: the panel keeps its selection and
        // saved sets, and offers no layout or action until a valid one arrives.
        cp::WorldSnapshot unusable{};
        unusable.entities = nullptr;
        unusable.entity_count = -1;
        panel_.ApplySnapshot(unusable);
        if (CancelPending(CancelReason::InvalidFrame)) {
            r.pending_cancelled = true;
            r.cancel_reason = CancelReason::InvalidFrame;
        }
        KillGesture();
    }

    frame_status_ = status;
    r.status = status;
    return r;
}

bool SelectionController::FrameUsable() const { return frame_valid_; }

FrameStatus SelectionController::LastFrameStatus() const { return frame_status_; }

const cp::CommandPanel& SelectionController::Panel() const { return panel_; }

bool SelectionController::UpdateLayout(const cp::LayoutInput& input) {
    return panel_.UpdateLayout(input);
}

// ---------------------------------------------------------------------------
// Pending work
// ---------------------------------------------------------------------------

// Pending work survives only while the stored intent still describes exactly
// the same units under the same world, context and selection.  A newer
// snapshot generation alone is fine; nothing is ever re-bound to new units.
bool SelectionController::PendingStillValid(CancelReason* why) const {
    if (!frame_valid_) {
        *why = CancelReason::InvalidFrame;
        return false;
    }
    if (intent_.session != world_) {
        *why = CancelReason::WorldChanged;
        return false;
    }
    if (intent_.context_generation != context_generation_) {
        *why = CancelReason::ContextChanged;
        return false;
    }
    if (intent_.needs_world_target != ((targeted_actions_ & cp::ActionBit(intent_.action)) != 0)) {
        *why = CancelReason::TargetKindChanged;
        return false;
    }
    switch (panel_.CheckIntent(intent_)) {
        case cp::IntentStatus::Current:
        case cp::IntentStatus::StaleSnapshot:
            break;
        case cp::IntentStatus::StaleSession:
            *why = CancelReason::WorldChanged;
            return false;
        case cp::IntentStatus::StaleContext:
            *why = CancelReason::ContextChanged;
            return false;
        case cp::IntentStatus::StaleEntity:
            *why = CancelReason::UnitStale;
            return false;
        case cp::IntentStatus::NotEligible:
            *why = CancelReason::UnitNotEligible;
            return false;
        default:
            *why = CancelReason::SelectionChanged;
            return false;
    }
    if (intent_.selection_revision != panel_.SelectionRevision()) {
        *why = CancelReason::SelectionChanged;
        return false;
    }
    return true;
}

bool SelectionController::CancelPending(CancelReason why) {
    if (pending_ == Pending::None) return false;
    pending_ = Pending::None;
    last_cancel_ = why;
    return true;
}

void SelectionController::ReconcilePending() {
    if (pending_ != Pending::None &&
        intent_.selection_revision != panel_.SelectionRevision()) {
        CancelPending(CancelReason::SelectionChanged);
    }
}

CancelReason SelectionController::LastCancelReason() const { return last_cancel_; }

// ---------------------------------------------------------------------------
// Picking and selection
// ---------------------------------------------------------------------------

// Nearest centre wins; an exact tie goes to the lowest native id, so the
// result does not depend on the order of rows in the frame.
int SelectionController::PickRow(float x, float y) const {
    int best = -1;
    double best_distance = 0.0;
    for (int i = 0; i < row_count_; ++i) {
        const ScreenCircle& c = circles_[i];
        if (!c.projected || !rows_[i].selectable) continue;
        const double distance = DistanceSquared(x, y, c.x, c.y);
        const double reach = static_cast<double>(c.radius) * static_cast<double>(c.radius);
        if (distance > reach) continue;
        if (best < 0 || distance < best_distance ||
            (distance == best_distance && rows_[i].key.id < rows_[best].key.id)) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

SelectionChange SelectionController::ApplyClick(float x, float y,
                                                const ClickModifiers& modifiers) {
    SelectionChange change{};
    const int row = PickRow(x, y);
    if (row < 0) {
        // Empty world: clear, unless the user is building a selection.
        if (!modifiers.additive) {
            change.result = panel_.Select(cp::SelectOp::Replace, nullptr, 0);
        }
        return change;
    }
    change.picked = true;
    ToNativeKey(rows_[row].key, &change.picked_key);

    // The clicked unit goes first so it is kept even if the group is larger
    // than the selection capacity; the rest follow in ascending id.
    PanelKey keys[kMaxUnits];
    int count = 0;
    keys[count++] = rows_[row].key;
    const cp::SquadDescriptor& squad = rows_[row].squad;
    if (modifiers.whole_squad && ValidSquad(squad)) {
        change.whole_squad = true;
        for (int i = 0; i < row_count_; ++i) {
            if (i == row) continue;
            const cp::SquadDescriptor& other = rows_[i].squad;
            if (other.present && other.squad_id == squad.squad_id &&
                other.squad_generation == squad.squad_generation) {
                keys[count++] = rows_[i].key;
            }
        }
        SortById(keys + 1, count - 1);
    }

    // Additive clicks toggle the whole group by the clicked unit's state.
    cp::SelectOp op = cp::SelectOp::Replace;
    if (modifiers.additive) {
        op = panel_.IsSelected(keys[0]) ? cp::SelectOp::Remove : cp::SelectOp::Add;
    }
    change.result = panel_.Select(op, keys, count);
    return change;
}

// A unit is boxed when its screen circle touches the rectangle, which makes a
// tiny drag over a unit behave like the click it nearly was.
SelectionChange SelectionController::ApplyBox(float x0, float y0, float x1, float y1,
                                              bool additive) {
    SelectionChange change{};
    const double left = x0 < x1 ? x0 : x1;
    const double right = x0 < x1 ? x1 : x0;
    const double top = y0 < y1 ? y0 : y1;
    const double bottom = y0 < y1 ? y1 : y0;

    PanelKey keys[kMaxUnits];
    int count = 0;
    for (int i = 0; i < row_count_; ++i) {
        const ScreenCircle& c = circles_[i];
        if (!c.projected || !rows_[i].selectable) continue;
        const double nearest_x = Clamp(c.x, left, right);
        const double nearest_y = Clamp(c.y, top, bottom);
        const double reach = static_cast<double>(c.radius) * static_cast<double>(c.radius);
        if (DistanceSquared(c.x, c.y, nearest_x, nearest_y) <= reach) {
            keys[count++] = rows_[i].key;
        }
    }
    SortById(keys, count);
    if (count == 0 && additive) return change;
    change.result =
        panel_.Select(additive ? cp::SelectOp::Add : cp::SelectOp::Replace, keys, count);
    return change;
}

SelectionChange SelectionController::SelectAll() {
    SelectionChange change{};
    if (!frame_valid_) return change;
    PanelKey keys[kMaxUnits];
    int count = 0;
    for (int i = 0; i < row_count_; ++i) {
        if (rows_[i].selectable) keys[count++] = rows_[i].key;
    }
    SortById(keys, count);
    change.result = panel_.Select(cp::SelectOp::Replace, keys, count);
    ReconcilePending();
    return change;
}

void SelectionController::ClearSelection() {
    panel_.ClearSelection();
    ReconcilePending();
}

int SelectionController::SelectionCount() const { return panel_.SelectionCount(); }

bool SelectionController::SelectionKey(int index, NativeKey* out) const {
    if (out == nullptr) return false;
    PanelKey key;
    if (!panel_.SelectionKey(index, &key)) {
        *out = NativeKey{};
        return false;
    }
    return ToNativeKey(key, out);
}

bool SelectionController::IsSelected(const NativeKey& key) const {
    PanelKey panel_key;
    if (!ToPanelKey(key, &panel_key)) return false;
    return panel_.IsSelected(panel_key);
}

bool SelectionController::SaveSet(int slot) { return panel_.SaveSelectionSet(slot); }

SelectionChange SelectionController::RecallSet(int slot, bool additive) {
    SelectionChange change{};
    if (!frame_valid_) return change;
    change.result = panel_.RecallSelectionSet(
        slot, additive ? cp::SelectOp::Add : cp::SelectOp::Replace);
    ReconcilePending();
    return change;
}

void SelectionController::ClearSet(int slot) { panel_.ClearSelectionSet(slot); }

int SelectionController::SetCount(int slot) const { return panel_.SelectionSetCount(slot); }

// ---------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------

void SelectionController::SetDragThreshold(float pixels) {
    if (Finite(pixels) && pixels >= 0.0f) drag_threshold_ = pixels;
}

bool SelectionController::PastThreshold(float x, float y) const {
    const double limit = drag_threshold_;
    return DistanceSquared(x, y, press_x_, press_y_) > limit * limit;
}

// A world press dies here and its release is ignored.  A press the panel
// captured stays with the panel, which keeps it consumed until release.
void SelectionController::KillGesture() {
    panel_.CancelPointer();
    if (gesture_ == Gesture::WorldPress || gesture_ == Gesture::WorldDrag) {
        gesture_ = Gesture::Dead;
    }
}

GestureResult SelectionController::PointerDown(float x, float y) {
    GestureResult r = BlankResult();
    if (suppress_) {
        r.outcome = GestureOutcome::Suppressed;
        return r;
    }
    if (!Finite(x) || !Finite(y)) {
        panel_.CancelPointer();
        gesture_ = Gesture::Dead;
        return r;
    }
    last_x_ = x;
    last_y_ = y;

    r.panel = panel_.PointerDown(x, y);
    if (r.panel.owner == cp::PointerOwner::Panel) {
        gesture_ = Gesture::Panel;
        r.outcome = GestureOutcome::PanelOwned;
        return r;
    }
    if (!frame_valid_) {
        gesture_ = Gesture::Dead;
        return r;
    }
    gesture_ = Gesture::WorldPress;
    press_x_ = x;
    press_y_ = y;
    cur_x_ = x;
    cur_y_ = y;
    // A press made while a target is armed is a target click from start to
    // finish; it never falls back to selecting.
    press_for_target_ = pending_ == Pending::Armed;
    press_arm_serial_ = arm_serial_;
    r.outcome = GestureOutcome::WorldPress;
    return r;
}

GestureResult SelectionController::PointerMove(float x, float y) {
    GestureResult r = BlankResult();
    if (!Finite(x) || !Finite(y)) return r;
    last_x_ = x;
    last_y_ = y;

    r.panel = panel_.PointerMove(x, y);
    if (suppress_) {
        r.outcome = GestureOutcome::Suppressed;
        return r;
    }
    if (r.panel.owner == cp::PointerOwner::Panel) r.outcome = GestureOutcome::PanelOwned;

    if (gesture_ == Gesture::WorldPress) {
        cur_x_ = x;
        cur_y_ = y;
        if (PastThreshold(x, y)) {
            gesture_ = press_for_target_ ? Gesture::Dead : Gesture::WorldDrag;
        }
        if (gesture_ == Gesture::WorldPress) {
            r.outcome = GestureOutcome::WorldPress;
        } else if (gesture_ == Gesture::WorldDrag) {
            r.outcome = GestureOutcome::WorldDrag;
        } else {
            r.outcome = GestureOutcome::None;
        }
    } else if (gesture_ == Gesture::WorldDrag) {
        cur_x_ = x;
        cur_y_ = y;
        r.outcome = GestureOutcome::WorldDrag;
    }
    return r;
}

GestureResult SelectionController::PointerUp(float x, float y,
                                             const ClickModifiers& modifiers) {
    GestureResult r = BlankResult();
    const Gesture gesture = gesture_;
    const bool suppressed = suppress_;
    gesture_ = Gesture::Idle;
    suppress_ = false;

    // A release always ends the press, but one without a usable position may
    // not act anywhere: the panel is told to drop its press first.
    const bool finite = Finite(x) && Finite(y);
    if (finite) {
        last_x_ = x;
        last_y_ = y;
    } else {
        panel_.CancelPointer();
        x = last_x_;
        y = last_y_;
    }

    cp::ActionIntent intent;
    std::memset(&intent, 0, sizeof(intent));
    r.panel = panel_.PointerUp(x, y, &intent);
    if (suppressed) {
        r.outcome = GestureOutcome::Suppressed;
        return r;
    }
    if (r.panel.owner == cp::PointerOwner::Panel) r.outcome = GestureOutcome::PanelOwned;
    if (!finite) return r;

    switch (gesture) {
        case Gesture::Panel:
            if (r.panel.owner == cp::PointerOwner::Panel &&
                r.panel.outcome == cp::PointerOutcome::IntentEmitted) {
                r.action = AcceptIntent(intent);
            }
            break;
        case Gesture::WorldPress:
        case Gesture::WorldDrag: {
            if (r.panel.owner != cp::PointerOwner::World || !frame_valid_) break;
            const bool dragged = gesture == Gesture::WorldDrag || PastThreshold(x, y);
            if (press_for_target_) {
                if (!dragged && pending_ == Pending::Armed &&
                    arm_serial_ == press_arm_serial_) {
                    r.outcome = GestureOutcome::TargetPoint;
                    r.target_token = arm_serial_;
                    r.x = x;
                    r.y = y;
                }
                break;
            }
            if (dragged) {
                r.selection = ApplyBox(press_x_, press_y_, x, y, modifiers.additive);
                r.outcome = GestureOutcome::Box;
            } else {
                r.selection = ApplyClick(x, y, modifiers);
                r.outcome = GestureOutcome::Click;
            }
            ReconcilePending();
            break;
        }
        default:
            break;
    }
    return r;
}

void SelectionController::PointerLeft() { panel_.PointerLeft(); }

bool SelectionController::DragRect(cp::Rect* out) const {
    if (gesture_ != Gesture::WorldDrag || out == nullptr) return false;
    const float left = press_x_ < cur_x_ ? press_x_ : cur_x_;
    const float right = press_x_ < cur_x_ ? cur_x_ : press_x_;
    const float top = press_y_ < cur_y_ ? press_y_ : cur_y_;
    const float bottom = press_y_ < cur_y_ ? cur_y_ : press_y_;
    out->x = left;
    out->y = top;
    out->w = right - left;
    out->h = bottom - top;
    return true;
}

void SelectionController::Cancel() {
    CancelPending(CancelReason::HostCancel);
    KillGesture();
}

void SelectionController::SuppressUntilRelease() {
    KillGesture();
    suppress_ = true;
}

// ---------------------------------------------------------------------------
// Actions and requests
// ---------------------------------------------------------------------------

// The single gate for every route into pending work: panel releases and
// host-owned input both end here.
ArmStatus SelectionController::AcceptIntent(const cp::ActionIntent& intent) {
    if (!frame_valid_) return ArmStatus::Unavailable;
    if (pending_ == Pending::Queued) return ArmStatus::QueueFull;
    if (intent.session != world_ || intent.context_generation != context_generation_ ||
        intent.selection_revision != panel_.SelectionRevision()) {
        return ArmStatus::Stale;
    }
    const cp::IntentStatus status = panel_.CheckIntent(intent);
    if (status == cp::IntentStatus::NotEligible) return ArmStatus::NotEligible;
    if (status != cp::IntentStatus::Current) return ArmStatus::Stale;
    for (int i = 0; i < intent.key_count; ++i) {
        if (!ToNativeKey(intent.keys[i], nullptr)) return ArmStatus::Stale;
    }

    // A newer action takes the place of an armed target; a queued request was
    // already refused above and is never overwritten.
    if (arm_serial_ == ~uint64_t(0)) return ArmStatus::Unavailable;
    CancelPending(CancelReason::Replaced);
    intent_ = intent;
    ++arm_serial_;
    pending_ = intent.needs_world_target ? Pending::Armed : Pending::Queued;
    return intent.needs_world_target ? ArmStatus::Armed : ArmStatus::Queued;
}

ArmStatus SelectionController::ArmAction(Action action) {
    if (!frame_valid_) return ArmStatus::Unavailable;
    cp::ActionIntent intent;
    std::memset(&intent, 0, sizeof(intent));
    if (!panel_.BuildIntent(action, &intent)) return ArmStatus::NotEligible;
    return AcceptIntent(intent);
}

bool SelectionController::HasArmedTarget() const { return pending_ == Pending::Armed; }

bool SelectionController::ArmedAction(Action* out) const {
    if (pending_ != Pending::Armed || out == nullptr) return false;
    *out = intent_.action;
    return true;
}

void SelectionController::CancelArmedTarget() {
    if (pending_ == Pending::Armed) CancelPending(CancelReason::HostCancel);
}

bool SelectionController::BuildRequest(Request* out) const {
    if (intent_.key_count == 0 || intent_.key_count > kMaxRequestKeys) return false;
    Request request{};
    request.action = intent_.action;
    request.has_point = false;
    request.world = world_;
    request.snapshot_generation = snapshot_generation_;
    request.context_generation = context_generation_;
    request.selection_revision = intent_.selection_revision;
    request.selected_count = intent_.selected_count;
    request.blocked_count = intent_.blocked_count;
    request.key_count = intent_.key_count;
    for (int i = 0; i < intent_.key_count; ++i) {
        if (!ToNativeKey(intent_.keys[i], &request.keys[i])) return false;
    }
    *out = request;
    return true;
}

uint64_t SelectionController::TargetToken() const {
    return pending_ == Pending::Armed ? arm_serial_ : 0;
}

TargetStatus SelectionController::SubmitTarget(uint64_t target_token, const WorldPoint& point, Request* out) {
    if (pending_ != Pending::Armed) return TargetStatus::NoArmedTarget;
    if (!target_token || target_token != arm_serial_) return TargetStatus::StaleArm;
    if (out == nullptr) return TargetStatus::NoOutput;
    if (!Finite(point.x) || !Finite(point.y) || !Finite(point.z)) {
        return TargetStatus::NonFinitePoint;
    }
    CancelReason why = CancelReason::None;
    if (!PendingStillValid(&why)) {
        CancelPending(why);
        return TargetStatus::Stale;
    }
    Request request{};
    if (!BuildRequest(&request)) {
        CancelPending(CancelReason::UnitStale);
        return TargetStatus::Stale;
    }
    request.has_point = true;
    request.point = point;
    *out = request;
    pending_ = Pending::None;
    return TargetStatus::Submitted;
}

bool SelectionController::HasRequest() const { return pending_ == Pending::Queued; }

bool SelectionController::TakeRequest(Request* out) {
    if (pending_ != Pending::Queued || out == nullptr) return false;
    CancelReason why = CancelReason::None;
    if (!PendingStillValid(&why)) {
        CancelPending(why);
        return false;
    }
    Request request{};
    if (!BuildRequest(&request)) {
        CancelPending(CancelReason::UnitStale);
        return false;
    }
    *out = request;
    pending_ = Pending::None;
    return true;
}

}  // namespace ce_command_selection
