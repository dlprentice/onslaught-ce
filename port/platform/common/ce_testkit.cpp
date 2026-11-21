// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_testkit.cpp: the test kit's core (ce_testkit.h; port/journeys/README.md). Only in test-kit builds.
//
// The core reads the journey's steps once (BEA_PORT_VERBS), runs one step at a time at one point per frame (the first Pump after
// each Present), and writes one JSON line per finished step to BEA_PORT_VERB_OUT. A step that cannot finish yet holds the queue
// until its tick budget (600 ticks, or "within N") runs out. The journey ends after its last step, or at its first failure,
// with a final line and the game closed by its own path.
//
// Built-in verbs: go, select row, focus row, back, key, wait (page, notice, state, ticks), dump, assert (state, file) and shot.
// Rows are read from the page's own model (the registered page adapters); select and focus still move the cursor with real
// key events, then check that it moved as planned.
#include "ce_testkit.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

extern "C" unsigned PortPresentCount(void);				// d3d9hooks.cpp: Presents completed so far
void	PortTestKitKey(unsigned sdlScancode, unsigned pcScanCode, bool press);	// window.cpp: as BEA_PORT_FAKE_KEYS does
void	PortTestKitMouse(int kind, float x, float y);	// window.cpp: 0 move, 1 left down, 2 left up, as BEA_PORT_FAKE_CLICK does
const char	*PortUserDir();

// The marker that a test-kit build carries and every other build lacks (port/tools/test_kit_tests.py checks for it).
extern "C" const char gCeTestKitMarker[] = "ce-testkit-v1";

