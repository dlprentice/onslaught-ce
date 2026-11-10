// SPDX-License-Identifier: GPL-3.0-only
// The lobby page's words for a refused or failed session. The real port/platform/common/ce_netlobby.cpp (or a mutant of it, LOBBY_SOURCE) is included below, so its own state and notices are
// read directly; only the transport is replaced by a fake whose phase, error and peer version the test sets. Every scenario drives one role of the lobby into one failure and compares the
// notice (or the compatibility line, or the join problem) the page would show with the player's exact words. Authored scratch folder only: no game, window or network.
// Run by port/tools/network_lobby_notice_tests.py, which also runs mutants of the lobby.
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

#include "ce_netcode.h"
#include "ce_netlobby.h"
#include "../../network/network.h"
#include "../../network/compatibility.h"

namespace
{
enum { WIRE_INFO = 1, WIRE_START = 2, WIRE_ACK = 3, WIRE_GO = 4 };	// the lobby's message numbers (CEL1 magic, then the number, then the payload)

struct SentMessage
{
	uint8_t type;
	std::vector<uint8_t> payload;
};

struct Net
{
	uint64_t now = 5000000;
	bool exists = false;
	bool createFails = false;			// the next CE_NetCreate fails (the port is taken, the socket cannot be made)
	unsigned createCalls = 0, destroyCalls = 0, closeCalls = 0;
	uint32_t role = 0;
	int phase = CE_NET_WAITING;
	uint32_t error = CE_NET_OK;
	std::deque<std::vector<uint8_t>> inbox;		// what the other player has delivered
	std::vector<SentMessage> outbox;			// what the lobby queued
	int refuseNext = 0;				// refuse this many of the next sends (back-pressure), then accept
	char peerVersion[40] = "authored";
};
Net N;
std::string gScratch;
int gChecks, gFailures;

void check(bool ok, const char *label)
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", label);
	gChecks++;
	gFailures += !ok;
}
}

struct CE_NetSession { int unused; };
static CE_NetSession gSession;

// ---- the fake transport (the lobby uses nothing else of it) ----
extern "C" {
uint64_t CE_NetNowMs(void) { return N.now; }
int CE_NetGenerateSecret(uint8_t out[CE_NET_SECRET_BYTES])
{
	static int n;
	for (int i = 0; i < CE_NET_SECRET_BYTES; i++)
		out[i] = uint8_t(i * 7 + 3 + 11 * n);
	n++;
	return 1;
}
CE_NetSession *CE_NetCreate(const CE_NetConfig *c)
{
	N.createCalls++;
	if (N.createFails)
	{
		N.exists = false;
		return NULL;
	}
	N.exists = true;
	N.role = c->role;
	N.phase = CE_NET_WAITING;
	N.error = CE_NET_OK;
	return &gSession;
}
void CE_NetDestroy(CE_NetSession *) { N.destroyCalls++; N.exists = false; }
void CE_NetClose(CE_NetSession *) { N.closeCalls++; }
void CE_NetPoll(CE_NetSession *, uint64_t, uint32_t) {}
void CE_NetGetStatus(const CE_NetSession *, CE_NetStatus *out)
{
	memset(out, 0, sizeof *out);
	out->phase = N.phase;
	out->error = N.error;
	out->role = N.role;
}
int CE_NetPeerVersion(const CE_NetSession *, char *out, uint32_t capacity)
{
	snprintf(out, capacity, "%s", N.peerVersion);
	return 1;
}
int CE_NetSend(CE_NetSession *, uint32_t kind, uint32_t, const uint8_t *data, uint32_t size)
{
	if (kind != CE_NET_CONTROL || size < 5)
		return -1;
	if (N.phase != CE_NET_ACTIVE)
		return 0;
	if (N.refuseNext > 0)
	{
		N.refuseNext--;
		return 0;
	}
	SentMessage m;
	m.type = data[4];
	m.payload.assign(data + 5, data + size);
	N.outbox.push_back(m);
	return 1;
}
int CE_NetReceive(CE_NetSession *, CE_NetMessage *m, uint8_t *out, uint32_t capacity)
{
	if (N.inbox.empty())
		return 0;
	const std::vector<uint8_t> &b = N.inbox.front();
	if (b.size() > capacity)
		return -1;
	memcpy(out, b.data(), b.size());
	m->kind = CE_NET_CONTROL;
	m->epoch = 0;
	m->tick = 0;
	m->size = uint32_t(b.size());
	N.inbox.pop_front();
	return 1;
}
}

