// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_PROJECT_H
#define CE_PROJECT_H
#include <stddef.h>

// Prototype entry: BEA_CE_PROJECT names an explicit .ceproject file; BEA_USER_DIR
// must name a separate owned custom directory. No career is read or written.
bool PortCeProjectRequested() noexcept;
// Pure startup validation: no profile/default-directory creation and no project file open.
bool PortCeProjectProfilePreflight(const char* gameRoot, const char* userRoot,
                                   size_t gameCapacity, size_t userCapacity) noexcept;
int PortCeProjectLaunchLevel() noexcept;
const char* PortCeProjectMessage() noexcept;
bool PortCeProjectLoaded() noexcept;
// The launch's own outcome for the process exit status: 0 once the linked map loaded with every placement
// applied, 1 otherwise. The game's shell drops CSystem::Run's result, so the Linux entry point reports this.
int PortCeProjectExitStatus() noexcept;
bool PortCeProjectCareerAllowed() noexcept;
// Shared native authoring entry fence. Query only: no launch, transport or profile changes.
bool PortCeAuthoringSessionAllowed() noexcept;
bool PortCeProjectPlacementActive() noexcept;
bool PortCeProjectBeginWorld(int level, bool base) noexcept;
bool PortCeProjectApplyPlacement(const char* name, size_t capacity, bool base,
                                 float* x, float* y, float* z, int* unitSide = nullptr) noexcept;
bool PortCeProjectEndWorld(bool base) noexcept;

// Native authoring and gameplay support v2 placements and v3 unit side overrides. Opaque documents retain all supported
// fields, IDs, bindings and source text. Unknown fields are rejected instead of being dropped on save.
struct CeProjectDocument;
// The most placements a project may hold (the editor asserts that its object table is at least this large).
enum { CeProjectPlacementLimit = 4096 };
#include "ce_hold.h"
struct CeProjectReach { bool enabled = false; float x = 0, y = 0, z = 0; CeHoldSettings hold{}; };
// An explicit solo walker start, independent of any placed carrier or unit.
struct CeProjectPlayerStart {
    bool enabled = false;
    float x = 0, y = 0, z = 0;
};
struct CeProjectPosition { const char* source; float x, y, z; int side = -1; };
CeProjectDocument* PortCeProjectDocumentNew(int level, const char* name, char* error, size_t capacity) noexcept;
CeProjectDocument* PortCeProjectDocumentRead(const char* bytes, size_t size, char* error, size_t capacity) noexcept;
void PortCeProjectDocumentDelete(CeProjectDocument* document) noexcept;
int PortCeProjectDocumentLevel(const CeProjectDocument* document) noexcept;
size_t PortCeProjectDocumentCount(const CeProjectDocument* document) noexcept;
CeProjectReach PortCeProjectDocumentReach(const CeProjectDocument* document, size_t index = 0) noexcept;
bool PortCeProjectDocumentPosition(const CeProjectDocument* document, size_t index, CeProjectPosition* position) noexcept;
// One transaction: any invalid or repeated source refuses the entire update, leaving the document intact.
bool PortCeProjectDocumentUpdate(CeProjectDocument* document, const CeProjectPosition* positions, size_t count,
                                  char* error, size_t capacity, const CeProjectReach* reach = nullptr, size_t reachCount = 1, const CeProjectPlayerStart* playerStart = nullptr) noexcept;
// Required bytes INCLUDING the terminating NUL; zero on refusal. A null destination queries the exact size.
size_t PortCeProjectDocumentWrite(const CeProjectDocument* document, char* bytes, size_t capacity,
                                  char* error, size_t errorCapacity) noexcept;

// A play test owns an immutable copy of one validated saved revision. Each consumer accepts exactly
// one level load, applies bindings before object initialization, and refuses missing or repeated names.
// Base-world objects never satisfy a level binding. Failure stays sticky for the rest of that load.
struct CeProjectPlayback;
CeProjectPlayback* PortCeProjectPlaybackNew(const CeProjectDocument* document, char* error, size_t capacity) noexcept;
void PortCeProjectPlaybackDelete(CeProjectPlayback* playback) noexcept;
bool PortCeProjectPlaybackBegin(CeProjectPlayback* playback, int level, bool base, char* error, size_t capacity) noexcept;
bool PortCeProjectPlaybackApply(CeProjectPlayback* playback, const char* name, size_t nameCapacity, bool base,
                               float* x, float* y, float* z, char* error, size_t capacity, int* unitSide = nullptr) noexcept;
bool PortCeProjectPlaybackEnd(CeProjectPlayback* playback, bool base, char* error, size_t capacity) noexcept;

CeProjectPlayerStart PortCeProjectDocumentPlayerStart(const CeProjectDocument* document) noexcept;
// Called before the native start creates its Battle Engine. Returns true only
// when a CE solo override was applied; ordinary play is not changed.
bool PortCeProjectApplyPlayerStart(int playerNumber, bool inverted, float& x, float& y, float& z, bool flying = false) noexcept;

#endif
