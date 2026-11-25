// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/ce_career_publish.cpp: Linux external .bes picker/reader, filename proposal,
// and atomic non-destructive publish-as-new behind CeCareerTakeFullSet.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../common/ce_career_publish.h"
#include "../common/ce_backup_name.h"
#include "../common/ce_backup_preview.h"
#include "../common/ce_career_restore.h"
#include "../../game/ce_career_view.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>

namespace
{
constexpr size_t kDirectoryEntryLimit = 2048;
constexpr size_t kCareerLimit = 512;

struct Failure
{
	CeCareerPublishStatus	status;
};

void	require(bool ok, CeCareerPublishStatus status = CE_PUBLISH_REFUSED)
{
	if (!ok)
		throw Failure{status};
}

struct Fd
{
	int	n = -1;
	explicit Fd(int f = -1) : n(f) {}
	~Fd()
	{
		if (n >= 0)
			close(n);
	}
	Fd(const Fd &) = delete;
	Fd &operator=(const Fd &) = delete;
};

void	copyText(char *dst, size_t cap, const char *src)
{
	if (!dst || !cap)
		return;
	std::snprintf(dst, cap, "%s", src ? src : "");
}

std::string	trimmedPath(const char *path)
{
	std::string out = path ? path : "";
	while (out.size() > 1 && out.back() == '/')
		out.pop_back();
	return out;
}

std::string	lowerAscii(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c = char(c + ('a' - 'A'));
	return s;
}

bool	hasBesExt(const std::string &s)
{
	return s.size() > 4 && lowerAscii(s.substr(s.size() - 4)) == ".bes";
}

bool	reservedDevice(const std::string &stem)
{
	std::string head = lowerAscii(stem);
	const size_t dot = head.find('.');
	if (dot != std::string::npos)
		head.resize(dot);
	while (!head.empty() && head.back() == ' ')
		head.pop_back();
	if (head == "con" || head == "prn" || head == "aux" || head == "nul" ||
		head == "conin$" || head == "conout$")
		return true;
	return head.size() == 4 &&
		   (head.substr(0, 3) == "com" || head.substr(0, 3) == "lpt") &&
		   head[3] >= '0' && head[3] <= '9';
}

bool	portableCareerName(const std::string &s)
{
	if (!hasBesExt(s) || s.size() > 35 || s.front() == ' ' ||
		s[s.size() - 5] == ' ' || s[s.size() - 5] == '.')
		return false;
	if (CeBackupFileHasDirectionMark(s.c_str(), s.size()))
		return false;
	for (unsigned char c : s)
		if (c < 32 || c >= 127 || std::strchr("<>:\"/\\|?*", c))
			return false;
	return !reservedDevice(s.substr(0, s.size() - 4));
}

bool	safePathText(const char *path, size_t maxLen)
{
	if (!path || !path[0] || strnlen(path, maxLen) >= maxLen)
		return false;
	if (path[0] != '/')
		return false;
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(path); *p; ++p)
		if (*p < 32 || *p == 127 || *p == '\\')
			return false;
	return true;
}

