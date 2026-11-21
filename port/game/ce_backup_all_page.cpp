// SPDX-License-Identifier: GPL-3.0-only
// Back Up All Careers (an Extras page): one verified set of every career and the
// options file, in the backup folder Back Up Career and Backup Preview use. Its
// careers and options are only read. Back Up Now is the one action; the storage
// call is made on this thread, once, after a frame showing Working has been
// presented (../platform/common/ce_full_backup_ui.h). Backup Preview lists the
// new set beside the per-career sets and says what it holds.
//
// The page exists only on the front end's Extras pages (ce_extras.cpp closes them
// when the Onslaught CE page is not the active page, so never while a level runs)
// and only where the CE menus do (never in a replay, a recording or the demo). Its
// gate is asked again from inside the storage call.
#ifdef CE_VERSION
#include "common.h"
#include "ce_extras.h"
#include "../platform/common/ce_career_backup.h"
#include "../platform/common/ce_full_backup_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int CE_SettingGetText(const char *, char *, int);
extern "C" unsigned PortPresentCount(void); // finished Presents (d3d9hooks.cpp)
void PortCeBackupWhen(char *out, size_t cap, const char *text, bool dateOnly); // ce_backup_preview.cpp
int PortUserFileState(const char *rel); // port/platform/<target>: 1 the file is there, 0 none, -1 not known
const char *PortUserDir();
const char *PortGameDir();
bool PortCeMenus();
bool PortCeProjectCareerAllowed() noexcept;
namespace {
CeBackupStatus folderView{}; // the folder choice, shared with Back Up Career's chooser
CeFullBackupDisplay flow{};
int careers = -1, options = -1;
bool allowed = false, workRows = false;
// No editor, museum or project has the careers fenced, no recording or replay
// tape, and the Extras pages are showing (only on the front end).
bool permit() {
  const char *r = getenv("BEA_PORT_RECORD"), *t = getenv("BEA_PORT_TAPE");
  return PortCeMenus() && PortCeProjectCareerAllowed() && PortCeExtrasActive() &&
         (!r || !*r) && (!t || !*t);
}
void plain(WCHAR *dst, int n, const char *s) { PortCeExtrasWiden(dst, n, s); }
const char *leafOf(const char *path) {
  const char *l = path;
  for (const char *c = path; *c; ++c)
    if (*c == '/' || *c == '\\')
      l = c + 1;
  return l;
}
void count() {
  careers = allowed ? PortCeCareerCount() : -1;
  options = allowed ? PortUserFileState("defaultoptions.bea") : -1;
}
void refresh() {
  CeBackupPoll(&folderView);
  CeFullBackupUiRead(&flow);
}
// The rows of each step. The offer: Back Up Now first (what the page is for),
// Choose Folder, the folder ("Folder", its name or "Not chosen"), what goes in, and Back last.
enum Act { A_NONE, A_NOW, A_CHOOSE, A_BACK, A_DONE };
struct Line {
  const char *left;
  char right[24];
  int tone, act;
  bool dim, folder;
};
Line lines[8];
int lineCount = 0;
Line &add(const char *left, const char *right, int act = A_NONE) {
  Line &l = lines[lineCount < 8 ? lineCount++ : 7];
  memset(&l, 0, sizeof(l));
  l.left = left;
  snprintf(l.right, sizeof(l.right), "%s", right);
  l.act = act;
  return l;
}
// The folder chooser is open (Back Up Career's, shared): nothing starts meanwhile.
bool picking() { return folderView.state == CE_BACKUP_PICKING; }
void build() {
  lineCount = 0;
  if (!allowed) {
    add("Back", "", A_BACK);
    return;
  }
  char number[16];
  switch (flow.phase) {
  case CE_FULL_BACKUP_IDLE: {
    Line &now = add("Back Up Now", "", A_NOW);
    now.dim = !folderView.folder[0] || careers == 0 || picking();
    add("Choose Folder", "", A_CHOOSE).dim = picking();
    add("", "").folder = true;
    snprintf(number, sizeof(number), "%d", careers);
    add("Careers", careers >= 0 ? number : "Unreadable");
    add("Options file", options == 1 ? "Included" : options == 0 ? "None" : "Unknown");
    add("Back", "", A_BACK);
    return;
  }
  case CE_FULL_BACKUP_WORKING:
    add("Working", "Please wait").dim = true;
    return;
  case CE_FULL_BACKUP_DONE: {
    const bool made = flow.outcome == CE_FULL_BACKUP_OUT_COMPLETE;
    const bool unsure = flow.outcome == CE_FULL_BACKUP_OUT_UNCERTAIN;
    Line &head = add(made ? "Backup verified" : unsure ? "Backup not confirmed" : "Not backed up",
                     made ? "Done" : unsure ? "Check" : "Refused");
    head.tone = made ? CE_TONE_GOOD : CE_TONE_BAD;
    if (flow.set[0])
      add("", "").folder = true;
    add("Your careers", "Unchanged");
    add("Back", "", A_DONE);
    return;
  }
  }
}
void sync() {
  refresh();
  build();
}
void open() {
  allowed = permit();
  workRows = false;
  char folder[4096] = {};
  if (allowed)
    CE_SettingGetText("career_backup_folder", folder, sizeof(folder));
  CeBackupEnter(folder);
  CeFullBackupUiOpen(PortUserDir(), PortGameDir());
  count();
  sync();
}
void close() {
  CeFullBackupUiLeave();
  CeBackupLeave();
  memset(&flow, 0, sizeof(flow));
  lineCount = 0;
  workRows = false;
}
void update() {
  if (allowed && !permit()) {
    close();
    allowed = false;
  }
  // In Working this makes the storage call, and only once a Present has
  // finished since draw() saw the Working rows drawn.
  if (allowed) {
    const CeFullBackupPhase before = flow.phase;
    CeFullBackupUiPump(PortPresentCount(), permit);
    refresh();
    if (before == CE_FULL_BACKUP_WORKING && flow.phase == CE_FULL_BACKUP_DONE)
      count();
  }
  sync();
}
// Called by the list box after it drew this frame's rows and hint.
void draw(float) {
  if (allowed && flow.phase == CE_FULL_BACKUP_WORKING && workRows)
    CeFullBackupUiDrawn(PortPresentCount());
  workRows = false;
}
int rows() { return lineCount; }
void row(int i, SCeListRow *out) {
  if (i < 0 || i >= lineCount)
    return;
  const Line &l = lines[i];
  if (l.folder && flow.phase == CE_FULL_BACKUP_DONE && flow.set[0]) {
    char when[96], text[160];
    PortCeBackupWhen(when, sizeof(when), flow.set, false);
    snprintf(text, sizeof(text), "Set %s", when);
    PortCeExtrasWidenName(out->mLeft, 80, text);
    out->mTone = CE_TONE_DIM;
    plain(out->mRight, 24, l.right);
  } else if (l.folder) {
    // The chosen folder, labelled like the rows under it: "Folder" on the left, its name (or "Not chosen") on the right,
    // so a refusal for want of a folder never comes as a surprise. A name too long for the column ends in "...".
    char name[24];
    const char *leaf = folderView.folder[0] ? leafOf(folderView.folder) : "Not chosen";
    if (strlen(leaf) < sizeof(name))
      snprintf(name, sizeof(name), "%s", leaf);
    else
      snprintf(name, sizeof(name), "%.*s...", int(sizeof(name) - 4), leaf);
    plain(out->mLeft, 80, "Folder");
    plain(out->mRight, 24, name);
  } else {
    plain(out->mLeft, 80, l.left);
    plain(out->mRight, 24, l.right);
  }
  if (!l.folder)
    out->mTone = l.tone;
  out->mDim = l.dim;
  if (flow.phase == CE_FULL_BACKUP_WORKING)
    workRows = true;
}
void hint(int i, WCHAR *help, WCHAR *extra, int n) {
  if (!allowed) {
    plain(help, n, "Backups are unavailable in this session.");
    plain(extra, n, "Nothing was written.");
    return;
  }
  if (flow.phase != CE_FULL_BACKUP_IDLE) {
    plain(help, n, flow.message);
    plain(extra, n, flow.detail);
    return;
  }
  if (picking()) {
    plain(help, n, folderView.message);
    plain(extra, n, "You can go back at any time.");
    return;
  }
  if (i < 0 || i >= lineCount)
    return;
  const Line &l = lines[i];
  if (l.act == A_NOW) {
    plain(help, n, flow.message);
    plain(extra, n, flow.detail);
  } else if (l.act == A_CHOOSE) {
    plain(help, n, "Choose a folder outside your game and careers.");
    plain(extra, n, folderView.state == CE_BACKUP_ERROR ? folderView.message : "");
  } else if (l.folder) {
    plain(help, n, "Your backups go into this folder.");
    plain(extra, n, "Choose Folder changes it.");
  } else if (!strcmp(l.left, "Careers")) {
    plain(help, n, "Every career in your saves goes into the set.");
    plain(extra, n, "Each is copied and checked. None is changed.");
  } else if (!strcmp(l.left, "Options file")) {
    plain(help, n, options == 1 ? "Your options file goes into the set too."
                                : "There is no options file to include.");
    plain(extra, n, "ce.ini is not part of the set.");
  } else if (l.act == A_BACK) {
    plain(help, n, "Return to Extras without making a backup.");
  }
}
bool select(int i) {
  if (i < 0 || i >= lineCount)
    return false;
  const int act = lines[i].act;
  bool did = false;
  if (act == A_BACK) {
    PortCeBackupAllBack();
    return true;
  }
  if (act == A_DONE) {
    CeFullBackupUiAcknowledge();
    PortCeBackupAllBack();
    return true;
  }
  if (!allowed)
    return false;
  if (act == A_CHOOSE && flow.phase == CE_FULL_BACKUP_IDLE && !picking()) {
    CeBackupChoose();
    did = true;
  } else if (act == A_NOW && flow.phase == CE_FULL_BACKUP_IDLE && !picking()) {
    CeBackupPoll(&folderView);
    count();
    const CeFullBackupRequest request = {folderView.folder, careers,
                                         folderView.state == CE_BACKUP_OTHER, permit};
    CeFullBackupUiStart(&request);
    did = true;
  }
  sync();
  return did;
}
const WCHAR *empty() { return L""; }
const SCeListPage page = {L"Back Up All Careers", rows, row, hint, select, empty, open, close, update,
                          draw, 0.78f, 0};
} // namespace
const SCeListPage *PortCeBackupAllPage() { return &page; }
#endif
