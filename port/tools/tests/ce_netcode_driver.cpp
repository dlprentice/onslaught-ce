// SPDX-License-Identifier: GPL-3.0-only
// The join code and launch handoff (port/platform/common/ce_netcode.cpp), exercised directly: no game, window or
// network. Run by port/tools/ce_netcode_tests.py, which also checks the printed known answers and kills mutants.
#include "ce_netcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

extern "C" int CE_NetGenerateSecret(uint8_t out[32])
{
	static uint64_t s = 0x9e3779b97f4a7c15ULL;
	for (int i = 0; i < 32; i++)
	{
		s ^= s << 13; s ^= s >> 7; s ^= s << 17;
		out[i] = uint8_t(s);
	}
	return 1;
}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static uint64_t rnd()
{
	static uint64_t s = 88172645463325252ULL;
	s ^= s << 13; s ^= s >> 7; s ^= s << 17;
	return s;
}

static void hex(const uint8_t *p, int n, char *out)
{
	for (int i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", p[i]);
}

static const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static std::string symbols(const char *code)
{
	std::string s;
	for (const char *p = code; *p; p++) if (*p != '-') s += *p;
	return s;
}

static void codes()
{
	// round trips, the shown form
	for (int t = 0; t < 2000; t++)
	{
		CE_JoinCode c = {rnd() & CE_CODE_KEY_MASK, uint32_t(rnd()) | 1, uint16_t(rnd() | 1)};
		char text[CE_CODE_TEXT];
		CHECK(CE_CodeEncode(&c, text, sizeof text) == 1, "encode");
		CHECK(strlen(text) == 28, "length %zu", strlen(text));
		for (int i = 0; i < 28; i++)
			CHECK((i % 5 == 4) == (text[i] == '-'), "dash at %d in %s", i, text);
		CE_JoinCode d = {};
		CHECK(CE_CodeDecode(text, &d) == CE_CODE_OK && d.key == c.key && d.ipv4 == c.ipv4 && d.port == c.port, "round trip %s", text);
	}
	CE_JoinCode c = {0x0123456789abcdefULL & CE_CODE_KEY_MASK, 0xc0a80114, 27652};
	char text[CE_CODE_TEXT];
	CE_CodeEncode(&c, text, sizeof text);
	printf("code %s\n", text);
	std::string s = symbols(text);
	// typing that is forgiven: case, no dashes, spaces, I/L for 1 and O for 0
	{
		std::string t;
		for (char ch : s) t += char(tolower(ch));
		CE_JoinCode d = {};
		CHECK(CE_CodeDecode(t.c_str(), &d) == CE_CODE_OK && d.key == c.key, "lower case");
		std::string spaced = "  " + s.substr(0, 7) + " - " + s.substr(7) + "  ";
		CHECK(CE_CodeDecode(spaced.c_str(), &d) == CE_CODE_OK && d.key == c.key, "spaces and dashes");
	}
	for (int t = 0; t < 400; t++)
	{
		CE_JoinCode r = {rnd() & CE_CODE_KEY_MASK, uint32_t(rnd()) | 1, uint16_t(rnd() | 1)};
		char rt[CE_CODE_TEXT];
		CE_CodeEncode(&r, rt, sizeof rt);
		std::string m = symbols(rt);
		bool any = false;
		for (char &ch : m)
		{
			if (ch == '1') { ch = (rnd() & 1) ? 'I' : 'l'; any = true; }
			else if (ch == '0') { ch = (rnd() & 1) ? 'O' : 'o'; any = true; }
		}
		CE_JoinCode d = {};
		CHECK(CE_CodeDecode(m.c_str(), &d) == CE_CODE_OK && d.key == r.key && d.ipv4 == r.ipv4 && d.port == r.port,
			  "I/L/O read as 1/0 (%s, %d)", m.c_str(), any);
	}
	// every single wrong symbol, in every position, is caught (never a different valid code)
	int singles = 0;
	for (int t = 0; t < 40; t++)
	{
		CE_JoinCode r = {rnd() & CE_CODE_KEY_MASK, uint32_t(rnd()) | 1, uint16_t(rnd() | 1)};
		char rt[CE_CODE_TEXT];
		CE_CodeEncode(&r, rt, sizeof rt);
		std::string m = symbols(rt);
		for (int i = 0; i < 23; i++)
			for (int a = 0; a < 32; a++)
			{
				if (kAlphabet[a] == m[i]) continue;
				std::string w = m;
				w[i] = kAlphabet[a];
				int got = CE_CodeDecode(w.c_str(), NULL);
				CHECK(got != CE_CODE_OK, "single substitution at %d accepted", i);
				CHECK(got == CE_CODE_CHECK, "single substitution at %d gave %d, not a typo", i, got);
				singles++;
			}
	}
	// every swap of two different symbols, any two positions, the check symbol's included
	int swaps = 0;
	for (int t = 0; t < 200; t++)
	{
		CE_JoinCode r = {rnd() & CE_CODE_KEY_MASK, uint32_t(rnd()) | 1, uint16_t(rnd() | 1)};
		char rt[CE_CODE_TEXT];
		CE_CodeEncode(&r, rt, sizeof rt);
		std::string m = symbols(rt);
		for (int i = 0; i < 23; i++)
			for (int j = i + 1; j < 23; j++)
			{
				if (m[i] == m[j]) continue;
				std::string w = m;
				std::swap(w[i], w[j]);
				CHECK(CE_CodeDecode(w.c_str(), NULL) != CE_CODE_OK, "swap %d,%d accepted (%s)", i, j, w.c_str());
				swaps++;
			}
	}
	printf("singles %d swaps %d\n", singles, swaps);
	CHECK(CE_CodeDecode("", NULL) == CE_CODE_EMPTY, "empty");
	CHECK(CE_CodeDecode(" - ", NULL) == CE_CODE_EMPTY, "dashes only");
	CHECK(CE_CodeDecode(s.substr(0, 22).c_str(), NULL) == CE_CODE_SHORT, "short");
	CHECK(CE_CodeDecode((s + "0").c_str(), NULL) == CE_CODE_LONG, "long");
	CHECK(CE_CodeDecode((s + s).c_str(), NULL) == CE_CODE_LONG, "much too long");
	for (const char *bad : {"U", "_", "@", "`", "*", "~", "$", "="})
	{
		std::string w = s;
		w[5] = bad[0];
		CHECK(CE_CodeDecode(w.c_str(), NULL) == CE_CODE_BAD_CHAR, "bad char %s", bad);
	}
	// a valid check over nonzero pad bits: not a code
	{
		std::string w = s;
		int v = int(strchr(kAlphabet, w[21]) - kAlphabet) ^ 1;
		w[21] = kAlphabet[v];
		bool found = false;
		for (int a = 0; a < 32 && !found; a++)
		{
			w[22] = kAlphabet[a];
			int got = CE_CodeDecode(w.c_str(), NULL);
			if (got != CE_CODE_CHECK) { CHECK(got == CE_CODE_BAD_PAD, "pad gave %d", got); found = true; }
		}
		CHECK(found, "no check symbol fits the padded code");
	}
	// encoding refuses what no code can hold
	CE_JoinCode big = {CE_CODE_KEY_MASK + 1, 1, 1}, noip = {1, 0, 1}, noport = {1, 1, 0};
	CHECK(!CE_CodeEncode(&big, text, sizeof text) && !CE_CodeEncode(&noip, text, sizeof text) && !CE_CodeEncode(&noport, text, sizeof text), "encode refusals");
	CHECK(!CE_CodeEncode(&c, text, 28), "small buffer");
	// a code whose address is 0.0.0.0, with its check symbol right: refused (bit 91, the address's last, is in symbol 18)
	{
		CE_JoinCode z = {5, 1, 7};
		CE_CodeEncode(&z, text, sizeof text);
		std::string w = symbols(text);
		int v = int(strchr(kAlphabet, w[18]) - kAlphabet) ^ 8;
		w[18] = kAlphabet[v];
		int got = CE_CODE_CHECK;
		for (int a = 0; a < 32 && got == CE_CODE_CHECK; a++)
		{
			w[22] = kAlphabet[a];
			got = CE_CodeDecode(w.c_str(), NULL);
		}
		CHECK(got == CE_CODE_BAD_ADDRESS, "address 0.0.0.0 gave %d", got);
	}
	// the problems are written in the game font's characters
	for (int r = CE_CODE_OK; r <= CE_CODE_BAD_ADDRESS + 1; r++)
		for (const char *p = CE_CodeProblem(r); *p; p++)
			CHECK(!strchr("_`~\\$@[]^{|}", *p), "problem %d has a character the font lacks", r);
	// keys: 60 bits, from the random source
	uint64_t k1 = 0, k2 = 0;
	CHECK(CE_CodeNewKey(&k1) && CE_CodeNewKey(&k2) && k1 != k2 && !(k1 & ~CE_CODE_KEY_MASK) && !(k2 & ~CE_CODE_KEY_MASK), "keys");
}

static void secrets()
{
	uint8_t a[32], b[32], s1[32], s2[32];
	char h[65];
	uint64_t key = 0x0123456789abcdefULL & CE_CODE_KEY_MASK;
	CE_CodeLobbySecret(key, a);
	hex(a, 32, h);
	printf("kat lobby %016llx %s\n", (unsigned long long)key, h);
	CE_CodeLobbySecret(key ^ 1, b);
	CHECK(memcmp(a, b, 32), "different keys, same secret");
	uint8_t n1[16], n2[16];
	for (int i = 0; i < 16; i++) { n1[i] = uint8_t(i); n2[i] = uint8_t(i); }
	n2[15] ^= 1;
	CE_CodeStartSecret(a, n1, s1);
	CE_CodeStartSecret(a, n2, s2);
	CHECK(memcmp(s1, s2, 32) && memcmp(s1, a, 32), "start secret depends on the nonce");
	hex(s1, 32, h);
	printf("kat start %s\n", h);
	CE_CodeLobbyDigest("abc123", "0.1.8", s1);
	hex(s1, 32, h);
	printf("kat digest %s\n", h);
	CE_CodeLobbyDigest("abc123", "0.1.9", s2);
	CHECK(memcmp(s1, s2, 32), "the digest depends on the version");
	CE_CodeLobbyDigest("abc124", "0.1.8", s2);
	CHECK(memcmp(s1, s2, 32), "the digest depends on the identity");
}

static std::string launchText(long long created, const char *extra = "", const char *drop = NULL, const char *replace = NULL,
							  const char *with = NULL)
{
	char t[2048];
	snprintf(t, sizeof t, "onslaught-ce handoff 1\nkind=launch\nrole=guest\nendpoint=127.0.0.1:39200\nsecret=%s\nlevel=852\n"
			 "nonce=000102030405060708090a0b0c0d0e0f\ncreated=%lld\nargs=-forcewindowed -res 640 480\n%s",
			 "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", created, extra);
	std::string s = t;
	if (drop)
	{
		size_t at = s.find(std::string("\n") + drop + "=");
		if (at != std::string::npos) s.erase(at + 1, s.find('\n', at + 1) - at);
	}
	if (replace)
	{
		size_t at = s.find(replace);
		if (at != std::string::npos) s.replace(at, strlen(replace), with);
	}
	return s;
}

static int parse(const std::string &t, long long now, CE_Handoff *h, const char **why)
{
	return CE_HandoffParse(t.data(), t.size(), now, h, why);
}

static void handoffs(const char *dir)
{
	long long now = 1800000000;
	CE_Handoff h;
	const char *why;
	CHECK(parse(launchText(now), now, &h, &why) == 1 && h.kind == CE_HANDOFF_LAUNCH && h.role == 2 && h.level == 852 &&
		  !strcmp(h.endpoint, "127.0.0.1:39200") && h.secret[1] == 0x11 && h.nonce[15] == 15 &&
		  !strcmp(h.args, "-forcewindowed -res 640 480"), "launch parses (%s)", why ? why : "");
	CHECK(parse(launchText(now - 60), now, &h, &why) == 1, "60 s old is accepted");
	CHECK(parse(launchText(now - 61), now, &h, &why) == 0 && h.kind == 0, "61 s old is refused");
	CHECK(parse(launchText(now + 30), now, &h, &why) == 0, "from the future");
	for (const char *field : {"kind", "role", "endpoint", "secret", "level", "nonce", "created", "args"})
		CHECK(parse(launchText(now, "", field), now, &h, &why) == 0, "missing %s accepted", field);
	CHECK(parse(launchText(now, "role=host\n"), now, &h, &why) == 0, "repeated line");
	CHECK(parse(launchText(now, "colour=red\n"), now, &h, &why) == 0, "unknown line");
	CHECK(parse(launchText(now, "text=x\n"), now, &h, &why) == 0, "outcome line in a launch");
	CHECK(parse(launchText(now, "", NULL, "role=guest", "role=spectator"), now, &h, &why) == 0, "role");
	CHECK(parse(launchText(now, "", NULL, "127.0.0.1:39200", "127.0.0.1:0"), now, &h, &why) == 0, "port 0");
	CHECK(parse(launchText(now, "", NULL, "127.0.0.1:39200", "127.0.0.256:39200"), now, &h, &why) == 0, "octet");
	CHECK(parse(launchText(now, "", NULL, "127.0.0.1:39200", "localhost:39200"), now, &h, &why) == 0, "name");
	CHECK(parse(launchText(now, "", NULL, "127.0.0.1:39200", "127.0.0.01:39200"), now, &h, &why) == 0, "leading zero");
	CHECK(parse(launchText(now, "", NULL, "secret=00112233", "secret=0011223"), now, &h, &why) == 0, "secret length");
	CHECK(parse(launchText(now, "", NULL, "secret=00112233", "secret=0011223G"), now, &h, &why) == 0, "secret digit");
	CHECK(parse(launchText(now, "", NULL, "level=852", "level=853"), now, &h, &why) == 0, "level");
	CHECK(parse(launchText(now, "", NULL, "level=852", "level=852x"), now, &h, &why) == 0, "level text");
	// the co-op maps the lobby can start (compatibility.h) are accepted, anything near them is not
	for (unsigned level : {100u, 856u, 858u})
	{
		char text[32];
		snprintf(text, sizeof text, "level=%u", level);
		CHECK(parse(launchText(now, "", NULL, "level=852", text), now, &h, &why) == 1 && h.level == level, "the co-op map %u is refused (%s)", level, why ? why : "");
	}
	for (const char *level : {"level=0", "level=99", "level=101", "level=851", "level=853", "level=855", "level=857", "level=859", "level=-856", "level=856 ", "level=0x358",
							  "level=4294968152", "level=18446744073709552472", "level="})
		CHECK(parse(launchText(now, "", NULL, "level=852", level), now, &h, &why) == 0, "%s accepted", level);
	CHECK(parse(launchText(now, "", NULL, "nonce=0001", "nonce=01"), now, &h, &why) == 0, "nonce");
	CHECK(parse(launchText(now, "", NULL, "created=", "created=x"), now, &h, &why) == 0, "created text");
	CHECK(parse(launchText(now, "", NULL, "onslaught-ce handoff 1", "onslaught-ce handoff 2"), now, &h, &why) == 0, "header");
	CHECK(parse(launchText(now, "", NULL, "-res 640", "-res \x01 640"), now, &h, &why) == 0, "control character");
	std::string cut = launchText(now);
	cut.pop_back();
	CHECK(parse(cut, now, &h, &why) == 0, "incomplete last line");
	std::string nul = launchText(now);
	nul[30] = 0;
	CHECK(parse(nul, now, &h, &why) == 0, "embedded NUL");
	// an outcome
	char out[512];
	snprintf(out, sizeof out, "onslaught-ce handoff 1\nkind=outcome\ngood=0\ncreated=%lld\ntext=The network game stopped: x.\n", now);
	CHECK(parse(out, now, &h, &why) == 1 && h.kind == CE_HANDOFF_OUTCOME && !h.good && !strcmp(h.text, "The network game stopped: x."), "outcome");
	snprintf(out, sizeof out, "onslaught-ce handoff 1\nkind=outcome\ngood=0\ncreated=%lld\ntext=x\nsecret=00\n", now);
	CHECK(parse(out, now, &h, &why) == 0, "launch line in an outcome");
	// format -> parse
	CE_Handoff w = {};
	w.kind = CE_HANDOFF_LAUNCH; w.role = 1; snprintf(w.endpoint, sizeof w.endpoint, "0.0.0.0:27652");
	for (int i = 0; i < 32; i++) w.secret[i] = uint8_t(i * 7);
	for (int i = 0; i < 16; i++) w.nonce[i] = uint8_t(255 - i);
	w.level = 852; w.created = CE_HandoffNow(); snprintf(w.args, sizeof w.args, "-skipfmv");
	char text[4096];
	size_t n = CE_HandoffFormat(&w, text, sizeof text);
	CHECK(n > 0 && CE_HandoffParse(text, n, w.created, &h, &why) == 1 && h.role == 1 && !memcmp(h.secret, w.secret, 32) &&
		  !memcmp(h.nonce, w.nonce, 16) && !strcmp(h.endpoint, w.endpoint), "format round trip");
	CHECK(CE_HandoffFormat(&w, text, 40) == 0, "format refuses a small buffer");
	// a co-op map survives the writer and the reader (the lobby writes the host's choice, the restart reads it)
	w.level = 856;
	n = CE_HandoffFormat(&w, text, sizeof text);
	CHECK(n > 0 && CE_HandoffParse(text, n, w.created, &h, &why) == 1 && h.level == 856, "format round trip of a co-op map (%s)", why ? why : "");
	w.level = 852;
	// files: written private and new, read once, removed as read
	std::string path = std::string(dir) + "/net-launch";
	CHECK(CE_HandoffWrite(path.c_str(), &w) == 1, "write");
	struct stat st;
	CHECK(lstat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600 && S_ISREG(st.st_mode), "written with mode 600 (%o)", st.st_mode & 0777);
	CHECK(CE_HandoffWrite(path.c_str(), &w) == 0, "occupied handoff refused");
	CHECK(CE_HandoffRead(path.c_str(), CE_HandoffNow(), &h, &why) == 1 && h.role == 1, "read (%s)", why ? why : "");
	CHECK(access(path.c_str(), F_OK) != 0, "removed as it was read");
	CHECK(CE_HandoffRead(path.c_str(), CE_HandoffNow(), &h, &why) == 0, "read twice");
	// mode 644: refused, and removed anyway
	CE_HandoffWrite(path.c_str(), &w);
	chmod(path.c_str(), 0644);
	CHECK(CE_HandoffRead(path.c_str(), CE_HandoffNow(), &h, &why) == 0 && h.kind == 0, "mode 644 accepted");
	CHECK(access(path.c_str(), F_OK) != 0, "a refused file is removed too");
	// a file last written 61 s ago, even with a fresh creation line
	CE_HandoffWrite(path.c_str(), &w);
	struct timeval tv[2];
	gettimeofday(&tv[0], NULL);
	tv[0].tv_sec -= 61; tv[1] = tv[0];
	utimes(path.c_str(), tv);
	CHECK(CE_HandoffRead(path.c_str(), CE_HandoffNow(), &h, &why) == 0, "an old file accepted");
	// a link to a private file
	std::string target = std::string(dir) + "/target";
	CE_HandoffWrite(target.c_str(), &w);
	unlink(path.c_str());
	CHECK(symlink(target.c_str(), path.c_str()) == 0, "symlink");
	CHECK(CE_HandoffRead(path.c_str(), CE_HandoffNow(), &h, &why) == 0, "a link accepted");
	CHECK(access(target.c_str(), F_OK) == 0, "the link's target is left alone");
	unlink(path.c_str());
	unlink(target.c_str());
	// a large file
	FILE *f = fopen(path.c_str(), "w");
	for (int i = 0; i < 5000; i++) fputc('x', f);
	fclose(f);
	chmod(path.c_str(), 0600);
	CHECK(CE_HandoffRead(path.c_str(), CE_HandoffNow(), &h, &why) == 0, "a large file accepted");
	unlink(path.c_str());
	// the secret never appears in a refusal's words
	CHECK(parse(launchText(now - 100), now, &h, &why) == 0 && !strstr(why, "0011"), "refusal words");
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: driver SCRATCH\n"); return 2; }
	codes();
	secrets();
	handoffs(argv[1]);
	if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	printf("PASS\n");
	return 0;
}
