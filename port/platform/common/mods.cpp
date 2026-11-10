// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/mods.cpp: Onslaught CE mods (experimental; mods.h has the model). At the first read of a game
// file the enabled mods are loaded once and checked:
// - the folder: <user folder>/mods/<id>/, id = lowercase letters, digits, '-' and '_' and not a Windows device name (con,
//   aux, nul, prn, com0 to com9, lpt0 to lpt9: no Windows folder can have one), opened without following links;
// - mod.json: strict JSON (no unknown or repeated keys), schema 1, the mod's id, name, version, author, description,
//   the CE versions it supports (ce_min, ce_max) and every file it replaces with its size and SHA-256;
// - each file: a game-relative path under data/ (no "..", no absolute or drive paths, no backslashes, no Windows device
//   names, streams, trailing dots or short-name look-alikes such as PROGRA~1; on Windows every name must also be spelled
//   as the volume holds it), a file the game itself has (or one of the two names a mod may add: a world body,
//   data/worlds/worldNNN.wrd, and the archive of a multiplayer level the game has none for, data/resources/NNN_res_PC.aya
//   with NNN from 850 to 899), reached without links, a regular file with one link, at most 64 MiB, matching its size and
//   SHA-256; a packed .aya file must have the block structure the game's reader can hold (CDXMemBuffer::ReadBlock), a
//   texture must be a DDS image the game's loader can hold (LoadFrame), and an added level archive must also have, as its
//   chunk headers declare it, the shape of the archives the game ships (CheckLevelArchive: at most 8 MiB packed and 32 MiB
//   unpacked);
// - bounds: 16 mods, 4096 files per mod, 8192 files and 1 GiB in all.
// A mod with any refused file is refused whole; bea --check-mods (PortModsCheck, mods.h) runs the same checks alone and
// prints every refusal instead of stopping at a mod's first. The log names each active mod and every replaced or added file by its
// game-relative path only (never the user folder's location). At run time a replaced Linux file is opened without
// following links, read again, checked and returned as a sealed immutable snapshot; on Windows the file is opened one
// component at a time below the held folder, hashed as it is copied into a private delete-on-close file, and that file is
// what the game reads (OpenVerified). If any step fails the game's file is used instead. Windows mods stay off in every
// build without PORT_MODS_WINDOWS_ON (PlatformOff) until the checks that only NTFS can show have passed.
#include "mods.h"
#include "portlog.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602	// FILE_ID_INFO and FileIdInfo are Windows 8 queries
#endif
#include <windows.h>
#include <winternl.h>
#else
#include <fcntl.h>
#include <linux/memfd.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

extern "C" {
#include "../../updater/sha256.h"
int uncompress2(unsigned char *dest, unsigned long *destLen, const unsigned char *source, unsigned long *sourceLen);
int CE_SettingGetText(const char *key, char *out, int n);
}
const char *PortUserDir();

namespace
{
const int kMaxMods = 16, kMaxFilesPerMod = 4096, kMaxFilesTotal = 8192, kMaxPathParts = 12, kMaxLogLines = 300;
const unsigned long long kMaxFile = 64ull << 20, kMaxTotal = 1ull << 30, kMaxWorld = 4ull << 20;
// An added level archive (data/resources/NNN_res_PC.aya): at most 8 MiB packed, 32 MiB unpacked and 2048 top-level chunks.
// The largest of the game's 66 level archives is 2.1 MB packed, 4.8 MB unpacked and 399 chunks, with a 142 KB world body.
const unsigned long long kMaxArchive = 8ull << 20, kMaxArchiveUnpacked = 32ull << 20;
const int kMaxLevelChunks = 2048;
// The levels a mod may add an archive for: the multiplayer range, as CGame::IsMultiplayer takes it. The host tests build
// with PORT_MODS_TEST_ANY_LEVEL to run the archive checks over every level archive the game ships.
#ifdef PORT_MODS_TEST_ANY_LEVEL
const int kArchiveFirst = 0, kArchiveLast = 999;
#else
const int kArchiveFirst = 850, kArchiveLast = 899;
#endif
const size_t kMaxManifest = 256 << 10, kMaxPath = 240, kMaxPart = 128;
// The game's packed-file reader (src/DXMemBuffer.cpp): 1 MiB read buffers, a 0x102927-byte block buffer; LoadFrame's
// 0x500000-byte scratch buffer for one texture file (a file that fills it is a fatal error).
const unsigned long kBlock = 1ul << 20, kPackBuffer = 0x102927, kTextureScratch = 0x500000;
const unsigned kMaxTextureSide = 4096;

#ifdef _WIN32
struct Identity
{
	unsigned long long volume;	// FILE_ID_INFO: the volume serial and the 128-bit file id name the file itself
	unsigned char fileId[16];
	unsigned long long size;
	long long written, changed;	// FILE_BASIC_INFO: an ordinary write moves both. A change DETECTOR, not a seal: on NTFS an opener with FILE_WRITE_ATTRIBUTES can set both back, so what
							// protects the bytes is the hash taken at every open and the private snapshot the game then reads (OpenVerified)
};
#else
struct Identity
{
	dev_t dev;
	ino_t ino;
	off_t size;
	struct timespec mtime, ctime;
};
#endif

struct Mod
{
	std::string id, name, version, author;
	int files = 0;
#ifdef _WIN32
	HANDLE root = INVALID_HANDLE_VALUE;	// <user folder>\mods\<id>, held open (no delete share) for the life of the process
#else
	int root = -1;
#endif
};

struct File
{
	std::string key;	// lowercase, '/'-separated: the lookup name
	std::string rel;	// as the manifest spells it: the name inside the mod's folder
	int mod;
	Identity id;
	unsigned long long size;
	unsigned char sha[32];
	unsigned char refused;	// set once a run-time mismatch was logged
	unsigned char served;	// set once the first read was logged
	unsigned char added;	// a world file or level archive the game does not have (the log says "adds", not "replaces")
	unsigned char archive;	// the added file is a level archive (a refusal then has no archive to fall back to)
};

std::vector<Mod> gMods;
std::vector<File> gFiles;	// sorted by key, one per key
std::atomic<int> gState{0};	// 0 not loaded, 1 loading, 2 loaded
const char *gFence;
int gLogLines;
// PortModsCheck (mods.h): every line of the loader goes to standard output without a limit, and a refusal does not end the checks of the mod.
bool gCheck;
const std::vector<std::string> *gCheckIds;	// the ids named on the command line, or NULL: ce.ini's mods= line
int gCheckRefusals;	// refusal lines, and below the mods (or ids) they refused
int gCheckRefusedMods;
const char kReported[] = "";	// LoadMod's answer when the check mode has printed the mod's refusals itself

void Log(const char *format, ...)
{
	char line[768];
	va_list ap;
	if (gCheck)
	{
		va_start(ap, format);
		vsnprintf(line, sizeof line, format, ap);
		va_end(ap);
		fputs(line, stdout);
		fputc('\n', stdout);
		return;
	}
	int count = __atomic_add_fetch(&gLogLines, 1, __ATOMIC_RELAXED);
	if (count > kMaxLogLines)
		return;
	if (count == kMaxLogLines)
	{
		fprintf(stderr, PORT_LOG_PREFIX "mods: (further mod messages are not shown)\n");
		return;
	}
	va_start(ap, format);
	vsnprintf(line, sizeof line, format, ap);
	va_end(ap);
	fprintf(stderr, PORT_LOG_PREFIX "mods: %s\n", line);
}

// One refusal of the mod `shown`: the line the game's log has, and in the check mode one more to the exit status.
void LogRefusal(const std::string &shown, const char *why)
{
	gCheckRefusals++;
	Log("refused %s: %s", shown.c_str(), why);
}

// Text from mod.json for the log: printable ASCII only, at most n characters.
std::string Clean(const std::string &s, size_t n = 64)
{
	std::string out;
	for (unsigned char c : s)
	{
		if (out.size() >= n)
		{
			out += "...";
			break;
		}
		out += (c >= 32 && c < 127) ? (char)c : '?';
	}
	return out;
}

// ---- the fences ------------------------------------------------------------------------------------------------------
// Every run whose result is compared, replayed or shared reads the game's own files: the deterministic clocks, input
// tapes, probes, recordings, the pilots and network sessions (until both peers can agree on the same mods).
// BEA_PORT_CE_LAYER (screenshots of the CE layer during a replay) does not lift this.
const char *Fence()
{
	static const char *const names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
										"BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN", "BEA_NET_HANDOFF"};
	for (const char *n : names)
	{
		const char *v = getenv(n);
		if (v && *v)
			return n;
	}
	return NULL;
}

// THE ONE SWITCH: why this build's loader does not run, or NULL. A build can switch the mod layer off (the host tests build that branch to check that it refuses and
// touches nothing). The Windows loader is written and runs under Wine, but nothing that only NTFS can show (junction and symlink refusal, a folder swapped after startup, case, short
// names, antivirus) has run, so neither a `mods=` line nor --check-mods may use it. The gate's mods cases build with PORT_MODS_WINDOWS_ON (port/build.py --mods-windows-test) to prove
// those properties; the commit that enables mods on Windows deletes the second branch.
const char *PlatformOff()
{
#if defined(PORT_MODS_PLATFORM_OFF)
	return "off in this build";
#elif defined(_WIN32) && !defined(PORT_MODS_WINDOWS_ON)
	return "off on Windows until the loader's checks on NTFS have passed";
#else
	return NULL;
#endif
}

// ---- names -----------------------------------------------------------------------------------------------------------
bool ValidId(const std::string &id)
{
	if (id.empty() || id.size() > 48)
		return false;
	for (char c : id)
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return false;
	return true;
}

bool DeviceName(const std::string &part)
{
	std::string stem = part.substr(0, part.find('.'));
	while (!stem.empty() && stem.back() == ' ')
		stem.pop_back();
	for (char &c : stem)
		c = (char)toupper((unsigned char)c);
	static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
	for (const char *n : names)
		if (stem == n)
			return true;
	return stem.size() == 4 && (!stem.compare(0, 3, "COM") || !stem.compare(0, 3, "LPT")) && stem[3] >= '0' && stem[3] <= '9';
}

// NULL when p is an acceptable replacement path (see the top of the file), else the reason.
const char *CheckPath(const std::string &p, std::vector<std::string> *parts)
{
	if (p.empty() || p.size() > kMaxPath)
		return "an empty or over-long path";
	parts->clear();
	size_t start = 0;
	while (true)
	{
		size_t slash = p.find('/', start);
		std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (part.empty())
			return "an absolute path or an empty path component";
		if (part == "." || part == "..")
			return "a '.' or '..' component";
		if (part.size() > kMaxPart)
			return "an over-long path component";
		for (unsigned char c : part)
			if (c < 32 || c >= 127 || strchr("\\:*?\"<>|", c))
				return "a character that is not allowed in a path (\\ : * ? \" < > | or a control or non-ASCII byte)";
		if (part[0] == ' ' || part.back() == ' ' || part.back() == '.')
			return "a component that starts with a space or ends with a space or a dot";
		for (size_t i = 0; i + 1 < part.size(); i++)
			if (part[i] == '~' && part[i + 1] >= '0' && part[i + 1] <= '9')
				return "a component that looks like a Windows short name (a '~' and a digit)";
		if (DeviceName(part))
			return "a Windows device name";
		parts->push_back(part);
		if ((int)parts->size() > kMaxPathParts)
			return "too many path components";
		if (slash == std::string::npos)
			break;
		start = slash + 1;
	}
	std::string first = (*parts)[0];
	for (char &c : first)
		c = (char)tolower((unsigned char)c);
	if (first != "data")
		return "a path outside data/";
	return NULL;
}

// data/worlds/worldNNN.wrd (key is lowercase): the first of the two names a mod may add to the game's files. CWorld::Load
// reads a world body from it, and a loose copy the game lacks is how a mod supplies a level's world.
bool WorldFile(const std::string &key)
{
	static const char head[] = "data/worlds/world";
	const size_t n = sizeof head - 1;
	if (key.size() != n + 3 + 4 || key.compare(0, n, head) != 0 || key.compare(n + 3, 4, ".wrd") != 0)
		return false;
	for (size_t i = 0; i < 3; i++)
		if (key[n + i] < '0' || key[n + i] > '9')
			return false;
	return true;
}

// data/resources/NNN_res_PC.aya (key is lowercase), NNN three digits in the multiplayer range: the second name a mod may
// add, for a level the game has no archive for (the caller tests that: an archive the game has is replaced as any file
// is). CResourceAccumulator::GetFileName builds this name for level NNN and ReadResources opens it; with no archive it
// returns without loading anything, so a level with none has no textures, meshes or terrain.
bool LevelArchiveName(const std::string &key, int *level)
{
	static const char head[] = "data/resources/", tail[] = "_res_pc.aya";
	const size_t h = sizeof head - 1, t = sizeof tail - 1;
	if (key.size() != h + 3 + t || key.compare(0, h, head) != 0 || key.compare(h + 3, t, tail) != 0)
		return false;
	int n = 0;
	for (size_t i = 0; i < 3; i++)
	{
		if (key[h + i] < '0' || key[h + i] > '9')
			return false;
		n = n * 10 + (key[h + i] - '0');
	}
	*level = n;
	return n >= kArchiveFirst && n <= kArchiveLast;
}

