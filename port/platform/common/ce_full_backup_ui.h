// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Back Up All Careers: the game/platform boundary of the Extras page that makes
// one full backup set (every career and the options file, ce_career_restore.h).
// Only plain records, fixed character arrays and function pointers cross it.
//
// Every call belongs to the game thread that called CeFullBackupUiOpen. The
// storage call is made by Pump on that thread, once, after the Working rows
// were drawn and a Present finished after them. It is never retried here.
enum CeFullBackupPhase {
  CE_FULL_BACKUP_IDLE,    // the offer; nothing begun
  CE_FULL_BACKUP_WORKING, // accepted; waiting for a drawn frame, then writing
  CE_FULL_BACKUP_DONE     // an outcome to read; nothing further happens
};
enum CeFullBackupOutcome {
  CE_FULL_BACKUP_OUT_NONE,
  CE_FULL_BACKUP_OUT_COMPLETE,    // a new verified set is in the folder
  CE_FULL_BACKUP_OUT_UNCERTAIN,   // a set may exist: inspect, never retried
  CE_FULL_BACKUP_OUT_REFUSED,     // no set was published
  CE_FULL_BACKUP_OUT_BUSY,        // another backup or restore holds the saves
  CE_FULL_BACKUP_OUT_CHANGED,     // a career or the options changed meanwhile
  CE_FULL_BACKUP_OUT_UNSUPPORTED, // not on this storage or this system
  CE_FULL_BACKUP_OUT_AUTHORITY,   // the session may not read careers for this
  CE_FULL_BACKUP_OUT_NO_FOLDER,   // no backup folder chosen
  CE_FULL_BACKUP_OUT_NO_CAREERS   // nothing to back up
};
// What the page knows when the player selects Back Up Now. permitted is the
// game's own gate, asked again from inside the storage call on this thread.
struct CeFullBackupRequest {
  const char *root;   // the backup folder, or empty
  int careers;        // careers the game lists now (0: none, -1: unreadable)
  bool otherBackup;   // the per-career backup is still running
  bool (*permitted)();
};
struct CeFullBackupDisplay {
  CeFullBackupPhase phase;
  CeFullBackupOutcome outcome;
  char message[192], detail[192];
  char set[201]; // the new set's folder name (never a path), when one exists
};
void CeFullBackupUiOpen(const char *user, const char *game);
void CeFullBackupUiLeave();
// Accepts the request (Working) or ends it at once with the reason (Done).
// Nothing is written by this call.
bool CeFullBackupUiStart(const CeFullBackupRequest *request);
// The Working rows were drawn in the frame the Present after presentCount shows.
void CeFullBackupUiDrawn(unsigned presentCount);
// Once an update. In Working it makes the one storage call, and only after a
// Present finished since the Working rows were drawn.
void CeFullBackupUiPump(unsigned presentCount, bool (*permitted)());
// The player has read the outcome.
bool CeFullBackupUiAcknowledge();
void CeFullBackupUiRead(CeFullBackupDisplay *out);