namespace
{
CeTestVerb	*gVerbs = nullptr;		// constant-initialized before any registrar runs
CeTestPage	*gPages = nullptr;
CeTestState	*gStates = nullptr;

namespace hash_impl {
#include "../../updater/sha256.c"
}

struct Pending { unsigned upTick, sdl, raw; };
struct MouseStep { unsigned tick; int kind; float x, y; };
struct HeldKey { const char *name; unsigned sdl, raw; };

struct Kit
{
	bool		inited = false, enabled = false, quit = false, stepActive = false;
	FILE		*out = nullptr;
	std::vector<std::string>	steps;
	size_t		index = 0;
	unsigned	firstPresent = 0, lastPresent = ~0u, tick = 0, budget = 600;
	bool		usedTestWrite = false;
	bool		skip = false;			// a require step this build does not meet ends the journey as skipped
	CeTestArgs	args{};
	char		words[2048];
	std::vector<Pending>	keys;
	std::vector<MouseStep>	mouse;			// in tick order (QueueMouse keeps it so)
	CeTestArgs	inner{};					// an until step's repeated step
	std::vector<HeldKey>	held;			// keys a press step holds down until its release step
	char		shot[1024] = "";
	bool		shotWanted = false;
	std::string	runDir;
} K;

void Escape(std::string &out, const char *text)
{
	for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
	{
		if (*p == '"' || *p == '\\') { out += '\\'; out += char(*p); }
		else if (*p == '\n') out += "\\n";
		else if (*p < 32) { char b[8]; snprintf(b, sizeof b, "\\u%04x", *p); out += b; }
		else out += char(*p);
	}
}
void Emit(const std::string &line)
{
	if (!K.out) return;
	fputs(line.c_str(), K.out);
	fputc('\n', K.out);
	fflush(K.out);
}

// The page the player sees: the active adapter with the highest priority.
const CeTestPage *Shown()
{
	const CeTestPage *best = nullptr;
	for (const CeTestPage *p = gPages; p; p = p->next)
		if (p->active && p->active() && (!best || p->priority > best->priority))
			best = p;
	return best;
}

// A dump: the page, its rows, focus and box, the tick and every registered state key that applies now.
std::string Dump(const char *label)
{
	std::string s = "{\"dump\":\"";
	Escape(s, label ? label : "");
	char t[64];
	snprintf(t, sizeof t, "\",\"tick\":%u", K.tick);
	s += t;
	if (const CeTestPage *p = Shown())
	{
		char id[128] = "", title[256] = "", box[1024] = "";
		if (p->id) p->id(id, sizeof id);
		if (p->title) p->title(title, sizeof title);
		if (p->box) p->box(box, sizeof box);
		s += ",\"page\":\""; Escape(s, id);
		s += "\",\"title\":\""; Escape(s, title);
		s += "\",\"box\":\""; Escape(s, box);
		s += "\",\"rows\":[";
		static CeTestRow rows[1024];
		const int n = p->rows ? p->rows(rows, 1024) : 0;
		for (int i = 0; i < n; ++i)
		{
			if (i) s += ",";
			s += "\""; Escape(s, rows[i].text); s += "\"";
		}
		snprintf(t, sizeof t, "],\"focused\":%d", p->focused ? p->focused() : -1);
		s += t;
	}
	else
		s += ",\"page\":null";
	s += ",\"state\":{";
	bool first = true;
	for (const CeTestState *st = gStates; st; st = st->next)
	{
		char value[512] = "";
		if (!st->value || !st->value(value, sizeof value)) continue;
		if (!first) s += ",";
		first = false;
		s += "\""; Escape(s, st->key); s += "\":\""; Escape(s, value); s += "\"";
	}
	s += "}}";
	return s;
}

// Words of a step: spaces separate, double quotes group. A comment is a whole line starting with '#' (Init leaves those out),
// so "#N" is a row number. Returns false on an open quote.
bool Split(const char *line, CeTestArgs &a, char *buffer, size_t size)
{
	a.count = 0;
	size_t used = 0;
	const char *p = line;
	while (*p)
	{
		while (*p == ' ' || *p == '\t') ++p;
		if (!*p) break;
		if (a.count >= CeTestArgs::kMaxWords || used + 1 >= size) return false;
		a.word[a.count++] = buffer + used;
		if (*p == '"')
		{
			++p;
			while (*p && *p != '"' && used + 1 < size) buffer[used++] = *p++;
			if (*p != '"') return false;
			++p;
		}
		else
			while (*p && *p != ' ' && *p != '\t' && used + 1 < size) buffer[used++] = *p++;
		buffer[used++] = 0;
	}
	return true;
}

const CeTestVerb *Find(const char *name)
{
	for (const CeTestVerb *v = gVerbs; v; v = v->next)
		if (!strcmp(v->name, name)) return v;
	return nullptr;
}

void Finish(bool pass, const char *why)
{
	std::string s = pass ? "{\"end\":\"pass\"" : K.skip ? "{\"end\":\"skip\",\"why\":\"" : "{\"end\":\"fail\",\"why\":\"";
	if (!pass) { Escape(s, why ? why : ""); s += "\""; }
	char t[96];
	snprintf(t, sizeof t, ",\"ticks\":%u,\"steps\":%u,\"testwrite\":%s}", K.tick, unsigned(K.index), K.usedTestWrite ? "true" : "false");
	s += t;
	Emit(s);
	K.quit = true;
}

void Init()
{
	K.inited = true;
	const char *path = getenv("BEA_PORT_VERBS");
	if (!path || !*path) return;
	const char *outPath = getenv("BEA_PORT_VERB_OUT");
	K.out = outPath && *outPath ? fopen(outPath, "w") : stdout;
	for (const char *refuse : {"BEA_PORT_TAPE", "BEA_PORT_RECORD"})
		if (const char *v = getenv(refuse); v && *v)
		{
			Emit(std::string("{\"refused\":\"the test kit does not run with ") + refuse + "\"}");
			fprintf(stderr, "onslaught-ce: test kit refused: %s is set\n", refuse);
			return;
		}
	FILE *f = fopen(path, "r");
	if (!f) { Emit("{\"refused\":\"the step file could not be read\"}"); return; }
	char line[2048];
	while (fgets(line, sizeof line, f))
	{
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
		const char *p = line;
		while (*p == ' ' || *p == '\t') ++p;
		if (*p && *p != '#') K.steps.push_back(p);
	}
	fclose(f);
	if (const char *d = getenv("BEA_PORT_VERB_RUN_DIR")) K.runDir = d;
	K.enabled = true;
	std::string head = "{\"kit\":\"ce-testkit-v1\",\"steps\":";
	head += std::to_string(K.steps.size());
	head += ",\"clock\":\"";
	Escape(head, getenv("BEA_PORT_CLOCK") ? getenv("BEA_PORT_CLOCK") : "real");
	head += "\",\"dt\":\"";
	Escape(head, getenv("BEA_PORT_CLOCK_DT") ? getenv("BEA_PORT_CLOCK_DT") : "");
	head += "\"}";
	Emit(head);
	fprintf(stderr, "onslaught-ce: test kit: %u steps from %s\n", unsigned(K.steps.size()), path);
	// Every step is checked before the first runs: a line that does not parse or names no verb refuses the whole journey,
	// rather than failing (or being skipped) part way through.
	for (size_t i = 0; i < K.steps.size(); ++i)
	{
		CeTestArgs probe{};
		char words[2048];
		probe.line = K.steps[i].c_str();
		std::string why;
		if (!Split(probe.line, probe, words, sizeof words) || !probe.count)
			why = "unreadable step";
		else if (!Find(probe.word[0]))
			why = std::string("no verb \"") + probe.word[0] + "\"";
		else if ((!strcmp(probe.word[0], "until") || !strcmp(probe.word[0], "repeat")) && probe.count > 5 && !Find(probe.word[5]))
			why = std::string("no verb \"") + probe.word[5] + "\" to repeat";
		if (!why.empty())
		{
			K.index = 0;		// no step has run
			Emit(Dump("refused"));
			Finish(false, ("line " + std::to_string(i + 1) + " of the steps: " + why + ": " + K.steps[i]).c_str());
			return;
		}
	}
}

void QueueMouse(const MouseStep &m)
{
	auto at = K.mouse.end();
	while (at != K.mouse.begin() && (at - 1)->tick > m.tick) --at;
	K.mouse.insert(at, m);
}

void SendMouse()
{
	size_t sent = 0;
	while (sent < K.mouse.size() && K.mouse[sent].tick <= K.tick)
	{
		PortTestKitMouse(K.mouse[sent].kind, K.mouse[sent].x, K.mouse[sent].y);
		++sent;
	}
	K.mouse.erase(K.mouse.begin(), K.mouse.begin() + long(sent));
}

void ReleaseHeld()
{
	for (auto it = K.held.rbegin(); it != K.held.rend(); ++it)
		PortTestKitKey(it->sdl, it->raw, false);
	K.held.clear();
}

void ReleaseKeys()
{
	for (size_t i = 0; i < K.keys.size();)
		if (K.tick >= K.keys[i].upTick)
		{
			PortTestKitKey(K.keys[i].sdl, K.keys[i].raw, false);
			K.keys.erase(K.keys.begin() + long(i));
		}
		else
			++i;
}
}

CeTestVerb::CeTestVerb(const char *n, const char *u, CeTestVerbKind k, CeTestVerbFn f) : name(n), usage(u), kind(k), fn(f), next(gVerbs) { gVerbs = this; }
CeTestPage::CeTestPage(const char *fam, int pri, bool (*a)(), void (*i)(char *, size_t), void (*t)(char *, size_t),
					   int (*r)(CeTestRow *, int), int (*fo)(), void (*b)(char *, size_t), int pr, unsigned pdk, unsigned pdr,
					   unsigned puk, unsigned pur)
	: family(fam), priority(pri), active(a), id(i), title(t), rows(r), focused(fo), box(b), pageRows(pr), pageDownKey(pdk),
	  pageDownRaw(pdr), pageUpKey(puk), pageUpRaw(pur), next(gPages) { gPages = this; }
CeTestState::CeTestState(const char *k, bool (*v)(char *, size_t)) : key(k), value(v), next(gStates) { gStates = this; }

void CeTestReplyf(CeTestReply &reply, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vsnprintf(reply.text, sizeof reply.text, format, ap);
	va_end(ap);
}

