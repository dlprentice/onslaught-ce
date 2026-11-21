// SPDX-License-Identifier: GPL-3.0-only
// Back Up All Careers: the controller between the Extras page and the storage
// interface (ce_career_restore.h). It never opens or writes a file itself.
//
// Start checks what the page knows (a folder, careers to back up, no other
// backup running, the game's gate) and either accepts or ends with the reason.
// The storage call runs on the game thread that opened the page, after the
// Working rows were drawn and a Present finished after them, exactly once.
// The game's career saves run on that same thread, so none can be in progress
// during the call; another process is caught by the store (Busy on its lock,
// Changed when a career or the options change while they are copied).
#include "ce_full_backup_ui.h"
#include "ce_career_restore.h"
#include "portlog.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {
#if defined(_WIN32) || defined(CE_FULL_BACKUP_UI_TEST_WINDOWS_COPY)
const bool kWindows = true;
#else
const bool kWindows = false;
#endif
using Status = CeCareerStorageStatus;

struct State {
  bool active = false, drawn = false, calling = false;
  std::thread::id owner;
  unsigned drawnAt = 0;
  CeFullBackupPhase phase = CE_FULL_BACKUP_IDLE;
  CeFullBackupOutcome outcome = CE_FULL_BACKUP_OUT_NONE;
  std::string user, game, package, root, set;
};
State &state() {
  static State *s = new State;
  return *s;
}
bool mine(const State &s) {
  return s.active && s.owner == std::this_thread::get_id();
}
void reset(State &s) {
  s.phase = CE_FULL_BACKUP_IDLE;
  s.outcome = CE_FULL_BACKUP_OUT_NONE;
  s.drawn = s.calling = false;
  s.drawnAt = 0;
  s.root.clear();
  s.set.clear();
}
void finish(State &s, CeFullBackupOutcome kind) {
  s.phase = CE_FULL_BACKUP_DONE;
  s.outcome = kind;
  s.drawn = s.calling = false;
}
// The storage interface takes a folder without a trailing separator.
std::string trimmed(const char *path) {
  std::string out = path ? path : "";
  while (out.size() > 1 && (out.back() == '/' || out.back() == '\\') &&
         !(out.size() == 3 && out[1] == ':'))
    out.pop_back();
  return out;
}
std::string leaf(const std::string &path) {
  const size_t at = path.find_last_of("/\\");
  return at == std::string::npos ? path : path.substr(at + 1);
}
CeFullBackupOutcome kindOf(Status status) {
  switch (status) {
  case Status::Complete:
    return CE_FULL_BACKUP_OUT_COMPLETE;
  case Status::Busy:
    return CE_FULL_BACKUP_OUT_BUSY;
  case Status::Changed:
    return CE_FULL_BACKUP_OUT_CHANGED;
  case Status::Uncertain:
    return CE_FULL_BACKUP_OUT_UNCERTAIN;
  case Status::Unsupported:
    return CE_FULL_BACKUP_OUT_UNSUPPORTED;
  default:
    return CE_FULL_BACKUP_OUT_REFUSED;
  }
}
// The storage call's owner, on the game thread's stack for the one call.
struct Call {
  bool (*permitted)() = nullptr;
  std::thread::id owner;
};
bool permittedHere(void *opaque) {
  const Call *call = static_cast<const Call *>(opaque);
  return call && call->owner == std::this_thread::get_id() &&
         call->permitted && call->permitted();
}
// Each hint is at most three lines: a message of one line (about 42
// characters) and a detail of at most two.
void words(const State &s, const char *&message, const char *&detail) {
  message = detail = "";
  switch (s.phase) {
  case CE_FULL_BACKUP_IDLE:
    message = "Back up every career and your options.";
    detail = "One new set in your backup folder. Your saves are only read.";
    return;
  case CE_FULL_BACKUP_WORKING:
    message = "Backing up every career and your options...";
    detail = "Each file is checked after it is written.";
    return;
  case CE_FULL_BACKUP_DONE:
    break;
  }
  switch (s.outcome) {
  case CE_FULL_BACKUP_OUT_NONE:
    return;
  case CE_FULL_BACKUP_OUT_COMPLETE:
    message = "Backup verified. Your careers are unchanged.";
    detail = "Saved as a new set in your backup folder.";
    return;
  case CE_FULL_BACKUP_OUT_UNCERTAIN:
    message = "Not confirmed: the new set may exist.";
    detail = "Check it in Backup Preview before another try. Nothing was "
             "removed.";
    return;
  case CE_FULL_BACKUP_OUT_REFUSED:
    message = "The backup could not be made.";
    detail = "No set was added. Your saves are unchanged.";
    return;
  case CE_FULL_BACKUP_OUT_BUSY:
    message = "Another backup or restore is in progress.";
    detail = "No set was added. Wait, then try again.";
    return;
  case CE_FULL_BACKUP_OUT_CHANGED:
    message = "A career or the options changed meanwhile.";
    detail = "No set was added. Your saves are as they are now. Try again.";
    return;
  case CE_FULL_BACKUP_OUT_UNSUPPORTED:
    if (kWindows) {
      message = "Back Up All Careers is unavailable on Windows.";
      detail = "Back Up Career still backs up one career.";
    } else {
      message = "This storage is not safe for a backup.";
      detail = "No set was added. Keep saves and backups on a local disk.";
    }
    return;
  case CE_FULL_BACKUP_OUT_AUTHORITY:
    message = "Careers cannot be backed up right now.";
    detail = "Nothing was written. Leave any editor, museum or recording "
             "first.";
    return;
  case CE_FULL_BACKUP_OUT_NO_FOLDER:
    message = "Choose a backup folder first.";
    detail = "Choose Folder picks one outside your game and careers.";
    return;
  case CE_FULL_BACKUP_OUT_NO_CAREERS:
    message = "There are no careers to back up.";
    detail = "Nothing was written. Save a career first.";
    return;
  }
}
} // namespace

