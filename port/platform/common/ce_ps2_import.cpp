// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_ps2_import.cpp: native read-only PS2 Level 888 importer for Onslaught CE.
// Reads the player's own PS2 disc image (.iso, streamed Redump .zip with ZIP32/ZIP64, or extracted LVLR .aya),
// converts the PS2 multiplayer Fenrir mesh (P2VB/MMPV -> PC PMVB), pairs the PS2 world body and static shadows
// with the player's own PC level 857 terrain and flagship descriptors, validates the resulting archive against
// the mod loader's packed/chunk/inline-mesh rules, atomically stages <user folder>/mods/flagship-duel-ps2/,
// and enables flagship-duel-ps2 in ce.ini via PortModsWriteLine.
#ifndef _WIN32
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE 1
#endif
#endif

#include "ce_ps2_import.h"
#include "mods.h"
#include "portlog.h"

#include <zlib.h>

#ifndef CE_PS2_IMPORT_NO_SDL
#include <SDL3/SDL.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include "../../updater/sha256.h"
int CE_SettingGetText(const char *key, char *out, int n);
}

static_assert(sizeof(uint64_t) == 8, "64-bit integers required");
#ifndef _WIN32
static_assert(sizeof(off_t) >= 8, "64-bit off_t required for dual-layer DVD images");
#endif

namespace
{

// ---- constants & ceilings --------------------------------------------------------------------------------------------

constexpr uint64_t	kSector = 2048;
constexpr uint64_t	kFirstDescriptorSector = 16;
constexpr uint32_t	kMaxDescriptors = 32;
constexpr uint64_t	kMaxImageBytes = 9000000000ULL;		// dual-layer DVD ceiling
constexpr uint32_t	kMaxDirectoryBytes = 1u << 20;		// 1 MiB root directory ceiling
constexpr uint32_t	kRcdfVersion = 100;
constexpr uint32_t	kRcdfHeaderBytes = 2048;
constexpr uint32_t	kRcdfRowBytes = 264;
constexpr uint32_t	kRcdfPathBytes = 256;
constexpr uint32_t	kMaxRcdfRows = 16384;
constexpr uint32_t	kMaxMemberBytes = 64u << 20;		// 64 MiB archive ceiling
constexpr int		kMaxStreamPasses = 2;
constexpr size_t	kStreamChunk = 256u << 10;			// 256 KiB read/inflate step

constexpr int		kTargetLevel = 888;
constexpr int		kDonorLevel = 857;
constexpr int		kSourceLevels[4] = {731, 732, 741, 742};
constexpr uint32_t	kInlineMeshId = 20000;
constexpr size_t	kLimitSmall = 16u << 20;
constexpr size_t	kLimitArchive = 64u << 20;
constexpr size_t	kPackBlock = 1u << 20;
constexpr size_t	kPackBuffer = 0x102927;
constexpr size_t	kMaxAddedPacked = 8u << 20;
constexpr size_t	kMaxAddedUnpacked = 32u << 20;
constexpr int		kMaxAddedChunks = 2048;

const char kPackageName[] = "DATA0.NYO";
const char kLevel888Member[] = "\\data\\Resources\\888_res_PS2.aya";
const char kFenrirMeshName[] = "fenrir-multiplayer.msh";
const char kArchiveRelPath[] = "data/resources/888_res_PC.aya";
const char kPhysicsRelPath[] = "data/default physics.dat";
const char kWorldHeadersRelPath[] = "data/worldheaders.dat";

const char kModId[] = "flagship-duel-ps2";
const char kModName[] = "Flagship Duel (PS2 Level 888)";
const char kModVersion[] = "1.0";
const char kModDescription[] =
	"Cut multiplayer Level 888 (Fenrir vs Venturer), imported locally from your own PS2 disc.";
const char kDefaultCeMin[] = "0.1.13";

// Error strings: concise plain English, FONT_SMALL-safe ASCII, ending with a period.
const char kErrCancelled[] = "PS2 Level 888 import was cancelled.";
const char kErrSourceOpen[] = "The selected PS2 file could not be opened as a normal file.";
const char kErrSourceInside[] = "Choose a PS2 file outside your game and user folders.";
const char kErrChd[] = "CHD disc images are not supported. Choose an .iso or .zip disc image.";
const char kErrZipMembers[] = "The selected zip does not hold a single PS2 .iso disc image.";
const char kErrZipEncrypted[] = "Encrypted zip archives are not supported. Unpack the .iso first.";
const char kErrZipDamaged[] = "The selected zip archive is damaged or uses an unsupported format.";
const char kErrImageTooBig[] = "The selected disc image is larger than a dual-layer DVD.";
const char kErrBadIso[] = "The selected file is not a valid ISO-9660 PS2 disc image.";
const char kErrNoPackage[] = "DATA0.NYO was not found on the disc. Choose a Battle Engine Aquila PS2 disc.";
const char kErrBadPackage[] = "The PS2 disc package (DATA0.NYO) is damaged or unsupported.";
const char kErrNoLevel888[] = "Level 888 was not found in the PS2 disc package.";
const char kErrBadMember[] = "The PS2 Level 888 archive exceeds the size limit or is damaged.";
const char kErrBadWorld[] = "The PS2 Level 888 world or shadow data is damaged.";
const char kErrBadMesh[] = "The PS2 Fenrir mesh is missing or could not be converted.";
const char kErrAlreadyInGame[] = "Your game folder already contains a Level 888 archive.";
const char kErrBadGameFiles[] = "Your PC game files could not be read to build Level 888.";
const char kErrVerifyFailed[] = "The built Level 888 archive failed verification.";
const char kErrStageFailed[] = "The Level 888 mod could not be written to your mods folder.";
const char kErrSettingsFailed[] = "Level 888 was imported, but your settings could not be saved.";

// ---- basic utilities -------------------------------------------------------------------------------------------------

void CopyStr(char *dst, size_t cap, const char *src)
{
	if (!dst || !cap)
		return;
	snprintf(dst, cap, "%s", src ? src : "");
}

uint16_t Le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (uint16_t(p[1]) << 8));
}

uint16_t Be16(const uint8_t *p)
{
	return (uint16_t)((uint16_t(p[0]) << 8) | p[1]);
}

uint32_t Le32(const uint8_t *p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint32_t Be32(const uint8_t *p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

int32_t LeI32(const uint8_t *p)
{
	return (int32_t)Le32(p);
}

uint64_t Le64(const uint8_t *p)
{
	return uint64_t(Le32(p)) | (uint64_t(Le32(p + 4)) << 32);
}

void PutLe16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
}

void PutLe32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
	p[2] = (uint8_t)((v >> 16) & 0xff);
	p[3] = (uint8_t)((v >> 24) & 0xff);
}

void AppendLe32(std::vector<uint8_t> &out, uint32_t v)
{
	uint8_t b[4];
	PutLe32(b, v);
	out.insert(out.end(), b, b + 4);
}

void AppendChunk(std::vector<uint8_t> &out, const char tag[4], const uint8_t *payload, size_t len)
{
	out.insert(out.end(), (const uint8_t *)tag, (const uint8_t *)tag + 4);
	AppendLe32(out, (uint32_t)len);
	if (len)
		out.insert(out.end(), payload, payload + len);
}

void AppendChunk(std::vector<uint8_t> &out, const char tag[4], const std::vector<uint8_t> &payload)
{
	AppendChunk(out, tag, payload.empty() ? nullptr : payload.data(), payload.size());
}

std::string AsciiLower(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
	return out;
}

std::string NormalizeAssetName(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
	{
		if (c == '/')
			c = '\\';
		else if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
	}
	return out;
}

std::string FoldMemberPath(const std::string &s)
{
	std::string out = NormalizeAssetName(s);
	if (out.empty() || out[0] != '\\')
		out.insert(out.begin(), '\\');
	return out;
}

bool EndsWithIgnoreCase(const std::string &s, const char *suffix)
{
	const size_t n = strlen(suffix);
	if (s.size() < n)
		return false;
	for (size_t i = 0; i < n; i++)
	{
		char a = s[s.size() - n + i];
		char b = suffix[i];
		if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
		if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
		if (a != b)
			return false;
	}
	return true;
}

bool ValidTagChar(uint8_t c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ' ';
}

bool ValidTag(const uint8_t *p)
{
	return ValidTagChar(p[0]) && ValidTagChar(p[1]) && ValidTagChar(p[2]) && ValidTagChar(p[3]);
}

void Sha256Hex(const uint8_t *data, size_t len, char out_hex[65])
{
	uint8_t digest[32];
	SHA256_CTX ctx;
	sha256_init(&ctx);
	if (len)
		sha256_update(&ctx, data, len);
	sha256_final(&ctx, digest);
	static const char kHex[] = "0123456789abcdef";
	for (int i = 0; i < 32; i++)
	{
		out_hex[2 * i] = kHex[digest[i] >> 4];
		out_hex[2 * i + 1] = kHex[digest[i] & 0x0f];
	}
	out_hex[64] = 0;
}

bool IsCancelled(const std::atomic<bool> *cancel)
{
	return cancel && cancel->load(std::memory_order_relaxed);
}

struct ImportFail
{
	const char *msg;
};

[[noreturn]] void Fail(const char *msg)
{
	throw ImportFail{msg};
}

void CheckCancel(const std::atomic<bool> *cancel)
{
	if (IsCancelled(cancel))
		Fail(kErrCancelled);
}

// ---- cross-platform UTF-8 / Win32 wide helpers & file safety ---------------------------------------------------------

#ifdef _WIN32
bool Utf8ToWide(const std::string &utf8, std::vector<wchar_t> *out)
{
	out->clear();
	if (utf8.empty())
		return false;
	const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), -1, nullptr, 0);
	if (needed <= 0 || needed > 32767)
		return false;
	out->resize((size_t)needed);
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), -1, out->data(), needed) == needed;
}

bool WideToUtf8(const wchar_t *wide, std::string *out)
{
	out->clear();
	if (!wide || !*wide)
		return false;
	const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 1)
		return false;
	std::vector<char> buf((size_t)needed);
	if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf.data(), needed, nullptr, nullptr) != needed)
		return false;
	out->assign(buf.data(), (size_t)(needed - 1));
	return true;
}

std::string NormalizePathKeyWin(const std::string &p)
{
	std::string out;
	out.reserve(p.size());
	for (char c : p)
	{
		char n = (c == '/') ? '\\' : c;
		if (n >= 'A' && n <= 'Z')
			n = (char)(n + 32);
		if (n == '\\' && !out.empty() && out.back() == '\\' && out.size() > 2)
			continue;
		out.push_back(n);
	}
	while (out.size() > 3 && out.back() == '\\')
		out.pop_back();
	return out;
}

bool WinFinalPath(HANDLE h, std::string *out)
{
	wchar_t buf[4096];
	const DWORD len = GetFinalPathNameByHandleW(h, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), FILE_NAME_NORMALIZED);
	if (len == 0 || len >= sizeof(buf) / sizeof(buf[0]))
		return false;
	std::string utf8;
	if (!WideToUtf8(buf, &utf8))
		return false;
	*out = NormalizePathKeyWin(utf8);
	return true;
}
#else
std::string NormalizeLexicalPosix(const std::string &p)
{
	std::string out;
	out.reserve(p.size());
	for (char c : p)
	{
		if (c == '/' && !out.empty() && out.back() == '/')
			continue;
		out.push_back(c);
	}
	while (out.size() > 1 && out.back() == '/')
		out.pop_back();
	return out;
}
#endif

bool PathEqualsOrInside(const std::string &candidate, const std::string &dir, char sep)
{
	if (candidate.empty() || dir.empty())
		return false;
	if (candidate == dir)
		return true;
	if (dir.size() == 1 && dir[0] == sep)
		return true;
	return candidate.size() > dir.size() &&
		   candidate.compare(0, dir.size(), dir) == 0 &&
		   candidate[dir.size()] == sep;
}

// A regular, non-symlink read-only file supporting 64-bit random reads.
class RawFile
{
public:
	RawFile() = default;
	RawFile(const RawFile &) = delete;
	RawFile &operator=(const RawFile &) = delete;
	~RawFile() { Close(); }

	bool OpenRegular(const char *path, uint64_t *size_out)
	{
		Close();
		if (!path || !*path || strlen(path) >= 4096)
			return false;
#ifdef _WIN32
		std::vector<wchar_t> wpath;
		if (!Utf8ToWide(path, &wpath))
			return false;
		HANDLE h = CreateFileW(wpath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
							   FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;
		BY_HANDLE_FILE_INFORMATION info{};
		LARGE_INTEGER sz{};
		if (GetFileType(h) != FILE_TYPE_DISK ||
			!GetFileInformationByHandle(h, &info) ||
			(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) ||
			!GetFileSizeEx(h, &sz) || sz.QuadPart < 0)
		{
			CloseHandle(h);
			return false;
		}
		mHandle = h;
		mSize = (uint64_t)sz.QuadPart;
		if (size_out)
			*size_out = mSize;
		return true;
#else
		struct stat lst{};
		if (lstat(path, &lst) != 0 || S_ISLNK(lst.st_mode) || !S_ISREG(lst.st_mode))
			return false;
		int flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
#ifdef O_LARGEFILE
		flags |= O_LARGEFILE;
#endif
		const int fd = open(path, flags);
		if (fd < 0)
			return false;
		struct stat st{};
		if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
			st.st_dev != lst.st_dev || st.st_ino != lst.st_ino)
		{
			close(fd);
			return false;
		}
		mFd = fd;
		mSize = (uint64_t)st.st_size;
		if (size_out)
			*size_out = mSize;
		return true;
#endif
	}