// ---- the lobby's other neighbours ----
extern "C" int CE_SettingGet(const char *, int def) { return def; }
extern "C" const char *CE_BootError(void) { return ""; }
extern "C" const char *CE_SessionError(void) { return ""; }
#ifdef CE_LOBBY_NOTICE_STANDALONE
// This fixture never enters a campaign. Drivers which include it provide their own boot boundary.
extern "C" int CE_BootCampaignAdmit(const CE_Handoff *)
{
	check(false, "the notice fixture never admits a campaign");
	return 0;
}
#endif
extern "C" const char *SDL_GetCurrentVideoDriver(void) { return "dummy"; }
extern "C" char *SDL_GetClipboardText(void) { return NULL; }
extern "C" bool SDL_SetClipboardText(const char *) { return true; }
extern "C" SDL_Keymod SDL_GetModState(void) { return 0; }
extern "C" void SDL_free(void *p) { free(p); }
extern "C" void SDL_Quit(void) {}
extern "C" int SDL_strcasecmp(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
		if (x != y || !x)
			return x - y;
	}
}

// ---- the lobby under test ----
#include LOBBY_SOURCE

namespace
{
const char *kOtherLabel = "9.9.9";			// a version label that is not this build's

std::vector<uint8_t> Msg(uint8_t type, const std::vector<uint8_t> &payload = std::vector<uint8_t>())
{
	std::vector<uint8_t> m = {'C', 'E', 'L', '1', type};
	m.insert(m.end(), payload.begin(), payload.end());
	return m;
}
std::vector<uint8_t> Be32(uint32_t v) { return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }
std::vector<uint8_t> Cat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) { a.insert(a.end(), b.begin(), b.end()); return a; }
std::vector<uint8_t> Nonce(uint8_t seed)
{
	std::vector<uint8_t> n(16);
	for (int i = 0; i < 16; i++)
		n[i] = uint8_t(seed * 17 + i);
	return n;
}
std::vector<uint8_t> InfoPayload(uint32_t level)
{
	std::vector<uint8_t> p(CE_NET_VERSION_BYTES, 0);
	memcpy(p.data(), "authored", 8);
	return Cat(p, Be32(level));
}

void Deliver(const std::vector<uint8_t> &m) { N.inbox.push_back(m); }
void Step(uint64_t ms = 0)
{
	N.now += ms;
	CE_LobbyUpdate();
}
void Reset()
{
	Forget();
	gRestart = RESTART_NONE;
	remove((gScratch + "/net-launch").c_str());
	N = Net();
}
std::vector<uint8_t> StartNonce()
{
	for (size_t i = N.outbox.size(); i-- > 0;)
		if (N.outbox[i].type == WIRE_START)
			return N.outbox[i].payload.size() == 20 ? std::vector<uint8_t>(N.outbox[i].payload.begin() + 4, N.outbox[i].payload.end()) : std::vector<uint8_t>();
	return std::vector<uint8_t>();
}

// the page's view
CE_LobbyStatus Page()
{
	CE_LobbyStatus st;
	CE_LobbyStatusGet(&st);
	return st;
}
bool Says(const char *field, const char *want) { return field && !strcmp(field, want); }

