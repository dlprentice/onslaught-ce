/* SPDX-License-Identifier: GPL-3.0-only */
/* port/updater/ce_update_v2.h: Onslaught CE's second updater (UPDATER-V2.md), included once by ce_launch.c after its
 * helpers (say, ini_get/ini_set, sha256_file, hex_to, set_env, log_close and friends).
 *
 * Rules, in the order the code applies them:
 *  - The feed is a NEW asset, ce-update-v2.json + .sig, that no 0.1.0-0.1.6 launcher fetches (they fetch only
 *    releases/latest/download/latest.json). Its signature is Ed25519 over UPD_SIG_DOMAIN followed by the exact feed
 *    bytes, so a v2 feed and signature never verify as an old latest.json pair, and an old pair never verifies here.
 *  - The signature is checked before the feed is parsed; the package's size and SHA-256 come from the signed feed and
 *    are both checked before anything is unpacked.
 *  - Downloads go to a stage directory this run created inside <user folder>/update-v2/. Nothing is written beside the
 *    installation: a new version is unpacked into <install>/versions/v<version>-<random>/ (created exclusively by
 *    this run and recorded in <install>/versions/state before anything is written into it), and becomes active when
 *    the state file is replaced atomically (Linux renameat2 RENAME_EXCHANGE, Windows MoveFileEx over a state file
 *    whose identity was just checked). The base folder's own files are never modified.
 *  - Nothing is ever deleted by name pattern. The updater deletes only (a) what this run created, and (b) paths its
 *    own records name - the state's entries, and the journal in each folder's lock file - after checking device and
 *    inode (Windows volume serial and file index), refusing symlinks, junctions and other reparse points, and refusing
 *    a tree that holds anything but regular files and directories on the same device.
 *  - The player is asked (Update, Later, Skip this version); BEA_NO_UPDATE_CHECK=1, update_check=0 in ce.ini or
 *    --no-update-check turn checking off; an unreachable feed is silent. */
#ifndef CE_UPDATE_V2_H
#define CE_UPDATE_V2_H
#include <zlib.h>
#include <inttypes.h>
#ifdef _WIN32
#include <winhttp.h>
#include <bcrypt.h>
#else
#include <sys/syscall.h>
#include <sys/types.h>
#endif

#define UPD_FEED_NAME "ce-update-v2.json"
#define UPD_FEED_URL "https://github.com/dlprentice/onslaught-ce/releases/latest/download/" UPD_FEED_NAME
#define UPD_FORMAT "onslaught-ce-update-v2"
#define UPD_SIG_DOMAIN "onslaught-ce update feed v2\n"
#define UPD_FEED_MAX 16384u
#define UPD_PACKAGE_MAX (512u << 20)
#define UPD_UNPACK_MAX (1024u << 20)
#define UPD_ENTRIES_MAX 4096u
#define UPD_STATE_MAGIC "onslaught-ce-updater-state 2"
#define UPD_JOURNAL_MAGIC "onslaught-ce-updater-journal 2"
#define UPD_MAX_VERSIONS 16
#ifdef _WIN32
#define UPD_EXT "zip"
#define UPD_LAUNCHER "onslaught-ce.exe"
#else
#define UPD_EXT "tar.gz"
#define UPD_LAUNCHER "onslaught-ce"		/* the package's wrapper script, which runs onslaught-ce-launch */
#endif

#ifdef CE_UPDATER_TESTING
#ifndef CE_UPDATER_TEST_KEY
#error "an updater test build pins its own test public key (CE_UPDATER_TEST_KEY)"
#endif
/* Test builds pin a test key and say so in --version; package.py refuses a launcher carrying this marker. */
static const char UPD_KEY_HEX[] = CE_UPDATER_TEST_KEY;
#define UPD_TEST_NOTE " [CE-UPDATER-TEST-BUILD]"
#else
#define UPD_KEY_HEX PUBLIC_KEY_HEX
#define UPD_TEST_NOTE ""
#endif

/* --update-only's exit status (UPDATER-V2.md). */
enum
{
	UPD_UPDATED = 0, UPD_CURRENT = 10, UPD_NOT_NOW = 11, UPD_DISABLED = 12, UPD_OFFLINE = 13, UPD_BAD_FEED = 14,
	UPD_BAD_PACKAGE = 15, UPD_INSTALL_FAILED = 16, UPD_BUSY = 17
};

static char sRoot[PATHN];		/* the installation: the base folder, whichever version is running */
static char sRunningEntry[64];	/* the versions/ entry this launcher runs from, or "" for the base folder */
static int sRootUsable;			/* 0: this copy is not in a layout the updater can manage */

/* ---- test hooks (compiled only into updater test builds) ------------------------------------------------------ */

static void upd_crash_point(const char *step)
{
#ifdef CE_UPDATER_TESTING
	/* "step" or "step:N" (the Nth time this run reaches it) */
	static int hits[16];
	static const char *seen[16];
	int slot = 0;
	while (slot < 15 && seen[slot] && strcmp(seen[slot], step)) ++slot;
	seen[slot] = step;
	int hit = ++hits[slot];
	const char *at = getenv("BEA_UPDATE_TEST_CRASH_AT");
	size_t sl = strlen(step);
	if (at && !strncmp(at, step, sl) && (at[sl] == 0 ? hit == 1 : at[sl] == ':' && atoi(at + sl + 1) == hit))
	{
		fprintf(stderr, "onslaught-ce: test crash at %s\n", step);
		fflush(stderr);
#ifdef _WIN32
		TerminateProcess(GetCurrentProcess(), 137);
#else
		raise(SIGKILL);
#endif
	}
#else
	(void)step;
#endif
}

static uint64_t sUpdWritten;
/* 1 when n more bytes may be written; test builds can simulate a full disk (BEA_UPDATE_TEST_WRITE_LIMIT bytes). */
static int upd_write_budget(size_t n)
{
#ifdef CE_UPDATER_TESTING
	const char *limit = getenv("BEA_UPDATE_TEST_WRITE_LIMIT");
	if (limit && *limit && sUpdWritten + n > strtoull(limit, NULL, 10)) return 0;
#endif
	sUpdWritten += n;
	return 1;
}

/* ---- small helpers -------------------------------------------------------------------------------------------- */

static int upd_join(char *out, size_t n, const char *a, const char *b)
{
	int k = snprintf(out, n, "%s" SEP "%s", a, b);
	return k > 0 && (size_t)k < n;
}

/* Names the updater makes or accepts in its own records: letters, digits, '.', '-', not starting with '.'. */
static int upd_name_ok(const char *s)
{
	size_t n = strlen(s);
	if (!n || n > 63 || s[0] == '.') return 0;
	for (size_t i = 0; i < n; ++i)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
			  s[i] == '.' || s[i] == '-')) return 0;
	return 1;
}

static int upd_version_ok(const char *v);

/* Exactly 16 lowercase hex digits and the end of the string. */
static int upd_hex16_end(const char *p)
{
	for (int i = 0; i < 16; ++i)
		if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) return 0;
	return p[16] == 0;
}

/* The only names the updater ever creates, so the only ones its records may name:
 * state.new-<16 hex>, stage-<16 hex>, and v<version>-<16 hex> (with that version when one is given). */
static int upd_tmp_name_ok(const char *n) { return !strncmp(n, "state.new-", 10) && upd_hex16_end(n + 10); }
static int upd_stage_name_ok(const char *n) { return !strncmp(n, "stage-", 6) && upd_hex16_end(n + 6); }
static int upd_entry_name_ok(const char *n, const char *version)
{
	size_t l = strlen(n);
	char v[32];
	if (l < 1 + 1 + 1 + 16 || l - 18 >= sizeof v || n[0] != 'v' || n[l - 17] != '-' || !upd_hex16_end(n + l - 16)) return 0;
	memcpy(v, n + 1, l - 18);
	v[l - 18] = 0;
	return upd_version_ok(v) && (!version || !strcmp(v, version));
}

/* One to four numeric parts of one to five digits ("0.1.8", "0.1.8.1"). */
static int upd_version_ok(const char *v)
{
	int parts = 0, digits = 0;
	for (const char *p = v;; ++p)
	{
		if (*p >= '0' && *p <= '9') { if (++digits > 5) return 0; continue; }
		if (!digits) return 0;
		++parts; digits = 0;
		if (!*p) break;
		if (*p != '.' || parts == 4) return 0;
	}
	return parts >= 1 && parts <= 4;
}

static int upd_version_cmp(const char *a, const char *b)
{
	for (int i = 0; i < 4; ++i)
	{
		unsigned long x = strtoul(a, (char **)&a, 10), y = strtoul(b, (char **)&b, 10);
		if (x != y) return x < y ? -1 : 1;
		if (*a == '.') ++a;
		if (*b == '.') ++b;
	}
	return 0;
}

static int upd_random_hex(char out[17])
{
	uint8_t r[8];
	int ok = 0;
#ifdef _WIN32
	ok = BCryptGenRandom(NULL, r, sizeof r, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
#ifdef SYS_getrandom
	ok = syscall(SYS_getrandom, r, sizeof r, 0) == (long)sizeof r;
#endif
	if (!ok)
	{
		int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
		ok = fd >= 0 && read(fd, r, sizeof r) == (ssize_t)sizeof r;
		if (fd >= 0) close(fd);
	}
#endif
	if (!ok) return 0;
	for (int i = 0; i < 8; ++i) snprintf(out + 2 * i, 3, "%02x", r[i]);
	return 1;
}

/* ---- file identity -------------------------------------------------------------------------------------------- */

typedef struct { uint64_t dev, ino; } UpdId;
enum { UPD_ABSENT = 0, UPD_DIR = 1, UPD_FILE = 2, UPD_OTHER = 3 };

static int upd_same(const UpdId *a, const UpdId *b) { return a->dev == b->dev && a->ino == b->ino; }

/* What path itself is, never following a final link: ABSENT, DIR, FILE (regular, one link), OTHER (a symlink, junction
 * or other reparse point, device, multiply linked file), or -1 when it cannot be inspected. */
static int upd_kind(const char *path, UpdId *id)
{
#ifdef _WIN32
	DWORD attr = GetFileAttributesA(path);
	if (attr == INVALID_FILE_ATTRIBUTES)
	{
		DWORD e = GetLastError();
		return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? UPD_ABSENT : -1;
	}
	if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return UPD_OTHER;
	HANDLE h = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h == INVALID_HANDLE_VALUE) return -1;
	BY_HANDLE_FILE_INFORMATION info;
	int ok = GetFileInformationByHandle(h, &info);
	CloseHandle(h);
	if (!ok) return -1;
	if (id)
	{
		id->dev = info.dwVolumeSerialNumber;
		id->ino = (uint64_t)info.nFileIndexHigh << 32 | info.nFileIndexLow;
	}
	if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return UPD_OTHER;
	if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return UPD_DIR;
	return info.nNumberOfLinks == 1 ? UPD_FILE : UPD_OTHER;
#else
	struct stat st;
	if (lstat(path, &st)) return errno == ENOENT ? UPD_ABSENT : -1;
	if (id) { id->dev = (uint64_t)st.st_dev; id->ino = (uint64_t)st.st_ino; }
	if (S_ISDIR(st.st_mode)) return UPD_DIR;
	return S_ISREG(st.st_mode) && st.st_nlink == 1 ? UPD_FILE : UPD_OTHER;
#endif
}