	bool CheckOutside(const char *path_str, const char *game_dir, const char *user_dir) const
	{
#ifdef _WIN32
		std::string fileFinal;
		const std::string fileLex = NormalizePathKeyWin(path_str ? path_str : "");
		const bool haveFileFinal = mHandle != INVALID_HANDLE_VALUE && WinFinalPath(mHandle, &fileFinal);
		for (const char *root : {game_dir, user_dir})
		{
			if (!root || !*root)
				continue;
			const std::string rootLex = NormalizePathKeyWin(root);
			if (!rootLex.empty() && PathEqualsOrInside(fileLex, rootLex, '\\'))
				return false;
			std::vector<wchar_t> wroot;
			if (haveFileFinal && Utf8ToWide(root, &wroot))
			{
				HANDLE dh = CreateFileW(wroot.data(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
										nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
				if (dh != INVALID_HANDLE_VALUE)
				{
					std::string dirFinal;
					const bool gotDir = WinFinalPath(dh, &dirFinal);
					CloseHandle(dh);
					if (gotDir && PathEqualsOrInside(fileFinal, dirFinal, '\\'))
						return false;
				}
			}
		}
		return true;
#else
		const std::string fileLex = NormalizeLexicalPosix(path_str ? path_str : "");
		std::string parentPath = fileLex;
		const size_t slash = parentPath.rfind('/');
		parentPath = (slash == std::string::npos) ? "." : (slash == 0 ? "/" : parentPath.substr(0, slash));
		char resolvedParent[PATH_MAX];
		std::string realFile;
		if (realpath(parentPath.c_str(), resolvedParent))
		{
			const std::string leaf = (slash == std::string::npos) ? fileLex : fileLex.substr(slash + 1);
			realFile = std::string(resolvedParent) + (strcmp(resolvedParent, "/") == 0 ? "" : "/") + leaf;
		}
		for (const char *root : {game_dir, user_dir})
		{
			if (!root || !*root)
				continue;
			const std::string rootLex = NormalizeLexicalPosix(root);
			if (!rootLex.empty() && rootLex != "." && PathEqualsOrInside(fileLex, rootLex, '/'))
				return false;
			char resolvedRoot[PATH_MAX];
			if (!realFile.empty() && realpath(root, resolvedRoot))
			{
				if (PathEqualsOrInside(realFile, resolvedRoot, '/'))
					return false;
			}
		}
		return true;
#endif
	}

	bool ReadAt(uint64_t offset, uint8_t *dst, size_t len) const
	{
		if (offset > mSize || len > mSize - offset)
			return false;
		if (len == 0)
			return true;
#ifdef _WIN32
		if (mHandle == INVALID_HANDLE_VALUE)
			return false;
		LARGE_INTEGER pos{};
		pos.QuadPart = (LONGLONG)offset;
		if (!SetFilePointerEx(mHandle, pos, nullptr, FILE_BEGIN))
			return false;
		size_t done = 0;
		while (done < len)
		{
			const DWORD chunk = (len - done) > (1u << 20) ? (1u << 20) : (DWORD)(len - done);
			DWORD got = 0;
			if (!ReadFile(mHandle, dst + done, chunk, &got, nullptr) || got == 0)
				return false;
			done += got;
		}
		return true;
#else
		if (mFd < 0)
			return false;
		size_t done = 0;
		while (done < len)
		{
			const size_t ask = std::min(len - done, (size_t)(1u << 20));
			const ssize_t got = pread(mFd, dst + done, ask, (off_t)(offset + done));
			if (got < 0)
			{
				if (errno == EINTR)
					continue;
				return false;
			}
			if (got == 0)
				return false;
			done += (size_t)got;
		}
		return true;
#endif
	}

	uint64_t Size() const { return mSize; }

	void Close()
	{
#ifdef _WIN32
		if (mHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(mHandle);
			mHandle = INVALID_HANDLE_VALUE;
		}
#else
		if (mFd >= 0)
		{
			close(mFd);
			mFd = -1;
		}
#endif
		mSize = 0;
	}

private:
#ifdef _WIN32
	HANDLE		mHandle = INVALID_HANDLE_VALUE;
#else
	int			mFd = -1;
#endif
	uint64_t	mSize = 0;
};

// ---- ISO / streamed ZIP32+ZIP64 image sources ------------------------------------------------------------------------

class ImageSource
{
public:
	virtual ~ImageSource() = default;
	virtual uint64_t Size() const = 0;
	virtual void Read(uint64_t offset, size_t len, uint8_t *out) = 0;
	virtual void FinishFirstPass() {}
	std::vector<uint8_t> ReadVec(uint64_t offset, size_t len)
	{
		std::vector<uint8_t> buf(len);
		if (len)
			Read(offset, len, buf.data());
		return buf;
	}
};

class PlainIsoSource final : public ImageSource
{
public:
	PlainIsoSource(RawFile *file, const std::atomic<bool> *cancel) : mFile(file), mCancel(cancel) {}
	uint64_t Size() const override { return mFile->Size(); }
	void Read(uint64_t offset, size_t len, uint8_t *out) override
	{
		CheckCancel(mCancel);
		if (offset > mFile->Size() || len > mFile->Size() - offset)
			Fail(kErrBadIso);
		size_t done = 0;
		while (done < len)
		{
			CheckCancel(mCancel);
			const size_t step = std::min(len - done, kStreamChunk);
			if (!mFile->ReadAt(offset + done, out + done, step))
				Fail(kErrBadIso);
			done += step;
		}
	}

private:
	RawFile					*mFile;
	const std::atomic<bool>	*mCancel;
};

// Single-ISO ZIP32/ZIP64 reader: streams forward via zlib in at most 2 passes, never writing the ISO to disk.
class StreamedZipSource final : public ImageSource
{
public:
	StreamedZipSource(RawFile *file, const std::atomic<bool> *cancel)
		: mFile(file), mCancel(cancel), mInBuf(kStreamChunk)
	{
		ParseArchive();
		OpenPass();
	}

	~StreamedZipSource() override
	{
		CloseStream();
	}

	uint64_t Size() const override { return mUncompSize; }

	void Read(uint64_t offset, size_t len, uint8_t *out) override
	{
		CheckCancel(mCancel);
		if (offset > mUncompSize || len > mUncompSize - offset)
			Fail(kErrBadIso);
		if (offset < mPos)
			OpenPass();
		std::vector<uint8_t> scratch;
		while (mPos < offset)
		{
			CheckCancel(mCancel);
			const size_t skip = (size_t)std::min<uint64_t>(offset - mPos, kStreamChunk);
			if (scratch.size() < skip)
				scratch.resize(skip);
			TakeBytes(scratch.data(), skip);
		}
		TakeBytes(out, len);
	}

	void FinishFirstPass() override
	{
		if (mPos < mUncompSize)
		{
			std::vector<uint8_t> scratch;
			while (mPos < mUncompSize)
			{
				CheckCancel(mCancel);
				const size_t step = (size_t)std::min<uint64_t>(mUncompSize - mPos, kStreamChunk);
				if (scratch.size() < step)
					scratch.resize(step);
				TakeBytes(scratch.data(), step);
			}
		}
	}

private:
	void ParseArchive()
	{
		const uint64_t fileSize = mFile->Size();
		if (fileSize < 22)
			Fail(kErrZipDamaged);

		const size_t tailLen = (size_t)std::min<uint64_t>(fileSize, 65557ULL);
		const uint64_t tailStart = fileSize - tailLen;
		std::vector<uint8_t> tail(tailLen);
		if (!mFile->ReadAt(tailStart, tail.data(), tailLen))
			Fail(kErrZipDamaged);

		int64_t eocdRel = -1;
		for (int64_t i = (int64_t)tailLen - 22; i >= 0; i--)
		{
			if (Le32(&tail[(size_t)i]) == 0x06054b50u)
			{
				const uint16_t commentLen = Le16(&tail[(size_t)i + 20]);
				if ((uint64_t)i + 22 + commentLen == tailLen)
				{
					eocdRel = i;
					break;
				}
			}
		}
		if (eocdRel < 0)
			Fail(kErrZipDamaged);

		const uint8_t *eocd = &tail[(size_t)eocdRel];
		const uint64_t eocdOffset = tailStart + (uint64_t)eocdRel;

		uint32_t diskNum = Le16(eocd + 4);
		uint32_t cdDisk = Le16(eocd + 6);
		uint64_t diskEntries = Le16(eocd + 8);
		uint64_t totalEntries = Le16(eocd + 10);
		uint64_t cdSize = Le32(eocd + 12);
		uint64_t cdOffset = Le32(eocd + 16);
		uint64_t cdUpperBound = eocdOffset;

		const bool hasLocator = eocdOffset >= 20 && [&]() {
			uint8_t loc[20];
			if (!mFile->ReadAt(eocdOffset - 20, loc, sizeof(loc)))
				return false;
			if (Le32(loc) != 0x07064b50u)
				return false;
			const uint32_t z64Disk = Le32(loc + 4);
			const uint64_t z64Off = Le64(loc + 8);
			const uint32_t totalDisks = Le32(loc + 16);
			if (z64Disk != 0 || totalDisks > 1 || z64Off > eocdOffset - 20 || (eocdOffset - 20) - z64Off < 56)
				Fail(kErrZipDamaged);
			uint8_t z64[56];
			if (!mFile->ReadAt(z64Off, z64, sizeof(z64)) || Le32(z64) != 0x06064b50u)
				Fail(kErrZipDamaged);
			const uint64_t z64RecSize = Le64(z64 + 4);
			if (z64RecSize < 44 || z64RecSize > (eocdOffset - 20) - z64Off - 12)
				Fail(kErrZipDamaged);
			diskNum = Le32(z64 + 16);
			cdDisk = Le32(z64 + 20);
			diskEntries = Le64(z64 + 24);
			totalEntries = Le64(z64 + 32);
			cdSize = Le64(z64 + 40);
			cdOffset = Le64(z64 + 48);
			cdUpperBound = z64Off;
			return true;
		}();

		if (!hasLocator && (diskNum == 0xffffu || cdDisk == 0xffffu || diskEntries == 0xffffu ||
							totalEntries == 0xffffu || cdSize == 0xffffffffu || cdOffset == 0xffffffffu))
			Fail(kErrZipDamaged);

		if (diskNum != 0 || cdDisk != 0)
			Fail(kErrZipDamaged);
		if (totalEntries != 1 || diskEntries != 1)
			Fail(kErrZipMembers);
		if (cdSize < 46 || cdSize > (1u << 20) || cdOffset > cdUpperBound || cdSize > cdUpperBound - cdOffset)
			Fail(kErrZipDamaged);

		std::vector<uint8_t> cd((size_t)cdSize);
		if (!mFile->ReadAt(cdOffset, cd.data(), cd.size()) || Le32(cd.data()) != 0x02014b50u)
			Fail(kErrZipDamaged);

		const uint16_t flags = Le16(&cd[8]);
		const uint16_t method = Le16(&cd[10]);
		const uint32_t crc = Le32(&cd[16]);
		uint64_t compSize = Le32(&cd[20]);
		uint64_t uncompSize = Le32(&cd[24]);
		const uint16_t nameLen = Le16(&cd[28]);
		const uint16_t extraLen = Le16(&cd[30]);
		const uint16_t commentLen = Le16(&cd[32]);
		uint32_t diskStart = Le16(&cd[34]);
		uint64_t lfhOffset = Le32(&cd[42]);

		if (46ULL + nameLen + extraLen + commentLen != cdSize)
			Fail(kErrZipMembers);
		if (flags & 0x1u)
			Fail(kErrZipEncrypted);
		if (method != 0 && method != 8)
			Fail(kErrZipDamaged);

		const std::string filename((const char *)&cd[46], nameLen);
		if (filename.empty() || filename.back() == '/' || filename.back() == '\\' || !EndsWithIgnoreCase(filename, ".iso"))
			Fail(kErrZipMembers);

		const bool needUncomp64 = (uncompSize == 0xffffffffULL);
		const bool needComp64 = (compSize == 0xffffffffULL);
		const bool needLfh64 = (lfhOffset == 0xffffffffULL);
		const bool needDisk64 = (diskStart == 0xffffu);
		if (needUncomp64 || needComp64 || needLfh64 || needDisk64)
		{
			bool parsed64 = false;
			size_t pos = 46 + (size_t)nameLen;
			const size_t end = pos + (size_t)extraLen;
			while (pos + 4 <= end)
			{
				const uint16_t hid = Le16(&cd[pos]);
				const uint16_t dsz = Le16(&cd[pos + 2]);
				pos += 4;
				if (dsz > end - pos)
					Fail(kErrZipDamaged);
				if (hid == 0x0001u)
				{
					size_t cursor = pos;
					const size_t fEnd = pos + dsz;
					if (needUncomp64)
					{
						if (fEnd - cursor < 8) Fail(kErrZipDamaged);
						uncompSize = Le64(&cd[cursor]);
						cursor += 8;
					}
					if (needComp64)
					{
						if (fEnd - cursor < 8) Fail(kErrZipDamaged);
						compSize = Le64(&cd[cursor]);
						cursor += 8;
					}
					if (needLfh64)
					{
						if (fEnd - cursor < 8) Fail(kErrZipDamaged);
						lfhOffset = Le64(&cd[cursor]);
						cursor += 8;
					}
					if (needDisk64)
					{
						if (fEnd - cursor < 4) Fail(kErrZipDamaged);
						diskStart = Le32(&cd[cursor]);
						cursor += 4;
					}
					parsed64 = true;
					break;
				}
				pos += dsz;
			}
			if (!parsed64)
				Fail(kErrZipDamaged);
		}

		if (diskStart != 0)
			Fail(kErrZipDamaged);
		if (uncompSize > kMaxImageBytes)
			Fail(kErrImageTooBig);
		if (lfhOffset > cdOffset || cdOffset - lfhOffset < 30)
			Fail(kErrZipDamaged);

		uint8_t lfh[30];
		if (!mFile->ReadAt(lfhOffset, lfh, sizeof(lfh)) || Le32(lfh) != 0x04034b50u)
			Fail(kErrZipDamaged);
		const uint64_t lfhSkip = 30ULL + Le16(lfh + 26) + Le16(lfh + 28);
		if (lfhSkip > cdOffset - lfhOffset)
			Fail(kErrZipDamaged);
		const uint64_t dataOffset = lfhOffset + lfhSkip;
		if (compSize > cdOffset - dataOffset)
			Fail(kErrZipDamaged);
		if (method == 0 && compSize != uncompSize)
			Fail(kErrZipDamaged);
		if (method == 8 && uncompSize > 0 && compSize == 0)
			Fail(kErrZipDamaged);

		mMethod = method;
		mExpectedCrc = crc;
		mDataOffset = dataOffset;
		mCompSize = compSize;
		mUncompSize = uncompSize;
	}

	void CloseStream()
	{
		if (mZlibOpen)
		{
			inflateEnd(&mZs);
			mZlibOpen = false;
		}
	}

	void OpenPass()
	{
		CloseStream();
		if (mPasses >= kMaxStreamPasses)
			Fail(kErrZipDamaged);
		mPasses++;
		mPos = 0;
		mCompPos = 0;
		mRunningCrc = (uint32_t)crc32(0L, Z_NULL, 0);
		mStreamEnded = false;
		if (mMethod == 8)
		{
			memset(&mZs, 0, sizeof(mZs));
			if (inflateInit2(&mZs, -MAX_WBITS) != Z_OK)
				Fail(kErrZipDamaged);
			mZlibOpen = true;
		}
	}

	void TakeBytes(uint8_t *dst, size_t len)
	{
		size_t outPos = 0;
		while (outPos < len)
		{
			CheckCancel(mCancel);
			if (mMethod == 0)
			{
				const size_t step = std::min(len - outPos, kStreamChunk);
				if (step > mCompSize - mCompPos || !mFile->ReadAt(mDataOffset + mCompPos, dst + outPos, step))
					Fail(kErrZipDamaged);
				mRunningCrc = (uint32_t)crc32(mRunningCrc, dst + outPos, (uInt)step);
				mCompPos += step;
				mPos += step;
				outPos += step;
			}
			else
			{
				if (mStreamEnded)
					Fail(kErrZipDamaged);
				if (mZs.avail_in == 0 && mCompPos < mCompSize)
				{
					const size_t ask = (size_t)std::min<uint64_t>(mCompSize - mCompPos, mInBuf.size());
					if (!mFile->ReadAt(mDataOffset + mCompPos, mInBuf.data(), ask))
						Fail(kErrZipDamaged);
					mCompPos += ask;
					mZs.next_in = mInBuf.data();
					mZs.avail_in = (uInt)ask;
				}
				const size_t want = std::min(len - outPos, kStreamChunk);
				mZs.next_out = dst + outPos;
				mZs.avail_out = (uInt)want;
				const int rc = inflate(&mZs, Z_NO_FLUSH);
				const size_t got = want - mZs.avail_out;
				if (got > 0)
				{
					mRunningCrc = (uint32_t)crc32(mRunningCrc, dst + outPos, (uInt)got);
					mPos += got;
					outPos += got;
				}
				if (rc == Z_STREAM_END)
				{
					mStreamEnded = true;
					if (outPos < len)
						Fail(kErrZipDamaged);
				}
				else if (rc != Z_OK)
				{
					Fail(kErrZipDamaged);
				}
				else if (got == 0 && mZs.avail_in == 0 && mCompPos >= mCompSize)
				{
					Fail(kErrZipDamaged);
				}
			}
		}

		if (mPos == mUncompSize)
		{
			if (mMethod == 8 && !mStreamEnded)
			{
				// Pump final deflate end-of-block marker if all uncompressed bytes were just produced.
				uint8_t dummy = 0;
				while (!mStreamEnded)
				{
					if (mZs.avail_in == 0 && mCompPos < mCompSize)
					{
						const size_t ask = (size_t)std::min<uint64_t>(mCompSize - mCompPos, mInBuf.size());
						if (!mFile->ReadAt(mDataOffset + mCompPos, mInBuf.data(), ask))
							Fail(kErrZipDamaged);
						mCompPos += ask;
						mZs.next_in = mInBuf.data();
						mZs.avail_in = (uInt)ask;
					}
					mZs.next_out = &dummy;
					mZs.avail_out = 1;
					const int rc = inflate(&mZs, Z_NO_FLUSH);
					if (mZs.avail_out == 0)
						Fail(kErrZipDamaged);
					if (rc == Z_STREAM_END)
						mStreamEnded = true;
					else if (rc != Z_OK || (mZs.avail_in == 0 && mCompPos >= mCompSize))
						Fail(kErrZipDamaged);
				}
			}
			if (mRunningCrc != mExpectedCrc)
				Fail(kErrZipDamaged);
		}
	}

	RawFile					*mFile;
	const std::atomic<bool>	*mCancel;
	std::vector<uint8_t>	mInBuf;
	uint16_t				mMethod = 0;
	uint32_t				mExpectedCrc = 0;
	uint64_t				mDataOffset = 0;
	uint64_t				mCompSize = 0;
	uint64_t				mUncompSize = 0;
	int						mPasses = 0;
	uint64_t				mPos = 0;
	uint64_t				mCompPos = 0;
	uint32_t				mRunningCrc = 0;
	bool					mZlibOpen = false;
	bool					mStreamEnded = false;
	z_stream				mZs{};
};

// ---- ISO-9660 + RCDF v100 extraction (ps2_disc.py) -------------------------------------------------------------------

uint32_t BothU32(const uint8_t *p, const char *err)
{
	const uint32_t le = Le32(p);
	const uint32_t be = Be32(p + 4);
	if (le != be)
		Fail(err);
	return le;
}

uint16_t BothU16(const uint8_t *p, const char *err)
{
	const uint16_t le = Le16(p);
	const uint16_t be = Be16(p + 2);
	if (le != be)
		Fail(err);
	return le;
}

struct RcdfRow
{
	std::string	path;
	uint32_t	offset = 0;
	uint32_t	size = 0;
};

std::vector<uint8_t> ReadPs2MemberFromDisc(ImageSource &source)
{
	const uint64_t sourceSize = source.Size();
	if (sourceSize > kMaxImageBytes)
		Fail(kErrImageTooBig);
	if ((sourceSize % kSector) != 0 || sourceSize < (kFirstDescriptorSector + 2) * kSector)
		Fail(kErrBadIso);

	// 1. Primary Volume Descriptor & terminator
	std::vector<uint8_t> pvd;
	uint64_t lastDescriptorSector = 0;
	bool haveTerminator = false;
	for (uint32_t idx = 0; idx < kMaxDescriptors; idx++)
	{
		const uint64_t sector = kFirstDescriptorSector + idx;
		if ((sector + 1) * kSector > sourceSize)
			Fail(kErrBadIso);
		std::vector<uint8_t> desc = source.ReadVec(sector * kSector, (size_t)kSector);
		if (memcmp(&desc[1], "CD001", 5) != 0)
			Fail(kErrBadIso);
		if (desc[0] == 1 && pvd.empty())
		{
			if (desc[6] != 1)
				Fail(kErrBadIso);
			pvd = std::move(desc);
		}
		else if (desc[0] == 255)
		{
			if (pvd.empty())
				Fail(kErrBadIso);
			lastDescriptorSector = sector;
			haveTerminator = true;
			break;
		}
	}
	if (!haveTerminator)
		Fail(kErrBadIso);

	if (BothU16(&pvd[128], kErrBadIso) != kSector)
		Fail(kErrBadIso);
	const uint64_t volumeSectors = BothU32(&pvd[80], kErrBadIso);
	const uint64_t volumeBytes = volumeSectors * kSector;
	if (volumeBytes > sourceSize || volumeBytes <= lastDescriptorSector * kSector)
		Fail(kErrBadIso);

	// 2. Root directory -> DATA0.NYO
	const uint8_t *rootRec = &pvd[156];
	if (rootRec[0] != 34 || !(rootRec[25] & 0x2u))
		Fail(kErrBadIso);
	const uint64_t rootSector = BothU32(rootRec + 2, kErrBadIso);
	const uint64_t rootBytes = BothU32(rootRec + 10, kErrBadIso);
	if (rootSector <= lastDescriptorSector || rootBytes < 68 || rootBytes > kMaxDirectoryBytes)
		Fail(kErrBadIso);
	if (rootSector * kSector > volumeBytes || rootBytes > volumeBytes - rootSector * kSector)
		Fail(kErrBadIso);

	const uint64_t rootEndSector = rootSector + (rootBytes + kSector - 1) / kSector;
	const std::vector<uint8_t> dirData = source.ReadVec(rootSector * kSector, (size_t)rootBytes);

	struct DirHit
	{
		uint32_t extent;
		uint32_t size;
		uint8_t flags;
		uint8_t interleaveA;
		uint8_t interleaveB;
	};
	std::vector<DirHit> hits;
	size_t at = 0;
	while (at < dirData.size())
	{
		const uint8_t recLen = dirData[at];
		if (recLen == 0)
		{
			at = (at / (size_t)kSector + 1) * (size_t)kSector;
			continue;
		}
		if (recLen < 34 || at + recLen > dirData.size() || (at % (size_t)kSector) + recLen > (size_t)kSector)
			Fail(kErrBadIso);
		const uint8_t *rec = &dirData[at];
		const uint8_t nameLen = rec[32];
		if (nameLen == 0 || 33u + nameLen > recLen)
			Fail(kErrBadIso);
		const uint32_t extent = BothU32(rec + 2, kErrBadIso);
		const uint32_t size = BothU32(rec + 10, kErrBadIso);
		const uint8_t flags = rec[25];
		const bool dotOrDotDot = (nameLen == 1 && (rec[33] == 0x00 || rec[33] == 0x01));
		if (!dotOrDotDot && !(flags & 0x2u))
		{
			std::string ident((const char *)(rec + 33), nameLen);
			const size_t semi = ident.find(';');
			if (semi != std::string::npos)
				ident.resize(semi);
			while (!ident.empty() && ident.back() == '.')
				ident.pop_back();
			if (AsciiLower(ident) == AsciiLower(kPackageName))
				hits.push_back({extent, size, flags, rec[26], rec[27]});
		}
		at += recLen;
	}

	if (hits.empty())
		Fail(kErrNoPackage);
	if (hits.size() > 1)
		Fail(kErrBadPackage);

	const DirHit pkg = hits[0];
	if ((pkg.flags & 0x80u) || pkg.interleaveA != 0 || pkg.interleaveB != 0)
		Fail(kErrBadPackage);
	if (pkg.extent < rootEndSector)
		Fail(kErrBadPackage);
	const uint64_t pkgBase = uint64_t(pkg.extent) * kSector;
	const uint64_t pkgBytes = pkg.size;
	if (pkgBase > volumeBytes || pkgBytes > volumeBytes - pkgBase || pkgBytes < kRcdfHeaderBytes + 4)
		Fail(kErrBadPackage);

	// 3. RCDF v100 header & index (pass 1 ends here on zip streams)
	const std::vector<uint8_t> rcdfHead = source.ReadVec(pkgBase, 12);
	if (memcmp(rcdfHead.data(), "RCDF", 4) != 0 || Le32(&rcdfHead[4]) != kRcdfVersion)
		Fail(kErrBadPackage);
	const uint32_t indexOffset = Le32(&rcdfHead[8]);
	if (indexOffset < kRcdfHeaderBytes || indexOffset > pkgBytes - 4)
		Fail(kErrBadPackage);
	const uint64_t span = pkgBytes - indexOffset - 4;
	if ((span % kRcdfRowBytes) != 0)
		Fail(kErrBadPackage);
	const uint64_t rowCount = span / kRcdfRowBytes;
	if (rowCount > kMaxRcdfRows)
		Fail(kErrBadPackage);

	const std::vector<uint8_t> indexBytes = source.ReadVec(pkgBase + indexOffset, (size_t)(4 + rowCount * kRcdfRowBytes));
	if (Le32(indexBytes.data()) != rowCount)
		Fail(kErrBadPackage);
	source.FinishFirstPass();

	std::vector<RcdfRow> rows;
	rows.reserve((size_t)rowCount);
	std::set<std::string> seenPaths;
	const std::string wantKey = FoldMemberPath(kLevel888Member);
	const RcdfRow *foundMember = nullptr;
	int foundIndex = -1;

	for (uint32_t i = 0; i < rowCount; i++)
	{
		const uint8_t *row = &indexBytes[4 + (size_t)i * kRcdfRowBytes];
		const void *nulPtr = memchr(row, 0, kRcdfPathBytes);
		if (!nulPtr || nulPtr == row)
			Fail(kErrBadPackage);
		const size_t pathLen = (size_t)((const uint8_t *)nulPtr - row);
		for (size_t k = 0; k < pathLen; k++)
			if (row[k] < 0x20 || row[k] > 0x7e)
				Fail(kErrBadPackage);
		for (size_t k = pathLen; k < kRcdfPathBytes; k++)
			if (row[k] != 0)
				Fail(kErrBadPackage);
		if (row[0] != '\\')
			Fail(kErrBadPackage);

		const uint32_t memberOffset = Le32(row + kRcdfPathBytes);
		const uint32_t memberSize = Le32(row + kRcdfPathBytes + 4);
		if (memberOffset < kRcdfHeaderBytes || uint64_t(memberOffset) + uint64_t(memberSize) > indexOffset)
			Fail(kErrBadPackage);

		std::string memberPath((const char *)row, pathLen);
		std::string folded = FoldMemberPath(memberPath);
		if (!seenPaths.insert(folded).second)
			Fail(kErrBadPackage);
		if (folded == wantKey)
			foundIndex = (int)rows.size();
		rows.push_back({std::move(memberPath), memberOffset, memberSize});
	}

	std::vector<std::pair<uint32_t, uint32_t>> ranges;
	ranges.reserve(rows.size());
	for (const RcdfRow &r : rows)
		ranges.emplace_back(r.offset, r.size);
	std::sort(ranges.begin(), ranges.end());
	for (size_t i = 1; i < ranges.size(); i++)
		if (uint64_t(ranges[i - 1].first) + uint64_t(ranges[i - 1].second) > ranges[i].first)
			Fail(kErrBadPackage);

	if (foundIndex < 0)
		Fail(kErrNoLevel888);
	foundMember = &rows[(size_t)foundIndex];
	if (foundMember->size < 8 || foundMember->size > kMaxMemberBytes)
		Fail(kErrBadMember);

	// 4. Read 888_res_PS2.aya bytes (pass 2 on zip streams)
	std::vector<uint8_t> member = source.ReadVec(pkgBase + foundMember->offset, foundMember->size);
	if (member.size() < 8 || memcmp(member.data(), "LVLR", 4) != 0)
		Fail(kErrBadMember);
	return member;
}

std::vector<uint8_t> ReadSourceMember(const char *source_path, const char *game_dir, const char *user_dir,
									  const std::atomic<bool> *cancel)
{
	RawFile file;
	uint64_t size = 0;
	if (!file.OpenRegular(source_path, &size))
		Fail(kErrSourceOpen);
	if (!file.CheckOutside(source_path, game_dir, user_dir))
		Fail(kErrSourceInside);
	if (size < 8)
		Fail(kErrBadIso);

	uint8_t magic[8] = {};
	if (!file.ReadAt(0, magic, sizeof(magic)))
		Fail(kErrSourceOpen);
	if (memcmp(magic, "MComprHD", 8) == 0)
		Fail(kErrChd);

	if (memcmp(magic, "LVLR", 4) == 0)
	{
		if (size > kMaxMemberBytes)
			Fail(kErrBadMember);
		std::vector<uint8_t> data((size_t)size);
		size_t done = 0;
		while (done < data.size())
		{
			CheckCancel(cancel);
			const size_t step = std::min(data.size() - done, kStreamChunk);
			if (!file.ReadAt(done, data.data() + done, step))
				Fail(kErrSourceOpen);
			done += step;
		}
		return data;
	}

	if (memcmp(magic, "PK\x03\x04", 4) == 0)
	{
		StreamedZipSource zip(&file, cancel);
		return ReadPs2MemberFromDisc(zip);
	}

	PlainIsoSource iso(&file, cancel);
	return ReadPs2MemberFromDisc(iso);
}

// ---- chunk tiling & PS2 Fenrir mesh conversion (ps2_mesh.py) ---------------------------------------------------------

struct ChunkSpan
{
	char	tag[5];
	size_t	start;	// payload start
	size_t	end;	// payload end
};

std::vector<ChunkSpan> TileChunks(const uint8_t *buf, size_t start, size_t end, const char *err)
{
	if (start > end)
		Fail(err);
	std::vector<ChunkSpan> out;
	size_t at = start;
	while (at < end)
	{
		if (end - at < 8 || !ValidTag(buf + at))
			Fail(err);
		const uint32_t sz = Le32(buf + at + 4);
		if (sz > end - at - 8)
			Fail(err);
		ChunkSpan c{};
		memcpy(c.tag, buf + at, 4);
		c.tag[4] = 0;
		c.start = at + 8;
		c.end = at + 8 + sz;
		out.push_back(c);
		at += 8 + sz;
	}
	return out;
}

struct Ps2Group
{
	uint8_t						texr[24];
	std::vector<uint8_t>		vertices;	// 36 * count
	std::vector<uint8_t>		draws;		// 0 or 1 per record
};

Ps2Group ParsePs2Group(const uint8_t *buf, size_t start, size_t end)
{
	const size_t len = end - start;
	if (len < 4 + 8 + 24 + 8 + 12 + 8 || Le32(buf + start) != 1)
		Fail(kErrBadMesh);
	const std::vector<ChunkSpan> kids = TileChunks(buf, start + 4, end, kErrBadMesh);
	if (kids.size() != 3 ||
		strcmp(kids[0].tag, "TEXR") != 0 || kids[0].end - kids[0].start != 24 ||
		strcmp(kids[1].tag, "NVRT") != 0 || kids[1].end - kids[1].start != 12 ||
		strcmp(kids[2].tag, "VRTS") != 0)
		Fail(kErrBadMesh);

	Ps2Group g{};
	memcpy(g.texr, buf + kids[0].start, 24);
	const uint32_t count = Le32(buf + kids[1].start);
	const uint32_t kicks = Le32(buf + kids[1].start + 4);
	const size_t vrtsBytes = kids[2].end - kids[2].start;
	if (count > 400000u || vrtsBytes != (size_t)count * 112u)
		Fail(kErrBadMesh);

	g.vertices.resize((size_t)count * 36u);
	g.draws.resize(count);
	uint32_t drawSum = 0;
	for (uint32_t n = 0; n < count; n++)
	{
		const uint8_t *rec = buf + kids[2].start + (size_t)n * 112u;
		uint8_t *dst = g.vertices.data() + (size_t)n * 36u;
		// PC 36-byte vertex: pos (w24..26), normal (w0..2), diffuse (w11), uv (w9..10)
		const int srcWords[9] = {24, 25, 26, 0, 1, 2, 11, 9, 10};
		for (int w = 0; w < 9; w++)
			memcpy(dst + w * 4, rec + srcWords[w] * 4, 4);
		const bool draws = (Le32(rec + 27 * 4) == 0);
		g.draws[n] = draws ? 1 : 0;
		drawSum += draws ? 1u : 0u;
	}
	if (drawSum != kicks)
		Fail(kErrBadMesh);
	return g;
}

std::vector<uint16_t> BuildPcStrip(const std::vector<uint16_t> &vids, const std::vector<uint8_t> &draws)
{
	const size_t n = vids.size();
	auto drawsAt = [&](size_t i) -> bool {
		if (!draws[i])
			return false;
		const uint16_t a = vids[i - 2], b = vids[i - 1], c = vids[i];
		return a != b && b != c && a != c;
	};

	std::vector<uint16_t> out;
	size_t i = 2;
	while (i < n)
	{
		if (!drawsAt(i))
		{
			i++;
			continue;
		}
		const size_t a = i;
		while (i + 1 < n && drawsAt(i + 1))
			i++;
		if (!out.empty())
		{
			out.push_back(out.back());
			out.push_back(vids[a - 2]);
		}
		if ((out.size() & 1u) != (a & 1u))
			out.push_back(vids[a - 2]);
		for (size_t k = a - 2; k <= i; k++)
			out.push_back(vids[k]);
		i++;
	}
	return out;
}

std::vector<uint8_t> BuildPcCmvb(size_t groups)
{
	if (groups > 12)
		Fail(kErrBadMesh);
	std::vector<uint8_t> b(296, 0);
	for (size_t g = 0; g < groups; g++)
		PutLe32(&b[8 + 4 * g], (uint32_t)(0x01000000u + 0x100u * g));
	PutLe32(&b[0x108], (uint32_t)groups);
	PutLe32(&b[0x10c], 0xdeadu);
	if (groups > 0)
	{
		PutLe32(&b[0x114], 36u);
		PutLe32(&b[0x118], 0x152u);
		PutLe32(&b[0x11c], 4u);
	}
	return b;
}

std::vector<uint8_t> BuildPcPmvb(const std::vector<Ps2Group> &groups)
{
	std::vector<uint8_t> out;
	const std::vector<uint8_t> cmvb = BuildPcCmvb(groups.size());
	AppendChunk(out, "CMVB", cmvb);
	if (groups.empty())
		return out;

	struct VertexKey
	{
		uint8_t bytes[36];
		bool operator<(const VertexKey &o) const { return memcmp(bytes, o.bytes, 36) < 0; }
	};
	std::map<VertexKey, uint16_t> indexMap;
	std::vector<uint8_t> poolBytes;

	for (const Ps2Group &g : groups)
	{
		const size_t count = g.draws.size();
		for (size_t k = 0; k < count; k++)
		{
			VertexKey vk{};
			memcpy(vk.bytes, &g.vertices[k * 36], 36);
			if (indexMap.find(vk) == indexMap.end())
			{
				const size_t nextIdx = poolBytes.size() / 36;
				if (nextIdx >= 0xffffu)
					Fail(kErrBadMesh);
				indexMap.emplace(vk, (uint16_t)nextIdx);
				poolBytes.insert(poolBytes.end(), vk.bytes, vk.bytes + 36);
			}
		}
	}

	const uint32_t poolCount = (uint32_t)(poolBytes.size() / 36);
	for (size_t gi = 0; gi < groups.size(); gi++)
	{
		const Ps2Group &g = groups[gi];
		const size_t count = g.draws.size();
		std::vector<uint16_t> vids(count);
		for (size_t k = 0; k < count; k++)
		{
			VertexKey vk{};
			memcpy(vk.bytes, &g.vertices[k * 36], 36);
			vids[k] = indexMap[vk];
		}
		const std::vector<uint16_t> strip = BuildPcStrip(vids, g.draws);
		if (strip.size() < 3 || strip.size() > 0xffffu)
			Fail(kErrBadMesh);

		uint8_t mmpt[24];
		PutLe32(mmpt + 0, (uint32_t)poolBytes.size());
		PutLe32(mmpt + 4, (uint32_t)(2 * strip.size()));
		PutLe32(mmpt + 8, (uint32_t)strip.size());
		PutLe32(mmpt + 12, poolCount);
		PutLe32(mmpt + 16, (uint32_t)(strip.size() - 2));
		PutLe32(mmpt + 20, 1u);
		AppendChunk(out, "MMPT", mmpt, sizeof(mmpt));

		std::vector<uint8_t> ibuf(2 * strip.size());
		for (size_t k = 0; k < strip.size(); k++)
			PutLe16(&ibuf[2 * k], strip[k]);
		AppendChunk(out, "IBUF", ibuf);
		if (gi == 0)
			AppendChunk(out, "VBUF", poolBytes);
		else
			AppendChunk(out, "VBUF", nullptr, 0);
		AppendChunk(out, "TEXR", g.texr, sizeof(g.texr));
	}
	return out;
}

std::vector<uint8_t> ConvertPs2Mesp(const uint8_t *buf, size_t start, size_t end)
{
	const std::vector<ChunkSpan> children = TileChunks(buf, start, end, kErrBadMesh);
	bool seenP2vb = false;
	std::vector<uint8_t> out;

	for (const ChunkSpan &child : children)
	{
		if (strcmp(child.tag, "P2VB") != 0)
		{
			AppendChunk(out, child.tag, buf + child.start, child.end - child.start);
			continue;
		}
		if (seenP2vb)
			Fail(kErrBadMesh);
		seenP2vb = true;

		const std::vector<ChunkSpan> kids = TileChunks(buf, child.start, child.end, kErrBadMesh);
		if (kids.empty() || strcmp(kids[0].tag, "MSVB") != 0 || kids[0].end - kids[0].start != 8)
			Fail(kErrBadMesh);
		const uint32_t groupCount = Le32(buf + kids[0].start);
		const uint32_t marker = Le32(buf + kids[0].start + 4);
		if (marker != 96u || groupCount > 12u || kids.size() != 1u + groupCount)
			Fail(kErrBadMesh);
		std::vector<Ps2Group> groups;
		groups.reserve(groupCount);
		for (uint32_t i = 0; i < groupCount; i++)
		{
			if (strcmp(kids[1 + i].tag, "MMPV") != 0)
				Fail(kErrBadMesh);
			groups.push_back(ParsePs2Group(buf, kids[1 + i].start, kids[1 + i].end));
		}
		const std::vector<uint8_t> pmvb = BuildPcPmvb(groups);
		AppendChunk(out, "PMVB", pmvb);
	}

	if (!seenP2vb)
		Fail(kErrBadMesh);
	return out;
}

std::vector<uint8_t> ConvertPs2MeshStream(const uint8_t *stream, size_t len,
										  const std::vector<std::string> &archiveTexNames,
										  const std::atomic<bool> *cancel)
{
	if (len == 0 || len > kMaxMemberBytes)
		Fail(kErrBadMesh);
	const std::vector<ChunkSpan> top = TileChunks(stream, 0, len, kErrBadMesh);
	if (top.empty() || strcmp(top[0].tag, "CMSH") != 0)
		Fail(kErrBadMesh);

	size_t partCount = 0;
	std::vector<uint8_t> out;
	for (const ChunkSpan &c : top)
	{
		CheckCancel(cancel);
		if (strcmp(c.tag, "MESP") == 0)
		{
			if (++partCount > 1024)
				Fail(kErrBadMesh);
			const std::vector<uint8_t> mesp = ConvertPs2Mesp(stream, c.start, c.end);
			AppendChunk(out, "MESP", mesp);
		}
		else if (strcmp(c.tag, "MSHT") == 0)
		{
			const std::vector<ChunkSpan> kids = TileChunks(stream, c.start, c.end, kErrBadMesh);
			if (kids.size() != 1 || strcmp(kids[0].tag, "TEXB") != 0 || kids[0].end - kids[0].start != 24)
				Fail(kErrBadMesh);
			const uint8_t *texb = stream + kids[0].start;
			const uint32_t texIdx = Le32(texb + 20);
			if (texIdx >= archiveTexNames.size())
				Fail(kErrBadMesh);
			const std::string &name = archiveTexNames[texIdx];
			if (name.empty() || name.size() >= 128)
				Fail(kErrBadMesh);
			for (unsigned char ch : name)
				if (ch < 0x20 || ch >= 0x7f)
					Fail(kErrBadMesh);
			std::vector<uint8_t> pcTexb(20 + 128, 0);
			memcpy(pcTexb.data(), texb, 20);
			memcpy(pcTexb.data() + 20, name.data(), name.size());
			std::vector<uint8_t> msht;
			AppendChunk(msht, "TEXB", pcTexb);
			AppendChunk(out, "MSHT", msht);
		}
		else
		{
			AppendChunk(out, c.tag, stream + c.start, c.end - c.start);
		}
	}
	return out;
}

struct Ps2ExtractedMember
{
	std::vector<uint8_t>	worldBody;
	std::vector<uint8_t>	shadows;
	std::vector<uint8_t>	fenrirCmsh;
};

Ps2ExtractedMember ExtractPs2MemberParts(const std::vector<uint8_t> &member, const std::atomic<bool> *cancel)
{
	const std::vector<ChunkSpan> top = TileChunks(member.data(), 0, member.size(), kErrBadMember);
	std::vector<std::string> texNames;
	std::vector<uint8_t> worldBody;
	std::vector<uint8_t> shadows;
	const uint8_t *fenrirStream = nullptr;
	size_t fenrirStreamLen = 0;
	int wresCount = 0;
	int sshdCount = 0;

	for (const ChunkSpan &c : top)
	{
		CheckCancel(cancel);
		const uint8_t *payload = member.data() + c.start;
		const size_t size = c.end - c.start;

		if (strcmp(c.tag, "TEXT") == 0)
		{
			if (size < 17)
				Fail(kErrBadMember);
			const uint32_t innerSize = Le32(payload + 4);
			if (innerSize > size - 8 || innerSize < 9)
				Fail(kErrBadMember);
			const uint8_t *inner = payload + 8;
			const size_t maxField = std::min<size_t>(160, innerSize - 8);
			const void *nul = memchr(inner + 8, 0, maxField);
			const size_t nlen = nul ? (size_t)((const uint8_t *)nul - (inner + 8)) : maxField;
			texNames.emplace_back((const char *)(inner + 8), nlen);
		}
		else if (strcmp(c.tag, "MESH") == 0 && !fenrirStream)
		{
			if (size >= 8 + 304 + 8 && memcmp(payload, "PMSH", 4) == 0)
			{
				const uint32_t pmshSize = Le32(payload + 4);
				if (pmshSize <= size - 8 && pmshSize > 304 + 8)
				{
					const uint8_t *pmsh = payload + 8;
					const void *nul = memchr(pmsh, 0, 304);
					if (nul)
					{
						const std::string mname((const char *)pmsh, (size_t)((const uint8_t *)nul - pmsh));
						if (AsciiLower(mname) == kFenrirMeshName)
						{
							if (memcmp(pmsh + 304, "CMSH", 4) != 0)
								Fail(kErrBadMesh);
							fenrirStream = pmsh + 304;
							fenrirStreamLen = pmshSize - 304;
						}
					}
				}
			}
		}
		else if (strcmp(c.tag, "SSHD") == 0)
		{
			sshdCount++;
			shadows.assign(payload, payload + size);
		}
		else if (strcmp(c.tag, "WRES") == 0)
		{
			wresCount++;
			const std::vector<ChunkSpan> wrlds = TileChunks(member.data(), c.start, c.end, kErrBadWorld);
			bool gotWrld = false;
			for (const ChunkSpan &wc : wrlds)
			{
				if (strcmp(wc.tag, "WRLD") != 0)
					continue;
				gotWrld = true;
				const std::vector<ChunkSpan> kids = TileChunks(member.data(), wc.start, wc.end, kErrBadWorld);
				bool gotWdat = false;
				int bodyCount = 0;
				for (const ChunkSpan &kc : kids)
				{
					const size_t ksz = kc.end - kc.start;
					if (strcmp(kc.tag, "WDAT") == 0)
					{
						if (ksz < 8)
							Fail(kErrBadWorld);
						const int32_t baseLvl = LeI32(member.data() + kc.start);
						const int32_t realLvl = LeI32(member.data() + kc.start + 4);
						if (baseLvl != -1 || realLvl != kTargetLevel)
							Fail(kErrBadWorld);
						gotWdat = true;
					}
					else if (strcmp(kc.tag, "BSWD") == 0)
					{
						Fail(kErrBadWorld);
					}
					else if (strcmp(kc.tag, "RLWD") == 0)
					{
						bodyCount++;
						worldBody.assign(member.data() + kc.start, member.data() + kc.end);
					}
				}
				if (!gotWdat || bodyCount != 1)
					Fail(kErrBadWorld);
				break;
			}
			if (!gotWrld)
				Fail(kErrBadWorld);
		}
	}

	if (wresCount != 1 || worldBody.empty() || sshdCount != 1 || shadows.empty())
		Fail(kErrBadWorld);
	if (!fenrirStream)
		Fail(kErrBadMesh);

	Ps2ExtractedMember res;
	res.worldBody = std::move(worldBody);
	res.shadows = std::move(shadows);
	res.fenrirCmsh = ConvertPs2MeshStream(fenrirStream, fenrirStreamLen, texNames, cancel);
	return res;
}

// ---- binary decoders: SSHD, worldheaders.dat, RLWD world body, default physics.dat ----------------------------------

struct ByteReader
{
	const uint8_t	*data;
	size_t			pos;
	size_t			end;
	const char		*err;

	const uint8_t *Take(size_t n)
	{
		if (n > end - pos)
			Fail(err);
		const uint8_t *p = data + pos;
		pos += n;
		return p;
	}

	uint8_t U8() { return *Take(1); }
	int8_t I8() { return (int8_t)*Take(1); }
	uint16_t U16() { return Le16(Take(2)); }
	int16_t I16() { return (int16_t)Le16(Take(2)); }
	uint32_t U32() { return Le32(Take(4)); }
	int32_t I32() { return LeI32(Take(4)); }

	float F32()
	{
		float f = 0.f;
		memcpy(&f, Take(4), 4);
		return f;
	}

	int32_t CountI32(int32_t limit = 1 << 20)
	{
		const int32_t v = I32();
		if (v < 0 || v > limit)
			Fail(err);
		return v;
	}

	int32_t CountU16()
	{
		return (int32_t)U16();
	}

	int32_t CountI16()
	{
		const int16_t v = I16();
		if (v < 0)
			Fail(err);
		return v;
	}

	std::string CStr()
	{
		const size_t rem = end - pos;
		const void *nul = memchr(data + pos, 0, std::min<size_t>(rem, 256));
		if (!nul)
			Fail(err);
		const size_t len = (size_t)((const uint8_t *)nul - (data + pos));
		std::string s((const char *)(data + pos), len);
		pos += len + 1;
		return s;
	}

	std::string LStrI8()
	{
		const int8_t len = I8();
		if (len < 0)
			Fail(err);
		const uint8_t *p = Take((size_t)len);
		return std::string((const char *)p, (size_t)len);
	}

	std::string LStrU8()
	{
		const uint8_t len = U8();
		const uint8_t *p = Take((size_t)len);
		return std::string((const char *)p, (size_t)len);
	}

	std::string LStrI32()
	{
		const int32_t len = CountI32(1 << 20);
		const uint8_t *p = Take((size_t)len);
		return std::string((const char *)p, (size_t)len);
	}

	void ChunkHeader()
	{
		Take(8);
	}
};

void ValidateSshd(const std::vector<uint8_t> &payload)
{
	ByteReader r{payload.data(), 0, payload.size(), kErrBadWorld};
	r.ChunkHeader();
	r.ChunkHeader();
	const int32_t count = r.CountI32();
	r.ChunkHeader();
	for (int32_t i = 0; i < count; i++)
	{
		r.ChunkHeader();
		r.Take(16);	// x, y, w, h
		const int32_t numMaps = r.CountI32();
		r.I32();	// thing
		for (int32_t m = 0; m < numMaps; m++)
		{
			r.ChunkHeader();
			r.I32();	// mx
			r.I32();	// my
			const int32_t mw = r.CountI32(16384);
			const int32_t mh = r.CountI32(16384);
			const int32_t isShadowed = r.I32();
			if (isShadowed)
			{
				r.ChunkHeader();
				const int64_t cells = int64_t(mw) * int64_t(mh);
				if (cells < 0 || cells > (1 << 20))
					Fail(kErrBadWorld);
				for (int64_t c = 0; c < cells; c++)
					if (r.I32() == 1)
						r.Take(0x200);
			}
		}
	}
	if (r.pos != r.end)
		Fail(kErrBadWorld);
}

struct WorldHeaderRecord
{
	int32_t						version = 0;
	int32_t						payloadSize = 0;
	int32_t						worldId = 0;
	std::vector<std::string>	configs;
	int32_t						trailing[4] = {};
};

WorldHeaderRecord ParseWorldHeaderForLevel(const std::vector<uint8_t> &data, int targetWorldId)
{
	ByteReader r{data.data(), 0, data.size(), kErrBadGameFiles};
	const int32_t fileVer = r.I32();
	const int32_t recCount = r.CountI32(65536);
	if (fileVer != 1)
		Fail(kErrBadGameFiles);

	bool found = false;
	WorldHeaderRecord target{};
	for (int32_t i = 0; i < recCount; i++)
	{
		const int32_t ver = r.I32();
		const int32_t psize = r.I32();
		if (ver < 3 || psize < 24)
			Fail(kErrBadGameFiles);
		const size_t payloadStart = r.pos;
		const int32_t wid = r.I32();
		const int32_t cfgCount = r.CountI32(128);
		std::vector<std::string> cfgs;
		cfgs.reserve((size_t)cfgCount);
		for (int32_t s = 0; s < cfgCount; s++)
		{
			const uint8_t slen = r.U8();
			const uint8_t *sp = r.Take(slen);
			for (uint8_t b = 0; b < slen; b++)
				if (sp[b] < 0x20 || sp[b] >= 0x7f)
					Fail(kErrBadGameFiles);
			cfgs.emplace_back((const char *)sp, slen);
		}
		int32_t tr[4];
		for (int t = 0; t < 4; t++)
			tr[t] = r.I32();
		if ((int32_t)(r.pos - payloadStart) != psize)
			Fail(kErrBadGameFiles);
		if (wid == targetWorldId && !found)
		{
			found = true;
			target.version = ver;
			target.payloadSize = psize;
			target.worldId = wid;
			target.configs = std::move(cfgs);
			for (int t = 0; t < 4; t++)
				target.trailing[t] = tr[t];
		}
	}
	if (r.pos != r.end || !found)
		Fail(kErrBadGameFiles);
	return target;
}

void ReadWorldScriptCode(ByteReader &r)
{
	const int32_t instrCount = r.CountI32();
	for (int32_t i = 0; i < instrCount; i++)
	{
		const int32_t op = r.I32();
		r.I32();
		if (op < 0 || op >= 27)
			Fail(r.err);
	}
	r.Take(13 * 4);	// eventPCs
	const int32_t symCount = r.CountI32();
	for (int32_t i = 0; i < symCount; i++)
	{
		(void)r.LStrI32();
		const int32_t dtype = r.I32();
		if (dtype == 1 || dtype == 4 || dtype == 5)
			r.I32();
		else if (dtype == 2)
			r.Take(4);
		else if (dtype == 3)
			(void)r.LStrI32();
		else if (dtype == 6)
			r.Take(12);
		else if (dtype != 0)
			Fail(r.err);
		r.Take(12);	// symbolType, index, flag
	}
	r.I32();	// nextSymbolIndex
	const int32_t fnCount = r.CountI32();
	for (int32_t i = 0; i < fnCount; i++)
	{
		r.I32();	// entryPC
		const int32_t paramCount = r.CountI32();
		r.Take((size_t)paramCount * 4);
	}
	r.Take(8);	// tail
}

bool IsDefinedThingOid(int32_t oid)
{
	return oid == 8 || oid == 25 || oid == 28 || oid == 35 || oid == 39;
}

bool IsSpawnableThingOid(int32_t oid)
{
	switch (oid)
	{
	case 3: case 7: case 8: case 10: case 11: case 15: case 16: case 17:
	case 18: case 19: case 21: case 25: case 26: case 27: case 28: case 29:
	case 31: case 33: case 35: case 36: case 37: case 38: case 39: case 41: case 43:
		return true;
	default:
		return false;
	}
}

void ReadThingInit(ByteReader &r, uint16_t version, int32_t oid)
{
	for (int k = 0; k < 6; k++)
	{
		const float f = r.F32();
		if (!std::isfinite(f))
			Fail(r.err);
	}
	r.Take(12);	// meshNumber, allegiance, target
	(void)r.CStr();	// script
	(void)r.CStr();	// name
	(void)r.CStr();	// spawnScript
	r.I32();	// active
	if (version > 45)
		r.I32();	// attachScriptsToUnits

	if (oid == 19)	// spawner
	{
		r.Take(20);	// amount, delay, squadDelay, initialDelay, squadSize
		(void)r.CStr();
		if (version > 43)
			(void)r.CStr();
	}
	else if (oid == 28)	// squad
	{
		r.Take(8);
	}
	else if (oid == 26)	// cutscene
	{
		(void)r.CStr();
		(void)r.CStr();
	}
	else if (oid == 15 || oid == 41)	// start / spawn-point
	{
		r.Take(8);
	}
	else if (oid == 7)	// tree
	{
		(void)r.CStr();
	}
	else if (oid == 33)	// wall
	{
		r.Take(8);
		(void)r.CStr();
	}
	else if (oid == 36)	// sphere-trigger
	{
		r.Take(4);
	}
}

// Validates the PS2 RLWD world body against CWorld::LoadWorld and worldheaders.dat record 888, returning unit definitions.
std::vector<std::string> ValidatePs2WorldBody(const std::vector<uint8_t> &body, const WorldHeaderRecord &rec888)
{
	ByteReader r{body.data(), 0, body.size(), kErrBadWorld};
	const uint16_t version = r.U16();
	if (version < 43 || version > 50)
		Fail(kErrBadWorld);
	const int32_t hversion = r.I32();
	const int32_t unused1 = r.I32();
	const int32_t unused2 = r.I32();
	const int32_t cfgCount = r.CountI32(128);
	std::vector<std::string> cfgs;
	cfgs.reserve((size_t)cfgCount);
	for (int32_t i = 0; i < cfgCount; i++)
		cfgs.push_back(r.LStrI8());

	int32_t v2[3] = {};
	int32_t worldType = 0;
	if (hversion < 3)
		Fail(kErrBadWorld);
	for (int i = 0; i < 3; i++)
		v2[i] = r.I32();
	worldType = r.I32();

	// Header agreement with PC worldheaders.dat record 888 (level888_build.py:disc_world)
	if (unused1 != rec888.payloadSize || unused2 != rec888.worldId ||
		cfgs != rec888.configs ||
		v2[0] != rec888.trailing[0] || v2[1] != rec888.trailing[1] || v2[2] != rec888.trailing[2] ||
		worldType != rec888.trailing[3])
		Fail(kErrBadWorld);

	const int32_t baseLevel = r.I32();
	if (baseLevel != -1)
		Fail(kErrBadWorld);

	const int32_t scriptCount = r.CountI32(1024);
	for (int32_t i = 0; i < scriptCount; i++)
	{
		(void)r.LStrI32();
		ReadWorldScriptCode(r);
		r.Take(10);
	}
	r.I32();	// mapNumber
	const int32_t namedMeshCount = r.CountI32(4096);
	for (int32_t i = 0; i < namedMeshCount; i++)
		(void)r.LStrU8();

	const int32_t thingCount = r.CountU16();
	if (thingCount > 1000)
		Fail(kErrBadWorld);
	std::vector<std::string> unitDefs;
	for (int32_t i = 0; i < thingCount; i++)
	{
		const int32_t oid = r.I32();
		if (!IsSpawnableThingOid(oid))
			Fail(kErrBadWorld);
		ReadThingInit(r, version, oid);
		if (IsDefinedThingOid(oid))
		{
			std::string def = r.LStrI8();
			r.I32();	// diesOn
			if (oid == 8 && std::find(unitDefs.begin(), unitDefs.end(), def) == unitDefs.end())
				unitDefs.push_back(std::move(def));
		}
	}
	if (unitDefs.empty())
		Fail(kErrBadWorld);

	const int32_t treeGroups = r.CountI16();
	for (int32_t i = 0; i < treeGroups; i++)
	{
		r.Take(20);
		(void)r.LStrI8();
		r.I32();
	}
	const int32_t treeTypes = r.CountI32(4096);
	for (int32_t i = 0; i < treeTypes; i++)
	{
		(void)r.LStrI8();
		const int32_t posCount = r.CountI32();
		r.Take((size_t)posCount * 12);
	}

	const uint16_t infVer = r.U16();
	if (infVer != 0 && infVer != 1)
		Fail(kErrBadWorld);
	const int32_t infNodes = r.CountI32();
	r.Take((size_t)infNodes * (infVer == 0 ? 12u : 16u));
	const int32_t infLinks = r.CountI32();
	r.Take((size_t)infLinks * (infVer == 0 ? 8u : 12u));

	const int32_t pathCount = r.CountI16();
	for (int32_t i = 0; i < pathCount; i++)
	{
		(void)r.LStrI8();
		const int32_t slots = r.CountI32(4096);
		r.Take((size_t)slots * 2);
	}

	r.Take(44);	// timings
	if (version >= 45 && version < 48)
		r.Take(20);
	if (version >= 47)
	{
		for (int g = 0; g < 3; g++)
		{
			const int32_t gv = r.I32();
			if (gv == 1)
				r.Take(0x8000);
			else if (gv == 2)
				r.Take(0x2000);
			else if (gv != 0)
				Fail(kErrBadWorld);
		}
		if (version < 50)
			r.Take(20);
	}
	if (version >= 49)
		r.Take(4);

	if (r.pos != r.end)
		Fail(kErrBadWorld);
	return unitDefs;
}

// ---- default physics.dat parser & unit mesh closure ------------------------------------------------------------------

struct PhysNode
{
	uint32_t				field;
	std::vector<uint8_t>	value;
};

struct PhysStmt
{
	uint32_t				kind;
	std::string				name;
	std::vector<PhysNode>	nodes;
};

std::string PhysCStr(const std::vector<uint8_t> &val)
{
	const void *nul = val.empty() ? nullptr : memchr(val.data(), 0, val.size());
	if (!nul || nul == val.data())
		Fail(kErrBadGameFiles);
	const size_t len = (size_t)((const uint8_t *)nul - val.data());
	for (size_t i = 0; i < len; i++)
		if (val[i] < 0x20 || val[i] >= 0x7f)
			Fail(kErrBadGameFiles);
	return std::string((const char *)val.data(), len);
}

class PhysicsDb
{
public:
	explicit PhysicsDb(const std::vector<uint8_t> &data)
	{
		if (data.size() < 6 || Le16(data.data()) != 0x12u)
			Fail(kErrBadGameFiles);
		size_t at = 2;
		size_t count = 0;
		while (true)
		{
			if (data.size() - at < 4)
				Fail(kErrBadGameFiles);
			if (LeI32(data.data() + at) == -1)
			{
				at += 4;
				break;
			}
			if (++count >= 10000 || data.size() - at < 8)
				Fail(kErrBadGameFiles);
			const uint32_t kind = Le32(data.data() + at);
			at += 8;
			const void *nul = memchr(data.data() + at, 0, data.size() - at);
			if (!nul)
				Fail(kErrBadGameFiles);
			const size_t nlen = (size_t)((const uint8_t *)nul - (data.data() + at));
			std::string name((const char *)(data.data() + at), nlen);
			at += nlen + 1;

			std::vector<PhysNode> nodes;
			while (true)
			{
				if (nodes.size() >= 1024 || data.size() - at < 8)
					Fail(kErrBadGameFiles);
				const uint32_t field = Le32(data.data() + at);
				const uint32_t length = Le32(data.data() + at + 4);
				if (length > (1u << 20) || length + 12u > data.size() - at)
					Fail(kErrBadGameFiles);
				PhysNode nd;
				nd.field = field;
				nd.value.assign(data.data() + at + 8, data.data() + at + 8 + length);
				at += 8 + length;
				const int32_t link = LeI32(data.data() + at);
				at += 4;
				nodes.push_back(std::move(nd));
				if (link == -1)
					break;
				if (link != 0)
					Fail(kErrBadGameFiles);
			}
			mBy.emplace(std::make_pair(kind, name), PhysStmt{kind, name, std::move(nodes)});
		}
		if (at != data.size())
			Fail(kErrBadGameFiles);
	}

	void UnitMeshClosure(const std::string &unit, std::vector<std::string> *visitedUnits,
						 std::vector<std::string> *meshes) const
	{
		if (std::find(visitedUnits->begin(), visitedUnits->end(), unit) != visitedUnits->end())
			return;
		visitedUnits->push_back(unit);

		auto addMesh = [&](const std::string &m) {
			if (m.empty())
				return;
			const std::string ml = AsciiLower(m);
			for (const std::string &known : *meshes)
				if (AsciiLower(known) == ml)
					return;
			meshes->push_back(m);
		};

		PhysValues uv = Values(1, unit, {});
		addMesh(uv.mesh);
		std::vector<std::string> spawners = uv.spawners;
		for (const std::string &comp : uv.components)
		{
			PhysValues cv = Values(7, comp, {});
			addMesh(cv.mesh);
			spawners.insert(spawners.end(), cv.spawners.begin(), cv.spawners.end());
		}
		for (const std::string &sp : spawners)
		{
			PhysValues sv = Values(5, sp, {});
			for (const std::string &made : sv.units)
				UnitMeshClosure(made, visitedUnits, meshes);
		}
	}

private:
	struct PhysValues
	{
		std::string					mesh;
		std::vector<std::string>	components;
		std::vector<std::string>	spawners;
		std::vector<std::string>	units;
	};

	PhysValues Values(uint32_t kind, const std::string &name,
					  std::vector<std::pair<uint32_t, std::string>> trail) const
	{
		const auto key = std::make_pair(kind, name);
		if (std::find(trail.begin(), trail.end(), key) != trail.end())
			Fail(kErrBadGameFiles);
		const auto it = mBy.find(key);
		if (it == mBy.end())
			Fail(kErrBadGameFiles);
		trail.push_back(key);

		const uint32_t basedOnField = (kind == 1) ? 39u : 10u;
		const uint32_t meshField = (kind == 1) ? 9u : (kind == 7 ? 3u : 0xffffffffu);
		const uint32_t compField = (kind == 1) ? 27u : 0xffffffffu;
		const uint32_t spawnerField = (kind == 1) ? 18u : (kind == 7 ? 19u : 0xffffffffu);
		const uint32_t unitField = (kind == 5) ? 1u : 0xffffffffu;

		PhysValues out;
		for (const PhysNode &node : it->second.nodes)
		{
			if (node.field == basedOnField)
			{
				PhysValues base = Values(kind, PhysCStr(node.value), trail);
				if (!base.mesh.empty())
					out.mesh = base.mesh;
				out.components.insert(out.components.end(), base.components.begin(), base.components.end());
				out.spawners.insert(out.spawners.end(), base.spawners.begin(), base.spawners.end());
				out.units.insert(out.units.end(), base.units.begin(), base.units.end());
			}
			else if (node.field == meshField)
				out.mesh = PhysCStr(node.value);
			else if (node.field == compField)
				out.components.push_back(PhysCStr(node.value));
			else if (node.field == spawnerField)
				out.spawners.push_back(PhysCStr(node.value));
			else if (node.field == unitField)
				out.units.push_back(PhysCStr(node.value));
		}
		return out;
	}

	std::map<std::pair<uint32_t, std::string>, PhysStmt>	mBy;
};

// ---- PC game folder read-only access, .aya unpack/pack, and descriptor merge -----------------------------------------

class PcInstall
{
public:
	explicit PcInstall(const char *game_dir) : mRoot(game_dir ? game_dir : "")
	{
		if (mRoot.empty())
			Fail(kErrBadGameFiles);
	}

	bool ExistsAny(const std::string &rel) const
	{
#ifdef _WIN32
		std::string full = mRoot;
		if (!full.empty() && full.back() != '\\' && full.back() != '/')
			full.push_back('\\');
		for (char c : rel)
			full.push_back(c == '/' ? '\\' : c);
		std::vector<wchar_t> wfull;
		if (!Utf8ToWide(full, &wfull))
			return false;
		return GetFileAttributesW(wfull.data()) != INVALID_FILE_ATTRIBUTES;
#else
		std::string resolved;
		return ResolvePosix(rel, &resolved, true);
#endif
	}

	std::vector<uint8_t> ReadFile(const std::string &rel, size_t maxBytes) const
	{
		std::string path;
#ifdef _WIN32
		if (!ResolveWin(rel, &path))
			Fail(kErrBadGameFiles);
#else
		if (!ResolvePosix(rel, &path, false))
			Fail(kErrBadGameFiles);
#endif
		RawFile f;
		uint64_t sz = 0;
		if (!f.OpenRegular(path.c_str(), &sz) || sz > maxBytes)
			Fail(kErrBadGameFiles);
		std::vector<uint8_t> buf((size_t)sz);
		if (sz && !f.ReadAt(0, buf.data(), buf.size()))
			Fail(kErrBadGameFiles);
		return buf;
	}

private:
#ifdef _WIN32
	bool ResolveWin(const std::string &rel, std::string *out) const
	{
		// Walk component by component, refusing reparse points on every parent directory.
		std::string cur = mRoot;
		while (cur.size() > 3 && (cur.back() == '\\' || cur.back() == '/'))
			cur.pop_back();
		std::vector<wchar_t> wcur;
		if (!Utf8ToWide(cur, &wcur))
			return false;
		const DWORD rootAttr = GetFileAttributesW(wcur.data());
		if (rootAttr == INVALID_FILE_ATTRIBUTES || !(rootAttr & FILE_ATTRIBUTE_DIRECTORY) ||
			(rootAttr & FILE_ATTRIBUTE_REPARSE_POINT))
			return false;

		size_t start = 0;
		while (start < rel.size())
		{
			const size_t slash = rel.find('/', start);
			const std::string part = rel.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
			if (part.empty() || part == "." || part == "..")
				return false;
			cur += "\\" + part;
			if (!Utf8ToWide(cur, &wcur))
				return false;
			const DWORD attr = GetFileAttributesW(wcur.data());
			if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT))
				return false;
			if (slash != std::string::npos && !(attr & FILE_ATTRIBUTE_DIRECTORY))
				return false;
			start = (slash == std::string::npos) ? rel.size() : slash + 1;
		}
		*out = std::move(cur);
		return true;
	}
#else
	bool ResolvePosix(const std::string &rel, std::string *out, bool allowAnyLeafKind) const
	{
		std::string cur = NormalizeLexicalPosix(mRoot);
		struct stat st{};
		if (lstat(cur.c_str(), &st) != 0 || S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode))
			return false;