char gWant[400];
const char *Want(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *Want(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(gWant, sizeof gWant, fmt, ap);
	va_end(ap);
	return gWant;
}

// A lobby that waits as the host (nobody has connected).
bool HostWaiting()
{
	Reset();
	return CE_LobbyHost() == 1 && L.state == CE_LOBBY_HOST_WAITING;
}

// A lobby that is READY as the host (the guest has connected and sent its INFO).
bool HostReady(uint32_t level = 856)
{
	Reset();
	if (!CE_LobbyHost())
		return false;
	L.level = level;
	N.phase = CE_NET_AUTHENTICATING;
	Step();
	N.phase = CE_NET_ACTIVE;
	Step();							// the host's INFO goes out
	Deliver(Msg(WIRE_INFO, InfoPayload(level)));
	Step();
	return L.state == CE_LOBBY_READY && L.host;
}

// The code a host shows: a guest joins with it.
std::string HostCode()
{
	Reset();
	if (!CE_LobbyHost())
		return "";
	return Page().code;
}

// A lobby that has joined (connecting), with the code of a lobby it hosted first.
bool GuestJoining()
{
	const std::string code = HostCode();
	if (code.empty())
		return false;
	Forget();
	N = Net();
	const char *problem = NULL;
	if (!CE_LobbyJoin(code.c_str(), "", &problem))
		return false;
	return L.state == CE_LOBBY_JOIN_CONNECTING && !L.host;
}

// A lobby that is READY as the guest.
bool GuestReady(uint32_t level = 856)
{
	if (!GuestJoining())
		return false;
	N.phase = CE_NET_ACTIVE;
	Step();
	Deliver(Msg(WIRE_INFO, InfoPayload(level)));
	Step();
	return L.state == CE_LOBBY_READY && !L.host;
}

// What the Join page shows for a code (and an address) that cannot be joined.
std::string JoinProblem(const std::string &code, const char *address, bool createFails = false)
{
	Forget();
	N = Net();
	N.createFails = createFails;
	const char *problem = NULL;
	return CE_LobbyJoin(code.c_str(), address, &problem) ? "(joined)" : (problem ? problem : "(no problem given)");
}

// ================================ the host's page ================================
void HostNotices()
{
	// A1: a player with another version label tries to join (the transport refuses the hello: its label differs)
	if (HostWaiting())
	{
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_INCOMPATIBLE;
		snprintf(N.peerVersion, sizeof N.peerVersion, "%s", kOtherLabel);
		Step();
		CE_LobbyStatus st = Page();
		check(Says(st.notice, Want("A player with Onslaught CE %s tried to join: both players need the same version.", kOtherLabel)) && L.state == CE_LOBBY_HOST_WAITING && st.failed == 1 && N.exists,
			  "A1 host: a joiner with another version label is named and the host keeps waiting");
	}
	else
		check(false, "A1 host: a joiner with another version label is named and the host keeps waiting");

	// A1: the same label but another build (files, simulation identity): the labels are not named
	if (HostWaiting())
	{
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_INCOMPATIBLE;
		snprintf(N.peerVersion, sizeof N.peerVersion, "%s", kVersion);
		Step();
		check(Says(Page().notice, Want("A player with another Onslaught CE build (both say %s) tried to join: both players need the same download.", kVersion)),
			  "A1 host: a joiner with the same label but another build is told to get the same download");
	}
	else
		check(false, "A1 host: a joiner with the same label but another build is told to get the same download");

	// A4: failed attempts are counted out loud, and the fifth makes a new code
	if (HostWaiting())
	{
		const std::string first = Page().code;
		bool counted = true;
		for (int i = 1; i < kMaxFailed; i++)
		{
			N.phase = CE_NET_FAILED;
			N.error = CE_NET_AUTH;
			Step();
			counted = counted && Says(Page().notice, Want("A connection attempt failed (%d of %d). A wrong code, or the player gave up.", i, kMaxFailed));
		}
		check(counted, "A4 host: attempts 1 to 4 of 5 are counted in the notice (a wrong code, or the player gave up)");
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_AUTH;
		Step();
		CE_LobbyStatus st = Page();
		check(Says(st.notice, "Five attempts failed, so this game has a new code. Give the other player the new code.") && first != st.code,
			  "A4 host: the fifth failed attempt makes a new code and says so");
	}
	else
		check(false, "A4 host: attempts 1 to 4 of 5 are counted in the notice (a wrong code, or the player gave up)");

	// A15: nobody has reached the host for 45 seconds: it is told what the game cannot know, once, and never over a failed attempt's words
	if (HostWaiting())
	{
		Step(44999);
		const bool early = Page().notice[0] == 0;
		Step(1);
		check(early && Says(Page().notice, Want("Nobody has reached this PC yet. The game cannot tell why: the other player may not have joined yet, may have typed the code or address wrongly, "
												"or a router or firewall may be blocking UDP port %u. See How to connect over the Internet.", (unsigned)CE_LobbyPort())),
			  "A15 host: after 45 seconds with nobody there the host is told what the game cannot know, and not before");
		N.phase = CE_NET_AUTHENTICATING;
		Step();
		check(L.state == CE_LOBBY_HOST_CONNECTING && Page().notice[0] == 0, "A15 host: a player who starts connecting takes the note away");
	}
	else
	{
		check(false, "A15 host: after 45 seconds with nobody there the host is told what the game cannot know, and not before");
		check(false, "A15 host: a player who starts connecting takes the note away");
	}
	if (HostWaiting())
	{
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_AUTH;
		Step();
		const std::string failed = Page().notice;
		Step(120000);
		const bool kept = failed == Want("A connection attempt failed (1 of %d). A wrong code, or the player gave up.", kMaxFailed) && Page().notice == failed;
		L.st.notice[0] = 0;		// whatever else took those words away: an attempt DID reach this PC, so "nobody has reached it" would be false
		Step(120000);
		check(kept && Page().notice[0] == 0, "A15 host: a host whose attempt failed keeps that attempt's words, and is never told that nobody has come");
	}
	else
		check(false, "A15 host: a host whose attempt failed keeps that attempt's words, and is never told that nobody has come");

	// A6: the guest leaves the lobby
	if (HostReady())
	{
		N.phase = CE_NET_CLOSED;
		N.error = CE_NET_PEER_LEFT;
		Step();
		CE_LobbyStatus st = Page();
		check(Says(st.notice, "The other player left.") && L.state == CE_LOBBY_HOST_WAITING && st.failed == 0,
			  "A6 host: the other player leaving the lobby is told, and it is not counted as a failed attempt");
	}
	else
		check(false, "A6 host: the other player leaving the lobby is told, and it is not counted as a failed attempt");

	// A5: the host cannot listen
	Reset();
	N.createFails = true;
	check(CE_LobbyHost() == 0 && Says(Page().notice, Want("This PC could not listen on UDP port %u: is another game using it?", (unsigned)CE_LobbyPort())),
		  "A5 host: a taken port is named with its number");
	if (HostReady())
	{
		N.createFails = true;
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_AUTH;
		Step();
		check(Says(Page().notice, "This PC could not listen for players again.") && L.state == CE_LOBBY_OFF,
			  "A5 host: when it cannot listen again after a failure the lobby says so and closes");
	}
	else
		check(false, "A5 host: when it cannot listen again after a failure the lobby says so and closes");
	Reset();
	setenv("BEA_NET_LOBBY_BIND", "not-an-address", 1);
	check(CE_LobbyHost() == 0 && Says(Page().notice, "BEA_NET_LOBBY_BIND is not an IPv4 address."), "A5 host: a bind address that is not IPv4 is refused with its name");
	setenv("BEA_NET_LOBBY_BIND", "127.0.0.1", 1);

	// A5: not available: no launcher, a co-op test session
	Reset();
	const std::string game = getenv("BEA_GAME_DIR");
	unsetenv("BEA_GAME_DIR");
	check(CE_LobbyHost() == 0 && Says(Page().notice, "Network games need Onslaught CE started from its launcher (onslaught-ce), which names the game and user folders."),
		  "A5 host: a game not started from the launcher is told to use it");
	setenv("BEA_GAME_DIR", game.c_str(), 1);
	Reset();
	setenv("BEA_LOCKSTEP_HOST", "127.0.0.1:27000", 1);
	check(CE_LobbyHost() == 0 && Says(Page().notice, "Network game is unavailable while a co-op test session is selected."), "A5 host: a co-op test session in the environment makes the lobby unavailable");
	unsetenv("BEA_LOCKSTEP_HOST");

	// A7: the start handshake: no acknowledgement
	if (HostReady() && CE_LobbyStart() == 1)
	{
		Step(3000 + 200);
		check(Says(Page().notice, "The other player did not answer the start. Try Start again."), "A7 host: no acknowledgement within the wait is told in full");
	}
	else
		check(false, "A7 host: no acknowledgement within the wait is told in full");

	// A9: GO refused by the transport
	if (HostReady() && CE_LobbyStart() == 1)
	{
		const std::vector<uint8_t> nonce = StartNonce();
		N.refuseNext = 1;
		Deliver(Msg(WIRE_ACK, nonce));
		Step();
		check(Says(Page().notice, "The other player could not be told to start. Try Start again.") && L.state == CE_LOBBY_READY, "A9 host: a GO the transport refused is told, and the lobby stays open");
	}
	else
		check(false, "A9 host: a GO the transport refused is told, and the lobby stays open");

	// A9: the start file cannot be written
	if (HostReady() && CE_LobbyStart() == 1)
	{
		const std::vector<uint8_t> nonce = StartNonce();
		unsetenv("BEA_USER_DIR");
		Deliver(Msg(WIRE_ACK, nonce));
		Step();
		setenv("BEA_USER_DIR", gScratch.c_str(), 1);
		check(Says(Page().notice, "The game could not be started: its private start file could not be written.") && L.state == CE_LOBBY_HOST_WAITING,
			  "A9 host: a start file that cannot be written is told, and the host waits again");
	}
	else
		check(false, "A9 host: a start file that cannot be written is told, and the host waits again");

	// A9: ... and this PC cannot listen again either
	if (HostReady() && CE_LobbyStart() == 1)
	{
		const std::vector<uint8_t> nonce = StartNonce();
		unsetenv("BEA_USER_DIR");
		N.createFails = true;
		Deliver(Msg(WIRE_ACK, nonce));
		Step();
		setenv("BEA_USER_DIR", gScratch.c_str(), 1);
		check(Says(Page().notice, "The game could not be started, and this PC could not listen again."),
			  "A9 host: a start file that cannot be written and a port that cannot be listened on again are told together");
	}
	else
		check(false, "A9 host: a start file that cannot be written and a port that cannot be listened on again are told together");
}

// ================================ the guest's page ================================
void GuestNotices()
{
	// A2: the host refuses the hello of another version label
	if (GuestJoining())
	{
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_INCOMPATIBLE;
		snprintf(N.peerVersion, sizeof N.peerVersion, "%s", kOtherLabel);
		Step();
		CE_LobbyStatus st = Page();
		check(L.state == CE_LOBBY_REFUSED && Says(st.compat, Want("Different Onslaught CE version: yours %s, theirs %s", kVersion, kOtherLabel)) && Says(st.peer_version, kOtherLabel),
			  "A2 guest: another version label is refused with both labels named");
	}
	else
		check(false, "A2 guest: another version label is refused with both labels named");

	// A2: the same label but another build
	if (GuestJoining())
	{
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_INCOMPATIBLE;
		snprintf(N.peerVersion, sizeof N.peerVersion, "%s", kVersion);
		Step();
		CE_LobbyStatus st = Page();
		check(L.state == CE_LOBBY_REFUSED && Says(st.compat, Want("Different Onslaught CE build (both say %s): both need the same download", kVersion)),
			  "A2 guest: the same label but another build is refused without naming labels");
	}
	else
		check(false, "A2 guest: the same label but another build is refused without naming labels");

	// A3: nobody answers (a wrong code, a host that is not waiting, a firewall)
	if (GuestJoining())
	{
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_TIMEOUT;
		Step();
		check(L.state == CE_LOBBY_FAILED &&
				  Says(Page().notice, Want("No game answered at 127.0.0.1, port %u. The game cannot tell why: the code or address may be wrong, the host may not be waiting (a host makes a new code "
										   "after five failed attempts), or a router or firewall may be blocking UDP port %u. See How to connect over the Internet.",
										   (unsigned)CE_LobbyPort(), (unsigned)CE_LobbyPort())),
			  "A3 guest: no answer names the address and port, says the game cannot tell why and lists the causes");
	}
	if (GuestJoining())
	{
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_AUTH;
		Step();
		check(L.state == CE_LOBBY_FAILED &&
				  Says(Page().notice, Want("No game answered at 127.0.0.1, port %u. The game cannot tell why: the code or address may be wrong, the host may not be waiting (a host makes a new code "
										   "after five failed attempts), or a router or firewall may be blocking UDP port %u. See How to connect over the Internet.",
										   (unsigned)CE_LobbyPort(), (unsigned)CE_LobbyPort())),
			  "silent-host-auth");
	}
	else
		check(false, "A3 guest: no answer names the address and port, says the game cannot tell why and lists the causes");

	// A6: the host leaves, or has closed the game before the guest was ready
	if (GuestReady())
	{
		N.phase = CE_NET_CLOSED;
		N.error = CE_NET_PEER_LEFT;
		Step();
		check(L.state == CE_LOBBY_LEFT && Says(Page().notice, "The host left the game."), "A6 guest: the host leaving a lobby the guest was ready in is told");
	}
	else
		check(false, "A6 guest: the host leaving a lobby the guest was ready in is told");
	if (GuestJoining())
	{
		N.phase = CE_NET_CLOSED;
		N.error = CE_NET_PEER_LEFT;
		Step();
		check(L.state == CE_LOBBY_LEFT && Says(Page().notice, "The host closed the game."), "A6 guest: the host closing the game before the guest was ready is told");
	}
	else
		check(false, "A6 guest: the host closing the game before the guest was ready is told");

	// A8: the host did not start the game
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		Step(5000 + 500);
		check(Says(Page().notice, "The host did not start the game. Wait for the host to try again."), "A8 guest: no GO within the wait is told in full");
	}
	else
		check(false, "A8 guest: no GO within the wait is told in full");
}

