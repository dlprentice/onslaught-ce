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
bool PortCeProjectBeginWorld(int level, bool base) noexcept;
bool PortCeProjectApplyPlacement(const char* name, size_t capacity, bool base,
                                 float* x, float* y, float* z) noexcept;
bool PortCeProjectEndWorld(bool base) noexcept;

// Native authoring uses the same strict v2 format as project launch. Opaque documents retain all supported
// fields, IDs, bindings and source text. Unknown fields are rejected instead of being dropped on save.
struct CeProjectDocument;
struct CeProjectPosition { const char* source; float x, y, z; };
CeProjectDocument* PortCeProjectDocumentNew(int level, const char* name, char* error, size_t capacity) noexcept;
CeProjectDocument* PortCeProjectDocumentRead(const char* bytes, size_t size, char* error, size_t capacity) noexcept;
void PortCeProjectDocumentDelete(CeProjectDocument* document) noexcept;
int PortCeProjectDocumentLevel(const CeProjectDocument* document) noexcept;
size_t PortCeProjectDocumentCount(const CeProjectDocument* document) noexcept;
bool PortCeProjectDocumentPosition(const CeProjectDocument* document, size_t index, CeProjectPosition* position) noexcept;
// One transaction: any invalid or repeated source refuses the entire update, leaving the document intact.
bool PortCeProjectDocumentUpdate(CeProjectDocument* document, const CeProjectPosition* positions, size_t count,
                                  char* error, size_t capacity) noexcept;
// Required bytes INCLUDING the terminating NUL; zero on refusal. A null destination queries the exact size.
size_t PortCeProjectDocumentWrite(const CeProjectDocument* document, char* bytes, size_t capacity,
                                  char* error, size_t errorCapacity) noexcept;
#endif