void CeTestKitPress(unsigned sdl, unsigned raw)
{
	PortTestKitKey(sdl, raw, true);
	K.keys.push_back(Pending{K.tick + 6, sdl, raw});	// held 6 ticks, as the key routes that work on every page hold them
}
bool CeTestKitKeysIdle() { return K.keys.empty(); }
const CeTestPage *CeTestKitPage() { return Shown(); }
const char *CeTestKitShotPath() { return K.shotWanted ? K.shot : nullptr; }
void CeTestKitShotDone() { K.shotWanted = false; }
bool CeTestKitWantsQuit() { return K.quit; }

void CeTestKitFrame()
{
	if (!K.inited) Init();
	if (!K.enabled || K.quit) return;
	const unsigned present = PortPresentCount();
	if (present == K.lastPresent) return;			// once per Present
	if (K.lastPresent == ~0u) K.firstPresent = present;
	K.lastPresent = present;
	K.tick = present - K.firstPresent;
	ReleaseKeys();
	SendMouse();
	if (K.shotWanted) return;						// the frame asked for is taken at this Present first
	if (K.index >= K.steps.size())
	{
		if (K.keys.empty() && K.mouse.empty())
		{
			ReleaseHeld();
			Finish(true, nullptr);
		}
		return;
	}
	CeTestArgs &a = K.args;
	if (!K.stepActive)
	{
		memset(&a, 0, sizeof a);
		a.line = K.steps[K.index].c_str();
		if (!Split(a.line, a, K.words, sizeof K.words) || !a.count)
		{
			Emit(std::string("{\"step\":") + std::to_string(K.index) + ",\"ok\":false,\"why\":\"unreadable step\"}");
			Finish(false, a.line);
			return;
		}
		K.budget = 600;
		if (a.count >= 3 && !strcmp(a.word[a.count - 2], "within"))
		{
			K.budget = unsigned(atoi(a.word[a.count - 1]));
			a.count -= 2;
		}
		a.started = K.tick;
		K.stepActive = true;
	}
	a.tick = K.tick;
	const CeTestVerb *verb = Find(a.word[0]);
	CeTestReply reply{};
	CeTestResult r = CE_TEST_FAILED;
	if (!verb)
		CeTestReplyf(reply, "no verb \"%s\"", a.word[0]);
	else
		r = verb->fn(a, reply);
	if (r == CE_TEST_PENDING)
	{
		if (K.tick - a.started <= K.budget) return;
		CeTestReplyf(reply, "not done within %u ticks", K.budget);
		r = CE_TEST_FAILED;
	}
	std::string s = "{\"step\":" + std::to_string(K.index) + ",\"tick\":" + std::to_string(K.tick) + ",\"line\":\"";
	Escape(s, a.line);
	s += "\",\"ok\":";
	s += r == CE_TEST_DONE ? "true" : "false";
	if (verb && verb->kind == CE_TEST_TESTWRITE) { s += ",\"testwrite\":true"; K.usedTestWrite = true; }
	s += ",\"reply\":\"";
	Escape(s, reply.text);
	s += "\"}";
	Emit(s);
	K.stepActive = false;
	if (K.skip)
	{
		Finish(false, reply.text);
		return;
	}
	if (r != CE_TEST_DONE)
	{
		Emit(Dump("at failure"));
		Finish(false, reply.text);
		return;
	}
	++K.index;
}

