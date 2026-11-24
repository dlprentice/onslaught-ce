// SPDX-License-Identifier: GPL-3.0-only
#include "../common/ce_backup_preview.h"
#include "../../game/ce_career_view.h"
#include "../common/ce_backup_verify.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
namespace {
struct Fd {
  int n = -1;
  explicit Fd(int fd = -1) : n(fd) {}
  ~Fd() {
    if (n >= 0)
      close(n);
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
};
bool same(const struct stat &a, const struct stat &b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
bool stamp(const struct stat &a, const struct stat &b) {
  return same(a, b) && a.st_size == b.st_size &&
         a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
         a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
         a.st_ctim.tv_sec == b.st_ctim.tv_sec &&
         a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
bool isOsSystemSymlink(int parentFd, const char *part) {
  struct stat pst{}, lst{};
  return fstat(parentFd, &pst) == 0 && pst.st_uid == 0 &&
         !(pst.st_mode & (S_IWGRP | S_IWOTH)) &&
         fstatat(parentFd, part, &lst, AT_SYMLINK_NOFOLLOW) == 0 &&
         S_ISLNK(lst.st_mode) && lst.st_uid == 0;
}
int directory(const std::string &p, bool allowUserAncestors = false) {
  if (p.empty() || p[0] != '/' || p.size() >= 4096 || p.find('\0') != p.npos)
    return -1;
  int at = open("/", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
  for (size_t pos = 1; at >= 0 && pos < p.size();) {
    size_t end = p.find('/', pos);
    const bool isLeaf = (end == p.npos);
    std::string part = p.substr(pos, end - pos);
    if (part.empty() || part == "." || part == "..") {
      close(at);
      return -1;
    }
    int child = openat(at, part.c_str(),
                       O_NOFOLLOW | O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (child < 0 && (errno == ELOOP || errno == ENOTDIR) && !isLeaf &&
        (allowUserAncestors || isOsSystemSymlink(at, part.c_str()))) {
      child = openat(at, part.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    }
    close(at);
    at = child;
    if (isLeaf)
      break;
    pos = end + 1;
  }
  return at;
}
bool named(int f, const std::string &p, bool allowUserAncestors = false) {
  Fd now(directory(p, allowUserAncestors));
  struct stat a{}, b{};
  return now.n >= 0 && !fstat(now.n, &a) && !fstat(f, &b) && same(a, b);
}
bool outside(int f, int protectedFd) {
  struct stat root{}, cur{}, up{};
  if (fstat(protectedFd, &root))
    return false;
  Fd at(dup(f));
  for (int i = 0; i < 256 && at.n >= 0; ++i) {
    if (fstat(at.n, &cur) || same(cur, root))
      return false;
    Fd parent(
        openat(at.n, "..", O_DIRECTORY | O_NOFOLLOW | O_RDONLY | O_CLOEXEC));
    if (parent.n < 0 || fstat(parent.n, &up))
      return false;
    if (same(cur, up))
      return true;
    close(at.n);
    at.n = dup(parent.n);
  }
  return false;
}
struct File {
  Fd fd;
  std::string leaf, data;
  struct stat initial{};
  bool openRead(int parent, const std::string &name, size_t limit) {
    leaf = name;
    fd.n = openat(parent, leaf.c_str(),
                  O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd.n < 0 || fstat(fd.n, &initial) || !S_ISREG(initial.st_mode) ||
        initial.st_nlink != 1 || initial.st_size < 0 ||
        uint64_t(initial.st_size) > limit)
      return false;
    data.resize(size_t(initial.st_size));
    size_t got = 0;
    while (got < data.size()) {
      ssize_t n = read(fd.n, &data[got], data.size() - got);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        return false;
      got += size_t(n);
    }
    char more;
    return read(fd.n, &more, 1) == 0 && unchanged(parent);
  }
  bool sameObject(const File &other) const { return same(initial, other.initial); }
  bool unchanged(int parent) const {
    struct stat a{}, b{};
    return !fstat(fd.n, &a) &&
           !fstatat(parent, leaf.c_str(), &b, AT_SYMLINK_NOFOLLOW) &&
           stamp(initial, a) && stamp(initial, b);
  }
};
bool slotNames(int root, std::set<std::string> &out) {
  Fd dir(openat(root, "savegames",
                O_DIRECTORY | O_NOFOLLOW | O_RDONLY | O_CLOEXEC));
  if (dir.n < 0)
    return errno == ENOENT;
  DIR *dp = fdopendir(dup(dir.n));
  if (!dp)
    return false;
  size_t count = 0;
  int err;
  for (;;) {
    errno = 0;
    dirent *each = readdir(dp);
    if (!each) {
      err = errno;
      break;
    }
    if (++count > 8192) {
      err = EOVERFLOW;
      break;
    }
    out.insert(ce_preview::fold(each->d_name));
  }
  closedir(dp);
  return !err;
}
std::string proposal(std::set<std::string> &taken) {
  for (int i = 1; i <= 999; ++i) {
    std::string n = i == 1 ? "Backup Copy.bes"
                           : "Backup Copy " + std::to_string(i) + ".bes";
    if (taken.insert(ce_preview::fold(n)).second)
      return n;
  }
  return {};
}
} // namespace
bool CePreviewRead(const std::string &set, const std::string &user,
                   const std::string &game, const std::string &package,
                   CePreviewSet &out, std::string &error) {
  out = {};
  error = "Choose a backup folder outside the game and careers.";
  Fd folder(directory(set, false)), u(directory(user, true)), g(directory(game, true)),
      pkg(directory(package, true));
  if (folder.n < 0 || u.n < 0 || g.n < 0 || pkg.n < 0 ||
      !outside(folder.n, u.n) || !outside(folder.n, g.n) ||
      !outside(folder.n, pkg.n))
    return false;
  std::set<std::string> used;
  bool scanned = false, known = false;
  auto suggest = [&]() -> std::string {
    if (!scanned) {
      scanned = true;
      known = slotNames(u.n, used) && slotNames(g.n, used);
    }
    return known ? proposal(used) : std::string();
  };
  auto current = [&]() {
    return named(folder.n, set) && named(u.n, user, true) && named(g.n, game, true) &&
           named(pkg.n, package, true);
  };
  return ce_backup_verify::read<File>(folder.n, suggest, current, out, error);
}

bool CePreviewList(const std::string &root, const std::string &user,
                   const std::string &game, const std::string &package,
                   std::vector<std::string> &sets, std::string &error) {
  sets.clear();
  error = "The configured backup folder could not be checked.";
  Fd dir(directory(root, false)), u(directory(user, true)), g(directory(game, true)),
      pkg(directory(package, true));
  if (dir.n < 0 || u.n < 0 || g.n < 0 || pkg.n < 0 || !outside(dir.n, u.n) ||
      !outside(dir.n, g.n) || !outside(dir.n, pkg.n))
    return false;
  DIR *stream = fdopendir(dup(dir.n));
  if (!stream)
    return false;
  size_t entries = 0;
  int err = 0;
  std::vector<std::string> candidates;
  for (;;) {
    errno = 0;
    dirent *e = readdir(stream);
    if (!e) {
      err = errno;
      break;
    }
    if (++entries > 4096) {
      err = E2BIG;
      break;
    }
    std::string leaf = e->d_name;
    if (leaf.compare(0, sizeof("onslaught-backup-") - 1, "onslaught-backup-") ||
        !ce_preview::safeName(leaf))
      continue;
    Fd child(openat(dir.n, leaf.c_str(),
                    O_DIRECTORY | O_NOFOLLOW | O_RDONLY | O_CLOEXEC));
    struct stat m{};
    if (child.n >= 0 &&
        !fstatat(child.n, "onslaught-backup.json", &m, AT_SYMLINK_NOFOLLOW) &&
        S_ISREG(m.st_mode) && m.st_nlink == 1)
      candidates.push_back(leaf);
  }
  closedir(stream);
  if (err || !named(dir.n, root))
    return false;
  std::sort(candidates.rbegin(), candidates.rend());
  if (candidates.size() > 64)
    candidates.resize(64);
  sets = std::move(candidates);
  error.clear();
  return true;
}
