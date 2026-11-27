// SPDX-License-Identifier: GPL-3.0-only
#include "ce_editor.h"
#include "ce_modes.h"
#include <cmath>
#include <cstring>

namespace {
const float CoordinateLimit = 1048576.0f;
bool SamePoint(CeEditorPoint a, CeEditorPoint b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool SourceName(const char *name, size_t *length) {
    if (!name || !length) return false;
    size_t n = 0;
    bool visible = false;
    while (n < sizeof(CeEditorObject::source) && name[n]) {
        const unsigned char c = static_cast<unsigned char>(name[n]);
        if (c < 32 || c > 126) return false;
        visible = visible || c > 32;
        ++n;
    }
    if (!visible || n == sizeof(CeEditorObject::source)) return false;
    *length = n;
    return true;
}
} // namespace

CeEditorState::CeEditorState() { Reset(); }
void CeEditorState::Reset() {
    count_ = 0;
    selected_ = -1;
    historySize_ = historyAt_ = 0;
    moving_ = sealed_ = false;
    collectionValid_ = true;
    moveStart_ = {};
    route_ = savedRoute_ = {};
    playerStartAvailable_ = false;
    originalPlayerStart_ = {};
    playerStart_ = savedPlayerStart_ = movePlayerStart_ = {};
    moveCarriesStart_ = false;
    moveStartPoint_ = {};
}
bool CeEditorState::Valid(CeEditorPoint point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
           std::fabs(point.x) <= CoordinateLimit && std::fabs(point.y) <= CoordinateLimit &&
           std::fabs(point.z) <= CoordinateLimit;
}
bool CeEditorState::Add(const char *source, CeEditorPoint position, unsigned identity, bool available, int side) {
    size_t length = 0;
    if (sealed_) return false;
    if (count_ == ObjectLimit || !SourceName(source, &length) || !Valid(position) || side < -1 || side > 2) {
        collectionValid_ = false;
        return false;
    }
    CeEditorObject &object = objects_[count_];
    std::memcpy(object.source, source, length + 1);
    object.position = object.saved = position;
    object.side = object.savedSide = side;
    object.identity = identity;
    object.available = available;
    for (int i = 0; i < count_; ++i)
        if (!std::strcmp(objects_[i].source, source)) {
            object.available = objects_[i].available = false;
            if (selected_ == i) selected_ = -1;
        }
    ++count_;
    return true;
}
bool CeEditorState::Seal() {
    if (sealed_) return true;
    if (!collectionValid_) return false;
    for (int i = 0; i < count_; ++i)
        if (objects_[i].available) {
            sealed_ = true;
            return true;
        }
    if (playerStartAvailable_) { sealed_ = true; return true; }
    return false;
}
const CeEditorObject *CeEditorState::Object(int index) const {
    return index >= 0 && index < count_ ? &objects_[index] : 0;
}

bool CeEditorState::SameStart(const CeEditorPlayerStart& a, const CeEditorPlayerStart& b) {
    return a.enabled == b.enabled && (!a.enabled || SamePoint(a.position, b.position));
}
bool CeEditorState::InitializePlayerStart(CeEditorPoint original) {
    if (sealed_ || moving_ || playerStartAvailable_ || !Valid(original)) return false;
    originalPlayerStart_ = original; playerStartAvailable_ = true;
    playerStart_ = savedPlayerStart_ = {false, original}; return true;
}
bool CeEditorState::RestorePlayerStart(bool enabled, CeEditorPoint position) {
    if (moving_ || (enabled && (!playerStartAvailable_ || !Valid(position)))) return false;
    playerStart_ = savedPlayerStart_ = {enabled, enabled ? position : originalPlayerStart_}; return true;
}
bool CeEditorState::ChangeStart(const CeEditorPlayerStart& next) {
    if (!sealed_ || moving_ || !playerStartAvailable_ || SameStart(playerStart_, next)) return false;
    Change change{}; change.object = PlayerStartSelection;
    change.oldStart = playerStart_; change.newStart = next;
    playerStart_ = next; selected_ = PlayerStartSelection; Record(change); return true;
}
bool CeEditorState::SetPlayerStart(CeEditorPoint position) {
    if (!Valid(position)) return false;
    return ChangeStart({true, position});
}
bool CeEditorState::UseOriginalPlayerStart() {
    return ChangeStart({false, originalPlayerStart_});
}

bool CeEditorState::Select(int index) {
    if ((index == PlayerStartSelection && !playerStartAvailable_) || !sealed_ || moving_ || index < PlayerStartSelection || index >= count_ || (index >= 0 && !objects_[index].available))
        return false;
    selected_ = index;
    return true;
}
bool CeEditorState::Cycle(int direction) {
    if (!sealed_ || moving_ || !count_ || (direction != -1 && direction != 1)) return false;
    int next = selected_;
    if (next < 0) next = direction > 0 ? count_ - 1 : 0;
    for (int i = 0; i < count_; ++i) {
        next = (next + direction + count_) % count_;
        if (objects_[next].available) {
            selected_ = next;
            return true;
        }
    }
    selected_ = -1;
    return false;
}
bool CeEditorState::BeginMove(bool carryStart) {
    if (PlayerStartSelected()) {
        if (!sealed_ || moving_ || !playerStartAvailable_) return false;
        moving_ = true; moveStart_ = PlayerStart(); movePlayerStart_ = playerStart_;
        return true;
    }

    if (!sealed_ || moving_ || selected_ < 0 || !objects_[selected_].available) return false;
    moving_ = true;
    moveStart_ = objects_[selected_].position;
    moveCarriesStart_ = carryStart && playerStartAvailable_;
    movePlayerStart_ = playerStart_;
    moveStartPoint_ = PlayerStart();
    return true;
}
bool CeEditorState::Move(CeEditorPoint position) {
    if (PlayerStartSelected()) {
        if (!moving_ || !playerStartAvailable_ || !Valid(position)) return false;
        playerStart_ = SamePoint(position, moveStart_) ? movePlayerStart_ : CeEditorPlayerStart{true, position};
        return true;
    }

    if (!moving_ || selected_ < 0 || !objects_[selected_].available || !Valid(position)) return false;
    if (moveCarriesStart_) {
        const CeEditorPoint carried{moveStartPoint_.x + position.x - moveStart_.x,
                                    moveStartPoint_.y + position.y - moveStart_.y,
                                    moveStartPoint_.z + position.z - moveStart_.z};
        if (!Valid(carried)) return false;
        playerStart_ = SamePoint(position, moveStart_) ? movePlayerStart_ : CeEditorPlayerStart{true, carried};
    }
    objects_[selected_].position = position;
    return true;
}
bool CeEditorState::FinishMove(bool accept) {
    if (PlayerStartSelected()) {
        if (!moving_) return false;
        moving_ = false;
        if (!accept) { playerStart_ = movePlayerStart_; return true; }
        if (SameStart(playerStart_, movePlayerStart_)) return false;
        Change change{}; change.object = PlayerStartSelection;
        change.oldStart = movePlayerStart_; change.newStart = playerStart_; Record(change);
        return true;
    }

    if (!moving_ || selected_ < 0) return false;
    moving_ = false;
    const bool carried = moveCarriesStart_;
    moveCarriesStart_ = false;
    CeEditorPoint &after = objects_[selected_].position;
    if (!accept) {
        after = moveStart_;
        if (carried) playerStart_ = movePlayerStart_;
        return true;
    }
    if (SamePoint(after, moveStart_)) return true;
    Change change{selected_, moveStart_, after, objects_[selected_].side, objects_[selected_].side};
    if (carried && !SameStart(movePlayerStart_, playerStart_)) {
        change.startFollows = true; change.oldStart = movePlayerStart_; change.newStart = playerStart_;
    }
    Record(change);
    return true;
}
bool CeEditorState::ChangeSide(int side) {
    if (!sealed_ || moving_ || selected_ < 0 || !objects_[selected_].available || side < -1 || side > 2) return false;
    CeEditorObject &o = objects_[selected_];
    if (o.side == side) return false;
    Record(Change{selected_, o.position, o.position, o.side, side});
    o.side = side;
    return true;
}
bool CeEditorState::SameRoute(const Route& a, const Route& b) {
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i) if (!SamePoint(a.points[i], b.points[i]) || !(a.hold[i] == b.hold[i])) return false;
    return true;
}
bool CeEditorState::ChangeRoute(const Route& next) {
    if (!sealed_ || moving_ || SameRoute(route_, next)) return false;
    Change change{};
    change.object = -1;
    change.oldRoute = route_;
    change.newRoute = next;
    Record(change);
    route_ = next;
    return true;
}
bool CeEditorState::SetReachAt(int index, const CeEditorPoint* point, const CeHoldSettings* hold) {
    if (index < 0 || index >= 2 || index > route_.count) return false;
    Route next = route_;
    if (point) {
        if (!Valid(*point) || (hold && !hold->Valid())) return false;
        if (index == next.count) ++next.count;
        next.points[index] = *point;
        if (hold) next.hold[index] = *hold;
    } else {
        if (index >= next.count) return false;
        for (int i = index + 1; i < next.count; ++i) { next.points[i - 1] = next.points[i]; next.hold[i - 1] = next.hold[i]; }
        next.hold[next.count - 1] = {};
        next.points[--next.count] = {};
    }
    return ChangeRoute(next);
}
bool CeEditorState::SetHoldAt(int index, CeHoldSettings hold) {
    if (index < 0 || index >= route_.count || !route_.hold[index].Enabled() || !hold.Enabled() || !hold.Valid()) return false;
    Route next = route_; next.hold[index] = hold;
    return ChangeRoute(next);
}
bool CeEditorState::ReverseReach() {
    if (route_.count != 2) return false;
    Route next = route_;
    next.points[0] = route_.points[1]; next.points[1] = route_.points[0];
    next.hold[0] = route_.hold[1]; next.hold[1] = route_.hold[0];
    return ChangeRoute(next);
}
bool CeEditorState::RestoreReach(const CeEditorPoint* points, int count, const CeHoldSettings* holds) {
    if (sealed_ || count < 0 || count > 2 || (count && !points)) return false;
    Route next{};
    for (int i = 0; i < count; ++i) {
        if (!Valid(points[i]) || (holds && !holds[i].Valid())) return false;
        if (holds) next.hold[i] = holds[i];
        next.points[next.count++] = points[i];
    }
    route_ = savedRoute_ = next;
    return true;
}
void CeEditorState::Record(const Change &change) {
    // A new edit after Undo discards only the redo branch. The oldest change is retired at the bound.
    historySize_ = historyAt_;
    if (historySize_ == HistoryLimit) {
        for (int i = 1; i < HistoryLimit; ++i) history_[i - 1] = history_[i];
        --historySize_;
        --historyAt_;
    }
    history_[historySize_++] = change;
    historyAt_ = historySize_;
}
bool CeEditorState::Undo() {
    if (moving_ || !historyAt_) return false;
    const Change &change = history_[--historyAt_];
    if (change.object == PlayerStartSelection) { playerStart_ = change.oldStart; selected_ = PlayerStartSelection; return true; }
    else if (change.object < 0) { route_ = change.oldRoute; return true; }
    objects_[change.object].position = change.before;
    objects_[change.object].side = change.beforeSide;
    if (change.startFollows) playerStart_ = change.oldStart;
    selected_ = change.object;
    return true;
}
bool CeEditorState::Redo() {
    if (moving_ || historyAt_ == historySize_) return false;
    const Change &change = history_[historyAt_++];
    if (change.object == PlayerStartSelection) { playerStart_ = change.newStart; selected_ = PlayerStartSelection; return true; }
    else if (change.object < 0) { route_ = change.newRoute; return true; }
    objects_[change.object].position = change.after;
    objects_[change.object].side = change.afterSide;
    if (change.startFollows) playerStart_ = change.newStart;
    selected_ = change.object;
    return true;
}
bool CeEditorState::Dirty() const {
    if (!SameStart(playerStart_, savedPlayerStart_)) return true;
    if (!SameRoute(route_, savedRoute_)) return true;
    for (int i = 0; i < count_; ++i)
        if (!SamePoint(objects_[i].position, objects_[i].saved) || objects_[i].side != objects_[i].savedSide) return true;
    return false;
}
void CeEditorState::MarkSaved() {
    if (moving_) return;
    savedRoute_ = route_;
    for (int i = 0; i < count_; ++i) { objects_[i].saved = objects_[i].position; objects_[i].savedSide = objects_[i].side; }
    savedPlayerStart_ = playerStart_;
}
int CeEditorState::Ambiguous() const {
    int result = 0;
    for (int i = 0; i < count_; ++i)
        if (!objects_[i].available) ++result;
    return result;
}
bool CeEditorRayPlane(CeEditorPoint origin, CeEditorPoint direction, float height, CeEditorPoint *point) {
    if (!point || !CeEditorState::Valid(origin) || !CeEditorState::Valid(direction) ||
        !std::isfinite(height) || std::fabs(height) > CoordinateLimit || std::fabs(direction.z) < 1e-6f)
        return false;
    const double distance = (double(height) - origin.z) / direction.z;
    if (!std::isfinite(distance) || distance < 0) return false;
    const double x = origin.x + distance * direction.x, y = origin.y + distance * direction.y;
    if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > CoordinateLimit ||
        std::fabs(y) > CoordinateLimit)
        return false;
    const CeEditorPoint value{static_cast<float>(x), static_cast<float>(y), height};
    if (!CeEditorState::Valid(value)) return false;
    *point = value;
    return true;
}
bool CeEditorState::KeepHistory(const CeEditorState &before) {
    if (playerStartAvailable_ != before.playerStartAvailable_) return false;
    if (moving_ || before.count_ != count_ || before.historySize_ < 0 || before.historySize_ > HistoryLimit) return false;
    for (int i = 0; i < count_; ++i)
        if (strcmp(objects_[i].source, before.objects_[i].source) || objects_[i].available != before.objects_[i].available)
            return false;
    for (int i = 0; i < before.historySize_; ++i)
        if (before.history_[i].object < PlayerStartSelection || before.history_[i].object >= count_ || (before.history_[i].object >= 0 && !objects_[before.history_[i].object].available))
            return false;
    for (int i = 0; i < before.historySize_; ++i) history_[i] = before.history_[i];
    historySize_ = before.historySize_;
    historyAt_ = before.historyAt_;
    savedRoute_ = before.savedRoute_; savedPlayerStart_ = before.savedPlayerStart_;
    for (int i = 0; i < count_; ++i) {
        objects_[i].saved = before.objects_[i].saved;
        objects_[i].savedSide = before.objects_[i].savedSide;
    }
    return true;
}
CeEditorBounds CeEditorLevelBounds(int width, int height) {
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536)
        return CeEditorBounds{-CoordinateLimit, -CoordinateLimit, CoordinateLimit, CoordinateLimit};
    const float side = float(width > height ? width : height), margin = side * 0.1f > 64.f ? side * 0.1f : 64.f;
    return CeEditorBounds{-margin, -margin, float(width) + margin, float(height) + margin};
}
bool CeEditorInside(const CeEditorBounds &b, CeEditorPoint p) {
    return CeEditorState::Valid(p) && p.x >= b.minX && p.x <= b.maxX && p.y >= b.minY && p.y <= b.maxY;
}
bool CeEditorFitView(const CeEditorPoint *points, int count, const CeEditorBounds &bounds, CeEditorPoint *focus, float *distance) {
    if (!points || !focus || !distance) return false;
    bool first = true;
    float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0;
    for (int i = 0; i < count; ++i) {
        const CeEditorPoint p = points[i];
        if (!CeEditorInside(bounds, p)) continue;   // an object far outside the level never takes the view with it
        if (first) {
            minX = maxX = p.x;
            minY = maxY = p.y;
            minZ = p.z;
            first = false;
            continue;
        }
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
        if (p.z < minZ) minZ = p.z;
    }
    if (first) return false;
    *focus = CeEditorPoint{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, minZ};
    const float span = maxX - minX > maxY - minY ? maxX - minX : maxY - minY;
    *distance = span * 0.8f;
    if (*distance < 40) *distance = 40;
    if (*distance > 500) *distance = 500;
    CeEditorClampView(bounds, focus, distance);
    return true;
}
void CeEditorClampView(const CeEditorBounds &b, CeEditorPoint *focus, float *distance) {
    if (focus) {
        if (!std::isfinite(focus->x)) focus->x = (b.minX + b.maxX) * 0.5f;
        if (!std::isfinite(focus->y)) focus->y = (b.minY + b.maxY) * 0.5f;
        if (!std::isfinite(focus->z)) focus->z = 0;
        focus->x = focus->x < b.minX ? b.minX : focus->x > b.maxX ? b.maxX : focus->x;
        focus->y = focus->y < b.minY ? b.minY : focus->y > b.maxY ? b.maxY : focus->y;
        const float side = b.maxX - b.minX > b.maxY - b.minY ? b.maxX - b.minX : b.maxY - b.minY;
        focus->z = focus->z < -side ? -side : focus->z > side ? side : focus->z;
    }
    if (distance) {
        const float side = b.maxX - b.minX > b.maxY - b.minY ? b.maxX - b.minX : b.maxY - b.minY;
        const float most = side < 1500.f ? side : 1500.f;
        if (!std::isfinite(*distance) || *distance < 5) *distance = 5;
        if (*distance > most) *distance = most;
    }
}

#ifdef CE_VERSION
#include "ce_world_view.h"
#include "common.h"
#include "game.h"
#include "frontend.h"
#include "Career.h"
#include "world.h"
#include "thing.h"
#include "player.h"
#include "BattleEngine.h"
#include "Camera.h"
#include "Controller.h"
#include "CLIParams.h"
#include "ltshell.h"
#include "mouse.h"
#include "Hud.h"
#include "SpriteRenderer.h"
#include "renderinfo.h"
#include "Texture.h"
#include "ce_photo.h"
#include "ce_project.h"
#include "ce_editor_storage.h"
#include "ce_editor_ui.h"
#include "FrontEndText.h"
#include "unit.h"
#include "map.h"
#include "collisionseekingthing.h"
#include "MeshCollisionVolume.h"
#include "Line.h"
#include "GameInterface.h"
#include "eventmanager.h"
#include <stdio.h>
#include <stdlib.h>
extern "C" int PortCeRecordFeaturesAllowed(void) __attribute__((weak));

// Open matches every placement of a project to one editor object; a project may never hold more placements than that table.
static_assert(int(CeProjectPlacementLimit) <= int(CeEditorState::ObjectLimit), "a project's placements must fit the editor's objects");

bool PortCeMenus();
bool PortRecording();
unsigned long long PortClockRawNs();
unsigned long long PortClockPeekNs();
extern "C" bool PortCeMuseumVisitActive(void) __attribute__((weak));
extern "C" bool PortCeMuseumActive(void) __attribute__((weak));

