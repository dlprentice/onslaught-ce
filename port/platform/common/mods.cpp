// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/mods.cpp: Onslaught CE mods (experimental; mods.h has the model). At the first read of a game
// file the enabled mods are loaded once and checked:
// - the folder: <user folder>/mods/<id>/, id = lowercase letters, digits, '-' and '_', opened without following links;
// - mod.json: strict JSON (no unknown or repeated keys), schema 1, the mod's id, name, version, author, description,
//   the CE versions it supports (ce_min, ce_max) and every file it replaces with its size and SHA-256;
// - each file: a game-relative path under data/ (no "..", no absolute or drive paths, no backslashes, no Windows device
//   names, streams or trailing dots), a file the game itself has, reached without links, a regular file with one link,
//   at most 64 MiB, matching its size and SHA-256; a packed .aya file must have the block structure the game's reader
//   can hold (CDXMemBuffer::ReadBlock), and a texture must be a DDS image the game's loader can hold (LoadFrame);
// - bounds: 16 mods, 4096 files per mod, 8192 files and 1 GiB in all.
// A mod with any refused file is refused whole. The log names each active mod and every replaced file by its
// game-relative path only (never the user folder's location). At run time a replaced Linux file is opened without
// following links, read again, checked and returned as a sealed immutable snapshot. If any step fails the game's file
// is used instead. Windows mods remain off until path traversal and path-only readers have a safe implementation.
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
#include <windows.h>
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
const size_t kMaxManifest = 256 << 10, kMaxPath = 240, kMaxPart = 128;
// The game's packed-file reader (src/DXMemBuffer.cpp): 1 MiB read buffers, a 0x102927-byte block buffer; LoadFrame's
// 0x500000-byte scratch buffer for one texture file (a file that fills it is a fatal error).
const unsigned long kBlock = 1ul << 20, kPackBuffer = 0x102927, kTextureScratch = 0x500000;
const unsigned kMaxTextureSide = 4096;

#ifdef _WIN32
struct Identity
{
	DWORD volume, high, low;
	FILETIME written;
	unsigned long long size;
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
	std::string root;
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
	unsigned char added;	// a world file the game does not have (the log says "adds", not "replaces")
};

std::vector<Mod> gMods;
std::vector<File> gFiles;	// sorted by key, one per key
std::atomic<int> gState{0};	// 0 not loaded, 1 loading, 2 loaded
const char *gFence;
int gLogLines;

void Log(const char *format, ...)
{
	int count = __atomic_add_fetch(&gLogLines, 1, __ATOMIC_RELAXED);
	if (count > kMaxLogLines)
		return;
	if (count == kMaxLogLines)
	{
		fprintf(stderr, PORT_LOG_PREFIX "mods: (further mod messages are not shown)\n");
		return;
	}
	char line[768];
	va_list ap;
	va_start(ap, format);
	vsnprintf(line, sizeof line, format, ap);
	va_end(ap);
	fprintf(stderr, PORT_LOG_PREFIX "mods: %s\n", line);
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

// data/worlds/worldNNN.wrd (key is lowercase): the one name a mod may add to the game's files. CWorld::Load reads a world
// body from it, and a loose copy the game lacks is how a mod supplies a level's world.
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
	for (const auto &f : root.fields)
		if (std::find_if(std::begin(known), std::end(known), [&](const char *k) { return f.first == k; }) == std::end(known))
			return "an unknown key in the manifest (schema 1 knows schema, id, name, version, author, description, ce_min, "
				   "ce_max and files)";
	const Json *schema = root.Get("schema");
	if (!schema || schema->type != Json::INT || schema->negative || schema->n != 1)
		return "schema is not 1";
	const Json *jid = root.Get("id");
	if (!jid || jid->type != Json::STR || jid->s != id)
		return "id is not the mod folder's name";
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
			return why;
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
			return "ce_min or ce_max is not a version (numbers separated by dots, such as 0.1.8)";
		if (released)
		{
			int order = VersionCompare(running, want);
			if (strcmp(k, "ce_min") == 0 ? order < 0 : order > 0)
				return "this version of Onslaught CE is outside the mod's ce_min..ce_max";
		}
	}
	const Json *files = root.Get("files");
	if (!files || files->type != Json::ARR || files->items.empty())
		return "files is missing, not a list, or empty";
	if ((int)files->items.size() > kMaxFilesPerMod)
		return "more than 4096 files";
	for (const Json &f : files->items)
	{
		if (f.type != Json::OBJ)
			return "a files entry is not an object";
		for (const auto &k : f.fields)
			if (k.first != "path" && k.first != "size" && k.first != "sha256")
				return "an unknown key in a files entry (path, size and sha256 only)";
		const Json *path = f.Get("path"), *sz = f.Get("size"), *sha = f.Get("sha256");
		if (!path || path->type != Json::STR || !sz || sz->type != Json::INT || sz->negative || !sha || sha->type != Json::STR)
			return "a files entry lacks path (text), size (a whole number) or sha256 (text)";
		Entry e;
		e.path = path->s;
		e.size = sz->n;
		if (!Hex(sha->s, e.sha))
			return "a sha256 that is not 64 lowercase hexadecimal digits";
		if (e.size > kMaxFile)
			return "a file larger than 64 MiB";
		entries->push_back(e);
	}
	return NULL;
}

