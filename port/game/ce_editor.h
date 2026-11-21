// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_EDITOR_H
#define CE_EDITOR_H

#include <stddef.h>
#include "ce_hold.h"

// The native editor keeps authoring state separate from actors and careers.
// A source name is an existing object's exact project binding, not a display-name lookup.
struct CeEditorPoint { float x, y, z; };
struct CeEditorObject {
    char source[96];
    CeEditorPoint position, saved;
    unsigned identity;
    int side, savedSide;  // -1 keeps the installed side.
    bool available;
};

struct CeEditorPlayerStart {
    bool enabled = false;
    CeEditorPoint position{};
};
class CeEditorState {
public:
    enum { ObjectLimit = 4096, HistoryLimit = 128 };
    CeEditorState();
    void Reset();
    // Every occurrence must be supplied. A repeated source name makes both records unavailable.
    bool Add(const char* source, CeEditorPoint position, unsigned identity, bool available = true, int side = -1);
    // Seal before editing: failed or truncated collection never yields a partially trusted scene.
    bool Seal();
    bool Sealed() const { return sealed_; }
    int Count() const { return count_; }
    int Selected() const { return selected_; }
    const CeEditorObject* Object(int index) const;
    enum { PlayerStartSelection = -2 };
    bool InitializePlayerStart(CeEditorPoint original);
    bool RestorePlayerStart(bool enabled, CeEditorPoint position);
    bool SetPlayerStart(CeEditorPoint position);
    bool UseOriginalPlayerStart();
    bool PlayerStartAvailable() const { return playerStartAvailable_; }
    bool PlayerStartEdited() const { return playerStart_.enabled; }
    bool PlayerStartSelected() const { return selected_ == PlayerStartSelection; }
    CeEditorPoint PlayerStart() const { return playerStart_.enabled ? playerStart_.position : originalPlayerStart_; }
    bool Select(int index);
    bool Cycle(int direction);
    // carryStart: the Player Start stands on the selected object (its deck), so it moves with it, in the same
    // change: one Undo, Redo or cancel puts both back. The caller decides by the scene's geometry.
    bool BeginMove(bool carryStart = false);
    bool MoveCarriesStart() const { return moving_ && moveCarriesStart_; }
    bool Move(CeEditorPoint position);
    bool FinishMove(bool accept);
    bool ChangeSide(int side);
    bool HasReach() const { return route_.count != 0; }
    int ReachCount() const { return route_.count; }
    CeEditorPoint Reach(int index = 0) const { return index >= 0 && index < route_.count ? route_.points[index] : CeEditorPoint{}; }
    CeHoldSettings Hold(int index) const { return index >= 0 && index < route_.count ? route_.hold[index] : CeHoldSettings{}; }
    bool SetReachAt(int index, const CeEditorPoint* point, const CeHoldSettings* hold = nullptr);
    bool SetHoldAt(int index, CeHoldSettings hold);
    bool ReverseReach();
    bool RestoreReach(const CeEditorPoint* points, int count, const CeHoldSettings* holds = nullptr);
    bool Undo();
    bool Redo();
    bool Moving() const { return moving_; }
    bool CanUndo() const { return !moving_ && historyAt_ > 0; }
    bool CanRedo() const { return !moving_ && historyAt_ < historySize_; }
    bool Dirty() const;
    void MarkSaved();
    int Ambiguous() const;
    // Carry Undo, Redo and the last explicit save across a preview: true only when every object's source
    // is the same, in the same order, so each remembered change still names its own object.
    bool KeepHistory(const CeEditorState& before);
    static bool Valid(CeEditorPoint point);
private:
    struct Route { int count = 0; CeEditorPoint points[2] = {}; CeHoldSettings hold[2]{}; };
    struct Change { int object; CeEditorPoint before, after; int beforeSide, afterSide; Route oldRoute{}, newRoute{}; CeEditorPlayerStart oldStart{}, newStart{}; bool startFollows = false; };
    static bool SameRoute(const Route& a, const Route& b);
    bool ChangeRoute(const Route& next);
    void Record(const Change& change);
    CeEditorObject objects_[ObjectLimit];
    Change history_[HistoryLimit];
    int count_, selected_, historySize_, historyAt_;
    bool moving_, sealed_, collectionValid_;
    CeEditorPoint moveStart_;
    Route route_, savedRoute_;
    bool playerStartAvailable_ = false;
    CeEditorPoint originalPlayerStart_{};
    CeEditorPlayerStart playerStart_{}, savedPlayerStart_{}, movePlayerStart_{};
    bool moveCarriesStart_ = false;
    CeEditorPoint moveStartPoint_{};
    static bool SameStart(const CeEditorPlayerStart& a, const CeEditorPlayerStart& b);
    bool ChangeStart(const CeEditorPlayerStart& next);

};