// Direct loss is not evidence that another player chose Leave. Observe cleanup as well as the words.
void DirectConnectionLoss()
{
	const struct Loss { int phase; uint32_t error; const char *host; const char *guest; } losses[] = {
		{CE_NET_FAILED, CE_NET_PEER_LEFT, "A16 host: raw disconnect keeps a listening lobby and reports connection loss",
		 "A16 guest: raw disconnect retires the session and reports connection loss"},
		{CE_NET_FAILED, CE_NET_TIMEOUT, "A16 host: timeout keeps a listening lobby and reports connection loss",
		 "A16 guest: timeout retires the session and reports connection loss"},
		{CE_NET_CLOSED, CE_NET_OK, "A16 host: unconfirmed close keeps a listening lobby and reports connection loss",
		 "A16 guest: unconfirmed close retires the session and reports connection loss"},
	};
	for (const auto &loss : losses)
	{
		if (HostReady())
		{
			const std::string code = Page().code;
			const unsigned created = N.createCalls, destroyed = N.destroyCalls, closed = N.closeCalls;
			N.phase = loss.phase; N.error = loss.error;
			Step();
			const auto status = Page();
			check(status.state == CE_LOBBY_HOST_WAITING && status.failed == 0 && code == status.code &&
				  Says(status.notice, "The connection to the other player was lost. Waiting for another player with the same code.") &&
				  L.net && N.exists && N.createCalls == created + 1 && N.destroyCalls == destroyed + 1 &&
				  N.closeCalls == closed && !CE_LobbyRestartDue(), loss.host);
		}
		else check(false, loss.host);
		if (GuestReady())
		{
			const unsigned created = N.createCalls, destroyed = N.destroyCalls, closed = N.closeCalls;
			N.phase = loss.phase; N.error = loss.error;
			Step();
			const auto status = Page();
			check(status.state == CE_LOBBY_FAILED &&
				  Says(status.notice, "The connection to the host was lost. Check that the host is still waiting, then join again.") &&
				  !L.net && !N.exists && N.createCalls == created && N.destroyCalls == destroyed + 1 &&
				  N.closeCalls == closed && !CE_LobbyRestartDue(), loss.guest);
		}
		else check(false, loss.guest);
	}
}

