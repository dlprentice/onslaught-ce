#include "../../network/network.h"
// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_netcode.cpp: the join code, its secrets and the private launch handoff (ce_netcode.h).
#include "ce_netcode.h"
#include "../../network/compatibility.h"		// CE_CompatibilityLockstepLevel: the co-op maps a handoff may name

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>
// statx, not stat: the port's C++ compiles with -mms-bitfields, which moves struct stat's 64-bit fields on i686, while
// struct statx aligns every field itself, so both layouts agree (checked below).
static_assert(offsetof(struct statx, stx_uid) == 20 && offsetof(struct statx, stx_mode) == 28 &&
			  offsetof(struct statx, stx_size) == 40 && offsetof(struct statx, stx_mtime) == 112 &&
			  sizeof(struct statx) == 256, "statx layout");
#endif

// The launcher's SHA-256 (FIPS 180-4, port/updater/sha256.c), linked into the game too (port/build.py).
extern "C" {
#include "../../updater/sha256.h"
}

extern "C" int CE_NetGenerateSecret(uint8_t out[32]);	// port/network/transport.cpp: the system's random source

namespace {
const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const int kData = 22, kBits = 108;

// GF(32) with x^5 + x^2 + 1 (primitive): the check symbol's arithmetic.
unsigned GfMul(unsigned a, unsigned b)
{
	unsigned r = 0;
	for (int i = 0; i < 5; i++)
	{
		if (b & 1)
			r ^= a;
		b >>= 1;
		a <<= 1;
		if (a & 0x20)
			a ^= 0x25;
	}
	return r & 31;
}

// Weight of data symbol i: x^(i+1). None is 1, so moving a symbol into the check position is caught too.
unsigned Weight(int i)
{
	unsigned w = 1;
	for (int k = 0; k <= i; k++)
		w = GfMul(w, 2);
	return w;
}

unsigned Check(const unsigned *d)
{
	unsigned c = 0;
	for (int i = 0; i < kData; i++)
		c ^= GfMul(Weight(i), d[i]);
	return c;
}

int Symbol(char c)
{
	if (c >= 'a' && c <= 'z')
		c = char(c - 'a' + 'A');
	if (c == 'I' || c == 'L')
		c = '1';
	if (c == 'O')
		c = '0';
	for (int i = 0; i < 32; i++)
		if (kAlphabet[i] == c)
			return i;
	return -1;
}

// The 108 payload bits, most significant first: key (60), address (32), port (16).
int Bit(const CE_JoinCode *c, int n)
{
	if (n < 60)
		return int((c->key >> (59 - n)) & 1);
	if (n < 92)
		return int((c->ipv4 >> (91 - n)) & 1);
	if (n < kBits)
		return int((c->port >> (107 - n)) & 1);
	return 0;
}

void Hex(const uint8_t *p, size_t n, char *out)
{
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++)
	{
		out[2 * i] = digits[p[i] >> 4];
		out[2 * i + 1] = digits[p[i] & 15];
	}
	out[2 * n] = 0;
}

bool Unhex(const char *s, uint8_t *out, size_t n)
{
	if (strlen(s) != 2 * n)
		return false;
	for (size_t i = 0; i < 2 * n; i++)
	{
		char c = s[i];
		int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
		if (v < 0)
			return false;
		if (i & 1)
			out[i / 2] |= uint8_t(v);
		else
			out[i / 2] = uint8_t(v << 4);
	}
	return true;
}

// "a.b.c.d:port" with every part in range and a nonzero port.
bool Endpoint(const char *s)
{
	unsigned a, b, c, d, p;
	char tail;
	if (sscanf(s, "%3u.%3u.%3u.%3u:%5u%c", &a, &b, &c, &d, &p, &tail) != 5)
		return false;
	char again[32];
	snprintf(again, sizeof again, "%u.%u.%u.%u:%u", a, b, c, d, p);
	return a < 256 && b < 256 && c < 256 && d < 256 && p > 0 && p < 65536 && !strcmp(again, s);
}

bool Printable(const char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s < 0x20 || *s == 0x7f)
			return false;
	return true;
}
}