namespace {
struct EditorGameAccess : CGame {
    static BOOL CGame::*PauseField() { return &EditorGameAccess::mPause; }
    static EGameState CGame::*StateField() { return &EditorGameAccess::mGameState; }
};
class EditorInput : public IController {
  public:
    void ReceiveButtonAction(CController *, int, float) override {}
    BOOL CanBeControlledWhenInPause() override { return TRUE; }
    EControlType GetControlType() override { return CONTROL_GAME_MENU; }
};
EditorInput gEditorInput;
CeEditorState gEditorState;
struct SourceObject {
    char name[96];
    CeEditorPoint initial;
    int installedSide;
};
SourceObject gEditorSources[CeEditorState::ObjectLimit];
// Open's working set (about half a megabyte) lives here, not on the stack: the Windows main thread reserves 1 MiB.
CeEditorState gEditorCandidate;
// The scene as a play test left it, so Undo and Redo carry over the play test and the way back.
CeEditorState gEditorKept;
int gEditorMatches[CeEditorState::ObjectLimit];
CThing *gEditorActors[CeEditorState::ObjectLimit];
CeEditorPoint gEditorActorStart[CeEditorState::ObjectLimit];
CeProjectPosition gEditorPositions[CeEditorState::ObjectLimit];
CeEditorPoint gEditorFitPoints[CeEditorState::ObjectLimit];
int gEditorSourceCount = 0, gEditorLevel = -1, gEditorReturnLevel = 0;
bool gEditorRequested = false, gEditorWorldStarted = false, gEditorWorldComplete = false;
bool gEditorReady = false, gEditorInputOwned = false, gEditorOpening = false, gEditorOriginalPause = false;
bool gEditorKeys[256], gEditorOrbit = false, gEditorViewValid = false, gEditorRefusalParked = false;
// The open project file holds exactly the scene's saved state, so Play can replay it without writing another file.
bool gEditorFileCurrent = false;
CCareer *gEditorCareer = nullptr;
CeProjectDocument *gEditorDocument = nullptr;
CeProjectDocument *gEditorPlayDocument = nullptr;
CeProjectPlayback *gEditorPlayback = nullptr;
// Refused: a map load the editor cannot use. The load is left to finish untouched (every hook answers success and applies
// nothing), the level parks paused, and the first frame leaves through the ordinary quit to the menu, where the page shows
// the sentence. The original answers a failed load with its fatal error, so no editor refusal may fail one.
enum EditorPhase { Authoring, LoadPlaytest, Playing, ReturnToAuthoring, Refused };
EditorPhase gEditorPhase = Authoring;
char gEditorPlaySelection[96] = "";
bool gEditorChoosingPlayerStart = false;
int gEditorNativeStartCount = 0;
bool gEditorNativeStartFlying = false;
bool gEditorStartRejected = false;
int gEditorStartRejectedFit = 0;    // the EditorStartFit that sent the play test back
CeEditorPoint gEditorNativeStart{};
bool gEditorChoosingReach = false, gEditorReached = false, gEditorReachPlayerValid = false;
CeProjectReach gEditorTestReach;
CeProjectReach gEditorNextReach{};
bool gEditorSecondNeedsExit = false;
double gEditorHoldElapsed = 0;
bool gEditorHoldInside = false;
unsigned long long gEditorHoldLastNs = 0;
int gEditorReachStage = 0, gEditorReachLength = 0, gEditorPlaceIndex = 0, gEditorRouteRow = 0;
float gEditorReachDistance = 0;
bool gEditorReachAtSurface = false;
CeEditorPoint gEditorReachPrevious{};
const void *gEditorReachPilot = nullptr;
int gEditorReachBearing = 0;
// The volume includes the body of a walking Battle Engine approaching a point on the surface.
const int kEditorReachRadius = 18;
CCamera *gEditorPreviousCamera = nullptr;
class EditorCamera;
EditorCamera *gEditorCamera = nullptr;
class EditorCamera : public CGenericCamera {
  public:
    ~EditorCamera() override {
        if (gEditorCamera == this) gEditorCamera = nullptr;
    }
};
float gEditorYaw = 0, gEditorPitch = 1.05f, gEditorDistance = 90;
CeEditorPoint gEditorFocus{0, 0, 0}, gEditorDragOffset{0, 0, 0};
float gEditorPointerX = 0, gEditorPointerY = 0, gEditorPlaneZ = 0;
unsigned long long gEditorLastTime = 0;
std::uint64_t gEditorSessionToken = 1;
float gEditorView[16], gEditorProjection[16];
CeViewViewport gEditorViewport;
// The screen furniture's state: the focused tool, the focused question button, the help key, the passing notice and the
// pointer, which shows once the mouse has moved (the hover and its cue need a move too).
int gEditorToolFocus = 0, gEditorButtonFocus = 0;
bool gEditorHelp = false, gEditorPointerSeen = false, gEditorPadActive = false, gEditorPadTap = false, gEditorPadMove = false, gEditorMouseDrag = false;
float gEditorHoverX = -1, gEditorHoverY = -1;
unsigned long long gEditorNoticeUntil = 0;
// What a notice is about, so it goes when that is no longer true (EditorNoticeSettle): a passing message stays its five
// seconds; a refusal goes when the selection changes; "finish moving first" goes when the move ends. Any notice goes when a
// list, a question or the key help opens over it. A closing editor's message is the menu page's and is never settled.
enum EditorNoticeKind { NoticePassing, NoticeRefusal, NoticeUntilMoveEnds };
int gEditorNoticeKind = NoticePassing, gEditorNoticeSelection = -1;
bool gEditorClosing = false;
// The editor's clock: the game's virtual time when a run fixes it (so a notice lasts as many frames as it would at the
// game's own rate), otherwise the system's. Never counted as a read of the game's clock.
unsigned long long EditorNowNs() {
    const unsigned long long virtualNs = PortClockPeekNs();
    return virtualNs ? virtualNs : PortClockRawNs();
}
enum EditorCueKind { CueMove, CueSelect, CueBack };
void EditorCue(int cue);
// What the last drawn frame has under a point (window pixels): a tool, a list row, a question button, or other furniture.
struct EditorHitResult { int tool, row, button; bool furniture; int scroll; bool card = false; };
EditorHitResult EditorHit(float x, float y);
void EditorStartView();
bool gEditorLayoutValid = false;
// A project's name for the player, from its file's name.
void ProjectLabel(const char *file, char *out, size_t capacity);
char gEditorNote[192] = "";
char gEditorFilename[128] = "New project";
CeEditorFile gEditorFiles[2048];
int gEditorFileCount = 0, gEditorFileIndex = 0, gEditorFileTop = 0;
// What each listed project is, learned by reading it the first time its row shows: grey rows cannot be opened here.
enum EditorFileKind { FileUnknown = 0, FileOpens, FileOtherLevel, FileUnreadable };
signed char gEditorFileKinds[2048];
short gEditorFileLevels[2048];   // the level a readable listed project is for
enum RouteAction { AddStop, MoveFirst, MoveSecond, ReverseStops, RemoveFirst, RemoveSecond, CloseRoute };
struct RouteChoice { RouteAction action; const char* label; };
enum EditorDialog { DialogNone, DialogFiles, DialogDiscardExit, DialogDiscardOpen, DialogRoute, DialogHoldMenu, DialogHoldInspector };
EditorDialog gEditorDialog = DialogNone;
bool gEditorPlaceHold = false;
int gEditorHoldIndex = 0;
char gHoldTitle[72]{}, gHoldAction[3][80]{};
CDXBitmapFont *gEditorFont = nullptr;
CDXTexture *gEditorWhite = nullptr;
float gEditorWidth = 0, gEditorHeight = 0, gEditorScale = 1;
// What EditorPlayerStartFit finds at a Player Start.
enum EditorStartFit { StartFits, StartNoFooting, StartCovered, StartEdge, StartCrowded };
bool EditorTrap(WORD key, int event);

// The latest message: a passing notice in the editor for five seconds, and the menu page's message when the editor closes.
void EditorNote(const char *message, int kind = NoticePassing) {
    snprintf(gEditorNote, sizeof(gEditorNote), "%s", message);
    gEditorNoticeUntil = gEditorNote[0] ? EditorNowNs() + 5000000000ull : 0;
    gEditorNoticeKind = kind;
    gEditorNoticeSelection = gEditorState.Selected();
}
// A notice whose cause is gone, or that a list, question or the key help now covers, is dropped (see EditorNoticeKind).
void EditorNoticeSettle() {
    if (gEditorClosing || !gEditorNote[0]) return;
    const bool covered = gEditorDialog != DialogNone || gEditorHelp;
    const bool moved = gEditorNoticeKind == NoticeUntilMoveEnds && !gEditorState.Moving();
    const bool reselected = gEditorNoticeKind != NoticePassing && gEditorState.Selected() != gEditorNoticeSelection;
    if (covered || moved || reselected) {
        gEditorNote[0] = 0;
        gEditorNoticeUntil = 0;
    }
}
// Latch the first refusal of this load. Always true: the engine's load must finish as if nothing had been asked.
bool EditorRefuse(const char *message) {
    if (gEditorPhase != Refused) {
        EditorNote(message);
        gEditorPhase = Refused;
    }
    return true;
}
CeEditorPoint Point(const FVector &p) { return CeEditorPoint{p.X, p.Y, p.Z}; }
FVector Vector(CeEditorPoint p) { return FVector(p.x, p.y, p.z); }
CeEditorPoint NativePosition(int index) {
    if (index == CeEditorState::PlayerStartSelection) return gEditorState.PlayerStart();
    const CeEditorObject *object = gEditorState.Object(index);
    const CeEditorPoint initial = gEditorSources[index].initial, actor = gEditorActorStart[index];
    return CeEditorPoint{actor.x + object->position.x - initial.x, actor.y + object->position.y - initial.y,
                         actor.z + object->position.z - initial.z};
}
// After MoveTo, make the world's line tests find a moved object where it now is; the parked editor never ticks, so
// nothing else does. Two things are stale: the thing's map-who sector (refreshed as CActor::Teleport does; the previous
// position is left alone), and its collision mesh's cached part transforms, which are rebuilt only once the event clock
// passes the frame they were cached at (PartTransform) and the parked clock stands still. Dropping that cache
// (SetRenderThing frees it) makes the next test evaluate each part at the object's current place. Without this, the
// spawn boat's deck was still found where the boat had been (native run on 3de5b785).
void EditorRefreshCollision(CThing *thing) {
    if (!thing) return;
    if (CMapWhoEntry *entry = thing->GetMapWhoEntry()) {
        CSector before = entry->GetSectorIn();
        if (entry->UpdateEntry(thing->GetPos()) && thing->GetCSPT()) thing->GetCSPT()->MovedSector(before, entry->GetSectorIn());
    }
    CCSPersistentThing *seeker = thing->GetCSPT();
    CGeneralVolume *volume = seeker ? seeker->GetCollisionMeshVolume() : nullptr;
    if (volume && volume->GetType() == 3) {
        CMeshCollisionVolume *mesh = static_cast<CMeshCollisionVolume *>(volume);
        mesh->SetRenderThing(mesh->mRenderThing);
    }
}
CThing *LiveActor(int index) {
    const CeEditorObject *object = gEditorState.Object(index);
    if (!gEditorReady || gEditorLevel != GAME.GetCurrentLevel() || !object || !object->available)
        return nullptr;
    ListIterator<CThing> things(&WORLD.GetThingNB());
    for (CThing *current = things.First(); current; current = things.Next())
        if (current == gEditorActors[index] && unsigned(current->GetThingNumber()) == object->identity)
            return current;
    return nullptr;
}
bool ApplyOne(int index) {
    if (index == CeEditorState::PlayerStartSelection) return true;
    CThing *actor = LiveActor(index);
    if (!actor) return false;
    const CeEditorPoint position = NativePosition(index);
    if (!CeEditorState::Valid(position)) return false;
    actor->MoveTo(Vector(position));
    EditorRefreshCollision(actor);
    return true;
}
void EditorExit() {
    gEditorClosing = true;
    GAME.SetQuit(QT_USER_QUIT_TO_FRONTEND);
}
// The player's own leave: the menu page shows no message.
void EditorLeave() {
    gEditorNote[0] = 0;
    EditorExit();
}
static void EditorCopyCareerOptions(CCareer& target, CCareer& current) {
    target.SetMusicVolume(current.GetMusicVolume());
    target.SetSoundVolume(current.GetSoundVolume());
    for (int player = 0; player < 2; ++player) {
        target.SetInvertYAxis(player, current.GetInvertYAxis(player));
        target.SetInvertJetYAxis(player, current.GetInvertJetYAxis(player));
        target.SetIsGod(player, current.GetIsGod(player));
        target.SetVibration(player, current.GetVibration(player));
        target.SetControllerConfigurationNum(player, current.GetControllerConfigurationNum(player));
    }
    target.SetScreenPosX(current.GetScreenPosX());
    target.SetScreenPosY(current.GetScreenPosY());
}

static void EditorRestoreCareer() {
    if (!gEditorCareer) return;
    CCareer restored;
    memcpy(&restored, gEditorCareer, sizeof(restored));
    EditorCopyCareerOptions(restored, CAREER);
    memcpy(&CAREER, &restored, sizeof(CAREER));
}
bool EditorPrepareReload(EditorPhase phase) {
    if (gEditorSessionToken == ~std::uint64_t(0)) {
        EditorNote("Restart Onslaught CE to use the editor again.");
        return false;
    }
    EditorRestoreCareer();
    gEditorPhase = phase;
    gEditorWorldStarted = gEditorWorldComplete = false;
    gEditorDialog = DialogNone;
    gEditorOrbit = gEditorViewValid = gEditorPadActive = gEditorPadMove = gEditorMouseDrag = gEditorLayoutValid = false;
    memset(gEditorKeys, 0, sizeof(gEditorKeys));
    CeWorldViewInvalidate(CeWorldViewOwner::Editor);
    ++gEditorSessionToken;
    return true;
}
bool EditorReturnToAuthoring() {
    if (!EditorPrepareReload(ReturnToAuthoring)) {
        EditorExit();
        return false;
    }
    EditorNote("");
    GAME.SetQuit(QT_RESTART_LEVEL);
    return true;
}
// The view's bounds: the level's own ground and a margin (read once the level has loaded).
CeEditorBounds EditorBounds() { return CeEditorLevelBounds(MAP.mGridWidth, MAP.mGridHeight); }
// Frame the selected object (false when it lies outside the level: the view stays), or every object on the level.
bool EditorFit(bool selected) {
    const CeEditorBounds bounds = EditorBounds();
    if (selected && gEditorState.Selected() != -1) {
        const CeEditorPoint p = NativePosition(gEditorState.Selected());
        if (!CeEditorInside(bounds, p)) return false;
        gEditorFocus = p;
        gEditorDistance = 45;
        CeEditorClampView(bounds, &gEditorFocus, &gEditorDistance);
        return true;
    }
    int count = 0;
    for (int i = 0; i < gEditorState.Count(); ++i)
        if (gEditorState.Object(i)->available) gEditorFitPoints[count++] = NativePosition(i);
    if (!CeEditorFitView(gEditorFitPoints, count, bounds, &gEditorFocus, &gEditorDistance)) {
        // Nothing on the level itself: its middle, from a height that shows it.
        gEditorFocus = CeEditorPoint{(bounds.minX + bounds.maxX) * 0.5f, (bounds.minY + bounds.maxY) * 0.5f, 0};
        gEditorDistance = 500;
        CeEditorClampView(bounds, &gEditorFocus, &gEditorDistance);
    }
    return true;
}
void EditorGamepadUpdate(float elapsed);
void EditorCameraUpdate(float elapsed) {
    if (!gEditorCamera) return;
    if (elapsed > 0.f) EditorGamepadUpdate(elapsed);
    if (gEditorDialog == DialogNone && !gEditorHelp && !gEditorState.Moving()) {
        float x = float(gEditorKeys[DIK_D]) - float(gEditorKeys[DIK_A]);
        float y = float(gEditorKeys[DIK_W]) - float(gEditorKeys[DIK_S]);
        if (gEditorKeys[DIK_LCONTROL] || gEditorKeys[DIK_RCONTROL]) x = y = 0;
        FMatrix turn;
        turn.MakeRotationYawF(gEditorYaw);
        FVector step = turn * FVector(x, y, 0);
        const float speed =
            gEditorDistance * elapsed * (gEditorKeys[DIK_LSHIFT] || gEditorKeys[DIK_RSHIFT] ? 1.2f : 0.5f);
        gEditorFocus.x += step.X * speed;
        gEditorFocus.y += step.Y * speed;
        if (gEditorOrbit) {
            gEditorYaw += (float(MOUSE_X) - gEditorPointerX) * 0.006f;
            gEditorPitch += (float(MOUSE_Y) - gEditorPointerY) * 0.006f;
        }
    }
    if (gEditorPitch < 0.15f) gEditorPitch = 0.15f;
    if (gEditorPitch > 1.5f) gEditorPitch = 1.5f;
    // The view never leaves the level, whatever a project holds or the keys ask for.
    CeEditorClampView(EditorBounds(), &gEditorFocus, &gEditorDistance);
    gEditorPointerX = float(MOUSE_X);
    gEditorPointerY = float(MOUSE_Y);
    gEditorCamera->mOrientation.MakeRotationF(gEditorYaw, gEditorPitch, 0);
    const FVector forward = gEditorCamera->mOrientation * FVector(0, 1, 0);
    gEditorCamera->mPos = Vector(gEditorFocus) - forward * gEditorDistance;
}
bool EditorSnapshot() {
    CeWorldViewSample sample;
    gEditorViewValid = CeWorldViewRead(CeWorldViewOwner::Editor, PortCeEditorWorldViewToken(), &sample);
    if (!gEditorViewValid) return false;
    memcpy(gEditorView, sample.view, sizeof(gEditorView));
    memcpy(gEditorProjection, sample.projection, sizeof(gEditorProjection));
    gEditorViewport = sample.viewport;
    return true;
}
bool EditorRay(float x, float y, float height, CeEditorPoint *point) {
    if (!EditorSnapshot()) return false;
    CeViewPoint nearPoint, farPoint;
    if (!CeViewRay(gEditorView, gEditorProjection, gEditorViewport, x, y, &nearPoint, &farPoint))
        return false;
    return CeEditorRayPlane(
        CeEditorPoint{nearPoint.x, nearPoint.y, nearPoint.z},
        CeEditorPoint{farPoint.x - nearPoint.x, farPoint.y - nearPoint.y, farPoint.z - nearPoint.z}, height,
        point);
}
// Intersect the cursor with the surface under it, including the sea over submerged ground.
bool EditorReachPointAt(float x, float y, CeEditorPoint *out) {
    if (!out || !EditorSnapshot()) return false;
    CeViewPoint from, to;
    if (!CeViewRay(gEditorView,gEditorProjection,gEditorViewport,x,y,&from,&to)) return false;
    const double dx=to.x-from.x, dy=to.y-from.y, dz=to.z-from.z, len=sqrt(dx*dx+dy*dy+dz*dz);
    if (!(len>0) || !std::isfinite(len)) return false;
    float previous=-1.f;
    auto surface = [&](float distance, CeEditorPoint &point, float &delta) {
        point={float(from.x+dx*distance/len),float(from.y+dy*distance/len),float(from.z+dz*distance/len)};
        if (!CeEditorState::Valid(point) || point.x<0 || point.y<0 || point.x>=MAP.mGridWidth-1 || point.y>=MAP.mGridHeight-1) return false;
        FVector probe(point.x,point.y,0);
        const float ground=MAP.Collide(probe), water=MAP.GetWaterLevel();
        if (!std::isfinite(ground) || !std::isfinite(water)) return false;
        const float height=ground<water ? ground : water;
        delta=point.z-height; point.z=height; return true;
    };
    for (int i=0;i<=1024;++i) {
        const float distance=float(i)*2.f;
        CeEditorPoint hit; float depth;
        if (!surface(distance,hit,depth)) { previous=-1.f; continue; }
        if (depth>=0 && previous>=0) {
            float lo=previous,hi=distance;
            for (int n=0;n<9;++n) { const float mid=(lo+hi)*.5f; CeEditorPoint q; float h;
                if (!surface(mid,q,h)) return false;
                if (h>=0) {hi=mid;hit=q;} else lo=mid;
            }
            *out=hit; return true;
        }
        previous=depth<0 ? distance : -1.f;
    }
    return false;
}
bool EditorPixel(int index, CeViewPoint *pixel) {
    if (index == CeEditorState::PlayerStartSelection) {
        if (!gEditorState.PlayerStartAvailable()) return false;
        const CeEditorPoint position = gEditorState.PlayerStart();
        return CeViewProject(gEditorView, gEditorProjection, gEditorViewport,
                             CeViewPoint{position.x, position.y, position.z}, pixel);
    }

    if (!gEditorViewValid || !gEditorState.Object(index)->available) return false;
    const CeEditorPoint position = NativePosition(index);
    return CeViewProject(gEditorView, gEditorProjection, gEditorViewport,
                         CeViewPoint{position.x, position.y, position.z}, pixel);
}
// Whether the Battle Engine can begin at a start: something to stand on just below it (dry ground, or any object's
// body such as a carrier's deck), and nothing above it (a start dropped inside or under a building or bridge).
// How far above the water a surface must be to stand on: dry ground just above it; an object's body (a deck) a metre
// above, so a boat's hull at the waterline is not taken for its deck (a start there slid into the sea in a native run).
float EditorStandAbove(int hitType) { return hitType == kCollideGround ? 0.25f : 1.0f; }
// The pilot's centre-of-gravity height above its feet (at least 1 m): a start is that far above what it stands on.
float EditorStartClearance(float height) {
    return !std::isfinite(height) || height < 1.f ? 1.f : height;
}
// placed: the player put this start here (or moved it with a deck). Only such a start is also checked around the body; a
// map's own start is where its designers put it.
EditorStartFit EditorPlayerStartFit(CeEditorPoint start, bool placed) {
    if (!CeEditorState::Valid(start) || !CeEditorInside(EditorBounds(), start)) return StartNoFooting;
    CPlayer* player = GAME.GetPlayer(0);
    auto* pilot = player ? player->GetBattleEngine() : nullptr;
    if (!pilot) return StartNoFooting;
    const float clearance = EditorStartClearance(pilot ? pilot->COfGHeight() : 1.f);
    // First, down from well above: a hit before the start's own height is a roof, a floor or a bridge over it. This
    // comes first because a start inside a building usually finds nothing below it either (the line starts inside).
    const float above = 60.f;
    const FVector sky(start.x, start.y, start.z - above), top(start.x, start.y, start.z - 0.5f);
    CWorldLineColReport over;
    WORLD.FindFirstThingToHitLine(CLine(sky, top), pilot, &over, FALSE, TRUE, 2, 0, TRUE);
    if (over.mHitType != kCollideNothing && std::isfinite(over.mDistToImpact) && over.mDistToImpact >= 0 &&
        over.mDistToImpact < above - 0.5f) return StartCovered;
    // Around the body: a structure (not the ground) within 2 m of the walker's centre, at that height or a body higher,
    // is a wall or tower it would stand inside or lean into (a start on a tower's ledge did, in a native run).
    if (placed) {
        static const float kAround[8][2] = {{1, 0}, {0.7071f, 0.7071f}, {0, 1}, {-0.7071f, 0.7071f},
                                             {-1, 0}, {-0.7071f, -0.7071f}, {0, -1}, {0.7071f, -0.7071f}};
        const float reach = 2.f, lifts[2] = {0.f, clearance};
        for (int k = 0; k < 8; ++k)
            for (int h = 0; h < 2; ++h) {
                const float z = start.z - lifts[h];
                CWorldLineColReport side;
                WORLD.FindFirstThingToHitLine(CLine(FVector(start.x, start.y, z),
                                                    FVector(start.x + kAround[k][0] * reach, start.y + kAround[k][1] * reach, z)),
                                              pilot, &side, FALSE, TRUE, 2, 0, TRUE);
                if (side.mHitType != kCollideNothing && side.mHitType != kCollideGround) return StartCrowded;
            }
    }
    const FVector origin(start.x, start.y, start.z - 0.25f);
    const FVector end(start.x, start.y, start.z + clearance + 4.f);
    CWorldLineColReport report;
    WORLD.FindFirstThingToHitLine(CLine(origin, end), pilot, &report, FALSE, TRUE, 2, 0, TRUE);
    if (report.mHitType == kCollideNothing || !std::isfinite(report.mDistToImpact) ||
        report.mDistToImpact < 0 || report.mDistToImpact > clearance + 4.25f) return StartNoFooting;
    const float surface = origin.Z + report.mDistToImpact;
    if (surface >= MAP.GetWaterLevel() - EditorStandAbove(report.mHitType)) return StartNoFooting;
    // Under the whole footprint, not just the centre: 1.5 m out in four directions there must be something to stand on, within
    // a metre of the centre's height. A start on a hull's sloping side passed the centre test, and the walker slid into the
    // sea at once (native runs g-c221 and i-c221a).
    if (placed) {
        static const float kFeet[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int k = 0; k < 4; ++k) {
            const float x = start.x + kFeet[k][0] * 1.5f, y = start.y + kFeet[k][1] * 1.5f;
            const FVector from(x, y, start.z - 0.25f), to(x, y, start.z + clearance + 4.f);
            CWorldLineColReport foot;
            WORLD.FindFirstThingToHitLine(CLine(from, to), pilot, &foot, FALSE, TRUE, 2, 0, TRUE);
            if (foot.mHitType == kCollideNothing || !std::isfinite(foot.mDistToImpact) || foot.mDistToImpact < 0 ||
                foot.mDistToImpact > clearance + 4.25f) return StartEdge;
            const float under = from.Z + foot.mDistToImpact;
            if (under >= MAP.GetWaterLevel() - EditorStandAbove(foot.mHitType) || std::fabs(under - surface) > 1.f)
                return StartEdge;
        }
    }
    return StartFits;
}
bool EditorPlayerStartSupported(CeEditorPoint start) { return EditorPlayerStartFit(start, true) == StartFits; }
const char *EditorStartRefusal(EditorStartFit fit) {
    return fit == StartCovered ? "Player Start is under or inside something. Move it."
         : fit == StartEdge    ? "Player Start is on an edge or a slope. Put it on flat ground or a deck."
         : fit == StartCrowded ? "Too close to a wall or rail. Move it clear."
                               : "Player Start has nothing under it. Put it on land or a deck.";
}
// The object the Player Start stands on, such as the carrier whose deck it is on, or -1. Moving that object
// moves the start with it.
int EditorStartStandsOn() {
    if (!gEditorState.PlayerStartAvailable()) return -1;
    const CeEditorPoint start = gEditorState.PlayerStart();
    CPlayer* player = GAME.GetPlayer(0);
    auto* pilot = player ? player->GetBattleEngine() : nullptr;
    const float clearance = EditorStartClearance(pilot ? pilot->COfGHeight() : 1.f);
    const FVector origin(start.x, start.y, start.z - 0.25f);
    const FVector end(start.x, start.y, start.z + clearance + 4.f);
    CWorldLineColReport report;
    WORLD.FindFirstThingToHitLine(CLine(origin, end), pilot, &report, FALSE, TRUE, 2, 0, TRUE);
    if (report.mHitType == kCollideNothing || !report.mHitThing) return -1;
    for (int i = 0; i < gEditorState.Count(); ++i)
        if (gEditorActors[i] == report.mHitThing && LiveActor(i)) return i;
    return -1;
}
bool EditorBeginMove() {
    const int selected = gEditorState.Selected();
    const bool carry = selected >= 0 && EditorStartStandsOn() == selected;
    if (!gEditorState.BeginMove(carry)) return false;
    gEditorPadMove = false;
    gEditorMouseDrag = false;
    const CeEditorPoint initial = NativePosition(selected);
    gEditorPlaneZ = initial.z;
    gEditorDragOffset = CeEditorPoint{0.f, 0.f, 0.f};
    if (carry) fprintf(stderr, "CE editor: the Player Start rides on %s\n", gEditorState.Object(selected)->source);
    return true;
}
bool EditorPlayerStartAt(float x, float y, CeEditorPoint& out) {
    if (!gEditorState.PlayerStartAvailable() || !EditorSnapshot()) return false;
    CeViewPoint from{}, to{};
    if (!CeViewRay(gEditorView, gEditorProjection, gEditorViewport, x, y, &from, &to)) return false;
    const FVector origin(from.x, from.y, from.z), end(to.x, to.y, to.z);
    const FVector delta = end - origin;
    const float length = delta.Magnitude();
    if (!std::isfinite(length) || length <= 0) return false;
    CPlayer* player = GAME.GetPlayer(0);
    auto* pilot = player ? player->GetBattleEngine() : nullptr;
    CWorldLineColReport report;
    WORLD.FindFirstThingToHitLine(CLine(origin, end), pilot, &report, FALSE, TRUE, 2, 0, TRUE);
    if (report.mHitType == kCollideNothing || !std::isfinite(report.mDistToImpact) ||
        report.mDistToImpact < 0 || report.mDistToImpact > length) return false;
    const FVector point = origin + delta * (report.mDistToImpact / length);
    // A ground hit below the water is not a walkable player start. A mesh hit
    // can be a carrier deck above it; moving that carrier then moves the start (EditorStartStandsOn).
    if (point.Z >= MAP.GetWaterLevel() - EditorStandAbove(report.mHitType)) return false;
    float clearance = pilot ? pilot->COfGHeight() : 1.f;
    if (!std::isfinite(clearance) || clearance < 1.f) clearance = 1.f;
    out = {point.X, point.Y, point.Z - clearance};
    return CeEditorState::Valid(out) && CeEditorInside(EditorBounds(), out);
}
int EditorPick(float x, float y) {
    if (!EditorSnapshot()) return -1;
    float best = 196 * gEditorScale * gEditorScale;
    int hit = -1;
    for (int i = 0; i < gEditorState.Count(); ++i) {
        CeViewPoint p;
        if (!EditorPixel(i, &p)) continue;
        const float dx = p.x - x, dy = p.y - y, d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            hit = i;
        }
    }
    CeViewPoint startPixel{};
    if (EditorPixel(CeEditorState::PlayerStartSelection, &startPixel)) {
        const float dx = x - startPixel.x, dy = y - startPixel.y;
        if (dx * dx + dy * dy < best) hit = CeEditorState::PlayerStartSelection;
    }
    return hit;
}
// Both explicit Save and preview apply the same placements and rules. Only Save owns a filename.
bool EditorUpdateRevision(CeProjectDocument *&document, char *error, size_t capacity) {
    if (!document) {
        char name[80];
        snprintf(name, sizeof(name), "Installed map %d project", gEditorLevel);
        document = PortCeProjectDocumentNew(gEditorLevel, name, error, capacity);
        if (!document) {
            EditorNote(error);
            return false;
        }
    }
    size_t count = 0;
    for (int i = 0; i < gEditorState.Count(); ++i) {
        const CeEditorObject *object = gEditorState.Object(i);
        if (!object->available) continue;
        gEditorPositions[count++] =
            CeProjectPosition{object->source, object->position.x, object->position.y, object->position.z, object->side};
    }
    CeProjectReach goals[2]{};
    for (int i = 0; i < gEditorState.ReachCount(); ++i) {
        const CeEditorPoint pt = gEditorState.Reach(i);
        goals[i] = {true, pt.x, pt.y, pt.z, gEditorState.Hold(i)};
    }
    CeProjectPlayerStart playerStart;
    playerStart.enabled = gEditorState.PlayerStartEdited();
    const CeEditorPoint startPoint = gEditorState.PlayerStart();
    playerStart.x = startPoint.x; playerStart.y = startPoint.y; playerStart.z = startPoint.z;
    if (!PortCeProjectDocumentUpdate(document, gEditorPositions, count, error, capacity, goals, gEditorState.ReachCount(), &playerStart)) {
        EditorNote(error);
        return false;
    }
    return true;
}
bool EditorSave() {
    if (!gEditorReady || gEditorState.Moving()) {
        EditorNote("Finish moving the object first.", NoticeUntilMoveEnds);
        return false;
    }
    gEditorChoosingReach = false;
    gEditorChoosingPlayerStart = false;
    if (gEditorFileCurrent && !gEditorState.Dirty()) {
        EditorNote("Nothing has changed since the last save.");
        return true;
    }
    char error[192];
    if (!EditorUpdateRevision(gEditorDocument, error, sizeof(error))) return false;
    const size_t size = PortCeProjectDocumentWrite(gEditorDocument, nullptr, 0, error, sizeof(error));
    if (!size) {
        EditorNote(error);
        return false;
    }
    char *bytes = static_cast<char *>(malloc(size));
    if (!bytes) {
        EditorNote("Not enough memory to save. Earlier saves are kept.");
        return false;
    }
    bool ok = PortCeProjectDocumentWrite(gEditorDocument, bytes, size, error, sizeof(error)) == size;
    char name[128];
    if (ok) ok = PortCeEditorSave(gEditorLevel, bytes, size - 1, name, sizeof(name), error, sizeof(error));
    free(bytes);
    if (!ok) {
        fprintf(stderr, "CE editor: save refused: %s\n", error);
        EditorNote("The project could not be saved. Your earlier saves are kept.");
        return false;
    }
    snprintf(gEditorFilename, sizeof(gEditorFilename), "%s", name);
    gEditorState.MarkSaved();
    gEditorFileCurrent = true;
    char label[96], saved[160];
    ProjectLabel(gEditorFilename, label, sizeof(label));
    snprintf(saved, sizeof(saved), "Saved as %s. Earlier saves are kept.", label);
    EditorNote(saved);
    return true;
}
bool EditorApplyDocument(CeProjectDocument *document, const char *filename) {
    if (!document) return false;
    if (PortCeProjectDocumentLevel(document) != gEditorLevel) {
        PortCeProjectDocumentDelete(document);
        EditorNote("This project is for another level. Open that level first.");
        return false;
    }
    // Resolve every source before touching the scene. A partial or ambiguous project changes nothing.
    const size_t count = PortCeProjectDocumentCount(document);
    bool ok = count <= size_t(CeEditorState::ObjectLimit);
    int *matches = gEditorMatches;
    for (size_t p = 0; ok && p < count; ++p) {
        CeProjectPosition position;
        ok = ok && PortCeProjectDocumentPosition(document, p, &position);
        if (!ok) break;
        matches[p] = -1;
        for (int i = 0; i < gEditorState.Count(); ++i)
            if (!strcmp(gEditorState.Object(i)->source, position.source) &&
                gEditorState.Object(i)->available) {
                matches[p] = i;
                break;
            }
        if (matches[p] < 0 || !LiveActor(matches[p])) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        PortCeProjectDocumentDelete(document);
        EditorNote("This project does not match this level, so it was not opened.");
        return false;
    }
    // Start from the installed map positions, so omitted placements do not retain edits from another project.
    CeEditorState &candidate = gEditorCandidate;
    candidate.Reset();
    for (int i = 0; i < gEditorState.Count(); ++i) {
        CeEditorPoint position = gEditorSources[i].initial;
        int side = -1;
        for (size_t p = 0; p < count; ++p)
            if (matches[p] == i) {
                CeProjectPosition value;
                PortCeProjectDocumentPosition(document, p, &value);
                position = CeEditorPoint{value.x, value.y, value.z};
                side = value.side;
                break;
            }
        const CeEditorObject *original = gEditorState.Object(i);
        ok = ok && candidate.Add(original->source, position, original->identity, original->available, side);
        if (side >= 0 && (gEditorSources[i].installedSide < 0 || gEditorSources[i].installedSide > 2)) ok = false;
        if (original->available) {
            if (!LiveActor(i)) ok = false;
            const CeEditorPoint base = gEditorSources[i].initial, actor = gEditorActorStart[i];
            if (!CeEditorState::Valid(CeEditorPoint{actor.x + position.x - base.x,
                                                    actor.y + position.y - base.y,
                                                    actor.z + position.z - base.z}))
                ok = false;
        }
    }
    CeEditorPoint targets[2]{};
    CeHoldSettings holdSettings[2]{};
    int targetCount = 0;
    for (int i = 0; i < 2; ++i) {
        const CeProjectReach reach = PortCeProjectDocumentReach(document, i);
        if (!reach.enabled) break;
        CeEditorPoint target{reach.x, reach.y, reach.z};
        if (target.x < 0 || target.y < 0 || target.x >= MAP.mGridWidth - 1 || target.y >= MAP.mGridHeight - 1) ok = false;
        holdSettings[targetCount] = reach.hold;
        targets[targetCount++] = target;
    }
    if (!candidate.RestoreReach(targets, targetCount, holdSettings)) ok = false;
    if (gEditorNativeStartCount == 1) candidate.InitializePlayerStart(gEditorNativeStart);
    const CeProjectPlayerStart start = PortCeProjectDocumentPlayerStart(document);
    if (!candidate.RestorePlayerStart(start.enabled, {start.x, start.y, start.z})) ok = false;
    if (!ok || !candidate.Seal()) {
        PortCeProjectDocumentDelete(document);
        EditorNote("The project could not be opened. Nothing was changed.");
        return false;
    }
    // A null filename is the return trip from a disposable preview, not an Open or a Save.
    if (!filename && !candidate.KeepHistory(gEditorKept)) {
        PortCeProjectDocumentDelete(document);
        EditorNote("The map no longer matches the draft.");
        return false;
    }
    gEditorState = candidate;
    gEditorChoosingReach=false;
    gEditorChoosingPlayerStart=false;
    for (int i = 0; i < gEditorState.Count(); ++i)
        if (gEditorState.Object(i)->available) (void)ApplyOne(i);
    gEditorDialog = DialogNone;
    if (!filename) {
        PortCeProjectDocumentDelete(document);
        return true;
    }
    PortCeProjectDocumentDelete(gEditorDocument);
    gEditorDocument = document;
    snprintf(gEditorFilename, sizeof(gEditorFilename), "%s", filename);
    gEditorState.MarkSaved();
    gEditorFileCurrent = true;
    char label[96], opened[160];
    ProjectLabel(gEditorFilename, label, sizeof(label));
    snprintf(opened, sizeof(opened), "%s opened.", label);
    EditorNote(opened);
    return true;
}
bool EditorOpenFile(int selected) {
    if (selected < 0 || selected >= gEditorFileCount || gEditorState.Moving()) return false;
    char error[192];
    size_t size = 0;
    char *bytes = PortCeEditorRead(gEditorFiles[selected].name, &size, error, sizeof(error));
    CeProjectDocument *document = bytes ? PortCeProjectDocumentRead(bytes, size, error, sizeof(error)) : nullptr;
    if (bytes) PortCeEditorFileFree(bytes);
    if (!document) {
        fprintf(stderr, "CE editor: open refused: %s\n", error);
        EditorNote("This project could not be read, so it was not opened.");
        return false;
    }
    if (!EditorApplyDocument(document, gEditorFiles[selected].name)) return false;
    // Show the opened project's edits, independently of where the previous project left the view.
    EditorStartView();
    return true;
}
bool EditorStartPlaytest() {
    if (!gEditorReady || gEditorPhase != Authoring || !gEditorInputOwned || gEditorState.Moving() ||
        !gEditorSessionToken || gEditorSessionToken >= ~std::uint64_t(0) - 1 || !PortCeEditorWorldViewToken() ||
        LT.GetKeytrap() != (pKeyTrapper)EditorTrap || !GAME.GetController(0) ||
        GAME.GetController(0)->GetToControl() != &gEditorInput || !PortCeAuthoringSessionAllowed()) {
        const bool moving = gEditorState.Moving();
        EditorNote(moving ? "Finish moving the object first." : "The level cannot be played from the editor right now.", moving ? NoticeUntilMoveEnds : NoticeRefusal);
        return false;
    }
    // A preview describes the map already in memory. Even the first preview of an unnamed draft
    // must work without opening or writing a project file. The authoring document retains its metadata.
    char error[192] = {};
    if (gEditorState.PlayerStartAvailable() &&
        (gEditorState.PlayerStartEdited() || !gEditorNativeStartFlying)) {
        const EditorStartFit fit = EditorPlayerStartFit(gEditorState.PlayerStart(), gEditorState.PlayerStartEdited());
        const CeEditorPoint at = gEditorState.PlayerStart();
        fprintf(stderr, "CE editor: Player Start %.1f %.1f %.1f (%s, on object %d): %s\n", at.x, at.y, at.z,
                gEditorState.PlayerStartEdited() ? "placed" : "the map's own", EditorStartStandsOn(),
                fit == StartFits ? "fits" : EditorStartRefusal(fit));
        if (fit != StartFits) {
            EditorNote(EditorStartRefusal(fit), NoticeRefusal);
            return false;
        }
    }
    CeProjectDocument *document = nullptr;
    if (!EditorUpdateRevision(document, error, sizeof(error))) {
        PortCeProjectDocumentDelete(document);
        return false;
    }
    CeProjectPlayback *playback = document ? PortCeProjectPlaybackNew(document, error, sizeof(error)) : nullptr;
    if (!playback) {
        PortCeProjectDocumentDelete(document);
        fprintf(stderr, "CE editor: play refused: %s\n", error);
        EditorNote("This draft could not be previewed. You are still editing.");
        return false;
    }
    PortCeProjectPlaybackDelete(gEditorPlayback);
    PortCeProjectDocumentDelete(gEditorPlayDocument);
    gEditorPlayback = playback;
    gEditorPlayDocument = document;
    gEditorKept = gEditorState;
    const CeEditorObject *selected = gEditorState.Object(gEditorState.Selected());
    snprintf(gEditorPlaySelection, sizeof(gEditorPlaySelection), "%s", selected ? selected->source : "");
    if (!EditorPrepareReload(LoadPlaytest)) return false;
    gEditorChoosingReach = false;
    gEditorChoosingPlayerStart = false;
    EditorNote("");
    GAME.SetQuit(QT_RESTART_LEVEL);
    return true;
}
// The serial of one of this level's own saves ("level-0221-000012.ceproject"), or 0 for any other file.
int EditorOwnSerial(const char *name) {
    int level = 0, serial = 0;
    char tail[16] = "";
    if (sscanf(name, "level-%4d-%6d%15s", &level, &serial, tail) != 3 || strcmp(tail, ".ceproject") || level != gEditorLevel)
        return 0;
    return serial > 0 ? serial : 0;
}
bool EditorFileBefore(const CeEditorFile &a, const CeEditorFile &b) {
    const int sa = EditorOwnSerial(a.name), sb = EditorOwnSerial(b.name);
    return sa > sb;   // own saves before other files, a later one before an earlier one; other files keep their order
}
void EditorBrowse() {
    if (gEditorState.Moving()) {
        EditorCue(CueBack);
        EditorNote("Finish moving the object first.", NoticeUntilMoveEnds);
        return;
    }
    gEditorChoosingReach = false;
    gEditorChoosingPlayerStart = false;
    char error[192];
    gEditorFileCount = PortCeEditorFiles(gEditorFiles, 2048, error, sizeof(error));
    if (gEditorFileCount < 0) {
        gEditorFileCount = 0;
        fprintf(stderr, "CE editor: listing refused: %s\n", error);
        EditorNote("The projects folder could not be read.");
        EditorCue(CueBack);
        return;
    }
    if (!gEditorFileCount) {
        EditorNote("There are no saved projects yet. Choose Save to make one.");
        EditorCue(CueBack);
        return;
    }
    // This level's own saves first, newest first (they are numbered in order), so the latest work is the first row; then
    // every other project in the folder's order.
    for (int i = 1; i < gEditorFileCount; ++i) {
        const CeEditorFile file = gEditorFiles[i];
        int j = i;
        for (; j > 0 && EditorFileBefore(file, gEditorFiles[j - 1]); --j) gEditorFiles[j] = gEditorFiles[j - 1];
        gEditorFiles[j] = file;
    }
    memset(gEditorFileKinds, 0, sizeof(gEditorFileKinds));
    gEditorFileIndex = gEditorFileTop = 0;
    gEditorDialog = DialogFiles;
    gEditorOrbit = false;
    EditorCue(CueSelect);
}
// Read the chosen project before the player is asked to discard work for it: an unreadable one, or another level's, is
// refused with its sentence and no question.
bool EditorProjectOpens(int selected) {
    if (selected < 0 || selected >= gEditorFileCount) return false;
    char error[192];
    size_t size = 0;
    char *bytes = PortCeEditorRead(gEditorFiles[selected].name, &size, error, sizeof(error));
    CeProjectDocument *document = bytes ? PortCeProjectDocumentRead(bytes, size, error, sizeof(error)) : nullptr;
    if (bytes) PortCeEditorFileFree(bytes);
    if (!document) {
        gEditorFileKinds[selected] = FileUnreadable;
        fprintf(stderr, "CE editor: open refused: %s\n", error);
        EditorNote("This project could not be read, so it was not opened.");
        return false;
    }
    const bool level = PortCeProjectDocumentLevel(document) == gEditorLevel;
    gEditorFileLevels[selected] = short(PortCeProjectDocumentLevel(document));
    PortCeProjectDocumentDelete(document);
    gEditorFileKinds[selected] = level ? FileOpens : FileOtherLevel;
    if (!level) EditorNote("This project is for another level. Open that level first.");
    return level;
}
// True for a listed project that cannot be opened on this level (unreadable, or another level's), read once per listing.
bool EditorFileGrey(int index) {
    if (index < 0 || index >= gEditorFileCount) return true;
    if (gEditorFileKinds[index] == FileUnknown) {
        char error[192];
        size_t size = 0;
        char *bytes = PortCeEditorRead(gEditorFiles[index].name, &size, error, sizeof(error));
        CeProjectDocument *document = bytes ? PortCeProjectDocumentRead(bytes, size, error, sizeof(error)) : nullptr;
        if (bytes) PortCeEditorFileFree(bytes);
        gEditorFileKinds[index] = !document ? FileUnreadable : PortCeProjectDocumentLevel(document) == gEditorLevel ? FileOpens : FileOtherLevel;
        gEditorFileLevels[index] = short(document ? PortCeProjectDocumentLevel(document) : 0);
        PortCeProjectDocumentDelete(document);
    }
    return gEditorFileKinds[index] != FileOpens;
}
void EditorRequestOpen() {
    if (gEditorState.Dirty()) {
        if (!EditorProjectOpens(gEditorFileIndex)) {
            gEditorDialog = DialogNone;
            EditorCue(CueBack);
            return;
        }
        gEditorDialog = DialogDiscardOpen;
        gEditorButtonFocus = 0;
        EditorCue(CueSelect);
    } else if (EditorOpenFile(gEditorFileIndex))
        EditorCue(CueSelect);
    else {
        gEditorDialog = DialogNone;
        EditorCue(CueBack);
    }
}
// A question's answer: No (and Esc) keeps everything as it was; Yes does what the question asked.
void EditorAnswer(bool yes) {
    const EditorDialog question = gEditorDialog;
    if (!yes) {
        gEditorDialog = question == DialogDiscardOpen ? DialogFiles : DialogNone;
        EditorCue(CueBack);
        return;
    }
    EditorCue(CueSelect);
    if (question == DialogDiscardExit)
        EditorLeave();
    else if (!EditorOpenFile(gEditorFileIndex))
        gEditorDialog = DialogNone;
}
void EditorUndo(bool redo) {
    if (gEditorState.Moving()) {
        if (!redo) {
            const int sel = gEditorState.Selected();
            gEditorState.FinishMove(false);
            gEditorPadMove = false;
            EditorCue(CueBack);
            if (sel >= 0 && !ApplyOne(sel)) {
                EditorNote("The selected object is gone, so the editor closed. Your saves are kept.");
                EditorExit();
                return;
            }
            EditorNote("Move cancelled.");
            return;
        }
        EditorCue(CueBack);
        EditorNote("Finish moving the object first.", NoticeUntilMoveEnds);
        return;
    }
    gEditorChoosingReach = false;
    gEditorChoosingPlayerStart = false;
    if (redo ? gEditorState.Redo() : gEditorState.Undo()) {
        EditorCue(CueSelect);
        if (gEditorState.Selected() >= 0 && !ApplyOne(gEditorState.Selected())) {
            EditorNote("The selected object is gone, so the editor closed. Your saves are kept.");
            EditorExit();
        }
    } else {
        EditorCue(CueBack);
        EditorNote(redo ? "There is nothing to redo." : "There is nothing to undo.", NoticeRefusal);
    }
}
// Keep Exit at the end so Up from Save reaches it even when an edit tool is unavailable.
const RouteChoice* EditorRouteChoices(int& count) {
    static const RouteChoice one[] = {{AddStop, "Add Stop 2"}, {MoveFirst, "Move Stop 1"},
        {RemoveFirst, "Remove Stop 1"}, {CloseRoute, "Back"}};
    static const RouteChoice two[] = {{MoveFirst, "Move Stop 1"}, {MoveSecond, "Move Stop 2"},
        {ReverseStops, "Reverse Order: 2 then 1"}, {RemoveFirst, "Remove Stop 1"}, {RemoveSecond, "Remove Stop 2"}, {CloseRoute, "Back"}};
    if (gEditorState.ReachCount() == 2) { count = int(sizeof(two) / sizeof(*two)); return two; }
    count = int(sizeof(one) / sizeof(*one)); return one;
}
void EditorPlaceStop(int index, bool hold = false) {
    gEditorChoosingPlayerStart = false;
    gEditorPlaceHold = hold;
    gEditorDialog = DialogNone;
    gEditorChoosingReach = true;
    gEditorPlaceIndex = index;
    if (hold) { EditorNote("Hold Area: click the centre on visible ground."); return; }
    EditorNote(index == gEditorState.ReachCount() ? (index ? "Click ground to add Stop 2." : "Click ground to add Stop 1.") :
               (index ? "Click ground to move Stop 2." : "Click ground to move Stop 1."));
}
void EditorChooseRoute(int row) {
    int count = 0;
    const RouteChoice* choices = EditorRouteChoices(count);
    if (row < 0 || row >= count) return;
    const RouteAction action = choices[row].action;
    gEditorDialog = DialogNone;
    if (action == AddStop || action == MoveFirst || action == MoveSecond) {
        EditorPlaceStop(action == MoveFirst ? 0 : 1);
    } else if (action == ReverseStops) {
        if (gEditorState.ReverseReach()) EditorNote("Order reversed. Stop 2 is now first.");
    } else if (action == RemoveFirst || action == RemoveSecond) {
        const int index = action == RemoveFirst ? 0 : 1;
        if (gEditorState.SetReachAt(index, nullptr)) EditorNote(index ? "Stop 2 removed. Undo restores it." : "Stop 1 removed. Undo restores it.");
    }
    EditorCue(action == CloseRoute ? CueBack : CueSelect);
}
// Hold Area has its own toolbar button so first use does not require discovering a route menu.
int EditorHoldMenuRows(bool *add, int *index) {
    int n = 0;
    for (int i=0; i<gEditorState.ReachCount(); ++i) if (gEditorState.Hold(i).Enabled()) {
        add[n]=false; index[n]=i;
        snprintf(gHoldAction[n],sizeof(gHoldAction[n]),"Edit Stop %d: Hold Area",i+1); ++n;
    }
    if (gEditorState.ReachCount()<2) {
        add[n]=true; index[n]=gEditorState.ReachCount();
        snprintf(gHoldAction[n],sizeof(gHoldAction[n]),"Add Hold Area at Stop %d",index[n]+1); ++n;
    }
    return n;
}
void EditorHoldInspect(int index) {
    gEditorHoldIndex=index; gEditorDialog=DialogHoldInspector; gEditorRouteRow=0;
}
void EditorOpenHold() {
    gEditorChoosingPlayerStart = false;
    if (gEditorState.Moving()) { EditorCue(CueBack); EditorNote("Finish moving the object first.",NoticeUntilMoveEnds); return; }
    gEditorChoosingReach=false;
    bool add[3]{}; int index[3]{}; const int n=EditorHoldMenuRows(add,index);
    if (!n) { EditorCue(CueBack); EditorNote("Route full. Remove a stop under Reach Point.",NoticeRefusal); return; }
    if (n==1) { if (add[0]) EditorPlaceStop(index[0], true); else EditorHoldInspect(index[0]); }
    else { gEditorDialog=DialogHoldMenu; gEditorRouteRow=0; }
    EditorCue(CueSelect);
}
void EditorHoldChoice(int row) {
    if (gEditorDialog==DialogHoldMenu) {
        bool add[3]{}; int index[3]{}; const int n=EditorHoldMenuRows(add,index);
        if (row==n) { gEditorDialog=DialogNone; EditorCue(CueBack); return; }
        if (row<0 || row>=n) return;
        if (add[row]) EditorPlaceStop(index[row],true); else EditorHoldInspect(index[row]);
        EditorCue(CueSelect); return;
    }
    if (row==2) { gEditorDialog=DialogNone; EditorCue(CueSelect); return; }
    if (row<0 || row>1) return;
    CeHoldSettings h=gEditorState.Hold(gEditorHoldIndex);
    const int seconds[]={1,2,3,5,10,20,30}, radius[]={6,12,18,30,60,120};
    const int *values=row==0?seconds:radius;
    const int n=row==0?int(sizeof(seconds)/sizeof(*seconds)):int(sizeof(radius)/sizeof(*radius));
    int &v=row==0?h.seconds:h.radius; int i=0; while(i<n && values[i]<=v) ++i;
    v=values[i==n?0:i];
    if (gEditorState.SetHoldAt(gEditorHoldIndex,h)) EditorCue(CueMove);
}
void EditorToolbar(int item) {
    if (item == 0)
        EditorCue(EditorSave() ? CueSelect : CueBack);
    else if (item == 1)
        EditorBrowse();
    else if (item == 2)
        EditorUndo(false);
    else if (item == 3)
        EditorUndo(true);
    else if (item == 4) {
        (void)EditorStartPlaytest();
        EditorCue(gEditorPhase == LoadPlaytest ? CueSelect : CueBack);
    }
    else if (item == 5) {
        if (gEditorState.Moving()) {
            EditorCue(CueBack);
            EditorNote("Finish moving the object first.", NoticeUntilMoveEnds);
            return;
        }
        gEditorChoosingReach = false;
        gEditorChoosingPlayerStart = false;
        const int i = gEditorState.Selected();
        const CeEditorObject *object = gEditorState.Object(i);
        if (!object || gEditorSources[i].installedSide < 0 || gEditorSources[i].installedSide > 2) {
            EditorCue(CueBack);
            EditorNote("Select a unit to change its side.", NoticeRefusal);
            return;
        }
        const int original = gEditorSources[i].installedSide;
        const int next = ((object->side < 0 ? original : object->side) + 1) % 3;
        if (gEditorState.ChangeSide(next == original ? -1 : next)) EditorCue(CueSelect);
    } else if (item == 6) {
        if (gEditorState.Moving()) { EditorCue(CueBack); EditorNote("Finish moving the object first.", NoticeUntilMoveEnds); return; }
        gEditorChoosingPlayerStart = false;
        if (!gEditorState.HasReach()) EditorPlaceStop(0);
        else { gEditorChoosingReach = false; gEditorRouteRow = 0; gEditorDialog = DialogRoute; }
        EditorCue(CueSelect);
    } else if (item == 7) {
        EditorOpenHold();
    } else if (item == 8) {
        if (gEditorState.Moving()) {
            EditorCue(CueBack); EditorNote("Finish moving the object first.", NoticeUntilMoveEnds); return;
        }
        if (!gEditorState.PlayerStartAvailable()) {
            EditorCue(CueBack); EditorNote("This map has no editable Player Start.", NoticeRefusal); return;
        }
        gEditorChoosingReach = false; gEditorChoosingPlayerStart = true;
        gEditorDialog = DialogNone; gEditorState.Select(CeEditorState::PlayerStartSelection);
        EditorCue(CueSelect);
        EditorNote("Place Player Start on dry ground or a deck.");
        return;
    }
    if (item == 9) {
        if (gEditorState.Moving()) {
            EditorCue(CueBack);
            EditorNote("Finish moving the object first.", NoticeUntilMoveEnds);
            return;
        }
        gEditorChoosingReach = false;
        gEditorChoosingPlayerStart = false;
        if (gEditorState.Dirty()) {
            gEditorDialog = DialogDiscardExit;
            gEditorButtonFocus = 0;
            EditorCue(CueSelect);
        } else {
            EditorCue(CueBack);
            EditorLeave();
        }
    }
}
// History and unit-side tools are enabled only when their action is available.
bool EditorToolEnabled(int tool) {
    if (tool == 8) return gEditorState.PlayerStartAvailable() && !gEditorState.Moving();
    if (tool == 6 || tool == 7) return !gEditorState.Moving();
    if (tool == 5) {
        const int i = gEditorState.Selected();
        return i >= 0 && !gEditorState.Moving() && gEditorSources[i].installedSide >= 0 && gEditorSources[i].installedSide <= 2;
    }
    return !(tool == 2 && !gEditorState.CanUndo()) && !(tool == 3 && !gEditorState.CanRedo());
}
// The Open list moves by rows (the wheel) or a page (a click on a mark); the focus stays on a row that is shown.
void EditorScrollFiles(int rows) {
    const int last = gEditorFileCount > CeEditorUiRowMax ? gEditorFileCount - CeEditorUiRowMax : 0;
    int top = gEditorFileTop + rows;
    if (top > last) top = last;
    if (top < 0) top = 0;
    if (top == gEditorFileTop) return;
    gEditorFileTop = top;
    if (gEditorFileIndex < top) gEditorFileIndex = top;
    if (gEditorFileIndex > top + CeEditorUiRowMax - 1) gEditorFileIndex = top + CeEditorUiRowMax - 1;
    EditorCue(CueMove);
}
bool EditorTrap(WORD key, int event) {
    if (!gEditorReady || !gEditorInputOwned) return false;
    if (gEditorPhase != Authoring) return true;
    const bool question = gEditorDialog == DialogDiscardExit || gEditorDialog == DialogDiscardOpen;
    if (event == KEY_EVENT_UP) {
        if (key < 256) gEditorKeys[key] = false;
        if (key == DIK_F1) gEditorHelp = false;
        return true;
    }
    if (event == 3) {
        const bool padWasActive = gEditorPadActive;
        if (!(padWasActive && gEditorState.Moving() && key == 0))
            gEditorPadActive = false;
        if ((key == 3 || key == 4) && gEditorDialog == DialogFiles) {
            EditorScrollFiles(key == 3 ? -1 : 1);
            return true;
        }
        if (key == 0 && gEditorState.Moving() && !padWasActive && gEditorMouseDrag && gEditorDialog == DialogNone && !gEditorHelp) {
            gEditorPadMove = false;
            gEditorMouseDrag = false;
            (void)gEditorState.FinishMove(true);
        }
        if (key == 2) gEditorOrbit = false;
        if ((key == 3 || key == 4) && gEditorDialog == DialogNone && !gEditorState.Moving())
            gEditorDistance *= key == 3 ? 0.8f : 1.25f;
        return true;
    }
    if (event == 4) {
        gEditorPadActive = false;
        gEditorPointerSeen = true;
        const float x = float(MOUSE_X), y = float(MOUSE_Y);
        const EditorHitResult hit = EditorHit(x, y);
        if (gEditorDialog == DialogHoldMenu || gEditorDialog == DialogHoldInspector) {
            if (key == 0 && hit.row >= 0) EditorHoldChoice(hit.row);
            return true;
        }
        if (gEditorDialog == DialogRoute) {
            if (key == 0 && hit.row >= 0) EditorChooseRoute(hit.row);
            return true;
        }
        if (gEditorDialog == DialogFiles) {
            if (key == 0 && hit.scroll)
                EditorScrollFiles(hit.scroll * (CeEditorUiRowMax - 1));
            else if (key == 0 && hit.row >= 0 && gEditorFileTop + hit.row < gEditorFileCount) {
                gEditorFileIndex = gEditorFileTop + hit.row;
                EditorRequestOpen();
            }
            return true;
        }
        if (question) {
            if (key == 0 && hit.button >= 0) EditorAnswer(hit.button == 1);
            return true;
        }
        if (gEditorHelp) return true;
        if (key == 0 && hit.tool >= 0) {
            gEditorToolFocus = hit.tool;
            EditorToolbar(hit.tool);
            return true;
        }
        if (key == 0 && gEditorChoosingPlayerStart && !hit.furniture) {
            CeEditorPoint point{};
            if (!EditorPlayerStartAt(x, y, point)) {
                EditorCue(CueBack); EditorNote("Choose dry ground or a visible deck inside the map."); return true;
            }
            if (gEditorState.SetPlayerStart(point)) {
                gEditorChoosingPlayerStart = false; EditorCue(CueSelect);
                EditorNote("Player Start placed. Play Draft to try it; Save keeps it.");
            }
            return true;
        }
        if (key == 0 && gEditorChoosingReach && !hit.furniture) {
            CeEditorPoint target;
            if (!EditorReachPointAt(x,y,&target)) { EditorCue(CueBack); EditorNote("Choose visible ground inside the map."); return true; }
            const bool adding = gEditorPlaceIndex == gEditorState.ReachCount();
            const CeHoldSettings hold{3,18};
            const bool changed = gEditorState.SetReachAt(gEditorPlaceIndex, &target, gEditorPlaceHold ? &hold : nullptr);
            gEditorChoosingReach = false;
            EditorCue(changed ? CueSelect : CueBack);
            if (changed && gEditorPlaceHold) {
                EditorHoldInspect(gEditorPlaceIndex); EditorNote("Hold Area placed. Set its duration and radius."); return true;
            }
            EditorNote(!changed ? "Stop unchanged." : adding ? (gEditorPlaceIndex ? "Stop 2 added. Play Draft with F5." : "Reach Point set. Play Draft with F5.") : "Stop moved. Save your project to keep it.");
            return true;
        }
        if (hit.furniture) {
            // The card holds no controls: a click on it is told why nothing happened, never silently eaten.
            if (key == 0 && hit.card)
                EditorNote("That is the selected object's card. Click the map beside it.");
            return true;
        }
        if (key == 2) {
            gEditorOrbit = true;
            gEditorPointerX = x;
            gEditorPointerY = y;
            return true;
        }
        if (key == 0) {
            if (gEditorState.Moving()) {
                gEditorPadMove = false;
                gEditorMouseDrag = true;
                return true;
            }
            const int before = gEditorState.Selected();
            const int picked = EditorPick(x, y);
            if (!gEditorState.Select(picked)) return true;
            if (picked != before) EditorCue(picked != -1 ? CueSelect : CueMove);
            if (picked != -1) {
                const CeEditorPoint initial = NativePosition(picked);
                CeEditorPoint point;
                if (EditorRay(x, y, initial.z, &point) && EditorBeginMove()) {
                    gEditorMouseDrag = true;
                    gEditorPlaneZ = initial.z;
                    gEditorDragOffset = CeEditorPoint{initial.x - point.x, initial.y - point.y, 0};
                }
            }
        }
        return true;
    }
    if (event != KEY_EVENT_DOWN || key >= 256) return true;
    const bool repeat = gEditorKeys[key];
    gEditorKeys[key] = true;
    if (!gEditorPadTap) gEditorPadActive = false;
    if (repeat) return true;
    if (gEditorHelp) {
        if (key == DIK_ESCAPE || key == DIK_F1) gEditorHelp = false;
        return true;
    }
    if (gEditorDialog == DialogNone && !gEditorHelp) {
        if (gEditorChoosingPlayerStart && key == DIK_ESCAPE) {
            gEditorChoosingPlayerStart = false; EditorCue(CueBack); EditorNote(""); return true;
        }
        if (gEditorChoosingReach && key == DIK_ESCAPE) {
            gEditorChoosingReach = false; EditorCue(CueBack); EditorNote(""); return true;
        }
        if (gEditorChoosingReach && key == DIK_DELETE) {
            if (gEditorPlaceIndex < gEditorState.ReachCount() && gEditorState.SetReachAt(gEditorPlaceIndex, nullptr)) { EditorCue(CueSelect); EditorNote("Stop removed. Undo restores it."); }
            else { EditorCue(CueBack); EditorNote("Stop unchanged."); }
            gEditorChoosingReach = false; return true;
        }
        if (gEditorState.PlayerStartSelected() && key == DIK_DELETE) {
            if (gEditorState.Moving()) {
                EditorCue(CueBack);
                EditorNote("Finish moving the object first.", NoticeUntilMoveEnds);
                return true;
            }
            gEditorChoosingPlayerStart = false;
            if (gEditorState.UseOriginalPlayerStart()) {
                EditorCue(CueSelect);
                EditorNote("Original Player Start restored. Undo restores your edit.");
            } else {
                EditorCue(CueBack);
                EditorNote("Player Start is already at its original place.", NoticeRefusal);
            }
            return true;
        }
    }
    if (gEditorDialog == DialogHoldMenu || gEditorDialog == DialogHoldInspector) {
        bool add[3]{}; int index[3]{};
        const int count=gEditorDialog==DialogHoldInspector ? 3 : EditorHoldMenuRows(add,index)+1;
        if (key==DIK_ESCAPE) { gEditorDialog=DialogNone; EditorCue(CueBack); }
        else if (key==DIK_UP || key==DIK_DOWN) { gEditorRouteRow=(gEditorRouteRow+(key==DIK_DOWN?1:count-1))%count; EditorCue(CueMove); }
        else if (key==DIK_RETURN) EditorHoldChoice(gEditorRouteRow);
        return true;
    }
    if (gEditorDialog == DialogRoute) {
        int count = 0; EditorRouteChoices(count);
        if (key == DIK_ESCAPE) { gEditorDialog = DialogNone; EditorCue(CueBack); }
        else if (key == DIK_UP || key == DIK_DOWN) { gEditorRouteRow = (gEditorRouteRow + (key == DIK_DOWN ? 1 : count - 1)) % count; EditorCue(CueMove); }
        else if (key == DIK_RETURN) EditorChooseRoute(gEditorRouteRow);
        return true;
    }
    if (key == DIK_F1 && gEditorDialog == DialogNone) {
        if (gEditorState.Moving()) {
            EditorCue(CueBack);
            EditorNote("Finish moving the object first.", NoticeUntilMoveEnds);
            return true;
        }
        gEditorHelp = true;
        return true;
    }
    if (question) {
        if (key == DIK_ESCAPE)
            EditorAnswer(false);
        else if (key == DIK_RETURN)
            EditorAnswer(gEditorButtonFocus == 1);
        else if (key == DIK_LEFT || key == DIK_RIGHT || key == DIK_UP || key == DIK_DOWN || key == DIK_TAB) {
            gEditorButtonFocus = 1 - gEditorButtonFocus;
            EditorCue(CueMove);
        }
        return true;
    }
    if (gEditorDialog == DialogFiles) {
        if (key == DIK_ESCAPE) {
            gEditorDialog = DialogNone;
            EditorCue(CueBack);
        } else if (key == DIK_RETURN)
            EditorRequestOpen();
        else if (key == DIK_UP && gEditorFileIndex > 0) {
            --gEditorFileIndex;
            EditorCue(CueMove);
        } else if (key == DIK_DOWN && gEditorFileIndex + 1 < gEditorFileCount) {
            ++gEditorFileIndex;
            EditorCue(CueMove);
        }
        if (gEditorFileIndex < gEditorFileTop) gEditorFileTop = gEditorFileIndex;
        if (gEditorFileIndex >= gEditorFileTop + CeEditorUiRowMax) gEditorFileTop = gEditorFileIndex - (CeEditorUiRowMax - 1);
        return true;
    }
    const bool control = gEditorKeys[DIK_LCONTROL] || gEditorKeys[DIK_RCONTROL];
    const bool shift = gEditorKeys[DIK_LSHIFT] || gEditorKeys[DIK_RSHIFT];
    if (key == DIK_ESCAPE) {
        if (gEditorState.Moving()) {
            gEditorState.FinishMove(false);
            gEditorPadMove = false;
            (void)ApplyOne(gEditorState.Selected());
            EditorCue(CueBack);
        } else
            EditorToolbar(9);
    } else if (control && key == DIK_S)
        EditorToolbar(0);
    else if (control && key == DIK_O)
        EditorToolbar(1);
    else if (control && key == DIK_Z)
        EditorToolbar(2);
    else if (control && key == DIK_Y)
        EditorToolbar(3);
    else if (key == DIK_F5)
        EditorToolbar(4);
    else if (key == DIK_T)
        EditorToolbar(5);
    else if (key == DIK_R)
        EditorToolbar(6);
    else if (key == DIK_H)
        EditorToolbar(7);
    else if (key == DIK_P)
        EditorToolbar(8);
    else if (key == DIK_UP || key == DIK_DOWN) {
        gEditorToolFocus = (gEditorToolFocus + (key == DIK_DOWN ? 1 : 9)) % 10;
        for (int skip = 0; skip < 9 && !EditorToolEnabled(gEditorToolFocus); ++skip)   // past Undo or Redo when there is none
            gEditorToolFocus = (gEditorToolFocus + (key == DIK_DOWN ? 1 : 9)) % 10;
        EditorCue(CueMove);
    } else if (key == DIK_RETURN)
        EditorToolbar(gEditorToolFocus);
    else if (key == DIK_TAB) {
        gEditorChoosingReach = false;
        gEditorChoosingPlayerStart = false;
        EditorCue(gEditorState.Cycle(shift ? -1 : 1) ? CueMove : CueBack);
    }
    else if (key == DIK_C) {
        if (gEditorState.Selected() != -1 && EditorFit(true))
            EditorCue(CueMove);
        else if (gEditorState.Selected() != -1) {
            EditorCue(CueBack);
            EditorNote("This object is far outside the level, so the view stays here.", NoticeRefusal);
        } else {
            EditorCue(CueBack);
            EditorNote("Select an object first.", NoticeRefusal);
        }
    } else if (key == DIK_HOME) {
        EditorFit(false);
        EditorCue(CueMove);
    } else if (key == DIK_PRIOR || key == DIK_NEXT) {
        if (gEditorState.Selected() != -1 && gEditorState.Moving()) {
            CeEditorPoint position = (gEditorState.PlayerStartSelected() ? gEditorState.PlayerStart() : gEditorState.Object(gEditorState.Selected())->position);
            const float step = shift ? 5 : 0.5f;
            const float dz = key == DIK_PRIOR ? -step : step;
            position.z += dz;
            if (gEditorState.Move(position)) {
                gEditorPlaneZ += dz;
                (void)ApplyOne(gEditorState.Selected());
                EditorCue(CueMove);
            } else {
                EditorCue(CueBack);
            }
        } else if (gEditorState.Selected() != -1 && EditorBeginMove()) {
            CeEditorPoint position = (gEditorState.PlayerStartSelected() ? gEditorState.PlayerStart() : gEditorState.Object(gEditorState.Selected())->position);
            const float step = shift ? 5 : 0.5f;
            position.z += key == DIK_PRIOR ? -step : step;
            (void)gEditorState.Move(position);
            (void)gEditorState.FinishMove(true);
            (void)ApplyOne(gEditorState.Selected());
            EditorCue(CueMove);
        } else {
            EditorCue(CueBack);
            EditorNote("Select an object first.", NoticeRefusal);
        }
    }
    return true;
}
// The front end's three cues (the pause menu plays them in a level too): move, select, back.
void EditorCue(int cue) {
    CFrontEnd::PlaySound(cue == CueSelect ? FES_SELECT : cue == CueBack ? FES_BACK : FES_MOVE);
}
void EditorGamepadUpdate(float elapsed) {
    static std::uint64_t sessionSeen = 0;
    static unsigned prevButtons = 0;
    static DWORD prevPov = 0xFFFFFFFFu;
    static float lastMouseX = 0.f, lastMouseY = 0.f;
    static bool mousePosSeeded = false;
    if (sessionSeen != gEditorSessionToken) {
        sessionSeen = gEditorSessionToken;
        gEditorPadActive = false;
        gEditorPadMove = false;
        prevButtons = 0;
        prevPov = 0xFFFFFFFFu;
        mousePosSeeded = false;
    }
    bool anyPad = false, padInput = false;
    bool kbmInput = LT.mMouseButtons != 0;
    for (int k = 0; !kbmInput && k < 256; ++k)
        if (gEditorKeys[k]) kbmInput = true;
    // Authoring calls ShutdownMouse() and reads windowed MOUSE_X/MOUSE_Y, so track pixel motion directly.
    const float mx = float(MOUSE_X), my = float(MOUSE_Y);
    const bool mouseMoved = mousePosSeeded && (mx != lastMouseX || my != lastMouseY);
    if (mouseMoved && !(gEditorPadMove && gEditorState.Moving())) kbmInput = true;
    lastMouseX = mx;
    lastMouseY = my;
    mousePosSeeded = true;

    const DIJOYSTATE2 *joy = nullptr;
    bool rotated = true;
    if (LT.mJoypads > 0 && LT.mUnknown33474[0] && LT.mJoyState[0]) {
        anyPad = true;
        joy = LT.mJoyState[0];
        rotated = LT.mJoypadIsNewtype[0] != FALSE;
    }
    auto deadAxis = [](LONG v) -> float {
        const float f = float(v);
        if (std::fabs(f) <= 220.f) return 0.f;
        const float out = (f - (f > 0.f ? 220.f : -220.f)) / 780.f;
        return out < -1.f ? -1.f : out > 1.f ? 1.f : out;
    };
    unsigned buttons = 0;
    DWORD pov = 0xFFFFFFFFu;
    if (joy) {
        for (int b = 0; b < 32; ++b)
            if (joy->rgbButtons[b] & 0x80) buttons |= 1u << b;
        pov = joy->rgdwPOV[0];
        const bool povOn = (pov & 0xFFFFu) != 0xFFFFu;
        if (buttons || povOn || std::abs(joy->lX) > 250 || std::abs(joy->lY) > 250 ||
            std::abs(joy->lZ) > 250 || std::abs(joy->lRz) > 250)
            padInput = true;
    }
    const bool wasPadActive = gEditorPadActive;
    gEditorPadActive = CeEditorUiUpdateDevice(gEditorPadActive, anyPad, padInput, kbmInput);
    if (wasPadActive && !gEditorPadActive && gEditorState.Moving() && gEditorState.Selected() != -1) {
        gEditorPadMove = false;
        const CeEditorPoint cur = NativePosition(gEditorState.Selected());
        gEditorPlaneZ = cur.z;
        CeEditorPoint rayPt;
        if (EditorRay(mx, my, cur.z, &rayPt))
            gEditorDragOffset = CeEditorPoint{cur.x - rayPt.x, cur.y - rayPt.y, 0.f};
    }
    if (mouseMoved && !gEditorPadActive) gEditorPointerSeen = true;
    if (!joy) {
        prevButtons = 0;
        prevPov = 0xFFFFFFFFu;
        return;
    }
    const unsigned pressed = buttons & ~prevButtons;
    const bool povChanged = pov != prevPov;
    prevButtons = buttons;
    prevPov = pov;

    const unsigned bitB = 1u << (rotated ? 0 : 1), bitX = 1u << (rotated ? 1 : 2),
                   bitY = 1u << (rotated ? 2 : 3), bitA = 1u << (rotated ? 3 : 0),
                   bitBack = 1u << 4, bitStart = 1u << 6, bitL3 = 1u << 7,
                   bitLb = 1u << 9, bitRb = 1u << 10,
                   bitLt = 1u << 16, bitRt = 1u << 17;
    if (gEditorDialog == DialogNone && !gEditorHelp) {
        const float lx = deadAxis(joy->lX), ly = -deadAxis(joy->lY);
        if (gEditorState.Moving()) {
            if (lx != 0.f || ly != 0.f) {
                FMatrix turn;
                turn.MakeRotationYawF(gEditorYaw);
                const FVector step = turn * FVector(lx, ly, 0);
                const float speed = gEditorDistance * elapsed * 0.5f;
                const int sel = gEditorState.Selected();
                if (sel != -1) {
                    const CeEditorBounds bounds = EditorBounds();
                    const bool isStart = gEditorState.PlayerStartSelected();
                    CeEditorPoint pos = isStart
                        ? gEditorState.PlayerStart()
                        : gEditorState.Object(sel)->position;
                    pos.x += step.X * speed;
                    pos.y += step.Y * speed;
                    if (isStart) {
                        if (pos.x < bounds.minX) pos.x = bounds.minX;
                        if (pos.x > bounds.maxX) pos.x = bounds.maxX;
                        if (pos.y < bounds.minY) pos.y = bounds.minY;
                        if (pos.y > bounds.maxY) pos.y = bounds.maxY;
                    } else {
                        const CeEditorPoint init = gEditorSources[sel].initial, actor = gEditorActorStart[sel];
                        float wx = actor.x + pos.x - init.x, wy = actor.y + pos.y - init.y;
                        if (wx < bounds.minX) wx = bounds.minX;
                        if (wx > bounds.maxX) wx = bounds.maxX;
                        if (wy < bounds.minY) wy = bounds.minY;
                        if (wy > bounds.maxY) wy = bounds.maxY;
                        pos.x = wx - actor.x + init.x;
                        pos.y = wy - actor.y + init.y;
                    }
                    if (gEditorState.Move(pos)) (void)ApplyOne(sel);
                }
            }
        } else if (lx != 0.f || ly != 0.f) {
            FMatrix turn;
            turn.MakeRotationYawF(gEditorYaw);
            const FVector step = turn * FVector(lx, ly, 0);
            const float speed = gEditorDistance * elapsed * 0.7f;
            gEditorFocus.x += step.X * speed;
            gEditorFocus.y += step.Y * speed;
        }
        const float rx = deadAxis(joy->lZ), ry = deadAxis(joy->lRz);
        if (rx != 0.f) gEditorYaw += rx * elapsed * 1.8f;
        if (ry != 0.f) gEditorPitch += ry * elapsed * 1.4f;
        if (buttons & bitRt) gEditorDistance *= std::pow(0.45f, elapsed);
        if (buttons & bitLt) gEditorDistance *= std::pow(2.2f, elapsed);
    }
    auto tap = [](WORD key) {
        gEditorPadTap = true;
        (void)EditorTrap(key, KEY_EVENT_DOWN);
        (void)EditorTrap(key, KEY_EVENT_UP);
        gEditorPadTap = false;
    };
    if (povChanged && (pov & 0xFFFFu) != 0xFFFFu) {
        if (pov >= 31500u || pov <= 4500u) tap(DIK_UP);
        else if (pov >= 13500u && pov <= 22500u) tap(DIK_DOWN);
        else if (pov > 4500u && pov < 13500u) tap(DIK_RIGHT);
        else if (pov > 22500u && pov < 31500u) tap(DIK_LEFT);
    }
    if (pressed & bitA) {
        if (gEditorDialog == DialogNone && !gEditorHelp && gEditorChoosingPlayerStart) {
            CeEditorPoint point{};
            const float cx = float(PLATFORM.GetWindowWidth()) * 0.5f, cy = float(PLATFORM.GetWindowHeight()) * 0.5f;
            if (!EditorPlayerStartAt(cx, cy, point)) {
                EditorCue(CueBack); EditorNote("Choose dry ground or a visible deck inside the map.");
            } else if (gEditorState.SetPlayerStart(point)) {
                gEditorChoosingPlayerStart = false; EditorCue(CueSelect);
                EditorNote("Player Start placed. Play Draft to try it; Save keeps it.");
            }
        } else if (gEditorDialog == DialogNone && !gEditorHelp && gEditorChoosingReach) {
            CeEditorPoint target{};
            const float cx = float(PLATFORM.GetWindowWidth()) * 0.5f, cy = float(PLATFORM.GetWindowHeight()) * 0.5f;
            if (!EditorReachPointAt(cx, cy, &target)) {
                EditorCue(CueBack); EditorNote("Choose visible ground inside the map.");
            } else {
                const bool adding = gEditorPlaceIndex == gEditorState.ReachCount();
                const CeHoldSettings hold{3, 18};
                const bool changed = gEditorState.SetReachAt(gEditorPlaceIndex, &target, gEditorPlaceHold ? &hold : nullptr);
                gEditorChoosingReach = false;
                EditorCue(changed ? CueSelect : CueBack);
                if (changed && gEditorPlaceHold) {
                    EditorHoldInspect(gEditorPlaceIndex); EditorNote("Hold Area placed. Set its duration and radius.");
                } else {
                    EditorNote(!changed ? "Stop unchanged." : adding ? (gEditorPlaceIndex ? "Stop 2 added. Play Draft with F5." : "Reach Point set. Play Draft with F5.") : "Stop moved. Save your project to keep it.");
                }
            }
        } else if (gEditorDialog == DialogNone && !gEditorHelp && gEditorState.Moving()) {
            gEditorPadMove = false;
            (void)gEditorState.FinishMove(true);
            EditorCue(CueSelect);
        } else {
            tap(DIK_RETURN);
        }
    }
    if (pressed & bitB) tap(DIK_ESCAPE);
    if (gEditorDialog == DialogNone && !gEditorHelp) {
        if (pressed & bitL3) {
            if (gEditorState.Moving()) {
                gEditorPadMove = false;
                (void)gEditorState.FinishMove(true);
                EditorCue(CueSelect);
            } else if (!gEditorChoosingPlayerStart && !gEditorChoosingReach && gEditorState.Selected() != -1 && EditorBeginMove()) {
                gEditorPadMove = true;
                const CeEditorPoint initial = NativePosition(gEditorState.Selected());
                CeEditorPoint point;
                if (EditorRay(float(MOUSE_X), float(MOUSE_Y), initial.z, &point))
                    gEditorDragOffset = CeEditorPoint{initial.x - point.x, initial.y - point.y, 0.f};
                EditorCue(CueSelect);
                EditorNote("Left Stick moves object. A confirms, B cancels.", NoticeUntilMoveEnds);
            }
        }
        if (pressed & bitRb) tap(DIK_TAB);
        if (pressed & bitLb) {
            gEditorChoosingReach = false;
            gEditorChoosingPlayerStart = false;
            EditorCue(gEditorState.Cycle(-1) ? CueMove : CueBack);
        }
        if (pressed & bitX) tap(DIK_PRIOR);
        if (pressed & bitY) tap(DIK_NEXT);
        if (pressed & bitBack) tap(DIK_DELETE);
        if (pressed & bitStart) tap(DIK_F5);
    }
}
CeEditorUiModel gEditorModel;
CeEditorUiLayout gEditorLayout;
CeEditorUiMarker gEditorMarkers[CeEditorState::ObjectLimit];
char gEditorRows[CeEditorUiRowMax][CeEditorUiTextMax];
const char *const kEditorTools[10] = {"Save", "Open", "Undo", "Redo", "Play Draft", "Change Side", "Reach Point", "Hold Area", "Player Start", "Exit"};
EditorHitResult EditorHit(float x, float y) {
    EditorHitResult result = {-1, -1, -1, false, 0};
    if (!gEditorLayoutValid) return result;
    result.tool = CeEditorUiToolAt(gEditorLayout, gEditorModel, x, y);
    result.row = CeEditorUiRowAt(gEditorLayout, gEditorModel, x, y);
    result.button = CeEditorUiButtonAt(gEditorLayout, gEditorModel, x, y);
    result.furniture = CeEditorUiOverFurniture(gEditorLayout, gEditorModel, x, y);
    result.scroll = CeEditorUiScrollAt(gEditorLayout, gEditorModel, x, y);
    result.card = CeEditorUiWhatShows(gEditorModel).card && gEditorLayout.card.Contains(x, y);
    return result;
}
// Start beside an editable object. On reopen, prefer one whose position or side the project changed.
// Keep the opening camera's bearing; returning from play keeps the player's own view and never calls this.
void EditorStartView() {
    EditorFit(false);  // Leave the bounded overview when every editable object is outside the map.
    CeEditorPoint origin = gEditorFocus;
    gEditorYaw = 0;
    gEditorPitch = 0.75f;
    if (CCamera *start = gEditorPreviousCamera) {
        const FVector position = start->GetPos();
        if (CeEditorState::Valid(Point(position))) origin = Point(position);
        const FVector forward = start->GetOrientation() * FVector(0, 1, 0);
        if (std::isfinite(forward.X) && std::isfinite(forward.Y) && forward.X * forward.X + forward.Y * forward.Y > 0.0001f) {
            FMatrix turn;
            turn.MakeRotationYawF(0);
            const FVector ahead = turn * FVector(0, 1, 0);
            turn.MakeRotationYawF(1.5707963f);
            const FVector side = turn * FVector(0, 1, 0);
            gEditorYaw = atan2f(forward.X * side.X + forward.Y * side.Y, forward.X * ahead.X + forward.Y * ahead.Y);
        }
    }
    if (gEditorState.HasReach()) {
        if (gEditorState.ReachCount() == 2) {
            const CeEditorPoint stops[] = {gEditorState.Reach(0), gEditorState.Reach(1)};
            if (CeEditorFitView(stops, 2, EditorBounds(), &gEditorFocus, &gEditorDistance)) return;
        }
        gEditorFocus=gEditorState.Reach(); gEditorDistance=45; return;
    }
    const CeEditorBounds bounds = EditorBounds();
    int nearest = -1;
    bool nearestMoved = false;
    float nearestDistance = 0;
    for (int i = 0; i < gEditorState.Count(); ++i) {
        const CeEditorObject *object = gEditorState.Object(i);
        if (!object->available) continue;
        const CeEditorPoint p = NativePosition(i);
        if (!CeEditorInside(bounds, p)) continue;
        CeEditorPoint framed = p;
        CeEditorClampView(bounds, &framed, nullptr);
        if (!SamePoint(p, framed)) continue;
        const bool moved = !SamePoint(object->position, gEditorSources[i].initial) || object->side >= 0;
        const float x = p.x - origin.x, y = p.y - origin.y, z = p.z - origin.z;
        const float distance = x * x + y * y + z * z;
        if (nearest < 0 || (moved && !nearestMoved) || (moved == nearestMoved && distance < nearestDistance)) {
            nearest = i;
            nearestDistance = distance;
            nearestMoved = moved;
        }
    }
    if (nearest < 0) return;
    gEditorFocus = NativePosition(nearest);
    gEditorDistance = 45;
    if (nearestMoved) gEditorState.Select(nearest);
}
void Latin1(const WCHAR *text, char *out, size_t capacity) {
    size_t n = 0;
    for (; text && text[n] && n + 1 < capacity; ++n) out[n] = text[n] < 256 ? char(text[n]) : ' ';
    if (capacity) out[n] = 0;
}
void LevelName(int level, char *out, size_t capacity) { Latin1(CFrontEndText::GetLevelName(level), out, capacity); }
// A project's name for the player: "Project 3" for the editor's own saves, the level's name when it is another level's,
// else the file's name without its ending, with dashes and underscores as spaces.
void ProjectLabel(const char *file, char *out, size_t capacity) {
    int level = 0, serial = 0;
    char tail[16] = "";
    if (!file || !file[0] || !strcmp(file, "New project")) {
        snprintf(out, capacity, "New Project");
        return;
    }
    if (sscanf(file, "level-%4d-%6d%15s", &level, &serial, tail) == 3 && !strcmp(tail, ".ceproject") && serial > 0) {
        if (level == gEditorLevel)
            snprintf(out, capacity, "Project %d", serial);
        else {
            char name[64];
            LevelName(level, name, sizeof(name));
            snprintf(out, capacity, "Project %d, %s", serial, name);
        }
        return;
    }
    size_t n = 0;
    for (; file[n] && n + 1 < capacity && strcmp(file + n, ".ceproject"); ++n)
        out[n] = file[n] == '-' || file[n] == '_' ? ' ' : file[n];
    out[n] = 0;
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = char(out[0] - 'a' + 'A');   // a name, as the game writes one
}
// A name longer than its panel is cut and marked, never squeezed (L4).
void Cut(const char *text, char *out, size_t keep) {
    const size_t n = strlen(text);
    if (n <= keep) snprintf(out, CeEditorUiTextMax, "%s", text);
    else snprintf(out, CeEditorUiTextMax, "%.*s...", int(keep - 3), text);
}
float Tenth(float value) {
    const float rounded = floorf(value * 10.f + 0.5f) / 10.f;
    return rounded == 0.f ? 0.f : rounded;
}
void ThingWords(CThing *thing, char *kind, size_t kindCapacity, char *side, size_t sideCapacity) {
    snprintf(kind, kindCapacity, "Object");
    snprintf(side, sideCapacity, "None");
    if (!thing || !thing->IsA(THING_TYPE_UNIT)) return;
    CUnit *unit = static_cast<CUnit *>(thing);
    if (CUnitData *data = unit->GetUnitData())
        if (data->mName && data->mName[0]) {
            size_t n = 0;
            for (; data->mName[n] && n + 1 < kindCapacity; ++n) {
                const char c = data->mName[n];
                kind[n] = c == '_' || c == '-' ? ' ' : c;
            }
            kind[n] = 0;
        }
    const int allegiance = int(unit->GetAllegiance());
    snprintf(side, sideCapacity, "%s", allegiance == 0 ? "Forseti" : allegiance == 1 ? "Muspell" : allegiance == 2 ? "Neutral" : "None");
}
// In a play test: the game shows a screen of its own (the opening pan, its pause menu and the pages under it, the debrief),
// over which nothing of the editor's is drawn.
bool EditorGameScreen() {
    return GAME.GetGameState() != GAME_STATE_PLAYING || GAME.IsPaused() || GAMEINTERFACE.IsMenuUp() || GAMEINTERFACE.mShowJoypad;
}
unsigned long long gEditorRenderNs = 0;
void EditorBuildModel(CeEditorUiModel &m) {
    memset(&m, 0, sizeof(m));
    m.width = float(PLATFORM.GetWindowWidth());
    m.height = float(PLATFORM.GetWindowHeight());
    const unsigned long long now = EditorNowNs();
    const bool gameScreen = PortCeEditorPlaying() && EditorGameScreen();
    // A notice the game's own screen hides waits under it: its time runs only while it can be read.
    if (gameScreen && gEditorNote[0] && gEditorRenderNs && now > gEditorRenderNs && gEditorNoticeUntil > gEditorRenderNs)
        gEditorNoticeUntil += now - gEditorRenderNs;
    gEditorRenderNs = now;
    if (gEditorNote[0] && now < gEditorNoticeUntil) {
        // The HUD's own opening and closing: a quarter of a second open, half a second closed.
        m.notice = gEditorNote;
        const float left = float(gEditorNoticeUntil - now) * 1e-9f, age = 5.f - left;
        const float opening = age < 0.25f ? age / 0.25f : 1.f, closing = left < 0.5f ? left / 0.5f : 1.f;
        m.noticeAlpha = opening < closing ? opening : closing;
    }
    if (PortCeEditorPlaying()) {
        m.playing = true;
        m.gameScreen = gameScreen;
        if (gEditorTestReach.enabled) {
            char title[32] = "Reach Point";
            if (gEditorReachLength == 2) snprintf(title, sizeof(title), "Stop %d of 2", gEditorReachStage + 1);
            if (gEditorReached) snprintf(m.objective, sizeof(m.objective), "%s", gEditorReachLength == 2 ? "Project complete: both stops" : gEditorTestReach.hold.Enabled() ? "Project complete: Hold Area" : "Project complete: Reach Point");
            else if (gEditorTestReach.hold.Enabled()) {
            const CeHoldSettings h=gEditorTestReach.hold;
            const char* state=gEditorHoldInside ? "Holding" : "Enter area";
            if (gEditorReachLength==2) snprintf(m.objective,sizeof(m.objective),"Stop %d: %s  %.1f / %d s  (%d m)",gEditorReachStage+1,state,gEditorHoldElapsed,h.seconds,h.radius);
            else snprintf(m.objective,sizeof(m.objective),"Hold Area: %s  %.1f / %d s  (%d m)",state,gEditorHoldElapsed,h.seconds,h.radius);
        } else if (gEditorSecondNeedsExit) snprintf(m.objective, sizeof(m.objective), "Stop 2: leave, then return");
            else if (gEditorReachPlayerValid && gEditorReachAtSurface)
                snprintf(m.objective, sizeof(m.objective), "%s: near the surface  %.0f m", title, gEditorReachDistance);
            else if (gEditorReachPlayerValid) {
                static const char* const compass[]={"ahead","ahead right","right","behind right","behind","behind left","left","ahead left"};
                snprintf(m.objective, sizeof(m.objective), "%s: %s  %.0f m", title, compass[gEditorReachBearing], gEditorReachDistance);
            } else snprintf(m.objective, sizeof(m.objective), "%s: within %d m", title, int(kEditorReachRadius));
        }
        return;
    }
    LevelName(gEditorLevel, m.level, sizeof(m.level));
    ProjectLabel(gEditorFilename, m.project, sizeof(m.project));

    m.toolCount = 10;
    for (int i = 0; i < m.toolCount; ++i) {
        m.tools[i] = kEditorTools[i];
        m.toolEnabled[i] = EditorToolEnabled(i);
    }
    m.toolFocus = gEditorDialog == DialogNone ? gEditorToolFocus : -1;
    const int selected = gEditorState.Selected();
    if (const CeEditorObject *object = gEditorState.Object(selected)) {
        m.card = true;
        Cut(object->source, m.cardName, 28);
        ThingWords(LiveActor(selected), m.cardKind, sizeof(m.cardKind), m.cardSide, sizeof(m.cardSide));
        if (object->side >= 0) {
            const char *const sides[] = {"Forseti", "Muspell", "Neutral"};
            snprintf(m.cardSide, sizeof(m.cardSide), "%s", sides[object->side]);
        }
        snprintf(m.cardPosition, sizeof(m.cardPosition), "%.1f, %.1f m", Tenth(object->position.x), Tenth(object->position.y));
        snprintf(m.cardHeight, sizeof(m.cardHeight), "%.1f m", Tenth(-object->position.z));
    }
    m.placingPlayerStart = gEditorChoosingPlayerStart;
    m.playerStartSelected = gEditorState.PlayerStartSelected();
    CeViewPoint startPixel{};
    if (gEditorState.PlayerStartAvailable() && EditorPixel(CeEditorState::PlayerStartSelection, &startPixel)) {
        m.playerStartMarker = true; m.playerStartX = startPixel.x; m.playerStartY = startPixel.y;
    }
    if (m.playerStartSelected) {
        m.card = true;
        snprintf(m.cardName, sizeof(m.cardName), "Player Start");
        snprintf(m.cardKind, sizeof(m.cardKind), "Battle Engine walker");
        snprintf(m.cardSide, sizeof(m.cardSide), "Player");
        const CeEditorPoint start = gEditorState.PlayerStart();
        snprintf(m.cardPosition, sizeof(m.cardPosition), "%.1f, %.1f m", Tenth(start.x), Tenth(start.y));
        snprintf(m.cardHeight, sizeof(m.cardHeight), "%.1f m", Tenth(-start.z));
        snprintf(m.cardNote, sizeof(m.cardNote), "Where the Battle Engine begins. It moves with the deck it is on.");
    } else if (m.card) {
        const int side = selected >= 0 ? gEditorSources[selected].installedSide : -1;
        snprintf(m.cardNote, sizeof(m.cardNote), "%s",
                 side >= 0 && side <= 2 ? "Move it or change its side." : "You can move it.");
    }
    CeEditorUiDescribeEditing(m, gEditorState.Dirty() || !gEditorFileCurrent,
                              gEditorChoosingReach, gEditorPlaceHold, gEditorState.Moving());
    for (int i = 0; i < gEditorState.ReachCount(); ++i) {
        const CeEditorPoint target = gEditorState.Reach(i); CeViewPoint pixel;
        const bool visible = gEditorViewValid && CeViewProject(gEditorView, gEditorProjection, gEditorViewport,
            CeViewPoint{target.x,target.y,target.z}, &pixel);
        if (i == 0) { m.reachMarker = visible; if (visible) { m.reachX = pixel.x; m.reachY = pixel.y; } }
        else { m.secondMarker = visible; if (visible) { m.secondX = pixel.x; m.secondY = pixel.y; } }
    }
    m.routeLength = gEditorState.ReachCount();
    m.reachHold = gEditorState.Hold(0).Enabled();
    m.secondHold = gEditorState.Hold(1).Enabled();
    m.gamepad = gEditorPadActive;
    if (gEditorChoosingReach) snprintf(m.objective, sizeof(m.objective), "%s Stop %d: click ground",
        gEditorPlaceIndex == gEditorState.ReachCount() ? "Add" : "Move", gEditorPlaceIndex + 1);
    else if (gEditorState.Hold(0).Enabled() || gEditorState.Hold(1).Enabled()) {
        if (m.routeLength==2) snprintf(m.objective,sizeof(m.objective),"%s then %s",gEditorState.Hold(0).Enabled()?"Hold Area":"Reach Point",gEditorState.Hold(1).Enabled()?"Hold Area":"Reach Point");
        else { const CeHoldSettings h=gEditorState.Hold(0); snprintf(m.objective,sizeof(m.objective),"Hold Area: %d s inside %d m",h.seconds,h.radius); }
    } else if (m.routeLength == 2) snprintf(m.objective, sizeof(m.objective), "Reach route: Stop 1, then Stop 2");
    else if (gEditorState.HasReach()) snprintf(m.objective, sizeof(m.objective), "Reach Point: within %d m", int(kEditorReachRadius));
    int markers = 0;
    for (int i = 0; i < gEditorState.Count() && markers < CeEditorState::ObjectLimit; ++i) {
        CeViewPoint pixel;
        if (!EditorPixel(i, &pixel) || pixel.x < 0 || pixel.y < 0 || pixel.x >= m.width || pixel.y >= m.height) continue;
        gEditorMarkers[markers++] = CeEditorUiMarker{pixel.x, pixel.y, i == selected};
    }
    m.markers = gEditorMarkers;
    m.markerCount = markers;
    if (gEditorDialog == DialogNone && !gEditorHelp) {
        CeEditorUiFillPrompts(m, gEditorState.Moving(), gEditorChoosingPlayerStart, gEditorChoosingReach,
                              gEditorPlaceIndex < gEditorState.ReachCount(), gEditorState.PlayerStartSelected(),
                              selected >= 0);
    }
    m.help = gEditorHelp && gEditorDialog == DialogNone;
    if (gEditorDialog == DialogHoldMenu) {
        bool add[3]{}; int index[3]{}; const int n=EditorHoldMenuRows(add,index);
        m.dialog=CeEditorUiList; m.dialogTitle="Hold Area"; m.dialogNote="Choose a stop";
        m.rowCount=n+1; m.rowFocus=gEditorRouteRow;
        for(int i=0;i<n;++i) m.rows[i]=gHoldAction[i];
        m.rows[n]="Back";
    } else if (gEditorDialog == DialogHoldInspector) {
        const CeHoldSettings h=gEditorState.Hold(gEditorHoldIndex);
        snprintf(gHoldTitle,sizeof(gHoldTitle),"Hold Area: Stop %d",gEditorHoldIndex+1);
        snprintf(gHoldAction[0],sizeof(gHoldAction[0]),"Duration: %d s   (change)",h.seconds);
        snprintf(gHoldAction[1],sizeof(gHoldAction[1]),"Radius: %d m   (change)",h.radius);
        m.dialog=CeEditorUiList; m.dialogTitle=gHoldTitle; m.dialogNote="Leaving the area restarts the timer";
        m.rowCount=3; m.rowFocus=gEditorRouteRow; m.rows[0]=gHoldAction[0]; m.rows[1]=gHoldAction[1]; m.rows[2]="Done";
    } else if (gEditorDialog == DialogRoute) {
        int count = 0; const RouteChoice* choices = EditorRouteChoices(count);
        m.dialog = CeEditorUiList; m.dialogTitle = "Reach Point Route";
        m.dialogNote = "Choose a change or go back";
        m.rowCount = count; m.rowFocus = gEditorRouteRow;
        for (int i = 0; i < count; ++i) m.rows[i] = choices[i].label;
    }
    if (gEditorDialog == DialogFiles || gEditorDialog == DialogDiscardOpen) {
        m.dialog = CeEditorUiList;
        m.dialogTitle = "Open Project";
        m.dialogNote = "Enter Open      Esc Back";
        for (int row = 0; row < CeEditorUiRowMax && gEditorFileTop + row < gEditorFileCount; ++row) {
            const int index = gEditorFileTop + row;
            char label[CeEditorUiTextMax];
            ProjectLabel(gEditorFiles[index].name, label, sizeof(label));
            m.rowGrey[row] = EditorFileGrey(index);
            // Another level's project under a name of its own says which level it is for, as the editor's own saves do.
            if (gEditorFileKinds[index] == FileOtherLevel && !strchr(label, ',')) {
                char name[64], named[CeEditorUiTextMax];
                LevelName(gEditorFileLevels[index], name, sizeof(name));
                if (name[0]) {
                    snprintf(named, sizeof(named), "%s, %s", label, name);
                    snprintf(label, sizeof(label), "%s", named);
                }
            }
            Cut(label, gEditorRows[row], 36);
            m.rows[row] = gEditorRows[row];
            m.rowCount = row + 1;
        }
        m.rowFocus = gEditorFileIndex - gEditorFileTop;
        m.moreAbove = gEditorFileTop > 0;
        m.moreBelow = gEditorFileTop + CeEditorUiRowMax < gEditorFileCount;
    }
    if (gEditorDialog == DialogDiscardExit || gEditorDialog == DialogDiscardOpen) {
        m.dialog = CeEditorUiQuestion;
        m.dialogTitle = gEditorDialog == DialogDiscardExit ? "Leave without saving?" : "Open without saving?";
        m.dialogText = "Changes since your last save will be lost.";
        m.buttonFocus = gEditorButtonFocus;
    }
    m.pointer = gEditorPointerSeen && !gEditorHelp && !gEditorPadActive;
    m.pointerX = float(MOUSE_X);
    m.pointerY = float(MOUSE_Y);
}
} // namespace
extern "C" bool PortCeEditorActive(void) { return gEditorRequested; }

