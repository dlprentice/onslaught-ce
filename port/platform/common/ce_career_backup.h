// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>

enum CeBackupState { CE_BACKUP_IDLE, CE_BACKUP_PICKING, CE_BACKUP_WORKING, CE_BACKUP_OTHER, CE_BACKUP_OK, CE_BACKUP_ERROR };
struct CeBackupStatus { CeBackupState state; char folder[4096], location[4352], message[160]; };
// Identifies the actual Windows directory entry selected when the record opened.
struct CeBackupOrigin {
    char file[256];
    unsigned long long volume, written, changed;
    unsigned char id[16];
};
#ifdef _WIN32
bool CeBackupRememberWindows(const char *user, const void *typedName, CeBackupOrigin &origin);
#endif
struct CeBackupInput {
    const char *user, *game, *package, *folder, *filename;
    const unsigned char *selected;
    size_t length;
    const CeBackupOrigin *origin = nullptr;
};

// The caller owns the snapshot. Only the new set under folder is writable.
bool CeBackupStore(const CeBackupInput &input, char *location, size_t room, const char *&error);
void CeBackupEnter(const char *configured);
void CeBackupLeave();
void CeBackupChoose();
void CeBackupPoll(CeBackupStatus *out);
bool CeBackupStart(const char *user, const char *game, const char *filename,
                   const unsigned char *selected, size_t length, const CeBackupOrigin *origin = nullptr);
