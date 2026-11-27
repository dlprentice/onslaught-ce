// Read one explicitly chosen local backup. Every open below is read-only.
#include "ce_backup_paths.h"
#include "../common/ce_backup_verify.h"
#include "../common/ce_win_reparse.h"
#include <set>

namespace {
using namespace ce_backup_paths;

struct Stamp {
  FILE_ID_INFO id{};
  LONGLONG size = 0, written = 0, changed = 0;
};
bool stamp(HANDLE h, const Wide &leaf, Stamp &s) {
  FILE_BASIC_INFO basic{};
  FILE_STANDARD_INFO standard{};
  if (GetFileType(h) != FILE_TYPE_DISK ||
      !GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic) ||
      (basic.FileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN |
        FILE_ATTRIBUTE_SYSTEM)) ||
      ce_win_handle_is_link(h, basic.FileAttributes) ||
      !GetFileInformationByHandleEx(h, FileStandardInfo, &standard,
                                    sizeof standard) ||
      standard.Directory || standard.DeletePending ||
      standard.NumberOfLinks != 1 || standard.EndOfFile.QuadPart < 0 ||
      !GetFileInformationByHandleEx(h, FileIdInfo, &s.id, sizeof s.id) ||
      !physicalLeaf(h, leaf))
    return false;
  s.size = standard.EndOfFile.QuadPart;
  s.written = basic.LastWriteTime.QuadPart;
  s.changed = basic.ChangeTime.QuadPart;
  return true;
}
bool equal(const Stamp &a, const Stamp &b) {
  return sameId(a.id, b.id) && a.size == b.size && a.written == b.written &&
         a.changed == b.changed;
}
struct File {
  std::unique_ptr<Handle> file;
  Wide leaf;
  Stamp initial;
  std::string data;
  bool sameObject(const File &other) const {
    return sameId(initial.id, other.initial.id);
  }
  bool unchanged(HANDLE parent) const {
    Stamp held, named;
    Handle again = child(parent, leaf, false, false);
    return again && stamp(again.h, leaf, named) && stamp(file->h, leaf, held) &&
           equal(initial, held) && equal(initial, named);
  }
  bool openRead(HANDLE parent, const std::string &name, size_t limit) {
    if (name.find('\0') != name.npos || !wide(name.c_str(), leaf) ||
        !component(leaf))
      return false;
    file.reset(new Handle(child(parent, leaf, false, false)));
    if (!*file || !stamp(file->h, leaf, initial) ||
        static_cast<unsigned long long>(initial.size) > limit)
      return false;
    data.resize(static_cast<size_t>(initial.size));
    for (size_t pos = 0; pos < data.size();) {
      DWORD got = 0;
      const DWORD ask =
          static_cast<DWORD>(std::min<size_t>(data.size() - pos, 65536));
      if (!ReadFile(file->h, &data[pos], ask, &got, nullptr) || !got)
        return false;
      pos += got;
    }
    char extra;
    DWORD got = 0;
    return ReadFile(file->h, &extra, 1, &got, nullptr) && !got &&
           unchanged(parent);
  }
};

// Enumerate the handle already admitted, with bounded work even for a large or
// concurrently growing directory. Never resolve the listing through a wildcard.
template <class Visit> bool entries(HANDLE dir, Visit visit) {
  alignas(FILE_ID_BOTH_DIR_INFO) unsigned char buf[16384];
  auto kind = FileIdBothDirectoryRestartInfo;
  size_t count = 0;
  while (GetFileInformationByHandleEx(dir, kind, buf, sizeof buf)) {
    kind = FileIdBothDirectoryInfo;
    for (size_t pos = 0;;) {
      constexpr size_t head = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
      if (pos > sizeof buf - head)
        return false;
      const auto *e =
          reinterpret_cast<const FILE_ID_BOTH_DIR_INFO *>(buf + pos);
      const size_t len = e->FileNameLength;
      if (++count > 4096 || !len || len % sizeof(wchar_t) ||
          len > sizeof buf - pos - head)
        return false;
      Wide leaf(e->FileName, len / sizeof(wchar_t));
      if (leaf != L"." && leaf != L".." && !visit(leaf, e->FileAttributes))
        return false;
      if (!e->NextEntryOffset)
        break;
      if (e->NextEntryOffset < head + len ||
          e->NextEntryOffset % alignof(FILE_ID_BOTH_DIR_INFO) ||
          e->NextEntryOffset > sizeof buf - pos)
        return false;
      pos += e->NextEntryOffset;
    }
  }
  return GetLastError() == ERROR_NO_MORE_FILES;
}