		size_t start = 0;
		while (start < rel.size())
		{
			const size_t slash = rel.find('/', start);
			const bool isLast = (slash == std::string::npos);
			const std::string want = rel.substr(start, isLast ? std::string::npos : slash - start);
			if (want.empty() || want == "." || want == "..")
				return false;

			DIR *dir = opendir(cur.c_str());
			if (!dir)
				return false;
			std::vector<std::string> matches;
			const std::string wantLower = AsciiLower(want);
			while (struct dirent *ent = readdir(dir))
			{
				const std::string ename = ent->d_name;
				if (AsciiLower(ename) == wantLower)
					matches.push_back(ename);
			}
			closedir(dir);
			if (matches.empty())
				return false;
			std::sort(matches.begin(), matches.end());
			const std::string nextPath = cur + (cur == "/" ? "" : "/") + matches[0];
			if (lstat(nextPath.c_str(), &st) != 0)
				return false;
			if (isLast && allowAnyLeafKind)
			{
				*out = nextPath;
				return true;
			}
			if (S_ISLNK(st.st_mode))
				return false;
			if (!isLast && !S_ISDIR(st.st_mode))
				return false;
			if (isLast && !S_ISREG(st.st_mode))
				return false;
			cur = nextPath;
			start = isLast ? rel.size() : slash + 1;
		}
		*out = std::move(cur);
		return true;
	}
