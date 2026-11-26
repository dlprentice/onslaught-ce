// SPDX-License-Identifier: GPL-3.0-only
// Host driver for the Restore as New controller. It calls the same two plain
// boundaries the game page calls (ce_backup_preview_ui.h and
// ce_career_restore_ui.h) and nothing behind them, on a disposable profile.
//
// Test seams, each a stand-in and none of them the game:
// - <SDL3/SDL.h> is a small shim the test script writes; the three SDL calls
//   the preview controller makes are defined below (the folder dialog always
//   answers "cancelled").
// - The Present count is a number this driver advances. It stands for the
//   platform's count of finished Presents; nothing is rendered here.
// - Permission and name compatibility are outcomes each scenario chooses.
//   They are not the game's gates and not the game's filename semantics.
// - The linker wraps flock (one call per storage operation that reaches its
//   lock), linkat and fsync (to fail the flush after publication).
// - The linker also wraps the preview's CePreviewRestoreCard, so a scenario can
//   change only the live card's stamp (the real preview always changes its
//   generation with it); this is how the controller's own stamp check is reached.
// The card type, for the stamp seam only; the scenarios use the plain boundaries.
#define CE_CAREER_RESTORE_UI_INTERNAL 1
#include "../../platform/common/ce_backup_preview_ui.h"
#include "../../platform/common/ce_career_restore_ui.h"
#include <SDL3/SDL.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

static std::string gBase;
static const char *gScenario = "";
static int gFails = 0;
static int gLocks = 0, gTargetDir = -1;
static bool gLinked = false, gFailFlush = false;
static int gPermitCalls = 0, gNameCalls = 0;
static int gPermitLimit = -1, gNameLimit = -1; // true for the first N calls
static bool gPermit = true, gCompatible = true;
static std::string gSeenSource, gSeenTarget;
static unsigned gPresent = 100;
static bool gTwistStamp = false; // the live card reports a different stamp
static CePreviewDisplay gView;
static CeRestoreDisplay gFlow;
static const char *const kSet = "onslaught-backup-20260405-123000";

#define EXPECT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s line %d: %s\n", gScenario, __LINE__, #cond);        \
      ++gFails;                                                                \
    }                                                                          \
  } while (0)

const char *SDL_GetBasePath(void) {
  static std::string path;
  path = gBase + "/package/";
  return path.c_str();
}
SDL_Window *SDL_GetKeyboardFocus(void) { return nullptr; }
void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback callback, void *userdata,
                              SDL_Window *, const char *, bool) {
  const char *const none[] = {nullptr};
  callback(userdata, none, 0);
}

extern "C" {
int __real_flock(int, int);
int __real_linkat(int, const char *, int, const char *, int);
int __real_fsync(int);
int __wrap_flock(int fd, int operation) {
  ++gLocks;
  return __real_flock(fd, operation);
}
int __wrap_linkat(int olddir, const char *oldname, int newdir,
                  const char *newname, int flags) {
  const int rc = __real_linkat(olddir, oldname, newdir, newname, flags);
  if (rc == 0) {
    gLinked = true;
    gTargetDir = newdir;
  }
  return rc;
}
void __real__Z20CePreviewRestoreCardR13CeRestoreCard(CeRestoreCard &out);
void __wrap__Z20CePreviewRestoreCardR13CeRestoreCard(CeRestoreCard &out) {
  __real__Z20CePreviewRestoreCardR13CeRestoreCard(out);
  if (gTwistStamp && !out.stamp.empty())
    out.stamp += "-other";
}
int __wrap_fsync(int fd) {
  if (gFailFlush && gLinked && fd == gTargetDir) {
    errno = EIO;
    return -1;
  }
  return __real_fsync(fd);
}
}

static bool permitted() {
  ++gPermitCalls;
  return gPermit && (gPermitLimit < 0 || gPermitCalls <= gPermitLimit);
}
static bool compatible(const char *source, const char *target) {
  ++gNameCalls;
  gSeenSource = source ? source : "";
  gSeenTarget = target ? target : "";
  return gCompatible && (gNameLimit < 0 || gNameCalls <= gNameLimit);
}
static const CeRestoreAuthority kAuthority = {permitted, compatible};

