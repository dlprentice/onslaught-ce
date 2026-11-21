#include "../../game/ce_career_view.h"
#include <cstdio>
#include <cstring>
// SPDX-License-Identifier: GPL-3.0-only
#include "ce_backup_preview.h"
#include "ce_career_restore.h"
#define CE_CAREER_RESTORE_UI_INTERNAL
#include "ce_career_restore_ui.h"
#include <SDL3/SDL.h>
#include <mutex>
#include <new>
#include <thread>
namespace {
struct State {
  std::mutex lock;
  uint64_t page = 0, action = 0;
  bool active = false;
  CePreviewState view;
  CePreviewSet result;
  bool hasSet = false;
  // Taken by the verification that produced result, never afterwards: a card
  // that is already showing is not given a newer stamp.
  std::string stamp;
  std::string pick, user, game, package, base;
};
State &state() {
  static State *s = new State;
  return *s;
}
// The storage interface takes a folder without a trailing separator.
std::string trimmed(std::string path) {
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
  return path;
}
// Read-only: binds the identity just verified to the set's folder and files.
// Empty when they cannot be bound (always so where restoring is unsupported).
std::string takeStamp(const std::string &set, const std::string &user,
                      const std::string &game, const std::string &package,
                      const std::string &identity) {
  try {
    CeCareerRestoreRequest request;
    request.user = trimmed(user);
    request.game = trimmed(game);
    request.package = trimmed(package);
    request.sourceSet = trimmed(set);
    request.sourceIdentity = identity;
    std::string stamp;
    if (CeCareerRestoreStamp(request, stamp))
      return stamp;
  } catch (...) {
  }
  return std::string();
}
void message(State &s, CePreviewState::Phase p, const char *text) {
  s.view.phase = p;
  s.view.message = text;
}
void start(State &s, bool same) {
  auto token = s.page, action = ++s.action;
  auto path = s.pick, user = s.user, game = s.game, package = s.package;
  std::string identity = same && s.hasSet ? s.result.identity : "";
  s.hasSet = false;
  s.stamp.clear();
  ++s.view.generation;
  message(s, CePreviewState::Working, "Checking every file in this backup...");
  try {
    std::thread([=] {
      CePreviewSet value;
      std::string why, stamp;
      bool ok = false;
      try {
        ok = CePreviewRead(path, user, game, package, value, why);
        if (ok && !identity.empty() && value.identity != identity) {
          ok = false;
          why = "This backup changed. Choose its folder again.";
        }
        // The stamp is taken here, against the identity this card will show.
        if (ok)
          stamp = takeStamp(path, user, game, package, value.identity);
      } catch (...) {
        ok = false;
        why = "This backup could not be read.";
      }
      auto &shared = state();
      std::lock_guard<std::mutex> guard(shared.lock);
      if (shared.active && shared.page == token && shared.action == action) {
        shared.hasSet = ok;
        shared.result = std::move(value);
        if (ok)
          shared.stamp = std::move(stamp);
        else
          shared.stamp.clear();
        ++shared.view.generation;
        message(shared, ok ? CePreviewState::Verified : CePreviewState::Error,
                ok ? "Backup verified. Verifying restores nothing."
                   : why.c_str());
      }
    }).detach();
  } catch (...) {
    message(s, CePreviewState::Error,
            "Verification could not start. Try again.");
  }
}
struct Ticket {
  uint64_t page;
  CePreviewState::Phase before;
};
void SDLCALL chosen(void *opaque, const char *const *files, int) {
  Ticket saved = *static_cast<Ticket *>(opaque);
  delete static_cast<Ticket *>(opaque);
  const Ticket *t = &saved;
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  if (!s.active || s.page != t->page || s.view.phase != CePreviewState::Picking)
    return;
  if (!files || !files[0] || !*files[0]) {
    message(s, t->before, "Folder choice cancelled. Nothing was restored.");
    return;
  }
  size_t length = 0;
  while (length < 4096 && files[0][length])
    ++length;
  if (length >= 4096) {
    s.hasSet = false;
    s.stamp.clear();
    ++s.view.generation;
    s.pick.clear();
    message(s, CePreviewState::Error, "Choose a shorter backup folder path.");
    return;
  }
  s.pick = files[0];
  start(s, false);
}
} // namespace
void CePreviewOpen(const char *user, const char *game, const char *root) {
  auto &s = state();
  std::lock_guard<std::mutex> g(s.lock);
  ++s.page;
  s.active = true;
  s.hasSet = false;
  s.stamp.clear();
  s.pick.clear();
  s.view = {};
  s.view.generation = s.page;
  s.user = user ? user : "";
  s.game = game ? game : "";
  const char *b = SDL_GetBasePath();
  s.package = b ? b : "";
  // Keep a Windows drive root absolute if the program is installed there.
  while (s.package.size() > 1 &&
         (s.package.back() == '/' || s.package.back() == '\\') &&
         !(s.package.size() == 3 && s.package[1] == ':'))
    s.package.pop_back();
  message(s, CePreviewState::Idle,
          "Choose the folder containing the backup manifest.");
  s.base = root ? root : "";
  if (s.base.empty())
    return;
  auto token = s.page, action = ++s.action;
  auto path = s.base, u = s.user, gam = s.game, pkg = s.package;
  message(s, CePreviewState::Working,
          "Looking for sets in your backup folder...");
  try {
    std::thread([=] {
      std::vector<std::string> list;
      std::vector<CePreviewState::Holds> holds;
      std::string why;
      bool ok = false;
      try {
        ok = CePreviewList(path, u, gam, pkg, list, why);
        // Each listed set is verified once here, read-only, so the list can
        // say what it holds; a set that does not verify says so.
        for (size_t i = 0; ok && i < list.size(); ++i) {
          CePreviewSet set;
          std::string error;
          CePreviewState::Holds row;
          row.verified =
              CePreviewRead(path + "/" + list[i], u, gam, pkg, set, error);
          if (row.verified) {
            row.careers = int(set.careers.size());
            row.options = set.members > row.careers;
            row.reason = set.reason;
          }
          holds.push_back(std::move(row));
        }
      } catch (...) {
        ok = false;
        list.clear();
        holds.clear();
        why = "The backup list could not be read.";
      }
      auto &shared = state();
      std::lock_guard<std::mutex> lock(shared.lock);
      if (shared.active && shared.page == token && shared.action == action) {
        shared.view.choices = std::move(list);
        shared.view.holds = std::move(holds);
        message(shared, ok ? CePreviewState::Idle : CePreviewState::Error,
                ok ? (shared.view.choices.empty()
                          ? "No published sets found. Choose another folder."
                          : "Choose a set to verify and inspect.")
                   : why.c_str());
      }
    }).detach();
  } catch (...) {
    message(s, CePreviewState::Error, "The backup list could not open.");
  }
}
void CePreviewSelectSet(unsigned index) {
  auto &s = state();
  std::lock_guard<std::mutex> g(s.lock);
  if (!s.active || s.view.phase == CePreviewState::Picking ||
      s.view.phase == CePreviewState::Working || index >= s.view.choices.size())
    return;
  s.pick = s.base + "/" + s.view.choices[index];
  start(s, false);
}
void CePreviewLeave() {
  auto &s = state();
  std::lock_guard<std::mutex> g(s.lock);
  ++s.page;
  ++s.action;
  s.active = false;
  s.hasSet = false;
  s.stamp.clear();
  s.pick.clear();
  s.view = {};
  s.view.generation = s.page;
}
void CePreviewChoose() {
  auto &s = state();
  Ticket *t = nullptr;
  {
    std::lock_guard<std::mutex> g(s.lock);
    if (!s.active || s.view.phase == CePreviewState::Working ||
        s.view.phase == CePreviewState::Picking)
      return;
    t = new (std::nothrow) Ticket{s.page, s.view.phase};
    if (!t) {
      message(s, CePreviewState::Error, "Folder choice could not start.");
      return;
    }
    message(s, CePreviewState::Picking, "Waiting for your folder choice.");
  }
  SDL_ShowOpenFolderDialog(chosen, t, SDL_GetKeyboardFocus(), nullptr, false);
}
void CePreviewAgain() {
  auto &s = state();
  std::lock_guard<std::mutex> g(s.lock);
  if (s.active && !s.pick.empty() && s.view.phase != CePreviewState::Picking &&
      s.view.phase != CePreviewState::Working)
    start(s, true);
}
void CePreviewDisplayRead(unsigned career, CePreviewDisplay *out) {
  if (!out)
    return;
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  *out = {};
  out->phase = static_cast<CePreviewPhase>(s.view.phase);
  out->choices = int(s.view.choices.size());
  std::snprintf(out->message, sizeof(out->message), "%s",
                s.view.message.c_str());
  if (!s.hasSet || s.result.careers.empty())
    return;
  const auto &v = s.result;
  out->members = v.members;
  out->careers = int(v.careers.size());
  auto &f = v.careers[career < v.careers.size() ? career : 0];
  std::snprintf(out->date, sizeof(out->date), "%s", v.date.c_str());
  std::snprintf(out->reason, sizeof(out->reason), "%s", v.reason.c_str());
  std::snprintf(out->name, sizeof(out->name), "%s", f.name.c_str());
  std::snprintf(out->proposal, sizeof(out->proposal), "%s", f.proposal.c_str());
  SCeCareerView progress{};
  if (CeCareerViewBuild(f.bytes.data(), int(f.bytes.size()), &progress)) {
    out->complete = progress.mMissionsComplete;
    out->goodies = progress.mGoodiesFound;
  }
}
// For the Restore as New controller only: what the card shows now, with the
// stamp its own verification took. Copies; the caller owns them.
void CePreviewRestoreCard(CeRestoreCard &out) {
  out = CeRestoreCard();
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  out.open = s.active;
  out.page = s.page;
  out.generation = s.view.generation;
  if (!s.active)
    return;
  out.user = trimmed(s.user);
  out.game = trimmed(s.game);
  out.package = trimmed(s.package);
  out.root = trimmed(s.base);
  if (!s.hasSet || s.view.phase != CePreviewState::Verified ||
      s.result.careers.empty() || s.pick.empty())
    return;
  out.set = trimmed(s.pick);
  out.identity = s.result.identity;
  out.stamp = s.stamp;
  out.date = s.result.date;
  for (const auto &career : s.result.careers) {
    CeRestoreCard::Career row;
    row.name = career.name;
    SCeCareerView progress{};
    if (CeCareerViewBuild(career.bytes.data(), int(career.bytes.size()),
                          &progress)) {
      row.complete = progress.mMissionsComplete;
      row.goodies = progress.mGoodiesFound;
    }
    out.careers.push_back(std::move(row));
  }
  out.verified = true;
}
bool CePreviewDisplaySetName(unsigned index, char *out, unsigned capacity) {
  if (!out || !capacity)
    return false;
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  if (index >= s.view.choices.size())
    return false;
  std::snprintf(out, capacity, "%s", s.view.choices[index].c_str());
  return true;
}
bool CePreviewDisplaySetHolds(unsigned index, CePreviewSetHolds *out) {
  if (!out)
    return false;
  std::memset(out, 0, sizeof(*out));
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  if (index >= s.view.choices.size() || index >= s.view.holds.size())
    return false;
  const auto &h = s.view.holds[index];
  out->verified = h.verified ? 1 : 0;
  out->careers = h.careers;
  out->options = h.options ? 1 : 0;
  std::snprintf(out->reason, sizeof(out->reason), "%s", h.reason.c_str());
  return true;
}