struct Folders {
  Directory selected, user, game, package;
  bool open(const std::string &select, const std::string &u,
            const std::string &g, const std::string &p) {
    for (const auto *name : {&select, &u, &g, &p})
      if (name->find('\0') != name->npos)
        return false;
    return selected.open(select.c_str()) && user.open(u.c_str()) &&
           game.open(g.c_str()) && package.open(p.c_str()) &&
           outside(selected, user, game, package);
  }
  bool current(const std::string &select, const std::string &u,
               const std::string &g, const std::string &p) const {
    Folders again;
    return again.open(select, u, g, p) &&
           sameId(selected.ids.back(), again.selected.ids.back()) &&
           sameId(user.ids.back(), again.user.ids.back()) &&
           sameId(game.ids.back(), again.game.ids.back()) &&
           sameId(package.ids.back(), again.package.ids.back());
  }
};
struct OrdinalLess {
  bool operator()(const Wide &a, const Wide &b) const {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                b.c_str(), static_cast<int>(b.size()),
                                TRUE) == CSTR_LESS_THAN;
  }
};
using Names = std::set<Wide, OrdinalLess>;
bool slots(HANDLE dir, Names &names) {
  NTSTATUS status = 0;
  Handle folder = child(dir, L"savegames", true, false, &status);
  if (!folder)
    return status == static_cast<NTSTATUS>(0xc0000034u) ||
           status == static_cast<NTSTATUS>(0xc000003au); // no such name/path
  FILE_ID_INFO id;
  return dirInfo(folder.h, id) && entries(folder.h, [&](const Wide &n, DWORD) {
           names.insert(n);
           return true;
         });
}
} // namespace

bool CePreviewRead(const std::string &set, const std::string &user,
                   const std::string &game, const std::string &package,
                   CePreviewSet &out, std::string &error) {
  out = {};
  error = "Choose a local backup folder outside the game and careers.";
  Folders dirs;
  if (!dirs.open(set, user, game, package))
    return false;
  Names used;
  bool scanned = false, known = false;
  auto suggest = [&]() -> std::string {
    if (!scanned) {
      scanned = true;
      known = slots(dirs.user.h(), used) && slots(dirs.game.h(), used);
    }
    if (known)
      for (int i = 1; i <= 999; ++i) {
        std::string name = i == 1 ? "Backup Copy.bes"
                                  : "Backup Copy " + std::to_string(i) + ".bes";
        if (used.emplace(name.begin(), name.end()).second)
          return name;
      }
    return {};
  };
  return ce_backup_verify::read<File>(
      dirs.selected.h(), suggest,
      [&] { return dirs.current(set, user, game, package); }, out, error);
}

bool CePreviewList(const std::string &root, const std::string &user,
                   const std::string &game, const std::string &package,
                   std::vector<std::string> &sets, std::string &error) {
  sets.clear();
  error = "The configured backup folder could not be checked.";
  Folders dirs;
  if (!dirs.open(root, user, game, package))
    return false;
  std::vector<std::string> found;
  if (!entries(dirs.selected.h(),
               [&](const Wide &leaf, DWORD attr) {
                 if (!(attr & FILE_ATTRIBUTE_DIRECTORY) ||
                     (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ||
                     leaf.compare(0, sizeof("onslaught-backup-") - 1,
                                  L"onslaught-backup-") != 0 ||
                     !component(leaf))
                   return true;
                 std::string name;
                 if (!utf8(leaf, name) || !ce_preview::safeName(name))
                   return true;
                 Handle folder = child(dirs.selected.h(), leaf, true, false);
                 FILE_ID_INFO id;
                 if (!folder || !dirInfo(folder.h, id))
                   return true;
                 Handle manifest =
                     child(folder.h, L"onslaught-backup.json", false, false);
                 Stamp s;
                 if (manifest && stamp(manifest.h, L"onslaught-backup.json", s))
                   found.emplace_back(std::move(name));
                 return true;
               }) ||
      !dirs.current(root, user, game, package))
    return false;
  std::sort(found.rbegin(), found.rend());
  if (found.size() > 64)
    found.resize(64);
  sets = std::move(found);
  error.clear();
  return true;
}