// Intersect a forward cursor ray with a horizontal editing plane. No terrain or engine state is read.
bool CeEditorRayPlane(CeEditorPoint origin, CeEditorPoint direction, float height, CeEditorPoint* point);

// Where the view may go: the level's ground and a margin around it. A project may hold an object anywhere the format
// allows (a million units out), but the view never leaves the level for it.
struct CeEditorBounds { float minX, minY, maxX, maxY; };
CeEditorBounds CeEditorLevelBounds(int width, int height);
bool CeEditorInside(const CeEditorBounds& bounds, CeEditorPoint point);
// The view that frames every point inside the bounds (points outside are left out); false when none is inside.
bool CeEditorFitView(const CeEditorPoint* points, int count, const CeEditorBounds& bounds, CeEditorPoint* focus, float* distance);
// Keep the view's focus inside the bounds and its distance within the level's size.
void CeEditorClampView(const CeEditorBounds& bounds, CeEditorPoint* focus, float* distance);

class CCareer;
class CCamera;
class IRenderableThing;
class FVector;

// Native menu session: Begin snapshots the career and requests the map itself; callers must not call
// SetQuit again. A true return lets the menu dismiss its page. Active covers loading, authoring,
// failure and teardown until TakeReturn restores the entry career and selects the tool page.
extern "C" bool PortCeEditorBegin(int installedLevel);
extern "C" bool PortCeEditorActive(void);
extern "C" void PortCeEditorFilterSave(CCareer* career, void* destination);
// Active stays true during play tests and map reloads so career guards remain closed.
bool PortCeEditorPlaying();
// Accessors for the test kit (ce_editor_testkit.cpp). They do nothing themselves; the view's fields are writable addresses.
bool PortCeEditorAuthoringShown();      // the editor is open on its map
int PortCeEditorPhaseNow();             // 0 authoring, 1 loading the play test, 2 playing, 3 returning, 4 refused; -1 closed
CeEditorPoint *PortCeEditorViewFocus(); // the view's fields, by address
float *PortCeEditorViewDistance();
float *PortCeEditorViewYaw();
float *PortCeEditorViewPitch();
const char *PortCeEditorNoticeShown();  // the notice on screen, or ""
int PortCeEditorSelection();            // the selected object, -1 none, CeEditorState::PlayerStartSelection the start
// Resolve the ordinary leave/retry result before the engine chooses its next level.
int PortCeEditorResolveQuit(int quit);
extern "C" void PortCeEditorCancel(void);
extern "C" bool PortCeEditorTakeReturn(void);
extern "C" bool PortCeEditorAfterLoad(void);
extern "C" bool PortCeEditorBeginWorld(int level, bool base);
extern "C" bool PortCeEditorApplyPlayerStart(float& x, float& y, float& z, bool flying);
extern "C" bool PortCeEditorObserveWorldObject(const char* name, size_t capacity, float x, float y, float z);
extern "C" bool PortCeEditorApplyWorldObject(const char* name, size_t capacity, float* x, float* y, float* z, int* side = nullptr);
extern "C" bool PortCeEditorEndWorld(bool base);
const char* PortCeEditorMessage();
int PortCeEditorMenuReturn();
bool PortCeEditorMousePixels();
bool PortCeEditorKey(unsigned key, bool repeat);
bool PortCeEditorSuppressFlightHud();
// Only the parked editor's live, bound placements use current translation for drawing.
// The actor and its previous simulation transform are never modified by this query.
bool PortCeEditorDrawPosition(const IRenderableThing* thing, FVector* position);
void PortCeEditorFrame();
void PortCeEditorRender();
void PortCeEditorLevelEnd();
#endif
