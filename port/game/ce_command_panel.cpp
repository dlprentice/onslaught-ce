// Commander selection/action panel: implementation.
//
// Standard C++17 only.  No engine headers, no rendering API, no key bindings.
// See ce_command_panel.h for the host contract.

#include "ce_command_panel.h"

#include <cmath>
#include <cstring>

namespace ce_command_panel {

namespace {

// ---------------------------------------------------------------------------
// Small bounded text helpers
// ---------------------------------------------------------------------------

struct TextBuf {
    char* data;
    int cap;
    int len;
    TextBuf(char* d, int c) : data(d), cap(c), len(0) {
        if (cap > 0) data[0] = '\0';
    }
};

void Append(TextBuf& b, const char* s) {
    if (b.cap <= 0 || s == nullptr) return;
    while (*s != '\0' && b.len < b.cap - 1) {
        b.data[b.len++] = *s++;
    }
    b.data[b.len] = '\0';
}

void AppendUInt(TextBuf& b, unsigned value) {
    char digits[12];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && n < 11);
    char one[2] = {'\0', '\0'};
    while (n > 0) {
        one[0] = digits[--n];
        Append(b, one);
    }
}

// Copy a host string that may lack a terminator.  Control characters become
// '?' so a damaged name cannot break the single-line layout.
void CopyBounded(char* dst, int dst_cap, const char* src, int src_cap) {
    int n = 0;
    if (dst_cap <= 0) return;
    if (src != nullptr) {
        while (n < dst_cap - 1 && n < src_cap && src[n] != '\0') {
            unsigned char c = static_cast<unsigned char>(src[n]);
            dst[n] = (c < 0x20u || c == 0x7fu) ? '?' : static_cast<char>(c);
            ++n;
        }
    }
    for (int i = n; i < dst_cap; ++i) dst[i] = '\0';
}

void CopyText(char* dst, int dst_cap, const char* src) {
    TextBuf b(dst, dst_cap);
    Append(b, src);
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

Rect MakeRect(float x, float y, float w, float h) {
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

bool IsFinite(float v) { return std::isfinite(v); }

bool RectContains(const Rect& r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

bool RectsIntersect(const Rect& a, const Rect& b) {
    if (a.w <= 0.0f || a.h <= 0.0f || b.w <= 0.0f || b.h <= 0.0f) return false;
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool RectsEqual(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

float RoundPx(float v) { return std::floor(v + 0.5f); }

float MaxF(float a, float b) { return a > b ? a : b; }
float MinF(float a, float b) { return a < b ? a : b; }

// ---------------------------------------------------------------------------
// Eligibility
// ---------------------------------------------------------------------------

enum class Block : uint8_t { None = 0, Unavailable, Human, NoAuthority, Unsupported };

bool IsHuman(ControlOwner c) {
    return c == ControlOwner::LocalHuman || c == ControlOwner::OtherHuman;
}

bool IsAiOrder(Action a) {
    return a == Action::Move || a == Action::Advance || a == Action::Hold ||
           a == Action::FallBack;
}

bool ValidAction(Action a) {
    return static_cast<unsigned>(a) < static_cast<unsigned>(kActionCount);
}

// The host's capability mask is necessary but not sufficient.  These extra
// refusals keep the panel truthful even when a snapshot is over-generous:
// nothing is offered for a dead/unavailable entity, a human-piloted unit is
// never offered an AI order, and AI orders need CommandAI ownership.
Block Classify(const EntitySnapshot& e, Action a) {
    if (e.availability != Availability::Available) return Block::Unavailable;
    if ((IsAiOrder(a) || a == Action::TakeControl) && IsHuman(e.control)) {
        return Block::Human;
    }
    if ((e.capabilities & ActionBit(a)) == 0u) return Block::Unsupported;
    if (IsAiOrder(a) && e.control != ControlOwner::CommandAI) return Block::NoAuthority;
    if (a == Action::ReturnToAI && e.control != ControlOwner::LocalHuman) {
        return Block::NoAuthority;
    }
    return Block::None;
}

uint64_t HashStart() { return 1469598103934665603ull; }

uint64_t HashMix(uint64_t h, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        h ^= static_cast<uint64_t>((v >> (i * 8)) & 0xffu);
        h *= 1099511628211ull;
    }
    return h;
}

EntityKind SaneKind(EntityKind k) {
    return k == EntityKind::Structure ? EntityKind::Structure : EntityKind::Unit;
}

TeamRelation SaneRelation(TeamRelation r) {
    switch (r) {
        case TeamRelation::Friendly:
        case TeamRelation::Neutral:
        case TeamRelation::Hostile:
            return r;
        default:
            return TeamRelation::Unknown;
    }
}

ControlOwner SaneControl(ControlOwner c) {
    switch (c) {
        case ControlOwner::None:
        case ControlOwner::CommandAI:
        case ControlOwner::IndependentAI:
        case ControlOwner::Scripted:
        case ControlOwner::LocalHuman:
        case ControlOwner::OtherHuman:
            return c;
        default:
            return ControlOwner::Unknown;
    }
}

Availability SaneAvailability(Availability a) {
    switch (a) {
        case Availability::Available:
        case Availability::Dead:
            return a;
        default:
            return Availability::Unavailable;
    }
}

QueueOwner SaneQueueOwner(QueueOwner o) {
    switch (o) {
        case QueueOwner::LocalCommander:
        case QueueOwner::Other:
            return o;
        default:
            return QueueOwner::Unknown;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CommandPanel::CommandPanel() { Reset(); }

void CommandPanel::Reset() {
    have_snapshot_ = false;
    session_ = 0;
    snapshot_generation_ = 0;
    context_generation_ = 0;
    targeted_actions_ = 0;
    has_armed_target_action_ = false;
    armed_target_action_ = Action::Move;
    entity_count_ = 0;
    std::memset(entities_, 0, sizeof(entities_));

    selection_count_ = 0;
    std::memset(selection_, 0, sizeof(selection_));
    selection_revision_ = 0;

    std::memset(saved_count_, 0, sizeof(saved_count_));
    std::memset(saved_, 0, sizeof(saved_));

    std::memset(&layout_, 0, sizeof(layout_));
    layout_dirty_ = true;
    layout_generation_ = 0;

    gesture_ = Gesture::Idle;
    consumed_reason_ = PointerOutcome::ConsumedPadding;
    armed_control_ = -1;
    armed_inside_ = false;
    armed_epoch_ = 0;
    epoch_ = 0;
    hover_control_ = -1;
    pointer_known_ = false;
    pointer_x_ = 0.0f;
    pointer_y_ = 0.0f;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

int CommandPanel::FindEntityById(uint32_t id) const {
    for (int i = 0; i < entity_count_; ++i) {
        if (entities_[i].key.id == id) return i;
    }
    return -1;
}

int CommandPanel::FindEntityExact(const EntityKey& key) const {
    if (!have_snapshot_ || key.session != session_) return -1;
    int i = FindEntityById(key.id);
    if (i < 0) return -1;
    return entities_[i].key == key ? i : -1;
}

int CommandPanel::FindSelected(const EntityKey& key) const {
    for (int i = 0; i < selection_count_; ++i) {
        if (selection_[i] == key) return i;
    }
    return -1;
}

bool CommandPanel::InsertSelected(const EntityKey& key) {
    if (selection_count_ >= kMaxSelection) return false;
    int at = selection_count_;
    while (at > 0 && selection_[at - 1].id > key.id) {
        selection_[at] = selection_[at - 1];
        --at;
    }
    selection_[at] = key;
    ++selection_count_;
    return true;
}

void CommandPanel::RemoveSelectedAt(int index) {
    if (index < 0 || index >= selection_count_) return;
    for (int i = index; i + 1 < selection_count_; ++i) {
        selection_[i] = selection_[i + 1];
    }
    --selection_count_;
}

void CommandPanel::SelectionChanged() {
    ++selection_revision_;
    layout_dirty_ = true;
    InvalidateInteraction();
}

// Anything that could make a pressed control mean something else ends the
// press: it stays owned by the panel until release and produces no intent.
void CommandPanel::InvalidateInteraction() {
    ++epoch_;
    if (gesture_ == Gesture::PanelArmed) {
        gesture_ = Gesture::PanelConsumed;
        consumed_reason_ = PointerOutcome::CancelledByChange;
        armed_control_ = -1;
        armed_inside_ = false;
    }
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

SnapshotResult CommandPanel::ApplySnapshot(const WorldSnapshot& snapshot) {
    SnapshotResult r{};
    if (snapshot.entity_count < 0 || snapshot.entity_count > kMaxEntities ||
        (snapshot.entity_count != 0 && snapshot.entities == nullptr)) {
        r.truncated_entities = snapshot.entity_count > kMaxEntities
            ? snapshot.entity_count - kMaxEntities : 0;
        have_snapshot_ = false;
        layout_ = PanelLayout{};
        layout_dirty_ = true;
        InvalidateInteraction();
        return r;
    }
    r.accepted = true;
    r.session_changed = have_snapshot_ && snapshot.session != session_;
    r.context_changed = have_snapshot_ && !r.session_changed &&
                        snapshot.context_generation != context_generation_;

    have_snapshot_ = true;
    session_ = snapshot.session;
    snapshot_generation_ = snapshot.snapshot_generation;
    context_generation_ = snapshot.context_generation;
    targeted_actions_ = snapshot.targeted_actions & kAllActionBits;
    has_armed_target_action_ =
        snapshot.has_armed_target_action && ValidAction(snapshot.armed_target_action);
    armed_target_action_ =
        has_armed_target_action_ ? snapshot.armed_target_action : Action::Move;

    entity_count_ = 0;
    int incoming = (snapshot.entities != nullptr && snapshot.entity_count > 0)
                       ? snapshot.entity_count
                       : 0;
    for (int i = 0; i < incoming; ++i) {
        const EntitySnapshot& src = snapshot.entities[i];
        if (src.key.session != snapshot.session || src.key.id == 0u ||
            FindEntityById(src.key.id) >= 0) {
            ++r.rejected_entities;
            continue;
        }
        if (entity_count_ >= kMaxEntities) {
            ++r.truncated_entities;
            continue;
        }
        EntitySnapshot& dst = entities_[entity_count_];
        std::memset(&dst, 0, sizeof(dst));
        dst.key = src.key;
        dst.kind = SaneKind(src.kind);
        dst.relation = SaneRelation(src.relation);
        dst.control = SaneControl(src.control);
        dst.availability = SaneAvailability(src.availability);
        dst.selectable = src.selectable;
        dst.capabilities = src.capabilities & kAllActionBits;
        dst.squad.present = src.squad.present;
        if (dst.squad.present) {
            dst.squad.squad_id = src.squad.squad_id;
            dst.squad.squad_generation = src.squad.squad_generation;
            dst.squad.has_member_count = src.squad.has_member_count;
            dst.squad.member_count = src.squad.has_member_count ? src.squad.member_count : 0;
            CopyBounded(dst.squad.name, kNameCapacity, src.squad.name, kNameCapacity);
        }
        dst.queue.present = src.queue.present;
        if (dst.queue.present) {
            dst.queue.owner = SaneQueueOwner(src.queue.owner);
            dst.queue.has_progress = src.queue.has_progress;
            dst.queue.progress_permille = src.queue.has_progress
                                              ? (src.queue.progress_permille > 1000
                                                     ? static_cast<uint16_t>(1000)
                                                     : src.queue.progress_permille)
                                              : static_cast<uint16_t>(0);
            dst.queue.has_pending_count = src.queue.has_pending_count;
            dst.queue.pending_count =
                src.queue.has_pending_count ? src.queue.pending_count : 0;
            CopyBounded(dst.queue.item, kNameCapacity, src.queue.item, kNameCapacity);
        }
        CopyBounded(dst.name, kNameCapacity, src.name, kNameCapacity);
        ++entity_count_;
    }
    r.accepted_entities = entity_count_;

    // Prune the selection: keep only keys that match this snapshot exactly.
    int kept = 0;
    for (int i = 0; i < selection_count_; ++i) {
        if (FindEntityExact(selection_[i]) >= 0) {
            selection_[kept++] = selection_[i];
        } else {
            ++r.pruned_selection;
        }
    }
    selection_count_ = kept;

    for (int s = 0; s < kMaxSavedSets; ++s) {
        int kept_saved = 0;
        for (int i = 0; i < saved_count_[s]; ++i) {
            if (FindEntityExact(saved_[s][i]) >= 0) {
                saved_[s][kept_saved++] = saved_[s][i];
            } else {
                ++r.pruned_saved;
            }
        }
        saved_count_[s] = kept_saved;
    }

    layout_dirty_ = true;
    if (r.pruned_selection > 0) ++selection_revision_;
    if (r.session_changed || r.context_changed || r.pruned_selection > 0) {
        InvalidateInteraction();
    }
    return r;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

SelectResult CommandPanel::Select(SelectOp op, const EntityKey* keys, int count) {
    SelectResult r{};
    if (keys == nullptr || count < 0) count = 0;
    int limit = count > kMaxEntities ? kMaxEntities : count;
    r.rejected_capacity += count - limit;

    // Validate against the present snapshot and drop repeats within the call.
    EntityKey valid[kMaxEntities];
    int valid_count = 0;
    for (int i = 0; i < limit; ++i) {
        const EntityKey& k = keys[i];
        int entity = FindEntityExact(k);
        if (entity < 0) {
            ++r.rejected_stale;
            continue;
        }
        bool repeat = false;
        for (int j = 0; j < valid_count; ++j) {
            if (valid[j] == k) {
                repeat = true;
                break;
            }
        }
        if (repeat) continue;
        if (op != SelectOp::Remove && !entities_[entity].selectable && FindSelected(k) < 0) {
            ++r.rejected_unselectable;
            continue;
        }
        valid[valid_count++] = k;
    }

    switch (op) {
        case SelectOp::Replace: {
            EntityKey old_keys[kMaxSelection];
            int old_count = selection_count_;
            for (int i = 0; i < old_count; ++i) old_keys[i] = selection_[i];
            selection_count_ = 0;
            for (int i = 0; i < valid_count; ++i) {
                if (!InsertSelected(valid[i])) ++r.rejected_capacity;
            }
            for (int i = 0; i < selection_count_; ++i) {
                bool was = false;
                for (int j = 0; j < old_count; ++j) {
                    if (old_keys[j] == selection_[i]) {
                        was = true;
                        break;
                    }
                }
                if (!was) ++r.added;
            }
            for (int j = 0; j < old_count; ++j) {
                if (FindSelected(old_keys[j]) < 0) ++r.removed;
            }
            break;
        }
        case SelectOp::Add: {
            for (int i = 0; i < valid_count; ++i) {
                if (FindSelected(valid[i]) >= 0) continue;
                if (InsertSelected(valid[i])) {
                    ++r.added;
                } else {
                    ++r.rejected_capacity;
                }
            }
            break;
        }
        case SelectOp::Toggle: {
            for (int i = 0; i < valid_count; ++i) {
                int at = FindSelected(valid[i]);
                if (at >= 0) {
                    RemoveSelectedAt(at);
                    ++r.removed;
                } else if (InsertSelected(valid[i])) {
                    ++r.added;
                } else {
                    ++r.rejected_capacity;
                }
            }
            break;
        }
        case SelectOp::Remove: {
            for (int i = 0; i < valid_count; ++i) {
                int at = FindSelected(valid[i]);
                if (at >= 0) {
                    RemoveSelectedAt(at);
                    ++r.removed;
                }
            }
            break;
        }
    }

    r.changed = r.added > 0 || r.removed > 0;
    if (r.changed) SelectionChanged();
    return r;
}

void CommandPanel::ClearSelection() {
    if (selection_count_ == 0) return;
    selection_count_ = 0;
    SelectionChanged();
}

int CommandPanel::SelectionCount() const { return selection_count_; }

bool CommandPanel::SelectionKey(int index, EntityKey* out) const {
    if (index < 0 || index >= selection_count_ || out == nullptr) return false;
    *out = selection_[index];
    return true;
}

bool CommandPanel::IsSelected(const EntityKey& key) const { return FindSelected(key) >= 0; }

uint32_t CommandPanel::SelectionRevision() const { return selection_revision_; }

bool CommandPanel::SaveSelectionSet(int slot) {
    if (slot < 0 || slot >= kMaxSavedSets) return false;
    for (int i = 0; i < selection_count_; ++i) saved_[slot][i] = selection_[i];
    saved_count_[slot] = selection_count_;
    return true;
}

SelectResult CommandPanel::RecallSelectionSet(int slot, SelectOp op) {
    SelectResult none{};
    if (slot < 0 || slot >= kMaxSavedSets || saved_count_[slot] == 0) return none;
    return Select(op, saved_[slot], saved_count_[slot]);
}

void CommandPanel::ClearSelectionSet(int slot) {
    if (slot < 0 || slot >= kMaxSavedSets) return;
    saved_count_[slot] = 0;
}

int CommandPanel::SelectionSetCount(int slot) const {
    if (slot < 0 || slot >= kMaxSavedSets) return 0;
    return saved_count_[slot];
}

// ---------------------------------------------------------------------------
// Summaries
// ---------------------------------------------------------------------------

SelectionSummary CommandPanel::DescribeSelection() const {
    SelectionSummary s{};
    uint32_t squad_ids[kMaxSelection];
    uint32_t squad_gens[kMaxSelection];
    int squad_count = 0;
    for (int i = 0; i < selection_count_; ++i) {
        int at = FindEntityExact(selection_[i]);
        if (at < 0) continue;
        const EntitySnapshot& e = entities_[at];
        ++s.selected;
        if (e.kind == EntityKind::Structure) {
            ++s.structures;
        } else {
            ++s.units;
        }
        if (e.availability != Availability::Available) {
            ++s.unavailable;
        } else if (IsHuman(e.control)) {
            ++s.human;
        } else if (e.control == ControlOwner::CommandAI) {
            ++s.commandable;
        } else {
            ++s.no_authority;
        }
        if (e.kind == EntityKind::Unit) {
            if (e.squad.present) {
                bool seen = false;
                for (int j = 0; j < squad_count; ++j) {
                    if (squad_ids[j] == e.squad.squad_id &&
                        squad_gens[j] == e.squad.squad_generation) {
                        seen = true;
                        break;
                    }
                }
                if (!seen && squad_count < kMaxSelection) {
                    squad_ids[squad_count] = e.squad.squad_id;
                    squad_gens[squad_count] = e.squad.squad_generation;
                    ++squad_count;
                }
            } else {
                ++s.without_squad;
            }
        }
    }
    s.squads = static_cast<uint16_t>(squad_count);
    return s;
}

ActionSummary CommandPanel::SummarizeInternal(Action action, EntityKey* eligible_out) const {
    ActionSummary s{};
    s.action = action;
    s.eligible_hash = HashStart();
    if (!have_snapshot_ || !ValidAction(action)) return s;
    for (int i = 0; i < selection_count_; ++i) {
        int at = FindEntityExact(selection_[i]);
        if (at < 0) continue;
        const EntitySnapshot& e = entities_[at];
        ++s.selected;
        if ((e.capabilities & ActionBit(action)) != 0u) s.shown = true;
        switch (Classify(e, action)) {
            case Block::None:
                if (eligible_out != nullptr) eligible_out[s.eligible] = e.key;
                ++s.eligible;
                s.eligible_hash = HashMix(s.eligible_hash, e.key.id);
                s.eligible_hash = HashMix(s.eligible_hash, e.key.generation);
                break;
            case Block::Unavailable:
                ++s.blocked_unavailable;
                break;
            case Block::Human:
                ++s.blocked_human;
                break;
            case Block::NoAuthority:
                ++s.blocked_no_authority;
                break;
            case Block::Unsupported:
                ++s.blocked_unsupported;
                break;
        }
    }
    // One local controller cannot take several actors; never pick an arbitrary survivor.
    if (action == Action::TakeControl && selection_count_ != 1) {
        s.blocked_unsupported += s.eligible;
        s.eligible = 0;
    }
    return s;
}

ActionSummary CommandPanel::Summarize(Action action) const {
    return SummarizeInternal(action, nullptr);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

namespace {

const char* ActionLabel(Action a) {
    switch (a) {
        case Action::Move:
            return "Move";
        case Action::Advance:
            return "Advance";
        case Action::Hold:
            return "Hold";
        case Action::FallBack:
            return "Fall Back";
        case Action::ReturnToAI:
            return "Return to AI";
        case Action::TakeControl:
            return "Take Control";
        case Action::Focus:
            return "Focus";
        case Action::RequestVehicle:
            return "Request Vehicle";
        case Action::CancelRequest:
            return "Cancel Request";
        default:
            return "";
    }
}

struct Measurer {
    MeasureTextFn fn;
    void* user;
    float scale;

    TextSize Measure(const char* text) const {
        TextSize s;
        s.w = 0.0f;
        s.h = 0.0f;
        if (fn != nullptr) s = fn(user, text, scale);
        if (!IsFinite(s.w) || s.w < 0.0f) s.w = 0.0f;
        if (!IsFinite(s.h) || s.h < 0.0f) s.h = 0.0f;
        s.w = std::ceil(s.w);
        s.h = std::ceil(s.h);
        return s;
    }
};

// Shorten `text` in place until its measured width fits `max_w`, marking the
// cut with "..".  Returns the final measured width (0 when nothing fits).
float FitText(const Measurer& m, char* text, int cap, float max_w) {
    float w = m.Measure(text).w;
    if (w <= max_w) return w;
    char original[kTextCapacity];
    CopyText(original, kTextCapacity, text);
    int len = static_cast<int>(std::strlen(original));
    for (int keep = len - 1; keep >= 1; --keep) {
        // Never cut in the middle of a multi-byte character.
        if ((static_cast<unsigned char>(original[keep]) & 0xC0u) == 0x80u) continue;
        char piece[kTextCapacity];
        std::memcpy(piece, original, static_cast<size_t>(keep));
        piece[keep] = '\0';
        int k = keep;
        while (k > 0 && piece[k - 1] == ' ') piece[--k] = '\0';
        if (k == 0) break;
        TextBuf b(text, cap);
        Append(b, piece);
        Append(b, "..");
        w = m.Measure(text).w;
        if (w <= max_w) return w;
    }
    if (cap > 0) text[0] = '\0';
    return 0.0f;
}

void AppendCount(TextBuf& b, unsigned n, const char* singular, const char* plural) {
    AppendUInt(b, n);
    Append(b, " ");
    Append(b, n == 1u ? singular : plural);
}

// Append ", " before every part except the first.
void AppendPart(TextBuf& b, bool* first) {
    if (!*first) Append(b, ", ");
    *first = false;
}

bool InteractionDiffers(const PanelLayout& a, const PanelLayout& b) {
    if (a.visible != b.visible) return true;
    if (a.session != b.session) return true;
    if (a.context_generation != b.context_generation) return true;
    if (a.selection_revision != b.selection_revision) return true;
    if (!RectsEqual(a.panel, b.panel)) return true;
    if (a.control_count != b.control_count) return true;
    for (int i = 0; i < a.control_count; ++i) {
        const Control& ca = a.controls[i];
        const Control& cb = b.controls[i];
        if (ca.action != cb.action || ca.enabled != cb.enabled ||
            ca.selected != cb.selected || ca.eligible != cb.eligible ||
            ca.blocked != cb.blocked || ca.eligible_hash != cb.eligible_hash ||
            !RectsEqual(ca.rect, cb.rect)) {
            return true;
        }
    }
    return false;
}

struct ControlDraft {
    Control control;
    int row;
    float x;
    float text_w;
    float detail_w;
};

}  // namespace

void CommandPanel::BuildLayout(const LayoutInput& input, PanelLayout* out) const {
    PanelLayout& L = *out;
    std::memset(&L, 0, sizeof(L));
    L.session = session_;
    L.snapshot_generation = snapshot_generation_;
    L.context_generation = context_generation_;
    L.selection_revision = selection_revision_;
    const float scale = (IsFinite(input.scale) && input.scale > 0.0f) ? input.scale : 1.0f;
    L.scale = scale;

    // Whole-pixel safe area, then cut away each host reservation, keeping the
    // largest remaining side each time.
    Rect avail = MakeRect(0.0f, 0.0f, 0.0f, 0.0f);
    if (IsFinite(input.safe.x) && IsFinite(input.safe.y) && IsFinite(input.safe.w) &&
        IsFinite(input.safe.h)) {
        float x0 = std::ceil(input.safe.x);
        float y0 = std::ceil(input.safe.y);
        float x1 = std::floor(input.safe.x + input.safe.w);
        float y1 = std::floor(input.safe.y + input.safe.h);
        avail = MakeRect(x0, y0, MaxF(0.0f, x1 - x0), MaxF(0.0f, y1 - y0));
    }
    int reservations = input.reservation_count;
    if (reservations < 0) reservations = 0;
    if (reservations > kMaxReservations) reservations = kMaxReservations;
    for (int i = 0; i < reservations; ++i) {
        const Rect& r = input.reservations[i];
        if (!IsFinite(r.x) || !IsFinite(r.y) || !IsFinite(r.w) || !IsFinite(r.h)) continue;
        float rx0 = std::floor(r.x);
        float ry0 = std::floor(r.y);
        float rx1 = std::ceil(r.x + r.w);
        float ry1 = std::ceil(r.y + r.h);
        Rect whole = MakeRect(rx0, ry0, rx1 - rx0, ry1 - ry0);
        if (!RectsIntersect(avail, whole)) continue;
        const float ax1 = avail.x + avail.w;
        const float ay1 = avail.y + avail.h;
        Rect options[4];
        options[0] = MakeRect(avail.x, ry1, avail.w, MaxF(0.0f, ay1 - ry1));        // below
        options[1] = MakeRect(avail.x, avail.y, avail.w, MaxF(0.0f, ry0 - avail.y)); // above
        options[2] = MakeRect(avail.x, avail.y, MaxF(0.0f, rx0 - avail.x), avail.h); // left
        options[3] = MakeRect(rx1, avail.y, MaxF(0.0f, ax1 - rx1), avail.h);        // right
        int best = 0;
        float best_area = -1.0f;
        for (int o = 0; o < 4; ++o) {
            float area = options[o].w * options[o].h;
            if (area > best_area) {
                best_area = area;
                best = o;
            }
        }
        avail = options[best];
    }
    L.available = avail;

    if (!have_snapshot_ || selection_count_ == 0) return;
    if (input.measure == nullptr || avail.w <= 0.0f || avail.h <= 0.0f) {
        L.no_room = true;
        return;
    }

    Measurer m;
    m.fn = input.measure;
    m.user = input.measure_user;
    m.scale = scale;

    const float line_h = MaxF(1.0f, m.Measure("Ag").h);
    const float pad = MaxF(2.0f, RoundPx(6.0f * scale));
    const float gap = MaxF(1.0f, RoundPx(4.0f * scale));
    const float line_gap = MaxF(0.0f, RoundPx(2.0f * scale));
    const float bpx = MaxF(2.0f, RoundPx(7.0f * scale));
    const float bpy = MaxF(1.0f, RoundPx(3.0f * scale));
    const float stripe_w = MaxF(2.0f, RoundPx(3.0f * scale));
    const float sep_h = MaxF(1.0f, RoundPx(scale));
    const float btn_h = line_h + 2.0f * bpy;

    // The panel stays compact: at most half the available width, but never
    // forced narrower than a readable minimum when the space exists.
    const float cap_w = MaxF(RoundPx(avail.w * 0.5f), RoundPx(352.0f * scale));
    const float max_w = MinF(avail.w, cap_w);
    const float inner_max = max_w - stripe_w - 2.0f * pad;
    if (inner_max < 2.0f * line_h) {
        L.no_room = true;
        return;
    }

    // ---- Text lines -------------------------------------------------------
    char text[kMaxLines][kTextCapacity];
    LineRole role[kMaxLines];
    int rank[kMaxLines];      // 0 = required; larger ranks are dropped first
    float width[kMaxLines];
    bool present[kMaxLines];
    int line_total = 0;
    for (int i = 0; i < kMaxLines; ++i) {
        text[i][0] = '\0';
        role[i] = LineRole::Title;
        rank[i] = 0;
        width[i] = 0.0f;
        present[i] = false;
    }

    const SelectionSummary sel = DescribeSelection();
    IdentityStyle identity = IdentityStyle::None;

    if (selection_count_ == 1) {
        int at = FindEntityExact(selection_[0]);
        if (at < 0) return;
        const EntitySnapshot& e = entities_[at];
        const bool is_structure = e.kind == EntityKind::Structure;

        {   // Title: the host's display name.
            TextBuf b(text[line_total], kTextCapacity);
            if (e.name[0] != '\0') {
                Append(b, e.name);
            } else {
                Append(b, is_structure ? "Unnamed structure" : "Unnamed unit");
            }
            role[line_total] = LineRole::Title;
            rank[line_total] = 0;
            ++line_total;
        }
        {   // Kind and team relation (descriptive only).
            TextBuf b(text[line_total], kTextCapacity);
            switch (e.relation) {
                case TeamRelation::Friendly:
                    Append(b, is_structure ? "Friendly structure" : "Friendly unit");
                    break;
                case TeamRelation::Neutral:
                    Append(b, is_structure ? "Neutral structure" : "Neutral unit");
                    break;
                case TeamRelation::Hostile:
                    Append(b, is_structure ? "Hostile structure" : "Hostile unit");
                    break;
                default:
                    Append(b, is_structure ? "Structure, side unknown" : "Unit, side unknown");
                    break;
            }
            role[line_total] = LineRole::Kind;
            rank[line_total] = 3;
            ++line_total;
        }
        {   // Owner / state: who drives it, or why it cannot act.
            TextBuf b(text[line_total], kTextCapacity);
            if (e.availability == Availability::Dead) {
                Append(b, "Destroyed");
                identity = IdentityStyle::Unavailable;
            } else if (e.availability != Availability::Available) {
                Append(b, "Unavailable");
                identity = IdentityStyle::Unavailable;
            } else {
                switch (e.control) {
                    case ControlOwner::CommandAI:
                        Append(b, "AI - your orders");
                        identity = IdentityStyle::Command;
                        break;
                    case ControlOwner::IndependentAI:
                        Append(b, "AI - not yours");
                        identity = IdentityStyle::NoAuthority;
                        break;
                    case ControlOwner::Scripted:
                        Append(b, "Scripted - no orders");
                        identity = IdentityStyle::NoAuthority;
                        break;
                    case ControlOwner::LocalHuman:
                        Append(b, "You are piloting");
                        identity = IdentityStyle::HumanLocal;
                        break;
                    case ControlOwner::OtherHuman:
                        Append(b, "Human pilot");
                        identity = IdentityStyle::HumanOther;
                        break;
                    case ControlOwner::None:
                        Append(b, "No controller");
                        identity = is_structure ? IdentityStyle::Structure
                                                : IdentityStyle::NoAuthority;
                        break;
                    default:
                        Append(b, "Control unknown");
                        identity = IdentityStyle::NoAuthority;
                        break;
                }
            }
            role[line_total] = LineRole::Owner;
            rank[line_total] = 0;
            ++line_total;
        }
        if (!is_structure && e.squad.present) {
            TextBuf b(text[line_total], kTextCapacity);
            Append(b, "Squad");
            if (e.squad.name[0] != '\0') {
                Append(b, ": ");
                Append(b, e.squad.name);
            }
            if (e.squad.has_member_count) {
                Append(b, " (1 of ");
                AppendUInt(b, e.squad.member_count);
                Append(b, ")");
            }
            role[line_total] = LineRole::Squad;
            rank[line_total] = 5;
            ++line_total;
        }
        if (e.queue.present) {
            // Only what the host explicitly reported; nothing is inferred.
            TextBuf b(text[line_total], kTextCapacity);
            if (e.queue.item[0] != '\0') {
                Append(b, "Request: ");
                Append(b, e.queue.item);
            } else {
                Append(b, "Request active");
            }
            role[line_total] = LineRole::Queue;
            rank[line_total] = 2;
            ++line_total;

            if (e.queue.owner != QueueOwner::Unknown || e.queue.has_progress ||
                e.queue.has_pending_count) {
                TextBuf d(text[line_total], kTextCapacity);
                bool first = true;
                if (e.queue.owner == QueueOwner::LocalCommander) {
                    AppendPart(d, &first);
                    Append(d, "Yours");
                } else if (e.queue.owner == QueueOwner::Other) {
                    AppendPart(d, &first);
                    Append(d, "Not yours");
                }
                if (e.queue.has_progress) {
                    AppendPart(d, &first);
                    AppendUInt(d, static_cast<unsigned>(e.queue.progress_permille) / 10u);
                    Append(d, "%");
                }
                if (e.queue.has_pending_count) {
                    AppendPart(d, &first);
                    AppendUInt(d, e.queue.pending_count);
                    Append(d, " waiting");
                }
                role[line_total] = LineRole::Queue;
                rank[line_total] = 4;
                ++line_total;
            }
        }
    } else {
        {   // Title: what the selection is made of.
            TextBuf b(text[line_total], kTextCapacity);
            if (sel.structures == 0) {
                AppendCount(b, sel.units, "unit", "units");
            } else if (sel.units == 0) {
                AppendCount(b, sel.structures, "structure", "structures");
            } else {
                AppendCount(b, sel.units, "unit", "units");
                Append(b, " + ");
                AppendCount(b, sel.structures, "structure", "structures");
            }
            role[line_total] = LineRole::Title;
            rank[line_total] = 0;
            ++line_total;
        }
        {   // Composition by who drives them.
            TextBuf b(text[line_total], kTextCapacity);
            bool first = true;
            if (sel.commandable > 0) {
                AppendPart(b, &first);
                AppendUInt(b, sel.commandable);
                Append(b, " yours");
            }
            if (sel.human > 0) {
                AppendPart(b, &first);
                AppendUInt(b, sel.human);
                Append(b, " piloted");
            }
            if (sel.no_authority > 0) {
                AppendPart(b, &first);
                AppendUInt(b, sel.no_authority);
                Append(b, " other");
            }
            if (sel.unavailable > 0) {
                AppendPart(b, &first);
                AppendUInt(b, sel.unavailable);
                Append(b, " n/a");
            }
            role[line_total] = LineRole::Owner;
            rank[line_total] = 0;
            ++line_total;
        }
        if (sel.squads > 0) {
            // The existing simulation squads, as distinct from this selection.
            TextBuf b(text[line_total], kTextCapacity);
            if (sel.squads == 1 && sel.without_squad == 0) {
                const SquadDescriptor* squad = nullptr;
                for (int i = 0; i < selection_count_ && squad == nullptr; ++i) {
                    int at = FindEntityExact(selection_[i]);
                    if (at >= 0 && entities_[at].kind == EntityKind::Unit &&
                        entities_[at].squad.present) {
                        squad = &entities_[at].squad;
                    }
                }
                Append(b, "Squad");
                if (squad != nullptr && squad->name[0] != '\0') {
                    Append(b, ": ");
                    Append(b, squad->name);
                }
                if (squad != nullptr && squad->has_member_count) {
                    if (sel.units == squad->member_count) {
                        Append(b, " (all ");
                        AppendUInt(b, squad->member_count);
                        Append(b, ")");
                    } else {
                        Append(b, " (");
                        AppendUInt(b, sel.units);
                        Append(b, " of ");
                        AppendUInt(b, squad->member_count);
                        Append(b, ")");
                    }
                } else {
                    Append(b, " (");
                    AppendUInt(b, sel.units);
                    Append(b, " selected)");
                }
            } else {
                AppendCount(b, sel.squads, "squad", "squads");
                if (sel.without_squad > 0) {
                    Append(b, ", ");
                    AppendUInt(b, sel.without_squad);
                    Append(b, " without squad");
                }
            }
            role[line_total] = LineRole::Squad;
            rank[line_total] = 5;
            ++line_total;
        }
        if (sel.human > 0) {
            identity = IdentityStyle::MixedWithHuman;
        } else if (sel.commandable == sel.selected) {
            identity = IdentityStyle::Command;
        } else if (sel.unavailable == sel.selected) {
            identity = IdentityStyle::Unavailable;
        } else {
            identity = IdentityStyle::Mixed;
        }
    }

    for (int i = 0; i < line_total; ++i) {
        width[i] = FitText(m, text[i], kTextCapacity, inner_max);
        present[i] = text[i][0] != '\0';
    }

    // ---- Controls: only actions some selected entity reports ---------------
    ControlDraft drafts[kActionCount];
    int draft_count = 0;
    int rows = 0;
    float row_x = 0.0f;
    bool any_fraction = false;
    for (int a = 0; a < kActionCount; ++a) {
        const Action action = static_cast<Action>(a);
        const ActionSummary as = SummarizeInternal(action, nullptr);
        if (!as.shown) continue;
        ControlDraft& d = drafts[draft_count];
        std::memset(&d, 0, sizeof(d));
        Control& c = d.control;
        c.action = action;
        c.enabled = as.eligible > 0;
        c.needs_target = (targeted_actions_ & ActionBit(action)) != 0u;
        c.host_armed = has_armed_target_action_ && armed_target_action_ == action;
        c.selected = as.selected;
        c.eligible = as.eligible;
        c.blocked = static_cast<uint16_t>(as.selected - as.eligible);
        c.eligible_hash = as.eligible_hash;

        const bool choose_one = action == Action::TakeControl && as.selected != 1;
        const bool fraction = as.selected > 1 && c.blocked > 0 && !choose_one;
        {
            TextBuf b(c.label, kNameCapacity);
            Append(b, ActionLabel(action));
            if (fraction) {
                Append(b, " ");
                AppendUInt(b, as.eligible);
                Append(b, "/");
                AppendUInt(b, as.selected);
            }
        }
        if (choose_one) {
            TextBuf b(c.detail, kTextCapacity);
            Append(b, "Select one unit to take control");
            d.detail_w = FitText(m, c.detail, kTextCapacity, inner_max);
        } else if (fraction) {
            any_fraction = true;
            // The label already carries "eligible/selected"; the detail says
            // who is left out, using the composition line's own words.
            TextBuf b(c.detail, kTextCapacity);
            Append(b, "Skips ");
            bool first = true;
            if (as.blocked_human > 0) {
                AppendPart(b, &first);
                AppendUInt(b, as.blocked_human);
                Append(b, " piloted");
            }
            if (as.blocked_no_authority > 0) {
                AppendPart(b, &first);
                AppendUInt(b, as.blocked_no_authority);
                Append(b, " other");
            }
            if (as.blocked_unavailable > 0) {
                AppendPart(b, &first);
                AppendUInt(b, as.blocked_unavailable);
                Append(b, " n/a");
            }
            if (as.blocked_unsupported > 0) {
                AppendPart(b, &first);
                AppendUInt(b, as.blocked_unsupported);
                Append(b, " unable");
            }
            d.detail_w = FitText(m, c.detail, kTextCapacity, inner_max);
        }

        d.text_w = FitText(m, c.label, kNameCapacity, inner_max - 2.0f * bpx);
        const float bw = d.text_w + 2.0f * bpx;
        if (row_x > 0.0f && row_x + gap + bw > inner_max) {
            ++rows;
            row_x = 0.0f;
        }
        d.row = rows;
        d.x = row_x > 0.0f ? row_x + gap : 0.0f;
        row_x = d.x + bw;
        ++draft_count;
    }
    if (draft_count > 0) ++rows;

    char legend[kTextCapacity];
    legend[0] = '\0';
    float legend_w = 0.0f;
    if (any_fraction) {
        CopyText(legend, kTextCapacity, "Counts: ready / selected");
        legend_w = FitText(m, legend, kTextCapacity, inner_max);
    }

    // ---- Fit the height, dropping optional parts before anything else ------
    int rows_kept = rows;
    bool detail = any_fraction;
    bool clipped = false;
    for (;;) {
        int nl = 0;
        for (int i = 0; i < line_total; ++i) {
            if (present[i]) ++nl;
        }
        float h = pad + pad;
        if (nl > 0) h += nl * line_h + (nl - 1) * line_gap;
        if (rows_kept > 0) {
            h += gap + sep_h + gap + rows_kept * btn_h + (rows_kept - 1) * gap;
            if (detail) h += gap + line_h;
        }
        if (h <= avail.h) break;

        int drop = -1;
        for (int i = 0; i < line_total; ++i) {
            if (present[i] && rank[i] > 0 && (drop < 0 || rank[i] > rank[drop])) drop = i;
        }
        clipped = true;
        if (drop >= 0) {
            present[drop] = false;
        } else if (detail) {
            detail = false;
        } else if (rows_kept > 0) {
            --rows_kept;
        } else {
            L.no_room = true;
            return;
        }
    }

    // ---- Final sizes ------------------------------------------------------
    float content_w = 0.0f;
    int nl = 0;
    for (int i = 0; i < line_total; ++i) {
        if (!present[i]) continue;
        ++nl;
        content_w = MaxF(content_w, width[i]);
    }
    for (int i = 0; i < draft_count; ++i) {
        if (drafts[i].row >= rows_kept) continue;
        content_w = MaxF(content_w, drafts[i].x + drafts[i].text_w + 2.0f * bpx);
        if (detail) content_w = MaxF(content_w, drafts[i].detail_w);
    }
    if (detail && rows_kept > 0) content_w = MaxF(content_w, legend_w);

    float panel_h = pad + pad;
    if (nl > 0) panel_h += nl * line_h + (nl - 1) * line_gap;
    if (rows_kept > 0) {
        panel_h += gap + sep_h + gap + rows_kept * btn_h + (rows_kept - 1) * gap;
        if (detail) panel_h += gap + line_h;
    }
    const float panel_w = content_w + stripe_w + 2.0f * pad;

    // Bottom-left of the available area, leaving the world view open.
    L.panel = MakeRect(avail.x, avail.y + avail.h - panel_h, panel_w, panel_h);
    L.stripe = MakeRect(L.panel.x, L.panel.y, stripe_w, panel_h);
    L.identity = identity;
    L.clipped = clipped;

    const float inner_x = L.panel.x + stripe_w + pad;
    float y = L.panel.y + pad;
    for (int i = 0; i < line_total; ++i) {
        if (!present[i]) continue;
        TextLine& line = L.lines[L.line_count];
        line.role = role[i];
        line.bounds = MakeRect(inner_x, y, width[i], line_h);
        CopyText(line.text, kTextCapacity, text[i]);
        ++L.line_count;
        y += line_h + line_gap;
    }
    if (nl > 0) y -= line_gap;

    if (rows_kept > 0) {
        y += gap;
        L.has_separator = true;
        L.separator = MakeRect(inner_x, y, content_w, sep_h);
        y += sep_h + gap;
        for (int i = 0; i < draft_count; ++i) {
            const ControlDraft& d = drafts[i];
            if (d.row >= rows_kept) {
                ++L.hidden_controls;
                continue;
            }
            Control& c = L.controls[L.control_count];
            c = d.control;
            c.rect = MakeRect(inner_x + d.x, y + d.row * (btn_h + gap),
                              d.text_w + 2.0f * bpx, btn_h);
            c.text_bounds = MakeRect(c.rect.x + bpx, c.rect.y + bpy, d.text_w, line_h);
            ++L.control_count;
        }
        y += rows_kept * btn_h + (rows_kept - 1) * gap;
        if (detail) {
            y += gap;
            L.has_detail_row = true;
            L.detail_row = MakeRect(inner_x, y, content_w, line_h);
            CopyText(L.detail_legend, kTextCapacity, legend);
        }
    } else {
        L.hidden_controls = draft_count;
    }
    L.visible = true;
}

bool CommandPanel::UpdateLayout(const LayoutInput& input) {
    PanelLayout next;
    BuildLayout(input, &next);
    const bool differs = InteractionDiffers(layout_, next);
    if (differs) {
        ++layout_generation_;
        InvalidateInteraction();
    }
    next.generation = layout_generation_;
    layout_ = next;
    layout_dirty_ = false;
    RefreshHover();
    return differs;
}

const PanelLayout& CommandPanel::Layout() const { return layout_; }

bool CommandPanel::LayoutIsCurrent() const { return !layout_dirty_; }

// ---------------------------------------------------------------------------
// Pointer interaction
// ---------------------------------------------------------------------------

int CommandPanel::ControlAt(float x, float y) const {
    if (!layout_.visible) return -1;
    for (int i = 0; i < layout_.control_count; ++i) {
        if (RectContains(layout_.controls[i].rect, x, y)) return i;
    }
    return -1;
}

void CommandPanel::RefreshHover() {
    hover_control_ = -1;
    if (!pointer_known_) return;
    if (gesture_ == Gesture::Idle) {
        hover_control_ = ControlAt(pointer_x_, pointer_y_);
    } else if (gesture_ == Gesture::PanelArmed) {
        armed_inside_ = armed_control_ >= 0 && armed_control_ < layout_.control_count &&
                        RectContains(layout_.controls[armed_control_].rect, pointer_x_,
                                     pointer_y_);
    }
}

HitKind CommandPanel::HitTest(float x, float y) const {
    if (!layout_.visible || !RectContains(layout_.panel, x, y)) return HitKind::None;
    int index = ControlAt(x, y);
    if (index < 0) return HitKind::Padding;
    return layout_.controls[index].enabled ? HitKind::Control : HitKind::DisabledControl;
}

PointerResult CommandPanel::PointerDown(float x, float y) {
    pointer_known_ = true;
    pointer_x_ = x;
    pointer_y_ = y;
    // A press always starts a fresh gesture; an unmatched older one is dropped.
    gesture_ = Gesture::Idle;
    armed_control_ = -1;
    armed_inside_ = false;
    hover_control_ = -1;

    PointerResult r;
    const HitKind hit = HitTest(x, y);
    if (hit == HitKind::None) {
        gesture_ = Gesture::World;
        r.owner = PointerOwner::World;
        r.outcome = PointerOutcome::World;
        return r;
    }

    r.owner = PointerOwner::Panel;
    if (layout_dirty_) {
        gesture_ = Gesture::PanelConsumed;
        consumed_reason_ = PointerOutcome::ConsumedStale;
    } else if (hit == HitKind::Control) {
        gesture_ = Gesture::PanelArmed;
        armed_control_ = ControlAt(x, y);
        armed_inside_ = true;
        armed_epoch_ = epoch_;
        r.outcome = PointerOutcome::Armed;
        return r;
    } else if (hit == HitKind::DisabledControl) {
        gesture_ = Gesture::PanelConsumed;
        consumed_reason_ = PointerOutcome::ConsumedDisabled;
    } else {
        gesture_ = Gesture::PanelConsumed;
        consumed_reason_ = PointerOutcome::ConsumedPadding;
    }
    r.outcome = consumed_reason_;
    return r;
}

PointerResult CommandPanel::PointerMove(float x, float y) {
    pointer_known_ = true;
    pointer_x_ = x;
    pointer_y_ = y;

    PointerResult r;
    r.owner = PointerOwner::World;
    r.outcome = PointerOutcome::World;
    switch (gesture_) {
        case Gesture::World:
            hover_control_ = -1;
            break;
        case Gesture::PanelArmed:
            RefreshHover();
            r.owner = PointerOwner::Panel;
            r.outcome = PointerOutcome::Armed;
            break;
        case Gesture::PanelConsumed:
            hover_control_ = -1;
            r.owner = PointerOwner::Panel;
            r.outcome = consumed_reason_;
            break;
        case Gesture::Idle:
            RefreshHover();
            if (HitTest(x, y) != HitKind::None) {
                r.owner = PointerOwner::Panel;
                r.outcome = PointerOutcome::PanelIdle;
            }
            break;
    }
    return r;
}

PointerResult CommandPanel::PointerUp(float x, float y, ActionIntent* intent) {
    pointer_known_ = true;
    pointer_x_ = x;
    pointer_y_ = y;

    const Gesture gesture = gesture_;
    const int armed = armed_control_;
    const uint32_t armed_epoch = armed_epoch_;
    gesture_ = Gesture::Idle;
    armed_control_ = -1;
    armed_inside_ = false;

    PointerResult r;
    r.owner = PointerOwner::World;
    r.outcome = PointerOutcome::World;
    switch (gesture) {
        case Gesture::Idle:
        case Gesture::World:
            // Not a press the panel captured: the release belongs to the world
            // even when it lands on the panel.
            break;
        case Gesture::PanelConsumed:
            r.owner = PointerOwner::Panel;
            r.outcome = consumed_reason_;
            break;
        case Gesture::PanelArmed: {
            r.owner = PointerOwner::Panel;
            r.outcome = PointerOutcome::CancelledByChange;
            if (layout_dirty_ || armed_epoch != epoch_ || armed < 0 ||
                armed >= layout_.control_count) {
                break;
            }
            const Control& c = layout_.controls[armed];
            if (!RectContains(c.rect, x, y)) {
                r.outcome = PointerOutcome::ReleasedOffControl;
                break;
            }
            if (intent == nullptr || !c.enabled) break;
            // The control must still describe exactly the same eligible set.
            const ActionSummary now = SummarizeInternal(c.action, nullptr);
            if (now.eligible != c.eligible || now.eligible_hash != c.eligible_hash) break;
            ActionIntent built;
            if (!BuildIntent(c.action, &built)) break;
            *intent = built;
            r.outcome = PointerOutcome::IntentEmitted;
            break;
        }
    }
    RefreshHover();
    return r;
}

void CommandPanel::CancelPointer() {
    if (gesture_ == Gesture::PanelArmed) {
        gesture_ = Gesture::PanelConsumed;
        consumed_reason_ = PointerOutcome::CancelledByChange;
    } else if (gesture_ == Gesture::World) {
        gesture_ = Gesture::Idle;
    }
    armed_control_ = -1;
    armed_inside_ = false;
    hover_control_ = -1;
}

void CommandPanel::PointerLeft() {
    pointer_known_ = false;
    hover_control_ = -1;
    armed_inside_ = false;
}

// ---------------------------------------------------------------------------
// Intents
// ---------------------------------------------------------------------------

bool CommandPanel::BuildIntent(Action action, ActionIntent* intent) const {
    if (intent == nullptr || !have_snapshot_ || !ValidAction(action)) return false;
    ActionIntent out;
    std::memset(&out, 0, sizeof(out));
    const ActionSummary s = SummarizeInternal(action, out.keys);
    if (s.eligible == 0) return false;
    out.action = action;
    out.needs_world_target = (targeted_actions_ & ActionBit(action)) != 0u;
    out.session = session_;
    out.snapshot_generation = snapshot_generation_;
    out.context_generation = context_generation_;
    out.layout_generation = layout_generation_;
    out.selection_revision = selection_revision_;
    out.selected_count = s.selected;
    out.blocked_count = static_cast<uint16_t>(s.selected - s.eligible);
    out.key_count = s.eligible;
    *intent = out;
    return true;
}

IntentStatus CommandPanel::CheckIntent(const ActionIntent& intent) const {
    if (!ValidAction(intent.action) || intent.key_count == 0 ||
        intent.key_count > kMaxSelection) {
        return IntentStatus::Malformed;
    }
    if (intent.action == Action::TakeControl &&
        (intent.key_count != 1 || intent.selected_count != 1 || selection_count_ != 1)) {
        return IntentStatus::Malformed;
    }
    if (!have_snapshot_ || intent.session != session_) return IntentStatus::StaleSession;
    if (intent.context_generation != context_generation_) return IntentStatus::StaleContext;
    bool not_eligible = false;
    for (int i = 0; i < intent.key_count; ++i) {
        int at = FindEntityExact(intent.keys[i]);
        if (at < 0) return IntentStatus::StaleEntity;
        if (Classify(entities_[at], intent.action) != Block::None) not_eligible = true;
    }
    if (not_eligible) return IntentStatus::NotEligible;
    if (intent.snapshot_generation != snapshot_generation_) return IntentStatus::StaleSnapshot;
    return IntentStatus::Current;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

namespace {

void Fill(const DrawCallbacks& draw, const Rect& r, uint32_t color) {
    if (draw.fill_rect != nullptr && r.w > 0.0f && r.h > 0.0f) {
        draw.fill_rect(draw.user, r, color);
    }
}

void Segment(const DrawCallbacks& draw, float x0, float y0, float x1, float y1,
             uint32_t color) {
    if (draw.line != nullptr) draw.line(draw.user, x0, y0, x1, y1, color);
}

// Outline drawn on the rectangle's own edge pixels, never outside it.
void Outline(const DrawCallbacks& draw, const Rect& r, uint32_t color) {
    if (r.w < 1.0f || r.h < 1.0f) return;
    const float x0 = r.x;
    const float y0 = r.y;
    const float x1 = r.x + r.w - 1.0f;
    const float y1 = r.y + r.h - 1.0f;
    Segment(draw, x0, y0, x1, y0, color);
    Segment(draw, x0, y1, x1, y1, color);
    Segment(draw, x0, y0, x0, y1, color);
    Segment(draw, x1, y0, x1, y1, color);
}

void Text(const DrawCallbacks& draw, const Rect& bounds, const char* text, float scale,
          uint32_t color) {
    if (draw.text != nullptr && text != nullptr && text[0] != '\0') {
        draw.text(draw.user, bounds.x, bounds.y, text, scale, color);
    }
}

bool HumanIdentity(IdentityStyle s) {
    return s == IdentityStyle::HumanLocal || s == IdentityStyle::HumanOther ||
           s == IdentityStyle::MixedWithHuman;
}

}  // namespace

void CommandPanel::Paint(const DrawCallbacks& draw, const Palette& palette) const {
    const PanelLayout& L = layout_;
    if (!L.visible) return;

    Fill(draw, L.panel, palette.background);
    Outline(draw, L.panel, palette.border);

    // Identity stripe.  Human-piloted identities use their own colour and
    // shape so they can never be mistaken for a commandable AI unit.
    switch (L.identity) {
        case IdentityStyle::Command:
            Fill(draw, L.stripe, palette.accent);
            break;
        case IdentityStyle::HumanLocal:
            Fill(draw, L.stripe, palette.human);
            break;
        case IdentityStyle::HumanOther:
            Fill(draw, L.stripe, palette.background);
            Outline(draw, L.stripe, palette.human);
            break;
        case IdentityStyle::MixedWithHuman: {
            const float half = std::floor(L.stripe.h * 0.5f);
            Fill(draw, MakeRect(L.stripe.x, L.stripe.y, L.stripe.w, half), palette.human);
            Fill(draw, MakeRect(L.stripe.x, L.stripe.y + half, L.stripe.w, L.stripe.h - half),
                 palette.muted);
            break;
        }
        case IdentityStyle::Structure:
            Fill(draw, L.stripe, palette.structure);
            break;
        case IdentityStyle::Unavailable:
            Fill(draw, L.stripe, palette.warning);
            break;
        case IdentityStyle::NoAuthority:
        case IdentityStyle::Mixed:
            Fill(draw, L.stripe, palette.muted);
            break;
        default:
            Fill(draw, L.stripe, palette.border);
            break;
    }

    for (int i = 0; i < L.line_count; ++i) {
        const TextLine& line = L.lines[i];
        uint32_t color = palette.text_dim;
        if (line.role == LineRole::Title) {
            color = palette.text;
        } else if (line.role == LineRole::Owner) {
            if (HumanIdentity(L.identity)) {
                color = palette.human;
            } else if (L.identity == IdentityStyle::Unavailable) {
                color = palette.warning;
            } else {
                color = palette.text;
            }
        }
        Text(draw, line.bounds, line.text, L.scale, color);
        if (line.role == LineRole::Title && HumanIdentity(L.identity) && line.bounds.w >= 1.0f) {
            const float uy = line.bounds.y + line.bounds.h;
            Segment(draw, line.bounds.x, uy, line.bounds.x + line.bounds.w - 1.0f, uy,
                    palette.human);
        }
    }

    if (L.has_separator) Fill(draw, L.separator, palette.separator);

    const bool pressing = gesture_ == Gesture::PanelArmed;
    for (int i = 0; i < L.control_count; ++i) {
        const Control& c = L.controls[i];
        uint32_t back = palette.button;
        if (!c.enabled) {
            back = palette.button_disabled;
        } else if (pressing && armed_control_ == i && armed_inside_) {
            back = palette.button_pressed;
        } else if (!pressing && hover_control_ == i) {
            back = palette.button_hover;
        }
        Fill(draw, c.rect, back);
        Outline(draw, c.rect, c.host_armed ? palette.accent : palette.button_border);
        Text(draw, c.text_bounds, c.label, L.scale,
             c.enabled ? palette.text : palette.text_disabled);
    }

    if (L.has_detail_row) {
        int focus = pressing ? armed_control_ : hover_control_;
        const char* detail = L.detail_legend;
        if (focus >= 0 && focus < L.control_count && L.controls[focus].detail[0] != '\0') {
            detail = L.controls[focus].detail;
        }
        Text(draw, L.detail_row, detail, L.scale, palette.text_dim);
    }
}

}  // namespace ce_command_panel