#endif

	std::string	mRoot;
};

std::vector<uint8_t> UnpackPcAya(const std::vector<uint8_t> &packed, const std::atomic<bool> *cancel)
{
	if (packed.empty() || packed.size() > kLimitArchive)
		Fail(kErrBadGameFiles);
	std::vector<uint8_t> blockOut(kPackBuffer);
	std::vector<uint8_t> raw;
	size_t pos = 0;
	bool shortSeen = false;
	int blocks = 0;

	while (pos < packed.size())
	{
		CheckCancel(cancel);
		if (packed.size() - pos < 4)
			Fail(kErrBadGameFiles);
		const uint32_t csize = Le32(packed.data() + pos);
		pos += 4;
		if (csize == 0 || csize > kPackBuffer || csize > packed.size() - pos || shortSeen)
			Fail(kErrBadGameFiles);
		unsigned long destLen = kPackBuffer;
		unsigned long srcLen = csize;
		if (uncompress2(blockOut.data(), &destLen, packed.data() + pos, &srcLen) != Z_OK || srcLen != csize)
			Fail(kErrBadGameFiles);
		if (destLen > kPackBlock)
			Fail(kErrBadGameFiles);
		if (destLen < kPackBlock)
			shortSeen = true;
		if (destLen > kLimitArchive - raw.size())
			Fail(kErrBadGameFiles);
		raw.insert(raw.end(), blockOut.data(), blockOut.data() + destLen);
		pos += csize;
		blocks++;
	}
	if (blocks == 0)
		Fail(kErrBadGameFiles);
	return raw;
}