// ---- Built-in verbs ---------------------------------------------------------------------------------------------------
namespace
{
struct KeyName { const char *name; unsigned sdl, raw; };
const KeyName kKeys[] = {
	{"RETURN", 40, 0x1c}, {"ESC", 41, 0x01}, {"BACKSPACE", 42, 0x0e}, {"TAB", 43, 0x0f}, {"SPACE", 44, 0x39},
	{"RIGHT", 79, 0x14d}, {"LEFT", 80, 0x14b}, {"DOWN", 81, 0x150}, {"UP", 82, 0x148}, {"PGUP", 75, 0x149},
	{"PGDN", 78, 0x151}, {"HOME", 74, 0x147}, {"END", 77, 0x14f}, {"DELETE", 76, 0x153}, {"F1", 58, 0x3b},
	{"F5", 62, 0x3f}, {"F6", 63, 0x40}, {"LCTRL", 224, 0x1d}, {"LSHIFT", 225, 0x2a},
	// letters: SDL scancodes 4 to 29, DirectInput's PC scan codes
	{"A", 4, 0x1e}, {"B", 5, 0x30}, {"C", 6, 0x2e}, {"D", 7, 0x20}, {"E", 8, 0x12}, {"F", 9, 0x21}, {"G", 10, 0x22},
	{"H", 11, 0x23}, {"I", 12, 0x17}, {"J", 13, 0x24}, {"K", 14, 0x25}, {"L", 15, 0x26}, {"M", 16, 0x32}, {"N", 17, 0x31},
	{"O", 18, 0x18}, {"P", 19, 0x19}, {"Q", 20, 0x10}, {"R", 21, 0x13}, {"S", 22, 0x1f}, {"T", 23, 0x14}, {"U", 24, 0x16},
	{"V", 25, 0x2f}, {"W", 26, 0x11}, {"X", 27, 0x2d}, {"Y", 28, 0x15}, {"Z", 29, 0x2c},
};
const KeyName *KeyByName(const char *name)
{
	for (const KeyName &k : kKeys)
		if (!strcasecmp(k.name, name)) return &k;
	return nullptr;
}

// The row a step names: "#N" (1-based), else the whole text, else a unique prefix. -1 none, -2 ambiguous.
int Target(const CeTestPage *p, const char *want, CeTestRow *rows, int n)
{
	if (want[0] == '#') { const int i = atoi(want + 1) - 1; return i >= 0 && i < n ? i : -1; }
	for (int i = 0; i < n; ++i)
		if (!strcmp(rows[i].text, want)) return i;
	int found = -1;
	const size_t len = strlen(want);
	for (int i = 0; i < n; ++i)
		if (!strncmp(rows[i].text, want, len)) { if (found >= 0) return -2; found = i; }
	(void)p;
	return found;
}
void RowList(CeTestReply &reply, const char *prefix, CeTestRow *rows, int n)
{
	std::string s = prefix;
	for (int i = 0; i < n && s.size() < 900; ++i) { s += i ? " | " : " rows: "; s += rows[i].text; }
	CeTestReplyf(reply, "%s", s.c_str());
}

const unsigned kSettle = 30;	// ticks a page is shown before a step presses keys on it (the Extras host takes a select only
							// 0.4 s after its page opens: SelectAllowed, ce_extras.cpp; 30 ticks is 0.5 s at the 1/60 step)

// Move the cursor to the named row with real keys, one key per few ticks; press Return when select is wanted.
// scratch: [0] phase (0 move, 1 pressed Return, 2 waiting release), [1] last focus, [2] presses without movement.
CeTestResult MoveTo(CeTestArgs &a, CeTestReply &reply, const char *want, bool select)
{
	if (!CeTestKitKeysIdle()) return CE_TEST_PENDING;
	if (a.scratch[0] == 1) return CE_TEST_DONE;			// Return pressed and released
	const CeTestPage *p = Shown();
	static CeTestRow rows[1024];
	const int n = p && p->rows && p->focused ? p->rows(rows, 1024) : 0;
	if (!n)
	{
		// a page still opening (a front-end transition) has no rows yet: wait, within the step's budget
		CeTestReplyf(reply, "waiting for a page with rows");
		return CE_TEST_PENDING;
	}
	// A page takes input only once it has settled (a page fading in drops a press, as a player's early press is dropped):
	// act only when the same page has been shown for kSettle ticks. scratch[4] the page id's hash, [5] the tick it was first seen.
	{
		char id[128] = "";
		if (p->id) p->id(id, sizeof id);
		int hash = 5381;
		for (const char *c = id; *c; ++c) hash = hash * 33 + *c;
		// On the real clock (no BEA_PORT_CLOCK) a tick is a Present of any length, so the page must also have been shown for
		// half a second of wall time; on a test clock a tick is a fixed step and the tick count is the game's time.
		const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		static long long seenMs = 0;
		if (!a.scratch[6] || a.scratch[4] != hash) { a.scratch[4] = hash; a.scratch[5] = int(a.tick); a.scratch[6] = 1; seenMs = nowMs; }
		const bool realClock = !getenv("BEA_PORT_CLOCK") || !*getenv("BEA_PORT_CLOCK");
		if (a.tick - unsigned(a.scratch[5]) < kSettle || (realClock && nowMs - seenMs < 500))
		{
			CeTestReplyf(reply, "waiting for %s to settle", id);
			return CE_TEST_PENDING;
		}
	}
	const int target = Target(p, want, rows, n);
	if (target < 0)
	{
		RowList(reply, target == -2 ? "more than one row starts with that text;" : "no such row;", rows, n);
		return CE_TEST_FAILED;
	}
	const int focus = p->focused();
	if (focus == target)
	{
		if (!select) return CE_TEST_DONE;
		CeTestKitPress(40, 0x1c);
		a.scratch[0] = 1;
		CeTestReplyf(reply, "%s", rows[target].text);
		return CE_TEST_PENDING;
	}
	if (a.scratch[3] && focus == a.scratch[1])
	{
		if (++a.scratch[2] > 4) { CeTestReplyf(reply, "the cursor does not move toward \"%s\" (at row %d)", want, focus); return CE_TEST_FAILED; }
	}
	else
		a.scratch[2] = 0;
	a.scratch[1] = focus;
	a.scratch[3] = 1;
	const int gap = target - focus;
	if (p->pageRows > 0 && (gap > p->pageRows || -gap > p->pageRows))
	{
		if (gap > 0) CeTestKitPress(p->pageDownKey, p->pageDownRaw);
		else CeTestKitPress(p->pageUpKey, p->pageUpRaw);
	}
	else if (gap > 0)
		CeTestKitPress(81, 0x150);
	else
		CeTestKitPress(82, 0x148);
	return CE_TEST_PENDING;
}

CeTestResult VerbSelect(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3 || strcmp(a.word[1], "row")) { CeTestReplyf(r, "usage: select row \"TEXT\" | select row #N"); return CE_TEST_FAILED; }
	return MoveTo(a, r, a.word[2], true);
}
CeTestResult VerbFocus(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3 || strcmp(a.word[1], "row")) { CeTestReplyf(r, "usage: focus row \"TEXT\" | focus row #N"); return CE_TEST_FAILED; }
	return MoveTo(a, r, a.word[2], false);
}

// go A > B > C: select each row in turn, waiting for the page to change after each but the last.
// scratch: [8] segment, [9] phase (0 select, 1 wait change), [10..11] reserved; [12] select's own scratch base.
CeTestResult VerbGo(CeTestArgs &a, CeTestReply &r)
{
	std::vector<std::string> segs(1);
	for (int i = 1; i < a.count; ++i)
	{
		if (!strcmp(a.word[i], ">")) { segs.emplace_back(); continue; }
		if (!segs.back().empty()) segs.back() += ' ';
		segs.back() += a.word[i];
	}
	int &seg = a.scratch[8], &phase = a.scratch[9];
	static char before[128];
	if (seg >= int(segs.size())) return CE_TEST_DONE;
	if (phase == 0)
	{
		if (a.scratch[0] == 0 && a.scratch[3] == 0)
		{
			const CeTestPage *p = Shown();
			before[0] = 0;
			if (p && p->id) p->id(before, sizeof before);
		}
		const CeTestResult s = MoveTo(a, r, segs[size_t(seg)].c_str(), true);
		if (s != CE_TEST_DONE) return s;
		for (int i = 0; i < 7; ++i) a.scratch[i] = 0;
		if (seg + 1 >= int(segs.size())) { CeTestReplyf(r, "%s", segs.back().c_str()); return CE_TEST_DONE; }
		phase = 1;
		return CE_TEST_PENDING;
	}
	const CeTestPage *p = Shown();
	char now[128] = "";
	if (p && p->id) p->id(now, sizeof now);
	if (!strcmp(now, before)) return CE_TEST_PENDING;		// still the page the row was chosen on
	static CeTestRow probe[4];
	if (!p || strstr(now, ":transition") || !p->rows || !p->rows(probe, 4)) return CE_TEST_PENDING;	// the next page is still opening
	phase = 0;
	++seg;
	return CE_TEST_PENDING;
}

