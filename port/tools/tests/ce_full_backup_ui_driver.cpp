// SPDX-License-Identifier: GPL-3.0-only
// Host driver for the Back Up All Careers controller. It calls the plain
// boundary the game page calls (ce_full_backup_ui.h) and nothing behind it, on
// a disposable profile, linked with the real Linux storage and preview reader.
//
// Test seams, each a stand-in and none of them the game:
// - <SDL3/SDL.h> is a shim the test script writes; SDL_GetBasePath is defined
//   below and names the profile's package folder.
// - The Present count is a number this driver advances; nothing is rendered.
// - Permission is an outcome each scenario chooses, not the game's gate.
#include "../../platform/common/ce_full_backup_ui.h"
#include "../../platform/common/ce_backup_preview.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
#include <vector>

static std::string gBase;
static bool gPermit = true;
static int gPermitCalls = 0, gPermitLimit = -1; // true for the first N calls
static int gFails = 0;
static const char *gScenario = "";
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
static bool permit() {
  ++gPermitCalls;
  if (gPermitLimit >= 0)
    return gPermitCalls <= gPermitLimit;
  return gPermit;
}
static CeFullBackupDisplay view() {
  CeFullBackupDisplay d;
  CeFullBackupUiRead(&d);
  return d;
}
static std::vector<std::string> listed() {
  std::vector<std::string> sets;
  std::string error;
  CePreviewList(gBase + "/external", gBase + "/user", gBase + "/game",
                gBase + "/package", sets, error);
  return sets;
}
static void open() {
  const std::string user = gBase + "/user", game = gBase + "/game";
  CeFullBackupUiOpen(user.c_str(), game.c_str());
}
static CeFullBackupRequest request(int careers = 2, bool other = false,
                                   const char *root = nullptr) {
  static std::string folder;
  folder = gBase + "/external";
  return CeFullBackupRequest{root ? root : folder.c_str(), careers, other,
                             permit};
}
// Accepted, drawn and pumped once after a Present: the one storage call.
static void runOnce(unsigned &present) {
  CeFullBackupUiDrawn(present);
  CeFullBackupUiPump(present, permit); // same Present: no call yet
  EXPECT(view().phase == CE_FULL_BACKUP_WORKING && listed().empty());
  ++present;
  CeFullBackupUiPump(present, permit);
}

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  gBase = argv[1];
  const std::string scenario = argv[2];
  gScenario = argv[2];
  unsigned present = 100;
  open();
  if (scenario == "offer") {
    const auto d = view();
    EXPECT(d.phase == CE_FULL_BACKUP_IDLE &&
           d.outcome == CE_FULL_BACKUP_OUT_NONE &&
           !std::strcmp(d.message, "Back up every career and your options."));
  } else if (scenario == "complete") {
    const auto r = request();
    EXPECT(CeFullBackupUiStart(&r));
    EXPECT(view().phase == CE_FULL_BACKUP_WORKING);
    CeFullBackupUiPump(present + 5, permit); // never drawn: no call
    EXPECT(view().phase == CE_FULL_BACKUP_WORKING && listed().empty());
    const int before = gPermitCalls;
    runOnce(present);
    const auto d = view();
    EXPECT(d.phase == CE_FULL_BACKUP_DONE &&
           d.outcome == CE_FULL_BACKUP_OUT_COMPLETE);
    // The gate was asked on this thread before and inside the storage call.
    EXPECT(gPermitCalls - before >= 3);
    const auto sets = listed();
    EXPECT(sets.size() == 1 && sets[0] == d.set);
    CePreviewSet set;
    std::string error;
    EXPECT(sets.size() == 1 &&
           CePreviewRead(gBase + "/external/" + sets[0], gBase + "/user",
                         gBase + "/game", gBase + "/package", set, error) &&
           set.reason == "All careers" && set.careers.size() == 2 &&
           set.members == 3);
    EXPECT(!std::strcmp(d.message,
                        "Backup verified. Your careers are unchanged."));
    CeFullBackupUiPump(present + 10, permit); // done: never called again
    EXPECT(listed().size() == 1);
    EXPECT(CeFullBackupUiAcknowledge() && view().phase == CE_FULL_BACKUP_IDLE);
  } else if (scenario == "authority_at_start") {
    gPermit = false;
    const auto r = request();
    EXPECT(!CeFullBackupUiStart(&r));
    EXPECT(view().outcome == CE_FULL_BACKUP_OUT_AUTHORITY);
  } else if (scenario == "authority_before_call") {
    gPermitLimit = 1; // the Start check only
    const auto r = request();
    EXPECT(CeFullBackupUiStart(&r));
    runOnce(present);
    EXPECT(view().outcome == CE_FULL_BACKUP_OUT_AUTHORITY);
  } else if (scenario == "authority_inside_call") {
    gPermitLimit = 2; // Start and the Pump check; the store's own asks fail
    const auto r = request();
    EXPECT(CeFullBackupUiStart(&r));
    runOnce(present);
    EXPECT(view().outcome == CE_FULL_BACKUP_OUT_REFUSED);
  } else if (scenario == "no_folder") {
    const auto r = request(2, false, "");
    EXPECT(!CeFullBackupUiStart(&r));
    EXPECT(view().outcome == CE_FULL_BACKUP_OUT_NO_FOLDER);
  } else if (scenario == "no_careers") {
    const auto r = request(0);
    EXPECT(!CeFullBackupUiStart(&r));
    EXPECT(view().outcome == CE_FULL_BACKUP_OUT_NO_CAREERS &&
           !std::strcmp(view().message, "There are no careers to back up."));
  } else if (scenario == "other_backup") {
    const auto r = request(2, true);
    EXPECT(!CeFullBackupUiStart(&r));
    EXPECT(view().outcome == CE_FULL_BACKUP_OUT_BUSY);
  } else if (scenario == "custody_lock_held") {
    // Another backup or restore (here: another holder of the store's lock).
    const std::string lock = gBase + "/user/.ce-career-custody.lock";
    int fd = ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    EXPECT(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    const auto r = request();
    EXPECT(CeFullBackupUiStart(&r));
    runOnce(present);
    EXPECT(view().outcome == CE_FULL_BACKUP_OUT_BUSY);
    if (fd >= 0)
      close(fd);
  } else if (scenario == "other_thread") {
    const auto r = request();
    EXPECT(CeFullBackupUiStart(&r));
    CeFullBackupUiDrawn(present);
    std::thread([&] { CeFullBackupUiPump(present + 1, permit); }).join();
    EXPECT(view().phase == CE_FULL_BACKUP_WORKING && listed().empty());
  } else if (scenario == "leave_before_call") {
    const auto r = request();
    EXPECT(CeFullBackupUiStart(&r));
    CeFullBackupUiDrawn(present);
    CeFullBackupUiLeave();
    CeFullBackupUiPump(present + 1, permit);
    EXPECT(listed().empty() && view().phase == CE_FULL_BACKUP_IDLE);
  } else {
    return 2;
  }
  std::printf("%s %s\n", gFails ? "FAILED" : "ok", gScenario);
  return gFails ? 1 : 0;
}