/* 1 created, 0 something already has the name, -1 error. */
static int upd_mkdir(const char *path, int private_dir)
{
#ifdef _WIN32
	(void)private_dir;
	if (CreateDirectoryA(path, NULL)) return 1;
	return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
#else
	if (!mkdir(path, private_dir ? 0700 : 0755)) return 1;
	return errno == EEXIST ? 0 : -1;
#endif
}

static int upd_unlink_file(const char *path)
{
#ifdef _WIN32
	SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
	return DeleteFileA(path) != 0;
#else
	return !unlink(path);
#endif
}

static int upd_rmdir(const char *path)
{
#ifdef _WIN32
	return RemoveDirectoryA(path) != 0;
#else
	return !rmdir(path);
#endif
}

static void upd_sync_dir(const char *dir)
{
#ifndef _WIN32
	int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd >= 0) { fsync(fd); close(fd); }
#else
	(void)dir;
#endif
}

/* ---- exclusively created output files ------------------------------------------------------------------------- */

typedef struct
{
#ifdef _WIN32
	HANDLE h;
#else
	int fd;
#endif
	int failed;
} UpdFile;

static int upd_file_create(UpdFile *f, const char *path, int exec)
{
	f->failed = 0;
#ifdef _WIN32
	(void)exec;
	f->h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f->h == INVALID_HANDLE_VALUE) return 0;
	SetHandleInformation(f->h, HANDLE_FLAG_INHERIT, 0);
	return 1;
#else
	f->fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, exec ? 0755 : 0644);
	return f->fd >= 0;
#endif
}

static int upd_file_write(UpdFile *f, const void *p, size_t n)
{
	if (f->failed) return 0;
	if (!upd_write_budget(n)) { f->failed = 1; return 0; }
#ifdef _WIN32
	DWORD done = 0;
	if (!WriteFile(f->h, p, (DWORD)n, &done, NULL) || done != n) f->failed = 1;
#else
	for (const char *c = p; n && !f->failed;)
	{
		ssize_t k = write(f->fd, c, n);
		if (k < 0 && errno == EINTR) continue;
		if (k <= 0) { f->failed = 1; break; }
		c += k; n -= (size_t)k;
	}
#endif
	return !f->failed;
}

/* Flushes to the disk and closes; 0 when any write, the flush or the close failed. */
static int upd_file_close(UpdFile *f)
{
#ifdef _WIN32
	if (!FlushFileBuffers(f->h)) f->failed = 1;
	if (!CloseHandle(f->h)) f->failed = 1;
#else
	if (fsync(f->fd)) f->failed = 1;
	if (close(f->fd)) f->failed = 1;
#endif
	return !f->failed;
}

static int upd_write_new(const char *path, const void *data, size_t n)
{
	UpdFile f;
	if (!upd_file_create(&f, path, 0)) return 0;
	upd_file_write(&f, data, n);
	if (upd_file_close(&f)) return 1;
	upd_unlink_file(path);	/* this run created it exclusively */
	return 0;
}

/* A whole small regular file (not a link), at most max bytes. */
static char *upd_read_small(const char *path, size_t max, size_t *len)
{
	if (upd_kind(path, NULL) != UPD_FILE) return NULL;
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	char *b = malloc(max + 1);
	size_t n = b ? fread(b, 1, max + 1, f) : 0;
	int bad = !b || ferror(f) || n > max;
	fclose(f);
	if (bad) { free(b); return NULL; }
	b[n] = 0;
	if (len) *len = n;
	return b;
}

/* ---- lock files and their journals ---------------------------------------------------------------------------- */

/* Each updater folder has one lock file, created once and never deleted. While its lock is held it is also the
 * journal: before this run creates a temporary name, it writes the name there, so a later run can remove exactly
 * that name after a crash, and nothing else. */
typedef struct
{
#ifdef _WIN32
	HANDLE h;
#else
	int fd;
#endif
	int held;
} UpdLock;

typedef struct
{
	char tmp[64];			/* a temporary state file being written */
	char dir[64];			/* a versions/ entry created but perhaps not yet recorded in the state */
	char stage[64];			/* user folder: this run's stage directory */
	UpdId stage_id;
	int stage_has_id;
} UpdJournal;

/* 1 held, 0 another launcher holds it, -1 refused (a link, a foreign file) or failed. */
static int upd_lock(UpdLock *l, const char *dir)
{
	char path[PATHN];
	l->held = 0;
	if (!upd_join(path, sizeof path, dir, "lock")) return -1;
	int kind = upd_kind(path, NULL);
	if (kind != UPD_ABSENT && kind != UPD_FILE) return -1;
#ifdef _WIN32
	l->h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (l->h == INVALID_HANDLE_VALUE) return -1;
	BY_HANDLE_FILE_INFORMATION info;
	if (!GetFileInformationByHandle(l->h, &info) || info.nNumberOfLinks != 1 ||
		(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
	{ CloseHandle(l->h); return -1; }
	SetHandleInformation(l->h, HANDLE_FLAG_INHERIT, 0);
	OVERLAPPED at = {0};
	at.Offset = 0x7fffffff;	/* a byte past any journal: the journal itself stays writable through this handle */
	if (!LockFileEx(l->h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at))
	{
		DWORD e = GetLastError();
		CloseHandle(l->h);
		return e == ERROR_LOCK_VIOLATION || e == ERROR_IO_PENDING ? 0 : -1;
	}
#else
	l->fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (l->fd < 0) return -1;
	struct stat st;
	if (fstat(l->fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1) { close(l->fd); return -1; }
	if (flock(l->fd, LOCK_EX | LOCK_NB))
	{
		int e = errno;
		close(l->fd);
		return e == EWOULDBLOCK || e == EAGAIN ? 0 : -1;
	}
#endif
	l->held = 1;
	return 1;
}

static void upd_unlock(UpdLock *l)
{
	if (!l->held) return;
#ifdef _WIN32
	OVERLAPPED at = {0};
	at.Offset = 0x7fffffff;
	UnlockFileEx(l->h, 0, 1, 0, &at);
	CloseHandle(l->h);
#else
	flock(l->fd, LOCK_UN);
	close(l->fd);
#endif
	l->held = 0;
}

static int upd_journal_write(UpdLock *l, const UpdJournal *j)
{
	char text[512];
	int n = snprintf(text, sizeof text, UPD_JOURNAL_MAGIC "\n");
	if (*j->tmp) n += snprintf(text + n, sizeof text - (size_t)n, "tmp %s\n", j->tmp);
	if (*j->dir) n += snprintf(text + n, sizeof text - (size_t)n, "dir %s\n", j->dir);
	if (*j->stage)
	{
		if (j->stage_has_id)
			n += snprintf(text + n, sizeof text - (size_t)n, "stage %s %" PRIu64 "\n", j->stage, j->stage_id.ino);
		else n += snprintf(text + n, sizeof text - (size_t)n, "stage %s\n", j->stage);
	}
	if (n <= 0 || (size_t)n >= sizeof text) return 0;
#ifdef _WIN32
	DWORD done = 0;
	LARGE_INTEGER zero = {0};
	return SetFilePointerEx(l->h, zero, NULL, FILE_BEGIN) && WriteFile(l->h, text, (DWORD)n, &done, NULL) &&
		done == (DWORD)n && SetEndOfFile(l->h) && FlushFileBuffers(l->h);
#else
	return pwrite(l->fd, text, (size_t)n, 0) == n && !ftruncate(l->fd, n) && !fsync(l->fd);
#endif
}

/* A journal that cannot be read or parsed names nothing: then nothing is removed. */
static void upd_journal_read(UpdLock *l, UpdJournal *j)
{
	memset(j, 0, sizeof *j);
	char text[513];
	int n = 0;
#ifdef _WIN32
	DWORD got = 0;
	LARGE_INTEGER zero = {0};
	if (SetFilePointerEx(l->h, zero, NULL, FILE_BEGIN) && ReadFile(l->h, text, sizeof text - 1, &got, NULL)) n = (int)got;
#else
	ssize_t got = pread(l->fd, text, sizeof text - 1, 0);
	n = got > 0 ? (int)got : 0;
#endif
	text[n] = 0;
	if (strncmp(text, UPD_JOURNAL_MAGIC "\n", sizeof UPD_JOURNAL_MAGIC)) return;
	for (char *line = text + sizeof UPD_JOURNAL_MAGIC, *next; line && *line; line = next)
	{
		next = strchr(line, '\n');
		if (!next) break;	/* a torn last line is ignored */
		*next++ = 0;
		char word[16], name[64];
		uint64_t ino;
		int k = sscanf(line, "%15s %63s %" SCNu64, word, name, &ino);
		if (k < 2) continue;
		if (!strcmp(word, "tmp") && upd_tmp_name_ok(name)) snprintf(j->tmp, sizeof j->tmp, "%s", name);
		else if (!strcmp(word, "dir") && upd_entry_name_ok(name, NULL)) snprintf(j->dir, sizeof j->dir, "%s", name);
		else if (!strcmp(word, "stage") && upd_stage_name_ok(name))
		{
			snprintf(j->stage, sizeof j->stage, "%s", name);
			if (k == 3) { j->stage_id.ino = ino; j->stage_has_id = 1; }	/* the device: the stage area's, now */
		}
	}
}

/* ---- removing a tree this updater made ------------------------------------------------------------------------ */

/* Why a removal was refused (for the log); set by the scan. */
static const char *sUpdRefusal;

enum { UPD_RM_REMOVED, UPD_RM_ABSENT, UPD_RM_NOT_OURS, UPD_RM_FOREIGN, UPD_RM_IN_USE, UPD_RM_FAILED };

#ifndef _WIN32
/* statx (Linux 5.8+): the mount a name is on, and whether it is a mount's root. struct statx's fixed 256-byte ABI. */
struct upd_statx
{
	uint32_t mask, blksize;
	uint64_t attributes;
	uint32_t nlink, uid, gid;
	uint16_t mode, spare0;
	uint64_t ino, size, blocks, attributes_mask;
	struct { int64_t sec; uint32_t nsec; int32_t spare; } atime, btime, ctime, mtime;
	uint32_t rdev_major, rdev_minor, dev_major, dev_minor;
	uint64_t mnt_id;
	uint64_t spare2[13];
};
#define UPD_STATX_MNT_ID 0x1000u
#define UPD_STATX_ATTR_MOUNT_ROOT 0x2000u

/* 1 with *mnt: name (not followed) is on mount *mnt and is not a mount's root. 0: a mount root, or this system
 * cannot say (then nothing is removed: a same-device bind mount is otherwise indistinguishable). */
static int upd_mount_of(int dirfd, const char *name, uint64_t *mnt)
{
#ifdef SYS_statx
	struct upd_statx x;
	memset(&x, 0, sizeof x);
	if (syscall(SYS_statx, dirfd, name, AT_SYMLINK_NOFOLLOW | 0x800 /* AT_NO_AUTOMOUNT */, UPD_STATX_MNT_ID, &x))
	{
		sUpdRefusal = "its mount points could not be checked (statx failed)";
		return 0;
	}
	if (!(x.mask & UPD_STATX_MNT_ID) || !(x.attributes_mask & UPD_STATX_ATTR_MOUNT_ROOT))
	{
		sUpdRefusal = "this system cannot report mount points (Linux 5.8 or newer is needed to remove old versions)";
		return 0;
	}
	if (x.attributes & UPD_STATX_ATTR_MOUNT_ROOT) { sUpdRefusal = "it holds a mount point"; return 0; }
	*mnt = x.mnt_id;
	return 1;
#else
	(void)dirfd; (void)name; (void)mnt;
	sUpdRefusal = "this build cannot check mount points";
	return 0;
#endif
}

/* scan: check only; otherwise delete. Only regular files with one link and directories on the tree's own device and
 * mount; a link, a mount point, a device, a FIFO, a hard-linked file or anything else makes the whole removal refuse
 * (the scan runs first and completely, so nothing is half removed). */
static int upd_tree_at(int parent, const char *name, dev_t dev, uint64_t mnt, int depth, int scan)
{
	struct stat st;
	uint64_t here;
	if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) { sUpdRefusal = "an item could not be inspected"; return 0; }
	if (depth > 24) { sUpdRefusal = "it is nested too deeply"; return 0; }
	if (S_ISLNK(st.st_mode)) { sUpdRefusal = "it holds a symbolic link"; return 0; }
	if (st.st_dev != dev) { sUpdRefusal = "it holds another file system"; return 0; }
	if (!upd_mount_of(parent, name, &here)) return 0;
	if (here != mnt) { sUpdRefusal = "it holds a mount point"; return 0; }
	if (S_ISREG(st.st_mode))
	{
		if (st.st_nlink != 1) { sUpdRefusal = "it holds a hard-linked file"; return 0; }
		if (scan) return 1;
		if (unlinkat(parent, name, 0)) { sUpdRefusal = "a file could not be removed"; return 0; }
		return 1;
	}
	if (!S_ISDIR(st.st_mode)) { sUpdRefusal = "it holds a device, FIFO or socket"; return 0; }
	for (int pass = 0; pass < 3; ++pass)
	{
		int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (fd < 0) { sUpdRefusal = "a folder could not be opened"; return 0; }
		struct stat again;
		if (fstat(fd, &again) || again.st_dev != st.st_dev || again.st_ino != st.st_ino)
		{ close(fd); sUpdRefusal = "a folder changed while it was checked"; return 0; }
		DIR *d = fdopendir(fd);
		if (!d) { close(fd); sUpdRefusal = "a folder could not be read"; return 0; }
		int ok = 1;
		struct dirent *e;
		while (ok && (e = readdir(d)))
			if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
				ok = upd_tree_at(fd, e->d_name, dev, mnt, depth + 1, scan);
		closedir(d);
		if (!ok) return 0;
		if (scan) return 1;
		if (!unlinkat(parent, name, AT_REMOVEDIR)) return 1;
		if (errno != ENOTEMPTY && errno != EEXIST) { sUpdRefusal = "a folder could not be removed"; return 0; }
	}
	sUpdRefusal = "a folder kept refilling";
	return 0;
}
#else
static int upd_tree_win(const char *path, int depth, int scan)
{
	DWORD attr = GetFileAttributesA(path);
	if (attr == INVALID_FILE_ATTRIBUTES) { sUpdRefusal = "an item could not be inspected"; return 0; }
	if (attr & FILE_ATTRIBUTE_REPARSE_POINT) { sUpdRefusal = "it holds a link, junction or mount point"; return 0; }
	if (depth > 24) { sUpdRefusal = "it is nested too deeply"; return 0; }
	if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		if (scan || upd_unlink_file(path)) return 1;
		sUpdRefusal = "a file could not be removed";
		return 0;
	}
	char pattern[PATHN], child[PATHN];
	if (!upd_join(pattern, sizeof pattern, path, "*")) return 0;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) { sUpdRefusal = "a folder could not be read"; return 0; }
	int ok = 1;
	do
	{
		if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		{ sUpdRefusal = "it holds a link, junction or mount point"; ok = 0; }
		else if (!upd_join(child, sizeof child, path, fd.cFileName)) ok = 0;
		else ok = upd_tree_win(child, depth + 1, scan);
	} while (ok && FindNextFileA(h, &fd));
	FindClose(h);
	if (!ok) return 0;
	if (scan || upd_rmdir(path)) return 1;
	sUpdRefusal = "a folder could not be removed";
	return 0;
}
#endif