CeTestResult VerbBack(CeTestArgs &a, CeTestReply &r)
{
	(void)r;
	if (!a.scratch[0]) { CeTestKitPress(41, 0x01); a.scratch[0] = 1; return CE_TEST_PENDING; }
	return CeTestKitKeysIdle() ? CE_TEST_DONE : CE_TEST_PENDING;
}

// key NAME [hold N]
// key NAME[+NAME...] [hold N]: a key, or a chord such as LCTRL+Z (pressed in order on one tick, released in reverse after N
// ticks, 3 by default).
CeTestResult VerbKey(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 2) { CeTestReplyf(r, "usage: key NAME[+NAME] [hold N]"); return CE_TEST_FAILED; }
	const KeyName *chord[4];
	int n = 0;
	char names[128];
	snprintf(names, sizeof names, "%s", a.word[1]);
	for (char *save = nullptr, *t = strtok_r(names, "+", &save); t; t = strtok_r(nullptr, "+", &save))
	{
		const KeyName *k = KeyByName(t);
		if (!k || n == 4) { CeTestReplyf(r, k ? "a chord has at most four keys" : "no key \"%s\"", t); return CE_TEST_FAILED; }
		chord[n++] = k;
	}
	if (!n) { CeTestReplyf(r, "usage: key NAME[+NAME] [hold N]"); return CE_TEST_FAILED; }
	if (!a.scratch[0])
	{
		for (int i = 0; i < n; ++i) PortTestKitKey(chord[i]->sdl, chord[i]->raw, true);
		a.scratch[0] = 1;
		a.scratch[1] = (a.count >= 4 && !strcmp(a.word[2], "hold")) ? atoi(a.word[3]) : 3;
		return CE_TEST_PENDING;
	}
	if (a.tick - a.started < unsigned(a.scratch[1])) return CE_TEST_PENDING;
	for (int i = n - 1; i >= 0; --i) PortTestKitKey(chord[i]->sdl, chord[i]->raw, false);
	return CE_TEST_DONE;
}

bool StateValue(const char *key, char *out, size_t size)
{
	for (const CeTestState *st = gStates; st; st = st->next)
		if (!strcmp(st->key, key)) return st->value && st->value(out, size);
	return false;
}
bool SplitPair(const char *pair, std::string &key, std::string &value)
{
	const char *eq = strchr(pair, '=');
	if (!eq) return false;
	key.assign(pair, size_t(eq - pair));
	value = eq + 1;
	return true;
}

// A state test: KEY OP VALUE, where KEY may name one comma-separated field (playunit.pose[3]), OP is =, <, >, <= or >=, "=*"
// asks only that the key applies, and the other operators compare numbers. Writes the key's present value to now (or
// "(none)"), and returns whether the test holds; false with ok cleared when the text is not a test.
bool StateTest(const char *text, std::string &shown, bool &ok)
{
	ok = false;
	const char *op = strpbrk(text, "<>=");
	if (!op || op == text) return false;
	std::string key(text, size_t(op - text));
	std::string oper(1, *op);
	if ((op[0] == '<' || op[0] == '>') && op[1] == '=') oper += '=';
	const std::string want = op + oper.size();
	if (want.empty()) return false;
	int field = -1;
	const size_t bracket = key.find('[');
	if (bracket != std::string::npos)
	{
		if (key.back() != ']') return false;
		field = atoi(key.c_str() + bracket + 1);
		key.resize(bracket);
	}
	ok = true;
	char now[512] = "";
	if (!StateValue(key.c_str(), now, sizeof now)) { shown = key + "=(none)"; return false; }
	std::string value = now;
	if (field >= 0)
	{
		size_t start = 0;
		for (int i = 0; i < field && start != std::string::npos; ++i)
		{
			start = value.find(',', start);
			if (start != std::string::npos) ++start;
		}
		if (start == std::string::npos) { shown = key + "=" + now + " (no field " + std::to_string(field) + ")"; return false; }
		value = value.substr(start, value.find(',', start) - start);
	}
	shown = key + (field >= 0 ? "[" + std::to_string(field) + "]" : "") + "=" + value;
	if (oper == "=") return want == "*" || value == want;
	char *end1 = nullptr, *end2 = nullptr;
	const double have = strtod(value.c_str(), &end1), limit = strtod(want.c_str(), &end2);
	if (end1 == value.c_str() || end2 == want.c_str()) return false;
	if (oper == "<") return have < limit;
	if (oper == ">") return have > limit;
	if (oper == "<=") return have <= limit;
	return have >= limit;
}

// wait page ID | wait notice "TEXT" | wait state KEY=VALUE | wait ticks N
CeTestResult VerbWait(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3) { CeTestReplyf(r, "usage: wait page ID | notice TEXT | state KEY=VALUE | ticks N"); return CE_TEST_FAILED; }
	const char *what = a.word[1], *arg = a.word[2];
	if (!strcmp(what, "ticks")) return a.tick - a.started >= unsigned(atoi(arg)) ? CE_TEST_DONE : CE_TEST_PENDING;
	const CeTestPage *p = Shown();
	if (!strcmp(what, "page"))
	{
		char id[128] = "";
		if (p && p->id) p->id(id, sizeof id);
		CeTestReplyf(r, "%s", id);
		return !strcmp(id, arg) ? CE_TEST_DONE : CE_TEST_PENDING;
	}
	if (!strcmp(what, "notice"))
	{
		char box[1024] = "";
		if (p && p->box) p->box(box, sizeof box);
		CeTestReplyf(r, "%s", box);
		return strstr(box, arg) ? CE_TEST_DONE : CE_TEST_PENDING;
	}
	if (!strcmp(what, "state"))
	{
		std::string shown;
		bool ok = false;
		const bool holds = StateTest(arg, shown, ok);
		if (!ok) { CeTestReplyf(r, "usage: wait state KEY=VALUE (or KEY[N], and <, >, <=, >=; KEY=* for any value)"); return CE_TEST_FAILED; }
		CeTestReplyf(r, "%s", shown.c_str());
		return holds ? CE_TEST_DONE : CE_TEST_PENDING;
	}
	CeTestReplyf(r, "no wait \"%s\"", what);
	return CE_TEST_FAILED;
}

