// SPDX-License-Identifier: GPL-3.0-only
// Restore as New: the controller between the Backup Preview page and the
// storage interface (ce_career_restore.h). It never opens or writes a file
// itself.
//
// A restore starts from the card the preview is showing: its set, identity,
// chosen career and the stamp that card's own verification took. Those are
// copied once and compared with the live card again at confirmation and
// immediately before the storage call, so a card verified again, a set chosen
// again or a page reopened can never be written from an older choice.
//
// The name lookup is read-only and runs on a worker that owns copies; its
// answer is dropped unless the same page and the same request are still
// waiting. Filename semantics, permission and the one storage call all run on
// the game thread. The storage call waits until the Working rows were drawn and
// a Present finished after them. It is made once: no outcome is retried here.
#define CE_CAREER_RESTORE_UI_INTERNAL
#include "ce_career_restore_ui.h"
#include "ce_career_restore.h"
#include "portlog.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace {
// CE_RESTORE_UI_TEST_WINDOWS_COPY is a host test seam only: it selects the
// Windows wording for a test built with the Windows storage source. It makes
// no claim about a Windows system.
#if defined(_WIN32) || defined(CE_RESTORE_UI_TEST_WINDOWS_COPY)
const bool kWindows = true;
#else
const bool kWindows = false;
#endif

using Status = CeCareerStorageStatus;