extern "C" {

void CE_CodeWipe(void *p, size_t n)
{
	volatile unsigned char *v = (volatile unsigned char *)p;
	while (n--)
		*v++ = 0;
}

void CE_CodeSha256(const void *data, size_t size, uint8_t out[32])
{
	SHA256_CTX c;
	sha256_init(&c);
	sha256_update(&c, data, size);
	sha256_final(&c, out);
	CE_CodeWipe(&c, sizeof c);
}

int CE_CodeEncode(const CE_JoinCode *code, char *out, size_t size)
{
	if (!code || !out || size < CE_CODE_TEXT || (code->key & ~CE_CODE_KEY_MASK) || !code->ipv4 || !code->port)
		return 0;
	unsigned d[kData];
	for (int i = 0; i < kData; i++)
	{
		d[i] = 0;
		for (int b = 0; b < 5; b++)
			d[i] = (d[i] << 1) | unsigned(Bit(code, 5 * i + b));
	}
	unsigned check = Check(d);
	int n = 0;
	for (int i = 0; i < CE_CODE_SYMBOLS; i++)
	{
		if (i && !(i % 4))
			out[n++] = '-';
		out[n++] = kAlphabet[i < kData ? d[i] : check];
	}
	out[n] = 0;
	CE_CodeWipe(d, sizeof d);
	return 1;
}

int CE_CodeDecode(const char *text, CE_JoinCode *code)
{
	if (!text)
		return CE_CODE_EMPTY;
	unsigned d[CE_CODE_SYMBOLS + 1];
	int n = 0;
	for (const char *p = text; *p; p++)
	{
		if (*p == '-' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			continue;
		int v = Symbol(*p);
		if (v < 0)
			return CE_CODE_BAD_CHAR;
		if (n > CE_CODE_SYMBOLS)
			return CE_CODE_LONG;
		d[n++] = unsigned(v);
	}
	int result = CE_CODE_OK;
	if (!n)
		result = CE_CODE_EMPTY;
	else if (n < CE_CODE_SYMBOLS)
		result = CE_CODE_SHORT;
	else if (n > CE_CODE_SYMBOLS)
		result = CE_CODE_LONG;
	else if (Check(d) != d[kData])
		result = CE_CODE_CHECK;
	else if (d[kData - 1] & 3)
		result = CE_CODE_BAD_PAD;
	CE_JoinCode c = {0, 0, 0};
	if (result == CE_CODE_OK)
	{
		for (int b = 0; b < kBits; b++)
		{
			unsigned bit = (d[b / 5] >> (4 - b % 5)) & 1;
			if (b < 60)
				c.key = (c.key << 1) | bit;
			else if (b < 92)
				c.ipv4 = (c.ipv4 << 1) | bit;
			else
				c.port = uint16_t((c.port << 1) | bit);
		}
		if (!c.ipv4 || !c.port)
			result = CE_CODE_BAD_ADDRESS;
		else if (code)
			*code = c;
	}
	CE_CodeWipe(d, sizeof d);
	CE_CodeWipe(&c, sizeof c);
	return result;
}

const char *CE_CodeProblem(int result)
{
	switch (result)
	{
	case CE_CODE_OK:			return "The code is complete.";
	case CE_CODE_EMPTY:			return "Type or paste the host's code.";
	case CE_CODE_SHORT:			return "The code is too short: it has 23 letters and digits.";
	case CE_CODE_LONG:			return "The code is too long: it has 23 letters and digits.";
	case CE_CODE_BAD_CHAR:		return "Codes use only digits and the letters A to Z except U.";
	case CE_CODE_CHECK:			return "The code has a typo: check it with the host.";
	case CE_CODE_BAD_PAD:		return "This is not an Onslaught CE code: check it with the host.";
	case CE_CODE_BAD_ADDRESS:	return "This code has no address: check it with the host.";
	}
	return "The code is not valid.";
}

int CE_CodeNewKey(uint64_t *key)
{
	uint8_t r[32];
	if (!key || !CE_NetGenerateSecret(r))
		return 0;
	uint64_t k = 0;
	for (int i = 0; i < 8; i++)
		k = (k << 8) | r[i];
	CE_CodeWipe(r, sizeof r);
	*key = k & CE_CODE_KEY_MASK;
	CE_CodeWipe(&k, sizeof k);
	return *key != 0;
}

void CE_CodeLobbySecret(uint64_t key, uint8_t out[32])
{
	static const char domain[] = "onslaught-ce join v1";
	uint8_t m[sizeof domain - 1 + 8];
	memcpy(m, domain, sizeof domain - 1);
	for (int i = 0; i < 8; i++)
		m[sizeof domain - 1 + i] = uint8_t(key >> (56 - 8 * i));
	CE_CodeSha256(m, sizeof m, out);
	CE_CodeWipe(m, sizeof m);
}

void CE_CodeStartSecret(const uint8_t lobby[32], const uint8_t nonce[16], uint8_t out[32])
{
	static const char domain[] = "onslaught-ce start v1";
	uint8_t m[sizeof domain - 1 + 32 + 16];
	memcpy(m, domain, sizeof domain - 1);
	memcpy(m + sizeof domain - 1, lobby, 32);
	memcpy(m + sizeof domain - 1 + 32, nonce, 16);
	CE_CodeSha256(m, sizeof m, out);
	CE_CodeWipe(m, sizeof m);
}

void CE_CodeLobbyDigest(const char *identity, const char *version, uint8_t out[32])
{
	static const char domain[] = "onslaught-ce lobby v1";
	char m[512];
	size_t n = 0;
	memcpy(m, domain, sizeof domain);		// with its NUL
	n = sizeof domain;
	size_t a = strlen(identity ? identity : ""), b = strlen(version ? version : "");
	if (a > 200) a = 200;
	if (b > 200) b = 200;
	memcpy(m + n, identity ? identity : "", a); n += a;
	m[n++] = 0;
	memcpy(m + n, version ? version : "", b); n += b;
	CE_CodeSha256(m, n, out);
}

//*********************************************************************************
int64_t CE_HandoffNow(void)
{
	return (int64_t)time(NULL);
}

static size_t LegacyCE_HandoffFormat(const CE_Handoff *h, char *out, size_t size)
{
	if (!h || !out)
		return 0;
	int n;
	if (h->kind == CE_HANDOFF_LAUNCH)
	{
		char secret[65], nonce[33];
		Hex(h->secret, 32, secret);
		Hex(h->nonce, 16, nonce);
		n = snprintf(out, size, "onslaught-ce handoff 1\nkind=launch\nrole=%s\nendpoint=%s\nsecret=%s\nlevel=%u\nnonce=%s\n"
					 "created=%lld\nargs=%s\n", h->role == 1 ? "host" : "guest", h->endpoint, secret, (unsigned)h->level,
					 nonce, (long long)h->created, h->args);
		CE_CodeWipe(secret, sizeof secret);
	}
	else if (h->kind == CE_HANDOFF_OUTCOME)
		n = snprintf(out, size, "onslaught-ce handoff 1\nkind=outcome\ngood=%d\ncreated=%lld\ntext=%s\n", h->good ? 1 : 0,
					 (long long)h->created, h->text);
	else
		return 0;
	if (n <= 0 || (size_t)n >= size)
	{
		CE_CodeWipe(out, size);
		return 0;
	}
	return (size_t)n;
}

static int LegacyCE_HandoffParse(const char *text, size_t size, int64_t now, CE_Handoff *h, const char **error)
{
	const char *why = NULL;
	static const char *const keys[] = {"kind", "role", "endpoint", "secret", "level", "nonce", "created", "args", "good", "text"};
	const int kKeys = int(sizeof keys / sizeof keys[0]);
	char value[kKeys][1024];
	bool seen[kKeys] = {};
	CE_Handoff r;
	memset(&r, 0, sizeof r);
	const char *p = text, *end = text + size;
	const char header[] = "onslaught-ce handoff 1\n";
	if (!text || size > CE_HANDOFF_MAX_BYTES || memchr(text, 0, size))
		why = "the handoff file is too large or not text";
	else if (size < sizeof header - 1 || memcmp(text, header, sizeof header - 1))
		why = "the handoff file has no Onslaught CE header";
	else
		p += sizeof header - 1;
	while (!why && p < end)
	{
		const char *nl = (const char *)memchr(p, '\n', size_t(end - p));
		if (!nl)
		{
			why = "the handoff file is incomplete";
			break;
		}
		const char *eq = (const char *)memchr(p, '=', size_t(nl - p));
		int k = -1;
		if (eq)
			for (int i = 0; i < kKeys; i++)
				if (size_t(eq - p) == strlen(keys[i]) && !memcmp(p, keys[i], size_t(eq - p)))
					k = i;
		if (k < 0)
			why = "the handoff file has an unknown line";
		else if (seen[k])
			why = "the handoff file repeats a line";
		else if (size_t(nl - eq - 1) >= sizeof value[k])
			why = "a handoff line is too long";
		else
		{
			seen[k] = true;
			memcpy(value[k], eq + 1, size_t(nl - eq - 1));
			value[k][nl - eq - 1] = 0;
			if (!Printable(value[k]))
				why = "a handoff line has a control character";
		}
		p = nl + 1;
	}
	// 0 kind, 1 role, 2 endpoint, 3 secret, 4 level, 5 nonce, 6 created, 7 args, 8 good, 9 text
	if (!why)
	{
		bool launch = seen[0] && !strcmp(value[0], "launch"), outcome = seen[0] && !strcmp(value[0], "outcome");
		char *e = NULL;
		long long created = seen[6] ? strtoll(value[6], &e, 10) : 0;
		if (!launch && !outcome)
			why = "the handoff file has no known kind";
		else if (!seen[6] || !*value[6] || *e)
			why = "the handoff file has no creation time";
		else if (created > now + 5 || now - created > CE_HANDOFF_MAX_AGE)
			why = "the handoff file is too old (over 60 seconds)";
		else if (launch)
		{
			unsigned long level = seen[4] ? strtoul(value[4], &e, 10) : 0;
			if (!seen[1] || !seen[2] || !seen[3] || !seen[4] || !seen[5] || !seen[7] || seen[8] || seen[9])
				why = "the launch handoff is missing a line";
			else if (strcmp(value[1], "host") && strcmp(value[1], "guest"))
				why = "the launch handoff has no role";
			else if (!Endpoint(value[2]) || strlen(value[2]) >= sizeof r.endpoint)
				why = "the launch handoff has no valid address";
			else if (!Unhex(value[3], r.secret, 32))
				why = "the launch handoff has no valid secret";
			else if (*e || level > 0xffffffffUL || !(level == 852 || CE_CompatibilityLockstepLevel((uint32_t)level)))
				why = "the launch handoff names a level that cannot be played over the network";
			else if (!Unhex(value[5], r.nonce, 16))
				why = "the launch handoff has no valid start number";
			else
			{
				r.kind = CE_HANDOFF_LAUNCH;
				r.role = strcmp(value[1], "host") ? 2 : 1;
				memcpy(r.endpoint, value[2], strlen(value[2]) + 1);
				r.level = (uint32_t)level;
				memcpy(r.args, value[7], strlen(value[7]) + 1);
			}
		}
		else
		{
			if (!seen[8] || !seen[9] || seen[1] || seen[2] || seen[3] || seen[4] || seen[5] || seen[7])
				why = "the outcome handoff is missing a line";
			else if (strcmp(value[8], "0") && strcmp(value[8], "1"))
				why = "the outcome handoff has no result";
			else if (strlen(value[9]) >= sizeof r.text)
				why = "the outcome handoff's text is too long";
			else
			{
				r.kind = CE_HANDOFF_OUTCOME;
				r.good = value[8][0] == '1';
				memcpy(r.text, value[9], strlen(value[9]) + 1);
			}
		}
		r.created = created;
	}
	CE_CodeWipe(value[3], sizeof value[3]);
	if (why)
	{
		CE_CodeWipe(&r, sizeof r);
		if (h)
			memset(h, 0, sizeof *h);
		if (error)
			*error = why;
		return 0;
	}
	if (h)
		*h = r;
	CE_CodeWipe(&r, sizeof r);
	if (error)
		*error = NULL;
	return 1;
}

#ifdef _WIN32
static bool Wide(const char *path, WCHAR *out, int n)
{
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, n) > 0;
}
#endif

int CE_HandoffWrite(const char *path, const CE_Handoff *h)
{
	// Occupied names are refused; failed writes leave the path in place.
	// Handle cleanup is best-effort; an incomplete or complete file can remain.
	char text[CE_HANDOFF_MAX_BYTES];
	size_t n = CE_HandoffFormat(h, text, sizeof text);
	if (!n || !path)
		return 0;
	bool ok = false;
#ifdef _WIN32
	WCHAR w[1024];
	if (Wide(path, w, 1024))
	{
		// The user folder (%APPDATA%) carries the account's own access control; Windows has no mode bits to set.
		HANDLE f = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (f != INVALID_HANDLE_VALUE)
		{
			DWORD wrote = 0;
			ok = WriteFile(f, text, (DWORD)n, &wrote, NULL) && wrote == n;
			if (!ok) {
				LARGE_INTEGER zero = {};
				if (SetFilePointerEx(f, zero, NULL, FILE_BEGIN))
					(void)SetEndOfFile(f);
			}
			ok = CloseHandle(f) && ok;
		}
	}
#else
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd >= 0)
	{
		ok = fchmod(fd, 0600) == 0 && write(fd, text, n) == (ssize_t)n;
			if (!ok)
				(void)ftruncate(fd, 0);
		ok = close(fd) == 0 && ok;
	}
#endif
	CE_CodeWipe(text, sizeof text);
	return ok ? 1 : 0;
}

