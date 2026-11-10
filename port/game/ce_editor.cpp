// SPDX-License-Identifier: GPL-3.0-only
#include "ce_editor.h"
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
    moveStart_ = CeEditorPoint{0, 0, 0};
}
bool CeEditorState::Valid(CeEditorPoint point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
           std::fabs(point.x) <= CoordinateLimit && std::fabs(point.y) <= CoordinateLimit &&
           std::fabs(point.z) <= CoordinateLimit;
}
bool CeEditorState::Add(const char *source, CeEditorPoint position, unsigned identity, bool available) {
    size_t length = 0;
    if (sealed_) return false;
    if (count_ == ObjectLimit || !SourceName(source, &length) || !Valid(position)) {
        collectionValid_ = false;
        return false;
    }
    CeEditorObject &object = objects_[count_];
    std::memcpy(object.source, source, length + 1);
    object.position = object.saved = position;
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
    return false;
}
const CeEditorObject *CeEditorState::Object(int index) const {
    return index >= 0 && index < count_ ? &objects_[index] : 0;
}
bool CeEditorState::Select(int index) {
    if (!sealed_ || moving_ || index < -1 || index >= count_ || (index >= 0 && !objects_[index].available))
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
bool CeEditorState::BeginMove() {
    if (!sealed_ || moving_ || selected_ < 0 || !objects_[selected_].available) return false;
    moving_ = true;
    moveStart_ = objects_[selected_].position;
    return true;
}
bool CeEditorState::Move(CeEditorPoint position) {
    if (!moving_ || selected_ < 0 || !objects_[selected_].available || !Valid(position)) return false;
    objects_[selected_].position = position;
    return true;
}
bool CeEditorState::FinishMove(bool accept) {
    if (!moving_ || selected_ < 0) return false;
    moving_ = false;
    CeEditorPoint &after = objects_[selected_].position;
    if (!accept) {
        after = moveStart_;
        return true;
    }
    if (SamePoint(after, moveStart_)) return true;
    // A new edit after Undo discards only the redo branch. The oldest complete move is retired at the bound.
    historySize_ = historyAt_;
    if (historySize_ == HistoryLimit) {
        for (int i = 1; i < HistoryLimit; ++i) history_[i - 1] = history_[i];
        --historySize_;
        --historyAt_;
    }
    history_[historySize_++] = Change{selected_, moveStart_, after};
    historyAt_ = historySize_;
    return true;
}
bool CeEditorState::Undo() {
    if (moving_ || !historyAt_) return false;
    const Change &change = history_[--historyAt_];
    objects_[change.object].position = change.before;
    selected_ = change.object;
    return true;
}
bool CeEditorState::Redo() {
    if (moving_ || historyAt_ == historySize_) return false;
    const Change &change = history_[historyAt_++];
    objects_[change.object].position = change.after;
    selected_ = change.object;
    return true;
}
bool CeEditorState::Dirty() const {
    for (int i = 0; i < count_; ++i)
        if (!SamePoint(objects_[i].position, objects_[i].saved)) return true;
    return false;
}
void CeEditorState::MarkSaved() {
    if (moving_) return;
    for (int i = 0; i < count_; ++i) objects_[i].saved = objects_[i].position;
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

#ifdef CE_VERSION
#include "ce_world_view.h"
#include "common.h"
#include "game.h"
#include "frontend.h"
#include "Career.h"
#include "world.h"
#include "thing.h"
#include "player.h"
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
#include <stdio.h>
#include <stdlib.h>

bool PortCeMenus();
bool PortRecording();
unsigned long long PortClockRawNs();
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
};
SourceObject gEditorSources[CeEditorState::ObjectLimit];
CThing *gEditorActors[CeEditorState::ObjectLimit];
CeEditorPoint gEditorActorStart[CeEditorState::ObjectLimit];
CeProjectPosition gEditorPositions[CeEditorState::ObjectLimit];
int gEditorSourceCount = 0, gEditorLevel = -1, gEditorReturnLevel = 0;
bool gEditorRequested = false, gEditorWorldStarted = false, gEditorWorldComplete = false;
bool gEditorReady = false, gEditorInputOwned = false, gEditorOpening = false, gEditorOriginalPause = false;
bool gEditorKeys[256], gEditorOrbit = false, gEditorViewValid = false;
CCareer *gEditorCareer = nullptr;
CeProjectDocument *gEditorDocument = nullptr;
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
char gEditorNote[192] = "Open an installed map to begin authoring.";
char gEditorFilename[128] = "New project";
CeEditorFile gEditorFiles[2048];
int gEditorFileCount = 0, gEditorFileIndex = 0, gEditorFileTop = 0;
enum EditorDialog { DialogNone, DialogFiles, DialogDiscardExit, DialogDiscardOpen };
EditorDialog gEditorDialog = DialogNone;
CDXBitmapFont *gEditorFont = nullptr;
CDXTexture *gEditorWhite = nullptr;
float gEditorWidth = 0, gEditorHeight = 0, gEditorScale = 1;
bool EditorTrap(WORD key, int event);

void EditorNote(const char *message) { snprintf(gEditorNote, sizeof(gEditorNote), "%s", message); }
CeEditorPoint Point(const FVector &p) { return CeEditorPoint{p.X, p.Y, p.Z}; }
FVector Vector(CeEditorPoint p) { return FVector(p.x, p.y, p.z); }
CeEditorPoint NativePosition(int index) {
    const CeEditorObject *object = gEditorState.Object(index);
    const CeEditorPoint initial = gEditorSources[index].initial, actor = gEditorActorStart[index];
    return CeEditorPoint{actor.x + object->position.x - initial.x, actor.y + object->position.y - initial.y,
                         actor.z + object->position.z - initial.z};
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
    CThing *actor = LiveActor(index);
    if (!actor) return false;
    const CeEditorPoint position = NativePosition(index);
    if (!CeEditorState::Valid(position)) return false;
    actor->MoveTo(Vector(position));
    return true;
}
void EditorExit() { GAME.SetQuit(QT_USER_QUIT_TO_FRONTEND); }
void EditorFit(bool selected) {
    if (selected && gEditorState.Selected() >= 0) {
        gEditorFocus = NativePosition(gEditorState.Selected());
        gEditorDistance = 45;
        return;
    }
    bool first = true;
    float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0;
    for (int i = 0; i < gEditorState.Count(); ++i)
        if (gEditorState.Object(i)->available) {
            const CeEditorPoint p = NativePosition(i);
            if (first) {
                minX = maxX = p.x;
                minY = maxY = p.y;
                minZ = p.z;
                first = false;
            } else {
                if (p.x < minX) minX = p.x;
                if (p.x > maxX) maxX = p.x;
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
                if (p.z < minZ) minZ = p.z;
            }
        }
    if (first) return;
    gEditorFocus = CeEditorPoint{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, minZ};
    float span = maxX - minX > maxY - minY ? maxX - minX : maxY - minY;
    gEditorDistance = span * 0.8f;
    if (gEditorDistance < 40) gEditorDistance = 40;
    if (gEditorDistance > 500) gEditorDistance = 500;
}
void EditorCameraUpdate(float elapsed) {
    if (!gEditorCamera) return;
    if (gEditorDialog == DialogNone && !gEditorState.Moving()) {
        float x = float(gEditorKeys[DIK_D] || gEditorKeys[DIK_RIGHT]) -
                  float(gEditorKeys[DIK_A] || gEditorKeys[DIK_LEFT]);
        float y = float(gEditorKeys[DIK_W] || gEditorKeys[DIK_UP]) -
                  float(gEditorKeys[DIK_S] || gEditorKeys[DIK_DOWN]);
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
    if (gEditorDistance < 5) gEditorDistance = 5;
    if (gEditorDistance > 1500) gEditorDistance = 1500;
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
bool EditorPixel(int index, CeViewPoint *pixel) {
    if (!gEditorViewValid || !gEditorState.Object(index)->available) return false;
    const CeEditorPoint position = NativePosition(index);
    return CeViewProject(gEditorView, gEditorProjection, gEditorViewport,
                         CeViewPoint{position.x, position.y, position.z}, pixel);
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
    return hit;
}
bool EditorSave() {
    if (!gEditorReady || gEditorState.Moving()) {
        EditorNote("Finish the current move before saving.");
        return false;
    }
    char error[192];
    if (!gEditorDocument) {
        char name[80];
        snprintf(name, sizeof(name), "Installed map %d project", gEditorLevel);
        gEditorDocument = PortCeProjectDocumentNew(gEditorLevel, name, error, sizeof(error));
        if (!gEditorDocument) {
            EditorNote(error);
            return false;
        }
    }
    size_t count = 0;
    for (int i = 0; i < gEditorState.Count(); ++i) {
        const CeEditorObject *object = gEditorState.Object(i);
        if (!object->available) continue;
        gEditorPositions[count++] =
            CeProjectPosition{object->source, object->position.x, object->position.y, object->position.z};
    }
    if (!PortCeProjectDocumentUpdate(gEditorDocument, gEditorPositions, count, error, sizeof(error))) {
        EditorNote(error);
        return false;
    }
    const size_t size = PortCeProjectDocumentWrite(gEditorDocument, nullptr, 0, error, sizeof(error));
    if (!size) {
        EditorNote(error);
        return false;
    }
    char *bytes = static_cast<char *>(malloc(size));
    if (!bytes) {
        EditorNote("There is not enough memory to save the complete project.");
        return false;
    }
    bool ok = PortCeProjectDocumentWrite(gEditorDocument, bytes, size, error, sizeof(error)) == size;
    char name[128];
    if (ok) ok = PortCeEditorSave(gEditorLevel, bytes, size - 1, name, sizeof(name), error, sizeof(error));
    free(bytes);
    if (!ok) {
        EditorNote(error);
        return false;
    }
    snprintf(gEditorFilename, sizeof(gEditorFilename), "%s", name);
    gEditorState.MarkSaved();
    EditorNote("Saved a new project revision. Existing files were kept.");
    return true;
}
bool EditorOpenFile(int selected) {
    if (selected < 0 || selected >= gEditorFileCount || gEditorState.Moving()) return false;
    char error[192];
    size_t size = 0;
    char *bytes = PortCeEditorRead(gEditorFiles[selected].name, &size, error, sizeof(error));
    if (!bytes) {
        EditorNote(error);
        return false;
    }
    CeProjectDocument *document = PortCeProjectDocumentRead(bytes, size, error, sizeof(error));
    PortCeEditorFileFree(bytes);
    if (!document) {
        EditorNote(error);
        return false;
    }
    if (PortCeProjectDocumentLevel(document) != gEditorLevel) {
        PortCeProjectDocumentDelete(document);
        EditorNote("This project belongs to another installed map. Open that map first.");
        return false;
    }
    // Resolve every source before touching the scene. A partial or ambiguous project changes nothing.
    const size_t count = PortCeProjectDocumentCount(document);
    bool ok = true;
    int matches[CeEditorState::ObjectLimit];
    for (size_t p = 0; p < count; ++p) {
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
        EditorNote("A project source is missing, ambiguous, or no longer live. Nothing was changed.");
        return false;
    }
    // Start from the installed map positions, so omitted placements do not retain edits from another project.
    CeEditorState candidate;
    for (int i = 0; i < gEditorState.Count(); ++i) {
        CeEditorPoint position = gEditorSources[i].initial;
        for (size_t p = 0; p < count; ++p)
            if (matches[p] == i) {
                CeProjectPosition value;
                PortCeProjectDocumentPosition(document, p, &value);
                position = CeEditorPoint{value.x, value.y, value.z};
                break;
            }
        const CeEditorObject *original = gEditorState.Object(i);
        ok = ok && candidate.Add(original->source, position, original->identity, original->available);
        if (original->available) {
            if (!LiveActor(i)) ok = false;
            const CeEditorPoint base = gEditorSources[i].initial, actor = gEditorActorStart[i];
            if (!CeEditorState::Valid(CeEditorPoint{actor.x + position.x - base.x,
                                                    actor.y + position.y - base.y,
                                                    actor.z + position.z - base.z}))
                ok = false;
        }
    }
    if (!ok || !candidate.Seal()) {
        PortCeProjectDocumentDelete(document);
        EditorNote("The scene changed before the project could open. Nothing was changed.");
        return false;
    }
    gEditorState = candidate;
    for (int i = 0; i < gEditorState.Count(); ++i)
        if (gEditorState.Object(i)->available) (void)ApplyOne(i);
    PortCeProjectDocumentDelete(gEditorDocument);
    gEditorDocument = document;
    snprintf(gEditorFilename, sizeof(gEditorFilename), "%s", gEditorFiles[selected].name);
    gEditorDialog = DialogNone;
    gEditorState.MarkSaved();
    EditorNote("Project opened. Object links and mission source text were retained.");
    return true;
}
void EditorBrowse() {
    if (gEditorState.Moving()) return;
    char error[192];
    gEditorFileCount = PortCeEditorFiles(gEditorFiles, 2048, error, sizeof(error));
    if (gEditorFileCount < 0) {
        gEditorFileCount = 0;
        EditorNote(error);
        return;
    }
    if (!gEditorFileCount) {
        EditorNote("No saved projects yet. Save creates a revision in the CE user projects folder.");
        return;
    }
    gEditorFileIndex = gEditorFileTop = 0;
    gEditorDialog = DialogFiles;
    gEditorOrbit = false;
}
void EditorRequestOpen() {
    if (gEditorState.Dirty())
        gEditorDialog = DialogDiscardOpen;
    else
        (void)EditorOpenFile(gEditorFileIndex);
}
void EditorUndo(bool redo) {
    if (redo ? gEditorState.Redo() : gEditorState.Undo()) {
        if (!ApplyOne(gEditorState.Selected())) {
            EditorNote("The selected object changed; ending this editor session.");
            EditorExit();
        }
    }
}
void EditorToolbar(int item) {
    if (item == 0)
        (void)EditorSave();
    else if (item == 1)
        EditorBrowse();
    else if (item == 2)
        EditorUndo(false);
    else if (item == 3)
        EditorUndo(true);
    else if (item == 4) {
        if (gEditorState.Dirty())
            gEditorDialog = DialogDiscardExit;
        else
            EditorExit();
    }
}
bool EditorTrap(WORD key, int event) {
    if (!gEditorReady || !gEditorInputOwned) return false;
    if (event == KEY_EVENT_UP) {
        if (key < 256) gEditorKeys[key] = false;
        return true;
    }
    if (event == 3) {
        if (key == 0 && gEditorState.Moving()) (void)gEditorState.FinishMove(true);
        if (key == 2) gEditorOrbit = false;
        if ((key == 3 || key == 4) && gEditorDialog == DialogNone && !gEditorState.Moving())
            gEditorDistance *= key == 3 ? 0.8f : 1.25f;
        return true;
    }
    if (event == 4) {
        const float x = float(MOUSE_X), y = float(MOUSE_Y);
        if (gEditorDialog != DialogNone) {
            if (key == 0 && gEditorDialog == DialogFiles && x > 40 * gEditorScale &&
                x < gEditorWidth - 40 * gEditorScale) {
                const int row = int((y - 100 * gEditorScale) / (24 * gEditorScale));
                if (row >= 0 && row < 10 && gEditorFileTop + row < gEditorFileCount) {
                    gEditorFileIndex = gEditorFileTop + row;
                    EditorRequestOpen();
                }
            }
            return true;
        }
        if (key == 0 && y >= 34 * gEditorScale && y < 62 * gEditorScale && x >= 10 * gEditorScale &&
            x < 410 * gEditorScale) {
            EditorToolbar(int((x / gEditorScale - 10) / 80));
            return true;
        }
        if (y < 68 * gEditorScale || y >= gEditorHeight - 92 * gEditorScale) return true;
        if (key == 2) {
            gEditorOrbit = true;
            gEditorPointerX = x;
            gEditorPointerY = y;
            return true;
        }
        if (key == 0) {
            const int hit = EditorPick(x, y);
            if (!gEditorState.Select(hit)) return true;
            if (hit >= 0) {
                const CeEditorPoint initial = NativePosition(hit);
                CeEditorPoint point;
                if (EditorRay(x, y, initial.z, &point) && gEditorState.BeginMove()) {
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
    if (repeat) return true;
    if (gEditorDialog == DialogDiscardExit) {
        if (key == DIK_ESCAPE)
            gEditorDialog = DialogNone;
        else if (key == DIK_X)
            EditorExit();
        else if (key == DIK_S && EditorSave())
            EditorExit();
        return true;
    }
    if (gEditorDialog == DialogDiscardOpen) {
        if (key == DIK_ESCAPE)
            gEditorDialog = DialogFiles;
        else if (key == DIK_RETURN)
            (void)EditorOpenFile(gEditorFileIndex);
        return true;
    }
    if (gEditorDialog == DialogFiles) {
        if (key == DIK_ESCAPE)
            gEditorDialog = DialogNone;
        else if (key == DIK_RETURN)
            EditorRequestOpen();
        else if (key == DIK_UP && gEditorFileIndex > 0)
            --gEditorFileIndex;
        else if (key == DIK_DOWN && gEditorFileIndex + 1 < gEditorFileCount)
            ++gEditorFileIndex;
        if (gEditorFileIndex < gEditorFileTop) gEditorFileTop = gEditorFileIndex;
        if (gEditorFileIndex >= gEditorFileTop + 10) gEditorFileTop = gEditorFileIndex - 9;
        return true;
    }
    const bool control = gEditorKeys[DIK_LCONTROL] || gEditorKeys[DIK_RCONTROL];
    if (key == DIK_ESCAPE) {
        if (gEditorState.Moving()) {
            gEditorState.FinishMove(false);
            (void)ApplyOne(gEditorState.Selected());
        } else if (gEditorState.Dirty())
            gEditorDialog = DialogDiscardExit;
        else
            EditorExit();
    } else if (control && key == DIK_S)
        (void)EditorSave();
    else if (control && key == DIK_O)
        EditorBrowse();
    else if (control && key == DIK_Z)
        EditorUndo(false);
    else if (control && key == DIK_Y)
        EditorUndo(true);
    else if (key == DIK_TAB)
        (void)gEditorState.Cycle(gEditorKeys[DIK_LSHIFT] || gEditorKeys[DIK_RSHIFT] ? -1 : 1);
    else if (key == DIK_C)
        EditorFit(true);
    else if (key == DIK_HOME)
        EditorFit(false);
    else if ((key == DIK_PRIOR || key == DIK_NEXT) && gEditorState.Selected() >= 0 &&
             gEditorState.BeginMove()) {
        CeEditorPoint position = gEditorState.Object(gEditorState.Selected())->position;
        const float step = gEditorKeys[DIK_LSHIFT] || gEditorKeys[DIK_RSHIFT] ? 5 : 0.5f;
        position.z += key == DIK_PRIOR ? -step : step;
        (void)gEditorState.Move(position);
        (void)gEditorState.FinishMove(true);
        (void)ApplyOne(gEditorState.Selected());
    }
    return true;
}
void EditorBox(float x, float y, float width, float height, DWORD color) {
    CSPRITERENDERER::DrawColouredSprite(x, y, 0.001f, gEditorWhite, color,
                                        width / gEditorWhite->GetRealWidth(),
                                        height / gEditorWhite->GetRealHeight());
}
void EditorText(float x, float y, const char *text, DWORD color = 0xffe8edf2, float width = 0) {
    WCHAR converted[256];
    unsigned n = 0;
    for (; text[n] && n + 1 < 256; ++n)
        converted[n] =
            gEditorFont->CanDisplay(WCHAR((unsigned char)text[n])) ? WCHAR((unsigned char)text[n]) : L'-';
    converted[n] = 0;
    SIZE extent;
    gEditorFont->GetTextExtent(converted, &extent);
    float scale = gEditorScale;
    if (width > 0 && float(extent.cx) * scale > width && extent.cx > 0) scale = width / float(extent.cx);
    gEditorFont->DrawTextWithShadow(x, y, color, converted, 0, 0, scale, scale);
}
void EditorDrawBegin() {
    RENDERINFO.SetFogEnabled(false);
    RENDERINFO.SetLightingEnabled(false);
    LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
    LT.SRS(D3DRS_ALPHAREF, 8);
    LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
    LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
    LT.SRS(D3DRS_ZENABLE, FALSE);
    LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    RENDERINFO.Apply();
}
void EditorDrawEnd() {
    LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
    LT.SRS(D3DRS_ZENABLE, TRUE);
    RENDERINFO.Apply();
}
} // namespace
extern "C" bool PortCeEditorActive(void) { return gEditorRequested; }
extern "C" std::uint64_t PortCeEditorWorldViewToken() {
    return gEditorReady && gEditorCamera && gEditorLevel == GAME.GetCurrentLevel() &&
                   GAME.GetCamera(0) == gEditorCamera
               ? gEditorSessionToken
               : 0;
}
const char *PortCeEditorMessage() { return gEditorNote; }
bool PortCeEditorMousePixels() { return gEditorReady || gEditorOpening; }
bool PortCeEditorSuppressFlightHud() { return gEditorReady && gEditorCamera != nullptr; }
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
    if (!gEditorSessionToken || gEditorRequested || !PortCeMenus() || !FRONTEND.IsRunning() ||
        PortRecording() || !PortCeAuthoringSessionAllowed() ||
        (PortCeMuseumVisitActive && PortCeMuseumVisitActive()) ||
        (PortCeMuseumActive && PortCeMuseumActive()) || CLIPARAMS.mRecordDemo || CLIPARAMS.mPlaybackDemo ||
        CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode || installedLevel < 1 || installedLevel > 9999) {
        EditorNote("The editor needs an ordinary local CE menu session.");
        return false;
    }
    if (!gEditorCareer) gEditorCareer = static_cast<CCareer *>(malloc(sizeof(CCareer)));
    if (!gEditorCareer) {
        EditorNote("The editor could not preserve the current career in memory.");
        return false;
    }
    memcpy(gEditorCareer, &CAREER, sizeof(CCareer));
    ++gEditorSessionToken;
    if (!gEditorSessionToken) {
        EditorNote("The editor session identity limit was reached. Restart CE.");
        return false;
    }
    gEditorState.Reset();
    gEditorSourceCount = 0;
    gEditorLevel = installedLevel;
    gEditorReturnLevel = 0;
    gEditorRequested = true;
    gEditorWorldStarted = gEditorWorldComplete = gEditorReady = gEditorInputOwned = gEditorOpening = false;
    memset(gEditorKeys, 0, sizeof(gEditorKeys));
    gEditorDialog = DialogNone;
    gEditorOrbit = gEditorViewValid = false;
    gEditorYaw = 0;
    gEditorPitch = 1.05f;
    snprintf(gEditorFilename, sizeof(gEditorFilename), "New project");
    EditorNote("Loading the installed map for paused authoring.");
    FRONTEND.mSelectedLevel = installedLevel;
    FRONTEND.SetQuit(installedLevel);
    return true;
}
extern "C" bool PortCeEditorBeginWorld(int level, bool base) {
    if (!gEditorRequested) return false;
    if (base) return true;
    if (level != gEditorLevel || gEditorWorldStarted) {
        EditorNote("The editor's map load was changed or repeated.");
        return false;
    }
    gEditorWorldStarted = true;
    return true;
}
extern "C" bool PortCeEditorObserveWorldObject(const char *name, size_t capacity, float x, float y, float z) {
    if (!gEditorRequested || !gEditorWorldStarted || gEditorWorldComplete || !name || !capacity) return false;
    const char *end = static_cast<const char *>(memchr(name, 0, capacity));
    if (!end) {
        EditorNote("The map has an unterminated object identity.");
        return false;
    }
    const size_t length = size_t(end - name);
    if (!length) return true;
    if (length >= sizeof(SourceObject::name) || gEditorSourceCount == CeEditorState::ObjectLimit) {
        EditorNote("The map exceeds the editor's bounded object list.");
        return false;
    }
    size_t checked = 0;
    if (!SourceName(name, &checked) || !CeEditorState::Valid(CeEditorPoint{x, y, z})) {
        EditorNote("The map has an unsupported object identity or position.");
        return false;
    }
    SourceObject &object = gEditorSources[gEditorSourceCount++];
    memcpy(object.name, name, length + 1);
    object.initial = CeEditorPoint{x, y, z};
    return true;
}
extern "C" bool PortCeEditorEndWorld(bool base) {
    if (!gEditorRequested) return false;
    if (base) return true;
    gEditorWorldComplete = gEditorWorldStarted;
    return gEditorWorldComplete;
}
extern "C" bool PortCeEditorAfterLoad(void) {
    if (!gEditorRequested) return true;
    if (!gEditorWorldComplete || gEditorReady || GAME.IsMultiplayer() || GAME.GetNPlayers() != 1 ||
        !GAME.GetController(0) || !GAME.GetCamera(0) || GAME.IsFreeCameraOn(0)) {
        EditorNote("This map could not initialize a single-player authoring view.");
        return false;
    }
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
        if (!gEditorState.Add(gEditorSources[i].name, gEditorSources[i].initial,
                              available ? unsigned(actor->GetThingNumber()) : 0, available))
            return false;
    }
    if (!gEditorState.Seal()) {
        EditorNote("This map has no uniquely resolved named placements to edit.");
        return false;
    }
    gEditorPreviousCamera = GAME.GetCurrentCamera(0);
    if (!gEditorPreviousCamera || GAME.GetCamera(0) != gEditorPreviousCamera) {
        EditorNote("Another camera owns this map's starting view.");
        return false;
    }
    gEditorCamera = new (MEMTYPE_CAMERA) EditorCamera();
    if (!gEditorCamera) {
        gEditorPreviousCamera = nullptr;
        EditorNote("The editor camera could not be allocated.");
        return false;
    }
    gEditorCamera->mZoom = gEditorPreviousCamera->GetZoom();
    gEditorOriginalPause = GAME.IsPaused() != FALSE;
    GAME.*EditorGameAccess::PauseField() = TRUE;
    GAME.*EditorGameAccess::StateField() = GAME_STATE_PLAYING;
    GAME.SetCurrentCamera(0, gEditorCamera, false);
    gEditorReady = true;
    gEditorLastTime = PortClockRawNs();
    EditorFit(false);
    EditorCameraUpdate(0);
    PortCeDrawHooks(true);
    EditorNote("Paused authoring: move named placements; terrain and mission execution are not edited here.");
    return true;
}
void PortCeEditorFrame() {
    if (!gEditorReady) return;
    if (GAME.GetCurrentLevel() != gEditorLevel || !gEditorCamera || GAME.GetCamera(0) != gEditorCamera) {
        EditorNote("Camera ownership changed; returning without saving edits.");
        EditorExit();
        return;
    }
    GAME.*EditorGameAccess::PauseField() = TRUE;
    if (!gEditorInputOwned) {
        if (LT.GetKeytrap()) {
            EditorNote("Another input owner prevents authoring; returning safely.");
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
        gEditorPointerX = float(MOUSE_X);
        gEditorPointerY = float(MOUSE_Y);
    }
    if (LT.GetKeytrap() != (pKeyTrapper)EditorTrap || !GAME.GetController(0) ||
        GAME.GetController(0)->GetToControl() != &gEditorInput) {
        EditorExit();
        return;
    }
    const unsigned long long now = PortClockRawNs();
    float elapsed = now >= gEditorLastTime ? float(now - gEditorLastTime) * 1e-9f : 0;
    gEditorLastTime = now;
    if (elapsed > 0.1f) elapsed = 0.1f;
    EditorCameraUpdate(elapsed);
    if (gEditorState.Moving()) {
        CeEditorPoint point;
        if (EditorRay(float(MOUSE_X), float(MOUSE_Y), gEditorPlaneZ, &point)) {
            const int i = gEditorState.Selected();
            const CeEditorPoint origin = gEditorActorStart[i], initial = gEditorSources[i].initial;
            const CeEditorPoint position{point.x + gEditorDragOffset.x - origin.x + initial.x,
                                         point.y + gEditorDragOffset.y - origin.y + initial.y,
                                         initial.z + gEditorPlaneZ - origin.z};
            if (gEditorState.Move(position) && !ApplyOne(i)) {
                EditorNote("The selected object changed; returning safely.");
                EditorExit();
            }
        }
    }
}
bool PortCeEditorKey(unsigned key, bool repeat) {
    if (!gEditorReady || !gEditorInputOwned) return false;
    if (repeat) return true;
    return EditorTrap(WORD(key), KEY_EVENT_DOWN);
}
void PortCeEditorRender() {
    if (!gEditorReady || !gEditorCamera) return;
    gEditorFont = PLATFORM.Font(FONT_SMALL);
    gEditorWhite = HUD.GetWhiteTexture();
    gEditorWidth = float(PLATFORM.GetWindowWidth());
    gEditorHeight = float(PLATFORM.GetWindowHeight());
    if (!gEditorFont || gEditorFont->GetHeight() <= 0 || !gEditorWhite || gEditorWhite->GetRealWidth() <= 0 ||
        gEditorWhite->GetRealHeight() <= 0 || gEditorWidth <= 0 || gEditorHeight <= 0)
        return;
    gEditorScale = gEditorWidth / 640 < gEditorHeight / 480 ? gEditorWidth / 640 : gEditorHeight / 480;
    (void)EditorSnapshot();
    EditorDrawBegin();
    for (int i = 0; i < gEditorState.Count(); ++i) {
        CeViewPoint pixel;
        if (!EditorPixel(i, &pixel) || pixel.y < 68 * gEditorScale ||
            pixel.y >= gEditorHeight - 92 * gEditorScale)
            continue;
        const float half = (i == gEditorState.Selected() ? 6 : 3) * gEditorScale;
        const DWORD color = i == gEditorState.Selected() ? 0xffffd75e : 0xb05ac9e8;
        EditorBox(pixel.x - half, pixel.y - half, half * 2, gEditorScale, color);
        EditorBox(pixel.x - half, pixel.y + half, half * 2, gEditorScale, color);
        EditorBox(pixel.x - half, pixel.y - half, gEditorScale, half * 2, color);
        EditorBox(pixel.x + half, pixel.y - half, gEditorScale, half * 2, color);
    }
    EditorBox(0, 0, gEditorWidth, 68 * gEditorScale, 0xe0151d25);
    char title[200];
    snprintf(title, sizeof(title), "MAP EDITOR | map %d | %s%s", gEditorLevel, gEditorFilename,
             gEditorState.Dirty() ? " *" : "");
    EditorText(10 * gEditorScale, 8 * gEditorScale, title, 0xffe8edf2, gEditorWidth - 20 * gEditorScale);
    const char *buttons[] = {"Save", "Open", "Undo", "Redo", "Exit"};
    for (int i = 0; i < 5; ++i) {
        EditorBox((10 + 80 * i) * gEditorScale, 34 * gEditorScale, 74 * gEditorScale, 27 * gEditorScale,
                  0xff334552);
        EditorText((18 + 80 * i) * gEditorScale, 38 * gEditorScale, buttons[i]);
    }
    EditorBox(0, gEditorHeight - 92 * gEditorScale, gEditorWidth, 92 * gEditorScale, 0xe0151d25);
    if (gEditorState.Selected() >= 0) {
        const CeEditorObject *object = gEditorState.Object(gEditorState.Selected());
        snprintf(title, sizeof(title), "%s | X %.2f  Y %.2f  Z %.2f", object->source, object->position.x,
                 object->position.y, object->position.z);
    } else
        snprintf(title, sizeof(title), "Select a marker or press Tab | %d named records, %d unavailable",
                 gEditorState.Count(), gEditorState.Ambiguous());
    EditorText(10 * gEditorScale, gEditorHeight - 86 * gEditorScale, title, 0xffffd75e,
               gEditorWidth - 20 * gEditorScale);
    EditorText(10 * gEditorScale, gEditorHeight - 65 * gEditorScale,
               "Drag marker: move | Page Up/Down: height | C: focus | Home: frame map", 0xffc2cfda,
               gEditorWidth - 20 * gEditorScale);
    EditorText(10 * gEditorScale, gEditorHeight - 44 * gEditorScale,
               "WASD / arrows: pan | right drag: orbit | wheel: zoom | Ctrl+S/O/Z/Y", 0xffc2cfda,
               gEditorWidth - 20 * gEditorScale);
    EditorText(10 * gEditorScale, gEditorHeight - 23 * gEditorScale, gEditorNote, 0xffe8edf2,
               gEditorWidth - 20 * gEditorScale);
    if (gEditorDialog != DialogNone) {
        EditorBox(30 * gEditorScale, 76 * gEditorScale, gEditorWidth - 60 * gEditorScale, 280 * gEditorScale,
                  0xf018222c);
        if (gEditorDialog == DialogFiles) {
            EditorText(42 * gEditorScale, 80 * gEditorScale,
                       "Open project: arrows and Enter, or click a row; Esc cancels");
            for (int row = 0; row < 10 && gEditorFileTop + row < gEditorFileCount; ++row) {
                int i = gEditorFileTop + row;
                if (i == gEditorFileIndex)
                    EditorBox(40 * gEditorScale, (100 + row * 24) * gEditorScale,
                              gEditorWidth - 80 * gEditorScale, 23 * gEditorScale, 0xff344c5e);
                EditorText(46 * gEditorScale, (102 + row * 24) * gEditorScale, gEditorFiles[i].name);
            }
        } else if (gEditorDialog == DialogDiscardExit) {
            EditorText(44 * gEditorScale, 110 * gEditorScale, "There are unsaved edits.");
            EditorText(44 * gEditorScale, 144 * gEditorScale,
                       "S: save and exit   X: discard and exit   Esc: continue editing");
        } else {
            EditorText(44 * gEditorScale, 110 * gEditorScale,
                       "Opening a project will replace your unsaved edits.");
            EditorText(44 * gEditorScale, 144 * gEditorScale,
                       "Enter: discard and open   Esc: return to the project list");
        }
    }
    const float pointerX = float(MOUSE_X), pointerY = float(MOUSE_Y), arm = 6 * gEditorScale;
    EditorBox(pointerX - arm, pointerY, arm * 2, gEditorScale, 0xffffffff);
    EditorBox(pointerX, pointerY - arm, gEditorScale, arm * 2, 0xffffffff);
    EditorDrawEnd();
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
    }
    // The previous camera was detached, not deleted, on entry. An external owner may
    // have restored it already; never delete a camera which is back in the live slot.
    if (gEditorPreviousCamera) {
        if (GAME.GetCurrentCamera(0) != gEditorPreviousCamera) delete gEditorPreviousCamera;
        gEditorPreviousCamera = nullptr;
    }
    gEditorReady = false;
    gEditorOpening = false;
    gEditorViewValid = false;
    gEditorOrbit = false;
    memset(gEditorKeys, 0, sizeof(gEditorKeys));
    PortCeDrawHooks(PortCePhotoOn() || PortCeCmdSuppressFlightHud());
}
extern "C" void PortCeEditorCancel(void) {
    if (!gEditorRequested) return;
    PortCeEditorLevelEnd();
    if (gEditorCareer) memcpy(&CAREER, gEditorCareer, sizeof(CCareer));
    PortCeProjectDocumentDelete(gEditorDocument);
    gEditorDocument = nullptr;
    gEditorState.Reset();
    gEditorSourceCount = 0;
    gEditorRequested = gEditorWorldStarted = gEditorWorldComplete = false;
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
#endif