std::vector<uint8_t> PackPcAya(const std::vector<uint8_t> &raw, const std::atomic<bool> *cancel)
{
	if (raw.empty())
		Fail(kErrVerifyFailed);
	std::vector<uint8_t> out;
	std::vector<uint8_t> compBuf(compressBound((uLong)kPackBlock));
	for (size_t start = 0; start < raw.size(); start += kPackBlock)
	{
		CheckCancel(cancel);
		const size_t chunk = std::min(kPackBlock, raw.size() - start);
		uLongf compLen = (uLongf)compBuf.size();
		if (compress2(compBuf.data(), &compLen, raw.data() + start, (uLong)chunk, 9) != Z_OK ||
			compLen == 0 || compLen > kPackBuffer)
			Fail(kErrVerifyFailed);
		AppendLe32(out, (uint32_t)compLen);
		out.insert(out.end(), compBuf.data(), compBuf.data() + compLen);
	}
	return out;
}

std::vector<std::string> ReadCmshTextureNames(const std::vector<uint8_t> &stream, const char *err)
{
	if (stream.size() < 388 || memcmp(stream.data(), "CMSH", 4) != 0 || Le32(stream.data() + 4) != 372 ||
		memcmp(stream.data() + 380, "CMST", 4) != 0)
		Fail(err);
	const uint32_t count = Le32(stream.data() + 12);
	if (count > 1024 || Le32(stream.data() + 384) != 36u * count || 388u + 36u * count > stream.size())
		Fail(err);

	std::vector<uint32_t> frames(count, 1);
	for (uint32_t i = 0; i < count; i++)
	{
		frames[i] = Le32(stream.data() + 388 + 36u * i + 8);
		if (frames[i] == 0 || frames[i] > 16)
			Fail(err);
	}

	size_t at = 388 + 36u * count;
	std::vector<std::string> names;
	names.reserve(count);
	for (uint32_t i = 0; i < count; i++)
	{
		if (stream.size() - at < 16 || memcmp(stream.data() + at, "MSHT", 4) != 0 ||
			memcmp(stream.data() + at + 8, "TEXB", 4) != 0)
			Fail(err);
		const uint32_t mshtSz = Le32(stream.data() + at + 4);
		const uint32_t texbSz = Le32(stream.data() + at + 12);
		const size_t prefixBytes = 20u * frames[i];
		if (mshtSz != texbSz + 8 || texbSz != prefixBytes + 128u || mshtSz > stream.size() - at - 8)
			Fail(err);
		const uint8_t *nameField = stream.data() + at + 16 + prefixBytes;
		const void *nul = memchr(nameField, 0, 128);
		if (!nul || nul == nameField)
			Fail(err);
		const size_t nlen = (size_t)((const uint8_t *)nul - nameField);
		for (size_t k = 0; k < nlen; k++)
			if (nameField[k] < 0x20 || nameField[k] >= 0x7f)
				Fail(err);
		names.emplace_back((const char *)nameField, nlen);
		at += 8 + mshtSz;
	}
	return names;
}

struct ArchiveChunk
{
	std::string				tag;
	std::vector<uint8_t>	payload;
};

std::vector<ArchiveChunk> SplitArchiveChunks(const std::vector<uint8_t> &raw)
{
	const std::vector<ChunkSpan> spans = TileChunks(raw.data(), 0, raw.size(), kErrBadGameFiles);
	std::vector<ArchiveChunk> out;
	out.reserve(spans.size());
	for (const ChunkSpan &s : spans)
		out.push_back({s.tag, std::vector<uint8_t>(raw.data() + s.start, raw.data() + s.end)});
	return out;
}

std::string ReadAsciiCStringAt(const std::vector<uint8_t> &payload, size_t offset)
{
	if (offset >= payload.size())
		Fail(kErrBadGameFiles);
	const size_t rem = std::min<size_t>(payload.size() - offset, 301);
	const void *nul = memchr(payload.data() + offset, 0, rem);
	if (!nul)
		Fail(kErrBadGameFiles);
	const size_t len = (size_t)((const uint8_t *)nul - (payload.data() + offset));
	for (size_t i = 0; i < len; i++)
	{
		const uint8_t ch = payload[offset + i];
		if (ch < 0x20 || ch >= 0x7f)
			Fail(kErrBadGameFiles);
	}
	return std::string((const char *)(payload.data() + offset), len);
}

std::string DescriptorLogicalKey(const std::string &tag, const std::vector<uint8_t> &payload)
{
	if (tag == "TEXT")
	{
		if (payload.size() < 25 || memcmp(payload.data(), "DXTX", 4) != 0 ||
			Le32(payload.data() + 4) != payload.size() - 8 || memcmp(payload.data() + 8, "CTEX", 4) != 0)
			Fail(kErrBadGameFiles);
		return NormalizeAssetName(ReadAsciiCStringAt(payload, 24));
	}
	if (tag == "MESH")
	{
		if (payload.size() < 17 || memcmp(payload.data(), "PMSH", 4) != 0 ||
			Le32(payload.data() + 4) != payload.size() - 8 || memcmp(payload.data() + 8, "PMS2", 4) != 0 ||
			Le32(payload.data() + 12) != payload.size() - 16)
			Fail(kErrBadGameFiles);
		const std::string name = ReadAsciiCStringAt(payload, 16);
		return name.empty() ? "<empty-name>" : NormalizeAssetName(name);
	}
	return tag;
}

int32_t MeshDescriptorId(const std::vector<uint8_t> &payload)
{
	if (payload.size() != 325 || memcmp(payload.data() + 8, "PMS2", 4) != 0)
		Fail(kErrBadGameFiles);
	return LeI32(payload.data() + 316);
}

std::vector<uint8_t> WithMeshDescriptorId(const std::vector<uint8_t> &payload, int32_t newId)
{
	(void)MeshDescriptorId(payload);
	std::vector<uint8_t> out = payload;
	PutLe32(out.data() + 316, (uint32_t)newId);
	return out;
}

std::vector<uint8_t> MakeInlineMeshPayload(const std::vector<uint8_t> &cmshStream, uint32_t runtimeId)
{
	std::vector<uint8_t> body(309, 0);
	PutLe32(body.data() + 300, runtimeId);
	PutLe32(body.data() + 304, 0);
	body[308] = 1;	// kept = true
	body.insert(body.end(), cmshStream.begin(), cmshStream.end());

	std::vector<uint8_t> pms2;
	AppendChunk(pms2, "PMS2", body);
	std::vector<uint8_t> pmsh;
	AppendChunk(pmsh, "PMSH", pms2);
	return pmsh;
}

std::vector<ArchiveChunk> MergeDescriptors(const std::vector<ArchiveChunk> &donor,
										   const std::vector<std::vector<ArchiveChunk>> &sources,
										   const std::vector<std::string> &wantedMeshes,
										   const std::vector<std::string> &wantedTextures)
{
	using Key = std::pair<std::string, std::string>;
	std::map<Key, std::vector<uint8_t>> have;
	for (const ArchiveChunk &c : donor)
		if (c.tag == "TEXT" || c.tag == "MESH")
			have.emplace(std::make_pair(c.tag, DescriptorLogicalKey(c.tag, c.payload)), c.payload);

	std::set<int32_t> donorMeshIds;
	int32_t maxMeshId = -1;
	for (const auto &kv : have)
	{
		if (kv.first.first == "MESH")
		{
			const int32_t id = MeshDescriptorId(kv.second);
			if (!donorMeshIds.insert(id).second)
				Fail(kErrBadGameFiles);
			if (id > maxMeshId)
				maxMeshId = id;
		}
	}
	int32_t nextMeshId = maxMeshId + 1;

	std::vector<std::map<Key, std::vector<uint8_t>>> sourceIndexes(sources.size());
	for (size_t s = 0; s < sources.size(); s++)
		for (const ArchiveChunk &c : sources[s])
			if (c.tag == "TEXT" || c.tag == "MESH")
				sourceIndexes[s].emplace(std::make_pair(c.tag, DescriptorLogicalKey(c.tag, c.payload)), c.payload);

	std::set<std::string> texKeys;
	for (const std::string &t : wantedTextures)
		texKeys.insert(NormalizeAssetName(t));

	std::set<std::string> meshKeys;
	for (const std::string &m : wantedMeshes)
		meshKeys.insert(NormalizeAssetName(m));

	std::vector<std::vector<uint8_t>> addedText;
	for (const std::string &key : texKeys)
	{
		if (have.find(std::make_pair("TEXT", key)) != have.end())
			continue;
		bool found = false;
		for (const auto &idx : sourceIndexes)
		{
			const auto it = idx.find(std::make_pair("TEXT", key));
			if (it == idx.end())
				continue;
			const std::vector<uint8_t> &p = it->second;
			if (p.size() < 372)
				Fail(kErrBadGameFiles);
			const uint32_t frames = Le32(p.data() + 328);
			if (frames == 0 || frames > 16 || p.size() != 360u + 12u * frames)
				Fail(kErrBadGameFiles);
			addedText.push_back(p);
			found = true;
			break;
		}
		if (!found)
			Fail(kErrBadGameFiles);
	}

	std::vector<std::vector<uint8_t>> addedMesh;
	for (const std::string &key : meshKeys)
	{
		if (have.find(std::make_pair("MESH", key)) != have.end())
			continue;
		bool found = false;
		for (const auto &idx : sourceIndexes)
		{
			const auto it = idx.find(std::make_pair("MESH", key));
			if (it == idx.end())
				continue;
			const std::vector<uint8_t> &p = it->second;
			if (p.size() != 325 || nextMeshId >= (int32_t)kInlineMeshId)
				Fail(kErrBadGameFiles);
			addedMesh.push_back(WithMeshDescriptorId(p, nextMeshId++));
			found = true;
			break;
		}
		if (!found)
			Fail(kErrBadGameFiles);
	}

	int lastText = -1, lastMesh = -1;
	for (size_t i = 0; i < donor.size(); i++)
	{
		if (donor[i].tag == "TEXT") lastText = (int)i;
		if (donor[i].tag == "MESH") lastMesh = (int)i;
	}
	if (lastText < 0 || lastMesh < 0)
		Fail(kErrBadGameFiles);

	std::vector<ArchiveChunk> out;
	out.reserve(donor.size() + addedText.size() + addedMesh.size() + 2);
	for (size_t i = 0; i < donor.size(); i++)
	{
		out.push_back(donor[i]);
		if ((int)i == lastText)
			for (std::vector<uint8_t> &extra : addedText)
				out.push_back({"TEXT", std::move(extra)});
		if ((int)i == lastMesh)
			for (std::vector<uint8_t> &extra : addedMesh)
				out.push_back({"MESH", std::move(extra)});
	}
	return out;
}

