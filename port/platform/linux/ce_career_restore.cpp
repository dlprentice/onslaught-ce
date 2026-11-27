#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../common/ce_career_restore.h"
#include "../common/ce_backup_preview.h"
#include "../common/ce_backup_verify.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>

namespace {
using Status = CeCareerStorageStatus;
struct Failure {
  Status status;
};
void require(bool ok, Status status = Status::Refused) {
  if (!ok)
    throw Failure{status};
}
struct Fd {
  int n = -1;
  explicit Fd(int f = -1) : n(f) {}
  ~Fd() {
    if (n >= 0)
      close(n);
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
};
using Bytes = std::vector<unsigned char>;
constexpr size_t kMemberLimit = 2u * 1024u * 1024u;
constexpr size_t kSetLimit = 64u * 1024u * 1024u;
constexpr size_t kCountLimit = 512;
constexpr size_t kDirectoryEntryLimit = 2048;
const char *const kOptions = "defaultoptions.bea";
// The manifest's Reason, which Backup Preview reads to say what a set holds.
// Older sets keep theirs: "Manual" is the set Restore as New makes first.
const char *const kFullSetReason = "All careers";
const char *const kRestoreReason = "Manual";

bool same(const struct stat &a, const struct stat &b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
bool stamp(const struct stat &a, const struct stat &b) {
  return same(a, b) && a.st_size == b.st_size && a.st_nlink == b.st_nlink &&
         a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
         a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
         a.st_ctim.tv_sec == b.st_ctim.tv_sec &&
         a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
struct stat status(int fd) {
  struct stat s{};
  require(fstat(fd, &s) == 0);
  return s;
}
std::string lower(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return s;
}
bool career(const std::string &s) {
  return s.size() > 4 && lower(s.substr(s.size() - 4)) == ".bes";
}
bool portable(const std::string &s) {
  if (!career(s) || s.size() >= 256 || s.front() == ' ' ||
      s[s.size() - 5] == ' ' || s[s.size() - 5] == '.' ||
      CeBackupFileHasDirectionMark(s.data(), s.size()))
    return false;
  const size_t stemLen = s.size() - 4;
  size_t units = 0;
  std::vector<unsigned> codepoints;
  for (size_t i = 0; i < stemLen;) {
    unsigned char b0 = static_cast<unsigned char>(s[i++]);
    unsigned cp = 0;
    if (b0 < 0x80) {
      cp = b0;
      if (cp < 32 || cp == 127 || std::strchr("<>:\"/\\|?*", char(cp)))
        return false;
    } else {
      unsigned extra = 0, minCp = 0;
      if (b0 >= 0xc2 && b0 <= 0xdf) {
        cp = b0 & 0x1fu;
        extra = 1;
        minCp = 0x80;
      } else if (b0 >= 0xe0 && b0 <= 0xef) {
        cp = b0 & 0x0fu;
        extra = 2;
        minCp = 0x800;
      } else if (b0 >= 0xf0 && b0 <= 0xf4) {
        cp = b0 & 0x07u;
        extra = 3;
        minCp = 0x10000;
      } else
        return false;
      if (extra > stemLen - i)
        return false;
      while (extra--) {
        unsigned char bx = static_cast<unsigned char>(s[i++]);
        if ((bx & 0xc0u) != 0x80u)
          return false;
        cp = (cp << 6) | (bx & 0x3fu);
      }
      if (cp < minCp || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu) ||
          (cp >= 0x80u && cp <= 0x9fu) || CeBackupDirectionMark(cp))
        return false;
    }
    units += cp >= 0x10000u ? 2u : 1u;
    if (units > 31u)
      return false;
    codepoints.push_back(cp);
  }
  if (codepoints.empty())
    return false;
  size_t headEnd = 0;
  while (headEnd < codepoints.size() && codepoints[headEnd] != '.')
    ++headEnd;
  while (headEnd > 0 && codepoints[headEnd - 1] == ' ')
    --headEnd;
  std::string head;
  for (size_t i = 0; i < headEnd; ++i) {
    unsigned cp = codepoints[i];
    if (cp >= 'A' && cp <= 'Z')
      head.push_back(char(cp + ('a' - 'A')));
    else if (cp < 0x80u)
      head.push_back(char(cp));
    else
      head.push_back('!');
  }
  if (head == "con" || head == "prn" || head == "aux" || head == "nul" ||
      head == "conin$" || head == "conout$")
    return false;
  if (headEnd == 4 &&
      (head.compare(0, 3, "com") == 0 || head.compare(0, 3, "lpt") == 0)) {
    unsigned d = codepoints[3];
    if ((d >= '0' && d <= '9') || d == 0xb9u || d == 0xb2u || d == 0xb3u)
      return false;
  }
  return true;
}
bool isOsSystemSymlink(int parentFd, const char *part) {
  struct stat pst{}, lst{};
  return fstat(parentFd, &pst) == 0 && pst.st_uid == 0 &&
         !(pst.st_mode & (S_IWGRP | S_IWOTH)) &&
         fstatat(parentFd, part, &lst, AT_SYMLINK_NOFOLLOW) == 0 &&
         S_ISLNK(lst.st_mode) && lst.st_uid == 0;
}
int directory(const std::string &path, bool allowUserAncestors = false) {
  require(path.size() > 1 && path[0] == '/' && path.back() != '/');
  Fd at(open("/", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
  require(at.n >= 0);
  size_t begin = 1;
  while (begin < path.size()) {
    size_t end = path.find('/', begin);
    const bool isLeaf = (end == std::string::npos);
    if (isLeaf)
      end = path.size();
    std::string part = path.substr(begin, end - begin);
    require(!part.empty() && part != "." && part != ".." &&
            part.find('\0') == std::string::npos);
    int next = openat(at.n, part.c_str(),
                      O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (next < 0 && (errno == ELOOP || errno == ENOTDIR) && !isLeaf &&
        (allowUserAncestors || isOsSystemSymlink(at.n, part.c_str()))) {
      next = openat(at.n, part.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    }
    require(next >= 0);
    close(at.n);
    at.n = next;
    begin = end + 1;
  }
  int out = at.n;
  at.n = -1;
  return out;
}
bool local(int fd) {
  struct statfs fs{};
  const unsigned long t = fstatfs(fd, &fs) == 0 ? static_cast<unsigned long>(fs.f_type) : 0UL;
  return t == 0x9123683eUL || // btrfs
         t == 0xef53UL ||     // ext2/ext3/ext4
         t == 0x58465342UL || // xfs
         t == 0xF2F52010UL || // f2fs
         t == 0xca451a4eUL;   // bcachefs
}
bool ancestor(int a, int b) {
  struct stat sought = status(a);
  Fd cur(openat(b, ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
  require(cur.n >= 0);
  for (int count = 0; count < 512; ++count) {
    struct stat here = status(cur.n);
    if (same(sought, here))
      return true;
    Fd up(openat(cur.n, "..", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
    require(up.n >= 0);
    if (same(here, status(up.n)))
      return false;
    close(cur.n);
    cur.n = up.n;
    up.n = -1;
  }
  throw Failure{Status::Refused};
}
void bound(int fd, const std::string &name, bool allowUserAncestors = false) {
  Fd again(directory(name, allowUserAncestors));
  require(same(status(fd), status(again.n)), Status::Changed);
}
bool savegamesAbsent(int parentFd) {
  struct stat st{};
  return fstatat(parentFd, "savegames", &st, AT_SYMLINK_NOFOLLOW) != 0 &&
         errno == ENOENT;
}
int openOptionalSavegames(int parentFd, bool &present) {
  int fd = openat(parentFd, "savegames",
                  O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
  if (fd >= 0) {
    require(S_ISDIR(status(fd).st_mode));
    present = true;
    return fd;
  }
  require(errno == ENOENT && savegamesAbsent(parentFd));
  present = false;
  return -1;
}
std::string digest(const Bytes &bytes) {
  return ce_backup_verify::digest(std::string(bytes.begin(), bytes.end()));
}
std::string quoted(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
    } else if (c < 32) {
      char temp[7];
      std::snprintf(temp, sizeof temp, "\\u%04x", unsigned(c));
      out += temp;
    } else
      out += char(c);
  }
  return out + '"';
}
std::string digits(size_t count) {
  std::string out;
  while (out.size() < count) {
    unsigned char b[64];
    ssize_t n = getrandom(b, sizeof b, 0);
    if (n < 0 && errno == EINTR)
      continue;
    require(n > 0);
    for (ssize_t i = 0; i < n && out.size() < count; ++i)
      if (b[i] < 250)
        out += char('0' + b[i] % 10);
  }
  return out;
}
Bytes read(int parent, const std::string &name, struct stat &observed) {
  Fd f(openat(parent, name.c_str(),
              O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  require(f.n >= 0);
  observed = status(f.n);
  require(S_ISREG(observed.st_mode) && observed.st_nlink == 1 &&
          observed.st_size >= 0 &&
          static_cast<unsigned long long>(observed.st_size) <= kMemberLimit);
  Bytes result(static_cast<size_t>(observed.st_size));
  size_t done = 0;
  while (done < result.size()) {
    ssize_t n = ::read(f.n, result.data() + done, result.size() - done);
    if (n < 0 && errno == EINTR)
      continue;
    require(n > 0);
    done += size_t(n);
  }
  unsigned char tail;
  require(::read(f.n, &tail, 1) == 0 && stamp(observed, status(f.n)),
          Status::Changed);
  struct stat byName{};
  require(fstatat(parent, name.c_str(), &byName, AT_SYMLINK_NOFOLLOW) == 0 &&
              stamp(observed, byName),
          Status::Changed);
  return result;
}
void write(int fd, const Bytes &data) {
  size_t done = 0;
  while (done < data.size()) {
    ssize_t n = ::write(fd, data.data() + done, data.size() - done);
    if (n < 0 && errno == EINTR)
      continue;
    require(n > 0);
    done += size_t(n);
  }
  require(fsync(fd) == 0);
}
void identical(int fd, const Bytes &data) {
  require(static_cast<unsigned long long>(status(fd).st_size) == data.size());
  Bytes bytes(data.size());
  size_t done = 0;
  while (done < bytes.size()) {
    ssize_t n = pread(fd, bytes.data() + done, bytes.size() - done, done);
    if (n < 0 && errno == EINTR)
      continue;
    require(n > 0);
    done += size_t(n);
  }
  require(bytes == data);
}
struct Member {
  std::string name;
  bool user;
  struct stat identity{};
  Bytes bytes;
};
struct SkippedMember {
  std::string name;
  bool user;
  struct stat identity{};
};
struct Snapshot {
  std::vector<Member> all;
  std::map<std::string, size_t> visible;
  std::vector<SkippedMember> skipped;
};
bool hasCareers(const Snapshot &s) {
  for (const auto &it : s.visible)
    if (career(it.first))
      return true;
  return false;
}
void logSkippedSave(bool fromUser, const std::string &name,
                    const char *reason) {
  std::fprintf(
      stderr,
      "onslaught-ce: career storage skipped unreadable %s save \"%s\" (%s)\n",
      fromUser ? "user" : "game", name.c_str(), reason);
}
bool equal(const Snapshot &a, const Snapshot &b) {
  if (a.all.size() != b.all.size() || a.visible != b.visible ||
      a.skipped.size() != b.skipped.size())
    return false;
  for (size_t i = 0; i < a.all.size(); ++i)
    if (a.all[i].name != b.all[i].name || a.all[i].user != b.all[i].user ||
        !stamp(a.all[i].identity, b.all[i].identity) ||
        a.all[i].bytes != b.all[i].bytes)
      return false;
  for (size_t i = 0; i < a.skipped.size(); ++i)
    if (a.skipped[i].name != b.skipped[i].name ||
        a.skipped[i].user != b.skipped[i].user ||
        !stamp(a.skipped[i].identity, b.skipped[i].identity))
      return false;
  return true;
}
struct Session {
  const CeCareerStorageRequest &r;
  bool slotsPresent = false, originalPresent = false;
  Fd user, game, package, root, slots, original, lock;
  Snapshot saved;
  explicit Session(const CeCareerStorageRequest &in)
      : r(in), user(directory(r.user, true)), game(directory(r.game, true)),
        package(directory(r.package, true)), root(directory(r.backupRoot, false)),
        slots(openOptionalSavegames(user.n, slotsPresent)),
        original(openOptionalSavegames(game.n, originalPresent)) {
    permission();
    require(local(user.n) && local(game.n) && local(root.n) &&
                (!slotsPresent || local(slots.n)) &&
                (!originalPresent || local(original.n)),
            Status::Unsupported);
    for (int protectedDir : {user.n, game.n, package.n})
      require(!ancestor(protectedDir, root.n) &&
              !ancestor(root.n, protectedDir));
    require(!ancestor(user.n, game.n) && !ancestor(game.n, user.n));
    lock.n =
        openat(user.n, ".ce-career-custody.lock",
               O_RDWR | O_CLOEXEC | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600);
    require(lock.n >= 0);
    struct stat ls = status(lock.n);
    require(S_ISREG(ls.st_mode) && ls.st_nlink == 1 && ls.st_uid == geteuid());
    if (flock(lock.n, LOCK_EX | LOCK_NB))
      throw Failure{errno == EWOULDBLOCK ? Status::Busy : Status::Refused};
    roots();
    saved = snapshot("", true);
  }
  void permission(bool includeCancel = true) const {
    require(r.permitted && r.permitted(r.owner));
    require(!(includeCancel && r.cancelled && r.cancelled(r.owner)),
            Status::Cancelled);
  }
  void roots() const {
    bound(user.n, r.user, true);
    bound(game.n, r.game, true);
    bound(package.n, r.package, true);
    bound(root.n, r.backupRoot, false);
    if (slotsPresent) {
      Fd a(openat(user.n, "savegames",
                  O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
      require(a.n >= 0 && same(status(a.n), status(slots.n)), Status::Changed);
    } else
      require(savegamesAbsent(user.n), Status::Changed);
    if (originalPresent) {
      Fd b(openat(game.n, "savegames",
                  O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
      require(b.n >= 0 && same(status(b.n), status(original.n)),
              Status::Changed);
    } else
      require(savegamesAbsent(game.n), Status::Changed);
    struct stat held{};
    require(fstatat(user.n, ".ce-career-custody.lock", &held,
                    AT_SYMLINK_NOFOLLOW) == 0 &&
                same(held, status(lock.n)) && held.st_nlink == 1,
            Status::Changed);
  }
  void ensureUserSavegames() {
    if (slotsPresent)
      return;
    roots();
    if (mkdirat(user.n, "savegames", 0700) != 0)
      throw Failure{errno == EEXIST ? Status::Changed : Status::Refused};
    Fd created(openat(user.n, "savegames",
                      O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
    require(created.n >= 0, Status::Changed);
    const struct stat createdStat = status(created.n);
    struct stat byName{};
    require(S_ISDIR(createdStat.st_mode) &&
                fstatat(user.n, "savegames", &byName, AT_SYMLINK_NOFOLLOW) ==
                    0 &&
                same(createdStat, byName),
            Status::Changed);
    require(local(created.n), Status::Unsupported);
    require(fsync(user.n) == 0);
    slots.n = created.n;
    created.n = -1;
    slotsPresent = true;
  }
  Snapshot snapshot(const std::string &exclude = "",
                    bool logSkipped = false) const {
    Snapshot s;
    size_t total = 0;
    std::map<std::string, std::string> spelling;
    for (bool fromUser : {true, false}) {
      if (!(fromUser ? slotsPresent : originalPresent)) {
        require(savegamesAbsent(fromUser ? user.n : game.n), Status::Changed);
        continue;
      }
      int parent = fromUser ? slots.n : original.n;
      Fd copy(openat(parent, ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
      require(copy.n >= 0);
      DIR *raw = fdopendir(copy.n);
      require(raw != nullptr);
      copy.n = -1;
      std::unique_ptr<DIR, int (*)(DIR *)> stream(raw, closedir);
      std::set<std::string> names;
      errno = 0;
      size_t scanned = 0;
      while (dirent *ent = readdir(raw)) {
        require(++scanned <= kDirectoryEntryLimit);
        std::string name = ent->d_name;
        if (career(name))
          names.insert(name);
      }
      require(errno == 0);
      for (const std::string &name : names) {
        if (fromUser && name == exclude)
          continue;
        Fd f(openat(parent, name.c_str(),
                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        require(f.n >= 0);
        const struct stat obs = status(f.n);
        require(S_ISREG(obs.st_mode) && obs.st_nlink == 1 && obs.st_size >= 0);
        struct stat byName{};
        require(fstatat(parent, name.c_str(), &byName, AT_SYMLINK_NOFOLLOW) ==
                    0 &&
                    stamp(obs, byName),
                Status::Changed);
        if (!portable(name)) {
          if (logSkipped)
            logSkippedSave(fromUser, name, "non-portable or overlong filename");
          s.skipped.push_back(SkippedMember{name, fromUser, obs});
          continue;
        }
        const std::string key = lower(name);
        auto was = spelling.find(key);
        require(was == spelling.end() || (was->second == name && !fromUser));
        spelling[key] = name;
        if (static_cast<unsigned long long>(obs.st_size) != CE_CAREER_SIZE) {
          if (logSkipped)
            logSkippedSave(fromUser, name,
                           obs.st_size == 0 ? "empty 0-byte file"
                                            : "wrong file size");
          s.skipped.push_back(SkippedMember{name, fromUser, obs});
          continue;
        }
        Member m;
        m.name = name;
        m.user = fromUser;
        m.identity = obs;
        m.bytes.resize(CE_CAREER_SIZE);
        size_t done = 0;
        while (done < m.bytes.size()) {
          ssize_t n = ::read(f.n, m.bytes.data() + done, m.bytes.size() - done);
          if (n < 0 && errno == EINTR)
            continue;
          require(n > 0);
          done += size_t(n);
        }
        unsigned char tail;
        require(::read(f.n, &tail, 1) == 0 && stamp(m.identity, status(f.n)),
                Status::Changed);
        require(fstatat(parent, name.c_str(), &byName, AT_SYMLINK_NOFOLLOW) ==
                    0 &&
                    stamp(m.identity, byName),
                Status::Changed);
        SCeCareerView view{};
        if (!CeCareerViewBuild(m.bytes.data(), int(m.bytes.size()), &view) ||
            !CeCareerViewPlausible(view)) {
          if (logSkipped)
            logSkippedSave(fromUser, name, "corrupt or unreadable career data");
          s.skipped.push_back(SkippedMember{name, fromUser, m.identity});
          continue;
        }
        total += m.bytes.size();
        require(s.all.size() < kCountLimit && total <= kSetLimit);
        if (!s.visible.count(name))
          s.visible[name] = s.all.size();
        s.all.push_back(std::move(m));
      }
    }
    {
      Fd userCopy(openat(user.n, ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
      require(userCopy.n >= 0);
      DIR *rawUser = fdopendir(userCopy.n);
      require(rawUser != nullptr);
      userCopy.n = -1;
      std::unique_ptr<DIR, int (*)(DIR *)> userStream(rawUser, closedir);
      std::string optName;
      size_t userScanned = 0;
      errno = 0;
      while (dirent *ent = readdir(rawUser)) {
        require(++userScanned <= kDirectoryEntryLimit);
        std::string name = ent->d_name;
        if (lower(name) == kOptions) {
          require(optName.empty()); // refuse ambiguous case-duplicate DefaultOptions.bea files
          optName = std::move(name);
        }
      }
      require(errno == 0);
      if (!optName.empty()) {
        Member m;
        m.name = optName;
        m.user = true;
        m.bytes = read(user.n, optName, m.identity);
        total += m.bytes.size();
        require(s.all.size() < kCountLimit && total <= kSetLimit);
        s.visible[m.name] = s.all.size();
        s.all.push_back(std::move(m));
      }
    }
    return s;
  }
  void check(const std::string &exclude = "", bool allowCancel = true) const {
    permission(allowCancel);
    roots();
    require(equal(saved, snapshot(exclude)), Status::Changed);
  }
};
std::string setStamp(const CeCareerStorageRequest &r, const std::string &path,
                     const std::string &expected) {
  Fd dir(directory(path));
  require(local(dir.n), Status::Unsupported);
  const struct stat opened = status(dir.n);
  struct stat manifestIdentity{};
  const Bytes raw = read(dir.n, "onslaught-backup.json", manifestIdentity);
  const std::string text(raw.begin(), raw.end());
  ce_preview::Manifest manifest;
  ce_preview::Parser parser(text);
  require(parser.parse(manifest) && manifest.files.size() <= kCountLimit,
          Status::Changed);
  size_t total = 0;
  std::string record;
  const auto add = [&](const struct stat &s) {
    record += std::to_string(s.st_dev) + ":" + std::to_string(s.st_ino) + ":" +
              std::to_string(s.st_size) + ":" + std::to_string(s.st_nlink) +
              ":" + std::to_string(s.st_mtim.tv_sec) + ":" +
              std::to_string(s.st_mtim.tv_nsec) + ":" +
              std::to_string(s.st_ctim.tv_sec) + ":" +
              std::to_string(s.st_ctim.tv_nsec) + ";";
  };
  add(opened);
  add(manifestIdentity);
  std::vector<std::pair<std::string, struct stat>> heldMembers;
  heldMembers.emplace_back("onslaught-backup.json", manifestIdentity);
  for (const auto &member : manifest.files) {
    struct stat observed{};
    const Bytes bytes = read(dir.n, member.name, observed);
    total += bytes.size();
    require(total <= kSetLimit);
    record += member.name + ":" + digest(bytes) + ";";
    add(observed);
    heldMembers.emplace_back(member.name, observed);
  }
  CePreviewSet verified;
  std::string error;
  require(CePreviewRead(path, r.user, r.game, r.package, verified, error) &&
              verified.identity == expected,
          Status::Changed);
  for (const auto &member : heldMembers) {
    struct stat current{};
    require(fstatat(dir.n, member.first.c_str(), &current,
                    AT_SYMLINK_NOFOLLOW) == 0 &&
                stamp(current, member.second),
            Status::Changed);
  }
  require(stamp(opened, status(dir.n)), Status::Changed);
  bound(dir.n, path);
  return ce_backup_verify::digest(record + expected);
}
std::set<std::string> names(const CeCareerRestoreRequest &r) {
  Fd user(directory(r.user, true)), game(directory(r.game, true));
  bool slotsPresent = false, originalPresent = false;
  Fd slots(openOptionalSavegames(user.n, slotsPresent));
  Fd original(openOptionalSavegames(game.n, originalPresent));
  struct stat beforeUser = slotsPresent ? status(slots.n) : (struct stat){};
  struct stat beforeGame =
      originalPresent ? status(original.n) : (struct stat){};
  std::map<std::string, std::string> spellings;
  std::set<std::string> found;
  for (bool fromUser : {true, false}) {
    if (!(fromUser ? slotsPresent : originalPresent))
      continue;
    int folder = fromUser ? slots.n : original.n;
    Fd copy(openat(folder, ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    require(copy.n >= 0);
    DIR *stream = fdopendir(copy.n);
    require(stream != nullptr);
    copy.n = -1;
    std::unique_ptr<DIR, int (*)(DIR *)> holder(stream, closedir);
    std::set<std::string> entries;
    errno = 0;
    size_t scanned = 0;
    while (dirent *entry = readdir(stream)) {
      require(++scanned <= kDirectoryEntryLimit);
      if (career(entry->d_name))
        entries.insert(entry->d_name);
    }
    require(errno == 0 && entries.size() <= kCountLimit);
    for (const std::string &entry : entries) {
      struct stat st{};
      require(fstatat(folder, entry.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 &&
              S_ISREG(st.st_mode) && st.st_nlink == 1);
      std::string key = lower(entry);
      if (!portable(entry)) {
        found.insert(key);
        continue;
      }
      auto was = spellings.find(key);
      require(was == spellings.end() ||
              (was->second == entry && !fromUser));
      spellings[key] = entry;
      found.insert(key);
    }
  }
  require((slotsPresent ? stamp(beforeUser, status(slots.n))
                        : savegamesAbsent(user.n)) &&
              (originalPresent ? stamp(beforeGame, status(original.n))
                               : savegamesAbsent(game.n)),
          Status::Changed);
  bound(user.n, r.user, true);
  bound(game.n, r.game, true);
  return found;
}
struct CopyStem {
  std::string name;
  unsigned next = 1;
};
CopyStem copyStem(const std::string &file) {
  CopyStem out{file.substr(0, file.size() - 4), 1};
  const std::string marker = " restored";
  std::string folded = lower(out.name);
  size_t pos = folded.rfind(marker);
  if (pos == std::string::npos || pos == 0)
    return out;
  std::string tail = folded.substr(pos + marker.size());
  unsigned previous = 1;
  if (!tail.empty()) {
    if (tail[0] != ' ' || tail.size() < 2 || tail.size() > 5)
      return out;
    previous = 0;
    for (size_t i = 1; i < tail.size(); ++i) {
      if (tail[i] < '0' || tail[i] > '9')
        return out;
      previous = previous * 10 + unsigned(tail[i] - '0');
    }
    if (previous < 2 || previous > 9999)
      return out;
  }
  out.name.resize(pos);
  out.next = previous + 1;
  return out;
}
std::string proposal(const CopyStem &base, unsigned number) {
  std::string suffix = " restored";
  if (number > 1)
    suffix += " " + std::to_string(number);
  require(suffix.size() < 31);
  const size_t maxStem = 31 - suffix.size();
  size_t cut = std::min(base.name.size(), maxStem);
  if (cut < base.name.size()) {
    while (cut > 0 &&
           (static_cast<unsigned char>(base.name[cut]) & 0xc0u) == 0x80u)
      --cut;
  }
  std::string stem = base.name.substr(0, cut);
  while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
    stem.pop_back();
  require(!stem.empty());
  const std::string out = stem + suffix + ".bes";
  require(portable(out));
  return out;
}
Bytes selected(const CeCareerRestoreRequest &r) {
  require(!r.sourceStamp.empty() &&
              setStamp(r, r.sourceSet, r.sourceIdentity) == r.sourceStamp,
          Status::Changed);
  CePreviewSet set;
  std::string error;
  require(CePreviewRead(r.sourceSet, r.user, r.game, r.package, set, error) &&
              set.identity == r.sourceIdentity,
          Status::Changed);
  for (const auto &entry : set.careers)
    if (entry.name == r.sourceName)
      return entry.bytes;
  throw Failure{Status::Refused};
}
std::string manifest(Session &s, const char *reason) {
  std::time_t now = std::time(nullptr);
  struct tm utc{};
  require(gmtime_r(&now, &utc) != nullptr);
  char timestamp[32];
  require(std::strftime(timestamp, sizeof timestamp, "%Y-%m-%dT%H:%M:%SZ",
                        &utc) > 0);
  std::string out = "{\"Folder\":" + quoted(s.r.user) +
                    ",\"Game\":" + quoted(s.r.game) +
                    ",\"Created\":" + quoted(timestamp) +
                    ",\"Reason\":" + quoted(reason) +
                    ",\"Target\":1,\"Files\":[";
  bool first = true;
  for (const auto &it : s.saved.visible) {
    const Member &m = s.saved.all[it.second];
    if (!first)
      out += ',';
    first = false;
    std::string parent = lower(m.name) == kOptions
                             ? s.r.user
                             : (m.user ? s.r.user : s.r.game) + "/savegames";
    out += "{\"Name\":" + quoted(m.name) +
           ",\"Original\":" + quoted(parent + "/" + m.name) +
           ",\"Size\":" + std::to_string(m.bytes.size()) +
           ",\"Sha256\":" + quoted(digest(m.bytes)) + "}";
  }
  return out + "]}\n";
}
// What the preview reader will refuse is refused here, before anything is
// written: a set it cannot read (more members or manifest bytes than it
// accepts, or no career at all) is never staged, so it is never published.
void readable(const Session &s, const std::string &json) {
  ce_preview::Manifest parsed;
  ce_preview::Parser parser(json);
  require(parser.parse(parsed) && parsed.files.size() == s.saved.visible.size());
  bool careerFound = false;
  for (const auto &entry : parsed.files)
    careerFound = careerFound || ce_preview::career(entry.name);
  require(careerFound);
}
// A set is staged under a name the preview never lists, written, flushed and
// verified by the preview reader itself while still staged; only then is it
// renamed, without replacement, to a listed name. A cut at any step before
// the rename leaves only the private staging folder. After the rename the set
// is the one already verified; it is read back again as confirmation, and a
// failed confirmation reports Uncertain and never removes or renames it.
void backup(Session &s, CeCareerStorageResult &out, const char *reason) {
  s.check();
  std::time_t now = std::time(nullptr);
  struct tm utc{};
  require(gmtime_r(&now, &utc) != nullptr);
  char stampText[32];
  require(std::strftime(stampText, sizeof stampText, "%Y%m%d-%H%M%S", &utc) >
          0);
  std::string final = "onslaught-backup-" + std::string(stampText) + "-" +
                      digits(18),
              staging = ".pending-" + final;
  const std::string json = manifest(s, reason);
  readable(s, json);
  require(mkdirat(s.root.n, staging.c_str(), 0700) == 0);
  Fd dir(openat(s.root.n, staging.c_str(),
                O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
  require(dir.n >= 0);
  for (const auto &it : s.saved.visible) {
    s.permission();
    const Member &m = s.saved.all[it.second];
    Fd file(openat(dir.n, m.name.c_str(),
                   O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    require(file.n >= 0);
    write(file.n, m.bytes);
    identical(file.n, m.bytes);
  }
  Bytes bytes(json.begin(), json.end());
  Fd list(openat(dir.n, "onslaught-backup.json",
                 O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  require(list.n >= 0);
  write(list.n, bytes);
  identical(list.n, bytes);
  require(fsync(dir.n) == 0);
  CePreviewSet staged;
  std::string error;
  require(CePreviewRead(s.r.backupRoot + "/" + staging, s.r.user, s.r.game,
                        s.r.package, staged, error) &&
          size_t(staged.members) == s.saved.visible.size() &&
          staged.identity == digest(bytes));
  s.check();
  require(renameat2(s.root.n, staging.c_str(), s.root.n, final.c_str(),
                    RENAME_NOREPLACE) == 0);
  out.backupSet = s.r.backupRoot + "/" + final;
  require(fsync(s.root.n) == 0, Status::Uncertain);
  CePreviewSet parsed;
  require(CePreviewRead(out.backupSet, s.r.user, s.r.game, s.r.package, parsed,
                        error),
          Status::Uncertain);
  require(size_t(parsed.members) == s.saved.visible.size() &&
              parsed.identity == staged.identity,
          Status::Uncertain);
  out.backupIdentity = parsed.identity;
  out.backupStamp = setStamp(s.r, out.backupSet, out.backupIdentity);
  s.check();
}
void checkedBackup(Session &s, const CeCareerStorageResult &out) {
  require(!out.backupStamp.empty() &&
              setStamp(s.r, out.backupSet, out.backupIdentity) ==
                  out.backupStamp,
          Status::Changed);
  CePreviewSet parsed;
  std::string error;
  require(CePreviewRead(out.backupSet, s.r.user, s.r.game, s.r.package, parsed,
                        error) &&
              parsed.identity == out.backupIdentity,
          Status::Changed);
}
bool exists(int at, const std::string &name) {
  if (at < 0)
    return false;
  struct stat entry{};
  if (fstatat(at, name.c_str(), &entry, AT_SYMLINK_NOFOLLOW) == 0)
    return true;
  require(errno == ENOENT);
  return false;
}
} // namespace

bool CeCareerRestoreStamp(const CeCareerRestoreRequest &request,
                          std::string &stampOut) {
  stampOut.clear();
  try {
    stampOut = setStamp(request, request.sourceSet, request.sourceIdentity);
    return true;
  } catch (...) {
    return false;
  }
}

CeCareerStorageStatus
CeCareerRestoreSuggest(const CeCareerRestoreRequest &request,
                       std::string &targetName) {
  targetName.clear();
  try {
    require(portable(request.sourceName));
    selected(request);
    const auto taken = names(request);
    const auto stem = copyStem(request.sourceName);
    for (unsigned n = stem.next; n < stem.next + 1000 && n <= 9999; ++n) {
      std::string candidate = proposal(stem, n);
      if (!taken.count(lower(candidate))) {
        targetName = candidate;
        return Status::Complete;
      }
    }
    return Status::Occupied;
  } catch (const Failure &error) {
    return error.status;
  } catch (...) {
    return Status::Refused;
  }
}

CeCareerStorageResult CeCareerBackupAll(const CeCareerStorageRequest &request) {
  CeCareerStorageResult out;
  try {
    Session session(request);
    backup(session, out, kFullSetReason);
    out.status = Status::Complete;
  } catch (const Failure &error) {
    out.status = out.backupSet.empty() ? error.status : Status::Uncertain;
  } catch (...) {
    out.status = out.backupSet.empty() ? Status::Refused : Status::Uncertain;
  }
  return out;
}
CeCareerStorageResult
CeCareerTakeFullSet(const CeCareerStorageRequest &request) {
  CeCareerStorageResult out = CeCareerBackupAll(request);
  if (out.status == Status::Complete) {
    // Bound again here. The set is published, so a failure from here on is
    // Uncertain: the set is kept and named, but there is nothing to write behind.
    bool bound = false;
    try {
      CePreviewSet again;
      std::string error;
      bound = !out.backupSet.empty() && !out.backupIdentity.empty() &&
              !out.backupStamp.empty() &&
              setStamp(request, out.backupSet, out.backupIdentity) ==
                  out.backupStamp &&
              CePreviewRead(out.backupSet, request.user, request.game,
                            request.package, again, error) &&
              again.identity == out.backupIdentity;
    } catch (...) {
      bound = false;
    }
    if (bound)
      return out;
    out.status = Status::Uncertain;
  }
  // Not a set to write behind: only Uncertain keeps the path, to inspect.
  if (out.status != Status::Uncertain)
    out.backupSet.clear();
  out.backupIdentity.clear();
  out.backupStamp.clear();
  return out;
}

CeCareerStorageResult
CeCareerRestoreAsNew(const CeCareerRestoreRequest &request) {
  CeCareerStorageResult out;
  bool published = false;
  try {
    require(portable(request.targetName) && request.namePermitted &&
            request.namePermitted(request.owner, request.sourceName,
                                  request.targetName));
    Session session(request);
    const Bytes payload = selected(request);
    require(!payload.empty());
    const std::string target = request.targetName;
    for (const Member &member : session.saved.all)
      require(lower(member.name) != lower(target), Status::Occupied);
    for (const SkippedMember &member : session.saved.skipped)
      require(lower(member.name) != lower(target), Status::Occupied);
    require(!exists(session.slots.n, target) &&
                !exists(session.original.n, target),
            Status::Occupied);
    const bool needBackup = hasCareers(session.saved);
    if (needBackup) {
      backup(session, out, kRestoreReason);
      checkedBackup(session, out);
    }
    require(selected(request) == payload, Status::Changed);
    session.permission();
    require(request.namePermitted(request.owner, request.sourceName, target));
    session.check();
    session.ensureUserSavegames();
    Fd stage(
        openat(session.slots.n, ".", O_RDWR | O_TMPFILE | O_CLOEXEC, 0600));
    require(stage.n >= 0, Status::Unsupported);
    write(stage.n, payload);
    identical(stage.n, payload);
    require(!exists(session.slots.n, target) &&
                !exists(session.original.n, target),
            Status::Occupied);
    session.check();
    if (needBackup)
      checkedBackup(session, out);
    require(selected(request) == payload, Status::Changed);
    session.permission();
    require(request.namePermitted(request.owner, request.sourceName, target));
    session.roots();
    require(!exists(session.original.n, target), Status::Occupied);
    char fdPath[64];
    std::snprintf(fdPath, sizeof(fdPath), "/proc/self/fd/%d", stage.n);
    int linkRc = linkat(AT_FDCWD, fdPath, session.slots.n, target.c_str(),
                        AT_SYMLINK_FOLLOW);
    if (linkRc != 0 && errno == ENOENT)
      linkRc =
          linkat(stage.n, "", session.slots.n, target.c_str(), AT_EMPTY_PATH);
    if (linkRc)
      throw Failure{errno == EEXIST ? Status::Occupied : Status::Refused};
    published = true;
    out.newFile = target;
    require(fsync(session.slots.n) == 0, Status::Uncertain);
    Fd final(openat(session.slots.n, target.c_str(),
                    O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    require(final.n >= 0 && same(status(final.n), status(stage.n)) &&
                status(final.n).st_nlink == 1,
            Status::Uncertain);
    identical(final.n, payload);
    if (needBackup)
      checkedBackup(session, out);
    require(selected(request) == payload, Status::Changed);
    session.check(target, false);
    require(request.namePermitted(request.owner, request.sourceName, target));
    require(!exists(session.original.n, target), Status::Occupied);
    out.status = Status::Complete;
  } catch (const Failure &error) {
    out.status = published ? Status::Uncertain : error.status;
  } catch (...) {
    out.status = published ? Status::Uncertain : Status::Refused;
  }
  return out;
}
