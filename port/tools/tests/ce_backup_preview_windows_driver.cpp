// Authored Windows fixture. Only a disposable prefix created by the test can
// enter.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../../platform/common/ce_backup_preview.h"
#include "../../platform/common/ce_career_restore.h"
#define CE_CAREER_RESTORE_UI_INTERNAL
#include "../../platform/common/ce_career_restore_ui.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

namespace {
const char *base = "C:\\ce-preview-authored";
std::string user, game, pkg, root, good;
const std::string mainName = u8"Auréשל.bes";
const char *goodLeaf = "onslaught-backup-02",
           *secondLeaf = "onslaught-backup-01";
SDL_DialogFileCallback deliver;
void *ticket;
std::atomic<int> observing{0};
std::atomic<bool> lateCompletion{false};
void expect(bool condition, const char *label) {
  if (!condition) {
    std::printf("FAIL %s\n", label);
    std::fflush(stdout);
    std::exit(30);
  }
  std::printf("PASS %s\n", label);
  std::fflush(stdout);
}
CePreviewDisplay card(unsigned index = 0) {
  CePreviewDisplay out{};
  CePreviewDisplayRead(index, &out);
  return out;
}
bool waitPhase(CePreviewPhase phase) {
  for (int i = 0; i < 1000; ++i) {
    if (card().phase == phase)
      return true;
    Sleep(5);
  }
  return false;
}
void choose(const char *value) {
  auto callback = deliver;
  auto *key = ticket;
  deliver = nullptr;
  ticket = nullptr;
  if (!callback)
    std::exit(50);
  const char *files[] = {value, nullptr};
  callback(key, files, 0);
}
std::wstring w(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1,
                              nullptr, 0);
  if (n <= 0)
    std::exit(51);
  std::wstring result(n, L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1,
                          &result[0], n) != n)
    std::exit(51);
  result.pop_back();
  return result;
}
bool read(const std::string &path, CePreviewSet &out) {
  std::string err;
  return CePreviewRead(path, user, game, pkg, out, err);
}
bool refused(const std::string &p) {
  CePreviewSet out;
  out.members = 9;
  std::string err;
  return !CePreviewRead(p, user, game, pkg, out, err) && !err.empty() &&
         out.careers.empty() && !out.members;
}
bool listRefused(const std::string &p) {
  std::string err;
  std::vector<std::string> out{"old"};
  return !CePreviewList(p, user, game, pkg, out, err) && !err.empty() &&
         out.empty();
}
bool swapDirs(const std::string &current, const std::string &other,
              bool undo = false) {
  const auto a = w(current), b = w(other), tmp = w(other + "-held");
  if (undo)
    return MoveFileW(a.c_str(), b.c_str()) && MoveFileW(tmp.c_str(), a.c_str());
  return MoveFileW(a.c_str(), tmp.c_str()) && MoveFileW(b.c_str(), a.c_str());
}
} // namespace

namespace {
std::string mark(int n, const char *suffix) {
  return std::string(base) + "\\test-coordination\\" +
         (n == 1 ? "same-" : "leave-") + suffix;
}
bool exists(const std::string &p) {
  return GetFileAttributesW(w(p).c_str()) != INVALID_FILE_ATTRIBUTES;
}
void signal(const std::string &p) {
  auto f = CreateFileW(w(p).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                       CREATE_NEW, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE)
    std::exit(61);
  CloseHandle(f);
}
bool waitMark(const std::string &p) {
  for (int i = 0; i < 1800; ++i) {
    if (exists(p))
      return true;
    Sleep(5);
  }
  return false;
}
} // namespace
void CePreviewObserveHeld(const wchar_t *leaf) {
  int n = observing.load();
  if (!n || std::wstring(leaf) != w(mainName))
    return;
  if (!observing.compare_exchange_strong(n, 0))
    return;
  signal(mark(n, "entered"));
  if (!waitMark(mark(n, "release")))
    std::exit(62);
}
void CePreviewObserveCompleted() {
  if (lateCompletion.exchange(false))
    signal(mark(2, "finished"));
}
extern "C" const char *SDL_GetBasePath(void) {
  return "C:\\ce-preview-authored\\package\\";
}
extern "C" SDL_Window *SDL_GetKeyboardFocus(void) { return nullptr; }
extern "C" void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback done,
                                         void *data, SDL_Window *, const char *,
                                         bool) {
  deliver = done;
  ticket = data;
}