/* The running launcher's hold on its own versions/ entry: retention never removes a version that is running. Linux: a
 * shared flock on the entry folder, inherited by the game. Windows: a handle on the folder that denies deletion. */
#ifdef _WIN32
static HANDLE sUpdHold = INVALID_HANDLE_VALUE;
#else
static int sUpdHold = -1;
#endif
static void upd_hold_entry(const char *entry)
{
#ifdef _WIN32
	sUpdHold = CreateFileA(entry, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, NULL);
#else
	sUpdHold = open(entry, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);	/* kept across exec into the game */
	if (sUpdHold >= 0 && flock(sUpdHold, LOCK_SH | LOCK_NB)) { close(sUpdHold); sUpdHold = -1; }
#endif
}

/* 1 when some running launcher (or its game) holds this entry. */
static int upd_entry_in_use(const char *path)
{
#ifdef _WIN32
	HANDLE h = CreateFileA(path, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return 0; }
	return GetLastError() == ERROR_SHARING_VIOLATION;
#else
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return 0;
	int busy = flock(fd, LOCK_EX | LOCK_NB) != 0 && (errno == EWOULDBLOCK || errno == EAGAIN);
	close(fd);	/* releases the probe's own lock */
	return busy;
#endif
}

/* Removes parent/name when it is the directory recorded as `want`, nobody runs from it, and it holds only regular files
 * and directories on its own mount. Returns UPD_RM_*; anything but REMOVED/ABSENT leaves it exactly as it was found
 * (sUpdRefusal says why for FOREIGN and FAILED). */
static int upd_remove_owned(const char *parent, const char *name, const UpdId *want)
{
	char path[PATHN];
	UpdId id;
	sUpdRefusal = "";
	if (!upd_name_ok(name) || !upd_join(path, sizeof path, parent, name)) return UPD_RM_NOT_OURS;
	int kind = upd_kind(path, &id);
	if (kind == UPD_ABSENT) return UPD_RM_ABSENT;
	if (kind != UPD_DIR || !upd_same(&id, want)) return UPD_RM_NOT_OURS;
	if (upd_entry_in_use(path)) return UPD_RM_IN_USE;
#ifdef _WIN32
	if (!upd_tree_win(path, 0, 1)) return UPD_RM_FOREIGN;
	return upd_tree_win(path, 0, 0) ? UPD_RM_REMOVED : UPD_RM_FAILED;
#else
	int pfd = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (pfd < 0) return UPD_RM_FAILED;
	struct stat st;
	uint64_t mnt = 0;
	int rc = UPD_RM_NOT_OURS;
	if (!fstatat(pfd, name, &st, AT_SYMLINK_NOFOLLOW) && (uint64_t)st.st_dev == want->dev &&
		(uint64_t)st.st_ino == want->ino)
	{
		/* The entry is on the versions folder's own mount (itself not a mount root); so must everything in it be. */
		if (!upd_mount_of(AT_FDCWD, parent, &mnt) || !upd_tree_at(pfd, name, st.st_dev, mnt, 0, 1)) rc = UPD_RM_FOREIGN;
		else rc = upd_tree_at(pfd, name, st.st_dev, mnt, 0, 0) ? UPD_RM_REMOVED : UPD_RM_FAILED;
	}
	close(pfd);
	if (rc == UPD_RM_REMOVED) upd_sync_dir(parent);
	return rc;
#endif
}

/* ---- downloads ------------------------------------------------------------------------------------------------ */

/* The feed URL's scheme rules. Release builds fetch https only (redirects too). */
static const char *upd_feed_url(void)
{
#ifdef CE_UPDATER_TESTING
	const char *f = getenv("BEA_UPDATE_V2_FEED");
	if (f && *f) return f;
#endif
	return UPD_FEED_URL;
}

/* The package URL the signed feed names: release builds accept only this repository's release-asset URL for the
 * exact versioned file name; test builds also accept the same file name on http://127.0.0.1:PORT/. */
static int upd_package_url_ok(const char *url, const char *version)
{
	if (ce_update_url_allowed(url, version, PLATFORM, UPD_EXT)) return 1;
#ifdef CE_UPDATER_TESTING
	static const char local[] = "http://127.0.0.1:";
	char name[256];
	if (strncmp(url, local, sizeof local - 1)) return 0;
	const char *p = url + sizeof local - 1;
	if (!(*p >= '0' && *p <= '9')) return 0;
	while (*p >= '0' && *p <= '9') ++p;
	if (*p != '/' || strstr(p, "..") || strstr(p, "//")) return 0;
	for (const char *c = p; *c; ++c)
		if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '.' ||
			  *c == '-' || *c == '_' || *c == '/')) return 0;
	int n = snprintf(name, sizeof name, "/onslaught-ce-%s-%s.%s", version, PLATFORM, UPD_EXT);
	size_t ul = strlen(p);
	return n > 0 && (size_t)n <= ul && !strcmp(p + ul - (size_t)n, name);
#else
	return 0;
#endif
}

/* Downloads url into a new file `to` (created exclusively here), at most max bytes, within `seconds`. 1 on a complete
 * HTTP 200 transfer; on failure the partial file is removed (this run created it). */
#ifdef _WIN32
static int upd_fetch(const char *url, const char *to, uint64_t max, int seconds, uint64_t *got)
{
	*got = 0;
	wchar_t wurl[2048], host[256], path[2048];
	if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048)) return 0;
	URL_COMPONENTS u = {sizeof u};
	u.lpszHostName = host; u.dwHostNameLength = 256;
	u.lpszUrlPath = path; u.dwUrlPathLength = 2048;
	if (!WinHttpCrackUrl(wurl, 0, 0, &u)) return 0;
	int secure = u.nScheme == INTERNET_SCHEME_HTTPS;
#ifndef CE_UPDATER_TESTING
	if (!secure) return 0;