void CeFullBackupUiOpen(const char *user, const char *game) {
  State &s = state();
  reset(s);
  s.active = true;
  s.owner = std::this_thread::get_id();
  s.user = trimmed(user);
  s.game = trimmed(game);
  const char *base = SDL_GetBasePath();
  s.package = trimmed(base);
}

void CeFullBackupUiLeave() {
  State &s = state();
  if (s.calling)
    return; // never while the call runs (it cannot: the call is synchronous)
  reset(s);
  s.active = false;
}

bool CeFullBackupUiStart(const CeFullBackupRequest *request) {
  State &s = state();
  if (!mine(s) || s.phase != CE_FULL_BACKUP_IDLE || !request)
    return false;
  if (!request->permitted || !request->permitted())
    finish(s, CE_FULL_BACKUP_OUT_AUTHORITY);
  else if (!request->root || !*request->root)
    finish(s, CE_FULL_BACKUP_OUT_NO_FOLDER);
  else if (request->otherBackup)
    finish(s, CE_FULL_BACKUP_OUT_BUSY);
  else if (request->careers == 0)
    finish(s, CE_FULL_BACKUP_OUT_NO_CAREERS);
  else if (request->careers < 0)
    finish(s, CE_FULL_BACKUP_OUT_REFUSED);
  else if (s.user.empty() || s.game.empty() || s.package.empty())
    finish(s, CE_FULL_BACKUP_OUT_REFUSED);
  else {
    s.root = trimmed(request->root);
    s.phase = CE_FULL_BACKUP_WORKING;
    s.drawn = false;
    return true;
  }
  return false;
}

void CeFullBackupUiDrawn(unsigned presentCount) {
  State &s = state();
  if (mine(s) && s.phase == CE_FULL_BACKUP_WORKING && !s.calling &&
      !s.drawn) {
    s.drawn = true;
    s.drawnAt = presentCount;
  }
}

void CeFullBackupUiPump(unsigned presentCount, bool (*permitted)()) {
  State &s = state();
  if (!mine(s) || s.phase != CE_FULL_BACKUP_WORKING || s.calling ||
      !s.drawn || unsigned(presentCount - s.drawnAt) == 0u)
    return;
  if (!permitted || !permitted()) {
    finish(s, CE_FULL_BACKUP_OUT_AUTHORITY);
    return;
  }
  Call call;
  call.permitted = permitted;
  call.owner = std::this_thread::get_id();
  CeCareerStorageRequest request;
  request.user = s.user;
  request.game = s.game;
  request.package = s.package;
  request.backupRoot = s.root;
  request.permitted = permittedHere;
  request.cancelled = nullptr; // no cancel is promised once this starts
  request.owner = &call;
  s.calling = true; // from here the call is made exactly once
  CeCareerStorageResult result;
  CeFullBackupOutcome kind = CE_FULL_BACKUP_OUT_UNCERTAIN;
  try {
    result = CeCareerBackupAll(request);
    kind = kindOf(result.status);
    // A set already published makes any other outcome Uncertain.
    if (kind != CE_FULL_BACKUP_OUT_COMPLETE && !result.backupSet.empty())
      kind = CE_FULL_BACKUP_OUT_UNCERTAIN;
  } catch (...) {
    kind = CE_FULL_BACKUP_OUT_UNCERTAIN;
  }
  std::fprintf(stderr,
               PORT_LOG_PREFIX "back up all careers: outcome %d after Present %u"
                               " (Working drawn before Present %u)\n",
               int(kind), presentCount, s.drawnAt);
  finish(s, kind);
  s.set = leaf(result.backupSet);
}

bool CeFullBackupUiAcknowledge() {
  State &s = state();
  if (!mine(s) || s.phase != CE_FULL_BACKUP_DONE)
    return false;
  reset(s);
  return true;
}

void CeFullBackupUiRead(CeFullBackupDisplay *out) {
  if (!out)
    return;
  std::memset(out, 0, sizeof(*out));
  const State &s = state();
  if (!s.active)
    return;
  out->phase = s.phase;
  out->outcome = s.outcome;
  const char *message = "", *detail = "";
  words(s, message, detail);
  std::snprintf(out->message, sizeof(out->message), "%s", message);
  std::snprintf(out->detail, sizeof(out->detail), "%s", detail);
  std::snprintf(out->set, sizeof(out->set), "%s", s.set.c_str());
}