void PortCeEditorFilterSave(CCareer* career, void* destination) {
    if (!gEditorRequested || !gEditorCareer || !career || !destination) return;
    CCareer restored;
    memcpy(&restored, gEditorCareer, sizeof(restored));
    EditorCopyCareerOptions(restored, *career);
    memcpy(destination, &restored, sizeof(restored));
}
extern "C" std::uint64_t PortCeEditorWorldViewToken() {
    return gEditorReady && gEditorPhase == Authoring && gEditorCamera && gEditorLevel == GAME.GetCurrentLevel() &&
                   GAME.GetCamera(0) == gEditorCamera
               ? gEditorSessionToken
               : 0;
}
const char *PortCeEditorMessage() { return gEditorNote; }
bool PortCeEditorPlaying() { return gEditorRequested && gEditorPhase == Playing; }
bool PortCeEditorMousePixels() { return (gEditorReady && gEditorPhase == Authoring) || gEditorOpening; }
bool PortCeEditorSuppressFlightHud() { return gEditorReady && gEditorPhase == Authoring && gEditorCamera != nullptr; }
int PortCeEditorResolveQuit(int quit) {
    if (!PortCeEditorPlaying()) return quit;
    if (quit == QT_RESTART_LEVEL) {
        char error[192];
        CeProjectPlayback *playback = PortCeProjectPlaybackNew(gEditorPlayDocument, error, sizeof(error));
        if (!playback) {
            EditorNote(error);
            return QT_USER_QUIT_TO_FRONTEND;
        }
        PortCeProjectPlaybackDelete(gEditorPlayback);
        gEditorPlayback = playback;
        if (!EditorPrepareReload(LoadPlaytest)) return QT_USER_QUIT_TO_FRONTEND;
        EditorNote("");
    } else if (quit == QT_QUIT_TO_FRONTEND && FRONTEND.mQuit == -1) {
        // The window was closed: the game's own close goes to the front end, which then ends the program. A play test
        // never turns that into a return to the editor; the career stays fenced until the program has ended.
        return quit;
    } else if (quit == QT_USER_QUIT_TO_FRONTEND || quit == QT_QUIT_TO_FRONTEND ||
               quit == QT_USER_QUIT_TO_TITLE_SCREEN || quit == QT_QUIT_TIMEOUT) {
        return EditorReturnToAuthoring() ? QT_RESTART_LEVEL : QT_USER_QUIT_TO_FRONTEND;
    }
    return quit;
}
bool PortCeEditorDrawPosition(const IRenderableThing *thing, FVector *position) {
    if (!thing || !position || !gEditorRequested || !PortCeEditorWorldViewToken() || !GAME.IsPaused()) return false;
    for (int i = 0; i < gEditorState.Count(); ++i) {
        if (static_cast<const IRenderableThing *>(gEditorActors[i]) != thing) continue;
        CThing *actor = LiveActor(i);
        if (!actor || !CeEditorState::Valid(Point(actor->GetPos()))) return false;
        *position = actor->GetPos();
        return true;
    }
    return false;
}
extern "C" bool PortCeEditorBegin(int installedLevel) {
    gEditorStartRejected = false;
    gEditorChoosingPlayerStart = false;
    if (!gEditorSessionToken || gEditorRequested || !PortCeMenus() || !FRONTEND.IsRunning() ||
        (PortRecording() && !(PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed())) || !PortCeAuthoringSessionAllowed() ||
        (PortCeMuseumVisitActive && PortCeMuseumVisitActive()) ||
        (PortCeMuseumActive && PortCeMuseumActive()) || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
        CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode || installedLevel < 1 || installedLevel > 9999) {
        EditorNote("The editor cannot open while a recording, a network game or another tool is running.");
        return false;
    }
    if (!gEditorCareer) gEditorCareer = static_cast<CCareer *>(malloc(sizeof(CCareer)));
    if (!gEditorCareer) {
        EditorNote("There is not enough memory to open the editor.");
        return false;
    }
    memcpy(gEditorCareer, &CAREER, sizeof(CCareer));
    if (CeModeBegin(CE_MODE_EDITOR) != CE_MODE_BEGUN) {
        EditorNote("The editor cannot open while a recording, a network game or another tool is running.");
        return false;
    }
    ++gEditorSessionToken;
    if (!gEditorSessionToken) {
        CeModeEnd(CE_MODE_EDITOR);
        EditorNote("Restart Onslaught CE to use the editor again.");
        return false;
    }
    CeCareerSnapshotAcquire(CE_MODE_EDITOR, gEditorCareer, sizeof(CCareer));
    gEditorState.Reset();
    gEditorSourceCount = 0;
    gEditorPhase = Authoring;
    gEditorLevel = installedLevel;
    gEditorPlaySelection[0] = 0;
    gEditorTestReach={}; gEditorChoosingReach=gEditorReachPlayerValid=gEditorReached=false; gEditorReachPilot=nullptr; gEditorReachAtSurface=false;
    gEditorSecondNeedsExit = false; gEditorNextReach = {}; gEditorReachLength = gEditorReachStage = gEditorPlaceIndex = gEditorRouteRow = 0;
    gEditorReturnLevel = 0;
    gEditorRequested = true;
    gEditorWorldStarted = gEditorWorldComplete = gEditorReady = gEditorInputOwned = gEditorOpening = false;
    memset(gEditorKeys, 0, sizeof(gEditorKeys));
    gEditorDialog = DialogNone;
    gEditorOrbit = gEditorViewValid = gEditorRefusalParked = gEditorFileCurrent = false;
    gEditorPadMove = gEditorMouseDrag = false;
    gEditorToolFocus = gEditorButtonFocus = 0;
    gEditorHelp = gEditorPointerSeen = gEditorLayoutValid = false;
    gEditorYaw = 0;
    gEditorPitch = 1.05f;
    snprintf(gEditorFilename, sizeof(gEditorFilename), "New project");
    gEditorClosing = false;
    EditorNote("");
    FRONTEND.mSelectedLevel = installedLevel;
    FRONTEND.SetQuit(installedLevel);
    return true;
}
extern "C" bool PortCeEditorBeginWorld(int level, bool base) {
    if (!base) { gEditorNativeStartCount = 0; gEditorNativeStart = {}; gEditorNativeStartFlying = false; }
    if (!gEditorRequested) return false;
    if (base || gEditorPhase == Refused) return true;
    if (gEditorPhase == Playing)
        return EditorRefuse("The play test stopped because another map started to load. Open the map again to go on.");
    if (level != gEditorLevel || gEditorWorldStarted)
        return EditorRefuse("The map did not load the way the editor expected. Nothing was changed.");
    if (gEditorPhase == LoadPlaytest) {
        char error[192];
        if (!PortCeProjectPlaybackBegin(gEditorPlayback, level, false, error, sizeof(error))) {
            fprintf(stderr, "CE editor: play test refused: %s\n", error);
            return EditorRefuse("The draft no longer matches this map, so it cannot be previewed. Your saves are kept.");
        }
    } else if (gEditorPhase == ReturnToAuthoring) {
        gEditorState.Reset();
        gEditorSourceCount = 0;
    }
    gEditorWorldStarted = true;
    return true;
}
extern "C" bool PortCeEditorApplyWorldObject(const char *name, size_t capacity, float *x, float *y, float *z, int *side) {
    if (!gEditorRequested || !x || !y || !z) return false;
    if (gEditorPhase == Refused) return true;
    if (!gEditorWorldStarted || gEditorWorldComplete)
        return EditorRefuse("The map did not load the way the editor expected. Nothing was changed.");
    if (gEditorPhase != LoadPlaytest)
        return PortCeEditorObserveWorldObject(name, capacity, *x, *y, *z);
    // Placements are applied to a copy first, so a refused object leaves the level file's own coordinates in place.
    float px = *x, py = *y, pz = *z;
    char error[192];
    if (!PortCeProjectPlaybackApply(gEditorPlayback, name, capacity, false, &px, &py, &pz, error, sizeof(error), side)) {
        fprintf(stderr, "CE editor: play test refused: %s\n", error);
        return EditorRefuse("The draft no longer matches this map, so it cannot be previewed. Your saves are kept.");
    }
    *x = px;
    *y = py;
    *z = pz;
    return true;
}
extern "C" bool PortCeEditorApplyPlayerStart(float& x, float& y, float& z, bool flying) {
    if (!gEditorRequested || gEditorPhase == Refused) return false;
    ++gEditorNativeStartCount; gEditorNativeStart = {x, y, z}; gEditorNativeStartFlying = flying;
    if (gEditorPhase != LoadPlaytest) return false;
    const CeProjectPlayerStart start = PortCeProjectDocumentPlayerStart(gEditorPlayDocument);
    if (!start.enabled) return false;
    x = start.x; y = start.y; z = start.z;
    return true;
}
extern "C" bool PortCeEditorObserveWorldObject(const char *name, size_t capacity, float x, float y, float z) {
    if (!gEditorRequested || !name || !capacity) return false;
    if (gEditorPhase == Refused) return true;
    if (!gEditorWorldStarted || gEditorWorldComplete)
        return EditorRefuse("The map did not load the way the editor expected. Nothing was changed.");
    const char *end = static_cast<const char *>(memchr(name, 0, capacity));
    if (!end) return EditorRefuse("This map has an object the editor cannot read yet.");
    const size_t length = size_t(end - name);
    if (!length) return true;
    if (length >= sizeof(SourceObject::name) || gEditorSourceCount == CeEditorState::ObjectLimit)
        return EditorRefuse("This map has too many objects for the editor yet.");
    size_t checked = 0;
    if (!SourceName(name, &checked) || !CeEditorState::Valid(CeEditorPoint{x, y, z}))
        return EditorRefuse("This map has an object the editor cannot read yet.");
    SourceObject &object = gEditorSources[gEditorSourceCount++];
    memcpy(object.name, name, length + 1);
    object.initial = CeEditorPoint{x, y, z};
    return true;
}
extern "C" bool PortCeEditorEndWorld(bool base) {
    if (!gEditorRequested) return false;
    if (base || gEditorPhase == Refused) return true;
    if (!gEditorWorldStarted || gEditorWorldComplete)
        return EditorRefuse("The map did not load the way the editor expected. Nothing was changed.");
    if (gEditorPhase == LoadPlaytest) {
        char error[192];
        if (!PortCeProjectPlaybackEnd(gEditorPlayback, false, error, sizeof(error))) {
            fprintf(stderr, "CE editor: play test refused: %s\n", error);
            return EditorRefuse("The draft no longer matches this map, so it cannot be previewed. Your saves are kept.");
        }
    }
    gEditorWorldComplete = true;
    return true;
}
// A refused load parks the level exactly as authoring does (paused, playing state), without taking its camera or input,
// so the first frame can leave through the ordinary quit to the menu.
static bool EditorParkRefusal() {
    if (!gEditorRefusalParked && !gEditorReady) {
        gEditorOriginalPause = GAME.IsPaused() != FALSE;
        gEditorRefusalParked = true;
    }
    GAME.*EditorGameAccess::PauseField() = TRUE;
    GAME.*EditorGameAccess::StateField() = GAME_STATE_PLAYING;
    return true;
}
// Every refusal here parks and returns success: the level is complete, and the first frame leaves it.
extern "C" bool PortCeEditorAfterLoad(void) {
    if (!gEditorRequested) return true;
    if (gEditorPhase == Refused) return EditorParkRefusal();
    if (!gEditorWorldComplete || gEditorReady) {
        EditorRefuse("The map did not load the way the editor expected. Nothing was changed.");
        return EditorParkRefusal();
    }
    if (GAME.IsMultiplayer() || GAME.GetNPlayers() != 1 || !GAME.GetController(0) || !GAME.GetCamera(0) ||
        GAME.IsFreeCameraOn(0)) {
        EditorRefuse("The editor opens one-player maps only.");
        return EditorParkRefusal();
    }
    if (gEditorPhase == LoadPlaytest) {
        // The start this play test actually uses: the placed one, or the map's own unless it begins in flight. Either is
        // checked here in the fully loaded world, where every moved object's collision is where the draft put it.
        const CeProjectPlayerStart authored = PortCeProjectDocumentPlayerStart(gEditorPlayDocument);
        const bool checked = authored.enabled || (gEditorNativeStartCount == 1 && !gEditorNativeStartFlying);
        const CeEditorPoint used = authored.enabled ? CeEditorPoint{authored.x, authored.y, authored.z} : gEditorNativeStart;
        const EditorStartFit fit = checked ? EditorPlayerStartFit(used, authored.enabled) : StartFits;
        if ((authored.enabled && gEditorNativeStartCount != 1) || fit != StartFits) {
            gEditorStartRejectedFit = fit;
            // The fully initialized world is authoritative. Return the same
            // snapshot to editing without advancing a mission tick or saving.
            gEditorPhase = Playing; gEditorStartRejected = true;
            return EditorReturnToAuthoring() ? true : EditorParkRefusal();
        }

        gEditorTestReach=PortCeProjectDocumentReach(gEditorPlayDocument);
        gEditorNextReach = PortCeProjectDocumentReach(gEditorPlayDocument, 1);
        gEditorReachStage=0; gEditorSecondNeedsExit=false;
        gEditorHoldElapsed=0; gEditorHoldInside=false; gEditorHoldLastNs=0;
        gEditorReachLength = gEditorNextReach.enabled ? 2 : gEditorTestReach.enabled ? 1 : 0;
        gEditorReachPlayerValid=gEditorReached=false; gEditorReachPilot=nullptr; gEditorReachAtSurface=false;
        gEditorPhase = Playing;
        GAME.*EditorGameAccess::PauseField() = FALSE;
        EditorNote("Nothing from this play test is kept.");
        return true;
    }
    const bool returning = gEditorPhase == ReturnToAuthoring;
    if (returning) EditorRestoreCareer();
    for (int i = 0; i < gEditorSourceCount; ++i) {
        CThing *actor = nullptr;
        unsigned matches = 0;
        ListIterator<CThing> things(&WORLD.GetThingNB());
        for (CThing *current = things.First(); current; current = things.Next()) {
            const char *name = current->GetName();
            if (name && !strcmp(name, gEditorSources[i].name)) {
                actor = current;
                ++matches;
            }
        }
        const bool available = matches == 1 && actor && CeEditorState::Valid(Point(actor->GetPos()));
        gEditorActors[i] = available ? actor : nullptr;
        gEditorActorStart[i] = available ? Point(actor->GetPos()) : gEditorSources[i].initial;
        gEditorSources[i].installedSide = available && actor->IsA(THING_TYPE_UNIT) ? int(static_cast<CUnit *>(actor)->GetAllegiance()) : -1;
        if (!gEditorState.Add(gEditorSources[i].name, gEditorSources[i].initial,
                              available ? unsigned(actor->GetThingNumber()) : 0, available)) {
            EditorRefuse("This map has an object the editor cannot read yet.");
            return EditorParkRefusal();
        }
    }
    if (gEditorNativeStartCount == 1) gEditorState.InitializePlayerStart(gEditorNativeStart);
    if (!gEditorState.Seal()) {
        EditorRefuse("This map has no objects the editor can move yet.");
        return EditorParkRefusal();
    }
    gEditorPreviousCamera = GAME.GetCurrentCamera(0);
    if (!gEditorPreviousCamera || GAME.GetCamera(0) != gEditorPreviousCamera) {
        gEditorPreviousCamera = nullptr;
        EditorRefuse("This map starts with a camera of its own, so the editor cannot open it yet.");
        return EditorParkRefusal();
    }
    gEditorCamera = new (MEMTYPE_CAMERA) EditorCamera();
    if (!gEditorCamera) {
        gEditorPreviousCamera = nullptr;
        EditorRefuse("There is not enough memory to open the editor.");
        return EditorParkRefusal();
    }
    gEditorCamera->mZoom = gEditorPreviousCamera->GetZoom();
    gEditorOriginalPause = GAME.IsPaused() != FALSE;
    GAME.*EditorGameAccess::PauseField() = TRUE;
    GAME.*EditorGameAccess::StateField() = GAME_STATE_PLAYING;
    GAME.SetCurrentCamera(0, gEditorCamera, false);
    gEditorReady = true;
    gEditorPhase = Authoring;
    gEditorClosing = false;
    if (returning) {
        CeProjectDocument *document = gEditorPlayDocument;
        gEditorPlayDocument = nullptr;
        if (!EditorApplyDocument(document, nullptr)) {
            EditorRefuse("The draft no longer matches this map, so the editor closed it. Your saves are kept.");
            return EditorParkRefusal();
        }
        for (int i = 0; i < gEditorState.Count(); ++i)
            if (!strcmp(gEditorState.Object(i)->source, gEditorPlaySelection)) {
                (void)gEditorState.Select(i);
                break;
            }
        PortCeProjectPlaybackDelete(gEditorPlayback);
        gEditorPlayback = nullptr;
    }
    gEditorLastTime = EditorNowNs();
    if (returning && gEditorKept.PlayerStartSelected() && gEditorState.PlayerStartAvailable())
        gEditorState.Select(CeEditorState::PlayerStartSelection);
    if (!returning) EditorStartView();
    // A start refused while the play test loaded says why; it is not replaced by the ordinary welcome back.
    const bool refused = returning && gEditorStartRejected;
    gEditorStartRejected = false;

    EditorCameraUpdate(0);
    PortCeDrawHooks(true);
    if (refused)
        EditorNote(EditorStartRefusal(EditorStartFit(gEditorStartRejectedFit)), NoticeRefusal);
    else
        EditorNote(returning ? "Back in the editor. Your draft is as you left it." : "");
    return true;
}
// Find the first entry into the sphere along this movement segment.
// Comparing the two entry times lets a fast pilot cross both stops in one game step, in order.
bool EditorReachEntry(const CeEditorPoint& from, const CeEditorPoint& to, const CeProjectReach& goal,
                      double& entry) {
    const double sx=double(to.x)-from.x, sy=double(to.y)-from.y, sz=double(to.z)-from.z;
    const double tx=double(goal.x)-from.x, ty=double(goal.y)-from.y, tz=double(goal.z)-from.z;
    const double span=sx*sx+sy*sy+sz*sz, radius=goal.hold.Enabled()?goal.hold.radius:kEditorReachRadius, radius2=radius*radius;
    if (span == 0) { entry=0; return tx*tx+ty*ty+tz*tz <= radius2; }
    const double middle=(tx*sx+ty*sy+tz*sz)/span;
    const double ex=tx-middle*sx, ey=ty-middle*sy, ez=tz-middle*sz;
    const double chord2=radius2-(ex*ex+ey*ey+ez*ez);
    if (chord2 < 0) return false;
    const double half=sqrt(chord2/span);
    const double first=middle-half, last=middle+half;
    entry=first > 0 ? first : 0;
    return entry <= 1 && entry <= last;
}
void EditorUpdateReach() {
    if (!gEditorTestReach.enabled || gEditorReached) return;
    if (GAME.IsPaused() || GAME.GetCurrentLevel() != gEditorLevel ||
        GAME.*EditorGameAccess::StateField() != GAME_STATE_PLAYING) {
        gEditorReachPlayerValid = false;
        gEditorHoldLastNs=0; // A paused interval is not time spent defending.
        return;
    }
    CPlayer *player = GAME.GetPlayer(0);
    auto *engine = player ? player->GetBattleEngine() : nullptr;
    if (!engine || engine->IsDying()) { gEditorReachPlayerValid=false; gEditorHoldElapsed=0; gEditorHoldInside=false; gEditorHoldLastNs=0; return; }
    const CeEditorPoint pos = Point(engine->GetPos());
    if (!CeEditorState::Valid(pos)) { gEditorReachPlayerValid=false; gEditorHoldElapsed=0; gEditorHoldInside=false; gEditorHoldLastNs=0; return; }
    // The actor's ordinary movement remembers its previous position. Teleport deliberately resets it
    // to the destination. Never connect two samples across such a discontinuity, even for the same pilot.
    const CeEditorPoint old = Point(engine->GetOldPos());
    const bool relocated = SamePoint(old,pos) && !SamePoint(gEditorReachPrevious,pos);
    const bool changedPilot=gEditorReachPilot && gEditorReachPilot!=engine;
    if (relocated || changedPilot) { gEditorHoldElapsed=0; gEditorHoldInside=false; gEditorHoldLastNs=0; }
    const CeEditorPoint from = gEditorReachPlayerValid && gEditorReachPilot == engine && !relocated ? gEditorReachPrevious : pos;
    const float dx = gEditorTestReach.x - pos.x, dy = gEditorTestReach.y - pos.y, dz = gEditorTestReach.z - pos.z;
    const float flat = hypotf(dx,dy);
    gEditorReachDistance = hypotf(flat,dz);
    const float radius=gEditorTestReach.hold.Enabled()?float(gEditorTestReach.hold.radius):float(kEditorReachRadius);
    gEditorReachAtSurface = flat <= radius && fabsf(dz) > radius;
    const FVector forward = engine->GetOrientation()*FVector(0,1,0), right = engine->GetOrientation()*FVector(1,0,0);
    const float ahead = dx*forward.X + dy*forward.Y, aside = dx*right.X + dy*right.Y;
    gEditorReachBearing = std::isfinite(ahead) && std::isfinite(aside) ? int(floorf(atan2f(aside,ahead)*1.273239545f+8.5f))%8 : 0;
    gEditorReachPrevious = pos;
    gEditorReachPilot = engine;
    gEditorReachPlayerValid = true;
    // Distance from the saved target to this pilot's whole movement since the last valid gameplay sample.
    // A fast pass through counts, a high flyover does not, and a pause or pilot replacement never draws a false crossing.
    if (gEditorSecondNeedsExit) {
        if (gEditorReachDistance > radius) gEditorSecondNeedsExit=false;
        return;
    }
    double entry = 0;
    bool met=false;
    if (gEditorTestReach.hold.Enabled()) {
        // Credit continuous observed occupancy, never time spent outside, paused or in another pilot.
        const double simSec = double(EVENT_MANAGER.GetTime());
        const unsigned long long now = (std::isfinite(simSec) && simSec >= 0.0)
            ? (1ull + static_cast<unsigned long long>(simSec * 1e9 + 0.5))
            : 0ull;
        const bool inside=gEditorReachDistance<=radius;
        if (!inside) gEditorHoldElapsed=0;
        else if (gEditorHoldInside && gEditorHoldLastNs && now>=gEditorHoldLastNs)
            gEditorHoldElapsed+=double(now-gEditorHoldLastNs)*1e-9;
        gEditorHoldInside=inside; gEditorHoldLastNs=now;
        met=inside && gEditorHoldElapsed>=gEditorTestReach.hold.seconds;
        if (met) { gEditorHoldElapsed=gEditorTestReach.hold.seconds; entry=1; }
    } else met = EditorReachEntry(from, pos, gEditorTestReach, entry);
    if (met) {
        if (gEditorReachLength == 2 && gEditorReachStage == 0) {
            gEditorTestReach = gEditorNextReach;
            gEditorHoldElapsed=0; gEditorHoldInside=false; gEditorHoldLastNs=0;
            gEditorReachStage = 1;
            // The next sweep starts at this endpoint, so it cannot count an earlier portion twice.
            gEditorReachDistance = hypotf(hypotf(gEditorTestReach.x-pos.x, gEditorTestReach.y-pos.y), gEditorTestReach.z-pos.z);
            gEditorReachAtSurface = false;
            double secondEntry = 0;
            if (!gEditorTestReach.hold.Enabled() && EditorReachEntry(from, pos, gEditorTestReach, secondEntry) && secondEntry > entry) gEditorReached = true;
            else if (!gEditorTestReach.hold.Enabled() && gEditorReachDistance <= kEditorReachRadius)
                gEditorSecondNeedsExit = true;
            else EditorNote("Stop 1 reached. Next: Stop 2.");
        } else gEditorReached = true;
    }
}
void PortCeEditorFrame() {
    if (PortCeEditorPlaying()) {EditorUpdateReach();return;}
    if (gEditorRequested && gEditorPhase == Refused) {
        EditorExit();
        return;
    }
    if (gEditorPhase != Authoring) return;
    if (!gEditorReady) return;
    if (GAME.GetCurrentLevel() != gEditorLevel || !gEditorCamera || GAME.GetCamera(0) != gEditorCamera) {
        EditorNote("Another part of the game took over the view, so the editor closed. Your saves are kept.");
        EditorExit();
        return;
    }
    GAME.*EditorGameAccess::PauseField() = TRUE;
    if (!gEditorInputOwned) {
        if (LT.GetKeytrap()) {
            EditorNote("Another part of the game took over the controls, so the editor closed. Your saves are kept.");
            EditorExit();
            return;
        }
        CCONTROLLER *controller = GAME.GetController(0);
        if (!controller) {
            EditorExit();
            return;
        }
        gEditorOpening = true;
        ShutdownMouse();
        gEditorOpening = false;
        controller->SetToControl(&gEditorInput);
        LT.SetKeytrap((pKeyTrapper)EditorTrap);
        gEditorInputOwned = true;
        gEditorPointerX = gEditorHoverX = float(MOUSE_X);
        gEditorPointerY = gEditorHoverY = float(MOUSE_Y);
        gEditorPointerSeen = gEditorHelp = false;
    }
    if (LT.GetKeytrap() != (pKeyTrapper)EditorTrap || !GAME.GetController(0) ||
        GAME.GetController(0)->GetToControl() != &gEditorInput) {
        EditorExit();
        return;
    }
    const unsigned long long now = EditorNowNs();
    float elapsed = now >= gEditorLastTime ? float(now - gEditorLastTime) * 1e-9f : 0;
    gEditorLastTime = now;
    if (elapsed > 0.1f) elapsed = 0.1f;
    EditorCameraUpdate(elapsed);
    // Hover: once the pointer moves in KBM mode, what it is over takes the focus, with the move cue (F4).
    const float hoverX = float(MOUSE_X), hoverY = float(MOUSE_Y);
    if (hoverX != gEditorHoverX || hoverY != gEditorHoverY) {
        gEditorHoverX = hoverX;
        gEditorHoverY = hoverY;
        if (!gEditorPadActive) {
            gEditorPointerSeen = true;
            if (!gEditorState.Moving() && !gEditorOrbit && !gEditorHelp) {
                const EditorHitResult hit = EditorHit(hoverX, hoverY);
                if ((gEditorDialog == DialogRoute || gEditorDialog == DialogHoldMenu || gEditorDialog == DialogHoldInspector) && hit.row >= 0 && hit.row != gEditorRouteRow) {
                    gEditorRouteRow = hit.row; EditorCue(CueMove);
                } else if (gEditorDialog == DialogFiles && hit.row >= 0 && gEditorFileTop + hit.row < gEditorFileCount &&
                    gEditorFileTop + hit.row != gEditorFileIndex) {
                    gEditorFileIndex = gEditorFileTop + hit.row;
                    EditorCue(CueMove);
                } else if ((gEditorDialog == DialogDiscardExit || gEditorDialog == DialogDiscardOpen) && hit.button >= 0 &&
                           hit.button != gEditorButtonFocus) {
                    gEditorButtonFocus = hit.button;
                    EditorCue(CueMove);
                } else if (gEditorDialog == DialogNone && hit.tool >= 0 && hit.tool != gEditorToolFocus && EditorToolEnabled(hit.tool)) {
                    gEditorToolFocus = hit.tool;
                    EditorCue(CueMove);
                }
            }
        }
    }
    EditorNoticeSettle();
    if (!gEditorState.Moving()) { gEditorPadMove = false; gEditorMouseDrag = false; }
    if (gEditorState.Moving() && !gEditorPadActive && !gEditorPadMove && gEditorDialog == DialogNone && !gEditorHelp) {
        CeEditorPoint point;
        if (EditorRay(float(MOUSE_X), float(MOUSE_Y), gEditorPlaneZ, &point)) {
            const int i = gEditorState.Selected();
            const bool start = i == CeEditorState::PlayerStartSelection;
            const CeEditorPoint origin = start ? gEditorState.PlayerStart() : gEditorActorStart[i];
            const CeEditorPoint initial = start ? origin : gEditorSources[i].initial;
            const CeEditorBounds bounds = EditorBounds();
            float wx = point.x + gEditorDragOffset.x;
            float wy = point.y + gEditorDragOffset.y;
            if (wx < bounds.minX) wx = bounds.minX;
            if (wx > bounds.maxX) wx = bounds.maxX;
            if (wy < bounds.minY) wy = bounds.minY;
            if (wy > bounds.maxY) wy = bounds.maxY;
            const CeEditorPoint position{wx - origin.x + initial.x,
                                         wy - origin.y + initial.y,
                                         initial.z + gEditorPlaneZ - origin.z};
            if (gEditorState.Move(position) && !ApplyOne(i)) {
                EditorNote("The selected object is gone, so the editor closed. Your saves are kept.");
                EditorExit();
            }
        }
    }
}
bool PortCeEditorKey(unsigned key, bool repeat) {
    if (PortCeEditorPlaying()) {
        if (LT.GetKeytrap() || GAME.IsPaused() || GAMEINTERFACE.IsMenuUp() || GAMEINTERFACE.mShowJoypad) return false;
        if (key == DIK_F6) {
            if (!repeat) GAME.SetQuit(QT_RESTART_LEVEL);
            return true;
        }
        if (key != DIK_F5) return false;
        if (!repeat) EditorReturnToAuthoring();
        return true;
    }
    if (gEditorRequested && gEditorPhase != Authoring) return key == DIK_F5 || key == DIK_F6;
    if (gEditorReady && gEditorPhase == Authoring && !gEditorInputOwned) return true;
    if (!gEditorReady || !gEditorInputOwned) return false;
    if (repeat) return true;
    return EditorTrap(WORD(key), KEY_EVENT_DOWN);
}
void PortCeEditorRender() {
    if (gEditorPhase != Authoring && !PortCeEditorPlaying()) return;
    if ((!gEditorReady || !gEditorCamera) && !PortCeEditorPlaying()) return;
    if (!PortCeEditorPlaying()) (void)EditorSnapshot();
    EditorBuildModel(gEditorModel);
    gEditorLayoutValid = CeEditorUiRender(gEditorModel, &gEditorLayout);
    const float scale = gEditorLayout.scale;
    if (gEditorLayoutValid && scale > 0) gEditorScale = scale;
}
void PortCeEditorLevelEnd() {
    if (!gEditorRequested) return;
    CeWorldViewInvalidate(CeWorldViewOwner::Editor);
    if (gEditorReady)
        for (int i = 0; i < gEditorState.Count(); ++i)
            if (CThing *actor = LiveActor(i)) actor->MoveTo(Vector(gEditorActorStart[i]));
    if (gEditorInputOwned) {
        if (LT.GetKeytrap() == (pKeyTrapper)EditorTrap) LT.SetKeytrap(nullptr);
        if (CCONTROLLER *controller = GAME.GetController(0))
            if (controller->GetToControl() == &gEditorInput) controller->RelinquishControl();
    }
    gEditorInputOwned = false;
    if (gEditorReady && GAME.GetCurrentLevel() == gEditorLevel) {
        if (gEditorCamera && GAME.GetCurrentCamera(0) == gEditorCamera && gEditorPreviousCamera) {
            CCamera *previous = gEditorPreviousCamera;
            gEditorPreviousCamera = nullptr;
            GAME.SetCurrentCamera(0, previous, true);
        }
        GAME.*EditorGameAccess::PauseField() = gEditorOriginalPause ? TRUE : FALSE;
    } else if (gEditorRefusalParked && GAME.GetCurrentLevel() == gEditorLevel)
        GAME.*EditorGameAccess::PauseField() = gEditorOriginalPause ? TRUE : FALSE;
    gEditorRefusalParked = false;
    // The previous camera was detached, not deleted, on entry. An external owner may
    // have restored it already; never delete a camera which is back in the live slot.
    if (gEditorPreviousCamera) {
        if (GAME.GetCurrentCamera(0) != gEditorPreviousCamera) delete gEditorPreviousCamera;
        gEditorPreviousCamera = nullptr;
    }
    if (gEditorCamera) {
        if (GAME.GetCurrentCamera(0) == gEditorCamera)
            GAME.SetCurrentCamera(0, nullptr, true);
        else if (GAME.GetCamera(0) != gEditorCamera)
            delete gEditorCamera;
        gEditorCamera = nullptr;
    }
    gEditorReady = false;
    gEditorOpening = false;
    gEditorViewValid = false;
    gEditorLayoutValid = false;
    gEditorOrbit = false;
    gEditorPadMove = false;
    memset(gEditorKeys, 0, sizeof(gEditorKeys));
    PortCeDrawHooks(PortCePhotoOn() || PortCeCmdSuppressFlightHud());
}
extern "C" void PortCeEditorCancel(void) {
    if (!gEditorRequested) {
        CeModeEnd(CE_MODE_EDITOR);
        return;
    }
    PortCeEditorLevelEnd();
    EditorRestoreCareer();
    CeModeEnd(CE_MODE_EDITOR);
    PortCeProjectDocumentDelete(gEditorDocument);
    gEditorDocument = nullptr;
    PortCeProjectDocumentDelete(gEditorPlayDocument);
    gEditorPlayDocument = nullptr;
    PortCeProjectPlaybackDelete(gEditorPlayback);
    gEditorPlayback = nullptr;
    gEditorPhase = Authoring;
    gEditorPlaySelection[0] = 0;
    gEditorState.Reset();
    gEditorSourceCount = 0;
    gEditorRequested = gEditorWorldStarted = gEditorWorldComplete = gEditorPadActive = false;
    gEditorChoosingReach = gEditorChoosingPlayerStart = gEditorHelp = false;
    gEditorLevel = -1;
}
extern "C" bool PortCeEditorTakeReturn(void) {
    if (!gEditorRequested) return false;
    gEditorReturnLevel = gEditorLevel;
    PortCeEditorCancel();
    return true;
}
int PortCeEditorMenuReturn() {
    int level = gEditorReturnLevel;
    gEditorReturnLevel = 0;
    return level;
}