// assert KEY=VALUE | assert file PATH sha256=HEX (PATH relative to the user folder, or absolute) | assert page ID
CeTestResult VerbAssert(CeTestArgs &a, CeTestReply &r)
{
	if (a.count >= 4 && !strcmp(a.word[1], "file"))
	{
		std::string path = a.word[2];
		if (path[0] != '/') path = std::string(PortUserDir()) + "/" + path;
		if (!strcmp(a.word[3], "exists"))
		{
			FILE *e = fopen(path.c_str(), "rb");
			CeTestReplyf(r, "%s: %s", a.word[2], e ? "exists" : "missing");
			if (e) fclose(e);
			return e ? CE_TEST_DONE : CE_TEST_FAILED;
		}
		std::string key, want;
		if (!SplitPair(a.word[3], key, want) || key != "sha256") { CeTestReplyf(r, "usage: assert file PATH sha256=HEX"); return CE_TEST_FAILED; }
		FILE *f = fopen(path.c_str(), "rb");
		if (!f) { CeTestReplyf(r, "%s: missing", a.word[2]); return want == "missing" ? CE_TEST_DONE : CE_TEST_FAILED; }
		hash_impl::SHA256_CTX c;
		hash_impl::sha256_init(&c);
		unsigned char buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0) hash_impl::sha256_update(&c, buf, n);
		fclose(f);
		unsigned char d[32];
		hash_impl::sha256_final(&c, d);
		char hex[65];
		for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", d[i]);
		CeTestReplyf(r, "%s sha256=%s", a.word[2], hex);
		return want == hex ? CE_TEST_DONE : CE_TEST_FAILED;
	}
	if (a.count >= 3 && !strcmp(a.word[1], "page"))
	{
		const CeTestPage *p = Shown();
		char id[128] = "";
		if (p && p->id) p->id(id, sizeof id);
		CeTestReplyf(r, "page %s", id);
		return !strcmp(id, a.word[2]) ? CE_TEST_DONE : CE_TEST_FAILED;
	}
	std::string shown;
	bool ok = false;
	const bool holds = a.count >= 2 && StateTest(a.word[1], shown, ok);
	if (!ok) { CeTestReplyf(r, "usage: assert KEY=VALUE (KEY[N], <, >, <=, >=) | file PATH sha256=HEX|exists | page ID"); return CE_TEST_FAILED; }
	CeTestReplyf(r, "%s", shown.c_str());
	return holds ? CE_TEST_DONE : CE_TEST_FAILED;
}

// click X Y and drag X1 Y1 X2 Y2 [over N]: the left button at window points, through the player's mouse path. The step
// finishes once its last event has gone. scratch[0] marks the events as queued.
CeTestResult MouseDone(CeTestArgs &a)
{
	(void)a;
	return K.mouse.empty() ? CE_TEST_DONE : CE_TEST_PENDING;
}
CeTestResult VerbClick(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3) { CeTestReplyf(r, "usage: click X Y"); return CE_TEST_FAILED; }
	if (!a.scratch[0])
	{
		const float x = float(atof(a.word[1])), y = float(atof(a.word[2]));
		QueueMouse(MouseStep{K.tick, 0, x, y});
		QueueMouse(MouseStep{K.tick + 2, 1, x, y});
		QueueMouse(MouseStep{K.tick + 6, 2, x, y});
		a.scratch[0] = 1;
		CeTestReplyf(r, "click %.0f %.0f", x, y);
	}
	return MouseDone(a);
}
CeTestResult VerbDrag(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 5) { CeTestReplyf(r, "usage: drag X1 Y1 X2 Y2 [over N]"); return CE_TEST_FAILED; }
	if (!a.scratch[0])
	{
		const float x1 = float(atof(a.word[1])), y1 = float(atof(a.word[2])), x2 = float(atof(a.word[3])), y2 = float(atof(a.word[4]));
		int over = a.count >= 7 && !strcmp(a.word[5], "over") ? atoi(a.word[6]) : 20;
		if (over < 1) over = 1;
		QueueMouse(MouseStep{K.tick, 0, x1, y1});
		QueueMouse(MouseStep{K.tick + 2, 1, x1, y1});
		for (int i = 1; i <= over; ++i)
		{
			const float t = float(i) / float(over);
			QueueMouse(MouseStep{K.tick + 2 + unsigned(i), 0, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t});
		}
		QueueMouse(MouseStep{K.tick + 4 + unsigned(over), 2, x2, y2});
		a.scratch[0] = 1;
		CeTestReplyf(r, "drag %.0f %.0f to %.0f %.0f over %d", x1, y1, x2, y2, over);
	}
	return MouseDone(a);
}

// test-dirty PATH: appends a line to a file in the user folder. TESTWRITE; it exists for the isolation proof's control, a run
// that must be caught changing the profile.
CeTestResult VerbTestDirty(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 2 || strstr(a.word[1], "..") || a.word[1][0] == '/') { CeTestReplyf(r, "usage: test-dirty RELATIVE-PATH"); return CE_TEST_FAILED; }
	const std::string path = std::string(PortUserDir()) + "/" + a.word[1];
	FILE *f = fopen(path.c_str(), "ab");
	if (!f) { CeTestReplyf(r, "%s: cannot open", a.word[1]); return CE_TEST_FAILED; }
	fputs("\n# isolation control\n", f);
	fclose(f);
	CeTestReplyf(r, "%s changed", a.word[1]);
	return CE_TEST_DONE;
}

