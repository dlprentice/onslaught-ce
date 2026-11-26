// SPDX-License-Identifier: GPL-3.0-only
// A backup can be inspected even after its original career is gone. Looking,
// choosing a set and choosing a career never install anything. The one writing
// action is Restore as New: it starts from the verified card showing here, the
// player sees the exact new name and confirms it (Cancel is highlighted first),
// and the storage call is made on this thread, once, after a frame showing
// Working has been presented (../platform/common/ce_career_restore_ui.h).
#ifdef CE_VERSION
#include "common.h"
#include "ce_extras.h"
#include "ce_name_text.h"
#include "../platform/common/ce_backup_preview_ui.h"
#include "../platform/common/ce_career_restore_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
const char *PortUserDir();
const char *PortGameDir();
bool PortCeMenus();
bool PortCeProjectCareerAllowed() noexcept;
extern "C" int CE_SettingGetText(const char *, char *, int);
extern "C" unsigned PortPresentCount(void); // finished Presents (d3d9hooks.cpp)
// A backup time as the player reads it, in this computer's local time. It takes
// a set's folder name (onslaught-backup-YYYYMMDD-HHMMSS..., UTC) or a manifest
// time (YYYY-MM-DDTHH:MM:SSZ, UTC); anything else is shown as it is.
void PortCeBackupWhen(char *out, size_t cap, const char *text, bool dateOnly) {
  int f[6] = {};
  const size_t p = sizeof("onslaught-backup-") - 1, n = strlen(text);
  bool ok = false;
  if (n >= p + 15 && !strncmp(text, "onslaught-backup-", p) && text[p + 8] == '-')
    ok = sscanf(text + p, "%4d%2d%2d-%2d%2d%2d", &f[0], &f[1], &f[2], &f[3],
                &f[4], &f[5]) == 6;
  else if (n >= 19 && text[10] == 'T')
    ok = sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d", &f[0], &f[1], &f[2], &f[3],
                &f[4], &f[5]) == 6;
  struct tm t = {}, local = {};
  if (ok) {
    t.tm_year = f[0] - 1900;
    t.tm_mon = f[1] - 1;
    t.tm_mday = f[2];
    t.tm_hour = f[3];
    t.tm_min = f[4];
    t.tm_sec = f[5];
#ifdef _WIN32
    const time_t at = _mkgmtime(&t);
    ok = at != (time_t)-1 && localtime_s(&local, &at) == 0;
#else
    const time_t at = timegm(&t);
    ok = at != (time_t)-1 && localtime_r(&at, &local) != NULL;
#endif
  }
  if (!ok || !strftime(out, cap, dateOnly ? "%Y-%m-%d" : "%Y-%m-%d  %H:%M:%S",
                       &local))
    snprintf(out, cap, "%s", text);
}
// The game's own filename semantics, asked on the game thread only: whether a
// career copied from sourceName behaves the same under targetName. Both are
// exact leaf file names with their extension. False refuses the pair.
bool PortCeRestoreNamesCompatible(const char *sourceName,
                                  const char *targetName);