// ---- self-validation matching mods.cpp (CheckPacked + CheckLevelArchive + CheckInlineMesh) ---------------------------

struct VerifyCursor
{
	const uint8_t	*b;
	size_t			at;
	size_t			end;

	bool Take(const char *tag, const uint8_t *&p, size_t &size)
	{
		if (end - at < 8 || memcmp(b + at, tag, 4) != 0)
			return false;
		size = Le32(b + at + 4);
		if (size > end - at - 8)
			return false;
		p = b + at + 8;
		at += 8 + size;
		return true;
	}
	bool Done() const { return at == end; }
};

bool VerifyMeshBox(VerifyCursor &m)
{
	const uint8_t *p = nullptr, *q = nullptr;
	size_t sz = 0, inner = 0;
	if (!m.Take("BBOX", p, sz) || sz != 48)
		return false;
	VerifyCursor w{p, 0, sz};
	return w.Take("BBOX", q, inner) && inner == 40 && w.Done();
}

bool VerifyCollisionGrid(const uint8_t *grid, size_t gridSize)
{
	VerifyCursor g{grid, 0, gridSize};
	const uint8_t *p = nullptr;
	size_t size = 0;
	if (!g.Take("CPBT", p, size) || size != 184)
		return false;
	const uint32_t width = Le32(p + 0x68), height = Le32(p + 0x6c);
	if (!width || !height || width > 16384 || height > 16384 || uint64_t(width) * height > 16384)
		return false;
	if (!g.Take("SIZS", p, size) || size != 8)
		return false;
	const uint32_t lists = Le32(p), indices = Le32(p + 4);
	if (lists > width * height || indices > (1u << 20))
		return false;
	uint32_t seenLists = 0, seenIndices = 0, listed = 0;
	std::vector<uint8_t> used(32767, 0);
	for (uint32_t cell = 0; cell < width * height; cell++)
	{
		if (!g.Take("PBCS", p, size) || size < 4)
			return false;
		const uint32_t present = Le32(p);
		if (present > 1)
			return false;
		if (!present)
		{
			if (size != 4)
				return false;
			continue;
		}
		VerifyCursor cc{p + 4, 0, size - 4};
		const uint8_t *e = nullptr, *s = nullptr;
		size_t esz = 0, ssz = 0;
		if (!cc.Take("PBET", e, esz) || !cc.Done())
			return false;
		VerifyCursor lc{e, 0, esz};
		if (!lc.Take("PBSQ", s, ssz) || !lc.Done() || ssz < 4)
			return false;
		const uint32_t cnt = Le32(s);
		if (cnt > (1u << 20) || ssz != 4 + 2 * (size_t)cnt)
			return false;
		for (uint32_t i = 0; i < cnt; i++)
		{
			const uint32_t tri = Le16(s + 4 + 2 * i);
			if (tri >= 32767)
				return false;
			used[tri] = 1;
			listed = std::max(listed, tri + 1);
		}
		if (++seenLists > lists || (seenIndices += cnt) > indices)
			return false;
	}
	if (!g.Take("PLQT", p, size) || size != 4)
		return false;
	const uint32_t triangles = Le32(p);
	if (triangles > 32767 || listed > triangles || !g.Take("PBFK", p, size))
		return false;
	VerifyCursor tc{p, 0, size};
	uint32_t maxVert = 0;
	for (uint32_t i = 0; i < triangles; i++)
	{
		const uint8_t *k = nullptr;
		size_t ksz = 0;
		if (!tc.Take("PBKE", k, ksz) || ksz != 12)
			return false;
		for (int v = 0; v < 3 && used[i]; v++)
			maxVert = std::max<uint32_t>(maxVert, uint32_t(Le16(k + 4 + 2 * v)) + 1u);
	}
	if (!tc.Done() || !g.Take("PBPN", p, size) || size != 4)
		return false;
	const uint32_t verts = Le32(p);
	if (verts > 32767 || maxVert > verts)
		return false;
	for (uint32_t i = 0; i < verts; i++)
		if (!g.Take("PBPP", p, size) || size != 6)
			return false;
	return g.Done();
}

struct VerifyPartLink
{
	uint16_t listedBy = 0;
	uint8_t depth = 0;
};

bool VerifyMeshPart(const uint8_t *part, size_t partSize, uint32_t index, uint32_t parts, uint32_t textures,
					uint32_t &vertices, uint32_t &indices, uint32_t &groups, uint8_t &typeOut, uint16_t &targetOut,
					VerifyPartLink *links, uint32_t &frames)
{
	VerifyCursor m{part, 0, partSize};
	const uint8_t *p = nullptr;
	size_t size = 0;
	if (!m.Take("CMSP", p, size) || size != 316)
		return false;
	const uint32_t partType = Le32(p + 0x8c), children = Le32(p + 0x90), vframes = Le32(p + 0xb8),
				   hframes = Le32(p + 0xbc), bones = Le32(p + 0xc0), cached = Le32(p + 0x118);
	if (partType < 1 || partType > 6 || Le32(p + 0x88) != index)
		return false;
	typeOut = (uint8_t)partType;
	targetOut = 0;
	const bool skeletal = partType == 3, camera = partType == 4, fov = Le32(p + 0xcc) != 0;
	if (Le32(p + 0x9c) || Le32(p + 0xa0) || Le32(p + 0xa4) || Le32(p + 0xa8) ||
		Le32(p + 0xac) || Le32(p + 0xb0) || Le32(p + 0xd4) || Le32(p + 0xd8))
		return false;
	if ((skeletal && (!bones || bones > 19)) || (!skeletal && (bones || Le32(p + 0xd0))) || fov != camera)
		return false;
	if (Le32(p + 0xb4) > 2 || !vframes || !hframes || children > parts ||
		vframes > 65535 || hframes > 65535 || cached > 65535)
		return false;
	frames = std::min(frames, vframes);
	const bool parent = Le32(p + 0x98) != 0, grid = Le32(p + 0x100) != 0, cachedPos = Le32(p + 0x104) != 0,
			   cachedOri = Le32(p + 0x108) != 0, ori = Le32(p + 0x10c) != 0, pos = Le32(p + 0xc8) != 0,
			   reference = Le32(p + 0x124) != 0, posStatic = Le32(p + 0x11c) != 0, oriStatic = Le32(p + 0x120) != 0;
	if (!ori || !pos || reference != (partType == 6))
		return false;
	if (children)
	{
		if (!m.Take("CHLD", p, size) || size != 4 * (size_t)children)
			return false;
		for (uint32_t i = 0; i < children; i++)
		{
			const uint32_t ch = Le32(p + 4 * i);
			if (ch <= index || ch >= parts || links[ch].listedBy || links[index].depth >= 32)
				return false;
			links[ch] = {(uint16_t)(index + 1), (uint8_t)(links[index].depth + 1)};
		}
	}
	if (parent && (!m.Take("PRNT", p, size) || size != 4 || Le32(p) >= index))
		return false;
	if ((parent ? Le32(p) + 1 : 0) != links[index].listedBy)
		return false;
	if (!VerifyMeshBox(m) || !m.Take("VHFM", p, size) || size != vframes)
		return false;
	for (uint32_t i = 0; i < vframes; i++)
		if (p[i] >= hframes)
			return false;
	if (!m.Take("HORI", p, size) || size != 48 * (size_t)hframes ||
		!m.Take("HPOS", p, size) || size != 16 * (size_t)hframes)
		return false;
	if (fov && (!m.Take("HFOV", p, size) || size != 4 * (size_t)hframes))
		return false;
	uint32_t offsets[19] = {};
	if (bones)
	{
		if (!m.Take("BONE", p, size) || size != 4 * (size_t)bones)
			return false;
		for (uint32_t i = 0; i < bones; i++)
		{
			if (Le32(p + 4 * i) >= parts)
				return false;
			const float f = 3.0f * i;
			memcpy(&offsets[i], &f, 4);
		}
	}
	if (grid && (!m.Take("PBKT", p, size) || !VerifyCollisionGrid(p, size)))
		return false;
	if ((cached != 1 && cached != vframes) || cachedPos == posStatic || cachedOri == oriStatic ||
		((posStatic || oriStatic) && !parent))
		return false;
	if (cachedPos && (!m.Take("CPOS", p, size) || size != 16 * (size_t)cached))
		return false;
	if (cachedOri && (!m.Take("CORI", p, size) || size != 48 * (size_t)cached))
		return false;
	if (reference)
	{
		if (!m.Take("REFR", p, size) || size != 4 || Le32(p) >= parts)
			return false;
		targetOut = (uint16_t)Le32(p);
	}
	if (!m.Take("PMVB", p, size))
		return false;
	VerifyCursor v{p, 0, size};
	if (!v.Take("CMVB", p, size) || size != 296)
		return false;
	const uint32_t pieces = Le32(p + 0x108);
	if (pieces > 12 || (skeletal ? !pieces : camera && pieces) || Le32(p + 0x110))
		return false;
	for (uint32_t slot = pieces; slot < 64; slot++)
		if (Le32(p + 8 + 4 * slot))
			return false;
	const uint32_t stride = skeletal ? 48u : 36u, fmt = skeletal ? 0u : 0x152u;
	if (pieces && (Le32(p + 0x114) != stride || Le32(p + 0x118) != fmt || Le32(p + 0x11c) != 4))
		return false;
	uint32_t vCount = 0, vBytes = 0;
	for (uint32_t piece = 0; piece < pieces; piece++)
	{
		if (!v.Take("MMPT", p, size) || size != 24)
			return false;
		const uint32_t vb = Le32(p), ib = Le32(p + 4), ic = Le32(p + 8), vc = Le32(p + 12),
					   prims = Le32(p + 16), built = Le32(p + 20);
		if (built != 1 || ic < 3 || ic > 65535 || ib != 2 * ic || prims != ic - 2)
			return false;
		if (!piece)
		{
			if (!vc || vc > 65535 || vb != stride * vc)
				return false;
			vCount = vc;
			vBytes = vb;
			vertices += vc;
		}
		else if (vb != vBytes || vc != vCount)
			return false;
		indices += ic;
		if (++groups > 1024 || vertices > 100000 || indices > 600000)
			return false;
		if (!v.Take("IBUF", p, size) || size != ib)
			return false;
		for (uint32_t i = 0; i < ic; i++)
			if (Le16(p + 2 * i) >= vCount)
				return false;
		if (!v.Take("VBUF", p, size) || size != (piece ? 0 : vBytes))
			return false;
		if (skeletal && !piece)
			for (size_t at = 12; at < vBytes; at += 48)
				for (size_t slot = 0; slot < 3; slot++)
					if (std::find(offsets, offsets + bones, Le32(p + at + 4 * slot)) == offsets + bones)
						return false;
		if (!v.Take("TEXR", p, size) || size != 24)
			return false;
		for (int slot = 0; slot < 6; slot++)
			if (Le32(p + 4 * slot) != 0xffffffffu && Le32(p + 4 * slot) >= textures)
				return false;
	}
	return v.Done() && m.Done();
}

bool VerifyInlineMesh(const uint8_t *stream, size_t n, uint32_t firstId)
{
	if (n > (16u << 20))
		return false;
	VerifyCursor c{stream, 0, n};
	uint32_t vertices = 0, indices = 0, groups = 0, ids[16] = {}, partFrames = ~0u, modeFrames = 0, firstModes = 0;
	ids[0] = firstId;

	for (uint32_t depth = 1;; depth++)
	{
		const uint8_t *p = nullptr;
		size_t size = 0;
		if (!c.Take("CMSH", p, size) || size != 372)
			return false;
		const uint32_t textures = Le32(p + 4), chain = Le32(p + 8), seq = Le32(p + 0xc),
					   animModes = Le32(p + 0x14), emitters = Le32(p + 0x1c), parts = Le32(p + 0x15c);
		if (!memchr(p + 0x24, 0, 300) || seq || textures > 32 || animModes > 32 || emitters > 512 || !parts || parts > 512)
			return false;
		if (!c.Take("CMST", p, size) || size != 36 * (size_t)textures)
			return false;
		uint32_t frames[32] = {};
		for (uint32_t i = 0; i < textures; i++)
		{
			frames[i] = Le32(p + 36 * i + 8);
			if (!frames[i] || frames[i] > 16)
				return false;
		}
		for (uint32_t i = 0; i < textures; i++)
		{
			const uint8_t *t = nullptr;
			size_t tsz = 0;
			if (!c.Take("MSHT", p, size))
				return false;
			VerifyCursor w{p, 0, size};
			if (!w.Take("TEXB", t, tsz) || !w.Done() || tsz != 20 * (size_t)frames[i] + 128)
				return false;
			const uint8_t *nm = t + 20 * (size_t)frames[i];
			const void *end = memchr(nm, 0, 128);
			if (!end || end == nm)
				return false;
			for (const uint8_t *q = nm; q < (const uint8_t *)end; q++)
				if (*q < 0x20 || *q >= 0x7f)
					return false;
		}
		std::vector<uint8_t> types(parts, 0);
		std::vector<uint16_t> targets(parts, 0);
		std::vector<VerifyPartLink> links(parts);
		for (uint32_t i = 0; i < parts; i++)
			if (!c.Take("MESP", p, size) ||
				!VerifyMeshPart(p, size, i, parts, textures, vertices, indices, groups,
								types[i], targets[i], links.data(), partFrames))
				return false;
		for (uint32_t i = 0; i < parts; i++)
			if (types[i] == 6 && types[targets[i]] != 1)
				return false;
		if (animModes)
		{
			if (!c.Take("CAMD", p, size) || size != 36 * (size_t)animModes)
				return false;
			for (uint32_t i = 0; i < animModes; i++)
			{
				const uint8_t *md = p + 36 * (size_t)i;
				if (!memchr(md, 0, 16))
					return false;
				const int64_t st = LeI32(md + 0x14), last = LeI32(md + 0x18), sc = LeI32(md + 0x1c);
				if (last != st + sc && !(sc > 0 && last == st + sc - 1))
					return false;
				if (Le32(md + 0x20) > 0x3f800000u)
					return false;
				modeFrames = std::max(modeFrames, std::max(Le32(md + 0x14), Le32(md + 0x18)));
			}
		}
		if (!VerifyMeshBox(c))
			return false;
		if (emitters)
		{
			if (!c.Take("CEMT", p, size) || size < 4 || Le32(p) != 336)
				return false;
			size_t eat = 4;
			for (uint32_t i = 0; i < emitters; i++)
			{
				if (size - eat < 336)
					return false;
				const bool indexed = Le32(p + eat + 0x40) != 0;
				eat += 336;
				if (indexed)
				{
					if (size - eat < 4 || Le32(p + eat) >= parts)
						return false;
					eat += 4;
				}
			}
			if (eat != size)
				return false;
		}

		if (depth == 1)
			firstModes = animModes;
		else if (animModes != firstModes)
			return false;

		if (!chain)
			return c.Done() && modeFrames < partFrames;

		if (depth == 16 || n - c.at < 16 + 309 + 8)
			return false;
		const size_t rest = n - c.at;
		const uint8_t *h = stream + c.at, *ref = h + 16;
		if (memcmp(h, "PMSH", 4) != 0 || Le32(h + 4) != rest - 8 ||
			memcmp(h + 8, "PMS2", 4) != 0 || Le32(h + 12) != rest - 16)
			return false;
		if (std::any_of(ref, ref + 300, [](uint8_t x) { return x != 0; }))
			return false;
		const uint32_t cid = Le32(ref + 300);
		if (cid < 20000 || cid > 29999 || Le32(ref + 304) != 0 || ref[308] > 1)
			return false;
		for (uint32_t i = 0; i < depth; i++)
			if (ids[i] == cid)
				return false;
		ids[depth] = cid;
		c.at += 16 + 309;
	}
}

