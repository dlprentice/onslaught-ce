// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_EDITOR_STORAGE_H
#define CE_EDITOR_STORAGE_H
#include <stddef.h>
struct CeEditorFile { char name[128]; unsigned size; };
// Only <current CE user folder>/projects is accessed. Listing never creates it.
// -1 is a refused/incomplete listing, zero is an empty or absent folder.
int PortCeEditorFiles(CeEditorFile* files, int capacity, char* error, size_t errorCapacity) noexcept;
// Saves an exclusive new revision; never overwrites an existing project. Returns its leaf filename.
bool PortCeEditorSave(int level, const char* bytes, size_t size, char* filename, size_t filenameCapacity,
                       char* error, size_t errorCapacity) noexcept;
// Caller owns the returned malloc allocation; free with PortCeEditorFileFree. No paths accepted, only listed leaf names.
char* PortCeEditorRead(const char* filename, size_t* size, char* error, size_t errorCapacity) noexcept;
void PortCeEditorFileFree(char* bytes) noexcept;
#endif