std::string Lower(std::string s)
{
	for (char &c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

// The lookup key for a name the game opens, as the path layer splits it: either separator, "." and empty parts drop,
// ".." removes the previous part (never climbing above the root). Absolute and drive paths are never replaced.
bool GameKey(const char *name, std::string *key)
{
	if (!name || !*name || name[0] == '/' || name[0] == '\\' || (isalpha((unsigned char)name[0]) && name[1] == ':'))
		return false;
	std::vector<std::string> parts;
	std::string cur;
	for (const char *p = name;; p++)
	{
		if (*p == '\\' || *p == '/' || !*p)
		{
			if (!cur.empty() && cur != ".")
			{
				if (cur == "..")
				{
					if (!parts.empty())
						parts.pop_back();
				}
				else
					parts.push_back(cur);
			}
			cur.clear();
			if (!*p)
				break;
		}
		else
		{
			cur += (char)tolower((unsigned char)*p);
			if (cur.size() > kMaxPart)
				return false;
		}
	}
	if (parts.empty())
		return false;
	key->clear();
	for (size_t i = 0; i < parts.size(); i++)
		*key += (i ? "/" : "") + parts[i];
	return key->size() <= kMaxPath;
}

// ---- mod.json --------------------------------------------------------------------------------------------------------
struct Json
{
	enum Type { NUL, BOOL, INT, STR, ARR, OBJ } type = NUL;
	bool negative = false;
	unsigned long long n = 0;
	std::string s;
	std::vector<Json> items;
	std::vector<std::pair<std::string, Json>> fields;
	const Json *Get(const char *k) const
	{
		for (const auto &f : fields)
			if (f.first == k)
				return &f.second;
		return NULL;
	}
};

// Strict JSON: integers only (the schema has no other numbers), bounded depth and size, repeated keys refused.
struct Parser
{
	const char *p, *end;
	const char *error = NULL;
	int depth = 0;
	size_t nodes = 0;

	bool Fail(const char *why)
	{
		if (!error)
			error = why;
		return false;
	}
	void Space()
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;
	}
	bool String(std::string *out)
	{
		if (p >= end || *p != '"')
			return Fail("a string was expected");
		p++;
		while (true)
		{
			if (p >= end)
				return Fail("an unterminated string");
			unsigned char c = (unsigned char)*p++;
			if (c == '"')
				return true;
			if (c < 32)
				return Fail("a control character in a string");
			if (c != '\\')
			{
				*out += (char)c;
				continue;
			}
			if (p >= end)
				return Fail("an unterminated string");
			c = (unsigned char)*p++;
			switch (c)
			{
			case '"': case '\\': case '/': *out += (char)c; break;
			case 'b': *out += '\b'; break;
			case 'f': *out += '\f'; break;
			case 'n': *out += '\n'; break;
			case 'r': *out += '\r'; break;
			case 't': *out += '\t'; break;
			case 'u':
			{
				if (end - p < 4)
					return Fail("a short \\u escape");
				unsigned v = 0;
				for (int i = 0; i < 4; i++)
				{
					char h = *p++;
					int digit = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10
										: h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
					if (digit < 0)
						return Fail("a bad \\u escape");
					v = v * 16 + (unsigned)digit;
				}
				if (v == 0)
					return Fail("a NUL character in a string");
				if (v >= 0x80)
					*out += '?';	// non-ASCII text is kept only as a placeholder (names are logged as ASCII)
				else
					*out += (char)v;
				break;
			}
			default: return Fail("an unknown escape in a string");
			}
			if (out->size() > 4096)
				return Fail("an over-long string");
		}
	}
	bool Value(Json *v)
	{
		if (++nodes > 100000)
			return Fail("too many values");
		Space();
		if (p >= end)
			return Fail("a value was expected");
		if (*p == '{' || *p == '[')
		{
			if (++depth > 8)
				return Fail("nesting deeper than 8");
			bool object = *p++ == '{';
			v->type = object ? Json::OBJ : Json::ARR;
			Space();
			if (p < end && *p == (object ? '}' : ']'))
			{
				p++;
				depth--;
				return true;
			}
			while (true)
			{
				Space();
				if (object)
				{
					std::string k;
					if (!String(&k))
						return false;
					for (const auto &f : v->fields)
						if (f.first == k)
							return Fail("a repeated key");
					Space();
					if (p >= end || *p++ != ':')
						return Fail("':' was expected");
					v->fields.emplace_back(k, Json());
					if (!Value(&v->fields.back().second))
						return false;
				}
				else
				{
					v->items.emplace_back();
					if (!Value(&v->items.back()))
						return false;
				}
				Space();
				if (p < end && *p == ',')
				{
					p++;
					continue;
				}
				if (p < end && *p == (object ? '}' : ']'))
				{
					p++;
					depth--;
					return true;
				}
				return Fail("',' or a closing bracket was expected");
			}
		}
		if (*p == '"')
		{
			v->type = Json::STR;
			return String(&v->s);
		}
		if (*p == '-' || (*p >= '0' && *p <= '9'))
		{
			v->type = Json::INT;
			if (*p == '-')
				v->negative = true, p++;
			if (p >= end || *p < '0' || *p > '9')
				return Fail("a bad number");
			if (*p == '0' && p + 1 < end && p[1] >= '0' && p[1] <= '9')
				return Fail("a number with a leading zero");
			while (p < end && *p >= '0' && *p <= '9')
			{
				if (v->n > (~0ull - 9) / 10)
					return Fail("a number too large");
				v->n = v->n * 10 + (unsigned)(*p++ - '0');
			}
			if (p < end && (*p == '.' || *p == 'e' || *p == 'E'))
				return Fail("a number that is not an integer");
			return true;
		}
		static const struct { const char *text; Json::Type type; bool b; } words[] = {
			{"true", Json::BOOL, true}, {"false", Json::BOOL, false}, {"null", Json::NUL, false}};
		for (const auto &w : words)
		{
			size_t n = strlen(w.text);
			if ((size_t)(end - p) >= n && !memcmp(p, w.text, n))
			{
				p += n;
				v->type = w.type;
				v->n = w.b;
				return true;
			}
		}
		return Fail("an unknown value");
	}
};

bool VersionParts(const std::string &s, unsigned out[4])
{
	memset(out, 0, sizeof(unsigned) * 4);
	int i = 0;
	size_t pos = 0;
	while (true)
	{
		if (i == 4 || pos >= s.size() || !isdigit((unsigned char)s[pos]))
			return false;
		unsigned v = 0;
		while (pos < s.size() && isdigit((unsigned char)s[pos]))
		{
			v = v * 10 + (unsigned)(s[pos++] - '0');
			if (v > 99999)
				return false;
		}
		out[i++] = v;
		if (pos == s.size())
			return true;
		if (s[pos++] != '.')
			return false;
	}
}

int VersionCompare(const unsigned a[4], const unsigned b[4])
{
	for (int i = 0; i < 4; i++)
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	return 0;
}

// The running CE version's numbers, or false on a development build (private builds, --ce-version dev).
bool RunningVersion(unsigned out[4])
{
#ifdef CE_VERSION
	std::string v = CE_VERSION;
	size_t cut = v.find_first_not_of("0123456789.");
	return VersionParts(v.substr(0, cut), out);
#else
	(void)out;
	return false;
#endif
}

struct Entry
{
	std::string path;
	unsigned long long size;
	unsigned char sha[32];
};

bool Hex(const std::string &s, unsigned char out[32])
{
	if (s.size() != 64)
		return false;
	for (int i = 0; i < 64; i++)
	{
		char c = s[i];
		int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
		if (v < 0)
			return false;
		if (i & 1)
			out[i / 2] |= (unsigned char)v;
		else
			out[i / 2] = (unsigned char)(v << 4);
	}
	return true;
}

// NULL and the mod's fields, or the reason the manifest is refused.
const char *ReadManifest(const char *text, size_t size, const std::string &id, Mod *mod, std::vector<Entry> *entries)
{
	Parser parser{text, text + size};
	Json root;
	if (!parser.Value(&root))
		return parser.error;
	parser.Space();
	if (parser.p != parser.end)
		return "text after the manifest's object";
	if (root.type != Json::OBJ)
		return "the manifest is not an object";
	static const char *const known[] = {"schema", "id", "name", "version", "author", "description", "ce_min", "ce_max",
										"files"};
	// The game stops at a mod's first refusal; the check mode prints it and reads on, for as long as the text can be read at all.
#define REFUSE(why) do { if (!gCheck) return (why); LogRefusal(id, (why)); } while (0)
	for (const auto &f : root.fields)
		if (std::find_if(std::begin(known), std::end(known), [&](const char *k) { return f.first == k; }) == std::end(known))
		{
			REFUSE("an unknown key in the manifest (schema 1 knows schema, id, name, version, author, description, ce_min, "
				   "ce_max and files)");
			break;
		}
	const Json *schema = root.Get("schema");
	if (!schema || schema->type != Json::INT || schema->negative || schema->n != 1)
		REFUSE("schema is not 1");
	const Json *jid = root.Get("id");
	if (!jid || jid->type != Json::STR || jid->s != id)
		REFUSE("id is not the mod folder's name");
	struct { const char *key; std::string *out; size_t min, max; } texts[] = {
		{"name", &mod->name, 1, 64}, {"version", &mod->version, 1, 32}, {"author", &mod->author, 0, 64},
		{"description", NULL, 0, 1024}};
	for (const auto &t : texts)
	{
		const Json *v = root.Get(t.key);
		if (!v && t.min == 0)
			continue;
		if (!v || v->type != Json::STR || v->s.size() < t.min || v->s.size() > t.max)
		{
			static char why[96];
			snprintf(why, sizeof why, "%s is missing, not a string, or not %u to %u characters", t.key, (unsigned)t.min,
					 (unsigned)t.max);
			REFUSE(why);
			continue;
		}
		if (t.out)
			*t.out = v->s;
	}
	unsigned running[4];
	bool released = RunningVersion(running);
	for (const char *k : {"ce_min", "ce_max"})
	{
		const Json *v = root.Get(k);
		if (!v)
			continue;
		unsigned want[4];
		if (v->type != Json::STR || !VersionParts(v->s, want))
		{
			REFUSE("ce_min or ce_max is not a version (numbers separated by dots, such as 0.1.8)");
			continue;
		}
		if (released)
		{
			int order = VersionCompare(running, want);
			if (strcmp(k, "ce_min") == 0 ? order < 0 : order > 0)
				REFUSE("this version of Onslaught CE is outside the mod's ce_min..ce_max");
		}
	}
	const Json *files = root.Get("files");
	if (!files || files->type != Json::ARR || files->items.empty())
	{
		REFUSE("files is missing, not a list, or empty");
		return NULL;
	}
	if ((int)files->items.size() > kMaxFilesPerMod)
	{
		REFUSE("more than 4096 files");
		return NULL;
	}
	for (const Json &f : files->items)
	{
		if (f.type != Json::OBJ)
		{
			REFUSE("a files entry is not an object");
			continue;
		}
		bool known_keys = true;
		for (const auto &k : f.fields)
			if (k.first != "path" && k.first != "size" && k.first != "sha256")
				known_keys = false;
		if (!known_keys)
		{
			REFUSE("an unknown key in a files entry (path, size and sha256 only)");
			continue;
		}
		const Json *path = f.Get("path"), *sz = f.Get("size"), *sha = f.Get("sha256");
		if (!path || path->type != Json::STR || !sz || sz->type != Json::INT || sz->negative || !sha || sha->type != Json::STR)
		{
			REFUSE("a files entry lacks path (text), size (a whole number) or sha256 (text)");
			continue;
		}
		Entry e;
		e.path = path->s;
		e.size = sz->n;
		if (!Hex(sha->s, e.sha))
		{
			REFUSE("a sha256 that is not 64 lowercase hexadecimal digits");
			continue;
		}
		if (e.size > kMaxFile)
		{
			REFUSE("a file larger than 64 MiB");
			continue;
		}
		entries->push_back(e);
	}
#undef REFUSE
	return NULL;
}

// ---- file checks -----------------------------------------------------------------------------------------------------
unsigned Le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

// NULL when a packed .aya file's blocks are what CDXMemBuffer::ReadBlock can hold: each [u32 length][zlib data] block
// fits the game's block buffer and unpacks to at most 1 MiB, every block but the last unpacks to exactly 1 MiB (the
// reader appends blocks into a 1 MiB buffer until it is full), and no bytes follow the last block. A texture must also
// unpack to a DDS image below LoadFrame's 5 MiB limit, at most 4096 texels a side. With keep set (an added level archive)
// the unpacked bytes are collected there, at most kMaxArchiveUnpacked of them.
const char *CheckPacked(const unsigned char *d, size_t n, bool texture, std::vector<unsigned char> *keep = NULL)
{
	std::vector<unsigned char> out(kPackBuffer);
	unsigned char head[128] = {0};
	size_t pos = 0, total = 0, headLen = 0;
	bool shortSeen = false;
	int blocks = 0;
	while (pos < n)
	{
		if (n - pos < 4)
			return "a packed file with a cut block header";
		unsigned long packed = Le32(d + pos);
		pos += 4;
		if (packed == 0 || packed > kPackBuffer)
			return "a packed block larger than the game's block buffer";
		if (n - pos < packed)
			return "a packed file with a cut block";
		if (shortSeen)
			return "a packed block under 1 MiB before the last block";
		unsigned long len = kPackBuffer;
		unsigned long consumed = packed;
		if (uncompress2(out.data(), &len, d + pos, &consumed) != 0 || consumed != packed)
			return "a packed block that does not unpack";
		if (len > kBlock)
			return "a packed block that unpacks to more than 1 MiB";
		if (len < kBlock)
			shortSeen = true;
		if (headLen < sizeof head)
		{
			size_t take = std::min(sizeof head - headLen, (size_t)len);
			memcpy(head + headLen, out.data(), take);
			headLen += take;
		}
		total += len;
		if (keep)
		{
			if (total > kMaxArchiveUnpacked)
				return "an added level archive that unpacks to more than 32 MiB";
			keep->insert(keep->end(), out.data(), out.data() + len);
		}
		pos += packed;
		blocks++;
		if (total > kMaxFile)
			return "a packed file that unpacks to more than 64 MiB";
	}
	if (!blocks)
		return "an empty packed file";
	if (texture)
	{
		if (total >= kTextureScratch)
			return "a texture that unpacks to 5 MiB or more (the game's texture buffer)";
		if (headLen < 128 || memcmp(head, "DDS ", 4) != 0 || Le32(head + 4) != 124)
			return "a texture that is not a DDS image";
		unsigned h = Le32(head + 12), w = Le32(head + 16);
		if (!w || !h || w > kMaxTextureSide || h > kMaxTextureSide)
			return "a texture over 4096 texels a side";
	}
	return NULL;
}

// ---- an inline mesh -----------------------------------------------------------------------------------------------------
// A MESH chunk of an added level archive whose reference carries no name holds the mesh's data after its 325 bytes: CMesh::Deserialize
// reads it from the archive's own reader when the name's first byte is zero. Its readers take their amounts from the counts in the records
// they have just read and never from a chunk's declared size, so a stream is walked here the way they read it, and refused where they
// would read a count's worth of something that is not there (the grammar is the preservation repository's cmsh-fields.md). One profile is
// admitted, built from what the shipped meshes have: no cutscene sequences, none of the older vertex and triangle arrays, 36-byte vertices
// in at most 12 pieces, skeletal parts of at most 19 bones whose 48-byte vertices name those bones, camera parts with a field of view for
// each hand frame and no vertices, parts numbered by their place with each parent before its part and each child after it, no part listed
// as a child twice, a PRNT on exactly the parts some part lists, naming that part, no part more than 32 parts below one that no part lists,
// each part's cache as CMeshPart::BuildCache leaves it (a cached-frame count of 1 or the virtual-frame count, a CPOS exactly when the position
// is not static and a CORI exactly when the orientation is not, and a static flag only on a part with a parent), and animation modes whose
// records are as the shipped ones are (CheckOneMesh); meshes chained to it are admitted, up to 16 in all, each framed as the game's own are
// and with as many animation modes as the first (CheckInlineMesh). It does not hold a mesh to everything the shipped ones share, and admits
// these, which no shipped mesh has: a bone that names a part other than a null part (the skeletal part itself, say, or a camera or reference
// part), a skeletal part whose bones name one part twice, more than one skeletal part in a mesh, a null, emitter or reference part with
// vertices, a camera part with a collision grid, an emitter whose part index names a part that is not an emitter part, more than one part
// that no part lists, a cached-frame count other than the one BuildCache gives (1 where the part or a part above it has more than one hand
// frame, or the virtual-frame count where none has), a static flag other than 1, a static flag on a part with more than one hand frame, a
// static position on a part whose orientation is not static, and a static part whose cached-frame count is not that of the part whose cache
// it reads.
// Every count, size and index is bounded, so a stream that passes cannot make the readers allocate, copy or index beyond what it holds (the
// cached transforms included, for a frame of 0 or more), and the frame an animation mode gives the drawing is never below 0 nor above any
// part's virtual-frame count. A motion controller of the buggy, the mech or the tentacle replaces that frame with its own (AdjustFrame), and
// the dropship's picks its door frames by mode name: these rules do not bound those frames, and whether an added level can give an inline
// mesh to a thing with such a controller was not traced. What the matrices, the positions, the fields of view or the texture and mode names
// say is not looked at. The shipped meshes hold up to 30 textures and 21 animation modes, in tables the readers allocate to their counts: the
// caps are there to bound what a file can ask for.
constexpr unsigned kMeshMaxBytes = 16u << 20, kMeshMaxTextures = 32, kMeshMaxParts = 512, kMeshMaxAnimModes = 32, kMeshMaxEmitters = 512,
				   kMeshMaxVertices = 100000, kMeshMaxIndices = 600000, kMeshMaxGroups = 1024, kMeshMaxFrames = 65535, kMeshMaxFrameSets = 2, kMeshMaxCells = 16384,
				   kMeshMaxGridTriangles = 32767, kMeshMaxGridIndices = 1u << 20, kMeshMaxGridVertices = 32767, kMeshMaxBones = 19, kMeshMaxDepth = 32, kMeshMaxChain = 16;

// How a part of the mesh is linked: the part that lists it as a child, plus 1 (0 when no part lists it), and how many parts are above it.
struct MeshLink
{
	unsigned short listedBy;
	unsigned char depth;
};

// A run of chunk headers inside [0, end) of b: Take reads the next one, which must be `tag` and fit, and leaves p at its payload.
struct MeshCursor
{
	const unsigned char *b;
	size_t at, end;
	bool Take(const char *tag, const unsigned char *&p, size_t &size)
	{
		if (end - at < 8 || memcmp(b + at, tag, 4))
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

// The bounding box of a part or a mesh: a BBOX wrapper (48 bytes) around one 40-byte BBOX.
bool TakeMeshBox(MeshCursor &m)
{
	const unsigned char *p, *q;
	size_t size, inner;
	if (!m.Take("BBOX", p, size) || size != 48)
		return false;
	MeshCursor w{p, 0, size};
	return w.Take("BBOX", q, inner) && inner == 40 && w.Done();
}

// A part's collision grid, the payload of its PBKT: CPBT, SIZS, a PBCS for each cell, PLQT, a PBFK around a PBKE for each triangle, PBPN and
// a PBPP for each vertex. The lists share SIZS's arrays, so the cells may not hold more than it declares; the triangles name vertices and
// the lists name triangles, so both are held to the counts PLQT and PBPN give (CPolyBucket::Load allocates the triangles from PLQT and
// reads the CPBT record, whose own triangle count the shipped grids and some PS2-converted ones disagree with, whole and never again).
// Only the triangles a list names are read through their vertices: the PS2 exporter left the unlisted tail of some grids as it found it.
const char *CheckMeshGrid(const unsigned char *grid, size_t gridSize)
{
	MeshCursor g{grid, 0, gridSize};
	const unsigned char *p;
	size_t size;
	if (!g.Take("CPBT", p, size) || size != 184)
		return "an inline mesh whose collision grid does not start with a 184-byte CPBT";
	const unsigned width = Le32(p + 0x68), height = Le32(p + 0x6C);
	if (!width || !height || width > kMeshMaxCells || height > kMeshMaxCells || (unsigned long long)width * height > kMeshMaxCells)
		return "an inline mesh whose collision grid has a size out of bounds";
	if (!g.Take("SIZS", p, size) || size != 8)
		return "an inline mesh whose collision grid has no 8-byte SIZS after its CPBT";
	const unsigned lists = Le32(p), indices = Le32(p + 4);
	if (lists > width * height || indices > kMeshMaxGridIndices)
		return "an inline mesh whose collision grid declares more lists or indices than it can hold";
	unsigned seenLists = 0, seenIndices = 0, listed = 0;
	std::vector<unsigned char> used(kMeshMaxGridTriangles, 0);
	for (unsigned cell = 0; cell < width * height; cell++)
	{
		if (!g.Take("PBCS", p, size) || size < 4)
			return "an inline mesh whose collision grid has fewer PBCS cells than its width and height";
		const unsigned present = Le32(p);
		if (present > 1)
			return "an inline mesh whose collision grid has a cell that is neither present nor absent";
		if (!present)
		{
			if (size != 4)
				return "an inline mesh whose collision grid has an absent cell with data";
			continue;
		}
		MeshCursor cellCursor{p + 4, 0, size - 4};
		const unsigned char *e, *s;
		size_t eSize, sSize;
		if (!cellCursor.Take("PBET", e, eSize) || !cellCursor.Done())
			return "an inline mesh whose collision grid has a present cell that is not one PBET";
		MeshCursor listCursor{e, 0, eSize};
		if (!listCursor.Take("PBSQ", s, sSize) || !listCursor.Done() || sSize < 4)
			return "an inline mesh whose collision grid has a PBET that is not one PBSQ";
		const unsigned count = Le32(s);
		if (count > kMeshMaxGridIndices || sSize != 4 + 2 * (size_t)count)
			return "an inline mesh whose collision grid has a list whose size is not its count";
		for (unsigned i = 0; i < count; i++)
		{
			const unsigned triangle = s[4 + 2 * i] | s[5 + 2 * i] << 8;
			if (triangle >= kMeshMaxGridTriangles)
				return "an inline mesh whose collision grid lists a triangle past 32767";
			used[triangle] = 1;
			listed = std::max(listed, triangle + 1);
		}
		seenLists++;
		seenIndices += count;
		if (seenLists > lists || seenIndices > indices)
			return "an inline mesh whose collision grid holds more lists or indices than its SIZS declares";
	}
	if (!g.Take("PLQT", p, size) || size != 4)
		return "an inline mesh whose collision grid has no 4-byte PLQT after its cells";
	const unsigned triangles = Le32(p);
	if (triangles > kMeshMaxGridTriangles || listed > triangles)
		return "an inline mesh whose collision grid lists a triangle it does not have, or has more than 32767";
	if (!g.Take("PBFK", p, size))
		return "an inline mesh whose collision grid has no PBFK";
	MeshCursor tri{p, 0, size};
	unsigned maxVertex = 0;
	for (unsigned i = 0; i < triangles; i++)
	{
		const unsigned char *k;
		size_t ksize;
		if (!tri.Take("PBKE", k, ksize) || ksize != 12)
			return "an inline mesh whose collision grid has fewer 12-byte PBKE triangles than its count";
		for (int v = 0; v < 3 && used[i]; v++)
			maxVertex = std::max(maxVertex, (unsigned)(k[4 + 2 * v] | k[5 + 2 * v] << 8) + 1);
	}
	if (!tri.Done())
		return "an inline mesh whose collision grid has bytes in its PBFK after its triangles";
	if (!g.Take("PBPN", p, size) || size != 4)
		return "an inline mesh whose collision grid has no 4-byte PBPN";
	const unsigned vertices = Le32(p);
	if (vertices > kMeshMaxGridVertices || maxVertex > vertices)
		return "an inline mesh whose collision grid has a listed triangle that names a vertex it does not have";
	for (unsigned i = 0; i < vertices; i++)
		if (!g.Take("PBPP", p, size) || size != 6)
			return "an inline mesh whose collision grid has fewer 6-byte PBPP vertices than its count";
	return g.Done() ? NULL : "an inline mesh whose collision grid has bytes after its last vertex";
}

// A part (the payload of an MESP): the chunks CMeshPart's reader takes, in its order, each there as the part record says. `type` and `target` come back for
// the mesh's own check of what a reference part names, which may come later in the stream; `links` holds, for each part of the mesh, the part that lists it
// as a child and its depth below the top (a part comes after the part that lists it, so its own entry is final when it is walked); `frames` is lowered to
// the part's virtual-frame count, for the chain's check of the animation modes.
const char *CheckMeshPart(const unsigned char *part, size_t partSize, unsigned index, unsigned parts, unsigned textures, unsigned &vertices, unsigned &indices,
						  unsigned &groups, unsigned char &type, unsigned short &target, MeshLink *links, unsigned &frames)
{
	MeshCursor m{part, 0, partSize};
	const unsigned char *p;
	size_t size;
	if (!m.Take("CMSP", p, size) || size != 316)
		return "an inline mesh part that does not start with a 316-byte CMSP record";
	const unsigned partType = Le32(p + 0x8C), children = Le32(p + 0x90), vframes = Le32(p + 0xB8), hframes = Le32(p + 0xBC), bones = Le32(p + 0xC0),
				   cached = Le32(p + 0x118);
	if (partType < 1 || partType > 6)
		return "an inline mesh part whose type is not one of the game's six";
	// The word before the type is the part's own number. The reader keeps it as the record has it (CMesh::Deserialize does not call InitFromMem or
	// OptimiseParts, which set the numbers), and the pose builder, the mesh controllers and the destructable-segment controller index their tables of the
	// mesh's parts with it and never compare it with the part count: as in every shipped mesh, it is the part's place in the mesh.
	if (Le32(p + 0x88) != index)
		return "an inline mesh part whose part number is not its place in the mesh";
	type = (unsigned char)partType;
	target = 0;
	const bool skeletal = partType == 3, camera = partType == 4, fov = Le32(p + 0xCC) != 0;
	if (Le32(p + 0x9C) || Le32(p + 0xA0) || Le32(p + 0xA4) || Le32(p + 0xA8) || Le32(p + 0xAC) || Le32(p + 0xB0) || Le32(p + 0xD4) || Le32(p + 0xD8))
		return "an inline mesh part with a chain, older-format vertices or triangles, or bone weights or slots (none is admitted)";
	// The reader allocates the bones, and so replaces the record's pointer (stale in every shipped skeletal part), only when the count is set, and the renderer
	// builds a palette matrix for each bone on its stack: a skeletal part has at most the 19 a shipped part has, which is within what the renderer takes. On
	// any other part the pointer would be kept and freed with the part. Only a camera has a field of view (the movie camera indexes it without a test; it finds
	// its camera through a cutscene sequence, which an added mesh may not have).
	if (skeletal && (!bones || bones > kMeshMaxBones))
		return "an inline mesh skeletal part with no bones or more than 19";
	if (!skeletal && (bones || Le32(p + 0xD0)))
		return "an inline mesh part that is not skeletal with bones or a bones pointer";
	if (fov != camera)
		return "an inline mesh part with a field of view that is not a camera, or a camera without one";
	// CMeshPart::Deserialize reads the record over the live part and keeps every pointer it does not allocate itself, and WipeVertexData frees the
	// frame maps it finds: the reader allocates them only for a part with virtual and hand frames, so a part without them is refused, and the frame-set
	// count (InitMemory allocates and loops that many times) is held to what the shipped parts have.
	if (Le32(p + 0xB4) > kMeshMaxFrameSets)
		return "an inline mesh part with more position sets than the shipped parts have (its frame-set count)";
	if (!vframes)
		return "an inline mesh part with no virtual frames (every shipped part has some; the reader would keep the record's pointer to their map)";
	if (!hframes)
		return "an inline mesh part with no hand frames (every shipped part has some; the reader would keep the record's pointers to their tables)";
	if (children > parts || vframes > kMeshMaxFrames || hframes > kMeshMaxFrames || cached > kMeshMaxFrames)
		return "an inline mesh part with a child, frame or cache count out of bounds";
	frames = std::min(frames, vframes);
	const bool parent = Le32(p + 0x98) != 0, grid = Le32(p + 0x100) != 0, cachedPositions = Le32(p + 0x104) != 0, cachedOrientations = Le32(p + 0x108) != 0,
			   orientations = Le32(p + 0x10C) != 0, positions = Le32(p + 0xC8) != 0, reference = Le32(p + 0x124) != 0, positionStatic = Le32(p + 0x11C) != 0,
			   orientationStatic = Le32(p + 0x120) != 0;
	// Every part's pose is read through its HORI and HPOS, which the reader leaves null without these words; a part with a REFR is a reference part and the
	// other way round, as in every shipped mesh (the mesh's check holds what it names to a normal part).
	if (!orientations || !positions)
		return "an inline mesh part without its HORI and HPOS (every shipped part has both)";
	if (reference != (partType == 6))
		return "an inline mesh part with a REFR that is not a reference part, or a reference part without one";
	// A part's pose is built up its parents and the drawing goes down its children, recursively, so the file may not make a cycle of either: as in every
	// shipped mesh a parent comes before the part and a child after it, which also leaves no room for a part to be its own. The drawing and the pose builder
	// also go down every entry of a child list without marking a part as done, so a part listed twice, or by two parts, is visited again with every part below
	// it, and the work can double at each level: as in every shipped mesh, a part is listed as a child once at most. Each of those walks, and the world
	// transforms that some callers take for every element, go as deep as the parts are linked: a part is at most 32 parts below one that no part lists (the
	// deepest shipped part is 11 below).
	if (children)
	{
		if (!m.Take("CHLD", p, size) || size != 4 * (size_t)children)
			return "an inline mesh part whose CHLD is not its child count";
		for (unsigned i = 0; i < children; i++)
		{
			if (Le32(p + 4 * i) <= index || Le32(p + 4 * i) >= parts)
				return "an inline mesh part with a child that is not a later part of the mesh";
			if (links[Le32(p + 4 * i)].listedBy)
				return "an inline mesh part with a child that it or another part of the mesh lists already";
			if (links[index].depth >= kMeshMaxDepth)
				return "an inline mesh part with a child more than 32 parts below a part that no part lists";
			links[Le32(p + 4 * i)] = {(unsigned short)(index + 1), (unsigned char)(links[index].depth + 1)};
		}
	}
	if (parent && (!m.Take("PRNT", p, size) || size != 4 || Le32(p) >= index))
		return "an inline mesh part whose PRNT is not one earlier part of the mesh";
	// The pose goes up the PRNT and the drawing down the child lists: as in every shipped mesh they agree, so a part has a PRNT exactly when some part lists
	// it, and its PRNT names that part.
	if ((parent ? Le32(p) + 1 : 0) != links[index].listedBy)
		return "an inline mesh part whose PRNT is not the part that lists it as a child (a part has one exactly when some part lists it)";
	if (!TakeMeshBox(m))
		return "an inline mesh part without its bounding box";
	if (vframes && (!m.Take("VHFM", p, size) || size != vframes))
		return "an inline mesh part whose VHFM is not its virtual-frame count";
	// Each byte of the map picks a hand frame: the arrays below are read through it, and nothing between the map and them holds it to their length.
	for (unsigned i = 0; i < vframes; i++)
		if (p[i] >= hframes)
			return "an inline mesh part whose VHFM names a frame past its frame count";
	if (hframes)
	{
		if (!m.Take("HORI", p, size) || size != 48 * (size_t)hframes)
			return "an inline mesh part whose HORI is not its frame count of matrices";
		if (!m.Take("HPOS", p, size) || size != 16 * (size_t)hframes)
			return "an inline mesh part whose HPOS is not its frame count of vectors";
		if (fov && (!m.Take("HFOV", p, size) || size != 4 * (size_t)hframes))
			return "an inline mesh part whose HFOV is not its frame count of values";
	}
	// What a palette offset of this part's vertices may be: the float 3 times the number of a bone, as its bits.
	unsigned offsets[kMeshMaxBones] = {};
	if (bones)
	{
		if (!m.Take("BONE", p, size) || size != 4 * (size_t)bones)
			return "an inline mesh part whose BONE is not its bone count";
		for (unsigned i = 0; i < bones; i++)
		{
			if (Le32(p + 4 * i) >= parts)
				return "an inline mesh part with a bone that is not a part of the mesh";
			const float offset = 3.0f * i;
			memcpy(&offsets[i], &offset, 4);
		}
	}
	if (grid)
	{
		if (!m.Take("PBKT", p, size))
			return "an inline mesh part that has a collision grid but no PBKT";
		if (const char *bad = CheckMeshGrid(p, size))
			return bad;
	}
	// The cache. CMeshPart::GetWorldTransforms takes it for a part with cached frames when it is given no motion controller (the thing has none, or the
	// caller asks for none) and the caller does not ask for the uncached transforms, as the drawing (CMeshPart::EvaluateAnimatedTransform), the skinned
	// bones, the static shadows, the collision volumes, damage and the destroyable segments do: it goes up the parents while the part's position (or
	// orientation) is marked static, then reads the cached position (or orientation) of the part it stops at, with the frame bounded from above only, by
	// that part's count less one. CMeshPart::Deserialize allocates the cached arrays only for the words that are set, and keeps the count and the two flags
	// as the file has them. So each part is held to what CMeshPart::BuildCache leaves in every shipped part: the count is 1 or the virtual-frame count, a
	// CPOS follows exactly when the position is not static and a CORI exactly when the orientation is not, and only a part with a parent (one that some
	// part lists) is static. The walk up then stops inside the mesh, at a part whose array holds at least one frame, and a frame of 0 or more reads in it:
	// the drawing's, as the animation modes give it, and the 0 that most other callers pass.
	if (cached != 1 && cached != vframes)
		return "an inline mesh part whose cached frame count is neither 1 nor its virtual-frame count";
	if (cachedPositions == positionStatic)
		return "an inline mesh part with a CPOS when its position is static, or without one when it is not";
	if (cachedOrientations == orientationStatic)
		return "an inline mesh part with a CORI when its orientation is static, or without one when it is not";
	if ((positionStatic || orientationStatic) && !parent)
		return "an inline mesh part with no parent whose position or orientation is static (the walk up its parents would leave the mesh)";
	if (cachedPositions && (!m.Take("CPOS", p, size) || size != 16 * (size_t)cached))
		return "an inline mesh part whose CPOS is not its cached frame count of vectors";
	if (cachedOrientations && (!m.Take("CORI", p, size) || size != 48 * (size_t)cached))
		return "an inline mesh part whose CORI is not its cached frame count of matrices";
	if (reference)
	{
		if (!m.Take("REFR", p, size) || size != 4 || Le32(p) >= parts)
			return "an inline mesh part whose REFR is not one part of the mesh";
		target = (unsigned short)Le32(p);
	}
	// the vertex buffer: CMVB, then for each piece MMPT, IBUF, VBUF and TEXR (the first piece's VBUF holds the vertices the others share)
	if (!m.Take("PMVB", p, size))
		return "an inline mesh part without its PMVB";
	MeshCursor v{p, 0, size};
	if (!v.Take("CMVB", p, size) || size != 296)
		return "an inline mesh part whose PMVB does not start with a 296-byte CMVB";
	const unsigned pieces = Le32(p + 0x108);
	if (pieces > 12)
		return "an inline mesh part with more than 12 vertex-buffer pieces";
	if (skeletal ? !pieces : camera && pieces)
		return "an inline mesh skeletal part without vertices, or a camera part with them";
	// CDXMeshVB::Load reads the record over the live object: it replaces the pieces' pointers it makes and keeps the rest, the destructor frees the first
	// slot, and the drawing and release code call the shader pointer, so none of those may be left to the file.
	if (Le32(p + 0x110))
		return "an inline mesh part whose vertex buffer carries a shader pointer (the reader would keep it and call it)";
	for (unsigned slot = pieces; slot < 64; slot++)
		if (Le32(p + 8 + 4 * slot))
			return "an inline mesh part whose vertex buffer carries a piece pointer beyond its pieces (the destructor would free it)";
	const unsigned stride = skeletal ? 48 : 36, format = skeletal ? 0 : 0x152;
	if (pieces && (Le32(p + 0x114) != stride || Le32(p + 0x118) != format || Le32(p + 0x11C) != 4))
		return "an inline mesh part whose vertices are not the 36-byte format of a rigid part or the 48-byte format of a skeletal one, drawn as a strip";
	unsigned vertexCount = 0, vertexBytes = 0;
	for (unsigned piece = 0; piece < pieces; piece++)
	{
		if (!v.Take("MMPT", p, size) || size != 24)
			return "an inline mesh piece without a 24-byte MMPT";
		const unsigned vb = Le32(p), ib = Le32(p + 4), ic = Le32(p + 8), vc = Le32(p + 12), prims = Le32(p + 16), built = Le32(p + 20);
		if (built != 1 || ic < 3 || ic > 65535 || ib != 2 * ic || prims != ic - 2)
			return "an inline mesh piece whose MMPT counts disagree (built, index count and bytes, primitive count)";
		if (!piece)
		{
			if (!vc || vc > 65535 || vb != stride * vc)
				return "an inline mesh piece whose vertex count is zero, over 65535, or not its vertex bytes";
			vertexCount = vc;
			vertexBytes = vb;
			vertices += vc;
		}
		else if (vb != vertexBytes || vc != vertexCount)
			return "an inline mesh piece that does not share the first piece's vertices";
		indices += ic;
		if (++groups > kMeshMaxGroups || vertices > kMeshMaxVertices || indices > kMeshMaxIndices)
			return "an inline mesh with more than 100000 vertices, 600000 indices or 1024 pieces";
		if (!v.Take("IBUF", p, size) || size != ib)
			return "an inline mesh piece whose IBUF is not its index bytes";
		for (unsigned i = 0; i < ic; i++)
			if ((unsigned)(p[2 * i] | p[2 * i + 1] << 8) >= vertexCount)
				return "an inline mesh piece with an index past its vertices";
		if (!v.Take("VBUF", p, size) || size != (piece ? 0 : vertexBytes))
			return "an inline mesh piece whose VBUF is not its vertices (the first piece) or empty (the others)";
		// After a 48-byte vertex's position come three palette offsets, which the skinning shaders move straight into the address register that picks the
		// palette's rows: each must be exactly what the game's vertex builder writes, 3 times the number of one of the part's bones.
		if (skeletal && !piece)
			for (size_t at = 12; at < vertexBytes; at += 48)
				for (size_t slot = 0; slot < 3; slot++)
					if (std::find(offsets, offsets + bones, Le32(p + at + 4 * slot)) == offsets + bones)
						return "an inline mesh skeletal vertex whose palette offset is not 3 times the number of one of its bones";
		if (!v.Take("TEXR", p, size) || size != 24)
			return "an inline mesh piece without a 24-byte TEXR";
		for (int slot = 0; slot < 6; slot++)
			if (Le32(p + 4 * slot) != 0xFFFFFFFFu && Le32(p + 4 * slot) >= textures)
				return "an inline mesh piece with a texture slot that is not -1 or one of the mesh's textures";
	}
	if (!v.Done())
		return "an inline mesh part with bytes in its PMVB after its last piece";
	return m.Done() ? NULL : "an inline mesh part with bytes after its vertex buffer";
}

// One mesh of the stream, from its CMSH to its last chunk (the box, or the emitters after it); the cursor is left after it. The totals are the whole stream's, so the meshes of a chain share
// them. `chained` is the record's next-in-chain word (a pointer, which the loader tests only for zero): the mesh that follows is for the caller to walk. `partFrames` is lowered to the
// smallest virtual-frame count of the mesh's parts, `modeFrames` raised to the largest start or last of its animation modes, taken unsigned, and `modes` set to its animation-mode count,
// for the caller's checks over the chain.
const char *CheckOneMesh(MeshCursor &c, unsigned &vertices, unsigned &indices, unsigned &groups, bool &chained, unsigned &partFrames, unsigned &modeFrames, unsigned &modes)
{
	const unsigned char *p;
	size_t size;
	if (!c.Take("CMSH", p, size) || size != 372)
		return "an inline mesh that does not start with a 372-byte CMSH record";
	const unsigned textures = Le32(p + 4), chain = Le32(p + 8), sequences = Le32(p + 0xC), animModes = Le32(p + 0x14), emitters = Le32(p + 0x1C),
				   parts = Le32(p + 0x15C);
	if (!memchr(p + 0x24, 0, 300))
		return "an inline mesh whose name is not terminated";
	if (sequences)
		return "an inline mesh with cutscene sequences (not admitted)";
	chained = chain != 0;
	modes = animModes;
	if (textures > kMeshMaxTextures || animModes > kMeshMaxAnimModes || emitters > kMeshMaxEmitters || !parts || parts > kMeshMaxParts)
		return "an inline mesh with a texture, animation-mode, emitter or part count out of bounds";
	if (!c.Take("CMST", p, size) || size != 36 * (size_t)textures)
		return "an inline mesh whose CMST is not its texture count of 36-byte records";
	unsigned frames[kMeshMaxTextures];
	for (unsigned i = 0; i < textures; i++)
	{
		frames[i] = Le32(p + 36 * i + 8);
		if (!frames[i] || frames[i] > 16)
			return "an inline mesh texture with no frames or more than 16";
	}
	for (unsigned i = 0; i < textures; i++)
	{
		const unsigned char *t;
		size_t tsize;
		if (!c.Take("MSHT", p, size))
			return "an inline mesh with fewer MSHT texture lists than its texture count";
		MeshCursor w{p, 0, size};
		if (!w.Take("TEXB", t, tsize) || !w.Done() || tsize != 20 * (size_t)frames[i] + 128)
			return "an inline mesh whose MSHT is not one TEXB of five lanes for each frame and a 128-byte name";
		const unsigned char *name = t + 20 * (size_t)frames[i];
		const void *end = memchr(name, 0, 128);
		if (!end || end == name)
			return "an inline mesh texture whose name is empty or not terminated";
		for (const unsigned char *q = name; q < (const unsigned char *)end; q++)
			if (*q < 0x20 || *q >= 0x7F)
				return "an inline mesh texture whose name is not printable ASCII";
	}
	unsigned char types[kMeshMaxParts];
	unsigned short targets[kMeshMaxParts];
	MeshLink links[kMeshMaxParts] = {};
	for (unsigned i = 0; i < parts; i++)
	{
		if (!c.Take("MESP", p, size))
			return "an inline mesh with fewer MESP parts than its part count";
		if (const char *bad = CheckMeshPart(p, size, i, parts, textures, vertices, indices, groups, types[i], targets[i], links, partFrames))
			return bad;
	}
	// A reference part draws the part it names as that part's own geometry: every shipped one names a normal part.
	for (unsigned i = 0; i < parts; i++)
		if (types[i] == 6 && types[targets[i]] != 1)
			return "an inline mesh reference part that does not name a normal part";
	// The animation modes (CAnimMode: a 16-byte name; the mode, start, last and frame scaler, signed; the rate, a float), which CMesh::Deserialize copies as
	// they are. A thing's animation steps its phase by the rate of the mode it plays at each tick (CMesh::GetFrameIncrement through the mesh's render thing);
	// CAnimation::SetAnimMode and Process play a step of at most 0.0001, negative ones included, as 1, a mode that loops drops back by 1 when the phase reaches
	// 1, and Process then holds the phase at 1 at most: it stays from 0 to 1 for any rate that is a number, and a rate that is not a number makes it NaN. The
	// drawing takes floor(start + scaler * phase), from start to start + scaler, as the part's virtual frame and reads the frame map with it and the frame after
	// it, bounded from above only (CMeshPart::EvaluateAnimatedTransform); the mesh controllers and the texture lanes take a signed modulo of it; the modes are
	// found by name (stricmp). So each record is as every shipped one is: its name is terminated, its last is start + scaler or, going forward, one less, and
	// its rate is the bits of a float from +0 to 1 (no NaN, infinity or negative value). With start and last virtual frames of every part of the chain
	// (CheckInlineMesh), the frame is never below 0 nor above any part's virtual-frame count.
	if (animModes)
	{
		if (!c.Take("CAMD", p, size) || size != 36 * (size_t)animModes)
			return "an inline mesh whose CAMD is not its animation-mode count of 36-byte records";
		for (unsigned i = 0; i < animModes; i++)
		{
			const unsigned char *mode = p + 36 * (size_t)i;
			if (!memchr(mode, 0, 16))
				return "an inline mesh animation mode whose name is not terminated";
			const long long start = (int32_t)Le32(mode + 0x14), last = (int32_t)Le32(mode + 0x18), scaler = (int32_t)Le32(mode + 0x1C);
			if (last != start + scaler && !(scaler > 0 && last == start + scaler - 1))
				return "an inline mesh animation mode whose last frame is not its start plus its frame scaler (or, going forward, one less)";
			if (Le32(mode + 0x20) > 0x3F800000u)
				return "an inline mesh animation mode whose rate is not a float from 0 to 1";
			modeFrames = std::max(modeFrames, std::max(Le32(mode + 0x14), Le32(mode + 0x18)));
		}
	}
	if (!TakeMeshBox(c))
		return "an inline mesh without its bounding box";
	if (emitters)
	{
		if (!c.Take("CEMT", p, size) || size < 4 || Le32(p) != 336)
			return "an inline mesh whose CEMT does not start with the 336-byte emitter record size";
		size_t at = 4;
		for (unsigned i = 0; i < emitters; i++)
		{
			if (size - at < 336)
				return "an inline mesh whose CEMT holds fewer emitters than its count";
			const bool indexed = Le32(p + at + 0x40) != 0;
			at += 336;
			if (indexed)
			{
				if (size - at < 4 || Le32(p + at) >= parts)
					return "an inline mesh emitter whose part index is missing or not a part of the mesh";
				at += 4;
			}
		}
		if (at != size)
			return "an inline mesh whose CEMT has bytes after its last emitter";
	}
	return NULL;
}

// NULL when the stream after an unnamed MESH chunk's 325 reference bytes is a mesh of the profile above, or a chain of them, else the reason. A mesh whose next-in-chain word is set is followed
// by the mesh it names, framed as the game's own chained inline meshes are (the 12 shipped chains hold 5 meshes each): a PMSH header and a PMS2 header, each around the rest of the stream, the
// 309-byte reference (an all-zero name, a runtime id of 20000 to 29999 that no other mesh of the chain has, no optional word, a kept flag of 0 or 1) and the mesh at its CMSH. The last mesh
// ends the stream; a chain is at most 16 meshes. `firstId` is the first mesh's runtime id (its reference is the archive's), 0 when there is none. The drawing goes down the chain for one
// thing (CMeshRenderer::Render and CRTMesh::Render follow the next-in-chain pointer), and each part reads the modes of its own mesh, the one CMesh::Deserialize hands its reader, with the
// index the thing's mode has in its render thing's mesh (CMesh::GetFrameIncrement), and no reader of a part's modes compares that index with its own mesh's mode count. So, as in every
// shipped chain, every mesh of a chain has as many animation modes as the first, and the start and last of every mode of the chain are virtual frames of every part of the chain.
const char *CheckInlineMesh(const unsigned char *stream, size_t n, unsigned firstId = 0)
{
	if (n > kMeshMaxBytes)
		return "an inline mesh over 16 MiB";
	MeshCursor c{stream, 0, n};
	unsigned vertices = 0, indices = 0, groups = 0, ids[kMeshMaxChain], partFrames = ~0u, modeFrames = 0, modes = 0, firstModes = 0;
	ids[0] = firstId;
	for (unsigned depth = 1;; depth++)
	{
		bool chained = false;
		if (const char *bad = CheckOneMesh(c, vertices, indices, groups, chained, partFrames, modeFrames, modes))
			return bad;
		if (depth == 1)
			firstModes = modes;
		else if (modes != firstModes)
			return "an inline mesh whose chained mesh has another number of animation modes than the first mesh of its chain";
		if (!chained)
		{
			if (!c.Done())
				return "an inline mesh with bytes after its last chunk";
			return modeFrames < partFrames ? NULL : "an inline mesh animation mode whose start or last is not a virtual frame of every part of its mesh and the meshes chained to it";
		}
		if (depth == kMeshMaxChain)
			return "an inline mesh with a chain of more than 16 meshes";
		const size_t rest = n - c.at;
		if (rest < 16 + 309 + 8)
			return "an inline mesh whose chain word is set and no chained mesh follows";
		const unsigned char *h = stream + c.at, *reference = h + 16;
		if (memcmp(h, "PMSH", 4) || Le32(h + 4) != rest - 8 || memcmp(h + 8, "PMS2", 4) || Le32(h + 12) != rest - 16)
			return "an inline mesh whose chained mesh is not framed by PMSH and PMS2 headers that fill the rest of the stream";
		if (std::any_of(reference, reference + 300, [](unsigned char x) { return x != 0; }))
			return "an inline mesh whose chained mesh has a reference name that is not all zero";
		const unsigned id = Le32(reference + 300);
		bool refused = id < 20000 || id > 29999;
		for (unsigned i = 0; i < depth; i++)
			refused = refused || ids[i] == id;
		if (refused)
			return "an inline mesh whose chained mesh has a runtime id outside 20000 to 29999 or one that another mesh of the chain has";
		if (Le32(reference + 304) || reference[308] > 1)
			return "an inline mesh whose chained mesh has an optional word set or a kept flag that is not 0 or 1";
		ids[depth] = id;
		c.at += 16 + 309;
	}
}

// NULL when an added level archive's unpacked stream has, as its chunk headers declare it, the shape of the archives the
// game ships; else the reason. The game's own archives are not walked (there is a shipped copy to trust); an added one has
// none. This checks the declared structure, not the readers: CChunkReader::GetNext never skips to a chunk's declared end,
// CWorld::Deserialize takes WRLD's sub-chunks by position and not by tag, and the readers below take fixed amounts whatever a
// header says. LVLR, TARG and AYAD are read at fixed sizes. A TEXT is read as the DXTX and CTEX headers and the 344-byte record
// (CDXTexture::Deserialize), then 12 bytes (a TFRM header and the level count) for each of the record's mNumFrames, whose array
// has 16 entries (more overwrites the texture object, and past 40 its allocation). A MESH is read as its PMSH and PMS2
// headers, a 300-byte name, the runtime id and the optional word, 324 bytes before any branch, and one byte more when the mesh
// is kept (CMesh::Deserialize); a mesh the game does not want is skipped by the PMS2 header's size. A named mesh keeps its data
// in its own file (data\resources\meshes\m_NAME.aya), so the chunk holds those 325 bytes and, for each chained mesh, 317 more
// (a chunk header and the same 309 bytes), a count that only that file knows; an unnamed mesh holds its data in the chunk. So
// the walk requires what makes the declared structure and those readers agree on the shipped archives, and nothing more. The
// stream starts LVLR (version 103), TARG (1) and AYAD (the five structure sizes of this build), which do not recur. Every chunk
// header and payload fits in the stream (at most 2048 chunks) and carries one of the eleven tags a level archive holds (the
// base and goodie archives alone hold VSDS, PLAT, PMIB, DMKR and GDIE, which ReadResources would also run for a level). The
// texture entries are exactly the size the game reads: DXTX and CTEX around 344 bytes and 12 for each of 1 to 16 frames. The
// model entries are at least that size: PMSH around a PMS2 that fills it, of 325 bytes or more, and for a named mesh exactly
// 325 bytes and 317 for each chained mesh (an unnamed one's data is not looked at). The last chunk is the only WRES; it holds
// one WRLD of WDAT (8 bytes: -1 or a base level, and this level's number as the real world), a BSWD only when there is a base
// world, and an RLWD, in that order and nothing else, each body of 2 bytes to 4 MiB and of a version CWorld::LoadWorld accepts
// (43 to 50). What a world body, a texture record, a mesh or the terrain holds is not looked at here.
const char *CheckLevelArchive(const std::vector<unsigned char> &raw, int level)
{
	static const char *const tags[] = {"LVLR", "TARG", "AYAD", "TEXT", "MESH", "IMPS", "LNDS", "SURF", "ERES", "SSHD", "WRES"};
	static const unsigned guard[5] = {344, 372, 316, 5084, 92};
	size_t at = 0, wresAt = 0, wresSize = 0;
	int count = 0, wres = 0, inlineMeshes = 0;
	while (at < raw.size())
	{
		if (raw.size() - at < 8)
			return "a level archive with a cut chunk header";
		if (++count > kMaxLevelChunks)
			return "a level archive with more than 2048 chunks";
		const unsigned char *h = raw.data() + at;
		const size_t size = Le32(h + 4);
		if (size > raw.size() - at - 8)
			return "a level archive chunk that runs past the end of the stream";
		const unsigned char *p = h + 8;
		bool known = false;
		for (const char *tag : tags)
			known = known || memcmp(h, tag, 4) == 0;
		if (!known)
			return "a level archive chunk with a tag a level archive does not hold";
		if (count == 1 && (memcmp(h, "LVLR", 4) || size != 4 || Le32(p) != 103))
			return "a level archive that does not start with LVLR 103";
		if (count == 2 && (memcmp(h, "TARG", 4) || size != 4 || Le32(p) != 1))
			return "a level archive without TARG 1 second";
		if (count == 3)
		{
			if (memcmp(h, "AYAD", 4) || size != 24)
				return "a level archive without its AYAD guards third";
			for (int i = 0; i < 5; i++)
				if (Le32(p + 4 * i) != guard[i])
					return "a level archive built for other structure sizes (AYAD)";
		}
		if (count > 3 && (!memcmp(h, "LVLR", 4) || !memcmp(h, "TARG", 4) || !memcmp(h, "AYAD", 4)))
			return "a level archive that repeats LVLR, TARG or AYAD after its first three chunks";
		if (!memcmp(h, "TEXT", 4))
		{
			if (size < 372 || memcmp(p, "DXTX", 4) || Le32(p + 4) != size - 8 || memcmp(p + 8, "CTEX", 4))
				return "a level archive TEXT chunk that is not a DXTX/CTEX texture record of 372 bytes or more";
			// The frame count (mNumFrames, +0x138 of the texture object) is read only here, where the 372 bytes are known to be
			// there. 1 to 16 frames and exactly their length: no frame is 360 bytes, which the check above has refused.
			const unsigned frames = Le32(p + 328);
			if (frames > 16 || size != 360 + 12 * frames)
				return "a level archive TEXT chunk with more than 16 frames, or a size that is not 360 bytes and 12 for each frame";
		}
		if (!memcmp(h, "MESH", 4))
		{
			if (size < 325 || memcmp(p, "PMSH", 4) || Le32(p + 4) != size - 8 || memcmp(p + 8, "PMS2", 4) || Le32(p + 12) != size - 16)
				return "a level archive MESH chunk that is not a PMSH record of 325 bytes or more whose PMS2 record fills it";
			// A named mesh keeps its data in its own file: the chunk is the 325 bytes and 317 more for each chained mesh.
			if (p[16] && (size - 325) % 317)
				return "a level archive MESH chunk of a named mesh whose size is not 325 bytes and 317 for each chained mesh";
			// An unnamed mesh holds its data in the chunk, after the 325 bytes: one is admitted, with an all-zero name, a runtime id in a range no
			// shipped mesh uses, no optional flag, and a stream of the profile CheckInlineMesh walks.
			// The host tests build with PORT_MODS_TEST_SKIP_INLINE_MESH to run the rest of this walk over archives that hold inline meshes.
#ifndef PORT_MODS_TEST_SKIP_INLINE_MESH
			if (!p[16])
			{
				if (++inlineMeshes > 1)
					return "a level archive with more than one inline (unnamed) MESH chunk";
				if (memchr(p + 16, 0, 300) != p + 16 || std::any_of(p + 16, p + 316, [](unsigned char c) { return c != 0; }))
					return "a level archive inline MESH chunk whose reference name is not all zero";
				if (Le32(p + 316) < 20000 || Le32(p + 316) > 29999 || Le32(p + 320) || p[324] > 1)
					return "a level archive inline MESH chunk whose runtime id is outside 20000 to 29999, whose optional word is set, or whose kept flag is not 0 or 1";
				if (const char *bad = CheckInlineMesh(p + 325, size - 325, Le32(p + 316)))
					return bad;
			}
#else
			(void)inlineMeshes;
			(void)&CheckInlineMesh;
#endif
		}
		if (!memcmp(h, "WRES", 4))
		{
			wres++;
			wresAt = at + 8;
			wresSize = size;
		}
		else if (wres)
			return "a level archive with a chunk after its WRES";
		at += 8 + size;
	}
	if (count < 3 || wres != 1)
		return "a level archive without exactly one WRES chunk";
	const unsigned char *w = raw.data() + wresAt;
	if (wresSize < 8 || memcmp(w, "WRLD", 4) || Le32(w + 4) != wresSize - 8)
		return "a level archive whose WRES does not hold one WRLD";
	size_t k = wresAt + 8, size = 0;
	const size_t end = wresAt + wresSize;
	// The next chunk of WRLD, which must be `tag`: NULL with k at its payload and size its length, else the reason.
	auto next = [&](const char *tag, const char *absent, const char *misplaced) -> const char * {
		if (k == end)
			return absent;
		if (end - k < 8 || Le32(raw.data() + k + 4) > end - k - 8)
			return "a level archive WRLD with a chunk that runs past its end";
		size = Le32(raw.data() + k + 4);
		if (memcmp(raw.data() + k, tag, 4))
			return misplaced;
		k += 8;
		return NULL;
	};
	if (const char *bad = next("WDAT", "a level archive WRLD without WDAT", "a level archive WRLD that does not start with WDAT"))
		return bad;
	if (size != 8)
		return "a level archive WRLD whose WDAT is not 8 bytes";
	const int base = (int)Le32(raw.data() + k), real = (int)Le32(raw.data() + k + 4);
	k += 8;
	if (real != level)
		return "a level archive that names another level's number as its real world (WDAT)";
	if (base != -1 && (base < 0 || base > 999))
		return "a level archive whose base world (WDAT) is not a level number";
	for (int pass = base == -1 ? 1 : 0; pass < 2; pass++)
	{
		const bool body = pass == 1;
		if (const char *bad = next(body ? "RLWD" : "BSWD",
								   body ? "a level archive WRLD without a real world body (RLWD)"
										: "a level archive whose WDAT names a base world but whose WRLD has no BSWD",
								   body ? "a level archive WRLD whose RLWD does not follow its WDAT and base world"
										: "a level archive WRLD whose BSWD does not follow its WDAT"))
			return bad;
		if (size < 2 || size > kMaxWorld)
			return "a level archive world body of an unusable size (2 bytes to 4 MiB)";
		const unsigned version = raw[k] | raw[k + 1] << 8;
		if (version < 43 || version > 50)
			return "a level archive world body of a version CWorld::LoadWorld refuses (43 to 50)";
		k += size;
	}
	if (k != end)
		return "a level archive WRLD with chunks after its real world body";
	return NULL;
}

bool EndsWith(const std::string &s, const char *tail)
{
	size_t n = strlen(tail);
	return s.size() >= n && !s.compare(s.size() - n, n, tail);
}

bool TexturePath(const std::string &key)
{
	return !key.compare(0, 24, "data/resources/textures/") || !key.compare(0, 28, "data/resources/dxtntextures/");
}

#ifdef _WIN32
bool SameIdentity(const Identity &a, const Identity &b)
{
	return a.volume == b.volume && !memcmp(a.fileId, b.fileId, sizeof a.fileId) && a.size == b.size && a.written == b.written &&
		   a.changed == b.changed;
}

typedef LONG(NTAPI *NtCreateFileFn)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG,
									PVOID, ULONG);

// One name inside the held folder `parent`: the kernel resolves exactly this component against that handle, so nothing above it can be swapped for a junction between a check and
// this open (the Windows counterpart of Linux's openat chain). No link is followed: OBJ_DONT_REPARSE refuses any reparse the kernel would follow, and FILE_OPEN_REPARSE_POINT opens
// a final one as itself, for Verify to refuse. A folder is opened without FILE_DIRECTORY_FILE, whose documented companions do not include FILE_OPEN_REPARSE_POINT; Verify checks the kind on the handle.
// The access is what the chain needs and no more, and the share mode keeps writers and deleters out while the handle is open (a folder may not be renamed or removed either).
HANDLE OpenChild(HANDLE parent, const std::string &name, bool folder)
{
	static const NtCreateFileFn create = (NtCreateFileFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
	if (!create || name.empty() || name.size() > kMaxPart || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos)
		return INVALID_HANDLE_VALUE;
	wchar_t wide[kMaxPart + 1];
	for (size_t i = 0; i < name.size(); i++)
		wide[i] = (unsigned char)name[i];	// CheckPath and ValidId admit printable ASCII only
	UNICODE_STRING us;
	us.Buffer = wide;
	us.Length = us.MaximumLength = (USHORT)(name.size() * sizeof(wchar_t));
	OBJECT_ATTRIBUTES oa;
	memset(&oa, 0, sizeof oa);
	oa.Length = sizeof oa;
	oa.RootDirectory = parent;
	oa.ObjectName = &us;
	oa.Attributes = OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE;	// Windows names match as the game's own do; NameIsExact then holds a mod to the spelling
	const ACCESS_MASK access = FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE;	// FILE_READ_DATA is FILE_LIST_DIRECTORY on a folder
	const ULONG share = FILE_SHARE_READ | (folder ? FILE_SHARE_WRITE : 0);
	const ULONG options = FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT | (folder ? 0 : FILE_NON_DIRECTORY_FILE);
	IO_STATUS_BLOCK io;
	HANDLE h = NULL;
	LONG status = 0;
	for (int attempt = 0; attempt < 3; attempt++)
	{
		h = NULL;
		status = create(&h, access, &oa, &io, NULL, 0, share, FILE_OPEN, options, NULL, 0);
		if (status != (LONG)0xC0000043 /* STATUS_SHARING_VIOLATION */ || folder)
			break;
		Sleep(100);	// a scanner or an indexer holding a just-written file, or an editor saving it: twice more, then it is refused like any other file that cannot be opened
	}
	return status < 0 || !h ? INVALID_HANDLE_VALUE : h;
}

// What an opened handle is: a plain folder (folder) or a regular disk file with one name (a file), never a link; for a file its
// identity too. NULL on success, else the reason.
const char *Verify(HANDLE h, bool folder, Identity *id)
{
	FILE_ATTRIBUTE_TAG_INFO tag;
	FILE_STANDARD_INFO standard;
	FILE_BASIC_INFO basic;
	FILE_ID_INFO fid;
	if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof tag) ||
		!GetFileInformationByHandleEx(h, FileStandardInfo, &standard, sizeof standard) ||
		!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic))
		return "a handle that cannot be queried";
	if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		return "a reparse point (a link or a junction, or a file a cloud or storage service keeps elsewhere)";
	if (((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != folder)
		return folder ? "a file where a folder should be" : "a folder where a file should be";
	if (folder)
		return NULL;
	if (GetFileType(h) != FILE_TYPE_DISK || standard.Directory)
		return "not a file on disk";
	if (standard.NumberOfLinks != 1)
		return "a file with more than one name (a hard link)";
	if (!GetFileInformationByHandleEx(h, FileIdInfo, &fid, sizeof fid))
		return "a volume that does not report file ids (mods on Windows need Windows 10 or later and NTFS or ReFS)";
	id->volume = fid.VolumeSerialNumber;
	memcpy(id->fileId, fid.FileId.Identifier, sizeof id->fileId);
	id->size = (unsigned long long)standard.EndOfFile.QuadPart;
	id->written = basic.LastWriteTime.QuadPart;
	id->changed = basic.ChangeTime.QuadPart;
	return NULL;
}

// The name the volume holds for this handle (the last component of its NORMALIZED path: the case stored on the volume, and the long name even when the open went through a
// short one; FILE_NAME_OPENED and FileNameInfo would give back the spelling the open used) against the manifest's spelling, byte for byte. The open matched case-insensitively
// as the game's own opens do, but a mod must be valid on Windows only if it is valid on Linux, where names are exact on a case-sensitive file system (a case-folding Linux folder
// matches any case, so there the Linux check can pass a mod this refuses: mods.h).
bool NameIsExact(HANDLE h, const std::string &name)
{
	std::vector<wchar_t> w(2 * MAX_PATH + 64);
	DWORD n = GetFinalPathNameByHandleW(h, w.data(), (DWORD)w.size(), VOLUME_NAME_NONE | FILE_NAME_NORMALIZED);
	if (n >= w.size())	// too small: n is the size needed, with the terminator
	{
		w.resize(n + 1);
		n = GetFinalPathNameByHandleW(h, w.data(), (DWORD)w.size(), VOLUME_NAME_NONE | FILE_NAME_NORMALIZED);
	}
	if (n == 0 || n >= w.size())
		return false;
	size_t start = n;
	while (start > 0 && w[start - 1] != L'\\')
		start--;
	if (n - start != name.size())
		return false;
	for (size_t i = 0; i < name.size(); i++)
		if (w[start + i] != (unsigned char)name[i])
			return false;
	return true;
}

// Opens rel (the manifest's spelling) below the held folder root, one component at a time, each against the handle of the one
// before: every folder on the way must be a plain folder and the file itself a regular file with one name; a link is refused.
HANDLE OpenBelow(HANDLE root, const std::string &rel, Identity *id, const char **why)
{
	HANDLE dir = root;	// the held root is not ours to close
	size_t start = 0;
	while (true)
	{
		size_t slash = rel.find('/', start);
		const bool last = slash == std::string::npos;
		HANDLE h = OpenChild(dir, rel.substr(start, last ? std::string::npos : slash - start), !last);
		if (dir != root)
			CloseHandle(dir);
		if (h == INVALID_HANDLE_VALUE)
		{
			*why = last ? "a file that cannot be opened" : "a missing folder, or a folder that is a link";
			return h;
		}
		if (const char *bad = Verify(h, !last, id))
		{
			CloseHandle(h);
			*why = bad;
			return INVALID_HANDLE_VALUE;
		}
		if (!NameIsExact(h, rel.substr(start, last ? std::string::npos : slash - start)))
		{
			CloseHandle(h);
			*why = "a name the volume holds differently from the manifest's spelling (another case, or a short name; a mod must match exactly on every platform)";
			return INVALID_HANDLE_VALUE;
		}
		if (last)
			return h;
		dir = h;
		start = slash + 1;
	}
}

bool ReadAll(HANDLE h, unsigned long long size, std::vector<unsigned char> *data)
{
	data->resize((size_t)size);
	size_t got = 0;
	while (got < size)
	{
		DWORD n = 0, want = (DWORD)std::min<unsigned long long>(size - got, 1 << 20);
		if (!ReadFile(h, data->data() + got, want, &n, NULL) || n == 0)
			return false;
		got += n;
	}
	unsigned char extra;
	DWORD n = 0;
	return ReadFile(h, &extra, 1, &n, NULL) && n == 0;
}
#else
bool SameIdentity(const Identity &a, const Identity &b)
{
	return a.dev == b.dev && a.ino == b.ino && a.size == b.size && a.mtime.tv_sec == b.mtime.tv_sec &&
		   a.mtime.tv_nsec == b.mtime.tv_nsec && a.ctime.tv_sec == b.ctime.tv_sec && a.ctime.tv_nsec == b.ctime.tv_nsec;
}

Identity IdentityOf(const struct stat &st)
{
	Identity id;
	id.dev = st.st_dev;
	id.ino = st.st_ino;
	id.size = st.st_size;
	id.mtime = st.st_mtim;
	id.ctime = st.st_ctim;
	return id;
}

// Opens rel (the manifest's spelling) below the held root descriptor, one component at a time and never following a
// link; the file must be a regular file with one link.
int OpenBelow(int root, const std::string &rel, Identity *id, const char **why)
{
	int dir = dup(root);
	size_t start = 0;
	while (dir >= 0)
	{
		size_t slash = rel.find('/', start);
		if (slash == std::string::npos)
			break;
		std::string part = rel.substr(start, slash - start);
		int next = openat(dir, part.c_str(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		close(dir);
		dir = next;
		start = slash + 1;
	}
	if (dir < 0)
	{
		*why = errno == ELOOP || errno == ENOTDIR ? "a folder on its path that is a link or not a folder"
												  : "a missing folder on its path";
		return -1;
	}
	int fd = openat(dir, rel.c_str() + start, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
	int error = errno;
	close(dir);
	if (fd < 0)
	{
		*why = error == ELOOP ? "a link" : "a file that cannot be opened";
		return -1;
	}
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1)
	{
		close(fd);
		*why = "not a regular file with a single name (a folder, device, pipe or hard link)";
		return -1;
	}
	fcntl(fd, F_SETFL, 0);	// clear O_NONBLOCK (it only kept a FIFO from blocking the open)
	*id = IdentityOf(st);
	return fd;
}

bool ReadAll(int fd, unsigned long long size, std::vector<unsigned char> *data)
{
	data->resize((size_t)size);
	size_t got = 0;
	while (got < size)
	{
		ssize_t n = read(fd, data->data() + got, (size_t)std::min<unsigned long long>(size - got, 1 << 20));
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return false;
		got += (size_t)n;
	}
	unsigned char extra;
	return read(fd, &extra, 1) == 0;
}
#endif

#ifdef _WIN32
void CloseFile(HANDLE h) { CloseHandle(h); }
bool Opened(HANDLE h) { return h != INVALID_HANDLE_VALUE; }
bool StillSame(HANDLE h, const Identity &id)
{
	Identity now;
	return Verify(h, false, &now) == NULL && SameIdentity(now, id);
}
#else
void CloseFile(int fd) { close(fd); }
bool Opened(int fd) { return fd >= 0; }
bool StillSame(int fd, const Identity &id)
{
	struct stat st;
	return fstat(fd, &st) == 0 && SameIdentity(IdentityOf(st), id);
}
#endif

const char kTooMuch[] = "the enabled mods hold more than 1 GiB";

// One entry of a mod's manifest: NULL and its File, or the reason it is refused.
const char *CheckEntry(const Mod *mod, const Entry &e, std::map<std::string, int> *seen, File *out, unsigned long long *total)
{
	static std::string reason;
	std::vector<std::string> parts;
	if (const char *bad = CheckPath(e.path, &parts))
		return (reason = "file " + Clean(e.path, 120) + ": " + bad).c_str();
	std::string key = Lower(e.path);
	if (seen->count(key))
		return (reason = "file " + e.path + ": listed twice").c_str();
	(*seen)[key] = 1;
	const bool world = WorldFile(key);
	const bool hasGameFile = PortGameHasFile(e.path.c_str());
	int archiveLevel = 0;
	const bool archive = !hasGameFile && LevelArchiveName(key, &archiveLevel);
	if (!world && !archive && !hasGameFile)
		return (reason = "file " + e.path + ": the game has no such file (a mod can only replace the game's files, or add a "
						 "world file, data/worlds/worldNNN.wrd, or the archive of a multiplayer level that has none, "
						 "data/resources/NNN_res_PC.aya for NNN 850 to 899)").c_str();
	if (world && e.size > kMaxWorld)
		return (reason = "file " + e.path + ": a world file larger than 4 MiB").c_str();
	if (archive && e.size > kMaxArchive)
		return (reason = "file " + e.path + ": an added level archive larger than 8 MiB").c_str();
	File &f = *out;
	f.key = key;
	f.rel = e.path;
	f.size = e.size;
	f.refused = 0;
	f.served = 0;
	f.added = (world || archive) && !hasGameFile;
	f.archive = archive;
	memcpy(f.sha, e.sha, 32);
	const char *why = NULL;
	auto h = OpenBelow(mod->root, e.path, &f.id, &why);
	if (!Opened(h))
		return (reason = "file " + e.path + ": " + why).c_str();
	if ((unsigned long long)f.id.size != e.size)
	{
		CloseFile(h);
		return (reason = "file " + e.path + ": its size is not the manifest's").c_str();
	}
	if ((*total += e.size) > kMaxTotal)
	{
		CloseFile(h);
		return kTooMuch;
	}
	std::vector<unsigned char> data;
	bool ok = ReadAll(h, e.size, &data) && StillSame(h, f.id);
	CloseFile(h);
	if (!ok)
		return (reason = "file " + e.path + ": it could not be read whole, or it changed while it was read").c_str();
	SHA256_CTX c;
	unsigned char sum[32];
	sha256_init(&c);
	sha256_update(&c, data.data(), data.size());
	sha256_final(&c, sum);
	if (memcmp(sum, e.sha, 32) != 0)
		return (reason = "file " + e.path + ": its SHA-256 is not the manifest's").c_str();
	if (EndsWith(key, ".aya"))
	{
		std::vector<unsigned char> unpacked;
		if (const char *bad = CheckPacked(data.data(), data.size(), TexturePath(key), archive ? &unpacked : NULL))
			return (reason = "file " + e.path + ": " + bad).c_str();
		if (archive)
			if (const char *bad = CheckLevelArchive(unpacked, archiveLevel))
				return (reason = "file " + e.path + ": " + bad).c_str();
	}
	return NULL;
}

// Loads one mod: NULL and its files, or the reason it is refused. In the check mode every refusal has been printed by the time this returns (kReported).
const char *LoadMod(const std::string &id, Mod *mod, std::vector<File> *files, unsigned long long *total)
{
	const int refusalsBefore = gCheckRefusals;
	mod->id = id;
#ifdef _WIN32
	{
		// The user folder itself may be reached through a link (as on Linux); `mods` and the mod's own folder may not.
		wchar_t userW[2 * MAX_PATH];
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, PortUserDir(), -1, userW, sizeof userW / sizeof userW[0])) {
			const DWORD error = GetLastError();
			if (error == ERROR_INSUFFICIENT_BUFFER)
				return "the user folder path is too long for mod loading";
			if (error == ERROR_NO_UNICODE_TRANSLATION)
				return "the user folder path is not valid UTF-8";
			return "Windows could not convert the user folder path";
		}
		HANDLE user = CreateFileW(userW, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
								  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		bool inexact = false;	// a folder the volume spells in another case than `mods` or the id: valid here, not on Linux
		HANDLE mods = user == INVALID_HANDLE_VALUE ? user : OpenChild(user, "mods", true);
		if (mods != INVALID_HANDLE_VALUE && (Verify(mods, true, NULL) || (inexact = !NameIsExact(mods, "mods"))))
		{
			CloseHandle(mods);
			mods = INVALID_HANDLE_VALUE;
		}
		HANDLE root = mods == INVALID_HANDLE_VALUE ? mods : OpenChild(mods, id, true);
		if (root != INVALID_HANDLE_VALUE && (Verify(root, true, NULL) || (inexact = !NameIsExact(root, id))))
		{
			CloseHandle(root);
			root = INVALID_HANDLE_VALUE;
		}
		if (user != INVALID_HANDLE_VALUE)
			CloseHandle(user);
		if (mods != INVALID_HANDLE_VALUE)
			CloseHandle(mods);
		mod->root = root;
		if (inexact)
			return "a mods folder or a mod folder that the volume spells differently from `mods` or the id (another case, or a short name; a mod must match exactly on every platform)";
		if (root == INVALID_HANDLE_VALUE)
			return "no such folder in the user folder's mods folder (or the folder is a link)";
	}
	Identity mid;
	const char *why = NULL;
	HANDLE m = OpenBelow(mod->root, "mod.json", &mid, &why);
#else
	int user = open(PortUserDir(), O_PATH | O_DIRECTORY | O_CLOEXEC);
	int mods = user < 0 ? -1 : openat(user, "mods", O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	mod->root = mods < 0 ? -1 : openat(mods, id.c_str(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (user >= 0)
		close(user);
	if (mods >= 0)
		close(mods);
	if (mod->root < 0)
		return "no such folder in the user folder's mods folder (or the folder is a link)";
	Identity mid;
	const char *why = NULL;
	int m = OpenBelow(mod->root, "mod.json", &mid, &why);
#endif
	static std::string reason;
	if (!Opened(m))	// with the open chain's own reason (on Windows, for example, a Mod.json the volume spells in another case)
		return (reason = std::string("mod.json is missing, a link, or not a regular file: ") + (why ? why : "it cannot be opened")).c_str();
	std::vector<unsigned char> text;
	bool read = (unsigned long long)mid.size <= kMaxManifest && ReadAll(m, mid.size, &text);
	CloseFile(m);
	if (!read)
		return "mod.json is larger than 256 KiB or could not be read";
	std::vector<Entry> entries;
	if (const char *bad = ReadManifest((const char *)text.data(), text.size(), id, mod, &entries))
		return bad;
	std::map<std::string, int> seen;
	for (const Entry &e : entries)
	{
		File f;
		const char *bad = CheckEntry(mod, e, &seen, &f, total);
		if (!bad)
		{
			files->push_back(f);
			continue;
		}
		if (!gCheck)
			return bad;
		LogRefusal(id, bad);
		if (bad == kTooMuch)
			break;	// nothing after it can fit either
	}
	return gCheckRefusals != refusalsBefore ? kReported : NULL;
}

void Load()
{
	gFence = Fence();
	std::vector<std::string> ids;
	if (gCheckIds)
		ids = *gCheckIds;
	else
	{
		char list[2048] = "";
		if (!CE_SettingGetText("mods", list, sizeof list) || !list[0])
		{
			if (gCheck)
				Log("no mods enabled: ce.ini has no usable mods= line");
			return;
		}
		for (const char *p = list; *p;)
		{
			const char *comma = strchr(p, ',');
			std::string id(p, comma ? (size_t)(comma - p) : strlen(p));
			while (!id.empty() && (id.back() == ' ' || id.back() == '\t'))
				id.pop_back();
			while (!id.empty() && (id[0] == ' ' || id[0] == '\t'))
				id.erase(0, 1);
			if (!id.empty())
				ids.push_back(id);
			p = comma ? comma + 1 : p + strlen(p);
		}
	}
	if (gFence && gCheck)
		Log("note: a game started with %s set keeps mods off; these checks are not affected", gFence);
	else if (gFence)
	{
		Log("off for this run (%s is set): %u enabled in ce.ini not loaded", gFence, (unsigned)ids.size());
		return;
	}
	if (const char *off = PlatformOff())
	{
		Log("%s", off);
		return;
	}
	unsigned running[4];
	if (!RunningVersion(running))
		Log("development build: the mods' CE version ranges are not checked");
	std::map<std::string, size_t> byKey;	// key -> index in files
	std::vector<File> files;
	unsigned long long total = 0;
	size_t admittedEntries = 0;	// counts every manifest entry, including files overridden by a later mod
	std::vector<std::string> listed;	// every id taken so far, loaded or refused: a second mention is neither checked nor counted again
	for (const std::string &id : ids)
	{
		if (std::find(listed.begin(), listed.end(), id) != listed.end())
		{
			Log("%s is listed twice%s; its first place counts", Clean(id, 48).c_str(), gCheckIds ? "" : " in ce.ini");
			continue;
		}
		listed.push_back(id);
		if (!ValidId(id))
		{
			LogRefusal(Clean(id, 48), "a mod id is 1 to 48 lowercase letters, digits, '-' or '_'");
			gCheckRefusedMods++;
			continue;
		}
		if (DeviceName(id))
		{
			LogRefusal(id, "a Windows device name, which no Windows folder can have (a mod must load on every platform)");
			gCheckRefusedMods++;
			continue;
		}
		if ((int)gMods.size() >= kMaxMods)
		{
			LogRefusal(id, "at most 16 mods can be enabled");
			gCheckRefusedMods++;
			continue;
		}
		Mod mod;
		std::vector<File> mine;
		unsigned long long before = total;
		if (const char *why = LoadMod(id, &mod, &mine, &total))
		{
			if (*why)
				LogRefusal(id, why);
			gCheckRefusedMods++;
			total = before;
			if (Opened(mod.root))
				CloseFile(mod.root);
			continue;
		}
		if (admittedEntries + mine.size() > (size_t)kMaxFilesTotal)
		{
			LogRefusal(id, "the enabled mods replace more than 8192 files");
			gCheckRefusedMods++;
			total = before;
			CloseFile(mod.root);
			continue;
		}
		int index = (int)gMods.size();
		admittedEntries += mine.size();
		mod.files = (int)mine.size();
		gMods.push_back(mod);
		for (File &f : mine)
		{
			f.mod = index;
			auto it = byKey.find(f.key);
			if (it == byKey.end())
			{
				byKey[f.key] = files.size();
				files.push_back(f);
			}
			else
			{
				Log("%s replaces %s over %s (the later mod in ce.ini wins)", id.c_str(), f.rel.c_str(),
					gMods[files[it->second].mod].id.c_str());
				files[it->second] = f;
			}
		}
	}
	std::sort(files.begin(), files.end(), [](const File &a, const File &b) { return a.key < b.key; });
	gFiles.swap(files);
	if (gMods.empty())
	{
		Log("none active");
		return;
	}
	unsigned added = 0;
	for (const File &f : gFiles)
		added += f.added;
	if (added)
		Log("%u active, %u game files replaced, %u added (experimental)", (unsigned)gMods.size(), (unsigned)gFiles.size() - added,
			added);
	else
		Log("%u active, %u game files replaced (experimental)", (unsigned)gMods.size(), (unsigned)gFiles.size());
	for (size_t i = 0; i < gMods.size(); i++)
		Log("active %u: %s \"%s\" version %s%s%s, %d files", (unsigned)(i + 1), gMods[i].id.c_str(),
			Clean(gMods[i].name).c_str(), Clean(gMods[i].version, 32).c_str(), gMods[i].author.empty() ? "" : " by ",
			Clean(gMods[i].author).c_str(), gMods[i].files);
	for (const File &f : gFiles)
	{
		char sha[17];
		for (int i = 0; i < 8; i++)
			snprintf(sha + 2 * i, 3, "%02x", f.sha[i]);
		Log("%s %s %s (%llu bytes, sha256 %s...)", gMods[f.mod].id.c_str(), f.added ? "adds" : "replaces", f.rel.c_str(),
			f.size, sha);
	}
}

const File *Find(const char *game_path)
{
	std::string key;
	if (gFiles.empty() || !GameKey(game_path, &key))
		return NULL;
	auto it = std::lower_bound(gFiles.begin(), gFiles.end(), key, [](const File &f, const std::string &k) { return f.key < k; });
	return it != gFiles.end() && it->key == key ? &*it : NULL;
}

// A refusal is logged once per file, with what the game reads instead. A replaced file falls back to the game's own copy.
// A world file the mod added has none: when the caller only asked by name (fallback, PortModHas) the level reads its own
// world from the archive, and when it opened the file (PortModOpenRead) the read fails. An archive the mod added has no
// fallback at all: the level has no archive, so it cannot load.
void Refused(const File *f, const char *why, bool fallback)
{
	if (__atomic_exchange_n(&const_cast<File *>(f)->refused, 1, __ATOMIC_RELAXED))
		return;
	const char *then = !f->added ? "the game's own file is read"
				 : f->archive ? "the game has no such level archive, so the level cannot load"
				 : fallback ? "the level's own world is read instead"
					    : "the game has no file of that name, so the read fails";
	Log("%s: %s %s; %s", gMods[f->mod].id.c_str(), f->rel.c_str(), why, then);
}

// The first time the game is given a mod's copy of a file, the log says so (once per file): what the game read is then
// in the log, not only what was checked at startup.
void Served(const File *f)
{
	if (!__atomic_exchange_n(&const_cast<File *>(f)->served, 1, __ATOMIC_RELAXED))
		Log("%s serves %s to the game", gMods[f->mod].id.c_str(), f->rel.c_str());
}
}

void PortModsInit()
{
	int expected = 0;
	if (gState.compare_exchange_strong(expected, 1))
	{
		Load();
		gState.store(2, std::memory_order_release);
		return;
	}
	while (gState.load(std::memory_order_acquire) != 2)
#ifdef _WIN32
		Sleep(0);
#else
		sched_yield();
#endif
}

int PortModsCheck(int count, char *const *ids)
{
	gCheck = true;
	if (const char *off = PlatformOff())
	{
		Log("%s", off);
		return 3;
	}
	static const std::vector<std::string> named(ids, ids + (count > 0 ? count : 0));
	if (!named.empty())
		gCheckIds = &named;
	PortModsInit();
	Log("result: %u passed, %d refused", (unsigned)gMods.size(), gCheckRefusedMods);
	return gCheckRefusals ? 1 : 0;
}

int PortModsActive()
{
	PortModsInit();
	return (int)gMods.size();
}

const char *PortModsFence()
{
	PortModsInit();
	return gFence;
}

// The startup table is not enough to answer: CWorld::Load skips the level archive's copy when this says yes, and the read
// that follows is refused when the file changed since startup (an editor saving it, a backup hard-linking it, a chmod),
// which would leave the level with no world at all. So this does the cheap half of PortModOpenRead: it opens the file
// below the mod's held folder and compares its identity with the one checked at startup (nothing is read or hashed; the
// identity is a change detector, the read that follows hashes). A file that no longer matches is not claimed.
// A change between this answer and the read that follows still ends in that read's refusal.
bool PortModHas(const char *game_path)
{
	PortModsInit();
	const File *f = Find(game_path);
	if (!f)
		return false;
	Identity id;
	const char *why = NULL;
	auto h = OpenBelow(gMods[f->mod].root, f->rel, &id, &why);
	if (!Opened(h))
	{
		Refused(f, "can no longer be opened safely", true);
		return false;
	}
	bool same = SameIdentity(id, f->id);
	CloseFile(h);
	if (!same)
	{
		Refused(f, "changed after it was checked at startup", true);
		return false;
	}
	return true;
}

#ifdef _WIN32
// A new file for one snapshot: an unused name (CREATE_NEW), no other opener at all from its first moment (share 0), removed by the system when its last handle closes, a crash
// included. FILE_ATTRIBUTE_TEMPORARY keeps it in memory while memory allows.
static HANDLE NewSnapshotFile()
{
	std::vector<wchar_t> folder(32768);
	DWORD n = GetTempPathW((DWORD)folder.size(), folder.data());
	if (n == 0 || n >= folder.size())
		return INVALID_HANDLE_VALUE;
	static std::atomic<unsigned long> serial{0};
	for (int attempt = 0; attempt < 16; attempt++)
	{
		std::wstring path = std::wstring(folder.data(), n) + L"onslaught-ce-mod-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(serial.fetch_add(1)) + L".tmp";
		HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
							   FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (h != INVALID_HANDLE_VALUE || (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS))
			return h;
	}
	return INVALID_HANDLE_VALUE;
}

// Every byte of the held source, block by block, into the hash and the snapshot, and the end of the file where the manifest says it is. False if the file is shorter or longer
// than `size`, or a read or a write fails (*wrote says which).
static bool CopyAndHash(HANDLE source, HANDLE snapshot, unsigned long long size, unsigned char sum[32], bool *wrote)
{
	SHA256_CTX c;
	sha256_init(&c);
	std::vector<unsigned char> block((size_t)std::min<unsigned long long>(size ? size : 1, kBlock));
	unsigned long long got = 0;
	*wrote = true;
	while (got < size)
	{
		DWORD n = 0, want = (DWORD)std::min<unsigned long long>(size - got, block.size());
		if (!ReadFile(source, block.data(), want, &n, NULL) || n == 0)
			return false;
		sha256_update(&c, block.data(), n);
		got += n;
		DWORD put = 0;
		if (!WriteFile(snapshot, block.data(), n, &put, NULL) || put != n)
			return *wrote = false;
	}
	unsigned char extra;
	DWORD n = 0;
	if (!ReadFile(source, &extra, 1, &n, NULL) || n != 0)
		return false;
	sha256_final(&c, sum);
	return true;
}

// The mod's file as the game reads it, the Windows counterpart of the sealed memfd below: opened below its held folder, compared with the identity checked at startup, then copied
// through that handle into a private file while its bytes are hashed against the manifest's SHA-256, at every open. The handle returned reads that file and nothing else: it was
// filled by this process alone, no other opener is admitted, it has no write access and the source is closed again, so neither a change to the source nor a section someone mapped
// writable before this open can reach what the game reads. The identity only detects an ordinary change: the hash is what makes the bytes the checked ones.
static HANDLE OpenVerified(const File *f)
{
	Identity id;
	const char *why = NULL;
	HANDLE source = OpenBelow(gMods[f->mod].root, f->rel, &id, &why);
	if (source == INVALID_HANDLE_VALUE)
	{
		Refused(f, "can no longer be opened safely", false);
		return INVALID_HANDLE_VALUE;
	}
	if (!SameIdentity(id, f->id))
	{
		CloseHandle(source);
		Refused(f, "changed after it was checked at startup", false);
		return INVALID_HANDLE_VALUE;
	}
	HANDLE snapshot = NewSnapshotFile();
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		CloseHandle(source);
		Refused(f, "could not create an immutable snapshot", false);
		return INVALID_HANDLE_VALUE;
	}
	unsigned char sum[32];
	bool wrote = true;
	bool whole = CopyAndHash(source, snapshot, f->size, sum, &wrote) && StillSame(source, f->id);
	CloseHandle(source);
	if (!whole)
	{
		CloseHandle(snapshot);
		Refused(f, wrote ? "could not be read whole or changed while it was read" : "could not finish an immutable snapshot", false);
		return INVALID_HANDLE_VALUE;
	}
	if (memcmp(sum, f->sha, sizeof sum) != 0)
	{
		CloseHandle(snapshot);
		Refused(f, "no longer matches its checked SHA-256", false);
		return INVALID_HANDLE_VALUE;
	}
	LARGE_INTEGER start;
	start.QuadPart = 0;
	HANDLE reader = INVALID_HANDLE_VALUE;
	bool sealed = SetFilePointerEx(snapshot, start, NULL, FILE_BEGIN) &&
				  DuplicateHandle(GetCurrentProcess(), snapshot, GetCurrentProcess(), &reader, FILE_GENERIC_READ, FALSE, 0);
	CloseHandle(snapshot);	// the read-only handle keeps the file; the last handle removes it
	if (!sealed)
	{
		Refused(f, "could not finish an immutable snapshot", false);
		return INVALID_HANDLE_VALUE;
	}
	return reader;
}

void *PortModOpenRead(const char *game_path)
{
	PortModsInit();
	const File *f = Find(game_path);
	if (!f)
		return NULL;
	HANDLE h = OpenVerified(f);
	if (h == INVALID_HANDLE_VALUE)
		return NULL;
	Served(f);
	return h;
}
#else
int PortModOpenRead(const char *game_path)
{
	PortModsInit();
	const File *f = Find(game_path);
	if (!f)
		return -1;
	Identity id;
	const char *why = NULL;
	int fd = OpenBelow(gMods[f->mod].root, f->rel, &id, &why);
	if (fd < 0)
	{
		Refused(f, "can no longer be opened safely", false);
		return -1;
	}
	if (!SameIdentity(id, f->id))
	{
		close(fd);
		Refused(f, "changed after it was checked at startup", false);
		return -1;
	}
	// Returning the checked descriptor itself is not safe: another process can change the same inode after this
	// check and before or during the caller's read. Re-hash a private copy and return a kernel-sealed snapshot.
	std::vector<unsigned char> data;
	bool read = ReadAll(fd, f->size, &data) && StillSame(fd, f->id);
	close(fd);
	if (!read)
	{
		Refused(f, "could not be read whole or changed while it was read", false);
		return -1;
	}
	SHA256_CTX c;
	unsigned char sum[32];
	sha256_init(&c);
	sha256_update(&c, data.data(), data.size());
	sha256_final(&c, sum);
	if (memcmp(sum, f->sha, sizeof sum) != 0)
	{
		Refused(f, "no longer matches its checked SHA-256", false);
		return -1;
	}
	int snapshot = (int)syscall(SYS_memfd_create, "onslaught-ce-mod", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (snapshot < 0)
	{
		Refused(f, "could not create an immutable snapshot", false);
		return -1;
	}
	size_t pos = 0;
	while (pos < data.size())
	{
		ssize_t n = write(snapshot, data.data() + pos, data.size() - pos);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		pos += (size_t)n;
	}
	if (pos != data.size() || fcntl(snapshot, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) < 0 ||
		lseek(snapshot, 0, SEEK_SET) != 0)
	{
		close(snapshot);
		Refused(f, "could not finish an immutable snapshot", false);
		return -1;
	}
	Served(f);
	return snapshot;
}
#endif

#if defined(_WIN32) && defined(CE_VERSION)
// bea.exe --check-mods. The entry point is the game's own WinMain, so the Windows form of the command is this object's constructor: it runs before WinMain, and after the objects above,
// which it needs. On a match the process ends here: no window, no device, no network, nothing written. A program that reads the output gets it through its pipe; a console that started
// bea.exe (a GUI program, so it has none of its own) gets it through CONOUT$.
namespace
{
struct CheckCommand
{
	CheckCommand()
	{
		if (__argc < 2 || strcmp(__argv[1], "--check-mods") != 0)
			return;
		HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode;
		bool redirected = out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN && !GetConsoleMode(out, &mode);
		if (AttachConsole(ATTACH_PARENT_PROCESS) && !redirected)
			(void)!freopen("CONOUT$", "w", stdout);
		PortPathsNamesOnly();
		int status = PortModsCheck(__argc - 2, __argv + 2);
		fflush(NULL);
		ExitProcess((UINT)status);
	}
} gCheckCommand;
}
#endif