void VerifyBuiltArchive(const std::vector<uint8_t> &packed, const std::vector<uint8_t> &expectedRaw,
						const std::atomic<bool> *cancel)
{
	if (packed.empty() || packed.size() > kMaxAddedPacked)
		Fail(kErrVerifyFailed);
	const std::vector<uint8_t> unpacked = UnpackPcAya(packed, cancel);
	if (unpacked.size() > kMaxAddedUnpacked || unpacked != expectedRaw)
		Fail(kErrVerifyFailed);

	static const char *const kTags[] = {"LVLR", "TARG", "AYAD", "TEXT", "MESH", "IMPS", "LNDS", "SURF", "ERES", "SSHD", "WRES"};
	static const uint32_t kGuards[5] = {344, 372, 316, 5084, 92};

	size_t at = 0, wresAt = 0, wresSize = 0;
	int count = 0, wres = 0, inlineMeshes = 0;
	while (at < unpacked.size())
	{
		if (unpacked.size() - at < 8 || ++count > kMaxAddedChunks)
			Fail(kErrVerifyFailed);
		const uint8_t *h = unpacked.data() + at;
		const size_t size = Le32(h + 4);
		if (size > unpacked.size() - at - 8)
			Fail(kErrVerifyFailed);
		const uint8_t *p = h + 8;
		bool known = false;
		for (const char *tag : kTags)
			known = known || memcmp(h, tag, 4) == 0;
		if (!known)
			Fail(kErrVerifyFailed);
		if (count == 1 && (memcmp(h, "LVLR", 4) != 0 || size != 4 || Le32(p) != 103))
			Fail(kErrVerifyFailed);
		if (count == 2 && (memcmp(h, "TARG", 4) != 0 || size != 4 || Le32(p) != 1))
			Fail(kErrVerifyFailed);
		if (count == 3)
		{
			if (memcmp(h, "AYAD", 4) != 0 || size != 24)
				Fail(kErrVerifyFailed);
			for (int i = 0; i < 5; i++)
				if (Le32(p + 4 * i) != kGuards[i])
					Fail(kErrVerifyFailed);
		}
		if (count > 3 && (memcmp(h, "LVLR", 4) == 0 || memcmp(h, "TARG", 4) == 0 || memcmp(h, "AYAD", 4) == 0))
			Fail(kErrVerifyFailed);
		if (memcmp(h, "TEXT", 4) == 0)
		{
			if (size < 372 || memcmp(p, "DXTX", 4) != 0 || Le32(p + 4) != size - 8 || memcmp(p + 8, "CTEX", 4) != 0)
				Fail(kErrVerifyFailed);
			const uint32_t frames = Le32(p + 328);
			if (frames == 0 || frames > 16 || size != 360 + 12 * frames)
				Fail(kErrVerifyFailed);
		}
		if (memcmp(h, "MESH", 4) == 0)
		{
			if (size < 325 || memcmp(p, "PMSH", 4) != 0 || Le32(p + 4) != size - 8 ||
				memcmp(p + 8, "PMS2", 4) != 0 || Le32(p + 12) != size - 16)
				Fail(kErrVerifyFailed);
			if (p[16] != 0)
			{
				if ((size - 325) % 317 != 0)
					Fail(kErrVerifyFailed);
			}
			else
			{
				if (++inlineMeshes > 1)
					Fail(kErrVerifyFailed);
				if (std::any_of(p + 16, p + 316, [](uint8_t c) { return c != 0; }))
					Fail(kErrVerifyFailed);
				const uint32_t rid = Le32(p + 316);
				if (rid < 20000 || rid > 29999 || Le32(p + 320) != 0 || p[324] > 1)
					Fail(kErrVerifyFailed);
				if (!VerifyInlineMesh(p + 325, size - 325, rid))
					Fail(kErrVerifyFailed);
			}
		}
		if (memcmp(h, "WRES", 4) == 0)
		{
			wres++;
			wresAt = at + 8;
			wresSize = size;
		}
		else if (wres)
			Fail(kErrVerifyFailed);
		at += 8 + size;
	}
	if (count < 3 || wres != 1)
		Fail(kErrVerifyFailed);
	const uint8_t *w = unpacked.data() + wresAt;
	if (wresSize < 8 + 16 + 10 || memcmp(w, "WRLD", 4) != 0 || Le32(w + 4) != wresSize - 8)
		Fail(kErrVerifyFailed);
	const std::vector<ChunkSpan> wkids = TileChunks(unpacked.data(), wresAt + 8, wresAt + wresSize, kErrVerifyFailed);
	if (wkids.size() != 2 || strcmp(wkids[0].tag, "WDAT") != 0 || wkids[0].end - wkids[0].start != 8 ||
		strcmp(wkids[1].tag, "RLWD") != 0)
		Fail(kErrVerifyFailed);
	if (LeI32(unpacked.data() + wkids[0].start) != -1 || LeI32(unpacked.data() + wkids[0].start + 4) != kTargetLevel)
		Fail(kErrVerifyFailed);
	const size_t bsz = wkids[1].end - wkids[1].start;
	if (bsz < 2 || bsz > (4u << 20))
		Fail(kErrVerifyFailed);
	const uint16_t bver = Le16(unpacked.data() + wkids[1].start);
	if (bver < 43 || bver > 50)
		Fail(kErrVerifyFailed);
}

// ---- full Level 888 archive build ------------------------------------------------------------------------------------

std::vector<uint8_t> BuildLevel888Archive(const PcInstall &install, const Ps2ExtractedMember &ps2,
										  const std::atomic<bool> *cancel)
{
	if (install.ExistsAny(kArchiveRelPath))
		Fail(kErrAlreadyInGame);

	ValidateSshd(ps2.shadows);
	const WorldHeaderRecord rec888 = ParseWorldHeaderForLevel(install.ReadFile(kWorldHeadersRelPath, kLimitSmall), kTargetLevel);
	const std::vector<std::string> worldUnits = ValidatePs2WorldBody(ps2.worldBody, rec888);

	const PhysicsDb physics(install.ReadFile(kPhysicsRelPath, kLimitSmall));
	std::vector<std::string> closureUnits;
	std::vector<std::string> closureMeshes;
	for (const std::string &u : worldUnits)
		physics.UnitMeshClosure(u, &closureUnits, &closureMeshes);

	bool usesFenrirMesh = false;
	for (const std::string &m : closureMeshes)
		if (AsciiLower(m) == kFenrirMeshName)
			usesFenrirMesh = true;
	if (!usesFenrirMesh)
		Fail(kErrBadWorld);

	std::vector<std::string> wantedMeshes;
	std::vector<std::string> wantedTextures;
	auto addUniqueTexture = [&](const std::string &tex) {
		const std::string key = NormalizeAssetName(tex);
		for (const std::string &known : wantedTextures)
			if (NormalizeAssetName(known) == key)
				return;
		wantedTextures.push_back(tex);
	};

	for (const std::string &mesh : closureMeshes)
	{
		CheckCancel(cancel);
		if (AsciiLower(mesh) == kFenrirMeshName)
		{
			for (const std::string &tex : ReadCmshTextureNames(ps2.fenrirCmsh, kErrBadMesh))
				addUniqueTexture(tex);
		}
		else
		{
			wantedMeshes.push_back(mesh);
			const std::string rel = "data/resources/meshes/m_" + mesh + ".aya";
			const std::vector<uint8_t> rawMesh = UnpackPcAya(install.ReadFile(rel, kLimitArchive), cancel);
			for (const std::string &tex : ReadCmshTextureNames(rawMesh, kErrBadGameFiles))
				addUniqueTexture(tex);
		}
	}

	auto unpackLevel = [&](int level) -> std::vector<ArchiveChunk> {
		char rel[64];
		snprintf(rel, sizeof(rel), "data/resources/%03d_res_PC.aya", level);
		return SplitArchiveChunks(UnpackPcAya(install.ReadFile(rel, kLimitArchive), cancel));
	};

	const std::vector<ArchiveChunk> donor = unpackLevel(kDonorLevel);
	std::vector<std::vector<ArchiveChunk>> sources;
	sources.reserve(4);
	for (int srcLvl : kSourceLevels)
		sources.push_back(unpackLevel(srcLvl));

	std::vector<ArchiveChunk> merged = MergeDescriptors(donor, sources, wantedMeshes, wantedTextures);

	// Insert inline Fenrir MESH chunk immediately after the last MESH chunk.
	int lastMeshIdx = -1;
	for (size_t i = 0; i < merged.size(); i++)
		if (merged[i].tag == "MESH")
			lastMeshIdx = (int)i;
	if (lastMeshIdx < 0)
		Fail(kErrBadGameFiles);
	merged.insert(merged.begin() + lastMeshIdx + 1,
				  ArchiveChunk{"MESH", MakeInlineMeshPayload(ps2.fenrirCmsh, kInlineMeshId)});

	// Replace the single SSHD chunk with the PS2 level's SSHD chunk.
	int sshdIdx = -1, sshdCount = 0;
	for (size_t i = 0; i < merged.size(); i++)
		if (merged[i].tag == "SSHD")
			{ sshdIdx = (int)i; sshdCount++; }
	if (sshdCount != 1)
		Fail(kErrBadGameFiles);
	merged[(size_t)sshdIdx].payload = ps2.shadows;

	// Assemble raw stream with WRES -> WRLD -> WDAT(-1, 888) + RLWD(ps2.worldBody).
	uint8_t wdatPayload[8];
	PutLe32(wdatPayload + 0, (uint32_t)(int32_t)-1);
	PutLe32(wdatPayload + 4, (uint32_t)kTargetLevel);
	std::vector<uint8_t> wrldInner;
	AppendChunk(wrldInner, "WDAT", wdatPayload, sizeof(wdatPayload));
	AppendChunk(wrldInner, "RLWD", ps2.worldBody);
	std::vector<uint8_t> wresPayload;
	AppendChunk(wresPayload, "WRLD", wrldInner);

	std::vector<uint8_t> rawArchive;
	int wresReplaced = 0;
	for (const ArchiveChunk &c : merged)
	{
		if (c.tag == "WRES")
		{
			wresReplaced++;
			AppendChunk(rawArchive, "WRES", wresPayload);
		}
		else
		{
			AppendChunk(rawArchive, c.tag.c_str(), c.payload);
		}
	}
	if (wresReplaced != 1)
		Fail(kErrBadGameFiles);

	std::vector<uint8_t> packed = PackPcAya(rawArchive, cancel);
	VerifyBuiltArchive(packed, rawArchive, cancel);
	return packed;
}

// ---- atomic mod staging & ce.ini enablement --------------------------------------------------------------------------

std::string ChooseCeMinVersion()
{
#ifdef CE_VERSION
	const std::string v = CE_VERSION;
	const size_t cut = v.find_first_not_of("0123456789.");
	const std::string num = v.substr(0, cut);
	if (!num.empty() && std::isdigit((unsigned char)num.front()) && std::isdigit((unsigned char)num.back()))
	{
		// Parse up to 4 numeric components; if running released version is older than 0.1.13, emit running version so it loads.
		unsigned parts[4] = {};
		int count = 0;
		size_t pos = 0;
		bool ok = true;
		while (ok && pos < num.size())
		{
			if (count == 4 || !std::isdigit((unsigned char)num[pos])) { ok = false; break; }
			unsigned val = 0;
			while (pos < num.size() && std::isdigit((unsigned char)num[pos]))
			{
				val = val * 10 + (unsigned)(num[pos++] - '0');
				if (val > 99999) { ok = false; break; }
			}
			parts[count++] = val;
			if (pos < num.size() && num[pos++] != '.')
				ok = false;
		}
		if (ok && count >= 2)
		{
			const unsigned def[4] = {0, 1, 13, 0};
			for (int i = 0; i < 4; i++)
			{
				if (parts[i] != def[i])
					return parts[i] < def[i] ? num : std::string(kDefaultCeMin);
			}
		}
	}
#endif
	return kDefaultCeMin;
}

std::string BuildManifestJson(size_t archiveSize, const char archiveSha[65])
{
	const std::string ceMin = ChooseCeMinVersion();
	char buf[1024];
	snprintf(buf, sizeof(buf),
			 "{\n"
			 "  \"schema\": 1,\n"
			 "  \"id\": \"%s\",\n"
			 "  \"name\": \"%s\",\n"
			 "  \"version\": \"%s\",\n"
			 "  \"description\": \"%s\",\n"
			 "  \"ce_min\": \"%s\",\n"
			 "  \"files\": [\n"
			 "    {\n"
			 "      \"path\": \"%s\",\n"
			 "      \"size\": %llu,\n"
			 "      \"sha256\": \"%s\"\n"
			 "    }\n"
			 "  ]\n"
			 "}\n",
			 kModId, kModName, kModVersion, kModDescription, ceMin.c_str(),
			 kArchiveRelPath, (unsigned long long)archiveSize, archiveSha);
	return buf;
}

#ifdef _WIN32
bool EnsureRealDirWin(const std::string &path)
{
	std::vector<wchar_t> wp;
	if (!Utf8ToWide(path, &wp))
		return false;
	if (!CreateDirectoryW(wp.data(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
		return false;
	const DWORD attr = GetFileAttributesW(wp.data());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) && !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
}

void RemoveTreeWin(const std::string &path)
{
	std::vector<wchar_t> wp;
	if (!Utf8ToWide(path, &wp))
		return;
	const DWORD attr = GetFileAttributesW(wp.data());
	if (attr == INVALID_FILE_ATTRIBUTES)
		return;
	if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) || !(attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		if (attr & FILE_ATTRIBUTE_DIRECTORY)
			RemoveDirectoryW(wp.data());
		else
			DeleteFileW(wp.data());
		return;
	}
	std::string pattern = path + "\\*";
	std::vector<wchar_t> wpat;
	if (Utf8ToWide(pattern, &wpat))
	{
		WIN32_FIND_DATAW fd{};
		HANDLE h = FindFirstFileW(wpat.data(), &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
					continue;
				std::string childName;
				if (WideToUtf8(fd.cFileName, &childName))
					RemoveTreeWin(path + "\\" + childName);
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}
	}
	RemoveDirectoryW(wp.data());
}

bool WriteNewFileWin(const std::string &path, const uint8_t *data, size_t len)
{
	std::vector<wchar_t> wp;
	if (!Utf8ToWide(path, &wp))
		return false;
	HANDLE h = CreateFileW(wp.data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	size_t done = 0;
	bool ok = true;
	while (ok && done < len)
	{
		const DWORD chunk = (len - done) > (1u << 20) ? (1u << 20) : (DWORD)(len - done);
		DWORD written = 0;
		if (!WriteFile(h, data + done, chunk, &written, nullptr) || written != chunk)
			ok = false;
		done += written;
	}
	if (ok)
		ok = FlushFileBuffers(h) != 0;
	CloseHandle(h);
	return ok;
}
#else
bool EnsureRealDirPosix(const std::string &path)
{
	struct stat st{};
	if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST)
		return false;
	return lstat(path.c_str(), &st) == 0 && !S_ISLNK(st.st_mode) && S_ISDIR(st.st_mode);
}

void RemoveTreePosix(const std::string &path)
{
	struct stat st{};
	if (lstat(path.c_str(), &st) != 0)
		return;
	if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode))
	{
		unlink(path.c_str());
		return;
	}
	if (DIR *d = opendir(path.c_str()))
	{
		while (struct dirent *ent = readdir(d))
		{
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;
			RemoveTreePosix(path + "/" + ent->d_name);
		}
		closedir(d);
	}
	rmdir(path.c_str());
}

bool WriteNewFilePosix(const std::string &path, const uint8_t *data, size_t len)
{
	int flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC;
#ifdef O_LARGEFILE
	flags |= O_LARGEFILE;
#endif
	const int fd = open(path.c_str(), flags, 0600);
	if (fd < 0)
		return false;
	size_t done = 0;
	bool ok = true;
	while (ok && done < len)
	{
		const size_t chunk = std::min(len - done, (size_t)(1u << 20));
		const ssize_t w = write(fd, data + done, chunk);
		if (w < 0)
		{
			if (errno == EINTR)
				continue;
			ok = false;
			break;
		}
		done += (size_t)w;
	}
	if (ok && fsync(fd) != 0)
		ok = false;
	if (close(fd) != 0)
		ok = false;
	return ok;
}
#endif

std::atomic<uint32_t> gStageCounter{1};