static std::string readBytes(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
}
static bool writeBytes(const std::string &path, const std::string &bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(bytes.data(), std::streamsize(bytes.size()));
  f.close();
  return bool(f);
}
static void nap() {
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
}
static void look(unsigned career = 0) {
  CePreviewDisplayRead(career, &gView);
  CeRestoreUiRead(career, &gFlow);
}
static bool waitPreview(CePreviewPhase wanted) {
  for (int i = 0; i < 4000; ++i) {
    CePreviewDisplayRead(0, &gView);
    if (gView.phase != CE_PREVIEW_WORKING)
      break;
    nap();
  }
  return gView.phase == wanted;
}
// As the page opens: both controllers, then the set chosen from the list.
static bool openCard() {
  const std::string user = gBase + "/user", game = gBase + "/game",
                    root = gBase + "/external";
  CePreviewOpen(user.c_str(), game.c_str(), root.c_str());
  CeRestoreUiOpen();
  if (!waitPreview(CE_PREVIEW_IDLE))
    return false;
  for (int i = 0; i < gView.choices; ++i) {
    char name[256] = {};
    if (CePreviewDisplaySetName(unsigned(i), name, sizeof(name)) &&
        !std::strcmp(name, kSet)) {
      CePreviewSelectSet(unsigned(i));
      return waitPreview(CE_PREVIEW_VERIFIED);
    }
  }
  return false;
}
static void leave() {
  CeRestoreUiLeave();
  CePreviewLeave();
}
// One game frame in the page's order: update, draw, Present.
static void frame() {
  CeRestoreUiPump(gPresent, &kAuthority);
  CeRestoreUiDrawn(gPresent);
  ++gPresent;
}
static void frames(int count) {
  for (int i = 0; i < count; ++i)
    frame();
}
// Updates only: no draw and no Present.
static CeRestorePhase settle(CeRestorePhase from, unsigned career = 0) {
  for (int i = 0; i < 4000; ++i) {
    CeRestoreUiPump(gPresent, &kAuthority);
    CeRestoreUiRead(career, &gFlow);
    if (gFlow.phase != from)
      break;
    nap();
  }
  return gFlow.phase;
}
static bool toConfirm(unsigned career = 0) {
  if (!openCard() || !CeRestoreUiBegin(career, &kAuthority))
    return false;
  return settle(CE_RESTORE_PROPOSING, career) == CE_RESTORE_CONFIRM;
}
static void say(const char *key, const char *value) {
  std::printf("%s=%s\n", key, value);
}