// ---- Accessors of the editor (ce_editor.h), for the test kit (ce_editor_testkit.cpp). They do nothing themselves. Most are
// reads; the view's fields are returned as writable addresses so a test's pose (TESTWRITE) writes what the keys and the orbit
// drag write, and the next frame's own limits (CeEditorClampView) apply as they do to the keys.
bool PortCeEditorAuthoringShown() {
    return gEditorReady && gEditorPhase == Authoring && gEditorCamera && GAME.GetCamera(0) == gEditorCamera;
}
int PortCeEditorPhaseNow() { return (!gEditorReady && gEditorPhase == Authoring) ? -1 : int(gEditorPhase); }
CeEditorPoint *PortCeEditorViewFocus() { return &gEditorFocus; }
float *PortCeEditorViewDistance() { return &gEditorDistance; }
float *PortCeEditorViewYaw() { return &gEditorYaw; }
float *PortCeEditorViewPitch() { return &gEditorPitch; }
const char *PortCeEditorNoticeShown() { return gEditorNote; }
int PortCeEditorSelection() { return gEditorState.Selected(); }

extern "C" int PortCeEditorAllowsBugClipHotkey(void) {
    return (gEditorReady && gEditorInputOwned && LT.GetKeytrap() == (pKeyTrapper)EditorTrap) ? 1 : 0;
}
#endif