// What one restore is bound to. Never changed after the lookup begins.
struct Bound {
  uint64_t page = 0, generation = 0;
  unsigned career = 0;
  int careers = 0, complete = 0, goodies = 0;
  std::string set, identity, stamp, user, game, package, root, date, source;
};
struct Outcome {
  CeRestoreOutcome kind = CE_RESTORE_OUT_NONE;
  bool lookup = false;   // ended before any confirmation
  bool possible = false; // a new career exists or may exist
  std::string backup;
};
struct State {
  std::mutex lock;
  uint64_t page = 0, op = 0;
  bool active = false, hasOwner = false;
  std::thread::id owner;
  CeRestorePhase phase = CE_RESTORE_IDLE;
  bool delivered = false, approved = false, publishing = false, drawn = false;
  unsigned drawnAt = 0;
  Status lookup = Status::Refused;
  Bound bound;
  std::string target;
  Outcome outcome;
};
State &state() {
  static State *s = new State;
  return *s;
}
// Lock held. The page is open and this is the thread that opened it.
bool mine(const State &s) {
  return s.active && s.hasOwner && s.owner == std::this_thread::get_id();
}
// Lock held. Any worker still out belongs to an older request from here on.
void reset(State &s) {
  ++s.op;
  s.phase = CE_RESTORE_IDLE;
  s.delivered = s.approved = s.publishing = s.drawn = false;
  s.drawnAt = 0;
  s.lookup = Status::Refused;
  s.bound = Bound();
  s.target.clear();
  s.outcome = Outcome();
}
void finish(State &s, CeRestoreOutcome kind, bool lookup, bool possible,
            std::string &&backup) {
  ++s.op;
  s.phase = CE_RESTORE_DONE;
  s.delivered = s.approved = s.publishing = s.drawn = false;
  s.drawnAt = 0;
  s.outcome.kind = kind;
  s.outcome.lookup = lookup;
  s.outcome.possible = possible;
  s.outcome.backup = std::move(backup);
}
void finish(State &s, CeRestoreOutcome kind, bool lookup) {
  finish(s, kind, lookup, false, std::string());
}
// An outcome the player must still be shown if the page closes first.
bool retained(const State &s) {
  return s.phase == CE_RESTORE_DONE &&
         (s.outcome.possible || !s.outcome.backup.empty());
}
bool card(CeRestoreCard &out) {
  try {
    CePreviewRestoreCard(out);
    return true;
  } catch (...) {
    out = CeRestoreCard();
    return false;
  }
}
CeRestoreOffer offerOf(const CeRestoreCard &c) {
  if (!c.open || !c.verified || c.careers.empty())
    return CE_RESTORE_OFFER_NONE;
  if (c.stamp.empty())
    return CE_RESTORE_OFFER_UNBOUND;
  if (c.root.empty())
    return CE_RESTORE_OFFER_NO_FOLDER;
  return CE_RESTORE_OFFER_READY;
}
// The live card is still exactly the one this restore was bound to.
bool matches(const CeRestoreCard &c, const Bound &b) {
  return c.open && c.verified && c.page == b.page &&
         c.generation == b.generation && !b.stamp.empty() &&
         c.stamp == b.stamp && c.identity == b.identity && c.set == b.set &&
         c.user == b.user && c.game == b.game && c.package == b.package &&
         c.root == b.root && b.career < c.careers.size() &&
         c.careers[b.career].name == b.source;
}
CeRestoreOutcome kindOf(Status status) {
  switch (status) {
  case Status::Complete:
    return CE_RESTORE_OUT_COMPLETE;
  case Status::Refused:
    return CE_RESTORE_OUT_REFUSED;
  case Status::Busy:
    return CE_RESTORE_OUT_BUSY;
  case Status::Cancelled:
    return CE_RESTORE_OUT_CANCELLED;
  case Status::Changed:
    return CE_RESTORE_OUT_CHANGED;
  case Status::Occupied:
    return CE_RESTORE_OUT_OCCUPIED;
  case Status::Uncertain:
    return CE_RESTORE_OUT_UNCERTAIN;
  case Status::Unsupported:
    return CE_RESTORE_OUT_UNSUPPORTED;
  }
  return CE_RESTORE_OUT_REFUSED;
}
CeCareerRestoreRequest requestOf(const Bound &b) {
  CeCareerRestoreRequest r;
  r.user = b.user;
  r.game = b.game;
  r.package = b.package;
  r.backupRoot = b.root;
  r.sourceSet = b.set;
  r.sourceIdentity = b.identity;
  r.sourceName = b.source;
  r.sourceStamp = b.stamp;
  return r;
}
// The storage call's owner: lives on the game thread's stack for exactly the
// one synchronous call. The pair is the one approved on that thread.
struct Call {
  CeRestoreAuthority authority{nullptr, nullptr};
  std::thread::id owner;
  std::string source, target;
};
bool permittedHere(void *opaque) {
  const Call *call = static_cast<const Call *>(opaque);
  return call && call->owner == std::this_thread::get_id() &&
         call->authority.permitted && call->authority.permitted();
}
// Compares with the frozen pair only: no game state is read here.
bool namedHere(void *opaque, const std::string &source,
               const std::string &target) {
  const Call *call = static_cast<const Call *>(opaque);
  return call && !call->target.empty() && source == call->source &&
         target == call->target;
}
void put(char *out, size_t room, const std::string &text) {
  std::snprintf(out, room, "%s", text.c_str());
}
std::string leaf(const std::string &path) {
  const size_t at = path.find_last_of("/\\");
  return at == std::string::npos ? path : path.substr(at + 1);
}
// Each hint is at most three lines on the page: a message of one line (about
// 42 characters) and a detail of at most two, which the list box balances.
void words(const State &s, CeRestoreOffer offer, const char *&message,
           const char *&detail) {
  message = detail = "";
  switch (s.phase) {
  case CE_RESTORE_IDLE:
    if (offer == CE_RESTORE_OFFER_READY) {
      message = "Copy it into your saves as a new career.";
      detail = "You see its new name and confirm first. Nothing is replaced.";
    } else if (offer == CE_RESTORE_OFFER_WINDOWS) {
      message = "Restore as New is unavailable on Windows.";
      detail = "You can still verify and inspect this backup.";
    } else if (offer == CE_RESTORE_OFFER_UNBOUND) {
      message = "This backup could not be held safely.";
      detail = "Select Verify Again. The set must be on a local disk.";
    } else if (offer == CE_RESTORE_OFFER_NO_FOLDER) {
      message = "Choose a backup folder first.";
      detail = "Your careers are backed up before a restore. Set it in Back "
               "Up Career.";
    }
    return;
  case CE_RESTORE_PROPOSING:
    message = "Looking for a free name...";
    detail = "Nothing has been written.";
    return;
  case CE_RESTORE_CONFIRM:
    message = "Nothing is written until you confirm.";
    detail = "The name is not reserved. If it is taken first, nothing is "
             "written.";
    return;
  case CE_RESTORE_WORKING:
    message = "Backing up your careers first...";
    detail = "Then the new career is added. This step cannot be cancelled.";
    return;
  case CE_RESTORE_DONE:
    break;
  }
  const bool lookup = s.outcome.lookup;
  switch (s.outcome.kind) {
  case CE_RESTORE_OUT_NONE:
    return;
  case CE_RESTORE_OUT_COMPLETE:
    message = "The new career was created.";
    detail = "Find it in Load Game. Nothing else was changed.";
    return;
  case CE_RESTORE_OUT_UNCERTAIN:
    message = "Not confirmed: it may already exist.";
    detail = "Check Load Game before any retry. Nothing was removed.";
    return;
  case CE_RESTORE_OUT_CHANGED:
    message = lookup ? "The backup or your saves changed."
                     : "No career created: something changed.";
    detail = "Nothing was written. Select Verify Again and review it first.";
    return;
  case CE_RESTORE_OUT_OCCUPIED:
    message = lookup ? "No free name was found."
                     : "No career created: the name was taken.";
    detail = lookup ? "Nothing was written. Rename or remove older restored "
                      "copies."
                    : "Start Restore as New again to see a new name.";
    return;
  case CE_RESTORE_OUT_BUSY:
    message = "Another save or backup is in progress.";
    detail = "No career was created. Wait, then try Restore as New again.";
    return;
  case CE_RESTORE_OUT_CANCELLED:
    message = "Cancelled. No career was created.";
    return;
  case CE_RESTORE_OUT_UNSUPPORTED:
    if (kWindows) {
      message = "This drive is not supported for restore.";
      detail = "Nothing was written. Keep saves and backups on a local NTFS "
               "drive.";
    } else {
      message = "This storage is not safe for a restore.";
      detail = "Nothing was written. Keep saves and backups on a local disk.";
    }
    return;
  case CE_RESTORE_OUT_NAME:
    message = "The game treats this name specially.";
    detail = "A copy under a new name would not behave the same. Nothing was "
             "written.";
    return;
  case CE_RESTORE_OUT_AUTHORITY:
    message = "Careers cannot be written right now.";
    detail = "Nothing was written. Leave any editor, museum or network "
             "session first.";
    return;
  case CE_RESTORE_OUT_FAILED:
    message = "The name check could not start.";
    detail = "Nothing was written. Select Restore as New to try again.";
    return;
  case CE_RESTORE_OUT_REFUSED:
    message = "This career cannot be restored here.";
    detail = lookup ? "Nothing was written. Its name or your saves could not "
                      "be used safely."
                    : "No career was created. Verify the backup again before "
                      "another try.";
    return;
  }
}
// The storage stopped after its first step, the backup of the current careers:
// that new set is real and is said so, and a refused write to the saves folder
// (after the folder was already found to be on a local disk) is named as that.
void backedUpWords(const State &s, const char *&message, const char *&detail) {
  if (s.phase != CE_RESTORE_DONE || s.outcome.backup.empty() ||
      s.outcome.kind == CE_RESTORE_OUT_COMPLETE ||
      s.outcome.kind == CE_RESTORE_OUT_UNCERTAIN)
    return;
  switch (s.outcome.kind) {
  case CE_RESTORE_OUT_UNSUPPORTED:
    message = "Your saves folder could not be written.";
    detail = "No career was created. Your careers were backed up first, as a "
             "new set.";
    return;
  case CE_RESTORE_OUT_CHANGED:
    detail = "Your careers were backed up first, as a new set. Verify Again "
             "before retrying.";
    return;
  case CE_RESTORE_OUT_OCCUPIED:
    detail = "Your careers were backed up first. Start Restore as New again "
             "for a new name.";
    return;
  default:
    detail = "No career was created. Your careers were backed up first, as a "
             "new set.";
    return;
  }
}
} // namespace

