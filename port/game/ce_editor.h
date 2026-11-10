// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_EDITOR_H
#define CE_EDITOR_H

#include <stddef.h>

// The native editor keeps authoring state separate from actors and careers.
// A source name is an existing object's exact project binding, not a display-name lookup.
struct CeEditorPoint { float x, y, z; };
struct CeEditorObject {
    char source[96];
    CeEditorPoint position, saved;
    unsigned identity;
    bool available;
};

class CeEditorState {
public:
    enum { ObjectLimit = 4096, HistoryLimit = 128 };
    CeEditorState();
    void Reset();
    // Every occurrence must be supplied. A repeated source name makes both records unavailable.
    bool Add(const char* source, CeEditorPoint position, unsigned identity, bool available = true);
    // Seal before editing: failed or truncated collection never yields a partially trusted scene.
    bool Seal();
    bool Sealed() const { return sealed_; }
    int Count() const { return count_; }
    int Selected() const { return selected_; }
    const CeEditorObject* Object(int index) const;
    bool Select(int index);
    bool Cycle(int direction);
    bool BeginMove();
    bool Move(CeEditorPoint position);
    bool FinishMove(bool accept);
    bool Undo();
    bool Redo();
    bool Moving() const { return moving_; }
    bool Dirty() const;
    void MarkSaved();
    int Ambiguous() const;
    static bool Valid(CeEditorPoint point);
private:
    struct Change { int object; CeEditorPoint before, after; };
    CeEditorObject objects_[ObjectLimit];
    Change history_[HistoryLimit];
    int count_, selected_, historySize_, historyAt_;
    bool moving_, sealed_, collectionValid_;
    CeEditorPoint moveStart_;
};

// Intersect a forward cursor ray with a horizontal editing plane. No terrain or engine state is read.
bool CeEditorRayPlane(CeEditorPoint origin, CeEditorPoint direction, float height, CeEditorPoint* point);

class CCamera;
class IRenderableThing;
class FVector;

// Native menu session: Begin snapshots the career and requests the map itself; callers must not call
// SetQuit again. A true return lets the menu dismiss its page. Active covers loading, authoring,
// failure and teardown until TakeReturn restores the entry career and selects the tool page.
extern "C" bool PortCeEditorBegin(int installedLevel);
extern "C" bool PortCeEditorActive(void);
extern "C" void PortCeEditorCancel(void);
extern "C" bool PortCeEditorTakeReturn(void);
extern "C" bool PortCeEditorAfterLoad(void);
extern "C" bool PortCeEditorBeginWorld(int level, bool base);
extern "C" bool PortCeEditorObserveWorldObject(const char* name, size_t capacity, float x, float y, float z);
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