// ================================ the Join page ================================
void JoinProblems()
{
	const std::string code = HostCode();
	check(!code.empty(), "A14 join: a host shows a code to join with");
	check(JoinProblem("", "") == "Type or paste the host's code.", "A14 join: an empty code asks for the host's code");
	check(JoinProblem(code.substr(0, 10), "") == "The code is too short: it has 23 letters and digits.", "A14 join: a short code is told to be short");
	check(JoinProblem(code + "AAAA", "") == "The code is too long: it has 23 letters and digits.", "A14 join: a long code is told to be long");
	std::string bad = code;
	bad[0] = '!';
	check(JoinProblem(bad, "") == "Codes use only digits and the letters A to Z except U.", "A14 join: a character that codes do not use is named");
	std::string typo = code;
	typo[0] = typo[0] == '7' ? '8' : '7';
	check(JoinProblem(typo, "") == "The code has a typo: check it with the host.", "A14 join: a mistyped code is told to be a typo");
	check(JoinProblem(code, "abc") == "The address must be four numbers with dots, for example 203.0.113.5.", "A14 join: an address that is not four numbers is told how it looks");
	check(JoinProblem(code, "1.2.3.4:99999") == "The address's port must be a number from 1 to 65535.", "A14 join: an address with a port that is out of range is told");
	check(JoinProblem(code, "", true) == "The connection could not be set up on this PC.", "A14 join: a connection that cannot be set up is told");
	const std::string game = getenv("BEA_GAME_DIR");
	unsetenv("BEA_GAME_DIR");
	check(JoinProblem(code, "") == "Network games need Onslaught CE started from its launcher (onslaught-ce), which names the game and user folders.", "A14 join: a game not started from the launcher is told to use it");
	setenv("BEA_GAME_DIR", game.c_str(), 1);
}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *scratch = getenv("LOBBY_TEST_SCRATCH");
	if (!scratch)
		return 2;
	gScratch = scratch;
	setenv("BEA_USER_DIR", scratch, 1);
	setenv("BEA_GAME_DIR", scratch, 1);
	setenv("BEA_NET_LOBBY_BIND", "127.0.0.1", 1);
	HostNotices();
	GuestNotices();
	DirectConnectionLoss();
	JoinProblems();
	printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures ? 1 : 0;
}