#endif
	HINTERNET s = WinHttpOpen(L"Onslaught-CE-updater/2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s) s = WinHttpOpen(L"Onslaught-CE-updater/2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!s) return 0;
	WinHttpSetTimeouts(s, 10000, 10000, 30000, 30000);
	HINTERNET c = WinHttpConnect(s, host, u.nPort, 0);
	HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		secure ? WINHTTP_FLAG_SECURE : 0) : NULL;
	int ok = 0;
	UpdFile f;
	int created = 0;
	ULONGLONG deadline = GetTickCount64() + (ULONGLONG)seconds * 1000;
	if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		WinHttpReceiveResponse(r, NULL))
	{
		DWORD status = 0, size = sizeof status;
		WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			&status, &size, WINHTTP_NO_HEADER_INDEX);
		wchar_t length[32];
		DWORD lsize = sizeof length;
		uint64_t announced = 0;
		int have_length = WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length,
			&lsize, WINHTTP_NO_HEADER_INDEX);
		if (have_length) announced = wcstoull(length, NULL, 10);
		if (status == 200 && (!have_length || announced <= max) && (created = upd_file_create(&f, to, 0)))
		{
			ok = 1;
			for (;;)
			{
				char buf[65536];
				DWORD n = 0;
				if (GetTickCount64() > deadline || !WinHttpReadData(r, buf, sizeof buf, &n)) { ok = 0; break; }
				if (!n) break;
				if (*got + n > max || !upd_file_write(&f, buf, n)) { ok = 0; break; }
				*got += n;
			}
			if (have_length && announced != *got) ok = 0;	/* a connection closed early */
		}
	}
	if (r) WinHttpCloseHandle(r);
	if (c) WinHttpCloseHandle(c);
	WinHttpCloseHandle(s);
	if (created && !upd_file_close(&f)) ok = 0;
	if (created && !ok) upd_unlink_file(to);
	return ok;
}
#else
static int upd_fetch(const char *url, const char *to, uint64_t max, int seconds, uint64_t *got)
{
	*got = 0;
	if (strncmp(url, "https://", 8)
#ifdef CE_UPDATER_TESTING
		&& strncmp(url, "http://127.0.0.1:", 17)
#endif
		) return 0;
	UpdFile f;
	if (!upd_file_create(&f, to, 0)) return 0;
	int p[2];
	if (pipe(p)) { upd_file_close(&f); upd_unlink_file(to); return 0; }
	char t[16], m[32];
	snprintf(t, sizeof t, "%d", seconds);
	snprintf(m, sizeof m, "%" PRIu64, max);
#ifdef CE_UPDATER_TESTING
	const char *protos = "=https,http";
#else
	const char *protos = "=https";
#endif
	/* -q first: no ~/.curlrc. Output on a pipe the launcher reads, so the byte cap holds whatever curl was told. */
	char *argv[] = {"curl", "-q", "-f", "-s", "-L", "--proto", (char *)protos, "--proto-redir", (char *)protos,
		"--connect-timeout", "10", "--max-time", t, "--max-filesize", m, "-o", "-", (char *)url, NULL};
	fflush(NULL);
	pid_t pid = fork();
	if (pid == 0)
	{
		int null = open("/dev/null", O_RDWR);
		dup2(p[1], 1);
		if (null >= 0) { dup2(null, 0); dup2(null, 2); }
		close(p[0]); close(p[1]);
		execvp("curl", argv);
		_exit(127);
	}
	close(p[1]);
	int ok = pid > 0, over = 0;
	char buf[65536];
	while (ok)
	{
		ssize_t k = read(p[0], buf, sizeof buf);
		if (k < 0 && errno == EINTR) continue;
		if (k <= 0) { if (k < 0) ok = 0; break; }
		if (*got + (uint64_t)k > max) { over = 1; ok = 0; break; }
		if (!upd_file_write(&f, buf, (size_t)k)) { ok = 0; break; }
		*got += (uint64_t)k;
	}
	close(p[0]);
	if (pid > 0)
	{
		if (!ok) kill(pid, SIGKILL);
		int st = 0;
		while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
		if (!WIFEXITED(st) || WEXITSTATUS(st)) ok = 0;
	}
	(void)over;
	if (!upd_file_close(&f)) ok = 0;
	if (!ok) upd_unlink_file(to);
	return ok;
}
#endif

/* ---- the feed ------------------------------------------------------------------------------------------------- */

typedef struct { char version[32], url[512], sha[65]; uint64_t size; } UpdOffer;

/* The signature file: exactly 128 lowercase hex digits, optionally one newline. Checked over domain || feed. */
static int upd_signature_ok(const char *feed, size_t n, const char *sig, size_t sn)
{
	uint8_t key[32], signature[64];
#ifdef CE_UPDATER_TESTING
	(void)PUBLIC_KEY_HEX;	/* kept in the source for the signing tools; a test build checks against its test key */
#endif
	if (!(sn == 128 || (sn == 129 && sig[128] == '\n'))) return 0;
	for (size_t i = 0; i < 128; ++i)
		if (!((sig[i] >= '0' && sig[i] <= '9') || (sig[i] >= 'a' && sig[i] <= 'f'))) return 0;
	if (!hex_to(UPD_KEY_HEX, key, 32) || !hex_to(sig, signature, 64)) return 0;
	size_t dl = sizeof UPD_SIG_DOMAIN - 1;
	uint8_t *msg = malloc(dl + n);
	if (!msg) return 0;
	memcpy(msg, UPD_SIG_DOMAIN, dl);
	memcpy(msg + dl, feed, n);
	int ok = crypto_ed25519_check(signature, key, msg, dl + n) == 0;
	free(msg);
	return ok;
}

/* A strict reader for the feed's JSON: objects, strings without escapes (printable ASCII), non-negative integers; any
 * other value only where it is skipped. Duplicate known keys and trailing bytes are refused. */
typedef struct { const char *p, *e; } UpdJson;
static void uj_ws(UpdJson *j) { while (j->p < j->e && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) ++j->p; }
static int uj_is(UpdJson *j, char c) { uj_ws(j); if (j->p < j->e && *j->p == c) { ++j->p; return 1; } return 0; }
static int uj_str(UpdJson *j, char *out, size_t n)
{
	if (!uj_is(j, '"')) return 0;
	size_t k = 0;
	while (j->p < j->e && *j->p != '"')
	{
		unsigned char c = (unsigned char)*j->p++;
		if (c < 0x20 || c > 0x7e || c == '\\' || k + 1 >= n) return 0;
		out[k++] = (char)c;
	}
	if (j->p >= j->e) return 0;
	++j->p;
	out[k] = 0;
	return 1;
}
static int uj_num(UpdJson *j, uint64_t *v)
{
	uj_ws(j);
	const char *s = j->p;
	*v = 0;
	while (j->p < j->e && *j->p >= '0' && *j->p <= '9')
	{
		if (j->p - s >= 15) return 0;
		*v = *v * 10 + (uint64_t)(*j->p++ - '0');
	}
	return j->p > s && !(j->p - s > 1 && *s == '0');
}
static int uj_skip(UpdJson *j, int depth)
{
	char tmp[1024];
	uint64_t v;
	uj_ws(j);
	if (depth > 8 || j->p >= j->e) return 0;
	if (*j->p == '"') return uj_str(j, tmp, sizeof tmp);
	if (*j->p >= '0' && *j->p <= '9') return uj_num(j, &v);
	if (*j->p == '{' || *j->p == '[')
	{
		char close = *j->p == '{' ? '}' : ']';
		int object = close == '}';
		++j->p;
		if (uj_is(j, close)) return 1;
		do
		{
			if (object && (!uj_str(j, tmp, sizeof tmp) || !uj_is(j, ':'))) return 0;
			if (!uj_skip(j, depth + 1)) return 0;
		} while (uj_is(j, ','));
		return uj_is(j, close);
	}
	static const char *const words[] = {"true", "false", "null"};
	for (int i = 0; i < 3; ++i)
	{
		size_t l = strlen(words[i]);
		if ((size_t)(j->e - j->p) >= l && !memcmp(j->p, words[i], l)) { j->p += l; return 1; }
	}
	return 0;
}

static int upd_parse_package(UpdJson *j, UpdOffer *o)
{
	int have_url = 0, have_sha = 0, have_size = 0;
	char key[64];
	if (!uj_is(j, '{')) return 0;
	if (uj_is(j, '}')) return 0;
	do
	{
		if (!uj_str(j, key, sizeof key) || !uj_is(j, ':')) return 0;
		if (!strcmp(key, "url")) { if (have_url++ || !uj_str(j, o->url, sizeof o->url)) return 0; }
		else if (!strcmp(key, "sha256")) { if (have_sha++ || !uj_str(j, o->sha, sizeof o->sha)) return 0; }
		else if (!strcmp(key, "size")) { if (have_size++ || !uj_num(j, &o->size)) return 0; }
		else if (!uj_skip(j, 2)) return 0;
	} while (uj_is(j, ','));
	if (!uj_is(j, '}') || !have_url || !have_sha || !have_size) return 0;
	if (strlen(o->sha) != 64) return 0;
	for (int i = 0; i < 64; ++i)
		if (!((o->sha[i] >= '0' && o->sha[i] <= '9') || (o->sha[i] >= 'a' && o->sha[i] <= 'f'))) return 0;
	return o->size > 0 && o->size <= UPD_PACKAGE_MAX;
}

/* 1 a complete offer for this platform; 0 refused (why says which rule). */
static int upd_parse_feed(const char *feed, size_t n, UpdOffer *o, const char **why)
{
	UpdJson j = {feed, feed + n};
	char key[64], format[64] = "";
	int have_format = 0, have_version = 0, have_packages = 0, have_ours = 0;
	memset(o, 0, sizeof *o);
	*why = "malformed";
	if (!uj_is(&j, '{')) return 0;
	do
	{
		if (!uj_str(&j, key, sizeof key) || !uj_is(&j, ':')) return 0;
		if (!strcmp(key, "format")) { if (have_format++ || !uj_str(&j, format, sizeof format)) return 0; }
		else if (!strcmp(key, "version")) { if (have_version++ || !uj_str(&j, o->version, sizeof o->version)) return 0; }
		else if (!strcmp(key, "packages"))
		{
			if (have_packages++ || !uj_is(&j, '{')) return 0;
			if (!uj_is(&j, '}'))
			{
				do
				{
					char plat[64];
					if (!uj_str(&j, plat, sizeof plat) || !uj_is(&j, ':')) return 0;
					if (!strcmp(plat, PLATFORM)) { if (have_ours++ || !upd_parse_package(&j, o)) return 0; }
					else if (!uj_skip(&j, 2)) return 0;
				} while (uj_is(&j, ','));
				if (!uj_is(&j, '}')) return 0;
			}
		}
		else if (!uj_skip(&j, 1)) return 0;
	} while (uj_is(&j, ','));
	if (!uj_is(&j, '}')) return 0;
	uj_ws(&j);
	if (j.p != j.e) return 0;
	if (!have_format || strcmp(format, UPD_FORMAT)) { *why = "not a v2 feed"; return 0; }
	if (!have_version || !upd_version_ok(o->version)) { *why = "bad version"; return 0; }
	if (!have_ours) { *why = "no package for " PLATFORM; return 0; }
	if (!upd_package_url_ok(o->url, o->version)) { *why = "package URL refused"; return 0; }
	return 1;
}

/* ---- unpacking ------------------------------------------------------------------------------------------------ */

typedef struct { const char *root, *top; unsigned files; uint64_t bytes; } UpdOut;

/* An archive member path: "<top>/..." with plain components; no links, no "..", no drive or device names. */
static int upd_member_ok(const UpdOut *o, const char *rel)
{
	size_t tl = strlen(o->top), n = strlen(rel);
	if (n > 240 || strncmp(rel, o->top, tl) || (rel[tl] != '/' && rel[tl] != 0)) return 0;
	int depth = 0;
	for (const char *p = rel; *p;)
	{
		size_t l = strcspn(p, "/");
		if (!l || l > 100 || (l == 1 && p[0] == '.') || (l == 2 && p[0] == '.' && p[1] == '.') || p[l - 1] == '.') return 0;
		for (size_t i = 0; i < l; ++i)
			if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') ||
				  p[i] == '.' || p[i] == '-' || p[i] == '_' || p[i] == '+')) return 0;
#ifdef _WIN32
		static const char *const reserved[] = {"con", "prn", "aux", "nul", "com", "lpt"};
		for (int r = 0; r < 6; ++r)
			if (!_strnicmp(p, reserved[r], 3) && (l == 3 || p[3] == '.' || (r >= 4 && l >= 4 && p[3] >= '0' && p[3] <= '9' && (l == 4 || p[4] == '.'))))
				return 0;
#endif
		if (++depth > 10) return 0;
		p += l;
		if (*p == '/') { ++p; if (!*p) return 0; }
	}
	return 1;
}