bool ListExactRealEntries(const std::string &dir, const std::vector<std::pair<const char *, bool>> &expected)
{
	std::vector<std::pair<std::string, bool>> found;
#ifdef _WIN32
	std::vector<wchar_t> wpat;
	if (!Utf8ToWide(dir + "\\*", &wpat))
		return false;
	WIN32_FIND_DATAW fd{};
	HANDLE h = FindFirstFileW(wpat.data(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	bool ok = true;
	do
	{
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			ok = false;
			break;
		}
		std::string name;
		if (!WideToUtf8(fd.cFileName, &name))
		{
			ok = false;
			break;
		}
		found.emplace_back(std::move(name), (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
		if (found.size() > expected.size())
		{
			ok = false;
			break;
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	if (!ok || found.size() != expected.size())
		return false;
#else
	DIR *d = opendir(dir.c_str());
	if (!d)
		return false;
	bool ok = true;
	while (struct dirent *ent = readdir(d))
	{
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;
		struct stat st{};
		const std::string child = dir + "/" + ent->d_name;
		if (lstat(child.c_str(), &st) != 0 || S_ISLNK(st.st_mode))
		{
			ok = false;
			break;
		}
		if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))
		{
			ok = false;
			break;
		}
		found.emplace_back(ent->d_name, S_ISDIR(st.st_mode));
		if (found.size() > expected.size())
		{
			ok = false;
			break;
		}
	}
	closedir(d);
	if (!ok || found.size() != expected.size())
		return false;
#endif
	for (const auto &want : expected)
	{
		bool matched = false;
		for (const auto &got : found)
		{
			if (got.first == want.first && got.second == want.second)
			{
				matched = true;
				break;
			}
		}
		if (!matched)
			return false;
	}
	return true;
}

bool IsCleanPriorImportDir(const std::string &dir)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	if (!ListExactRealEntries(dir, {{"data", true}, {"mod.json", false}}))
		return false;
	const std::string dataDir = dir + sep + "data";
	if (!ListExactRealEntries(dataDir, {{"resources", true}}))
		return false;
	const std::string resDir = dataDir + sep + "resources";
	if (!ListExactRealEntries(resDir, {{"888_res_PC.aya", false}}))
		return false;

	RawFile arch;
	uint64_t archSz = 0;
	if (!arch.OpenRegular((resDir + sep + "888_res_PC.aya").c_str(), &archSz) ||
		archSz == 0 || archSz > kMaxMemberBytes)
		return false;
	std::vector<uint8_t> archBytes((size_t)archSz);
	if (!arch.ReadAt(0, archBytes.data(), archBytes.size()))
		return false;
	char sha[65];
	Sha256Hex(archBytes.data(), archBytes.size(), sha);
	const std::string expectedManifest = BuildManifestJson(archBytes.size(), sha);

	RawFile mf;
	uint64_t mfSz = 0;
	if (!mf.OpenRegular((dir + sep + "mod.json").c_str(), &mfSz) || mfSz != expectedManifest.size())
		return false;
	std::string actualManifest((size_t)mfSz, '\0');
	if (!mf.ReadAt(0, (uint8_t *)actualManifest.data(), actualManifest.size()))
		return false;
	return actualManifest == expectedManifest;
}

void StageModAtomically(const char *user_dir, const std::vector<uint8_t> &archiveBytes)
{
	if (!user_dir || !*user_dir)
		Fail(kErrStageFailed);

	char archiveSha[65];
	Sha256Hex(archiveBytes.data(), archiveBytes.size(), archiveSha);
	const std::string manifest = BuildManifestJson(archiveBytes.size(), archiveSha);

#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	std::string udir(user_dir);
	while (udir.size() > 1 && (udir.back() == '/' || udir.back() == '\\'))
	{
#ifdef _WIN32
		if (udir.size() == 3 && udir[1] == ':')
			break;
#endif
		udir.pop_back();
	}

	const std::string modsDir = udir + sep + "mods";
	const std::string targetDir = modsDir + sep + kModId;

#ifdef _WIN32
	if (!EnsureRealDirWin(udir) || !EnsureRealDirWin(modsDir))
		Fail(kErrStageFailed);
	std::vector<wchar_t> wtarget;
	if (!Utf8ToWide(targetDir, &wtarget))
		Fail(kErrStageFailed);
	const DWORD existingAttr = GetFileAttributesW(wtarget.data());
	const bool targetExists = (existingAttr != INVALID_FILE_ATTRIBUTES);
	if (targetExists && ((existingAttr & FILE_ATTRIBUTE_REPARSE_POINT) || !(existingAttr & FILE_ATTRIBUTE_DIRECTORY)))
		Fail(kErrStageFailed);
	const unsigned long pid = (unsigned long)GetCurrentProcessId();
#else
	if (!EnsureRealDirPosix(udir) || !EnsureRealDirPosix(modsDir))
		Fail(kErrStageFailed);
	struct stat tst{};
	bool targetExists = false;
	if (lstat(targetDir.c_str(), &tst) == 0)
	{
		if (S_ISLNK(tst.st_mode) || !S_ISDIR(tst.st_mode))
			Fail(kErrStageFailed);
		targetExists = true;
	}
	else if (errno != ENOENT)
	{
		Fail(kErrStageFailed);
	}
	const unsigned long pid = (unsigned long)getpid();
#endif

	const uint32_t seq = gStageCounter.fetch_add(1, std::memory_order_relaxed);
	char suffix[64];
	snprintf(suffix, sizeof(suffix), "%lu-%u", pid, (unsigned)seq);
	const std::string stageDir = modsDir + sep + ".stage-" + kModId + "-" + suffix;
	const std::string backupDir = modsDir + sep + ".old-" + kModId + "-" + suffix;

	auto cleanupStage = [&]() {
#ifdef _WIN32
		RemoveTreeWin(stageDir);
#else
		RemoveTreePosix(stageDir);
#endif
	};

	bool ok = false;
#ifdef _WIN32
	ok = EnsureRealDirWin(stageDir) &&
		 EnsureRealDirWin(stageDir + "\\data") &&
		 EnsureRealDirWin(stageDir + "\\data\\resources") &&
		 WriteNewFileWin(stageDir + "\\data\\resources\\888_res_PC.aya", archiveBytes.data(), archiveBytes.size()) &&
		 WriteNewFileWin(stageDir + "\\mod.json", (const uint8_t *)manifest.data(), manifest.size());
#else
	ok = (mkdir(stageDir.c_str(), 0700) == 0) &&
		 (mkdir((stageDir + "/data").c_str(), 0700) == 0) &&
		 (mkdir((stageDir + "/data/resources").c_str(), 0700) == 0) &&
		 WriteNewFilePosix(stageDir + "/data/resources/888_res_PC.aya", archiveBytes.data(), archiveBytes.size()) &&
		 WriteNewFilePosix(stageDir + "/mod.json", (const uint8_t *)manifest.data(), manifest.size());
#endif
	if (!ok)
	{
		cleanupStage();
		Fail(kErrStageFailed);
	}

	// Verify written archive on disk matches SHA-256 before moving into place.
	{
		const std::string stagedArchive = stageDir + sep + "data" + sep + "resources" + sep + "888_res_PC.aya";
		RawFile check;
		uint64_t csz = 0;
		if (!check.OpenRegular(stagedArchive.c_str(), &csz) || csz != archiveBytes.size())
		{
			cleanupStage();
			Fail(kErrStageFailed);
		}
		std::vector<uint8_t> readBack((size_t)csz);
		char checkSha[65];
		if (!check.ReadAt(0, readBack.data(), readBack.size()))
		{
			cleanupStage();
			Fail(kErrStageFailed);
		}
		Sha256Hex(readBack.data(), readBack.size(), checkSha);
		if (strcmp(checkSha, archiveSha) != 0)
		{
			cleanupStage();
			Fail(kErrStageFailed);
		}
	}

	// Atomic directory swap with rollback if final rename fails.
#ifdef _WIN32
	std::vector<wchar_t> wstage, wbackup;
	if (!Utf8ToWide(stageDir, &wstage) || !Utf8ToWide(backupDir, &wbackup))
	{
		cleanupStage();
		Fail(kErrStageFailed);
	}
	bool movedOld = false;
	if (targetExists)
	{
		if (!MoveFileExW(wtarget.data(), wbackup.data(), 0))
		{
			cleanupStage();
			Fail(kErrStageFailed);
		}
		movedOld = true;
	}
	if (!MoveFileExW(wstage.data(), wtarget.data(), 0))
	{
		if (movedOld)
			MoveFileExW(wbackup.data(), wtarget.data(), 0);
		cleanupStage();
		Fail(kErrStageFailed);
	}
	if (movedOld)
	{
		if (IsCleanPriorImportDir(backupDir))
		{
			RemoveTreeWin(backupDir);
		}
		else
		{
			const std::string keptBackup = udir + sep + kModId + "-backup-" + suffix;
			std::vector<wchar_t> wkept;
			if (Utf8ToWide(keptBackup, &wkept))
				MoveFileExW(wbackup.data(), wkept.data(), 0);
		}
	}
#else
	bool movedOld = false;
	if (targetExists)
	{
		if (rename(targetDir.c_str(), backupDir.c_str()) != 0)
		{
			cleanupStage();
			Fail(kErrStageFailed);
		}
		movedOld = true;
	}
	if (rename(stageDir.c_str(), targetDir.c_str()) != 0)
	{
		if (movedOld)
			(void)rename(backupDir.c_str(), targetDir.c_str());
		cleanupStage();
		Fail(kErrStageFailed);
	}
	if (movedOld)
	{
		if (IsCleanPriorImportDir(backupDir))
		{
			RemoveTreePosix(backupDir);
		}
		else
		{
			const std::string keptBackup = udir + sep + kModId + "-backup-" + suffix;
			(void)rename(backupDir.c_str(), keptBackup.c_str());
		}
	}
#endif
}

bool ValidModIdLocal(const std::string &id)
{
	if (id.empty() || id.size() > 48)
		return false;
	for (char c : id)
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return false;
	std::string stem = id.substr(0, id.find('.'));
	for (char &c : stem)
		c = (char)std::toupper((unsigned char)c);
	static const char *const kDevs[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
	for (const char *d : kDevs)
		if (stem == d)
			return false;
	if (stem.size() == 4 && (!stem.compare(0, 3, "COM") || !stem.compare(0, 3, "LPT")) &&
		stem[3] >= '0' && stem[3] <= '9')
		return false;
	return true;
}

// Updates ce.ini's mods= line on the caller's thread via PortModsWriteLine.
// Returns true on success (setting *enabled_out to 1 if enabled, or 0 if 16 other mods were already on).
bool EnableModOnMainThread(int *enabled_out)
{
	if (enabled_out)
		*enabled_out = 0;

	std::vector<std::string> ids;
	char raw[2048] = "";
	if (CE_SettingGetText("mods", raw, sizeof(raw)))
	{
		const char *p = raw;
		while (*p)
		{
			while (*p == ' ' || *p == '\t' || *p == ',')
				p++;
			if (!*p)
				break;
			const char *start = p;
			while (*p && *p != ',')
				p++;
			const char *end = p;
			while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
				end--;
			std::string token(start, (size_t)(end - start));
			if (ValidModIdLocal(token) &&
				std::find(ids.begin(), ids.end(), token) == ids.end() &&
				(int)ids.size() < PORT_MODS_ON)
			{
				ids.push_back(std::move(token));
			}
		}
	}

	const bool alreadyOn = std::find(ids.begin(), ids.end(), kModId) != ids.end();
	if (!alreadyOn)
	{
		if ((int)ids.size() >= PORT_MODS_ON)
		{
			if (enabled_out)
				*enabled_out = 0;
			return true;
		}
		ids.push_back(kModId);
	}

	std::vector<const char *> ptrs;
	ptrs.reserve(ids.size());
	for (const std::string &s : ids)
		ptrs.push_back(s.c_str());

	if (!PortModsWriteLine(ptrs.data(), (int)ptrs.size()))
		return false;
	if (enabled_out)
		*enabled_out = 1;
	return true;
}

template <typename StageFn>
bool RunCorePipeline(const char *source_path, const char *game_dir, const char *user_dir,
					 const std::atomic<bool> *cancel, StageFn setStage, char *error_out, size_t error_cap)
{
	try
	{
		setStage("Reading PS2 disc image...");
		const std::vector<uint8_t> member = ReadSourceMember(source_path, game_dir, user_dir, cancel);

		CheckCancel(cancel);
		setStage("Converting PS2 Fenrir mesh...");
		const Ps2ExtractedMember ps2 = ExtractPs2MemberParts(member, cancel);

		CheckCancel(cancel);
		setStage("Building Level 888 archive...");
		const PcInstall install(game_dir);
		const std::vector<uint8_t> archive = BuildLevel888Archive(install, ps2, cancel);

		CheckCancel(cancel);
		setStage("Writing mod to mods folder...");
		StageModAtomically(user_dir, archive);
		CopyStr(error_out, error_cap, "");
		return true;
	}
	catch (const ImportFail &f)
	{
		CopyStr(error_out, error_cap, f.msg);
		return false;
	}
	catch (const std::bad_alloc &)
	{
		CopyStr(error_out, error_cap, "There is not enough memory to import Level 888.");
		return false;
	}
}

// ---- async file picker (SDL_ShowOpenFileDialog) ----------------------------------------------------------------------

struct PickerState
{
	std::mutex				lock;
	unsigned				generation = 0;
	bool					open = false;
	CePs2ImportPickStatus	view{CE_PS2_PICK_IDLE, "", "", ""};
};

struct PickerTicket
{
	unsigned	generation;
};

PickerState &Picker()
{
	static PickerState s;
	return s;
}

void SetPickNote(CePs2ImportPickStatus &v, CePs2ImportPickState st, const char *msg)
{
	v.state = st;
	CopyStr(v.message, sizeof(v.message), msg);
}

#ifndef CE_PS2_IMPORT_NO_SDL
static const SDL_DialogFileFilter kPs2Filters[] = {
	{"PS2 Disc Image / Level 888", "iso;zip;aya"},
};

void SDLCALL OnPs2FilePicked(void *userdata, const char *const *files, int)
{
	PickerTicket *ticket = static_cast<PickerTicket *>(userdata);
	const unsigned generation = ticket ? ticket->generation : 0;
	delete ticket;

	PickerState &s = Picker();
	std::lock_guard<std::mutex> hold(s.lock);
	if (!s.open || generation != s.generation)
		return;
	if (!files)
	{
		SetPickNote(s.view, CE_PS2_PICK_ERROR, "Your file chooser could not be opened.");
		return;
	}
	if (!files[0])
	{
		SetPickNote(s.view, CE_PS2_PICK_CANCELLED, "");
		return;
	}
	if (files[1] || !files[0][0] || strnlen(files[0], sizeof(s.view.path)) >= sizeof(s.view.path))
	{
		SetPickNote(s.view, CE_PS2_PICK_ERROR, "Choose one PS2 disc image file.");
		return;
	}
	const char *fwd = strrchr(files[0], '/');
	const char *bwd = strrchr(files[0], '\\');
	const char *sep = (!fwd) ? bwd : (!bwd) ? fwd : (fwd > bwd ? fwd : bwd);
	const char *leaf = sep ? sep + 1 : files[0];
	if (!leaf[0] || strnlen(leaf, sizeof(s.view.leaf)) >= sizeof(s.view.leaf))
	{
		SetPickNote(s.view, CE_PS2_PICK_ERROR, "Choose one PS2 disc image file.");
		return;
	}
	CopyStr(s.view.path, sizeof(s.view.path), files[0]);
	CopyStr(s.view.leaf, sizeof(s.view.leaf), leaf);
	SetPickNote(s.view, CE_PS2_PICK_READY, "Reading PS2 disc image...");
}
#endif

// ---- background worker state -----------------------------------------------------------------------------------------

struct WorkerState
{
	std::mutex			lock;
	std::thread			thread;
	std::atomic<bool>	cancel{false};
	bool				threadActive = false;
	bool				stagedPendingEnable = false;
	CePs2ImportStatus	status{CE_PS2_IMPORT_IDLE, 0, "", ""};
};

WorkerState &Worker()
{
	static WorkerState w;
	return w;
}

} // namespace

// ---- public API ------------------------------------------------------------------------------------------------------

void CePs2ImportPickEnter()
{
	PickerState &s = Picker();
	std::lock_guard<std::mutex> hold(s.lock);
	s.open = true;
	++s.generation;
	s.view = {CE_PS2_PICK_IDLE, "", "", ""};
}

void CePs2ImportPickLeave()
{
	PickerState &s = Picker();
	std::lock_guard<std::mutex> hold(s.lock);
	s.open = false;
	++s.generation;
	s.view.state = CE_PS2_PICK_IDLE;
}

void CePs2ImportPickAck()
{
	PickerState &s = Picker();
	std::lock_guard<std::mutex> hold(s.lock);
	if (s.view.state != CE_PS2_PICK_OPEN)
		s.view = {CE_PS2_PICK_IDLE, "", "", ""};
}

void CePs2ImportPickOpen()
{
	PickerState &s = Picker();
#ifdef CE_PS2_IMPORT_NO_SDL
	std::lock_guard<std::mutex> hold(s.lock);
	if (!s.open || s.view.state == CE_PS2_PICK_OPEN)
		return;
	SetPickNote(s.view, CE_PS2_PICK_ERROR, "Your file chooser could not be opened.");
#else
	PickerTicket *ticket = nullptr;
	{
		std::lock_guard<std::mutex> hold(s.lock);
		if (!s.open || s.view.state == CE_PS2_PICK_OPEN)
			return;
		ticket = new (std::nothrow) PickerTicket{s.generation};
		if (!ticket)
		{
			SetPickNote(s.view, CE_PS2_PICK_ERROR, "There is not enough memory.");
			return;
		}
		s.view.path[0] = 0;
		s.view.leaf[0] = 0;
		SetPickNote(s.view, CE_PS2_PICK_OPEN, "Choose your PS2 disc image in the file window.");
	}
	SDL_ShowOpenFileDialog(OnPs2FilePicked, ticket, SDL_GetKeyboardFocus(), kPs2Filters, 1, nullptr, false);
#endif
}

void CePs2ImportPickPoll(CePs2ImportPickStatus *out)
{
	if (!out)
		return;
	PickerState &s = Picker();
	std::lock_guard<std::mutex> hold(s.lock);
	*out = s.view;
}

bool CePs2ImportRun(const char *source_path, const char *game_dir, const char *user_dir,
					int *enabled_out, char *error_out, size_t error_cap)
{
	if (enabled_out)
		*enabled_out = 0;
	char localErr[256] = "";
	if (!RunCorePipeline(source_path, game_dir, user_dir, nullptr, [](const char *) {}, localErr, sizeof(localErr)))
	{
		CopyStr(error_out, error_cap, localErr);
		return false;
	}
	int enabled = 0;
	if (!EnableModOnMainThread(&enabled))
	{
		CopyStr(error_out, error_cap, kErrSettingsFailed);
		return false;
	}
	if (enabled_out)
		*enabled_out = enabled;
	CopyStr(error_out, error_cap, "");
	return true;
}

bool CePs2ImportStart(const char *source_path, const char *game_dir, const char *user_dir)
{
	WorkerState &w = Worker();
	std::thread joinPrev;
	{
		std::lock_guard<std::mutex> hold(w.lock);
		if (w.threadActive)
			return false;
		if (w.thread.joinable())
			joinPrev = std::move(w.thread);
	}
	if (joinPrev.joinable())
		joinPrev.join();

	const std::string src = source_path ? source_path : "";
	const std::string gdir = game_dir ? game_dir : "";
	const std::string udir = user_dir ? user_dir : "";

	std::lock_guard<std::mutex> hold(w.lock);
	if (w.threadActive)
		return false;
	w.cancel.store(false, std::memory_order_relaxed);
	w.stagedPendingEnable = false;
	w.status = {CE_PS2_IMPORT_RUNNING, 0, "", ""};
	CopyStr(w.status.stage, sizeof(w.status.stage), "Reading PS2 disc image...");
	w.threadActive = true;

	try
	{
		w.thread = std::thread([src, gdir, udir]() {
			WorkerState &ws = Worker();
			auto updateStage = [&ws](const char *stageText) {
				std::lock_guard<std::mutex> guard(ws.lock);
				CopyStr(ws.status.stage, sizeof(ws.status.stage), stageText);
			};
			char err[256] = "";
			const bool ok = RunCorePipeline(src.c_str(), gdir.c_str(), udir.c_str(), &ws.cancel,
											updateStage, err, sizeof(err));
			const bool wasCancelled = ws.cancel.load(std::memory_order_relaxed);

			std::lock_guard<std::mutex> guard(ws.lock);
			ws.threadActive = false;
			if (ok && !wasCancelled)
			{
				// EnableModOnMainThread runs on the next CePs2ImportPoll so ce.cpp's settings cache stays single-threaded.
				ws.stagedPendingEnable = true;
			}
			else if (wasCancelled)
			{
				ws.status.state = CE_PS2_IMPORT_CANCELLED;
				ws.status.stage[0] = 0;
				CopyStr(ws.status.error, sizeof(ws.status.error), kErrCancelled);
			}
			else
			{
				ws.status.state = CE_PS2_IMPORT_FAILED;
				ws.status.stage[0] = 0;
				CopyStr(ws.status.error, sizeof(ws.status.error), err[0] ? err : kErrStageFailed);
			}
		});
	}
	catch (...)
	{
		w.threadActive = false;
		w.status.state = CE_PS2_IMPORT_FAILED;
		w.status.stage[0] = 0;
		CopyStr(w.status.error, sizeof(w.status.error), "The background importer could not be started.");
		return false;
	}
	return true;
}

void CePs2ImportPoll(CePs2ImportStatus *out)
{
	WorkerState &w = Worker();
	std::thread finishedThread;
	bool doEnable = false;
	{
		std::lock_guard<std::mutex> hold(w.lock);
		if (!w.threadActive && w.thread.joinable())
			finishedThread = std::move(w.thread);
		if (w.stagedPendingEnable)
		{
			w.stagedPendingEnable = false;
			doEnable = true;
		}
	}
	if (finishedThread.joinable())
		finishedThread.join();

	if (doEnable)
	{
		int enabled = 0;
		const bool ok = EnableModOnMainThread(&enabled);
		std::lock_guard<std::mutex> hold(w.lock);
		w.status.stage[0] = 0;
		if (ok)
		{
			w.status.state = CE_PS2_IMPORT_DONE;
			w.status.enabled = enabled;
			w.status.error[0] = 0;
			fprintf(stderr, PORT_LOG_PREFIX "mods: imported %s (enabled=%d)\n", kModId, enabled);
		}
		else
		{
			w.status.state = CE_PS2_IMPORT_FAILED;
			w.status.enabled = 0;
			CopyStr(w.status.error, sizeof(w.status.error), kErrSettingsFailed);
		}
	}

	if (out)
	{
		std::lock_guard<std::mutex> hold(w.lock);
		*out = w.status;
	}
}

void CePs2ImportAck()
{
	WorkerState &w = Worker();
	std::lock_guard<std::mutex> hold(w.lock);
	if (!w.threadActive && !w.stagedPendingEnable)
		w.status = {CE_PS2_IMPORT_IDLE, 0, "", ""};
}

void CePs2ImportCancel()
{
	WorkerState &w = Worker();
	std::thread toJoin;
	{
		std::lock_guard<std::mutex> hold(w.lock);
		w.cancel.store(true, std::memory_order_relaxed);
		if (w.thread.joinable())
			toJoin = std::move(w.thread);
	}
	if (toJoin.joinable())
		toJoin.join();
	{
		std::lock_guard<std::mutex> hold(w.lock);
		w.threadActive = false;
		w.stagedPendingEnable = false;
		w.status = {CE_PS2_IMPORT_IDLE, 0, "", ""};
	}
}
