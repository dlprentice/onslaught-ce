// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_testkit.h: the test kit's verb channel (port/journeys/README.md). Included only by the kit's own
// files (named *testkit*), which build.py compiles only with --test-kit; no production file includes it.
//
// A journey's steps (BEA_PORT_VERBS=<file>) run inside the game at one point per frame, before the game reads input; replies
// go to BEA_PORT_VERB_OUT=<file> as JSON lines. A feature adds its own verbs, page adapters and state keys in its own kit
// file beside it (port/game/ce_<feature>_testkit.cpp), reading the feature through its header, with the three macros below: each makes a static registrar that links one entry into the
// kit's list at start-up. There is no shared switch to edit.
#pragma once

#include <stddef.h>

enum CeTestResult { CE_TEST_DONE = 0, CE_TEST_PENDING = 1, CE_TEST_FAILED = 2 };

// INPUT: the verb only produces input a player could produce, or only reads state. TESTWRITE: it changes game state
// directly (a camera pose, a placement); a journey that uses one is marked in its result.
enum CeTestVerbKind { CE_TEST_INPUT = 0, CE_TEST_TESTWRITE = 1 };

// One step's words (quotes removed), its tick budget and the step's own scratch, zeroed when the step starts.
struct CeTestArgs
{
	enum { kMaxWords = 24 };
	int			count;
	const char	*word[kMaxWords];	// word[0] is the verb
	const char	*line;				// the whole step as written
	unsigned	tick;				// game ticks (Presents) since the kit started
	unsigned	started;			// the tick this step started
	int			scratch[16];		// the verb's own state across PENDING calls
};

// A verb's reply: a short JSON-safe text (the kit escapes it) and, on failure, the reason.
struct CeTestReply
{
	char text[1024];
};
void CeTestReplyf(CeTestReply &reply, const char *format, ...);

typedef CeTestResult (*CeTestVerbFn)(CeTestArgs &args, CeTestReply &reply);

struct CeTestVerb
{
	const char		*name;
	const char		*usage;
	CeTestVerbKind	kind;
	CeTestVerbFn	fn;
	CeTestVerb		*next;
	CeTestVerb(const char *name, const char *usage, CeTestVerbKind kind, CeTestVerbFn fn);
};

// A page as the player sees it, read from the page's own model (never from pixels).
struct CeTestRow
{
	char	text[200];
	bool	enabled;
};
struct CeTestPage
{
	const char	*family;		// "fe", "ce", "xp", "pause", "box", ... (the id's prefix)
	int			priority;		// the active page with the highest priority is the one shown
	bool		(*active)();
	void		(*id)(char *out, size_t size);				// e.g. "ce:hub", "xp:backup-preview", "fe:main"
	void		(*title)(char *out, size_t size);
	int			(*rows)(CeTestRow *out, int max);			// rows in order; returns how many
	int			(*focused)();								// the highlighted row's index, or -1
	void		(*box)(char *out, size_t size);				// a message, details box or notice shown, else ""
	int			pageRows;									// rows a page key moves (0: no page keys)
	unsigned	pageDownKey, pageDownRaw, pageUpKey, pageUpRaw;	// SDL scancodes and PC scan codes of those keys
	CeTestPage	*next;
	CeTestPage(const char *family, int priority, bool (*active)(), void (*id)(char *, size_t), void (*title)(char *, size_t),
			   int (*rows)(CeTestRow *, int), int (*focused)(), void (*box)(char *, size_t), int pageRows = 0,
			   unsigned pageDownKey = 0, unsigned pageDownRaw = 0, unsigned pageUpKey = 0, unsigned pageUpRaw = 0);
};

// A named piece of feature state for "wait state", "assert" and "dump": writes its value as text; returns false when the
// key does not apply now (it is then left out of a dump).
struct CeTestState
{
	const char	*key;
	bool		(*value)(char *out, size_t size);
	CeTestState	*next;
	CeTestState(const char *key, bool (*value)(char *, size_t));
};

#define CE_TEST_JOIN2(a, b) a##b
#define CE_TEST_JOIN(a, b) CE_TEST_JOIN2(a, b)
#define CE_TEST_VERB(name, usage, kind, fn) static CeTestVerb CE_TEST_JOIN(sCeTestVerb, __LINE__)(name, usage, kind, fn)
#define CE_TEST_PAGE(...) static CeTestPage CE_TEST_JOIN(sCeTestPage, __LINE__)(__VA_ARGS__)
#define CE_TEST_STATE(key, fn) static CeTestState CE_TEST_JOIN(sCeTestState, __LINE__)(key, fn)

// Called by the platform: once per Pump (the kit acts once per Present), and at each Present for a shot and the end.
void		CeTestKitFrame();
const char	*CeTestKitShotPath();	// a frame the journey asked for (a .ppm path), or NULL
void		CeTestKitShotDone();
bool		CeTestKitWantsQuit();	// the journey ended: close the game by its own path

// For verbs: press a key through the same SDL path as the player (down now, up a few ticks later).
void		CeTestKitPress(unsigned sdlScancode, unsigned pcScanCode);
bool		CeTestKitKeysIdle();	// every pressed key has been released
// The page shown now (the highest-priority active adapter), or NULL.
const CeTestPage *CeTestKitPage();