/* The path under the root, with each parent directory made (by this run, inside its own fresh directory). */
static int upd_member_path(const UpdOut *o, const char *rel, char *out, size_t n, int is_dir)
{
	char tmp[PATHN];
	if (!upd_member_ok(o, rel) || snprintf(tmp, sizeof tmp, "%s", rel) >= (int)sizeof tmp) return 0;
	size_t base = strlen(o->root);
	if (base + 1 + strlen(tmp) >= n) return 0;
	snprintf(out, n, "%s", o->root);
	for (char *p = tmp, *slash;; p = slash + 1)
	{
		slash = strchr(p, '/');
		if (slash) *slash = 0;
		size_t at = strlen(out);
		snprintf(out + at, n - at, SEP "%s", p);
		if (!slash && !is_dir) return 1;
		int made = upd_mkdir(out, 0);
		if (made < 0 || (made == 0 && upd_kind(out, NULL) != UPD_DIR)) return 0;
		if (!slash) return 1;
	}
}

#ifndef _WIN32
static int upd_tar_octal(const char *p, size_t n, uint64_t *v)
{
	*v = 0;
	size_t i = 0;
	while (i < n && p[i] == ' ') ++i;
	if (i == n || p[i] < '0' || p[i] > '7') return 0;
	for (; i < n && p[i] >= '0' && p[i] <= '7'; ++i) *v = *v * 8 + (uint64_t)(p[i] - '0');
	for (; i < n; ++i) if (p[i] != ' ' && p[i] != 0) return 0;
	return 1;
}

static int upd_gz_read(gzFile g, void *p, size_t n) { return gzread(g, p, (unsigned)n) == (int)n; }