int main(int argc, char **argv) {
  const char *guard = std::getenv("CE_BACKUP_DISPOSABLE");
  if (!guard || std::strcmp(guard, "1") != 0 ||
      GetFileAttributesW(L"C:\\ce-preview-authored\\user\\ce.ini") ==
          INVALID_FILE_ATTRIBUTES)
    return 90;
  user = std::string(base) + "\\user";
  game = std::string(base) + "\\game";
  pkg = std::string(base) + "\\package";
  root = std::string(base) + "\\sets";
  good = root + "\\" + goodLeaf;
  if (argc == 2 && std::string(argv[1]) == "observed") {
    observing = 1;
    bool changed = refused(root + "\\held-same");
    signal(mark(1, "done"));
    expect(changed, "held-byte-identical-replacement");
    CePreviewLeave();
    CePreviewOpen(user.c_str(), game.c_str(), "");
    CePreviewChoose();
    observing = 2;
    lateCompletion = true;
    std::string stale = root + "\\held-leave";
    choose(stale.c_str());
    if (!waitMark(mark(2, "entered")))
      return 63;
    CePreviewLeave();
    CePreviewOpen(user.c_str(), game.c_str(), "");
    signal(mark(2, "closed"));
    if (!waitMark(mark(2, "finished")))
      return 64;
    signal(mark(2, "done"));
    expect(card().phase == CE_PREVIEW_IDLE && !card().careers &&
               !card().choices && !card().name[0],
           "final-owner-after-worker-back");
    CePreviewLeave();
    return 0;
  }
  std::vector<std::string> names;
  std::string error;
  expect(CePreviewList(root, user, game, pkg, names, error) &&
             names == std::vector<std::string>({goodLeaf, secondLeaf}),
         "list-and-sort");
  CePreviewSet a, b;
  expect(read(good, a) && a.members == 2 && a.careers.size() == 1 &&
             a.careers[0].name == mainName &&
             a.careers[0].proposal == "Backup Copy 3.bes" &&
             a.careers[0].bytes.size() == 10004 && a.identity.size() == 64 &&
             a.reason == "Manual",
         "selected-unicode-career-and-options");
  expect(read(root + "\\" + secondLeaf, b) && b.careers.size() == 2 &&
             b.members == 3 && b.careers[0].proposal == "Backup Copy 3.bes" &&
             b.careers[1].proposal == "Backup Copy 4.bes",
         "multi-career-and-disjoint-proposals");
  CePreviewSet writer;
  expect(read(std::string(base) + "\\writer\\published", writer) &&
             writer.careers.size() == 1 &&
             writer.careers[0].name == "Writer.bes" &&
             writer.careers[0].bytes == a.careers[0].bytes &&
             writer.careers[0].proposal == "Backup Copy 3.bes",
         "writer-interoperability");
  for (const auto &row :
       {std::pair<const char *, const CePreviewSet *>{"01", &b},
        {"02", &a},
        {"writer", &writer}})
    std::printf("READ %s %s %d %u\n", row.first, row.second->identity.c_str(),
                row.second->members,
                static_cast<unsigned>(row.second->careers.size()));
  for (const auto &test : {std::pair<const char *, const char *>{
                               "no-manifest", "missing-manifest"},
                           {"bad-career", "corrupt-career"},
                           {"bad-options", "corrupt-options"},
                           {"missing", "missing-member"},
                           {"bad-type", "unsupported-member"},
                           {"bad-leaf", "ambiguous-leaf"},
                           {"hard", "linked-member"},
                           {"bad-shape", "manifest-shape"}})
    expect(refused(root + "\\" + test.first), test.second);
  expect(refused(user + "\\nested\\onslaught-backup-05") && refused(game) &&
             refused(pkg) && listRefused("T:\\") &&
             refused("T:\\onslaught-backup-05") &&
             refused(root + "\\..\\" + goodLeaf),
         "protected-alias-and-roots");
  auto held = CreateFileW(w(good + "\\" + mainName).c_str(), GENERIC_WRITE,
                          FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
  bool locked = held != INVALID_HANDLE_VALUE && refused(good);
  if (held != INVALID_HANDLE_VALUE)
    CloseHandle(held);
  expect(locked && read(good, a), "read-only-busy-member");
  CePreviewOpen(user.c_str(), game.c_str(), root.c_str());
  char label[100]{};
  bool listed = waitPhase(CE_PREVIEW_IDLE) && card().choices == 2 &&
                CePreviewDisplaySetName(0, label, sizeof label) &&
                std::string(label) == goodLeaf;
  CePreviewSelectSet(0);
  bool ok = waitPhase(CE_PREVIEW_VERIFIED);
  auto verified = card();
  expect(listed && ok && verified.careers == 1 && verified.members == 2 &&
             verified.complete == 1 && verified.name == mainName &&
             std::string(verified.proposal) == "Backup Copy 3.bes" &&
             std::string(verified.reason) == "Manual",
         "normal-selected-card");
  CePreviewChoose();
  bool picking = card().phase == CE_PREVIEW_PICKING;
  choose(nullptr);
  expect(picking && card().phase == CE_PREVIEW_VERIFIED &&
             card().name == mainName,
         "cancel-keeps-card");
  CePreviewLeave();
  CePreviewOpen(user.c_str(), game.c_str(), "");
  CePreviewChoose();
  choose(nullptr);
  expect(card().phase == CE_PREVIEW_IDLE && card().careers == 0,
         "cancel-before-selection");
  CePreviewChoose();
  CePreviewLeave();
  CePreviewOpen(user.c_str(), game.c_str(), "");
  choose(good.c_str());
  expect(card().phase == CE_PREVIEW_IDLE && !card().careers && !card().choices,
         "stale-dialog-after-back");
  CePreviewChoose();
  choose(good.c_str());
  expect(waitPhase(CE_PREVIEW_VERIFIED) && card().name == mainName,
         "typed-folder-card");
  std::string replacement = root + "\\replacement";
  if (!swapDirs(good, replacement))
    return 52;
  CePreviewAgain();
  bool exchanged =
      waitPhase(CE_PREVIEW_ERROR) && card().careers == 0 && card().name[0] == 0;
  bool restored = swapDirs(good, replacement, true);
  expect(exchanged && restored, "recheck-replaced-set");
  CePreviewLeave();
  expect(card().phase == CE_PREVIEW_IDLE && !card().careers && !card().choices,
         "leave-clears-card");
  {
    CeRestoreCard rcard;
    CePreviewOpen((user + "\\\\").c_str(), (game + "\\/").c_str(), "R:\\");
    bool r1 = waitPhase(CE_PREVIEW_IDLE) && card().choices == 2;
    CePreviewSelectSet(0);
    r1 = r1 && waitPhase(CE_PREVIEW_VERIFIED);
    CePreviewRestoreCard(rcard);
    r1 = r1 && rcard.verified && rcard.root == "R:\\" &&
         rcard.set == std::string("R:\\") + goodLeaf &&
         rcard.user == user && rcard.game == game && rcard.package == pkg;
    CePreviewLeave();

    CePreviewOpen(user.c_str(), game.c_str(), "R:/");
    bool r2 = waitPhase(CE_PREVIEW_IDLE) && card().choices == 2;
    CePreviewSelectSet(0);
    r2 = r2 && waitPhase(CE_PREVIEW_VERIFIED);
    CePreviewRestoreCard(rcard);
    r2 = r2 && rcard.verified && rcard.root == "R:/" &&
         rcard.set == std::string("R:/") + goodLeaf;
    CePreviewChoose();
    choose((std::string("R:\\") + goodLeaf + "\\\\").c_str());
    r2 = r2 && waitPhase(CE_PREVIEW_VERIFIED);
    CePreviewRestoreCard(rcard);
    r2 = r2 && rcard.verified && rcard.root == "R:/" &&
         rcard.set == std::string("R:\\") + goodLeaf;
    CePreviewLeave();

    CePreviewOpen(user.c_str(), game.c_str(), "R:\\\\");
    r2 = r2 && waitPhase(CE_PREVIEW_IDLE) && card().choices == 2;
    CePreviewSelectSet(0);
    r2 = r2 && waitPhase(CE_PREVIEW_VERIFIED);
    CePreviewRestoreCard(rcard);
    r2 = r2 && rcard.verified && rcard.root == "R:\\" &&
         rcard.set == std::string("R:\\") + goodLeaf;
    CePreviewLeave();

    CePreviewOpen(user.c_str(), game.c_str(), (root + "\\\\").c_str());
    bool r3 = waitPhase(CE_PREVIEW_IDLE) && card().choices == 2;
    CePreviewSelectSet(0);
    r3 = r3 && waitPhase(CE_PREVIEW_VERIFIED);
    CePreviewRestoreCard(rcard);
    r3 = r3 && rcard.verified && rcard.root == root && rcard.set == good;
    CePreviewLeave();

    expect(r1 && r2 && r3, "drive-root-and-trailing-separator-card");
  }
  return 0;
}