namespace {
CePreviewDisplay view{};
CeRestoreDisplay flow{};
int current = 0;
bool allowed = false;
// What the page is showing. Browse is the preview itself; the others are the
// steps of Restore as New, each a short list of its own.
enum { S_BROWSE, S_PICK, S_LOOKUP, S_CONFIRM, S_WORK, S_DONE };
enum { A_NONE, A_CANCEL, A_PICK, A_CONFIRM, A_DONE };
enum { kRestoreRow = 0, kLines = 72 };
// The browse list, in order: what the player came to do first (a set to view,
// or Restore as New once a set is verified), then what the card shows, then
// the other choices, and Back last as on the game's own pages.
enum Kind { K_NONE, K_BACK, K_CHOOSE, K_AGAIN, K_STATUS, K_DATE, K_FILES,
            K_CAREER, K_MISSIONS, K_GOODIES, K_RESTORE, K_EARLIER, K_SET };
struct Line {
  char left[160], right[24], help[192], extra[192];
  int tone, act, arg;
  bool dim, name, hintName;
};
Line lines[kLines], spare;
int lineCount = 0;
int screen = S_BROWSE;
bool picking = false;  // choosing which career of a set with several
bool reshow = false;   // the page is only moving its highlight, not opening
bool workRows = false; // the Working rows were drawn in this frame
int earlier = 0;       // this visit: 1 a career was created, 2 one may exist
bool permit() {
  const char *r = getenv("BEA_PORT_RECORD"), *t = getenv("BEA_PORT_TAPE");
  return PortCeMenus() && PortCeProjectCareerAllowed() && (!r || !*r) &&
         (!t || !*t);
}
const CeRestoreAuthority kAuthority = {permit, PortCeRestoreNamesCompatible};
int baseRows(){return view.careers?11:4;}
Kind kind(int i, int *set) {
  if (!allowed)
    return i == 0 ? K_STATUS : i == 1 ? K_BACK : K_NONE;
  if (view.careers > 0) {
    static const Kind card[] = {K_RESTORE, K_STATUS, K_DATE,     K_FILES,
                                K_CAREER,  K_MISSIONS, K_GOODIES, K_EARLIER};
    if (i < 8)
      return card[i];
    i -= 8;
  }
  if (i < view.choices) {
    *set = i;
    return K_SET;
  }
  i -= view.choices;
  if (!view.careers) {
    if (i == 0)
      return K_STATUS;
    --i;
  }
  static const Kind tail[] = {K_AGAIN, K_CHOOSE, K_BACK};
  return i >= 0 && i < 3 ? tail[i] : K_NONE;
}
int count(){return !allowed?2:screen==S_BROWSE?baseRows()+view.choices:lineCount;}
void plain(WCHAR*dst,int n,const char*s){PortCeExtrasWiden(dst,n,s);}
void widenUtf8(WCHAR *dst, int n, const char *s) { CeDecodeName(dst, n, s, true); }
// A set's folder name as the player reads it: its local date and time (a
// same-second suffix is left off; the list's order still tells such sets apart).
void setLabel(char *out, size_t cap, const char *s) {
  PortCeBackupWhen(out, cap, s, false);
}
// The leaf of a path, for showing a folder by its own name only (handling trailing separators and root paths).
void leafOf(char *out, size_t cap, const char *path) {
  if (!cap)
    return;
  out[0] = 0;
  if (!path || !*path)
    return;
  size_t end = strlen(path);
  while (end > 1 && (path[end - 1] == '/' || path[end - 1] == '\\')) {
    if (end == 3 && path[1] == ':')
      break;
    --end;
  }
  if ((end == 1 && (path[0] == '/' || path[0] == '\\')) ||
      (end == 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))) {
    snprintf(out, cap, "%.*s", int(end), path);
    return;
  }
  size_t start = end;
  while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\')
    --start;
  if (start == end) {
    snprintf(out, cap, "%c", path[0]);
    return;
  }
  snprintf(out, cap, "%.*s", int(end - start), path + start);
}
// A manifest time ("2026-11-17T13:11:53Z") as the list shows times.
void readableTime(char *out, size_t cap, const char *iso) {
  PortCeBackupWhen(out, cap, iso, false);
}
// Why the set was made, in the player's words (the manifest's Reason).
const char *madeBy(const char*reason){if(!strncmp(reason,"Manual: selected",16))return "Made with Back Up Career.";if(!strcmp(reason,"Manual"))return "Made by Restore as New, before it wrote.";if(!strcmp(reason,"All careers"))return "Made with Back Up All Careers.";return "";}
// What a listed set holds, as the list says it: a short word for the row's
// right and a sentence for its hint ("All careers and options, 3 careers.").
// A set made by Back Up All Careers or before a restore holds every career.
void holds(unsigned set, char *word, size_t wordCap, char *line, size_t lineCap,
           const char **extra) {
  CePreviewSetHolds h;
  *extra = "Selecting a set restores nothing.";
  if (!CePreviewDisplaySetHolds(set, &h)) {
    snprintf(word, wordCap, "View set");
    snprintf(line, lineCap, "Select to verify this backup set.");
    return;
  }
  if (!h.verified) {
    snprintf(word, wordCap, "Not verified");
    snprintf(line, lineCap, "This set did not verify. Select to see why.");
    return;
  }
  char count[32];
  snprintf(count, sizeof(count), h.careers == 1 ? "%d career" : "%d careers",
           h.careers);
  const bool all = !strcmp(h.reason, "All careers") || !strcmp(h.reason, "Manual");
  if (all) {
    snprintf(word, wordCap, "%s", !strcmp(h.reason, "Manual") ? "Before restore" : "All careers");
    snprintf(line, lineCap, "%s, %s.", h.options ? "All careers and options" : "All careers", count);
    *extra = madeBy(h.reason);
  } else if (!strncmp(h.reason, "Manual: selected", 16) && h.careers == 1 && !h.options) {
    snprintf(word, wordCap, "One career");
    snprintf(line, lineCap, "One career, made with Back Up Career.");
  } else {
    snprintf(word, wordCap, "%s", count);
    snprintf(line, lineCap, h.options ? "%s and options." : "%s.", count);
  }
}
// A career as Load Game lists it: its file name without the extension.
void careerName(char *out, size_t cap, const char *file) {
  size_t n = strlen(file);
  if (n > 4 && file[n - 4] == '.' && (file[n - 3] | 0x20) == 'b' &&
      (file[n - 2] | 0x20) == 'e' && (file[n - 1] | 0x20) == 's')
    n -= 4;
  snprintf(out, cap, "%.*s", int(n), file);
}
Line *add(const char *left, const char *right, const char *help,
          const char *extra, int act = A_NONE) {
  Line *l = lineCount < kLines ? &lines[lineCount++] : &spare;
  memset(l, 0, sizeof(*l));
  snprintf(l->left, sizeof(l->left), "%s", left);
  snprintf(l->right, sizeof(l->right), "%s", right);
  snprintf(l->help, sizeof(l->help), "%s", help);
  snprintf(l->extra, sizeof(l->extra), "%s", extra);
  l->act = act;
  return l;
}
void buildPick() {
  static CePreviewDisplay each;
  for (int i = 0; i < view.careers && i < 32; ++i) {
    char name[208], text[192];
    CePreviewDisplayRead(unsigned(i), &each);
    careerName(name, sizeof(name), each.name);
    snprintf(text, sizeof(text),
             "%d missions completed, %d Goodies found.",
             each.complete, each.goodies);
    Line *l = add(name, "Choose", text,
                  "Select to see its new name. Nothing is written yet.", A_PICK);
    l->name = true;
    l->arg = i;
  }
  add("Cancel", "", "Return to the preview. Nothing is written.", "", A_CANCEL);
}
void buildConfirm(const char *source, const char *target) {
  char text[192], more[192], date[16];
  add("Restore as New", "", "Create the new career now.",
      "Once it starts, it cannot be cancelled.", A_CONFIRM);
  PortCeBackupWhen(date, sizeof(date), flow.date, true);
  setLabel(text, sizeof(text), flow.set);
  add("From backup", date, text,
      "This verified set is only read. It is never changed.")->hintName = true;
  snprintf(more, sizeof(more), "Backup file: %s", flow.source);
  add("Career in this backup", "", "This backed-up career is copied.", more)
      ->hintName = true;
  Line *l = add(source, "", "This backed-up career is copied.", more);
  l->name = l->hintName = true;
  snprintf(text, sizeof(text), "%d", flow.complete);
  add("Missions completed", text, "Read from this backed-up career.", "");
  snprintf(more, sizeof(more),
           "File: %s. The name is not reserved until it is written.",
           flow.target);
  add("New career name", "",
      "A separate new career with this name is added to your saves.", more)
      ->hintName = true;
  l = add(target, "New",
          "A separate new career with this name is added to your saves.", more);
  l->name = l->hintName = true;
  add("Your current careers", "Kept",
      "No career is replaced or loaded. All of them are backed up first.",
      "That backup is a new set in your backup folder.");
  add("Preferences", "Not restored",
      "No settings are restored.",
      "Your preferences file is backed up with your careers.");
  add("Cancel", "", "Return to the preview. Nothing is written.",
      "Your saves, settings and backups stay as they are.", A_CANCEL);
}
void buildDone(const char *source, const char *target) {
  const bool made = flow.outcome == CE_RESTORE_OUT_COMPLETE;
  const bool unsure = flow.outcome == CE_RESTORE_OUT_UNCERTAIN;
  (void)source;
  // Three facts: the new career and its name, where to find it, and that the
  // other careers were kept. Folders are named, never spelled out as paths.
  Line *l;
  if (flow.target[0]) {
    l = add(target, made ? "Created" : unsure ? "Check" : "Not created",
            flow.message, flow.detail);
    l->name = true;
    l->tone = made ? CE_TONE_GOOD : unsure ? CE_TONE_BAD : CE_TONE_DIM;
  } else {
    l = add("No career created", "", flow.message, flow.detail);
    l->tone = CE_TONE_DIM;
  }
  if (flow.possible)
    add("Find it in Load Game", "",
        made ? "Open Load Game from the main menu."
             : "Open Load Game and look for the name above.",
        made ? "It was not loaded." : "Look before trying again.");
  char set[64] = "", text[192] = "";
  if (flow.backup[0]) {
    char leaf[256];
    leafOf(leaf, sizeof(leaf), flow.backup);
    setLabel(set, sizeof(set), leaf);
    snprintf(text, sizeof(text), "Backed up first as the set of %s.", set);
  }
  add("Your other careers", unsure ? "Kept" : "Unchanged",
      unsure ? "Nothing was replaced or removed."
             : "Nothing was replaced, removed or loaded.",
      text);
  add("Back to Backup Preview", "", "Return to the preview.", "", A_DONE);
}
void build(int which) {
  lineCount = 0;
  char source[208], target[88];
  careerName(source, sizeof(source), flow.source);
  careerName(target, sizeof(target), flow.target);
  switch (which) {
  case S_PICK:
    buildPick();
    break;
  case S_LOOKUP:
    add("Checking for a free name", "Please wait", flow.message, flow.detail)
        ->dim = true;
    add("Cancel", "", "Stop and return to the preview. Nothing is written.", "",
        A_CANCEL);
    break;
  case S_CONFIRM:
    buildConfirm(source, target);
    break;
  case S_WORK:
    add("Working", "Please wait", flow.message, flow.detail);
    add(target, "", flow.message, flow.detail)->name = true;
    break;
  case S_DONE:
    buildDone(source, target);
    break;
  }
}
int screenNow() {
  if (!allowed)
    return S_BROWSE;
  switch (flow.phase) {
  case CE_RESTORE_PROPOSING:
    return S_LOOKUP;
  case CE_RESTORE_CONFIRM:
    return S_CONFIRM;
  case CE_RESTORE_WORKING:
    return S_WORK;
  case CE_RESTORE_DONE:
    return S_DONE;
  default:
    break;
  }
  return picking ? S_PICK : S_BROWSE;
}
void refresh() {
  if (allowed) {
    CePreviewDisplayRead(unsigned(current), &view);
    CeRestoreUiRead(unsigned(current), &flow);
  }
  if (current < 0 || current >= view.careers)
    current = 0;
  if (picking && (flow.offer != CE_RESTORE_OFFER_READY || view.careers < 2))
    picking = false;
  if (flow.phase == CE_RESTORE_DONE) {
    if (flow.outcome == CE_RESTORE_OUT_UNCERTAIN)
      earlier = 2;
    else if (flow.outcome == CE_RESTORE_OUT_COMPLETE && earlier != 2)
      earlier = 1;
  }
}
// Moves the highlight through the host's own way of showing a page; open()
// sees reshow and leaves everything as it is.
void focus(int row) {
  reshow = true;
  PortCeExtrasShow(XP_BACKUP_PREVIEW, row);
  reshow = false;
}
// Reads both displays and, when the step changed, highlights a row: the thing
// to do first in the preview and when choosing a career; Cancel (last) while a
// name is looked up or confirmed, so a stray Return never writes.
void sync() {
  refresh();
  const int next = screenNow();
  build(next);
  if (next == screen)
    return;
  screen = next;
  workRows = false;
  focus(next == S_CONFIRM || next == S_LOOKUP || next == S_DONE ? lineCount - 1
        : next == S_BROWSE && view.careers                      ? kRestoreRow
                                                                 : 0);
}
void open() {
  if (reshow)
    return;
  allowed = permit();
  current = 0;
  picking = false;
  workRows = false;
  earlier = 0;
  memset(&view, 0, sizeof(view));
  memset(&flow, 0, sizeof(flow));
  if (allowed) {
    char folder[4096]{};
    CE_SettingGetText("career_backup_folder", folder, sizeof(folder));
    CePreviewOpen(PortUserDir(), PortGameDir(), folder);
    CeRestoreUiOpen();
  }
  refresh();
  screen = screenNow(); // an unread outcome is shown first; else the preview
  build(screen);
}
void close() {
  CeRestoreUiLeave(); // a lookup or an unconfirmed choice ends here, unwritten
  CePreviewLeave();
  memset(&view, 0, sizeof(view));
  memset(&flow, 0, sizeof(flow));
  current = 0;
  picking = false;
  workRows = false;
  screen = S_BROWSE;
  lineCount = 0;
}
void update() {
  if (allowed && !permit()) {
    close();
    allowed = false;
  }
  // In Working this makes the storage call, and only once a Present has
  // finished since draw() saw the Working rows drawn.
  if (allowed)
    CeRestoreUiPump(PortPresentCount(), &kAuthority);
  sync();
}
// Called by the list box after it drew this frame's rows and hint.
void draw(float) {
  if (allowed && screen == S_WORK && workRows)
    CeRestoreUiDrawn(PortPresentCount());
  workRows = false;
}
void lineRow(int index, SCeListRow *out) {
  if (index < 0 || index >= lineCount)
    return;
  const Line &l = lines[index];
  if (l.name) {
    widenUtf8(out->mLeft, 80, l.left);
    PortCeExtrasInFont(out->mLeft);
  } else
    plain(out->mLeft, 80, l.left);
  plain(out->mRight, 24, l.right);
  out->mTone = l.tone;
  out->mDim = l.dim;
  if (screen == S_WORK)
    workRows = true;
}
void lineHint(int index, WCHAR *help, WCHAR *extra, int n) {
  if (index < 0 || index >= lineCount)
    return;
  const Line &l = lines[index];
  if (l.hintName) {
    widenUtf8(help, n, l.help);
    widenUtf8(extra, n, l.extra);
    PortCeExtrasInFont(help);
    PortCeExtrasInFont(extra);
  } else {
    plain(help, n, l.help);
    plain(extra, n, l.extra);
  }
}
bool lineSelect(int index) {
  if (index < 0 || index >= lineCount)
    return false;
  const int act = lines[index].act, arg = lines[index].arg;
  bool did = false;
  switch (act) {
  case A_CANCEL:
    if (screen == S_PICK) {
      picking = false;
      did = true;
    } else
      did = CeRestoreUiCancel();
    break;
  case A_PICK:
    if (screen == S_PICK && arg >= 0 && arg < view.careers && permit()) {
      current = arg;
      picking = false;
      CeRestoreUiBegin(unsigned(current), &kAuthority);
      did = true;
    }
    break;
  case A_CONFIRM:
    if (screen == S_CONFIRM && permit()) {
      CeRestoreUiConfirm(&kAuthority);
      did = true;
    }
    break;
  case A_DONE:
    did = CeRestoreUiAcknowledge();
    break;
  }
  sync();
  return did;
}
void row(int index, SCeListRow *out) {
  if (allowed && screen != S_BROWSE) {
    lineRow(index, out);
    return;
  }
  const bool has = view.careers > 0;
  int set = 0;
  const char *label = "";
  char detail[80]{};
  switch (kind(index, &set)) {
  case K_SET: {
    char leaf[201]{}, shown[201]{};
    if (CePreviewDisplaySetName(unsigned(set), leaf, sizeof(leaf)))
      setLabel(shown, sizeof(shown), leaf);
    widenUtf8(out->mLeft, 80, shown);
    PortCeExtrasInFont(out->mLeft);
    char word[24], line[96];
    const char *extra = "";
    holds(unsigned(set), word, sizeof(word), line, sizeof(line), &extra);
    plain(out->mRight, 24, word);
    if (!strcmp(word, "Not verified"))
      out->mTone = CE_TONE_BAD;
    return;
  }
  case K_BACK:
    label = "Back";
    break;
  case K_CHOOSE:
    label = "Choose Backup Set";
    break;
  case K_AGAIN:
    label = "Verify Again";
    out->mDim = !has;
    break;
  case K_STATUS:
    label = !allowed                                 ? "Unavailable"
            : view.phase == CE_PREVIEW_VERIFIED ? "Verified"
            : view.phase == CE_PREVIEW_ERROR    ? "Not verified"
            : view.phase == CE_PREVIEW_WORKING  ? "Checking backup"
            : view.phase == CE_PREVIEW_PICKING  ? "Choosing folder"
                                                     : "No backup selected";
    break;
  case K_DATE:
    label = "Backup date";
    PortCeBackupWhen(detail, sizeof(detail), view.date, true);
    break;
  case K_FILES:
    label = "Files verified";
    snprintf(detail, sizeof(detail), "%d", view.members);
    break;
  case K_CAREER:
    widenUtf8(out->mLeft, 80, view.name);
    PortCeExtrasInFont(out->mLeft);
    if (view.careers > 1)
      snprintf(detail, sizeof(detail), "%d of %d", current + 1,
               int(view.careers));
    plain(out->mRight, 24, detail);
    return;
  case K_MISSIONS:
    label = "Missions completed";
    snprintf(detail, sizeof(detail), "%d", view.complete);
    break;
  case K_GOODIES:
    label = "Goodies found";
    snprintf(detail, sizeof(detail), "%d", view.goodies);
    break;
  case K_RESTORE:
    label = "Restore as New";
    if (has && flow.offer != CE_RESTORE_OFFER_READY) {
      out->mDim = true;
      out->mTone = CE_TONE_DIM;
      snprintf(detail, sizeof(detail), "%s",
               flow.offer == CE_RESTORE_OFFER_WINDOWS ? "Not on Windows"
                                                      : "Unavailable");
    }
    break;
  case K_EARLIER:
    label = earlier == 1   ? "A career was restored as new"
            : earlier == 2 ? "A restore needs checking"
                           : "No career restored";
    break;
  case K_NONE:
    break;
  }
  plain(out->mLeft, 80, label);
  plain(out->mRight, 24, detail);
}
void hint(int i, WCHAR *help, WCHAR *extra, int n) {
  if (!allowed) {
    plain(help, n, "Backup preview is unavailable in this session.");
    return;
  }
  if (screen != S_BROWSE) {
    lineHint(i, help, extra, n);
    return;
  }
  int set = 0;
  switch (kind(i, &set)) {
  case K_SET: {
    char word[24], line[96];
    const char *more = "";
    holds(unsigned(set), word, sizeof(word), line, sizeof(line), &more);
    plain(help, n, line);
    plain(extra, n, *more ? more : "Selecting a set restores nothing.");
    return;
  }
  case K_BACK:
    plain(help, n, earlier ? "Return to Extras."
                           : "Return to Extras. No career was restored.");
    return;
  case K_CHOOSE:
    plain(help, n, "Choose the folder of one backup set.");
    plain(extra, n, "Pick a set, not the folder holding all of them.");
    return;
  case K_AGAIN:
    plain(help, n, "Check the set and every file again.");
    plain(extra, n, "A changed or missing file clears the preview.");
    return;
  case K_STATUS:
    plain(help, n, view.message);
    plain(extra, n, earlier ? "Verifying a backup writes nothing."
                            : "No career or settings have been written.");
    return;
  case K_DATE: {
    char when[64];
    readableTime(when, sizeof(when), view.date);
    char line[96];
    snprintf(line, sizeof(line), "Made on %s.", when);
    plain(help, n, line);
    plain(extra, n, madeBy(view.reason));
    return;
  }
  case K_FILES:
    plain(help, n, "Every file matched its recorded size and checksum.");
    return;
  case K_CAREER:
    plain(help, n, "The progress below comes from this backed-up career.");
    if (view.careers > 1)
      plain(extra, n, "Select to see the next career in this set.");
    return;
  case K_MISSIONS:
  case K_GOODIES:
    plain(help, n, "Read from the verified backup, not from your saves.");
    return;
  case K_RESTORE:
    // Why it is or is not offered: ready, not on Windows, or what to do first.
    plain(help, n, flow.message);
    plain(extra, n,
          flow.offer == CE_RESTORE_OFFER_READY && view.careers > 1
              ? "You choose which career next. Nothing is written yet."
              : flow.detail);
    return;
  case K_EARLIER:
    if (earlier == 1) {
      plain(help, n, "A new career was added during this visit.");
      plain(extra, n, "Find it in Load Game. Nothing else was changed.");
    } else if (earlier == 2) {
      plain(help, n, "An earlier restore was not confirmed.");
      plain(extra, n, "Check Load Game before trying again.");
    } else {
      plain(help, n, "Preview only. No career was restored or replaced.");
      plain(extra, n, "Your settings and the backup stay untouched.");
    }
    return;
  case K_NONE:
    return;
  }
}
bool select(int i) {
  // A step of Restore as New has its own rows, ending with the way out.
  if (allowed && screen != S_BROWSE)
    return lineSelect(i);
  int set = 0;
  const Kind k = kind(i, &set);
  if (k == K_BACK) {
    PortCeBackupPreviewBack();
    return true;
  }
  if (!allowed || !permit())
    return false;
  switch (k) {
  case K_SET:
    CePreviewSelectSet(unsigned(set));
    sync();
    return true;
  case K_CHOOSE:
    CePreviewChoose();
    sync();
    return true;
  case K_AGAIN:
    if (!view.careers)
      return false;
    CePreviewAgain();
    sync();
    return true;
  case K_CAREER:
    if (view.careers < 2)
      return false;
    current = (current + 1) % int(view.careers);
    return true;
  case K_RESTORE:
    if (!view.careers)
      return false;
    // Looking at a career never starts this; only this row does. A set with
    // several careers asks which one, and never assumes the one on show.
    sync();
    if (screen != S_BROWSE)
      return true;
    if (flow.offer != CE_RESTORE_OFFER_READY || !view.careers)
      return false;
    if (view.careers > 1)
      picking = true;
    else
      CeRestoreUiBegin(unsigned(current), &kAuthority);
    sync();
    return true;
  default:
    return false;
  }
}
const WCHAR *empty() { return L"Choose a backup set to inspect."; }
const SCeListPage page = {
    L"Backup Preview", count, row, hint, select, empty, open, close, update,
    draw,              0.78f, 0};
} // namespace
const SCeListPage *PortCeBackupPreviewPage() { return &page; }
int PortCeBackupPreviewStepBack() {
  sync();
  if (!allowed || screen == S_BROWSE)
    return 0;
  switch (screen) {
  case S_PICK:
    picking = false;
    sync();
    return 1;
  case S_LOOKUP:
  case S_CONFIRM:
    CeRestoreUiCancel();
    sync();
    return 1;
  case S_WORK:
    return -1;
  case S_DONE:
    CeRestoreUiAcknowledge();
    sync();
    return 1;
  default:
    return 0;
  }
}
// ---- The test kit's state keys for Restore as New (ce_testkit.h): what the page last read from the controller -------------
// Plain reads for the test kit (ce_extras.h). They change nothing.
bool PortCeRestoreShown() { return allowed; }
int PortCeRestorePhase() { return flow.phase; }
int PortCeRestoreOutcome() { return flow.outcome; }
const char *PortCeRestoreTarget() { return flow.target; }
bool PortCePreviewVerified() { return view.phase == CE_PREVIEW_VERIFIED; }
#endif