/* The Linux package: a gzip'd GNU tar of regular files and directories under one top folder. */
static int upd_unpack(const char *pkg, UpdOut *o)
{
	int fd = open(pkg, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return 0;
	gzFile g = gzdopen(fd, "rb");
	if (!g) { close(fd); return 0; }
	char h[512], longname[1536] = "", path[PATHN];
	static char buf[65536];
	int ok = 0, have_long = 0;
	for (unsigned entries = 0; entries < UPD_ENTRIES_MAX; ++entries)
	{
		if (!upd_gz_read(g, h, 512)) break;
		int zero = 1;
		for (int i = 0; i < 512; ++i) if (h[i]) { zero = 0; break; }
		if (zero) { ok = upd_gz_read(g, h, 512) && !memcmp(h, (char[512]){0}, 512); break; }
		uint64_t sum = 0, want, size, mode;
		for (int i = 0; i < 512; ++i) sum += (unsigned char)(i >= 148 && i < 156 ? ' ' : h[i]);
		if (!upd_tar_octal(h + 148, 8, &want) || want != sum || !upd_tar_octal(h + 124, 12, &size) ||
			!upd_tar_octal(h + 100, 8, &mode)) break;
		char type = h[156], name[256 + 2];
		if (type == 'L')
		{
			if (!size || size + 512 > sizeof longname || have_long ||
				!upd_gz_read(g, longname, (size_t)((size + 511) & ~511ull))) break;
			/* GNU long name: the name and its terminating NUL, nothing else. */
			if (longname[size - 1] || strlen(longname) != size - 1) break;
			have_long = 1;
			continue;
		}
		if (have_long) snprintf(name, sizeof name, "%s", longname);
		else if (!memcmp(h + 257, "ustar", 5) && h[345])
			snprintf(name, sizeof name, "%.155s/%.100s", h + 345, h);
		else snprintf(name, sizeof name, "%.100s", h);
		have_long = 0;
		size_t nl = strlen(name);
		if (type == '5')
		{
			if (nl && name[nl - 1] == '/') name[--nl] = 0;
			if (size || !upd_member_path(o, name, path, sizeof path, 1)) break;
			continue;
		}
		if (type != '0' && type != 0) break;	/* links, devices, pax and anything else are refused */
		if (o->bytes + size > UPD_UNPACK_MAX || !upd_member_path(o, name, path, sizeof path, 0)) break;
		UpdFile f;
		if (!upd_file_create(&f, path, (mode & 0100) != 0)) break;
		int good = 1;
		for (uint64_t left = size; left && good;)
		{
			size_t k = left < sizeof buf ? (size_t)left : sizeof buf;
			good = upd_gz_read(g, buf, k) && upd_file_write(&f, buf, k);
			left -= k;
		}
		if (!upd_file_close(&f)) good = 0;
		size_t pad = (size_t)((512 - size % 512) % 512);
		if (!good || (pad && !upd_gz_read(g, buf, pad))) break;
		o->files++;
		o->bytes += size;
		if (o->files == 1) upd_crash_point("unpack-mid");
	}
	gzclose(g);
	return ok && o->files;
}
#else
static uint32_t upd_le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t upd_le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The Windows package: a zip of stored or deflated regular files under one top folder. */
static int upd_unpack(const char *pkg, UpdOut *o)
{
	FILE *f = fopen(pkg, "rb");
	if (!f) return 0;
	int ok = 0;
	unsigned char *cd = NULL, *tail = NULL;
	static unsigned char in[65536], out[65536];
	if (fseek(f, 0, SEEK_END)) goto done;
	long total = ftell(f);
	if (total < 22 || (uint64_t)total > UPD_PACKAGE_MAX) goto done;
	long tl = total < 65557 ? total : 65557;
	tail = malloc((size_t)tl);
	if (!tail || fseek(f, total - tl, SEEK_SET) || fread(tail, 1, (size_t)tl, f) != (size_t)tl) goto done;
	long eocd = -1;
	for (long i = tl - 22; i >= 0; --i)
		if (upd_le32(tail + i) == 0x06054b50 && i + 22 + upd_le16(tail + i + 20) == tl) { eocd = i; break; }
	if (eocd < 0) goto done;
	unsigned count = upd_le16(tail + eocd + 10);
	uint32_t cdsize = upd_le32(tail + eocd + 12), cdoff = upd_le32(tail + eocd + 16);
	if (upd_le16(tail + eocd + 4) || upd_le16(tail + eocd + 6) || count != upd_le16(tail + eocd + 8) ||
		!count || count > UPD_ENTRIES_MAX || cdsize > (1u << 20) || (uint64_t)cdoff + cdsize > (uint64_t)(total - tl + eocd))
		goto done;
	cd = malloc(cdsize);
	if (!cd || fseek(f, (long)cdoff, SEEK_SET) || fread(cd, 1, cdsize, f) != cdsize) goto done;
	uint32_t at = 0;
	for (unsigned i = 0; i < count; ++i)
	{
		if (at + 46 > cdsize || upd_le32(cd + at) != 0x02014b50) goto done;
		unsigned flags = upd_le16(cd + at + 8), method = upd_le16(cd + at + 10);
		uint32_t crc = upd_le32(cd + at + 16), csize = upd_le32(cd + at + 20), usize = upd_le32(cd + at + 24);
		unsigned nl = upd_le16(cd + at + 28), xl = upd_le16(cd + at + 30), cl = upd_le16(cd + at + 32);
		uint32_t attr = upd_le32(cd + at + 38), lho = upd_le32(cd + at + 42);
		if (at + 46 + nl + xl + cl > cdsize || nl >= 256) goto done;
		char name[256], path[PATHN];
		memcpy(name, cd + at + 46, nl);
		name[nl] = 0;
		at += 46 + nl + xl + cl;
		unsigned type = (attr >> 16) & 0170000;
		if ((flags & 1) || (method != 0 && method != 8) || csize == 0xffffffffu || usize == 0xffffffffu ||
			lho == 0xffffffffu || memchr(name, 0, nl) || (type && type != 0100000 && type != 0040000)) goto done;
		if (nl && name[nl - 1] == '/')
		{
			name[nl - 1] = 0;
			if (usize || !upd_member_path(o, name, path, sizeof path, 1)) goto done;
			continue;
		}
		if (type == 0040000 || o->bytes + usize > UPD_UNPACK_MAX) goto done;
		unsigned char lh[30];
		if (fseek(f, (long)lho, SEEK_SET) || fread(lh, 1, 30, f) != 30 || upd_le32(lh) != 0x04034b50) goto done;
		uint64_t data = (uint64_t)lho + 30 + upd_le16(lh + 26) + upd_le16(lh + 28);
		if (data + csize > cdoff || fseek(f, (long)data, SEEK_SET)) goto done;
		if (!upd_member_path(o, name, path, sizeof path, 0)) goto done;
		UpdFile w;
		if (!upd_file_create(&w, path, 0)) goto done;
		int good = 1;
		uint32_t got = 0, sum = (uint32_t)crc32(0, NULL, 0);
		if (method == 0)
		{
			for (uint32_t left = csize; left && good;)
			{
				size_t k = left < sizeof in ? left : sizeof in;
				good = fread(in, 1, k, f) == k && upd_file_write(&w, in, k);
				sum = (uint32_t)crc32(sum, in, (uInt)k);
				got += (uint32_t)k;
				left -= (uint32_t)k;
			}
		}
		else
		{
			z_stream z;
			memset(&z, 0, sizeof z);
			good = inflateInit2(&z, -15) == Z_OK;
			int r = Z_OK;
			for (uint32_t left = csize; good && r != Z_STREAM_END;)
			{
				if (!z.avail_in)
				{
					if (!left) { good = 0; break; }
					size_t k = left < sizeof in ? left : sizeof in;
					if (fread(in, 1, k, f) != k) { good = 0; break; }
					z.next_in = in; z.avail_in = (uInt)k; left -= (uint32_t)k;
				}
				z.next_out = out; z.avail_out = sizeof out;
				r = inflate(&z, Z_NO_FLUSH);
				if (r != Z_OK && r != Z_STREAM_END) { good = 0; break; }
				size_t k = sizeof out - z.avail_out;
				if (got + k > usize || !upd_file_write(&w, out, k)) { good = 0; break; }
				sum = (uint32_t)crc32(sum, out, (uInt)k);
				got += (uint32_t)k;
			}
			inflateEnd(&z);
		}
		if (!upd_file_close(&w)) good = 0;
		if (!good || got != usize || sum != crc) goto done;
		o->files++;
		o->bytes += usize;
		if (o->files == 1) upd_crash_point("unpack-mid");
	}
	ok = o->files > 0;
done:
	free(cd);
	free(tail);
	fclose(f);
	return ok;
}
#endif

/* ---- the installation's state --------------------------------------------------------------------------------- */

typedef struct { char name[64], version[32]; UpdId id; int ready; } UpdEntry;
typedef struct
{
	UpdId dir, file;		/* versions/ itself; the state file as it was read (exchange check) */
	int have_file;
	char active[64], previous[64];	/* an entry's name, "base" (the folder's own files) or "none" */
	int n;
	UpdEntry e[UPD_MAX_VERSIONS];
} UpdState;

static UpdEntry *upd_entry(UpdState *s, const char *name)
{
	for (int i = 0; i < s->n; ++i) if (!strcmp(s->e[i].name, name)) return &s->e[i];
	return NULL;
}

static int upd_state_parse(char *text, UpdState *s)
{
	int have_dir = 0, have_active = 0, have_previous = 0;
	if (strncmp(text, UPD_STATE_MAGIC "\n", sizeof UPD_STATE_MAGIC)) return 0;
	for (char *line = text + sizeof UPD_STATE_MAGIC, *next; *line; line = next)
	{
		next = strchr(line, '\n');
		if (!next) return 0;
		*next++ = 0;
		char word[16], a[64], b[32], c[16];
		uint64_t x;
		/* Only inodes are recorded: device numbers (btrfs anonymous devices among them) can change across boots, so
		 * every identity is compared on the versions folder's current device (upd_state_load). */
		if (sscanf(line, "dir %" SCNu64 "%15s", &x, c) == 1) { s->dir.ino = x; have_dir++; }
		else if (sscanf(line, "active %63s", a) == 1 && (!strcmp(a, "base") || upd_entry_name_ok(a, NULL)))
		{ snprintf(s->active, 64, "%s", a); have_active++; }
		else if (sscanf(line, "previous %63s", a) == 1 && (!strcmp(a, "base") || !strcmp(a, "none") || upd_entry_name_ok(a, NULL)))
		{ snprintf(s->previous, 64, "%s", a); have_previous++; }
		else if (sscanf(line, "%15s %63s %" SCNu64 " %31s %15s", word, a, &x, b, c) == 5 &&
				 !strcmp(word, "entry") && upd_entry_name_ok(a, b) && s->n < UPD_MAX_VERSIONS &&
				 !upd_entry(s, a) && (!strcmp(c, "ready") || !strcmp(c, "unpacking")))
		{
			UpdEntry *e = &s->e[s->n++];
			snprintf(e->name, sizeof e->name, "%s", a);
			snprintf(e->version, sizeof e->version, "%s", b);
			e->id.dev = s->dir.dev; e->id.ino = x;
			e->ready = !strcmp(c, "ready");
		}
		else return 0;
	}
	if (have_dir != 1 || have_active != 1 || have_previous != 1) return 0;
	for (int k = 0; k < 2; ++k)
	{
		const char *ref = k ? s->previous : s->active;
		if (strcmp(ref, "base") && (k == 0 || strcmp(ref, "none")))
		{
			UpdEntry *e = upd_entry(s, ref);
			if (!e || !e->ready) return 0;
		}
	}
	return 1;
}

/* 1 loaded, 0 no state file, -1 unreadable, foreign or malformed (then the updater changes nothing). */
static int upd_state_load(const char *vdir, UpdState *s)
{
	char path[PATHN];
	UpdId here;
	memset(s, 0, sizeof *s);
	if (upd_kind(vdir, &here) != UPD_DIR || !upd_join(path, sizeof path, vdir, "state")) return -1;
	s->dir.dev = here.dev;	/* recorded identities are inodes on this device, as it is numbered now */
	int kind = upd_kind(path, &s->file);
	if (kind == UPD_ABSENT) return 0;
	if (kind != UPD_FILE) return -1;
	char *text = upd_read_small(path, 8192, NULL);
	if (!text) return -1;
	int ok = upd_state_parse(text, s);
	free(text);
	s->have_file = ok;
	return ok ? 1 : -1;
}

/* Replaces versions/state with s, atomically: a new exclusively created file (journaled first), exchanged with the
 * current state file whose identity is the one this run read. 1 committed; 0 not committed (the old state stands). */
static int upd_state_store(const char *vdir, UpdLock *lk, UpdJournal *jr, UpdState *s)
{
	char text[4096], hex[17], tmpname[64], tmp[PATHN], path[PATHN];
	int n = snprintf(text, sizeof text, UPD_STATE_MAGIC "\ndir %" PRIu64 "\nactive %s\nprevious %s\n",
		s->dir.ino, s->active, s->previous);
	for (int i = 0; i < s->n && n > 0 && (size_t)n < sizeof text; ++i)
		n += snprintf(text + n, sizeof text - (size_t)n, "entry %s %" PRIu64 " %s %s\n", s->e[i].name,
			s->e[i].id.ino, s->e[i].version, s->e[i].ready ? "ready" : "unpacking");
	if (n <= 0 || (size_t)n >= sizeof text || !upd_random_hex(hex)) return 0;
	snprintf(tmpname, sizeof tmpname, "state.new-%s", hex);
	if (!upd_join(tmp, sizeof tmp, vdir, tmpname) || !upd_join(path, sizeof path, vdir, "state")) return 0;
	snprintf(jr->tmp, sizeof jr->tmp, "%s", tmpname);
	if (!upd_journal_write(lk, jr)) { jr->tmp[0] = 0; return 0; }
	int ok = 0;
	if (upd_write_new(tmp, text, (size_t)n))
	{
		upd_crash_point("state-temp-written");
		UpdId now;
#ifdef _WIN32
		if (s->have_file)
			ok = upd_kind(path, &now) == UPD_FILE && upd_same(&now, &s->file) &&
				MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		else ok = upd_kind(path, NULL) == UPD_ABSENT && MoveFileExA(tmp, path, MOVEFILE_WRITE_THROUGH);
		if (!ok) upd_unlink_file(tmp);
#else
		if (s->have_file)
		{
			if (!syscall(SYS_renameat2, AT_FDCWD, tmp, AT_FDCWD, path, 2 /* RENAME_EXCHANGE */))
			{
				/* tmp now names what was the state file: it must be the one this run read. */
				if (upd_kind(tmp, &now) == UPD_FILE && upd_same(&now, &s->file)) ok = 1;
				else syscall(SYS_renameat2, AT_FDCWD, tmp, AT_FDCWD, path, 2);
				upd_unlink_file(tmp);
			}
			else if (errno == EINVAL || errno == ENOSYS)
			{
				/* A file system without RENAME_EXCHANGE: check, then the atomic replacing rename. */
				ok = upd_kind(path, &now) == UPD_FILE && upd_same(&now, &s->file) && !rename(tmp, path);
				if (!ok) upd_unlink_file(tmp);
				else say("update: state replaced with rename (no RENAME_EXCHANGE on this file system)");
			}
			else upd_unlink_file(tmp);
		}
		else
		{
			if (!syscall(SYS_renameat2, AT_FDCWD, tmp, AT_FDCWD, path, 1 /* RENAME_NOREPLACE */)) ok = 1;
			else if ((errno == EINVAL || errno == ENOSYS) && !link(tmp, path)) { ok = 1; upd_unlink_file(tmp); }
			else upd_unlink_file(tmp);
		}
#endif
	}
	upd_sync_dir(vdir);
	jr->tmp[0] = 0;
	upd_journal_write(lk, jr);
	if (ok)
	{
		ok = upd_kind(path, &s->file) == UPD_FILE;
		s->have_file = ok;
	}
	return ok;
}

/* ---- finding the installation --------------------------------------------------------------------------------- */

/* What the player can do when versions/state cannot be matched to this folder (it was copied, restored from a backup
 * or edited): this copy keeps running and nothing is changed. */
#define UPD_RECOVERY "versions/state does not match this Onslaught CE folder (copied, restored or edited?). This copy " \
	"keeps running and nothing was changed. To let updates work again, quit Onslaught CE and move the versions " \
	"folder out of the Onslaught CE folder (keep it until a new update works), or extract a new release into a new " \
	"empty folder."

/* The running copy's place: the base folder (root = here), or <root>/versions/<entry>/onslaught-ce-<version>-<plat>
 * when root's state records that entry with this identity. Anything else: the updater leaves this copy alone. */
static void upd_find_root(void)
{
	char parent[PATHN], grand[PATHN], top[128], vdir[PATHN];
	sRootUsable = 0;
	sRunningEntry[0] = 0;
	snprintf(sRoot, sizeof sRoot, "%s", sHere);
	snprintf(top, sizeof top, "onslaught-ce-%s-%s", CE_VERSION, PLATFORM);
	snprintf(parent, sizeof parent, "%s", sHere);
	char *s1 = strrchr(parent, SEP[0]);
	if (s1 && !strcmp(s1 + 1, top))
	{
		*s1 = 0;
		snprintf(grand, sizeof grand, "%s", parent);
		char *s2 = strrchr(grand, SEP[0]);
		if (s2)
		{
			const char *entry = s2 + 1;
			*s2 = 0;
			char *s3 = strrchr(grand, SEP[0]);
			if (s3 && !strcmp(s3 + 1, "versions"))
			{
				UpdState st;
				UpdId id;
				char name[64];
				snprintf(name, sizeof name, "%s", entry);
				snprintf(vdir, sizeof vdir, "%s", grand);
				UpdEntry *e;
				UpdId vid;
				if (upd_state_load(vdir, &st) == 1 && upd_kind(vdir, &vid) == UPD_DIR && upd_same(&vid, &st.dir) &&
					(e = upd_entry(&st, name)) && e->ready && upd_kind(parent, &id) == UPD_DIR && upd_same(&id, &e->id))
				{
					upd_hold_entry(parent);
					*s3 = 0;
					snprintf(sRoot, sizeof sRoot, "%s", grand);
					snprintf(sRunningEntry, sizeof sRunningEntry, "%s", name);
					sRootUsable = 1;
				}
				return;	/* inside some versions/ folder: never treat it as a base */
			}
		}
	}
	sRootUsable = 1;
}

/* The active version's program when it is a recorded, intact entry newer than this one: 1 with its path. */
static int upd_active_program(char *exe, size_t n, char *version, size_t vn, int after_update)
{
	char vdir[PATHN], entry[PATHN], top[128], inner[PATHN];
	UpdState st;
	UpdId id;
	if (!sRootUsable || (*sRunningEntry && !after_update) || !upd_join(vdir, sizeof vdir, sRoot, "versions")) return 0;
	int kind = upd_kind(vdir, &id);
	if (kind == UPD_ABSENT) return 0;
	int loaded = kind == UPD_DIR ? upd_state_load(vdir, &st) : -1;
	if (loaded == 0) return 0;
	if (loaded < 0 || !upd_same(&id, &st.dir))
	{
		fprintf(stderr, "onslaught-ce: " UPD_RECOVERY "\n");
		return 0;
	}
	if (!strcmp(st.active, "base")) return 0;
	UpdEntry *e = upd_entry(&st, st.active);
	if (!e || !e->ready || upd_version_cmp(e->version, CE_VERSION) <= 0) return 0;
	snprintf(top, sizeof top, "onslaught-ce-%s-%s", e->version, PLATFORM);
	if (!upd_join(entry, sizeof entry, vdir, e->name) || upd_kind(entry, &id) != UPD_DIR || !upd_same(&id, &e->id) ||
		!upd_join(inner, sizeof inner, entry, top) || upd_kind(inner, NULL) != UPD_DIR ||
		!upd_join(exe, n, inner, UPD_LAUNCHER) || upd_kind(exe, NULL) != UPD_FILE)
	{
		fprintf(stderr, "onslaught-ce: the active updated version (%s) is incomplete or changed; starting this copy\n",
			e->version);
		return 0;
	}
	snprintf(version, vn, "%s", e->version);
	return 1;
}

/* Starts exe with this launcher's arguments. Linux: exec (returns 0 only on failure). Windows: waits, puts the new
 * launcher's exit status in *rc and returns 1, or returns 0 when it could not be started. */
static int upd_run_program(const char *exe, int argc, char **argv, int *status)
{
#ifdef _WIN32
	char cmd[8192];
	if (!ce_windows_command(cmd, sizeof cmd, exe, argv + 1, argc - 1, 0)) return -1;
	STARTUPINFOA si = {sizeof si};
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE), err = GetStdHandle(STD_ERROR_HANDLE), in = GetStdHandle(STD_INPUT_HANDLE);
	int pass = out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN;
	if (pass)
	{
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = in; si.hStdOutput = out; si.hStdError = err;
		SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
		if (err && err != INVALID_HANDLE_VALUE) SetHandleInformation(err, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
		if (in && in != INVALID_HANDLE_VALUE) SetHandleInformation(in, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	}
	PROCESS_INFORMATION pi;
	if (!CreateProcessA(exe, cmd, NULL, NULL, pass, 0, NULL, NULL, &si, &pi)) return 0;
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD rc = 1;
	GetExitCodeProcess(pi.hProcess, &rc);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	*status = (int)rc;
	return 1;
#else
	(void)argc; (void)status;
	char *saved = argv[0];
	argv[0] = (char *)exe;
	execv(exe, argv);
	argv[0] = saved;
	return 0;
#endif
}

/* The base folder's launcher hands over to the active updated version. 1 with *status: it ran (Windows); 0: carry on
 * as this copy. after_update: this launcher just installed it, from whichever folder it runs. */
static int upd_start_active(int argc, char **argv, int after_update, int *status)
{
	char exe[PATHN], version[32];
	const char *off = getenv("BEA_CE_NO_HANDOFF");
	if (!after_update && off && *off && strcmp(off, "0")) return 0;
	if (!upd_active_program(exe, sizeof exe, version, sizeof version, after_update)) return 0;
	if (upd_run_program(exe, argc, argv, status)) return 1;
	fprintf(stderr, "onslaught-ce: could not start the updated version %s; starting this copy (v%s)\n", version, CE_VERSION);
	return 0;
}

/* ---- the update ----------------------------------------------------------------------------------------------- */

static const char *const UPD_STAGE_FILES[] = {"feed.json", "feed.json.sig", "package"};

/* The user folder's stage area: a journaled stage directory is removed when it is still the recorded directory and
 * holds only the three names this updater writes there. */
static void upd_stage_recover(const char *area, UpdLock *lk, UpdJournal *jr)
{
	if (!*jr->stage) return;
	char dir[PATHN], file[PATHN];
	UpdId id;
	UpdId here;
	int removed = 0;
	if (upd_stage_name_ok(jr->stage) && upd_kind(area, &here) == UPD_DIR && upd_join(dir, sizeof dir, area, jr->stage))
	{
		jr->stage_id.dev = here.dev;	/* the journal records the inode; the device is the stage area's, now */
		int kind = upd_kind(dir, &id);
		if (kind == UPD_ABSENT) removed = 1;
		else if (kind == UPD_DIR && (!jr->stage_has_id || upd_same(&id, &jr->stage_id)))
		{
			if (jr->stage_has_id)
				for (int i = 0; i < 3; ++i)
					if (upd_join(file, sizeof file, dir, UPD_STAGE_FILES[i]) && upd_kind(file, NULL) == UPD_FILE)
						upd_unlink_file(file);
			removed = upd_rmdir(dir);	/* only an empty directory: anything else stays */
		}
	}
	if (!removed) say("update: left an earlier stage folder %s in place (it is not as this updater left it)", jr->stage);
	jr->stage[0] = 0;
	jr->stage_has_id = 0;
	upd_journal_write(lk, jr);
}

/* With the install lock held: removes a crashed run's journaled leftovers and every recorded entry that is neither
 * active nor previous. Returns 1 when s is usable. */
static int upd_install_recover(const char *vdir, UpdLock *lk, UpdJournal *jr, UpdState *s)
{
	char path[PATHN];
	if (*jr->tmp)
	{
		char *text = upd_join(path, sizeof path, vdir, jr->tmp) ? upd_read_small(path, 8192, NULL) : NULL;
		/* Only a journaled state.new-<16 hex> (upd_journal_read) holding a state: never the state file itself. */
		if (text && upd_tmp_name_ok(jr->tmp) && !strncmp(text, UPD_STATE_MAGIC "\n", sizeof UPD_STATE_MAGIC))
			upd_unlink_file(path);
		free(text);
		jr->tmp[0] = 0;
	}
	if (*jr->dir)
	{
		/* Created but perhaps never recorded: removed only while still empty. */
		if (upd_entry_name_ok(jr->dir, NULL) && !upd_entry(s, jr->dir) && upd_join(path, sizeof path, vdir, jr->dir) &&
			upd_kind(path, NULL) == UPD_DIR)
			upd_rmdir(path);
		jr->dir[0] = 0;
	}
	upd_journal_write(lk, jr);
	int changed = 0;
	for (int i = 0; i < s->n;)
	{
		UpdEntry *e = &s->e[i];
		if (!strcmp(e->name, s->active) || !strcmp(e->name, s->previous)) { ++i; continue; }
		int rc = upd_remove_owned(vdir, e->name, &e->id);
		if (rc == UPD_RM_IN_USE)
		{
			say("update: the old version folder %s is in use by a running Onslaught CE; it is kept and removed later", e->name);
			++i;
			continue;
		}
		if (rc == UPD_RM_FOREIGN || rc == UPD_RM_FAILED)
		{
			say("update: could not remove the old version folder %s: %s; nothing in it was removed and it is kept "
				"(remove it by hand once you have checked it)", e->name, *sUpdRefusal ? sUpdRefusal : "an error");
			++i;
			continue;
		}
		if (rc == UPD_RM_REMOVED) say("update: removed the old version folder %s", e->name);
		else if (rc == UPD_RM_ABSENT) say("update: the old version folder %s is already gone; it is forgotten", e->name);
		else say("update: %s is no longer the folder this updater made; it is left untouched and forgotten", e->name);
		*e = s->e[--s->n];
		changed = 1;
	}
	if (changed && !upd_state_store(vdir, lk, jr, s)) return 0;
	return 1;
}

/* The prompt: 1 update, 0 later, 2 skip this version, -1 nobody could be asked. */
static int upd_ask(const UpdOffer *o)
{
#ifdef CE_UPDATER_TESTING
	const char *answer = getenv("BEA_UPDATE_ANSWER");
	if (answer && *answer) return !strcmp(answer, "update") ? 1 : !strcmp(answer, "skip") ? 2 : 0;
#endif
#ifndef _WIN32
	const char *x11 = getenv("DISPLAY"), *wayland = getenv("WAYLAND_DISPLAY");
	if (!(x11 && *x11) && !(wayland && *wayland)) return -1;
#endif
	char text[1024];
	snprintf(text, sizeof text, "Onslaught CE V%s is available (you have V%s).\n\n"
		"Update: download it (%.1f MB), check its signature and install it inside this Onslaught CE folder, beside "
		"this version, which is kept. Your careers and settings are not changed.\n"
		"Later: start this version; you will be asked again next time.\n"
		"Skip this version: start this version and do not ask about v%s again.", o->version, CE_VERSION,
		(double)o->size / 1e6, o->version);
	const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Update"},
		{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Later"}, {0, 2, "Skip this version"}};
	const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, "Onslaught CE", text, 3, buttons, NULL};
	int which = 0;
	return SDL_ShowMessageBox(&box, &which) ? which : -1;
}

static void upd_tell(int explicit_cmd, const char *text)
{
	if (explicit_cmd) return;	/* --update-only reports on the terminal (say) */
#ifdef CE_UPDATER_TESTING
	const char *answer = getenv("BEA_UPDATE_ANSWER");
	if (answer && *answer) return;
#endif
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Onslaught CE", text, NULL);
}

/* Unpacks the verified package into a new recorded entry and makes it active. Returns an UPD_* status. */
static int upd_install(const char *package, const UpdOffer *o, char *installed, size_t in)
{
	char vdir[PATHN], name[64], hex[17], entry[PATHN], top[128], probe[PATHN];
	UpdLock lk;
	UpdJournal jr;
	UpdState st;
	UpdId id;
	if (!upd_join(vdir, sizeof vdir, sRoot, "versions")) return UPD_INSTALL_FAILED;
	int made = upd_mkdir(vdir, 0);
	int kind = upd_kind(vdir, &id);
	if (made < 0 || kind != UPD_DIR) { say("update: the versions folder could not be made or is a link; nothing changed"); return UPD_INSTALL_FAILED; }
	int locked = upd_lock(&lk, vdir);
	if (locked == 0) { say("update: another Onslaught CE is updating this folder"); return UPD_BUSY; }
	if (locked < 0) { say("update: the versions folder's lock is not usable; nothing changed"); return UPD_INSTALL_FAILED; }
	upd_journal_read(&lk, &jr);
	int loaded = upd_state_load(vdir, &st);
	int rc = UPD_INSTALL_FAILED;
	if (loaded < 0) { say("update: " UPD_RECOVERY); goto out; }
	if (loaded == 0)
	{
		/* First update: a versions folder without state is adopted only while it is empty. */
		char pattern[PATHN];
		int empty = 1;
#ifdef _WIN32
		WIN32_FIND_DATAA fd;
		upd_join(pattern, sizeof pattern, vdir, "*");
		HANDLE h = FindFirstFileA(pattern, &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..") && strcmp(fd.cFileName, "lock")) empty = 0;
			while (FindNextFileA(h, &fd));
			FindClose(h);
		}
#else
		(void)pattern;
		DIR *d = opendir(vdir);
		struct dirent *e;
		while (d && (e = readdir(d)))
			if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && strcmp(e->d_name, "lock")) empty = 0;
		if (d) closedir(d);
#endif
		if (!empty && !*jr.tmp && !*jr.dir) { say("update: the versions folder holds files this updater did not record; nothing changed"); goto out; }
		st.dir = id;
		snprintf(st.active, sizeof st.active, "base");
		snprintf(st.previous, sizeof st.previous, "none");
	}
	if (!upd_same(&st.dir, &id)) { say("update: " UPD_RECOVERY); goto out; }
	if (!upd_install_recover(vdir, &lk, &jr, &st)) goto out;
	if (loaded == 0 && !upd_state_store(vdir, &lk, &jr, &st)) goto out;
	for (int i = 0; i < st.n; ++i)
		if (st.e[i].ready && upd_version_cmp(st.e[i].version, o->version) >= 0)
		{
			say("update: v%s or newer is already installed; nothing changed", o->version);
			rc = UPD_CURRENT;
			goto out;
		}
	/* Room for the new entry: active and previous stay, everything else has been removed. */
	if (st.n >= UPD_MAX_VERSIONS - 1) { say("update: too many recorded versions; nothing changed"); goto out; }
	if (!upd_random_hex(hex)) goto out;
	snprintf(name, sizeof name, "v%s-%s", o->version, hex);
	snprintf(top, sizeof top, "onslaught-ce-%s-%s", o->version, PLATFORM);
	if (!upd_join(entry, sizeof entry, vdir, name)) goto out;
	snprintf(jr.dir, sizeof jr.dir, "%s", name);
	if (!upd_journal_write(&lk, &jr) || upd_mkdir(entry, 0) != 1 || upd_kind(entry, &id) != UPD_DIR)
	{
		say("update: could not create the new version folder; nothing changed");
		goto out;
	}
	UpdEntry *e = &st.e[st.n++];
	snprintf(e->name, sizeof e->name, "%s", name);
	snprintf(e->version, sizeof e->version, "%s", o->version);
	e->id = id;
	e->ready = 0;
	if (!upd_state_store(vdir, &lk, &jr, &st))
	{
		st.n--;
		upd_rmdir(entry);
		say("update: could not record the new version folder; nothing changed");
		goto out;
	}
	jr.dir[0] = 0;
	upd_journal_write(&lk, &jr);
	upd_crash_point("entry-recorded");
	UpdOut out = {entry, top, 0, 0};
	char shortname[PATHN];
	ce_short_path(entry, shortname, sizeof shortname);
	int unpacked = upd_unpack(package, &out);
	if (unpacked)
	{
		snprintf(probe, sizeof probe, "%s" SEP "%s" SEP "%s", entry, top, UPD_LAUNCHER);
		unpacked = upd_kind(probe, NULL) == UPD_FILE;
		snprintf(probe, sizeof probe, "%s" SEP "%s" SEP "%s", entry, top, GAME_EXE);
		unpacked = unpacked && upd_kind(probe, NULL) == UPD_FILE;
#ifndef _WIN32
		snprintf(probe, sizeof probe, "%s" SEP "%s" SEP "onslaught-ce-launch", entry, top);
		unpacked = unpacked && upd_kind(probe, NULL) == UPD_FILE;
#endif
	}
	if (!unpacked)
	{
		say("update: unpacking failed after %u files (%" PRIu64 " bytes); the new folder is removed, nothing else changed",
			out.files, out.bytes);
		int rm = upd_remove_owned(vdir, name, &id);
		if (rm == UPD_RM_REMOVED || rm == UPD_RM_ABSENT) { st.n--; upd_state_store(vdir, &lk, &jr, &st); }
		else say("update: the unfinished folder %s is kept for the next run to remove (%s)", name, *sUpdRefusal ? sUpdRefusal : "in use");
		goto out;
	}
	say("update: unpacked %u files (%" PRIu64 " bytes) into versions" SEP "%s", out.files, out.bytes, name);
	upd_crash_point("unpacked");
	/* The switch: one atomic state replacement makes the entry ready and active; the running version stays as
	 * previous for --rollback. */
	char previous[64];
	snprintf(previous, sizeof previous, "%s", st.active);
	e = upd_entry(&st, name);
	e->ready = 1;
	snprintf(st.active, sizeof st.active, "%s", name);
	snprintf(st.previous, sizeof st.previous, "%s", previous);
	if (!upd_state_store(vdir, &lk, &jr, &st))
	{
		say("update: could not switch to the new version; this version stays active");
		goto out;
	}
	upd_crash_point("switched");
	say("update: v%s is now active (previous: %s)", o->version, previous);
	snprintf(installed, in, "%s", name);
	upd_install_recover(vdir, &lk, &jr, &st);	/* older entries beyond previous */
	rc = UPD_UPDATED;
out:
	upd_unlock(&lk);
	return rc;
}

/* The whole check: feed, signature, offer, question, download, install. explicit_cmd: --update-only. */
static int upd_check(int explicit_cmd)
{
	char opt[64], area[PATHN], stage[PATHN], feed[PATHN], sig[PATHN], pkg[PATHN], hex[17], sigurl[1100];
	const char *env = getenv("BEA_NO_UPDATE_CHECK");
	if (!upd_version_ok(CE_VERSION)) { say("update: off in a development build (v%s)", CE_VERSION); return UPD_DISABLED; }
	if ((env && *env && strcmp(env, "0")) || (ini_get("update_check", opt, sizeof opt) && !strcmp(opt, "0")))
	{
		if (explicit_cmd) say("update: checking is turned off (BEA_NO_UPDATE_CHECK or update_check=0 in ce.ini)");
		return UPD_DISABLED;
	}
	if (!sRootUsable) { say("update: this copy is not where its installation records it; not updating"); return UPD_DISABLED; }
	const char *forced = getenv("BEA_CE_NO_HANDOFF");
	if (!*sRunningEntry && forced && *forced && strcmp(forced, "0"))
	{
		/* The base was forced to run in place of the active version: never install from here (no downgrades). */
		say("update: not checking while BEA_CE_NO_HANDOFF runs the base version");
		return UPD_DISABLED;
	}
	/* Nothing at or below the newest installed version is offered: the running one, the active one, and every other
	 * recorded entry (after --rollback the newer one stays installed; --rollback switches back to it). */
	char floor[32], vdir[PATHN];
	snprintf(floor, sizeof floor, "%s", CE_VERSION);
	if (upd_join(vdir, sizeof vdir, sRoot, "versions") && upd_kind(vdir, NULL) != UPD_ABSENT)
	{
		UpdState st;
		UpdId vid;
		int loaded = upd_state_load(vdir, &st);
		if (loaded < 0 || (loaded == 1 && (upd_kind(vdir, &vid) != UPD_DIR || !upd_same(&vid, &st.dir))))
		{
			say("update: " UPD_RECOVERY);
			return UPD_INSTALL_FAILED;
		}
		for (int i = 0; loaded == 1 && i < st.n; ++i)
			if (st.e[i].ready && upd_version_cmp(st.e[i].version, floor) > 0)
				snprintf(floor, sizeof floor, "%s", st.e[i].version);
	}
	if (!upd_join(area, sizeof area, sUser, "update-v2")) return UPD_INSTALL_FAILED;
	int made = upd_mkdir(area, 1);
	if (made < 0 || upd_kind(area, NULL) != UPD_DIR) { say("update: the update-v2 folder could not be made or is a link"); return UPD_INSTALL_FAILED; }
	UpdLock lk;
	UpdJournal jr;
	int locked = upd_lock(&lk, area);
	if (locked == 0) { say("update: another Onslaught CE is checking for updates"); return UPD_BUSY; }
	if (locked < 0) { say("update: the update-v2 lock is not usable"); return UPD_INSTALL_FAILED; }
	upd_journal_read(&lk, &jr);
	upd_stage_recover(area, &lk, &jr);
	int rc = UPD_INSTALL_FAILED;
	char name[64];
	UpdId sid;
	if (!upd_random_hex(hex)) goto unlock;
	snprintf(name, sizeof name, "stage-%s", hex);
	snprintf(jr.stage, sizeof jr.stage, "%s", name);
	jr.stage_has_id = 0;
	if (!upd_join(stage, sizeof stage, area, name) || !upd_journal_write(&lk, &jr) || upd_mkdir(stage, 1) != 1 ||
		upd_kind(stage, &sid) != UPD_DIR)
	{
		say("update: could not make a stage folder");
		goto recover;
	}
	jr.stage_id = sid;
	jr.stage_has_id = 1;
	if (!upd_journal_write(&lk, &jr)) goto recover;
	upd_crash_point("stage-created");
	upd_join(feed, sizeof feed, stage, UPD_STAGE_FILES[0]);
	upd_join(sig, sizeof sig, stage, UPD_STAGE_FILES[1]);
	upd_join(pkg, sizeof pkg, stage, UPD_STAGE_FILES[2]);
	snprintf(sigurl, sizeof sigurl, "%s.sig", upd_feed_url());
	uint64_t fn = 0, sn = 0, pn = 0;
	say("update: checking for a signed v2 feed");
	if (!upd_fetch(upd_feed_url(), feed, UPD_FEED_MAX, 20, &fn) || !upd_fetch(sigurl, sig, 256, 20, &sn))
	{
		say("update: no feed reached (offline, or no update published)");
		rc = UPD_OFFLINE;
		goto recover;
	}
	upd_crash_point("feed-fetched");
	size_t flen = 0, slen = 0;
	char *ftext = upd_read_small(feed, UPD_FEED_MAX, &flen), *stext = upd_read_small(sig, 256, &slen);
	UpdOffer o;
	const char *why = "";
	if (!ftext || !stext || !upd_signature_ok(ftext, flen, stext, slen))
	{
		say("update: the feed's signature did not verify (%" PRIu64 " + %" PRIu64 " bytes); ignored", fn, sn);
		free(ftext); free(stext);
		rc = UPD_BAD_FEED;
		goto recover;
	}
	int parsed = upd_parse_feed(ftext, flen, &o, &why);
	free(ftext); free(stext);
	if (!parsed) { say("update: the signed feed was refused: %s", why); rc = UPD_BAD_FEED; goto recover; }
	say("update: signed feed verified: v%s, %" PRIu64 " bytes for " PLATFORM, o.version, o.size);
	if (upd_version_cmp(o.version, floor) <= 0)
	{
		if (upd_version_cmp(o.version, CE_VERSION) > 0)
			say("update: v%s is already installed in versions; --rollback switches to it", o.version);
		else if (explicit_cmd) say("update: v%s is current", CE_VERSION);
		rc = UPD_CURRENT;
		goto recover;
	}
	if (ini_get("skip_version", opt, sizeof opt) && !strcmp(opt, o.version))
	{
		say("update: v%s was skipped by the player", o.version);
		rc = UPD_NOT_NOW;
		goto recover;
	}
	int answer = upd_ask(&o);
	say("update: offered v%s: %s", o.version, answer == 1 ? "update" : answer == 2 ? "skip this version" :
		answer == 0 ? "later" : "nobody could be asked (no display)");
	if (answer == 2) ini_set("skip_version", o.version);
	if (answer != 1) { rc = UPD_NOT_NOW; goto recover; }
	if (!upd_fetch(o.url, pkg, o.size, 1800, &pn))
	{
		say("update: the package download failed or exceeded its signed size (%" PRIu64 " of %" PRIu64 " bytes)", pn, o.size);
		upd_tell(explicit_cmd, "The update could not be downloaded. Nothing was changed: this version starts now.");
		rc = UPD_BAD_PACKAGE;
		goto recover;
	}
	upd_crash_point("package-downloaded");
	uint8_t want[32], got[32];
	if (pn != o.size || !hex_to(o.sha, want, 32) || !sha256_file(pkg, got) || memcmp(got, want, 32))
	{
		say("update: the package does not match the signed feed (%" PRIu64 " bytes, expected %" PRIu64 "); discarded", pn, o.size);
		upd_tell(explicit_cmd, "The downloaded update did not match the signed release. Nothing was changed: this version starts now.");
		rc = UPD_BAD_PACKAGE;
		goto recover;
	}
	say("update: package size and SHA-256 match the signed feed");
	char installed[64];
	rc = upd_install(pkg, &o, installed, sizeof installed);
	if (rc != UPD_UPDATED && rc != UPD_BUSY)
		upd_tell(explicit_cmd, "The update could not be installed. Nothing was changed: this version starts now.");
recover:
	upd_stage_recover(area, &lk, &jr);
unlock:
	upd_unlock(&lk);
	return rc;
}

/* --rollback: the previous version becomes active again (one atomic state replacement); nothing is removed. */
static int upd_rollback(void)
{
	char vdir[PATHN];
	UpdLock lk;
	UpdJournal jr;
	UpdState st;
	UpdId id;
	if (!sRootUsable || !upd_join(vdir, sizeof vdir, sRoot, "versions") || upd_kind(vdir, &id) != UPD_DIR)
	{
		say("rollback: there is no updated version to roll back from");
		return UPD_CURRENT;
	}
	int locked = upd_lock(&lk, vdir);
	if (locked <= 0) { say("rollback: the versions folder is busy or its lock is not usable"); return locked ? UPD_INSTALL_FAILED : UPD_BUSY; }
	upd_journal_read(&lk, &jr);
	int rc = UPD_INSTALL_FAILED;
	if (upd_state_load(vdir, &st) != 1 || !upd_same(&id, &st.dir)) { say("rollback: " UPD_RECOVERY); goto out; }
	if (!strcmp(st.previous, "none")) { say("rollback: no previous version is recorded"); rc = UPD_CURRENT; goto out; }
	char was[64];
	snprintf(was, sizeof was, "%s", st.active);
	snprintf(st.active, sizeof st.active, "%s", st.previous);
	snprintf(st.previous, sizeof st.previous, "%s", was);
	if (upd_state_store(vdir, &lk, &jr, &st)) { say("rollback: %s is active again (previous: %s)", st.active, was); rc = 0; }
out:
	upd_unlock(&lk);
	return rc;
}

#endif