// press NAME[+NAME] / release NAME[+NAME]: hold keys down across later steps (driving while turning), and let them go. A
// journey's end lets every held key go. mouse-move DX DY [over N]: a relative movement (the level's mouse) spread evenly over N
// ticks (1 by default); mouse-down / mouse-up: the left button where the pointer is (held fire).
int Chord(const char *words, const KeyName **chord, CeTestReply &r)
{
	int n = 0;
	char names[128];
	snprintf(names, sizeof names, "%s", words);
	for (char *save = nullptr, *t = strtok_r(names, "+", &save); t; t = strtok_r(nullptr, "+", &save))
	{
		const KeyName *k = KeyByName(t);
		if (!k || n == 4) { CeTestReplyf(r, k ? "a chord has at most four keys" : "no key \"%s\"", t); return -1; }
		chord[n++] = k;
	}
	return n;
}
CeTestResult VerbPress(CeTestArgs &a, CeTestReply &r)
{
	const KeyName *chord[4];
	const int n = a.count >= 2 ? Chord(a.word[1], chord, r) : -1;
	if (n <= 0) { if (n == 0 || a.count < 2) CeTestReplyf(r, "usage: press NAME[+NAME]"); return CE_TEST_FAILED; }
	for (int i = 0; i < n; ++i)
	{
		const auto same = [&](const HeldKey &h) { return h.sdl == chord[i]->sdl; };
		if (std::find_if(K.held.begin(), K.held.end(), same) != K.held.end()) continue;
		PortTestKitKey(chord[i]->sdl, chord[i]->raw, true);
		K.held.push_back(HeldKey{chord[i]->name, chord[i]->sdl, chord[i]->raw});
	}
	CeTestReplyf(r, "%d held", int(K.held.size()));
	return CE_TEST_DONE;
}
CeTestResult VerbRelease(CeTestArgs &a, CeTestReply &r)
{
	const KeyName *chord[4];
	const int n = a.count >= 2 ? Chord(a.word[1], chord, r) : -1;
	if (n <= 0) { if (n == 0 || a.count < 2) CeTestReplyf(r, "usage: release NAME[+NAME]"); return CE_TEST_FAILED; }
	for (int i = n - 1; i >= 0; --i)
	{
		const auto same = [&](const HeldKey &h) { return h.sdl == chord[i]->sdl; };
		auto at = std::find_if(K.held.begin(), K.held.end(), same);
		if (at == K.held.end()) { CeTestReplyf(r, "%s is not held", chord[i]->name); return CE_TEST_FAILED; }
		PortTestKitKey(chord[i]->sdl, chord[i]->raw, false);
		K.held.erase(at);
	}
	CeTestReplyf(r, "%d held", int(K.held.size()));
	return CE_TEST_DONE;
}
CeTestResult VerbMouseMove(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 3) { CeTestReplyf(r, "usage: mouse-move DX DY [over N] [background]"); return CE_TEST_FAILED; }
	const bool background = !strcmp(a.word[a.count - 1], "background");
	if (!a.scratch[0])
	{
		const float dx = float(atof(a.word[1])), dy = float(atof(a.word[2]));
		int over = a.count >= 5 && !strcmp(a.word[3], "over") ? atoi(a.word[4]) : 1;
		if (over < 1) over = 1;
		// whole counts per tick that add up exactly to DX and DY
		float sentX = 0, sentY = 0;
		for (int i = 1; i <= over; ++i)
		{
			const float wantX = float(int(dx * float(i) / float(over))), wantY = float(int(dy * float(i) / float(over)));
			QueueMouse(MouseStep{K.tick + unsigned(i - 1), 3, wantX - sentX, wantY - sentY});
			sentX = wantX;
			sentY = wantY;
		}
		a.scratch[0] = 1;
		CeTestReplyf(r, "move %.0f %.0f over %d%s", dx, dy, over, background ? " in the background" : "");
	}
	// background: the movement goes on while later steps run (turning while a key is pressed), until it ends or mouse-stop
	return background || K.mouse.empty() ? CE_TEST_DONE : CE_TEST_PENDING;
}
// mouse-stop: drop the rest of a background movement (the outcome it was for has been reached)
CeTestResult VerbMouseStop(CeTestArgs &, CeTestReply &r)
{
	size_t dropped = 0;
	for (auto it = K.mouse.begin(); it != K.mouse.end();)
		if (it->kind == 3) { it = K.mouse.erase(it); ++dropped; }
		else ++it;
	CeTestReplyf(r, "%u movements dropped", unsigned(dropped));
	return CE_TEST_DONE;
}
// until KEY=VALUE every N do STEP...: runs STEP, then again every N ticks, until the state holds (checked each tick, before the
// step). The step's own budget does not apply; the until step's does ("within M" at the end, as for any step).
CeTestResult VerbUntil(CeTestArgs &a, CeTestReply &r)
{
	// until KEY... and repeat COUNT share the loop: until stops when the state test holds, repeat after COUNT runs
	const bool repeat = !strcmp(a.word[0], "repeat");
	if (a.count < 6 || strcmp(a.word[2], "every") || strcmp(a.word[4], "do"))
	{
		CeTestReplyf(r, repeat ? "usage: repeat COUNT every N do STEP" : "usage: until KEY=VALUE every N do STEP");
		return CE_TEST_FAILED;
	}
	std::string shown;
	if (repeat)
	{
		if (!a.scratch[10] && a.scratch[12] >= atoi(a.word[1])) { CeTestReplyf(r, "%d runs", a.scratch[12]); return CE_TEST_DONE; }
	}
	else
	{
		bool ok = false;
		if (StateTest(a.word[1], shown, ok)) { CeTestReplyf(r, "%s after %d tries", shown.c_str(), a.scratch[12]); return CE_TEST_DONE; }
		if (!ok) { CeTestReplyf(r, "usage: until KEY=VALUE every N do STEP"); return CE_TEST_FAILED; }
	}
	const CeTestVerb *step = Find(a.word[5]);
	if (!step || !strcmp(a.word[5], "until") || !strcmp(a.word[5], "repeat"))
	{
		CeTestReplyf(r, "no verb \"%s\" to repeat", a.word[5]);
		return CE_TEST_FAILED;
	}
	const unsigned every = unsigned(atoi(a.word[3]) > 0 ? atoi(a.word[3]) : 1);
	// scratch: [10] the inner step is running, [11] the tick it may start again, [12] tries
	if (!a.scratch[10])
	{
		if (a.tick < unsigned(a.scratch[11])) return CE_TEST_PENDING;
		memset(&K.inner, 0, sizeof K.inner);
		K.inner.line = a.line;
		K.inner.count = a.count - 5;
		for (int i = 0; i < K.inner.count; ++i) K.inner.word[i] = a.word[5 + i];
		K.inner.started = a.tick;
		a.scratch[10] = 1;
		a.scratch[11] = int(a.tick + every);
		++a.scratch[12];
	}
	K.inner.tick = a.tick;
	CeTestReply inner{};
	const CeTestResult done = step->fn(K.inner, inner);
	if (step->kind == CE_TEST_TESTWRITE) K.usedTestWrite = true;
	if (done == CE_TEST_FAILED) { CeTestReplyf(r, "the repeated step failed: %s", inner.text); return CE_TEST_FAILED; }
	if (done == CE_TEST_DONE) a.scratch[10] = 0;
	CeTestReplyf(r, "%s (tries %d)", shown.c_str(), a.scratch[12]);
	return CE_TEST_PENDING;
}
CeTestResult VerbMouseButton(CeTestArgs &a, CeTestReply &r)
{
	const bool down = !strcmp(a.word[0], "mouse-down");
	PortTestKitMouse(down ? 4 : 5, 0, 0);		// the level's mouse (the DirectInput poll), as held fire is read
	CeTestReplyf(r, "%s", down ? "left button down" : "left button up");
	return CE_TEST_DONE;
}

