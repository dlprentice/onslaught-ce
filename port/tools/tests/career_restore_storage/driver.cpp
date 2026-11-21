// Calls the production APIs; all syscalls are real except named fault controls.
#include "../../../platform/common/ce_backup_preview.h"
#include "../../../platform/common/ce_career_restore.h"
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using Clock = std::chrono::steady_clock;
static unsigned long long elapsed(Clock::time_point start) {
  return static_cast<unsigned long long>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                            start)
          .count());
}
static std::string mode, base;
static int staged = -1, targetDir = -1;
static bool linked = false, paused = false;
static unsigned stageWrites = 0;
// Full-set cuts: armed only for the operation under test. The staging folder,
// each file created in it (members in name order, then the manifest) and the
// backup root are recognised by the calls the store makes on them.
static bool armed = false, renamed = false, cut = false;
static int stagingDir = -1, rootDir = -1, manifestFd = -1, createdFd[16] = {};
static unsigned created = 0;
static std::string madeDir;
// Reads of a manifest during the operation: 1 the staged check, 2 the read-back,
// 3 and 4 the stamp; the take call's own re-bind is the fifth.
static unsigned manifestReads = 0;
static bool cutting(const char *name) { return armed && !cut && mode == name; }
static int createdIndex(int fd) {
  // The latest file opened on this number: a closed member's number is reused.
  for (unsigned i = created; i-- > 0;)
    if (createdFd[i] == fd)
      return int(i) + 1;
  return 0;
}
static bool memberCut(int fd) {
  // cut_member_K: the K-th file created in the staging folder.
  if (!armed || cut || mode.compare(0, 11, "cut_member_") || fd < 0)
    return false;
  const int k = std::atoi(mode.c_str() + 11);
  return k > 0 && createdIndex(fd) == k;
}
static std::string readText(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
}
static bool permit(void *) {
  return mode != "no_authority" &&
         access((base + "/revoke").c_str(), F_OK) != 0;
}
static bool cancelled(void *) {
  return mode == "cancel" || access((base + "/cancel").c_str(), F_OK) == 0;
}
static bool namePermit(void *, const std::string &source,
                       const std::string &target) {
  if (mode == "no_name_approval" ||
      access((base + "/deny-name").c_str(), F_OK) == 0)
    return false;
  if (mode == "immutable_pair")
    return source == "Amber.bes" && target == "Amber restored.bes";
  if (mode == "protected_tail")
    return (source.find("KEEP") != std::string::npos) ==
           (target.find("KEEP") != std::string::npos);
  return true;
}
static void notice(const char *point, const char *name = "") {
  if (paused)
    return;
  const char *value = std::getenv("CE_STORAGE_TEST_CONTROL_FD");
  if (!value)
    std::exit(91);
  paused = true;
  std::ofstream f(base + "/notice", std::ios::binary);
  f << point << '\n' << name << '\n';
  f.close();
  int fd = std::atoi(value);
  pollfd p{fd, POLLIN, 0};
  char c = 0;
  if (poll(&p, 1, 15000) != 1 || ::read(fd, &c, 1) != 1 || c != 'c')
    std::exit(92);
}
extern "C" {
int __real_openat(int, const char *, int, ...);
ssize_t __real_write(int, const void *, size_t);
ssize_t __real_pread(int, void *, size_t, off_t);
int __real_fsync(int);
int __real_linkat(int, const char *, int, const char *, int);
ssize_t __real_getrandom(void *, size_t, unsigned);
int __real_renameat2(int, const char *, int, const char *, unsigned);
int __real_mkdirat(int, const char *, mode_t);
int __wrap_mkdirat(int dir, const char *path, mode_t perms) {
  if (cutting("cut_staging")) {
    cut = true;
    errno = EIO;
    return -1;
  }
  const int rc = __real_mkdirat(dir, path, perms);
  if (rc == 0 && armed && madeDir.empty())
    madeDir = path;
  return rc;
}
int __wrap_openat(int dir, const char *path, int flags, ...) {
  mode_t perms = 0;
  if (flags & O_CREAT || (flags & O_TMPFILE) == O_TMPFILE) {
    va_list ap;
    va_start(ap, flags);
    perms = va_arg(ap, int);
    va_end(ap);
  }
  if ((flags & O_TMPFILE) == O_TMPFILE && mode == "unsupported") {
    errno = EOPNOTSUPP;
    return -1;
  }
  const bool manifestRead = armed && (flags & O_ACCMODE) == O_RDONLY &&
                            !(flags & O_CREAT) &&
                            std::strcmp(path, "onslaught-backup.json") == 0;
  if (manifestRead && ++manifestReads == 5 && cutting("cut_rebind")) {
    cut = true;
    errno = EIO;
    return -1;
  }
  if (manifestRead &&
      (cutting(renamed ? "cut_readback" : "cut_staged_verify"))) {
    cut = true;
    errno = EIO;
    return -1;
  }
  int fd = __real_openat(dir, path, flags, perms);
  if (armed && fd >= 0 && (flags & O_DIRECTORY) && stagingDir < 0 &&
      !madeDir.empty() && madeDir == path) {
    stagingDir = fd;
    rootDir = dir;
  }
  if (armed && fd >= 0 && dir == stagingDir && (flags & O_CREAT) &&
      (flags & O_EXCL) && created < 16) {
    createdFd[created++] = fd;
    if (std::strcmp(path, "onslaught-backup.json") == 0)
      manifestFd = fd;
  }
  if ((flags & O_TMPFILE) == O_TMPFILE && fd >= 0) {
    staged = fd;
    if (mode == "kill_before_stage")
      notice("before_stage");
  }
  if (linked && mode == "kill_before_readback" && dir == targetDir && fd >= 0 &&
      (flags & O_ACCMODE) == O_RDONLY)
    notice("before_readback", path);
  return fd;
}
ssize_t __wrap_write(int fd, const void *p, size_t n) {
  if ((memberCut(fd) && fd != manifestFd) ||
      (cutting("cut_manifest") && fd >= 0 && fd == manifestFd)) {
    // Half the file reaches the disk, then the write fails.
    cut = true;
    __real_write(fd, p, n / 2);
    errno = EIO;
    return -1;
  }
  if (fd == staged) {
    if (mode == "no_space") {
      errno = ENOSPC;
      return -1;
    }
    if (mode == "kill_partial_stage" && stageWrites++ == 0) {
      ssize_t wrote = __real_write(fd, p, n / 2);
      notice("partial_stage");
      return wrote;
    }
  }
  return __real_write(fd, p, n);
}
ssize_t __wrap_pread(int fd, void *p, size_t n, off_t offset) {
  if (mode == "stage_readback" && fd == staged)
    return 0;
  if (mode == "final_readback" && linked)
    return 0;
  return __real_pread(fd, p, n, offset);
}
int __wrap_fsync(int fd) {
  if ((cutting("cut_member_fsync") && createdIndex(fd) == 1) ||
      (cutting("cut_set_fsync") && fd == stagingDir) ||
      (cutting("cut_root_fsync") && renamed && fd == rootDir)) {
    cut = true;
    errno = EIO;
    return -1;
  }
  if (mode == "stage_flush" && fd == staged) {
    errno = EIO;
    return -1;
  }
  if (mode == "final_flush" && linked && fd == targetDir) {
    errno = EIO;
    return -1;
  }
  int rc = __real_fsync(fd);
  if (mode == "kill_after_stage" && fd == staged && rc == 0)
    notice("after_stage");
  return rc;
}
int __wrap_linkat(int olddir, const char *oldname, int newdir,
                  const char *newname, int flags) {
  targetDir = newdir;
  if (mode == "before_link" || mode == "kill_before_link" || mode == "race")
    notice("before_link", newname);
  int rc = __real_linkat(olddir, oldname, newdir, newname, flags);
  linked = rc == 0;
  if (linked && (mode == "after_link" || mode == "kill_after_link"))
    notice("after_link", newname);
  return rc;
}
ssize_t __wrap_getrandom(void *p, size_t n, unsigned flags) {
  if (mode == "fixed_name") {
    std::memset(p, 7, n);
    return n;
  }
  return __real_getrandom(p, n, flags);
}
int __wrap_renameat2(int od, const char *on, int nd, const char *nn,
                     unsigned flags) {
  if (mode == "backup_publish") {
    errno = EIO;
    return -1;
  }
  if (mode == "kill_before_backup" || mode == "collide_before_rename")
    notice("before_backup", nn);
  int rc = __real_renameat2(od, on, nd, nn, flags);
  if (rc == 0 && armed)
    renamed = true;
  if (rc == 0 && (mode == "after_backup" || mode == "kill_after_backup"))
    notice("after_backup", nn);
  return rc;
}
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 80;
  base = argv[1];
  mode = argv[2];
  if (readText(base + "/AUTHORED") != "disposable storage fixture\n")
    return 81;
  CeCareerRestoreRequest r;
  r.user = base + "/user";
  r.game = base + "/game";
  r.package = base + "/package";
  r.backupRoot = base + "/external";
  r.sourceSet = readText(base + "/source-set");
  if (r.sourceSet.empty())
    r.sourceSet = base + "/source/onslaught-backup-20260405-123000";
  r.sourceName = readText(base + "/source-name");
  if (r.sourceName.empty())
    r.sourceName = "Amber.bes";
  const auto stampStart = Clock::now();
  CePreviewSet preview;
  std::string error;
  if (!CePreviewRead(r.sourceSet, r.user, r.game, r.package, preview, error))
    return 82;
  r.sourceIdentity = preview.identity;
  if (!CeCareerRestoreStamp(r, r.sourceStamp))
    return 83;
  const auto stampTime = elapsed(stampStart);
  if (mode == "no_stamp")
    r.sourceStamp.clear();
  if (mode == "stale_preview")
    r.sourceIdentity = std::string(64, 'f');
  r.permitted = permit;
  r.cancelled = cancelled;
  r.namePermitted = namePermit;
  if (mode == "no_callback")
    r.permitted = nullptr;
  if (mode == "no_name_callback")
    r.namePermitted = nullptr;
  if (std::strcmp(argv[3], "list") == 0) {
    // What Backup Preview's reader lists in the backup folder, and whether
    // each listed set verifies: "name identity" or "name -".
    std::vector<std::string> names;
    const bool ok = CePreviewList(r.backupRoot, r.user, r.game, r.package,
                                  names, error);
    std::printf("%d\n", ok ? 1 : 0);
    for (const auto &name : names) {
      CePreviewSet set;
      std::string why;
      const bool good = CePreviewRead(r.backupRoot + "/" + name, r.user,
                                      r.game, r.package, set, why);
      std::printf("%s %s %d %zu %s\n", name.c_str(),
                  good ? set.identity.c_str() : "-", good ? set.members : 0,
                  good ? set.careers.size() : size_t(0),
                  good ? set.reason.c_str() : "-");
    }
    return 0;
  }
  if (std::strcmp(argv[3], "suggest") == 0) {
    std::string name;
    const auto suggestedAt = Clock::now();
    const auto state = CeCareerRestoreSuggest(r, name);
    std::printf("%d\n\n\n%s\n%llu %llu 0\n", int(state), name.c_str(),
                stampTime, elapsed(suggestedAt));
    return 0;
  }
  unsigned long long suggestTime = 0;
  r.targetName = readText(base + "/confirmed-name");
  if (r.targetName.empty()) {
    std::string name;
    r.targetName = "Amber restored.bes";
    const auto proposedAt = Clock::now();
    if (CeCareerRestoreSuggest(r, name) == CeCareerStorageStatus::Complete)
      r.targetName = name;
    suggestTime = elapsed(proposedAt);
  }
  const auto operationAt = Clock::now();
  armed = true;
  const bool take = std::strcmp(argv[3], "take") == 0;
  CeCareerStorageResult out = std::strcmp(argv[3], "backup") == 0
                                  ? CeCareerBackupAll(r)
                              : take ? CeCareerTakeFullSet(r)
                                     : CeCareerRestoreAsNew(r);
  armed = false;
  // A cut that was asked for and never reached proves nothing.
  if (mode.compare(0, 4, "cut_") == 0 && !cut)
    return 93;
  // take: the fourth line is the stamp (no career file is made).
  if (take)
    out.newFile = out.backupStamp;
  // Private driver protocol only; never print source bytes.
  std::printf("%d\n%s\n%s\n%s\n%llu %llu %llu\n", int(out.status),
              out.backupSet.c_str(), out.backupIdentity.c_str(),
              out.newFile.c_str(), stampTime, suggestTime,
              elapsed(operationAt));
}