// ---- file checks -----------------------------------------------------------------------------------------------------
unsigned Le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

// NULL when a packed .aya file's blocks are what CDXMemBuffer::ReadBlock can hold: each [u32 length][zlib data] block
// fits the game's block buffer and unpacks to at most 1 MiB, every block but the last unpacks to exactly 1 MiB (the
// reader appends blocks into a 1 MiB buffer until it is full), and no bytes follow the last block. A texture must also
// unpack to a DDS image below LoadFrame's 5 MiB limit, at most 4096 texels a side.
const char *CheckPacked(const unsigned char *d, size_t n, bool texture)
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
	return a.volume == b.volume && a.high == b.high && a.low == b.low && a.size == b.size &&
		   a.written.dwLowDateTime == b.written.dwLowDateTime && a.written.dwHighDateTime == b.written.dwHighDateTime;
}

// A directory that exists and is not a link (junction or symbolic link).
bool PlainDirectory(const std::string &path)
{
	DWORD a = GetFileAttributesA(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// Opens rel (the manifest's spelling) below root without following links: every folder on the way must be a plain
// folder, and the file itself is opened as the link if it is one, which then fails the checks.
HANDLE OpenBelow(const std::string &root, const std::string &rel, Identity *id, const char **why)
{
	std::string path = root;
	size_t start = 0;
	while (true)
	{
		size_t slash = rel.find('/', start);
		if (slash == std::string::npos)
			break;
		path += "\\" + rel.substr(start, slash - start);
		if (!PlainDirectory(path))
		{
			*why = "a missing folder, or a folder that is a link";
			return INVALID_HANDLE_VALUE;
		}
		start = slash + 1;
	}
	path += "\\" + rel.substr(start);
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
						   FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		*why = "a file that cannot be opened";
		return h;
	}
	BY_HANDLE_FILE_INFORMATION info;
	if (GetFileType(h) != FILE_TYPE_DISK || !GetFileInformationByHandle(h, &info) ||
		(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks != 1)
	{
		CloseHandle(h);
		*why = "a link, a folder or a file with more than one name";
		return INVALID_HANDLE_VALUE;
	}
	id->volume = info.dwVolumeSerialNumber;
	id->high = info.nFileIndexHigh;
	id->low = info.nFileIndexLow;
	id->written = info.ftLastWriteTime;
	id->size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
	return h;
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
	BY_HANDLE_FILE_INFORMATION info;
	if (!GetFileInformationByHandle(h, &info))
		return false;
	Identity now = id;
	now.written = info.ftLastWriteTime;
	now.size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
	return SameIdentity(now, id);
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

// Loads one mod: NULL and its files, or the reason it is refused.
const char *LoadMod(const std::string &id, Mod *mod, std::vector<File> *files, unsigned long long *total)
{
	mod->id = id;
#ifdef _WIN32
	std::string mods = std::string(PortUserDir()) + "\\mods";
	mod->root = mods + "\\" + id;
	if (!PlainDirectory(mods) || !PlainDirectory(mod->root))
		return "no such folder in the user folder's mods folder (or the folder is a link)";
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
	if (!Opened(m))
		return "mod.json is missing, a link, or not a regular file";
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
		static std::string reason;
		std::vector<std::string> parts;
		if (const char *bad = CheckPath(e.path, &parts))
			return (reason = "file " + Clean(e.path, 120) + ": " + bad).c_str();
		std::string key = Lower(e.path);
		if (seen.count(key))
			return (reason = "file " + e.path + ": listed twice").c_str();
		seen[key] = 1;
		const bool world = WorldFile(key);
		if (!world && !PortGameHasFile(e.path.c_str()))
			return (reason = "file " + e.path + ": the game has no such file (a mod can only replace the game's files, or add a "
							 "world file, data/worlds/worldNNN.wrd)").c_str();
		if (world && e.size > kMaxWorld)
			return (reason = "file " + e.path + ": a world file larger than 4 MiB").c_str();
		File f;
		f.key = key;
		f.rel = e.path;
		f.size = e.size;
		f.refused = 0;
		f.served = 0;
		f.added = world && !PortGameHasFile(e.path.c_str());
		memcpy(f.sha, e.sha, 32);
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
			return "the enabled mods hold more than 1 GiB";
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
			if (const char *bad = CheckPacked(data.data(), data.size(), TexturePath(key)))
				return (reason = "file " + e.path + ": " + bad).c_str();
		files->push_back(f);
	}
	return NULL;
}

void Load()
{
	gFence = Fence();
	char list[2048] = "";
	if (!CE_SettingGetText("mods", list, sizeof list) || !list[0])
		return;
	std::vector<std::string> ids;
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
	if (gFence)
	{
		Log("off for this run (%s is set): %u enabled in ce.ini not loaded", gFence, (unsigned)ids.size());
		return;
	}
#if defined(_WIN32) || defined(PORT_MODS_PLATFORM_OFF)
	// A path checked component-by-component can still have an ancestor swapped for a junction before a later open.
	// mmioOpenA also reopens by name after its check. Refuse until Windows uses a validated handle-based route.
	// (PORT_MODS_PLATFORM_OFF builds this branch for the host tests, which cannot run a Windows build.)
	Log("off on Windows until mod path safety is validated");
	return;
#endif
	unsigned running[4];
	if (!RunningVersion(running))
		Log("development build: the mods' CE version ranges are not checked");
	std::map<std::string, size_t> byKey;	// key -> index in files
	std::vector<File> files;
	unsigned long long total = 0;
	size_t admittedEntries = 0;	// counts every manifest entry, including files overridden by a later mod
	for (const std::string &id : ids)
	{
		if (!ValidId(id))
		{
			Log("refused %s: a mod id is 1 to 48 lowercase letters, digits, '-' or '_'", Clean(id, 48).c_str());
			continue;
		}
		if (std::any_of(gMods.begin(), gMods.end(), [&](const Mod &m) { return m.id == id; }))
		{
			Log("%s is listed twice in ce.ini; its first place counts", id.c_str());
			continue;
		}
		if ((int)gMods.size() >= kMaxMods)
		{
			Log("refused %s: at most 16 mods can be enabled", id.c_str());
			continue;
		}
		Mod mod;
		std::vector<File> mine;
		unsigned long long before = total;
		if (const char *why = LoadMod(id, &mod, &mine, &total))
		{
			Log("refused %s: %s", id.c_str(), why);
			total = before;
#ifndef _WIN32
			if (mod.root >= 0)
				close(mod.root);
#endif
			continue;
		}
		if (admittedEntries + mine.size() > (size_t)kMaxFilesTotal)
		{
			Log("refused %s: the enabled mods replace more than 8192 files", id.c_str());
			total = before;
#ifndef _WIN32
			close(mod.root);
#endif
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
// world from the archive, and when it opened the file (PortModOpenRead) the read fails.
void Refused(const File *f, const char *why, bool fallback)
{
	if (__atomic_exchange_n(&const_cast<File *>(f)->refused, 1, __ATOMIC_RELAXED))
		return;
	const char *then = !f->added ? "the game's own file is read"
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
// identity includes the change time, which a write cannot set back). A file that no longer matches is not claimed.
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
void *PortModOpenRead(const char *game_path)
{
	PortModsInit();
	const File *f = Find(game_path);
	if (!f)
		return NULL;
	Identity id;
	const char *why = NULL;
	HANDLE h = OpenBelow(gMods[f->mod].root, f->rel, &id, &why);
	if (h == INVALID_HANDLE_VALUE)
	{
		Refused(f, "can no longer be opened safely", false);
		return NULL;
	}
	if (!SameIdentity(id, f->id))
	{
		CloseHandle(h);
		Refused(f, "changed after it was checked at startup", false);
		return NULL;
	}
	Served(f);
	return h;
}

bool PortModPath(const char *game_path, char *out, size_t n)
{
	HANDLE h = (HANDLE)PortModOpenRead(game_path);
	if (!h)
		return false;
	CloseHandle(h);
	const File *f = Find(game_path);
	std::string path = gMods[f->mod].root + "\\" + f->rel;
	std::replace(path.begin(), path.end(), '/', '\\');
	return snprintf(out, n, "%s", path.c_str()) < (int)n;
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