int CE_HandoffRead(const char *path, int64_t now, CE_Handoff *h, const char **error)
{
	char text[CE_HANDOFF_MAX_BYTES + 1];
	size_t n = 0;
	const char *why = NULL;
	if (h)
		memset(h, 0, sizeof *h);
#ifdef _WIN32
	WCHAR w[1024];
	HANDLE f = (path && Wide(path, w, 1024)) ? CreateFileW(w, GENERIC_READ, 0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL) : INVALID_HANDLE_VALUE;
	if (f == INVALID_HANDLE_VALUE)
		why = "the handoff file is missing";
	else
	{
		BY_HANDLE_FILE_INFORMATION info;
		FILETIME ft;
		if (!GetFileInformationByHandle(f, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
			why = "the handoff file is not a plain file";
		else if (info.nFileSizeHigh || info.nFileSizeLow > CE_HANDOFF_MAX_BYTES)
			why = "the handoff file is too large";
		else
		{
			ft = info.ftLastWriteTime;
			int64_t written = (int64_t)((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ULL) - 11644473600LL;
			DWORD got = 0;
			if (written > now + 5 || now - written > CE_HANDOFF_MAX_AGE)
				why = "the handoff file is too old (over 60 seconds)";
			else if (!ReadFile(f, text, CE_HANDOFF_MAX_BYTES + 1, &got, NULL) || got > CE_HANDOFF_MAX_BYTES)
				why = "the handoff file could not be read";
			else
				n = got;
		}
		CloseHandle(f);
		DeleteFileW(w);
	}
#else
	int fd = path ? open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK) : -1;
	if (fd < 0)
		why = errno == ELOOP ? "the handoff file is a link" : "the handoff file is missing";
	else
	{
		// Removed first: whatever it holds is used once at most.
		unlink(path);
		struct statx st;
		memset(&st, 0, sizeof st);
		if (statx(fd, "", AT_EMPTY_PATH, STATX_TYPE | STATX_MODE | STATX_UID | STATX_SIZE | STATX_MTIME, &st) ||
			!S_ISREG(st.stx_mode))
			why = "the handoff file is not a plain file";
		else if (st.stx_uid != getuid())
			why = "the handoff file belongs to another user";
		else if ((st.stx_mode & 0777) != 0600)
			why = "the handoff file can be read by others (it must be private, mode 600)";
		else if (st.stx_size > CE_HANDOFF_MAX_BYTES)
			why = "the handoff file is too large";
		else if ((int64_t)st.stx_mtime.tv_sec > now + 5 || now - (int64_t)st.stx_mtime.tv_sec > CE_HANDOFF_MAX_AGE)
			why = "the handoff file is too old (over 60 seconds)";
		else
		{
			ssize_t got;
			while ((got = read(fd, text + n, CE_HANDOFF_MAX_BYTES + 1 - n)) > 0 && n < CE_HANDOFF_MAX_BYTES + 1)
				n += (size_t)got;
			if (got < 0 || n > CE_HANDOFF_MAX_BYTES)
				why = "the handoff file could not be read";
		}
		close(fd);
	}
#endif
	int ok = 0;
	if (!why)
		ok = CE_HandoffParse(text, n, now, h, &why);
	CE_CodeWipe(text, sizeof text);
	if (error)
		*error = why;
	return ok;
}

}


namespace {
uint64_t CampaignNoncePart(const uint8_t *nonce) {
    uint64_t value = 0;
    for (unsigned i = 0; i != sizeof(value); ++i)
        value = (value << 8) | nonce[i];
    return value;
}

size_t CampaignSuffix(const CE_CampaignLaunch &campaign, char *out, size_t size) {
    int count = snprintf(out, size,
        "campaign_version=1\ncampaign_session=%llu\ncampaign_attempt=%llu\ncampaign_mission=%u\n",
        (unsigned long long)campaign.session,
        (unsigned long long)campaign.attempt, (unsigned)campaign.mission);
    return count > 0 && size_t(count) < size ? size_t(count) : 0;
}

int CampaignParseRefuse(CE_Handoff *handoff, const char **error) {
    if (handoff) CE_CodeWipe(handoff, sizeof(*handoff));
    if (error) *error = "The campaign launch descriptor is invalid.";
    return 0;
}
}

int CE_HandoffCampaignValid(const CE_Handoff *handoff) {
    if (!handoff) return 0;
    const CE_CampaignLaunch &campaign = handoff->campaign;
    if (!campaign.version)
        return !campaign.session && !campaign.attempt && !campaign.mission;
    if (campaign.version != 1 || handoff->kind != CE_HANDOFF_LAUNCH ||
        (handoff->role != CE_NET_HOST && handoff->role != CE_NET_GUEST) ||
        !CE_CompatibilityLockstepLevel(handoff->level) ||
        campaign.mission != handoff->level || !campaign.session || !campaign.attempt)
        return 0;
    static_assert(sizeof(handoff->nonce) == 2 * sizeof(uint64_t), "launch nonce identity shape");
    return campaign.session == CampaignNoncePart(handoff->nonce) &&
        campaign.attempt == CampaignNoncePart(handoff->nonce + sizeof(uint64_t));
}

int CE_HandoffCampaignSet(CE_Handoff *handoff) {
    if (!handoff) return 0;
    CE_CodeWipe(&handoff->campaign, sizeof(handoff->campaign));
    handoff->campaign.version = 1;
    handoff->campaign.session = CampaignNoncePart(handoff->nonce);
    handoff->campaign.attempt = CampaignNoncePart(handoff->nonce + sizeof(uint64_t));
    handoff->campaign.mission = handoff->level;
    if (CE_HandoffCampaignValid(handoff)) return 1;
    CE_CodeWipe(&handoff->campaign, sizeof(handoff->campaign));
    return 0;
}

size_t LaunchCE_HandoffFormat(const CE_Handoff *handoff, char *out, size_t size) {
    if (!CE_HandoffCampaignValid(handoff)) return 0;
    size_t written = LegacyCE_HandoffFormat(handoff, out, size);
    if (!written || !handoff->campaign.version) return written;
    if (written >= size || out[written - 1] != '\n') return 0;
    size_t added = CampaignSuffix(handoff->campaign, out + written, size - written);
    if (!added || written + added >= CE_HANDOFF_MAX_BYTES) return 0;
    return written + added;
}

int LaunchCE_HandoffParse(const char *text, size_t size, int64_t now,
                    CE_Handoff *handoff, const char **error) {
    if (!text || !handoff || !size || size >= CE_HANDOFF_MAX_BYTES)
        return CampaignParseRefuse(handoff, error);
    const char marker[] = "\ncampaign_version=";
    const char *suffix = NULL;
    for (size_t i = 0; i + sizeof(marker) - 1 <= size; ++i) {
        if (!memcmp(text + i, marker, sizeof(marker) - 1)) {
            if (suffix) return CampaignParseRefuse(handoff, error);
            suffix = text + i + 1;
        }
    }
    if (!suffix) return LegacyCE_HandoffParse(text, size, now, handoff, error);
    const size_t prefix_size = size_t(suffix - text);
    if (!LegacyCE_HandoffParse(text, prefix_size, now, handoff, error) ||
        !CE_HandoffCampaignSet(handoff))
        return CampaignParseRefuse(handoff, error);
    char canonical[192];
    const size_t expected = CampaignSuffix(handoff->campaign, canonical, sizeof(canonical));
    const bool same = expected && expected == size - prefix_size &&
        !memcmp(suffix, canonical, expected);
    CE_CodeWipe(canonical, sizeof(canonical));
    if (!same) return CampaignParseRefuse(handoff, error);
    return 1;
}

static int CampaignReturnValid(const CE_Handoff *handoff) {
    return handoff && handoff->kind == CE_HANDOFF_OUTCOME &&
        handoff->campaign.version == 1 && handoff->campaign.session &&
        handoff->campaign.attempt && handoff->campaign.mission &&
        CE_CompatibilityLockstepLevel(handoff->campaign.mission) &&
        (handoff->good == 0 || handoff->good == 1);
}



static size_t CampaignReturnSuffix(const CE_CampaignLaunch &campaign,
                                   char *out, size_t capacity) {
    int count = snprintf(out, capacity,
        "\ncampaign_return_version=1\ncampaign_session=%llu\n"
        "campaign_attempt=%llu\ncampaign_mission=%u\n",
        (unsigned long long)campaign.session, (unsigned long long)campaign.attempt,
        (unsigned)campaign.mission);
    return count > 0 && (size_t)count < capacity ? (size_t)count : 0;
}

extern "C" size_t CE_HandoffFormat(const CE_Handoff *handoff, char *out, size_t capacity) {
    if (!handoff || handoff->kind != CE_HANDOFF_OUTCOME || !handoff->campaign.version)
        return LaunchCE_HandoffFormat(handoff, out, capacity);
    if (!CampaignReturnValid(handoff)) return 0;
    size_t count = LegacyCE_HandoffFormat(handoff, out, capacity);
    if (!count || count >= capacity) return 0;
    size_t suffix = CampaignReturnSuffix(handoff->campaign, out + count, capacity - count);
    if (!suffix || count + suffix > CE_HANDOFF_MAX_BYTES) return 0;
    return count + suffix;
}

static bool ReturnNumber(const char *&at, const char *end, const char *key,
                         uint64_t maximum, uint64_t *value) {
    size_t keySize = strlen(key);
    if ((size_t)(end - at) < keySize || memcmp(at, key, keySize) != 0) return false;
    at += keySize;
    if (at == end || *at < '0' || *at > '9') return false;
    uint64_t number = 0;
    while (at != end && *at >= '0' && *at <= '9') {
        unsigned digit = (unsigned)(*at++ - '0');
        if (number > maximum / 10 ||
            (number == maximum / 10 && digit > maximum % 10)) return false;
        number = number * 10 + digit;
    }
    if (at == end || *at++ != '\n') return false;
    *value = number;
    return true;
}

extern "C" int CE_HandoffParse(const char *text, size_t size, int64_t now,
                                CE_Handoff *handoff, const char **error) {
    const char marker[] = "\ncampaign_return_version=";
    if (!text || !handoff || size > CE_HANDOFF_MAX_BYTES)
        return CampaignParseRefuse(handoff, error);
    size_t split = size;
    for (size_t i = 0; i + sizeof(marker) - 1 <= size; ++i)
        if (memcmp(text + i, marker, sizeof(marker) - 1) == 0) { split = i; break; }
    if (split == size) return LaunchCE_HandoffParse(text, size, now, handoff, error);
    if (!LegacyCE_HandoffParse(text, split, now, handoff, error) ||
        handoff->kind != CE_HANDOFF_OUTCOME)
        return CampaignParseRefuse(handoff, error);
    const char *at = text + split + 1;
    const char *end = text + size;
    uint64_t version = 0, session = 0, attempt = 0, mission = 0;
    if (!ReturnNumber(at, end, "campaign_return_version=", UINT32_MAX, &version) ||
        !ReturnNumber(at, end, "campaign_session=", UINT64_MAX, &session) ||
        !ReturnNumber(at, end, "campaign_attempt=", UINT64_MAX, &attempt) ||
        !ReturnNumber(at, end, "campaign_mission=", UINT32_MAX, &mission) || at != end)
        return CampaignParseRefuse(handoff, error);
    handoff->campaign = {(uint32_t)version, session, attempt, (uint32_t)mission};
    if (!CampaignReturnValid(handoff))
        return CampaignParseRefuse(handoff, error);
    char canonical[CE_HANDOFF_MAX_BYTES + 1];
    size_t count = CE_HandoffFormat(handoff, canonical, sizeof(canonical));
    bool exact = count == size && memcmp(canonical, text, size) == 0;
    CE_CodeWipe(canonical, sizeof(canonical));
    if (!exact) return CampaignParseRefuse(handoff, error);
    return 1;
}


namespace {
static const size_t OwnedTextCapacity = CE_HANDOFF_MAX_BYTES;

static bool OwnerValid(const CE_HandoffOwner *owner) {
    return owner && owner->version == CE_HANDOFF_OWNER_VERSION;
}
static void OwnerDigest(const void *data, size_t size, uint8_t digest[32]) {
    SHA256_CTX context;
    sha256_init(&context);
    sha256_update(&context, data, size);
    sha256_final(&context, digest);
    CE_CodeWipe(&context, sizeof context);
}
static bool OwnerIdentityEqual(const CE_HandoffOwner &a, const CE_HandoffOwner &b) {
    return a.version == b.version && a.volume == b.volume &&
           a.file == b.file && a.stamp == b.stamp;
}
static bool OwnerDigestEqual(const uint8_t *a, const uint8_t *b) {
    unsigned difference = 0;
    for (size_t i = 0; i != 32; ++i) difference |= a[i] ^ b[i];
    return difference == 0;
}

#ifdef _WIN32
typedef HANDLE OwnedFile;
static const OwnedFile InvalidOwnedFile = INVALID_HANDLE_VALUE;
static OwnedFile OwnerOpen(const char *path, bool create) {
    return CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, 0,
                       create ? CREATE_NEW : OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
}
static bool OwnerInfo(OwnedFile file, CE_HandoffOwner &owner, uint64_t &size) {
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1) return false;
    size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size >= OwnedTextCapacity) return false;
    memset(&owner, 0, sizeof owner);
    owner.version = CE_HANDOFF_OWNER_VERSION;
    owner.volume = info.dwVolumeSerialNumber;
    owner.file = (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    owner.stamp = (uint64_t(info.ftCreationTime.dwHighDateTime) << 32) |
                  info.ftCreationTime.dwLowDateTime;
    return true;
}
static bool OwnerWriteBytes(OwnedFile file, const char *data, size_t size) {
    DWORD written = 0;
    return WriteFile(file, data, DWORD(size), &written, 0) && written == size &&
           FlushFileBuffers(file);
}
static bool OwnerReadBytes(OwnedFile file, char *data, size_t size) {
    DWORD got = 0;
    if (!ReadFile(file, data, DWORD(size), &got, 0) || got != size) return false;
    char extra;
    return ReadFile(file, &extra, 1, &got, 0) && got == 0;
}
static bool OwnerClearFile(OwnedFile file) {
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    return SetFilePointerEx(file, zero, 0, FILE_BEGIN) && SetEndOfFile(file) &&
           FlushFileBuffers(file);
}
static bool OwnerClose(OwnedFile file) { return CloseHandle(file) != 0; }
#else
typedef int OwnedFile;
static const OwnedFile InvalidOwnedFile = -1;
static OwnedFile OwnerOpen(const char *path, bool create) {
    const int flags = O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
    return open(path, flags | (create ? O_CREAT | O_EXCL : 0), 0600);
}
static bool OwnerInfo(OwnedFile file, CE_HandoffOwner &owner, uint64_t &size) {
    struct stat info;
    if (fstat(file, &info) || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
        info.st_nlink != 1 || (info.st_mode & 0777) != 0600 || info.st_size < 0 ||
        uint64_t(info.st_size) >= OwnedTextCapacity) return false;
    size = uint64_t(info.st_size);
    memset(&owner, 0, sizeof owner);
    owner.version = CE_HANDOFF_OWNER_VERSION;
    owner.volume = uint64_t(info.st_dev);
    owner.file = uint64_t(info.st_ino);
#ifdef __APPLE__
    owner.stamp = uint64_t(info.st_ctimespec.tv_sec) * 1000000000ull +
                  uint64_t(info.st_ctimespec.tv_nsec);
#else
    owner.stamp = uint64_t(info.st_ctim.tv_sec) * 1000000000ull +
                  uint64_t(info.st_ctim.tv_nsec);
#endif
    return true;
}
static bool OwnerWriteBytes(OwnedFile file, const char *data, size_t size) {
    ssize_t written;
    do { written = write(file, data, size); } while (written < 0 && errno == EINTR);
    return written == ssize_t(size) && fsync(file) == 0;
}
static bool OwnerReadBytes(OwnedFile file, char *data, size_t size) {
    size_t offset = 0;
    while (offset != size) {
        ssize_t got = read(file, data + offset, size - offset);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        offset += size_t(got);
    }
    char extra;
    ssize_t got;
    do { got = read(file, &extra, 1); } while (got < 0 && errno == EINTR);
    return got == 0;
}
static bool OwnerClearFile(OwnedFile file) {
    return ftruncate(file, 0) == 0 && fsync(file) == 0;
}
static bool OwnerClose(OwnedFile file) { return close(file) == 0; }
#endif

static int OwnerUse(const char *path, const CE_HandoffOwner *owner, int64_t now,
                    CE_Handoff *handoff, const char **error, bool parse) {
    if (handoff) memset(handoff, 0, sizeof *handoff);
    if (error) *error = "restart ownership was not verified";
    if (!path || !*path || !OwnerValid(owner) || (parse && !handoff)) return 0;
    OwnedFile file = OwnerOpen(path, false);
    if (file == InvalidOwnedFile) return 0;
    char text[OwnedTextCapacity] = {};
    CE_HandoffOwner before = {}, after = {};
    uint64_t size = 0, after_size = 0;
    bool known = OwnerInfo(file, before, size) && OwnerIdentityEqual(before, *owner) &&
                 OwnerReadBytes(file, text, size_t(size)) &&
                 OwnerInfo(file, after, after_size) && size == after_size &&
                 OwnerIdentityEqual(after, *owner);
    if (known) {
        OwnerDigest(text, size_t(size), before.digest);
        known = OwnerDigestEqual(before.digest, owner->digest);
    }
    CE_Handoff admitted = {};
    const char *parse_error = 0;
    bool parsed = known && (!parse || CE_HandoffParse(text, size_t(size), now,
                                                     &admitted, &parse_error));
    // Ownership is independent of age. A known stale record is still cleared.
    bool cleared = known && OwnerClearFile(file);
    bool closed = OwnerClose(file);
    bool ok = parsed && cleared && closed;
    if (ok && handoff) *handoff = admitted;
    if (error) {
        if (ok) *error = 0;
        else if (known && (!cleared || !closed)) *error = "restart cleanup failed";
        else if (known && !parsed && parse_error) *error = parse_error;
    }
    CE_CodeWipe(text, sizeof text);
    CE_CodeWipe(&before, sizeof before);
    CE_CodeWipe(&after, sizeof after);
    CE_CodeWipe(&admitted, sizeof admitted);
    return ok ? 1 : 0;
}

static bool OwnerNumber(const char *&cursor, const char *end, uint64_t &value) {
    value = 0;
    const char *start = cursor;
    while (cursor != end && *cursor >= '0' && *cursor <= '9') {
        unsigned digit = unsigned(*cursor++ - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return cursor != start && cursor != end && *cursor++ == ',';
}
}

int CE_HandoffOwnedWrite(const char *path, const CE_Handoff *handoff,
                         CE_HandoffOwner *owner, const char **error) {
    if (owner) memset(owner, 0, sizeof *owner);
    if (error) *error = "could not stage restart state";
    if (!path || !*path || !handoff || !owner) return 0;
    char text[OwnedTextCapacity] = {};
    size_t size = CE_HandoffFormat(handoff, text, sizeof text);
    if (!size) return 0;
    OwnedFile file = OwnerOpen(path, true);
    if (file == InvalidOwnedFile) { CE_CodeWipe(text, sizeof text); return 0; }
    bool mode_ok = true;
#ifndef _WIN32
    mode_ok = fchmod(file, 0600) == 0;
#endif
    CE_HandoffOwner captured = {};
    uint64_t actual_size = 0;
    bool ok = mode_ok && OwnerWriteBytes(file, text, size) &&
              OwnerInfo(file, captured, actual_size) && actual_size == size;
    if (ok) OwnerDigest(text, size, captured.digest);
    else (void)OwnerClearFile(file);
    bool closed = OwnerClose(file);
    ok = ok && closed;
    if (ok) { *owner = captured; if (error) *error = 0; }
    CE_CodeWipe(text, sizeof text);
    CE_CodeWipe(&captured, sizeof captured);
    return ok ? 1 : 0;
}

int CE_HandoffOwnedRead(const char *path, const CE_HandoffOwner *owner, int64_t now,
                        CE_Handoff *handoff, const char **error) {
    return OwnerUse(path, owner, now, handoff, error, true);
}

int CE_HandoffOwnedClear(const char *path, const CE_HandoffOwner *owner,
                         const char **error) {
    return OwnerUse(path, owner, 0, 0, error, false);
}

size_t CE_HandoffOwnerFormat(const CE_HandoffOwner *owner, char *out, size_t capacity) {
    if (out && capacity) out[0] = 0;
    if (!OwnerValid(owner) || !out || !capacity) return 0;
    char text[256];
    int size = snprintf(text, sizeof text, "%u,%llu,%llu,%llu,", owner->version,
                        (unsigned long long)owner->volume, (unsigned long long)owner->file,
                        (unsigned long long)owner->stamp);
    if (size < 0 || size_t(size) + 64 >= sizeof text || size_t(size) + 64 >= capacity) return 0;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i != 32; ++i) {
        text[size++] = hex[owner->digest[i] >> 4];
        text[size++] = hex[owner->digest[i] & 15];
    }
    text[size] = 0;
    memcpy(out, text, size_t(size) + 1);
    CE_CodeWipe(text, sizeof text);
    return size_t(size);
}

int CE_HandoffOwnerParse(const char *text, size_t size, CE_HandoffOwner *owner) {
    if (owner) memset(owner, 0, sizeof *owner);
    if (!text || !owner || !size || size >= 256 || memchr(text, 0, size)) return 0;
    CE_HandoffOwner parsed = {};
    const char *cursor = text, *end = text + size;
    uint64_t version = 0;
    if (!OwnerNumber(cursor, end, version) || version != CE_HANDOFF_OWNER_VERSION ||
        !OwnerNumber(cursor, end, parsed.volume) || !OwnerNumber(cursor, end, parsed.file) ||
        !OwnerNumber(cursor, end, parsed.stamp) || end - cursor != 64) return 0;
    parsed.version = uint32_t(version);
    for (size_t i = 0; i != 32; ++i) {
        unsigned byte = 0;
        for (int digit = 0; digit != 2; ++digit) {
            char c = *cursor++;
            unsigned value = c >= '0' && c <= '9' ? unsigned(c - '0') :
                             c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10) : 16;
            if (value == 16) return 0;
            byte = byte * 16 + value;
        }
        parsed.digest[i] = uint8_t(byte);
    }
    char canonical[256];
    size_t n = CE_HandoffOwnerFormat(&parsed, canonical, sizeof canonical);
    bool ok = n == size && memcmp(canonical, text, size) == 0;
    if (ok) *owner = parsed;
    CE_CodeWipe(canonical, sizeof canonical);
    CE_CodeWipe(&parsed, sizeof parsed);
    return ok ? 1 : 0;
}