void CeRestoreUiOpen() {
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  ++s.page;
  ++s.op;
  s.active = true;
  s.owner = std::this_thread::get_id();
  s.hasOwner = true;
  // An outcome that wrote, or may have written, stays until it is read.
  if (!retained(s))
    reset(s);
}

void CeRestoreUiLeave() {
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  ++s.page;
  ++s.op;
  s.active = false;
  if (!retained(s))
    reset(s);
}

bool CeRestoreUiBegin(unsigned career, const CeRestoreAuthority *authority) {
  auto &s = state();
  try {
    {
      std::lock_guard<std::mutex> guard(s.lock);
      if (!mine(s) || s.phase != CE_RESTORE_IDLE)
        return false;
    }
    CeRestoreCard now;
    if (!card(now) || offerOf(now) != CE_RESTORE_OFFER_READY ||
        career >= now.careers.size())
      return false;
    const bool allowed = authority && authority->permitted &&
                         authority->namesCompatible && authority->permitted();
    Bound bound;
    bound.page = now.page;
    bound.generation = now.generation;
    bound.career = career;
    bound.careers = int(now.careers.size());
    bound.complete = now.careers[career].complete;
    bound.goodies = now.careers[career].goodies;
    bound.set = now.set;
    bound.identity = now.identity;
    bound.stamp = now.stamp;
    bound.user = now.user;
    bound.game = now.game;
    bound.package = now.package;
    bound.root = now.root;
    bound.date = now.date;
    bound.source = now.careers[career].name;
    const CeCareerRestoreRequest request = requestOf(bound);
    std::lock_guard<std::mutex> guard(s.lock);
    if (!mine(s) || s.phase != CE_RESTORE_IDLE)
      return false;
    reset(s);
    s.bound = std::move(bound);
    if (!allowed) {
      finish(s, CE_RESTORE_OUT_AUTHORITY, true);
      return false;
    }
    const uint64_t page = s.page, op = ++s.op;
    s.phase = CE_RESTORE_PROPOSING;
    try {
      // Read-only. The worker owns its copies and can only deliver a name.
      std::thread([page, op, request] {
        std::string name;
        Status status = Status::Refused;
        try {
          status = CeCareerRestoreSuggest(request, name);
        } catch (...) {
          status = Status::Refused;
          name.clear();
        }
        auto &shared = state();
        std::lock_guard<std::mutex> lock(shared.lock);
        if (shared.active && shared.page == page && shared.op == op &&
            shared.phase == CE_RESTORE_PROPOSING && !shared.delivered) {
          shared.lookup = status;
          shared.target = std::move(name);
          shared.delivered = true;
        }
      }).detach();
    } catch (...) {
      finish(s, CE_RESTORE_OUT_FAILED, true);
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool CeRestoreUiCancel() {
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  if (!mine(s) || s.publishing)
    return false;
  if (s.phase != CE_RESTORE_PROPOSING && s.phase != CE_RESTORE_CONFIRM &&
      s.phase != CE_RESTORE_WORKING)
    return false;
  reset(s);
  return true;
}

bool CeRestoreUiConfirm(const CeRestoreAuthority *authority) {
  auto &s = state();
  try {
    Bound bound;
    uint64_t op = 0;
    {
      std::lock_guard<std::mutex> guard(s.lock);
      if (!mine(s) || s.phase != CE_RESTORE_CONFIRM || !s.approved)
        return false;
      bound = s.bound;
      op = s.op;
    }
    const bool allowed =
        authority && authority->permitted && authority->permitted();
    CeRestoreCard now;
    const bool same = card(now) && matches(now, bound);
    std::lock_guard<std::mutex> guard(s.lock);
    if (!mine(s) || s.op != op || s.phase != CE_RESTORE_CONFIRM || !s.approved)
      return false;
    if (!allowed) {
      finish(s, CE_RESTORE_OUT_AUTHORITY, false);
      return false;
    }
    if (!same) {
      finish(s, CE_RESTORE_OUT_CHANGED, false);
      return false;
    }
    s.phase = CE_RESTORE_WORKING;
    s.approved = true;
    s.publishing = false;
    s.drawn = false;
    s.drawnAt = 0;
    return true;
  } catch (...) {
    return false;
  }
}

void CeRestoreUiDrawn(unsigned presentCount) {
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  if (mine(s) && s.phase == CE_RESTORE_WORKING && s.approved &&
      !s.publishing && !s.drawn) {
    s.drawn = true;
    s.drawnAt = presentCount;
  }
}

void CeRestoreUiPump(unsigned presentCount,
                     const CeRestoreAuthority *authority) {
  auto &s = state();
  try {
    Bound bound;
    std::string target;
    uint64_t op = 0;
    bool approve = false;
    {
      std::lock_guard<std::mutex> guard(s.lock);
      if (!mine(s))
        return;
      if (s.phase == CE_RESTORE_PROPOSING && s.delivered) {
        if (s.lookup != Status::Complete || s.target.empty() ||
            s.target.size() >= sizeof(CeRestoreDisplay::target)) {
          CeRestoreOutcome kind = kindOf(s.lookup);
          if (kind == CE_RESTORE_OUT_COMPLETE ||
              kind == CE_RESTORE_OUT_UNCERTAIN)
            kind = CE_RESTORE_OUT_REFUSED; // a lookup writes nothing
          s.target.clear();
          finish(s, kind, true);
          return;
        }
        approve = true;
      } else if (s.phase == CE_RESTORE_WORKING && s.approved &&
                 !s.publishing && s.drawn &&
                 unsigned(presentCount - s.drawnAt) != 0u) {
        // A Present finished after the Working rows were drawn.
      } else {
        return;
      }
      bound = s.bound;
      target = s.target;
      op = s.op;
    }
    // Game thread, no lock held: the game's own gates.
    const bool allowed =
        authority && authority->permitted && authority->permitted();
    if (approve) {
      const bool compatible =
          allowed && authority->namesCompatible &&
          authority->namesCompatible(bound.source.c_str(), target.c_str());
      std::lock_guard<std::mutex> guard(s.lock);
      if (!mine(s) || s.op != op || s.phase != CE_RESTORE_PROPOSING ||
          !s.delivered)
        return;
      if (!allowed) {
        s.target.clear(); // never shown: it was not approved
        finish(s, CE_RESTORE_OUT_AUTHORITY, true);
      } else if (!compatible) {
        s.target.clear();
        finish(s, CE_RESTORE_OUT_NAME, true);
      } else {
        // This exact pair, and no other, is what the storage call may use.
        s.delivered = false;
        s.approved = true;
        s.phase = CE_RESTORE_CONFIRM;
      }
      return;
    }
    CeRestoreCard now;
    const bool same = card(now) && matches(now, bound);
    const bool compatible =
        allowed && same && authority->namesCompatible &&
        authority->namesCompatible(bound.source.c_str(), target.c_str());
    Call call;
    if (authority)
      call.authority = *authority;
    call.owner = std::this_thread::get_id();
    call.source = bound.source;
    call.target = target;
    CeCareerRestoreRequest request = requestOf(bound);
    request.targetName = target;
    request.permitted = permittedHere;
    request.cancelled = nullptr; // no cancel is promised once this starts
    request.namePermitted = namedHere;
    request.owner = &call;
    {
      std::lock_guard<std::mutex> guard(s.lock);
      if (!mine(s) || s.op != op || s.phase != CE_RESTORE_WORKING ||
          !s.approved || s.publishing || !s.drawn)
        return;
      if (!allowed) {
        finish(s, CE_RESTORE_OUT_AUTHORITY, false);
        return;
      }
      if (!same) {
        finish(s, CE_RESTORE_OUT_CHANGED, false);
        return;
      }
      if (!compatible) {
        finish(s, CE_RESTORE_OUT_NAME, false);
        return;
      }
      s.publishing = true; // from here the call is made exactly once
    }
    CeCareerStorageResult result;
    bool threw = false;
    try {
      result = CeCareerRestoreAsNew(request);
    } catch (...) {
      threw = true;
    }
    CeRestoreOutcome kind = CE_RESTORE_OUT_UNCERTAIN;
    bool possible = true;
    std::string backup;
    if (!threw) {
      kind = kindOf(result.status);
      possible = kind == CE_RESTORE_OUT_COMPLETE ||
                 kind == CE_RESTORE_OUT_UNCERTAIN || !result.newFile.empty();
      if (possible && kind != CE_RESTORE_OUT_COMPLETE)
        kind = CE_RESTORE_OUT_UNCERTAIN;
      // Complete must name exactly the confirmed file.
      if (kind == CE_RESTORE_OUT_COMPLETE && result.newFile != target)
        kind = CE_RESTORE_OUT_UNCERTAIN;
      backup.swap(result.backupSet);
    }
    std::fprintf(stderr, PORT_LOG_PREFIX "restore as new: outcome %d\n",
                 int(kind));
    std::lock_guard<std::mutex> guard(s.lock);
    finish(s, kind, false, possible, std::move(backup));
  } catch (...) {
    // Nothing was started by this pass: the state is as it was.
  }
}

bool CeRestoreUiAcknowledge() {
  auto &s = state();
  std::lock_guard<std::mutex> guard(s.lock);
  if (!mine(s) || s.phase != CE_RESTORE_DONE)
    return false;
  reset(s);
  return true;
}

void CeRestoreUiRead(unsigned career, CeRestoreDisplay *out) {
  if (!out)
    return;
  std::memset(out, 0, sizeof(*out));
  out->phase = CE_RESTORE_IDLE;
  out->offer = CE_RESTORE_OFFER_NONE;
  out->outcome = CE_RESTORE_OUT_NONE;
  try {
    CeRestoreCard now;
    card(now);
    auto &s = state();
    std::lock_guard<std::mutex> guard(s.lock);
    if (!s.active)
      return;
    const CeRestoreOffer offer = offerOf(now);
    out->phase = s.phase;
    out->offer = offer;
    const char *message = "", *detail = "";
    words(s, offer, message, detail);
    backedUpWords(s, message, detail);
    std::snprintf(out->message, sizeof(out->message), "%s", message);
    std::snprintf(out->detail, sizeof(out->detail), "%s", detail);
    if (s.phase == CE_RESTORE_IDLE) {
      out->careers = now.verified ? int(now.careers.size()) : 0;
      if (now.verified && career < now.careers.size()) {
        out->career = int(career);
        out->complete = now.careers[career].complete;
        out->goodies = now.careers[career].goodies;
        put(out->source, sizeof(out->source), now.careers[career].name);
        put(out->date, sizeof(out->date), now.date);
        put(out->set, sizeof(out->set), leaf(now.set));
      }
      return;
    }
    const Bound &b = s.bound;
    out->careers = b.careers;
    out->career = int(b.career);
    out->complete = b.complete;
    out->goodies = b.goodies;
    put(out->source, sizeof(out->source), b.source);
    put(out->date, sizeof(out->date), b.date);
    put(out->set, sizeof(out->set), leaf(b.set));
    // A name is shown only once this thread approved it for this source.
    if (s.phase != CE_RESTORE_PROPOSING)
      put(out->target, sizeof(out->target), s.target);
    if (s.phase == CE_RESTORE_DONE) {
      out->outcome = s.outcome.kind;
      out->possible = s.outcome.possible ? 1 : 0;
      put(out->backup, sizeof(out->backup), s.outcome.backup);
      if (s.outcome.possible)
        put(out->saves, sizeof(out->saves), b.user + "/savegames");
    }
  } catch (...) {
    std::memset(out, 0, sizeof(*out));
  }
}