// require KEY=VALUE: a journey that only some builds can take (state such as build.network); on another build the journey
// ends as skipped, not failed.
CeTestResult VerbRequire(CeTestArgs &a, CeTestReply &r)
{
	std::string key, want;
	if (a.count < 2 || !SplitPair(a.word[1], key, want)) { CeTestReplyf(r, "usage: require KEY=VALUE"); return CE_TEST_FAILED; }
	char now[512] = "";
	const bool has = StateValue(key.c_str(), now, sizeof now);
	CeTestReplyf(r, "%s=%s", key.c_str(), has ? now : "(none)");
	if (has && want == now) return CE_TEST_DONE;
	K.skip = true;
	return CE_TEST_DONE;
}

CeTestResult VerbDump(CeTestArgs &a, CeTestReply &r)
{
	Emit(Dump(a.count > 1 ? a.word[1] : ""));
	CeTestReplyf(r, "dumped");
	return CE_TEST_DONE;
}

// shot LABEL: one frame, written as LABEL.ppm in the run folder (the runner converts it to PNG).
CeTestResult VerbShot(CeTestArgs &a, CeTestReply &r)
{
	if (a.count < 2 || K.runDir.empty()) { CeTestReplyf(r, "usage: shot LABEL (needs BEA_PORT_VERB_RUN_DIR)"); return CE_TEST_FAILED; }
	if (!a.scratch[0])
	{
		snprintf(K.shot, sizeof K.shot, "%s/%s.ppm", K.runDir.c_str(), a.word[1]);
		K.shotWanted = true;
		a.scratch[0] = 1;
		return CE_TEST_PENDING;
	}
	if (K.shotWanted) return CE_TEST_PENDING;
	CeTestReplyf(r, "%s.ppm", a.word[1]);
	return CE_TEST_DONE;
}
}

CE_TEST_VERB("go", "go A > B > C: select rows by text in turn, waiting for each page", CE_TEST_INPUT, VerbGo);
CE_TEST_VERB("select", "select row \"TEXT\" | #N: move the cursor there with real keys and press Return", CE_TEST_INPUT, VerbSelect);
CE_TEST_VERB("focus", "focus row \"TEXT\" | #N: move the cursor there with real keys", CE_TEST_INPUT, VerbFocus);
CE_TEST_VERB("back", "back: Esc", CE_TEST_INPUT, VerbBack);
CE_TEST_VERB("key", "key NAME[+NAME] [hold N]: a raw key or chord (LCTRL+Z) through the player's path", CE_TEST_INPUT, VerbKey);
CE_TEST_VERB("wait", "wait page ID | notice TEXT | state KEY=VALUE | ticks N", CE_TEST_INPUT, VerbWait);
CE_TEST_VERB("assert", "assert KEY=VALUE | file PATH sha256=HEX|exists | page ID", CE_TEST_INPUT, VerbAssert);
CE_TEST_VERB("click", "click X Y: the left button at a window point", CE_TEST_INPUT, VerbClick);
CE_TEST_VERB("drag", "drag X1 Y1 X2 Y2 [over N]: press, move over N ticks (20), release", CE_TEST_INPUT, VerbDrag);
CE_TEST_VERB("test-dirty", "test-dirty PATH: append a line to a user file (the isolation proof's control)", CE_TEST_TESTWRITE, VerbTestDirty);
CE_TEST_VERB("press", "press NAME[+NAME]: hold keys down across later steps", CE_TEST_INPUT, VerbPress);
CE_TEST_VERB("release", "release NAME[+NAME]: let held keys go", CE_TEST_INPUT, VerbRelease);
CE_TEST_VERB("mouse-move", "mouse-move DX DY [over N]: a relative movement spread over N ticks", CE_TEST_INPUT, VerbMouseMove);
CE_TEST_VERB("mouse-stop", "mouse-stop: drop the rest of a background movement", CE_TEST_INPUT, VerbMouseStop);
CE_TEST_VERB("repeat", "repeat COUNT every N do STEP: run a step COUNT times, N ticks apart", CE_TEST_INPUT, VerbUntil);
CE_TEST_VERB("until", "until KEY=VALUE every N do STEP: repeat a step until the state holds", CE_TEST_INPUT, VerbUntil);
CE_TEST_VERB("mouse-down", "mouse-down: the left button down where the pointer is", CE_TEST_INPUT, VerbMouseButton);
CE_TEST_VERB("mouse-up", "mouse-up: the left button up", CE_TEST_INPUT, VerbMouseButton);
CE_TEST_VERB("require", "require KEY=VALUE: end as skipped on a build where the state differs", CE_TEST_INPUT, VerbRequire);
CE_TEST_VERB("dump", "dump [LABEL]: page, rows, focus, box, tick and every state key", CE_TEST_INPUT, VerbDump);
CE_TEST_VERB("shot", "shot LABEL: one frame", CE_TEST_INPUT, VerbShot);