// The page was never opened: every entry point refuses and asks nothing.
static void ordinary() {
  EXPECT(!CeRestoreUiBegin(0, &kAuthority));
  EXPECT(!CeRestoreUiConfirm(&kAuthority));
  EXPECT(!CeRestoreUiCancel());
  EXPECT(!CeRestoreUiAcknowledge());
  frames(5);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_IDLE);
  EXPECT(gFlow.offer == CE_RESTORE_OFFER_NONE);
  EXPECT(gPermitCalls == 0 && gNameCalls == 0 && gLocks == 0);
}
// Looking, verifying and choosing write nothing and start nothing.
static void browse() {
  EXPECT(openCard());
  look();
  EXPECT(gFlow.phase == CE_RESTORE_IDLE);
  EXPECT(gFlow.offer == CE_RESTORE_OFFER_READY);
  EXPECT(!std::strcmp(gFlow.source, "Amber.bes"));
  EXPECT(!std::strcmp(gFlow.set, kSet));
  EXPECT(!gFlow.target[0]);
  frames(5);
  CePreviewAgain();
  EXPECT(waitPreview(CE_PREVIEW_VERIFIED));
  frames(5);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_IDLE);
  EXPECT(gFlow.offer == CE_RESTORE_OFFER_READY);
  leave();
  look();
  EXPECT(gFlow.offer == CE_RESTORE_OFFER_NONE);
  EXPECT(gNameCalls == 0 && gLocks == 0);
}
// A proposal is shown, then Cancel, then Back: nothing is written either way.
static void cancel() {
  EXPECT(toConfirm());
  EXPECT(!std::strcmp(gFlow.source, "Amber.bes"));
  EXPECT(!std::strcmp(gFlow.target, "Amber restored.bes"));
  EXPECT(gSeenSource == "Amber.bes" && gSeenTarget == "Amber restored.bes");
  EXPECT(gNameCalls == 1);
  say("target", gFlow.target);
  frames(8); // frames and Presents alone never confirm
  look();
  EXPECT(gFlow.phase == CE_RESTORE_CONFIRM);
  EXPECT(gLocks == 0);
  EXPECT(CeRestoreUiCancel());
  look();
  EXPECT(gFlow.phase == CE_RESTORE_IDLE);
  EXPECT(gFlow.offer == CE_RESTORE_OFFER_READY);
  EXPECT(!gFlow.target[0]);
  EXPECT(!std::strcmp(gFlow.source, "Amber.bes")); // the card is as it was
  frames(8);
  EXPECT(gLocks == 0);
  EXPECT(CeRestoreUiBegin(0, &kAuthority));
  EXPECT(settle(CE_RESTORE_PROPOSING) == CE_RESTORE_CONFIRM);
  EXPECT(!std::strcmp(gFlow.target, "Amber restored.bes")); // nothing reserved
  leave(); // Back before confirmation
  frames(8);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_IDLE);
  EXPECT(gLocks == 0);
}
// Working needs its rows drawn and then a Present; then exactly one call.
static void restore() {
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  look();
  EXPECT(gFlow.phase == CE_RESTORE_WORKING);
  for (int i = 0; i < 5; ++i)
    CeRestoreUiPump(gPresent, &kAuthority); // updates only
  gPresent += 3; // Presents with no Working rows drawn
  for (int i = 0; i < 5; ++i)
    CeRestoreUiPump(gPresent, &kAuthority);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_WORKING && gLocks == 0);
  CeRestoreUiDrawn(gPresent); // drawn, but no Present has finished since
  for (int i = 0; i < 5; ++i)
    CeRestoreUiPump(gPresent, &kAuthority);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_WORKING && gLocks == 0);
  ++gPresent; // the Present that showed Working
  CeRestoreUiPump(gPresent, &kAuthority);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_COMPLETE);
  EXPECT(gFlow.possible == 1);
  EXPECT(!std::strcmp(gFlow.target, "Amber restored.bes"));
  EXPECT(gFlow.backup[0] && gFlow.saves[0]);
  EXPECT(std::strstr(gFlow.detail, "Load Game") != nullptr);
  EXPECT(gLocks == 1);
  say("target", gFlow.target);
  say("backup", gFlow.backup);
  say("saves", gFlow.saves);
  frames(10);
  EXPECT(gLocks == 1); // once
  EXPECT(!CeRestoreUiBegin(0, &kAuthority)); // not before it is read
  EXPECT(CeRestoreUiAcknowledge());
  // A second copy is a new, explicit flow with its own shown name.
  EXPECT(CeRestoreUiBegin(0, &kAuthority));
  EXPECT(settle(CE_RESTORE_PROPOSING) == CE_RESTORE_CONFIRM);
  EXPECT(!std::strcmp(gFlow.target, "Amber restored 2.bes"));
  EXPECT(CeRestoreUiCancel());
  EXPECT(gLocks == 1);
}
// A set with two careers: the one asked for is the one bound and copied.
static void multi() {
  EXPECT(openCard());
  look(1);
  EXPECT(gFlow.careers == 2);
  EXPECT(!std::strcmp(gFlow.source, "Second.bes"));
  EXPECT(!CeRestoreUiBegin(7, &kAuthority));
  look(1);
  EXPECT(gFlow.phase == CE_RESTORE_IDLE);
  EXPECT(CeRestoreUiBegin(1, &kAuthority));
  EXPECT(settle(CE_RESTORE_PROPOSING, 1) == CE_RESTORE_CONFIRM);
  CeRestoreUiRead(0, &gFlow); // the bound career, whichever one is on show
  EXPECT(gFlow.career == 1);
  EXPECT(!std::strcmp(gFlow.source, "Second.bes"));
  EXPECT(!std::strcmp(gFlow.target, "Second restored.bes"));
  EXPECT(gSeenSource == "Second.bes");
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  frames(3);
  look();
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_COMPLETE);
  EXPECT(!std::strcmp(gFlow.target, "Second restored.bes"));
  EXPECT(gLocks == 1);
  say("target", gFlow.target);
}
// The confirmed name is taken before the write: no other name is chosen.
static void duplicate() {
  EXPECT(toConfirm());
  const std::string saves = gBase + "/user/savegames/";
  EXPECT(writeBytes(saves + gFlow.target, readBytes(saves + "Current.bes")));
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  frames(3);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_OCCUPIED);
  EXPECT(gFlow.possible == 0);
  EXPECT(!std::strcmp(gFlow.target, "Amber restored.bes"));
  EXPECT(!gFlow.backup[0]);
  EXPECT(gLocks == 1);
  frames(10);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE && gLocks == 1); // no retry
  EXPECT(CeRestoreUiAcknowledge());
  EXPECT(CeRestoreUiBegin(0, &kAuthority));
  EXPECT(settle(CE_RESTORE_PROPOSING) == CE_RESTORE_CONFIRM);
  say("second", gFlow.target); // reviewed by the player before any write
  EXPECT(CeRestoreUiCancel());
  EXPECT(gLocks == 1);
}
// The backup's file is rewritten after the card was verified.
static void changed() {
  EXPECT(toConfirm());
  const std::string member = gBase + "/external/" + kSet + "/Amber.bes";
  EXPECT(writeBytes(member, readBytes(member))); // same bytes, a new file state
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  frames(3);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_CHANGED);
  EXPECT(gFlow.possible == 0 && !gFlow.backup[0]);
  frames(10);
  EXPECT(gLocks <= 1);
}
// The card is verified again after the proposal: the old choice is void.
static void reverified() {
  EXPECT(toConfirm());
  CePreviewAgain();
  EXPECT(waitPreview(CE_PREVIEW_VERIFIED));
  EXPECT(!CeRestoreUiConfirm(&kAuthority));
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_CHANGED);
  frames(10);
  EXPECT(gLocks == 0);
}
// Only the card's stamp differs at Confirm: refused, nothing called.
static void stampAtConfirm() {
  EXPECT(toConfirm());
  gTwistStamp = true;
  EXPECT(!CeRestoreUiConfirm(&kAuthority));
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_CHANGED);
  EXPECT(gFlow.possible == 0 && !gFlow.backup[0]);
  frames(10);
  EXPECT(gLocks == 0);
}
// Confirmed, then only the stamp differs before the storage call.
static void stampAtPump() {
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  gTwistStamp = true;
  frames(5);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_CHANGED);
  EXPECT(gFlow.possible == 0 && !gFlow.backup[0]);
  frames(10);
  EXPECT(gLocks == 0);
}
// Confirmed, then the card is verified again before the storage call.
static void reverifyAfterConfirm() {
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  CePreviewAgain();
  EXPECT(waitPreview(CE_PREVIEW_VERIFIED));
  frames(5);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_CHANGED);
  frames(10);
  EXPECT(gLocks == 0);
}
// Confirmed, then the set is picked again from the list before the call.
static void repickAfterConfirm() {
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  bool picked = false;
  for (int i = 0; i < gView.choices; ++i) {
    char name[256] = {};
    if (CePreviewDisplaySetName(unsigned(i), name, sizeof(name)) &&
        !std::strcmp(name, kSet)) {
      CePreviewSelectSet(unsigned(i));
      picked = waitPreview(CE_PREVIEW_VERIFIED);
    }
  }
  EXPECT(picked);
  frames(5);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_CHANGED);
  frames(10);
  EXPECT(gLocks == 0);
}
// The saves folder refuses new files after the backup step: the page says the
// backup set was made and blames the folder, not the disk.
static void readonlySaves() {
  EXPECT(toConfirm());
  const std::string saves = gBase + "/user/savegames";
  EXPECT(chmod(saves.c_str(), 0555) == 0);
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  frames(5);
  look();
  chmod(saves.c_str(), 0755);
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_UNSUPPORTED);
  EXPECT(gFlow.possible == 0 && gFlow.backup[0]);
  EXPECT(std::strstr(gFlow.message, "saves folder could not be written"));
  EXPECT(std::strstr(gFlow.detail, "backed up first"));
  EXPECT(!std::strstr(gFlow.detail, "Nothing was written"));
  EXPECT(!std::strstr(gFlow.detail, "local disk"));
  say("backup", gFlow.backup);
  say("message", gFlow.message);
  frames(10);
  EXPECT(gLocks == 1);
}
static void nameRefused() {
  gCompatible = false;
  EXPECT(openCard());
  EXPECT(CeRestoreUiBegin(0, &kAuthority));
  EXPECT(settle(CE_RESTORE_PROPOSING) == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_NAME);
  EXPECT(!gFlow.target[0]); // a refused name is never shown as a proposal
  EXPECT(gSeenSource == "Amber.bes" && gSeenTarget == "Amber restored.bes");
  EXPECT(!CeRestoreUiConfirm(&kAuthority));
  frames(10);
  EXPECT(gLocks == 0);
}
// Compatible when proposed, not when asked again just before the write.
static void nameRevoked() {
  gNameLimit = 1;
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  frames(3);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_NAME);
  EXPECT(gFlow.possible == 0);
  EXPECT(gNameCalls == 2 && gLocks == 0);
}
static void authority() {
  gPermit = false;
  EXPECT(openCard());
  EXPECT(!CeRestoreUiBegin(0, &kAuthority));
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_AUTHORITY);
  frames(10);
  EXPECT(gLocks == 0 && gNameCalls == 0);
}
static void authorityLate() {
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  gPermit = false; // lost between confirmation and the write
  frames(3);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_AUTHORITY);
  EXPECT(gFlow.possible == 0 && gLocks == 0);
}
// Permission holds for the controller's own check and is gone when the
// storage call asks: the call itself refuses before taking its lock.
static void authorityInside() {
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  CeRestoreUiDrawn(gPresent);
  ++gPresent;
  gPermitLimit = gPermitCalls + 1;
  CeRestoreUiPump(gPresent, &kAuthority);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_REFUSED);
  EXPECT(gFlow.possible == 0 && !gFlow.backup[0]);
  EXPECT(gPermitCalls >= gPermitLimit + 1); // storage asked for itself
  EXPECT(gLocks == 0);
}
// Another thread cannot draw, pump, cancel or confirm for the game thread.
static void wrongThread() {
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  bool cancelled = true, begun = true;
  std::thread other([&] {
    CeRestoreUiDrawn(gPresent);
    ++gPresent;
    for (int i = 0; i < 5; ++i)
      CeRestoreUiPump(gPresent, &kAuthority);
    cancelled = CeRestoreUiCancel();
    begun = CeRestoreUiBegin(0, &kAuthority);
  });
  other.join();
  look();
  EXPECT(!cancelled && !begun);
  EXPECT(gFlow.phase == CE_RESTORE_WORKING);
  EXPECT(gLocks == 0);
  EXPECT(CeRestoreUiCancel()); // the owner still can, before the write
  frames(5);
  EXPECT(gLocks == 0);
}
// A lookup started on one page must never surface on the next, nor after a
// cancel on the same page.
static void staleWorker() {
  EXPECT(openCard());
  EXPECT(CeRestoreUiBegin(0, &kAuthority));
  leave();
  EXPECT(openCard());
  for (int i = 0; i < 60; ++i) {
    frame();
    look();
    EXPECT(gFlow.phase == CE_RESTORE_IDLE);
    nap();
  }
  EXPECT(!gFlow.target[0]);
  EXPECT(!CeRestoreUiConfirm(&kAuthority));
  EXPECT(gNameCalls == 0);
  EXPECT(CeRestoreUiBegin(0, &kAuthority));
  EXPECT(CeRestoreUiCancel());
  for (int i = 0; i < 60; ++i) {
    frame();
    look();
    EXPECT(gFlow.phase == CE_RESTORE_IDLE);
    nap();
  }
  EXPECT(gNameCalls == 0 && gLocks == 0);
}
// The flush after publication fails: the file may exist. It is reported,
// kept, never retried, and still shown after the page is left and reopened.
static void uncertain() {
  gFailFlush = true;
  EXPECT(toConfirm());
  EXPECT(CeRestoreUiConfirm(&kAuthority));
  frames(3);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_UNCERTAIN);
  EXPECT(gFlow.possible == 1);
  EXPECT(!std::strcmp(gFlow.target, "Amber restored.bes"));
  EXPECT(gFlow.backup[0] && gFlow.saves[0]);
  EXPECT(std::strstr(gFlow.message, "may") != nullptr);
  EXPECT(std::strstr(gFlow.detail, "before any retry") != nullptr);
  EXPECT(gLocks == 1);
  say("target", gFlow.target);
  say("backup", gFlow.backup);
  frames(10);
  EXPECT(gLocks == 1);
  EXPECT(!CeRestoreUiBegin(0, &kAuthority));
  leave();
  frames(5);
  EXPECT(openCard());
  look();
  EXPECT(gFlow.phase == CE_RESTORE_DONE);
  EXPECT(gFlow.outcome == CE_RESTORE_OUT_UNCERTAIN);
  EXPECT(!std::strcmp(gFlow.target, "Amber restored.bes"));
  EXPECT(!CeRestoreUiBegin(0, &kAuthority));
  frames(10);
  EXPECT(gLocks == 1);
  EXPECT(CeRestoreUiAcknowledge());
  look();
  EXPECT(gFlow.phase == CE_RESTORE_IDLE);
}
// Built with the Windows host stand-in storage source (returns empty stamp / Unsupported on non-Windows host).
static void unsupported() {
  EXPECT(openCard()); // the read-only preview still works
  look();
  EXPECT(gView.careers == 1);
  EXPECT(gFlow.offer == CE_RESTORE_OFFER_UNBOUND);
  EXPECT(std::strstr(gFlow.message, "could not be held safely") != nullptr);
  EXPECT(!CeRestoreUiBegin(0, &kAuthority));
  frames(5);
  look();
  EXPECT(gFlow.phase == CE_RESTORE_IDLE);
  EXPECT(gPermitCalls == 0 && gNameCalls == 0 && gLocks == 0);
}