bool	sameStat(const struct stat &a, const struct stat &b)
{
	return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

bool	stampEq(const struct stat &a, const struct stat &b)
{
	return sameStat(a, b) && a.st_size == b.st_size && a.st_nlink == b.st_nlink &&
		   a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
		   a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
		   a.st_ctim.tv_sec == b.st_ctim.tv_sec &&
		   a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}

struct stat	statFd(int fd)
{
	struct stat s{};
	require(fstat(fd, &s) == 0);
	return s;
}

bool	isOsSystemSymlink(int parentFd, const char *part)
{
	struct stat pst{}, lst{};
	return fstat(parentFd, &pst) == 0 && pst.st_uid == 0 &&
		   !(pst.st_mode & (S_IWGRP | S_IWOTH)) &&
		   fstatat(parentFd, part, &lst, AT_SYMLINK_NOFOLLOW) == 0 &&
		   S_ISLNK(lst.st_mode) && lst.st_uid == 0;
}

int	openDirNoFollow(const std::string &path, bool allowUserAncestors = false)
{
	if (path.empty() || path[0] != '/' || path.size() >= 4096)
		return -1;
	if (path.size() > 1 && path.back() == '/')
		return -1;
	Fd at(open("/", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
	if (at.n < 0)
		return -1;
	size_t begin = 1;
	while (begin < path.size())
	{
		size_t end = path.find('/', begin);
		const bool isLeaf = (end == std::string::npos);
		if (isLeaf)
			end = path.size();
		std::string part = path.substr(begin, end - begin);
		if (part.empty() || part == "." || part == ".." || part.find('\0') != std::string::npos)
			return -1;
		int next = openat(at.n, part.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
		if (next < 0 && (errno == ELOOP || errno == ENOTDIR) && !isLeaf &&
			(allowUserAncestors || isOsSystemSymlink(at.n, part.c_str())))
		{
			next = openat(at.n, part.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
		}
		if (next < 0)
			return -1;
		close(at.n);
		at.n = next;
		begin = end + 1;
	}
	int out = at.n;
	at.n = -1;
	return out;
}

int	requireDir(const std::string &path, CeCareerPublishStatus status = CE_PUBLISH_REFUSED, bool allowUserAncestors = false)
{
	int fd = openDirNoFollow(path, allowUserAncestors);
	require(fd >= 0, status);
	return fd;
}

bool	boundDir(int fd, const std::string &path, bool allowUserAncestors = false)
{
	Fd again(openDirNoFollow(path, allowUserAncestors));
	struct stat a{}, b{};
	return again.n >= 0 && fstat(fd, &a) == 0 && fstat(again.n, &b) == 0 && sameStat(a, b);
}

bool	localFs(int fd)
{
	struct statfs fs{};
	const unsigned long t = fstatfs(fd, &fs) == 0 ? static_cast<unsigned long>(fs.f_type) : 0UL;
	return t == 0x9123683eUL || // btrfs
		   t == 0xef53UL ||     // ext2/ext3/ext4
		   t == 0x58465342UL || // xfs
		   t == 0xF2F52010UL || // f2fs
		   t == 0xca451a4eUL;   // bcachefs
}

// True if `a` is `b` or an ancestor of `b`, or on traversal error (fail closed).
bool	isAncestorOrSame(int a, int b)
{
	struct stat sought{};
	if (fstat(a, &sought) != 0)
		return true;
	Fd cur(openat(b, ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
	if (cur.n < 0)
		return true;
	for (int depth = 0; depth < 512; ++depth)
	{
		struct stat here{};
		if (fstat(cur.n, &here) != 0)
			return true;
		if (sameStat(sought, here))
			return true;
		Fd up(openat(cur.n, "..", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
		if (up.n < 0)
			return true;
		struct stat parent{};
		if (fstat(up.n, &parent) != 0)
			return true;
		if (sameStat(here, parent))
			return false;
		close(cur.n);
		cur.n = up.n;
		up.n = -1;
	}
	return true;
}

bool	entryExists(int dirFd, const std::string &name)
{
	if (dirFd < 0)
		return false;
	struct stat st{};
	if (fstatat(dirFd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0)
		return true;
	require(errno == ENOENT);
	return false;
}

bool	savegamesAbsent(int parentFd)
{
	struct stat st{};
	return fstatat(parentFd, "savegames", &st, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
}

int	openOptionalSavegames(int parentFd, bool &present)
{
	int fd = openat(parentFd, "savegames", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
	if (fd >= 0)
	{
		require(S_ISDIR(statFd(fd).st_mode));
		present = true;
		return fd;
	}
	require(errno == ENOENT && savegamesAbsent(parentFd));
	present = false;
	return -1;
}

void	writeFullSync(int fd, const unsigned char *data, size_t size)
{
	size_t done = 0;
	while (done < size)
	{
		ssize_t n = ::write(fd, data + done, size - done);
		if (n < 0 && errno == EINTR)
			continue;
		require(n > 0);
		done += size_t(n);
	}
	require(fsync(fd) == 0);
}

bool	readBackMatches(int fd, const unsigned char *data, size_t size)
{
	struct stat st{};
	if (fstat(fd, &st) != 0 || static_cast<unsigned long long>(st.st_size) != size)
		return false;
	std::vector<unsigned char> back(size);
	size_t done = 0;
	while (done < size)
	{
		ssize_t n = pread(fd, back.data() + done, size - done, off_t(done));
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return false;
		done += size_t(n);
	}
	unsigned char tail = 0;
	ssize_t extra = 0;
	do
	{
		extra = pread(fd, &tail, 1, off_t(size));
	}
	while (extra < 0 && errno == EINTR);
	return extra == 0 && std::memcmp(back.data(), data, size) == 0;
}

// Collects lower-cased .bes names across savegames in dirFd; returns false on unsafe entries or errors.
bool	collectCareerNames(int dirFd, bool allowMissing, std::set<std::string> &out)
{
	Fd saves(openat(dirFd, "savegames", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
	if (saves.n < 0)
		return allowMissing && errno == ENOENT && savegamesAbsent(dirFd);
	struct stat before{};
	if (fstat(saves.n, &before) != 0)
		return false;
	Fd copy(openat(saves.n, ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY));
	if (copy.n < 0)
		return false;
	DIR *raw = fdopendir(copy.n);
	if (!raw)
		return false;
	copy.n = -1;
	std::unique_ptr<DIR, int (*)(DIR *)> stream(raw, closedir);
	std::set<std::string> inDir;
	size_t scanned = 0, careers = 0;
	errno = 0;
	while (dirent *ent = readdir(raw))
	{
		if (++scanned > kDirectoryEntryLimit)
			return false;
		std::string name = ent->d_name;
		if (name == "." || name == "..")
			continue;
		if (!hasBesExt(name))
			continue;
		if (++careers > kCareerLimit)
			return false;
		struct stat st{};
		if (fstatat(saves.n, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 ||
			!S_ISREG(st.st_mode) || st.st_nlink != 1)
			return false;
		const std::string folded = lowerAscii(name);
		if (!inDir.insert(folded).second)
			return false;
		out.insert(folded);
	}
	if (errno != 0)
		return false;
	struct stat after{};
	return fstat(saves.n, &after) == 0 && stampEq(before, after);
}

// Sanitizes a leaf name into a clean ASCII base stem for filename suggestions.
std::string	cleanBaseStem(const char *sourceName, const char *fallback)
{
	std::string raw = sourceName ? sourceName : "";
	if (hasBesExt(raw))
		raw.resize(raw.size() - 4);
	std::string out;
	out.reserve(raw.size());
	for (unsigned char c : raw)
	{
		if (c < 32 || c >= 127 || std::strchr("<>:\"/\\|?*", c))
			continue;
		if (c == ' ' && (out.empty() || out.back() == ' '))
			continue;
		out.push_back(char(c));
	}
	while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
		out.pop_back();
	if (out.empty() || reservedDevice(out))
		out = fallback;
	return out;
}

struct SuffixBase
{
	std::string	stem;
	unsigned	next = 1;
};

SuffixBase	stripCopySuffix(const std::string &stem, const std::string &tag)
{
	SuffixBase out{stem, 1};
	const std::string marker = " " + tag;
	std::string folded = lowerAscii(out.stem);
	size_t pos = folded.rfind(marker);
	if (pos == std::string::npos || pos == 0)
		return out;
	std::string tail = folded.substr(pos + marker.size());
	unsigned previous = 1;
	if (!tail.empty())
	{
		if (tail[0] != ' ' || tail.size() < 2 || tail.size() > 5)
			return out;
		previous = 0;
		for (size_t i = 1; i < tail.size(); ++i)
		{
			if (tail[i] < '0' || tail[i] > '9')
				return out;
			previous = previous * 10u + unsigned(tail[i] - '0');
		}
		if (previous < 2 || previous > 9999)
			return out;
	}
	std::string trimmed = out.stem.substr(0, pos);
	while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '.'))
		trimmed.pop_back();
	if (trimmed.empty())
		return out;
	out.stem = trimmed;
	out.next = previous + 1;
	return out;
}

std::string	formatCandidate(const std::string &baseStem, const char *tag, unsigned number)
{
	std::string suffix;
	if (tag && *tag)
	{
		suffix = std::string(" ") + tag;
		if (number > 1)
			suffix += " " + std::to_string(number);
	}
	if (suffix.size() >= 31)
		return {};
	std::string stem = baseStem.substr(0, 31 - suffix.size());
	while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
		stem.pop_back();
	if (stem.empty())
		return {};
	std::string full = stem + suffix + ".bes";
	return portableCareerName(full) ? full : std::string();
}

CeCareerPublishStatus	mapStorageStatus(CeCareerStorageStatus s)
{
	switch (s)
	{
	case CeCareerStorageStatus::Complete:		return CE_PUBLISH_COMPLETE;
	case CeCareerStorageStatus::Refused:		return CE_PUBLISH_REFUSED;
	case CeCareerStorageStatus::Busy:			return CE_PUBLISH_BUSY;
	case CeCareerStorageStatus::Cancelled:		return CE_PUBLISH_CANCELLED;
	case CeCareerStorageStatus::Changed:		return CE_PUBLISH_CHANGED;
	case CeCareerStorageStatus::Occupied:		return CE_PUBLISH_OCCUPIED;
	case CeCareerStorageStatus::Uncertain:		return CE_PUBLISH_UNCERTAIN;
	case CeCareerStorageStatus::Unsupported:	return CE_PUBLISH_UNSUPPORTED;
	}
	return CE_PUBLISH_REFUSED;
}

// ---- Async external .bes picker state (SDL3 dialog callback may arrive on another thread) ----
struct PickerState
{
	std::mutex			lock;
	CeImportPickStatus	view{};
	unsigned long		generation = 0;
	bool				open = false;
};

PickerState	&picker()
{
	static PickerState *s = new PickerState;
	return *s;
}

struct PickerTicket
{
	unsigned long	generation;
};

static const SDL_DialogFileFilter	kBesFilters[] = {{"Career Files", "bes"}};

void	setPickNote(CeImportPickStatus &v, CeImportPickState state, const char *msg)
{
	v.state = state;
	copyText(v.message, sizeof(v.message), msg);
}

void SDLCALL	onFilePicked(void *context, const char *const *files, int)
{
	const unsigned long gen = static_cast<PickerTicket *>(context)->generation;
	delete static_cast<PickerTicket *>(context);
	PickerState &s = picker();
	std::lock_guard<std::mutex> hold(s.lock);
	if (!s.open || s.generation != gen || s.view.state != CE_IMPORT_PICK_OPEN)
		return;
	if (!files)
	{
		setPickNote(s.view, CE_IMPORT_PICK_ERROR, "The file chooser could not open.");
		return;
	}
	if (!files[0])
	{
		setPickNote(s.view, CE_IMPORT_PICK_CANCELLED, "No file chosen.");
		return;
	}
	if (files[1] || !safePathText(files[0], sizeof(s.view.path)))
	{
		setPickNote(s.view, CE_IMPORT_PICK_ERROR, "Choose one local career file.");
		return;
	}
	const char *slash = std::strrchr(files[0], '/');
	const char *leaf = slash ? slash + 1 : files[0];
	if (!leaf[0] || strnlen(leaf, sizeof(s.view.leaf)) >= sizeof(s.view.leaf))
	{
		setPickNote(s.view, CE_IMPORT_PICK_ERROR, "Choose one local career file.");
		return;
	}
	copyText(s.view.path, sizeof(s.view.path), files[0]);
	copyText(s.view.leaf, sizeof(s.view.leaf), leaf);
	setPickNote(s.view, CE_IMPORT_PICK_READY, "Checking the selected career file.");
}
} // namespace

void	CeCareerImportPickEnter()
{
	PickerState &s = picker();
	std::lock_guard<std::mutex> hold(s.lock);
	s.open = true;
	++s.generation;
	s.view = {};
}

void	CeCareerImportPickLeave()
{
	PickerState &s = picker();
	std::lock_guard<std::mutex> hold(s.lock);
	s.open = false;
	++s.generation;
	s.view.state = CE_IMPORT_PICK_IDLE;
}

void	CeCareerImportPickOpen()
{
	PickerState &s = picker();
	PickerTicket *ticket = nullptr;
	{
		std::lock_guard<std::mutex> hold(s.lock);
		if (!s.open || s.view.state == CE_IMPORT_PICK_OPEN)
			return;
		ticket = new (std::nothrow) PickerTicket{s.generation};
		if (!ticket)
		{
			setPickNote(s.view, CE_IMPORT_PICK_ERROR, "There is not enough memory.");
			return;
		}
		s.view.path[0] = 0;
		s.view.leaf[0] = 0;
		setPickNote(s.view, CE_IMPORT_PICK_OPEN, "Waiting for a career file.");
	}
	SDL_ShowOpenFileDialog(onFilePicked, ticket, SDL_GetKeyboardFocus(), kBesFilters, 1, nullptr, false);
}

void	CeCareerImportPickPoll(CeImportPickStatus *out)
{
	if (!out)
		return;
	PickerState &s = picker();
	std::lock_guard<std::mutex> hold(s.lock);
	*out = s.view;
}

void	CeCareerPublishPackageDir(char *out, size_t cap)
{
	if (!out || !cap)
		return;
	const std::string pkg = trimmedPath(SDL_GetBasePath());
	copyText(out, cap, pkg.c_str());
}

CeImportReadStatus	CeCareerImportRead(const char *filePath, const char *userDir, const char *gameDir,
									   const char *packageDir, unsigned char *bytesOut, size_t bytesCap,
									   char *leafOut, size_t leafCap, char *errorOut, size_t errorCap)
{
	if (leafOut && leafCap)
		leafOut[0] = 0;
	copyText(errorOut, errorCap, "This career file could not be read.");
	if (!bytesOut || bytesCap < CE_CAREER_SIZE || !safePathText(filePath, 4096))
	{
		copyText(errorOut, errorCap, "Choose a local career file outside your game and saves.");
		return CE_IMPORT_READ_BAD_PATH;
	}
	const std::string full(filePath);
	const size_t slash = full.rfind('/');
	if (slash == std::string::npos || slash + 1 >= full.size())
	{
		copyText(errorOut, errorCap, "Choose a local career file outside your game and saves.");
		return CE_IMPORT_READ_BAD_PATH;
	}
	const std::string parentPath = slash == 0 ? "/" : full.substr(0, slash);
	const std::string leaf = full.substr(slash + 1);
	if (leaf.empty() || leaf == "." || leaf == ".." || leaf.size() >= 256 ||
		!hasBesExt(leaf) || CeBackupFileHasDirectionMark(leaf.c_str(), leaf.size()))
	{
		copyText(errorOut, errorCap, "Choose a valid .bes career file.");
		return CE_IMPORT_READ_BAD_PATH;
	}

	const std::string user = trimmedPath(userDir);
	const std::string game = trimmedPath(gameDir);
	const std::string pkg = (packageDir && *packageDir) ? trimmedPath(packageDir) : trimmedPath(SDL_GetBasePath());
	Fd parentFd(openDirNoFollow(parentPath, false));
	Fd userFd(openDirNoFollow(user, true));
	Fd gameFd(openDirNoFollow(game, true));
	Fd pkgFd(openDirNoFollow(pkg, true));
	if (parentFd.n < 0 || userFd.n < 0 || gameFd.n < 0 || pkgFd.n < 0)
	{
		copyText(errorOut, errorCap, "The selected location cannot be opened safely.");
		return CE_IMPORT_READ_BAD_PATH;
	}
	if (isAncestorOrSame(userFd.n, parentFd.n) ||
		isAncestorOrSame(gameFd.n, parentFd.n) ||
		isAncestorOrSame(pkgFd.n, parentFd.n))
	{
		copyText(errorOut, errorCap, "Choose a career file outside your game, package and saves.");
		return CE_IMPORT_READ_BAD_PATH;
	}

	Fd fileFd(openat(parentFd.n, leaf.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
	if (fileFd.n < 0)
	{
		if (errno == ELOOP)
		{
			copyText(errorOut, errorCap, "Linked career files cannot be imported.");
			return CE_IMPORT_READ_UNSAFE_FILE;
		}
		copyText(errorOut, errorCap, "The selected file could not be opened.");
		return CE_IMPORT_READ_IO_ERROR;
	}
	struct stat st{};
	if (fstat(fileFd.n, &st) != 0)
		return CE_IMPORT_READ_IO_ERROR;
	if (!S_ISREG(st.st_mode) || st.st_nlink != 1)
	{
		copyText(errorOut, errorCap, "Choose a regular, unlinked career file.");
		return CE_IMPORT_READ_UNSAFE_FILE;
	}
	if (st.st_size != off_t(CE_CAREER_SIZE))
	{
		copyText(errorOut, errorCap, "This file is not a valid 10,004-byte career file.");
		return CE_IMPORT_READ_BAD_SIZE;
	}

	unsigned char buf[CE_CAREER_SIZE];
	size_t done = 0;
	while (done < CE_CAREER_SIZE)
	{
		ssize_t n = ::read(fileFd.n, buf + done, CE_CAREER_SIZE - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
		{
			copyText(errorOut, errorCap, "The selected file could not be read completely.");
			return CE_IMPORT_READ_IO_ERROR;
		}
		done += size_t(n);
	}
	unsigned char tail = 0;
	ssize_t extra = 0;
	do
	{
		extra = ::read(fileFd.n, &tail, 1);
	}
	while (extra < 0 && errno == EINTR);
	struct stat afterFd{}, afterName{};
	if (extra != 0 || fstat(fileFd.n, &afterFd) != 0 || !stampEq(st, afterFd) ||
		fstatat(parentFd.n, leaf.c_str(), &afterName, AT_SYMLINK_NOFOLLOW) != 0 ||
		!stampEq(st, afterName) || !boundDir(parentFd.n, parentPath))
	{
		copyText(errorOut, errorCap, "The selected file changed while it was being read.");
		return CE_IMPORT_READ_IO_ERROR;
	}

	SCeCareerView view{};
	if (!CeCareerViewBuild(buf, CE_CAREER_SIZE, &view) || !CeCareerViewPlausible(view))
	{
		copyText(errorOut, errorCap, "This file is not a valid Onslaught career.");
		return CE_IMPORT_READ_BAD_CAREER;
	}

	std::memcpy(bytesOut, buf, CE_CAREER_SIZE);
	copyText(leafOut, leafCap, leaf.c_str());
	copyText(errorOut, errorCap, "");
	return CE_IMPORT_READ_OK;
}

CeCareerPublishStatus	CeCareerPublishSuggest(CeCareerPublishKind kind, const char *userDir,
											   const char *gameDir, const char *sourceName,
											   CeCareerPublishNamePermitFn namePermitted, void *owner,
											   char *targetOut, size_t targetCap)
{
	if (targetOut && targetCap)
		targetOut[0] = 0;
	if (!targetOut || targetCap < 36 || !sourceName || !*sourceName || !namePermitted)
		return CE_PUBLISH_BAD_NAME;
	if (CeBackupFileHasDirectionMark(sourceName, strnlen(sourceName, 256)))
		return CE_PUBLISH_BAD_NAME;

	const std::string user = trimmedPath(userDir);
	const std::string game = trimmedPath(gameDir);
	Fd userFd(openDirNoFollow(user, true));
	Fd gameFd(openDirNoFollow(game, true));
	if (userFd.n < 0 || gameFd.n < 0)
		return CE_PUBLISH_REFUSED;

	std::set<std::string> taken;
	if (!collectCareerNames(userFd.n, true, taken) || !collectCareerNames(gameFd.n, true, taken))
		return CE_PUBLISH_REFUSED;
	if (!boundDir(userFd.n, user, true) || !boundDir(gameFd.n, game, true))
		return CE_PUBLISH_CHANGED;

	bool sawNameRefusal = false;
	const std::string clean = cleanBaseStem(sourceName, kind == CE_PUBLISH_IMPORT ? "Imported" : "Career");

	if (kind == CE_PUBLISH_IMPORT)
	{
		const std::string direct = formatCandidate(clean, nullptr, 1);
		if (!direct.empty() && !taken.count(lowerAscii(direct)))
		{
			if (namePermitted(owner, sourceName, direct.c_str()))
			{
				copyText(targetOut, targetCap, direct.c_str());
				return CE_PUBLISH_COMPLETE;
			}
			sawNameRefusal = true;
		}
		const SuffixBase base = stripCopySuffix(clean, "imported");
		for (unsigned n = base.next; n < base.next + 1000 && n <= 9999; ++n)
		{
			const std::string cand = formatCandidate(base.stem, "imported", n);
			if (cand.empty() || taken.count(lowerAscii(cand)))
				continue;
			if (namePermitted(owner, sourceName, cand.c_str()))
			{
				copyText(targetOut, targetCap, cand.c_str());
				return CE_PUBLISH_COMPLETE;
			}
			sawNameRefusal = true;
		}
		return sawNameRefusal ? CE_PUBLISH_BAD_NAME : CE_PUBLISH_OCCUPIED;
	}

	const SuffixBase base = stripCopySuffix(clean, "edited");
	for (unsigned n = base.next; n < base.next + 1000 && n <= 9999; ++n)
	{
		const std::string cand = formatCandidate(base.stem, "edited", n);
		if (cand.empty() || taken.count(lowerAscii(cand)))
			continue;
		if (namePermitted(owner, sourceName, cand.c_str()))
		{
			copyText(targetOut, targetCap, cand.c_str());
			return CE_PUBLISH_COMPLETE;
		}
		sawNameRefusal = true;
	}
	return sawNameRefusal ? CE_PUBLISH_BAD_NAME : CE_PUBLISH_OCCUPIED;
}

CeCareerPublishResult	CeCareerPublishNew(const CeCareerPublishRequest *request)
{
	CeCareerPublishResult out{};
	out.status = CE_PUBLISH_REFUSED;
	if (!request)
		return out;

	bool published = false;
	try
	{
		require(request->permitted && request->permitted(request->owner), CE_PUBLISH_AUTHORITY);
		require(!(request->cancelled && request->cancelled(request->owner)), CE_PUBLISH_CANCELLED);
		require(request->backupRoot && *request->backupRoot, CE_PUBLISH_NO_FOLDER);
		require(request->bytes && request->size == CE_CAREER_SIZE, CE_PUBLISH_BAD_CAREER);
		require(request->sourceName && *request->sourceName && request->targetName && *request->targetName,
				CE_PUBLISH_BAD_NAME);
		const std::string sourceName = request->sourceName;
		const std::string target = request->targetName;
		require(portableCareerName(target), CE_PUBLISH_BAD_NAME);
		require(request->namePermitted && request->namePermitted(request->owner, sourceName.c_str(), target.c_str()),
				CE_PUBLISH_BAD_NAME);

		SCeCareerView view{};
		require(CeCareerViewBuild(request->bytes, CE_CAREER_SIZE, &view) && CeCareerViewPlausible(view),
				CE_PUBLISH_BAD_CAREER);

		const std::string user = trimmedPath(request->userDir);
		const std::string game = trimmedPath(request->gameDir);
		const std::string pkg = (request->packageDir && *request->packageDir)
									? trimmedPath(request->packageDir)
									: trimmedPath(SDL_GetBasePath());
		const std::string root = trimmedPath(request->backupRoot);
		require(!user.empty() && !game.empty() && !pkg.empty() && !root.empty());

		// Pre-flight validation before taking the full-set backup: reject invalid directories,
		// non-local storage, protected backup roots, or already-occupied names without creating a backup.
		{
			Fd userFd(requireDir(user, CE_PUBLISH_REFUSED, true));
			Fd gameFd(requireDir(game, CE_PUBLISH_REFUSED, true));
			Fd pkgFd(requireDir(pkg, CE_PUBLISH_REFUSED, true));
			Fd rootFd(requireDir(root, CE_PUBLISH_REFUSED, false));
			bool slotsPresent = false, origPresent = false;
			Fd slotsFd(openOptionalSavegames(userFd.n, slotsPresent));
			Fd origFd(openOptionalSavegames(gameFd.n, origPresent));
			require(localFs(userFd.n) && localFs(gameFd.n) && localFs(rootFd.n) &&
						(!slotsPresent || localFs(slotsFd.n)) && (!origPresent || localFs(origFd.n)),
					CE_PUBLISH_UNSUPPORTED);
			for (int protectedDir : {userFd.n, gameFd.n, pkgFd.n})
				require(!isAncestorOrSame(protectedDir, rootFd.n) && !isAncestorOrSame(rootFd.n, protectedDir));
			require(!isAncestorOrSame(userFd.n, gameFd.n) && !isAncestorOrSame(gameFd.n, userFd.n));
			std::set<std::string> taken;
			require(collectCareerNames(userFd.n, true, taken) && collectCareerNames(gameFd.n, true, taken));
			require(!taken.count(lowerAscii(target)), CE_PUBLISH_OCCUPIED);
			require(!entryExists(slotsFd.n, target) && !entryExists(origFd.n, target), CE_PUBLISH_OCCUPIED);
		}

		// Mandatory verified full-set backup FIRST. If it does not complete, refuse with zero writes.
		CeCareerStorageRequest storageReq;
		storageReq.user = user;
		storageReq.game = game;
		storageReq.package = pkg;
		storageReq.backupRoot = root;
		storageReq.permitted = request->permitted;
		storageReq.cancelled = request->cancelled;
		storageReq.owner = request->owner;
		const CeCareerStorageResult backupRes = CeCareerTakeFullSet(storageReq);
		if (!backupRes.backupSet.empty())
			copyText(out.backupSet, sizeof(out.backupSet), backupRes.backupSet.c_str());
		if (backupRes.status != CeCareerStorageStatus::Complete)
		{
			out.status = mapStorageStatus(backupRes.status);
			return out;
		}
		require(!backupRes.backupSet.empty() && !backupRes.backupIdentity.empty() && !backupRes.backupStamp.empty(),
				CE_PUBLISH_UNCERTAIN);

		// Re-open roots and take the exclusive custody lock for the atomic publish step.
		Fd userFd(requireDir(user, CE_PUBLISH_REFUSED, true));
		Fd gameFd(requireDir(game, CE_PUBLISH_REFUSED, true));
		Fd pkgFd(requireDir(pkg, CE_PUBLISH_REFUSED, true));
		Fd rootFd(requireDir(root, CE_PUBLISH_REFUSED, false));
		bool slotsPresent = false, origPresent = false;
		Fd slotsFd(openOptionalSavegames(userFd.n, slotsPresent));
		Fd origFd(openOptionalSavegames(gameFd.n, origPresent));
		require(localFs(userFd.n) && localFs(gameFd.n) && localFs(rootFd.n) &&
					(!slotsPresent || localFs(slotsFd.n)) && (!origPresent || localFs(origFd.n)),
				CE_PUBLISH_UNSUPPORTED);
		for (int protectedDir : {userFd.n, gameFd.n, pkgFd.n})
			require(!isAncestorOrSame(protectedDir, rootFd.n) && !isAncestorOrSame(rootFd.n, protectedDir));
		require(!isAncestorOrSame(userFd.n, gameFd.n) && !isAncestorOrSame(gameFd.n, userFd.n));

		Fd lockFd(openat(userFd.n, ".ce-career-custody.lock",
						 O_RDWR | O_CLOEXEC | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600));
		require(lockFd.n >= 0);
		struct stat ls = statFd(lockFd.n);
		require(S_ISREG(ls.st_mode) && ls.st_nlink == 1 && ls.st_uid == geteuid());
		if (flock(lockFd.n, LOCK_EX | LOCK_NB) != 0)
			throw Failure{errno == EWOULDBLOCK ? CE_PUBLISH_BUSY : CE_PUBLISH_REFUSED};

		auto verifyRoots = [&]()
		{
			require(boundDir(userFd.n, user, true) && boundDir(gameFd.n, game, true) &&
						boundDir(pkgFd.n, pkg, true) && boundDir(rootFd.n, root, false),
					CE_PUBLISH_CHANGED);
			if (slotsPresent)
			{
				Fd a(openat(userFd.n, "savegames", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
				require(a.n >= 0 && sameStat(statFd(a.n), statFd(slotsFd.n)), CE_PUBLISH_CHANGED);
			}
			else
				require(savegamesAbsent(userFd.n), CE_PUBLISH_CHANGED);
			if (origPresent)
			{
				Fd b(openat(gameFd.n, "savegames", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
				require(b.n >= 0 && sameStat(statFd(b.n), statFd(origFd.n)), CE_PUBLISH_CHANGED);
			}
			else
				require(savegamesAbsent(gameFd.n), CE_PUBLISH_CHANGED);
			struct stat held{};
			require(fstatat(userFd.n, ".ce-career-custody.lock", &held, AT_SYMLINK_NOFOLLOW) == 0 &&
						sameStat(held, statFd(lockFd.n)) && held.st_nlink == 1,
					CE_PUBLISH_CHANGED);
		};

		auto verifyBackup = [&](bool allowCancel)
		{
			CeCareerRestoreRequest stampReq;
			stampReq.user = user;
			stampReq.game = game;
			stampReq.package = pkg;
			stampReq.backupRoot = root;
			stampReq.permitted = request->permitted;
			stampReq.cancelled = allowCancel ? request->cancelled : nullptr;
			stampReq.owner = request->owner;
			stampReq.sourceSet = backupRes.backupSet;
			stampReq.sourceIdentity = backupRes.backupIdentity;
			std::string stampNow;
			require(CeCareerRestoreStamp(stampReq, stampNow) && stampNow == backupRes.backupStamp,
					CE_PUBLISH_CHANGED);
			CePreviewSet checked;
			std::string error;
			require(CePreviewRead(backupRes.backupSet, user, game, pkg, checked, error) &&
						checked.identity == backupRes.backupIdentity,
					CE_PUBLISH_CHANGED);
		};

		verifyRoots();
		verifyBackup(true);
		require(request->permitted(request->owner), CE_PUBLISH_AUTHORITY);
		require(!(request->cancelled && request->cancelled(request->owner)), CE_PUBLISH_CANCELLED);
		require(request->namePermitted(request->owner, sourceName.c_str(), target.c_str()), CE_PUBLISH_BAD_NAME);
		{
			std::set<std::string> preStageTaken;
			require(collectCareerNames(userFd.n, true, preStageTaken) &&
					collectCareerNames(gameFd.n, true, preStageTaken));
			require(!preStageTaken.count(lowerAscii(target)), CE_PUBLISH_OCCUPIED);
			require(!entryExists(slotsFd.n, target) && !entryExists(origFd.n, target), CE_PUBLISH_OCCUPIED);
		}
		if (!slotsPresent)
		{
			if (mkdirat(userFd.n, "savegames", 0700) != 0)
				throw Failure{errno == EEXIST ? CE_PUBLISH_CHANGED : CE_PUBLISH_REFUSED};
			Fd created(openat(userFd.n, "savegames", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW));
			require(created.n >= 0, CE_PUBLISH_CHANGED);
			const struct stat createdSt = statFd(created.n);
			struct stat byName{};
			require(S_ISDIR(createdSt.st_mode) &&
						fstatat(userFd.n, "savegames", &byName, AT_SYMLINK_NOFOLLOW) == 0 &&
						sameStat(createdSt, byName),
					CE_PUBLISH_CHANGED);
			require(localFs(created.n), CE_PUBLISH_UNSUPPORTED);
			require(fsync(userFd.n) == 0);
			slotsFd.n = created.n;
			created.n = -1;
			slotsPresent = true;
		}

		// Stage into an anonymous O_TMPFILE inode inside user/savegames, fsync, and verify read-back.
		Fd stage(openat(slotsFd.n, ".", O_RDWR | O_TMPFILE | O_CLOEXEC, 0600));
		require(stage.n >= 0, CE_PUBLISH_UNSUPPORTED);
		writeFullSync(stage.n, request->bytes, CE_CAREER_SIZE);
		require(readBackMatches(stage.n, request->bytes, CE_CAREER_SIZE));

		std::set<std::string> taken;
		require(collectCareerNames(userFd.n, false, taken) && collectCareerNames(gameFd.n, true, taken));
		require(!taken.count(lowerAscii(target)), CE_PUBLISH_OCCUPIED);
		require(!entryExists(slotsFd.n, target) && !entryExists(origFd.n, target), CE_PUBLISH_OCCUPIED);
		verifyRoots();
		verifyBackup(true);
		require(request->permitted(request->owner), CE_PUBLISH_AUTHORITY);
		require(!(request->cancelled && request->cancelled(request->owner)), CE_PUBLISH_CANCELLED);
		require(request->namePermitted(request->owner, sourceName.c_str(), target.c_str()), CE_PUBLISH_BAD_NAME);

		// Atomic non-replacing publication: linkat with AT_EMPTY_PATH never overwrites an existing entry.
		if (linkat(stage.n, "", slotsFd.n, target.c_str(), AT_EMPTY_PATH) != 0)
			throw Failure{errno == EEXIST ? CE_PUBLISH_OCCUPIED : CE_PUBLISH_REFUSED};
		published = true;
		copyText(out.newFile, sizeof(out.newFile), target.c_str());
		out.possible = 1;

		require(fsync(slotsFd.n) == 0, CE_PUBLISH_UNCERTAIN);
		Fd finalFd(openat(slotsFd.n, target.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
		require(finalFd.n >= 0 && sameStat(statFd(finalFd.n), statFd(stage.n)) &&
					statFd(finalFd.n).st_nlink == 1,
				CE_PUBLISH_UNCERTAIN);
		require(readBackMatches(finalFd.n, request->bytes, CE_CAREER_SIZE), CE_PUBLISH_UNCERTAIN);
		verifyBackup(false);
		verifyRoots();
		{
			std::set<std::string> postUser, postGame;
			require(collectCareerNames(userFd.n, false, postUser) &&
						collectCareerNames(gameFd.n, true, postGame),
					CE_PUBLISH_UNCERTAIN);
			const std::string foldedTarget = lowerAscii(target);
			require(postUser.count(foldedTarget) == 1 && postGame.count(foldedTarget) == 0 &&
						!entryExists(origFd.n, target),
					CE_PUBLISH_UNCERTAIN);
		}
		require(request->permitted(request->owner), CE_PUBLISH_UNCERTAIN);
		require(request->namePermitted(request->owner, sourceName.c_str(), target.c_str()), CE_PUBLISH_UNCERTAIN);
		out.status = CE_PUBLISH_COMPLETE;
	}
	catch (const Failure &error)
	{
		out.status = published ? CE_PUBLISH_UNCERTAIN : error.status;
	}
	catch (...)
	{
		out.status = published ? CE_PUBLISH_UNCERTAIN : CE_PUBLISH_REFUSED;
	}
	return out;
}
