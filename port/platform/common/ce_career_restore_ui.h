// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Restore as New: the game/platform boundary of the Backup Preview page's one
// writing action. The game has a different allocator, so only plain records,
// fixed character arrays and function pointers cross it.
//
// Every call below belongs to the game thread that called CeRestoreUiOpen. The
// only other thread is the controller's own read-only naming worker, which
// holds immutable copies and can never publish.
enum CeRestorePhase {
  CE_RESTORE_IDLE,      // nothing begun; offer says whether a restore can begin
  CE_RESTORE_PROPOSING, // read-only: a readable new name is being looked up
  CE_RESTORE_CONFIRM,   // the exact source and new name await the player
  CE_RESTORE_WORKING,   // confirmed; waiting for a drawn frame, then writing
  CE_RESTORE_DONE       // an outcome to read; nothing further happens by itself
};
enum CeRestoreOffer {
  CE_RESTORE_OFFER_NONE,     // no verified card
  CE_RESTORE_OFFER_READY,    // a verified, bound card on a supported system
  CE_RESTORE_OFFER_WINDOWS,  // preview only: no restore on Windows
  CE_RESTORE_OFFER_UNBOUND,  // verified, but its files could not be bound
  CE_RESTORE_OFFER_NO_FOLDER // no backup folder for the pre-change backup
};
enum CeRestoreOutcome {
  CE_RESTORE_OUT_NONE,
  CE_RESTORE_OUT_COMPLETE,
  CE_RESTORE_OUT_UNCERTAIN,   // a new career or backup may exist: inspect
  CE_RESTORE_OUT_REFUSED,
  CE_RESTORE_OUT_CHANGED,     // the backup, the card or the saves changed
  CE_RESTORE_OUT_OCCUPIED,    // the confirmed name was taken; none other chosen
  CE_RESTORE_OUT_BUSY,
  CE_RESTORE_OUT_CANCELLED,
  CE_RESTORE_OUT_UNSUPPORTED,
  CE_RESTORE_OUT_NAME,        // the game's filename semantics refused the pair
  CE_RESTORE_OUT_AUTHORITY,   // the session may not write careers now
  CE_RESTORE_OUT_FAILED       // the read-only lookup could not start
};
// The game's own gates, asked on the game thread only. permitted is asked again
// from inside the storage call, which runs on that same thread. namesCompatible
// receives the two exact leaf file names, each including its extension.
struct CeRestoreAuthority {
  bool (*permitted)();
  bool (*namesCompatible)(const char *sourceName, const char *targetName);
};
struct CeRestoreDisplay {
  CeRestorePhase phase;
  CeRestoreOffer offer;
  CeRestoreOutcome outcome;
  int careers;  // careers in the bound set
  int career;   // the one this record describes
  int complete, goodies;
  int possible; // 1 when a new career exists or may exist under target
  // source and target are exact leaf file names; set is the backup set's
  // folder name; backup is the pre-change backup made by this operation and
  // saves the folder that holds the new career.
  char source[201], target[80], date[65], set[201];
  char message[192], detail[192];
  char backup[4352], saves[4352];
};
void CeRestoreUiOpen();
void CeRestoreUiLeave();
// Begins the read-only name lookup for one explicitly chosen career of the
// verified card. Nothing is written and no name is reserved.
bool CeRestoreUiBegin(unsigned career, const CeRestoreAuthority *authority);
// Before confirmation: drops the lookup or the proposal. Nothing was written.
bool CeRestoreUiCancel();
// The player confirmed the pair shown. Nothing is written by this call.
bool CeRestoreUiConfirm(const CeRestoreAuthority *authority);
// The Working rows were drawn in the frame that the Present after presentCount
// will show. presentCount is the platform's own count of finished Presents.
void CeRestoreUiDrawn(unsigned presentCount);
// Once an update. Approves a delivered proposal on this thread; in Working it
// makes the one storage call, and only after a Present finished since the
// Working rows were drawn.
void CeRestoreUiPump(unsigned presentCount,
                     const CeRestoreAuthority *authority);
// The player has read the outcome.
bool CeRestoreUiAcknowledge();
void CeRestoreUiRead(unsigned career, CeRestoreDisplay *out);

#ifdef CE_CAREER_RESTORE_UI_INTERNAL
// Platform code only: never include this part from a game file.
#include <cstdint>
#include <string>
#include <vector>
// What the preview currently shows, copied under its lock. generation changes
// with every verification, so a record taken from one card never matches
// another, even of the same backup.
struct CeRestoreCard {
  bool open = false, verified = false;
  uint64_t page = 0, generation = 0;
  // stamp was taken by the verification that produced this card. It is empty
  // when the files could not be bound, and is never made again for an old card.
  std::string set, identity, stamp, user, game, package, root, date;
  struct Career {
    std::string name;
    int complete = 0, goodies = 0;
  };
  std::vector<Career> careers;
};
void CePreviewRestoreCard(CeRestoreCard &out);
#endif