int main(int argc, char **argv) {
  if (argc != 3)
    return 80;
  gBase = argv[1];
  gScenario = argv[2];
  if (readBytes(gBase + "/AUTHORED") != "disposable restore ui fixture\n")
    return 81;
  const struct {
    const char *name;
    void (*run)();
  } table[] = {{"ordinary", ordinary},
               {"browse", browse},
               {"cancel", cancel},
               {"restore", restore},
               {"multi", multi},
               {"duplicate", duplicate},
               {"changed", changed},
               {"reverified", reverified},
               {"stamp_at_confirm", stampAtConfirm},
               {"stamp_at_pump", stampAtPump},
               {"reverify_after_confirm", reverifyAfterConfirm},
               {"repick_after_confirm", repickAfterConfirm},
               {"readonly_saves", readonlySaves},
               {"name_refused", nameRefused},
               {"name_revoked", nameRevoked},
               {"authority", authority},
               {"authority_late", authorityLate},
               {"authority_inside", authorityInside},
               {"wrong_thread", wrongThread},
               {"stale_worker", staleWorker},
               {"uncertain", uncertain},
               {"unsupported", unsupported}};
  bool found = false;
  for (const auto &entry : table)
    if (!std::strcmp(entry.name, gScenario)) {
      found = true;
      entry.run();
    }
  if (!found)
    return 82;
  std::printf("locks=%d\npermit_calls=%d\nname_calls=%d\n", gLocks,
              gPermitCalls, gNameCalls);
  std::printf("RESULT %s\n", gFails ? "fail" : "pass");
  std::fflush(stdout);
  // A read-only worker may still be finishing; leave without static teardown.
  _exit(gFails ? 1 : 0);
}
