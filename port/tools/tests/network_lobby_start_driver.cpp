// SPDX-License-Identifier: GPL-3.0-only
// The lobby's start handshake against a scripted peer. The real port/platform/common/ce_netlobby.cpp (or a mutant of it, LOBBY_SOURCE) is included below, so the lobby's own state, its
// restart flag and its start file are read directly; only the transport is replaced: a fake whose sends can be refused (back-pressure), whose inbox the test fills, whose phase and
// clock the test sets. SDL and the settings, session and compatibility hooks are stubs. Authored scratch folder only: no game, window or network.
// Run by port/tools/network_lobby_start_tests.py, which also runs mutants of the lobby, and the same scenarios against the lobby before the GO message existed.
//
// The scripted peer IS the test, so each scenario drives one role of the real lobby: the host (START out, the guest's ACK in, GO out) or the guest (START in, ACK out, GO in).
// The scenarios use only what the lobby before the fix also had (state, restart flag, files, the messages on the wire, whose numbers are written here), so that the same file builds
// against that lobby and shows the failures.
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <set>

#include "ce_netcode.h"
#include "ce_netlobby.h"
#include "../../network/network.h"
#include "ice_game.h"
#include "ice_exchange.h"
#include "../../network/compatibility.h"

namespace
{
enum { WIRE_INFO = 1, WIRE_START = 2, WIRE_ACK = 3, WIRE_GO = 4 };	// the lobby's message numbers (CEL1 magic, then the number, then the payload)
const uint64_t kStepHostMs = 3000, kStepGuestMs = 5000;			// the deadlines the scenarios step across

struct SentMessage
{
	uint8_t type;
	std::vector<uint8_t> payload;
	bool handoffPresent;		// the start file existed when this message was queued
};

struct Net
{
	uint64_t now = 5000000;
	bool exists = false;
	uint32_t role = 0;
	int phase = CE_NET_WAITING;
	uint32_t error = CE_NET_OK;
	std::deque<std::vector<uint8_t>> inbox;		// what the other player has delivered
	std::vector<SentMessage> outbox;			// what the lobby queued
	int refuseNext = 0;							// refuse this many of the next sends (back-pressure), then accept
	bool refuseAll = false;
	int refused = 0;
	std::vector<std::string> events;			// "send:N", "close" (a graceful leave), "destroy", in order
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

std::string HandoffFile() { return gScratch + "/net-launch"; }
static std::set<std::string> inspectedHandoffs;
static void RetainPriorHandoffs(const std::string &path) {
    for (const auto &entry : std::filesystem::directory_iterator(
             std::filesystem::path(path).parent_path()))
        if (entry.path().extension() == ".handoff")
            inspectedHandoffs.insert(entry.path().string());
}

bool FileExists(const std::string &p)
{
    for (const auto &entry : std::filesystem::directory_iterator(
             std::filesystem::path(p).parent_path())) {
        std::string name = entry.path().filename().string();
        if (name.rfind("ce-launch-", 0) == 0 && entry.path().extension() == ".handoff" &&
            entry.is_regular_file() && entry.file_size() > 0 &&
            !inspectedHandoffs.count(entry.path().string())) return true;
    }
    return false;
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
bool gGameCreateFail = false;
std::string gGameCreateProblem, gGameRouteProblem;
unsigned gGameCreateCalls = 0, gNullProblemCalls = 0, gSessionProblemCalls = 0;

CE_NetSession *CE_NetCreate(const CE_NetConfig *c)
{
	N.exists = true;
	N.role = c->role;
	N.phase = CE_NET_WAITING;
	N.error = CE_NET_OK;
	return &gSession;
}
extern "C" CE_NetSession *CE_NetCreateLobby(const CE_NetConfig *config) {
    ++gGameCreateCalls;
    return gGameCreateFail ? NULL : CE_NetCreate(config);
}
const char *CE_NetGameProblem(const CE_NetSession *session) {
    if (session) { ++gSessionProblemCalls; return gGameRouteProblem.c_str(); }
    ++gNullProblemCalls;
    return gGameCreateProblem.c_str();
}

void CE_NetDestroy(CE_NetSession *) { gGameRouteProblem.clear(); N.exists = false; N.events.push_back("destroy"); }
void CE_NetClose(CE_NetSession *) { N.events.push_back("close"); }
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
	if (N.refuseAll || N.refuseNext > 0)
	{
		if (N.refuseNext > 0)
			N.refuseNext--;
		N.refused++;
		return 0;
	}
	SentMessage m;
	m.type = data[4];
	m.payload.assign(data + 5, data + size);
	m.handoffPresent = FileExists(HandoffFile());
	N.outbox.push_back(m);
	N.events.push_back("send:" + std::to_string(int(m.type)));
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
// This fixture exercises direct transport and injected route failures, never
// selected signaling. Link the real boot authority; an unexpected attempt to
// grant it signaling context must fail this fixture rather than fabricate it.
extern "C" int CE_NetSignalingDirectorySelected(void) { return 0; }
bool CE_NetSelectSignalingDirectory(const char *) noexcept
{
    check(false, "the start fixture never selects a signaling directory");
    return false;
}
bool CeIceExchange::ContextForConfig(const CE_NetConfig *, uint8_t context[32]) noexcept
{
    check(false, "the start fixture never grants a signaling context");
    memset(context, 0, 32);
    return false;
}
extern "C" int CE_SettingGet(const char *, int def) { return def; }
extern "C" const char *CE_BootError(void) { return ""; }
extern "C" const char *CE_SessionError(void) { return ""; }
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
int Count(uint8_t type)
{
	int n = 0;
	for (const SentMessage &m : N.outbox)
		n += m.type == type;
	return n;
}
const SentMessage *Last(uint8_t type)
{
	for (size_t i = N.outbox.size(); i-- > 0;)
		if (N.outbox[i].type == type)
			return &N.outbox[i];
	return NULL;
}
int EventIndex(const char *what, int from = 0)
{
	for (size_t i = from; i < N.events.size(); i++)
		if (N.events[i] == what)
			return int(i);
	return -1;
}
bool Restarting() { return gRestart == RESTART_LAUNCH && L.state == CE_LOBBY_STARTING; }

void Reset()
{
	Forget();
	gRestart = RESTART_NONE;
	RetainPriorHandoffs(HandoffFile());
	N = Net();
    gGameCreateFail = false;
    gGameCreateProblem.clear(); gGameRouteProblem.clear();
    gGameCreateCalls = gNullProblemCalls = gSessionProblemCalls = 0;
}

// A lobby that is READY as the host (the guest has connected and sent its INFO) on the given map.
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
	N.outbox.clear();
	N.events.clear();
	return L.state == CE_LOBBY_READY && L.host;
}

// A lobby that is READY as the guest: it joins with the code of a lobby it hosted first, then the host's INFO arrives.
bool GuestReady(uint32_t level = 856)
{
	Reset();
	if (!CE_LobbyHost())
		return false;
	CE_LobbyStatus st;
	CE_LobbyStatusGet(&st);
	char code[64];
	snprintf(code, sizeof code, "%s", st.code);
	Forget();
	N = Net();
	const char *problem = NULL;
	if (!CE_LobbyJoin(code, "", &problem))
		return false;
	N.phase = CE_NET_ACTIVE;
	Step();
	Deliver(Msg(WIRE_INFO, InfoPayload(level)));
	Step();
	N.outbox.clear();
	N.events.clear();
	return L.state == CE_LOBBY_READY && !L.host;
}

bool ReadHandoff(CE_Handoff *h)
{
    // Inspect each new authored launch without removing any retained fixture.
    const char *directory = getenv("BEA_USER_DIR");
    if (!directory) return false;
    std::filesystem::path selected;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("ce-launch-", 0) != 0 || entry.path().extension() != ".handoff" ||
            !entry.is_regular_file() || entry.file_size() == 0 ||
            inspectedHandoffs.count(entry.path().string())) continue;
        if (!selected.empty()) return false;
        selected = entry.path();
    }
    if (selected.empty()) return false;
    inspectedHandoffs.insert(selected.string());
    std::ifstream input(selected, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const char *why = NULL;
    return CE_HandoffParse(bytes.data(), bytes.size(), CE_HandoffNow(), h, &why) != 0;
}

std::vector<uint8_t> StartNonce()
{
	const SentMessage *s = Last(WIRE_START);
	return s && s->payload.size() == 20 ? std::vector<uint8_t>(s->payload.begin() + 4, s->payload.end()) : std::vector<uint8_t>();
}

// ================================ the host ================================
void HostScenarios()
{
	// H1
	if (HostReady(100) && CE_LobbyStart() == 1)
	{
		const SentMessage *s = Last(WIRE_START);
		check(s && s->payload.size() == 20 && s->payload[0] == 0 && s->payload[3] == 100 && !Restarting() && Count(WIRE_GO) == 0,
			  "host: START is queued with the chosen map and a start number, and the lobby does not restart");
	}
	else
		check(false, "host: START is queued with the chosen map and a start number, and the lobby does not restart");

	// H2
	if (HostReady() && CE_LobbyStart() == 1)
	{
		const std::vector<uint8_t> nonce = StartNonce();
		Deliver(Msg(WIRE_ACK, nonce));
		Step();
		const SentMessage *go = Last(WIRE_GO);
		check(go && go->payload == nonce && Count(WIRE_GO) == 1, "host: a valid acknowledgement is answered with one GO carrying the same start number");
		check(go && go->handoffPresent, "host: the host's start file is written before GO is queued");
		const int sendGo = EventIndex("send:4"), close = EventIndex("close"), destroy = EventIndex("destroy");
		check(sendGo >= 0 && close > sendGo && destroy > close, "host: GO is queued first, then the lobby leaves gracefully (so that GO is delivered)");
		CE_Handoff h;
		check(Restarting() && ReadHandoff(&h) && h.role == 1 && h.level == 856 && !memcmp(h.nonce, nonce.data(), 16),
			  "host: the restart is due with a start file for the host, the chosen map and the start number");
	}
	else
		check(false, "host: a valid acknowledgement is answered with one GO carrying the same start number");

	// H3
	if (HostReady() && CE_LobbyStart() == 1)
	{
		Deliver(Msg(WIRE_ACK, Nonce(9)));
		Step();
		check(Count(WIRE_GO) == 0 && !Restarting() && !FileExists(HandoffFile()), "host: an acknowledgement with another start number is ignored (no GO, no restart, no start file)");
	}
	else
		check(false, "host: an acknowledgement with another start number is ignored (no GO, no restart, no start file)");

	// H4
	if (HostReady())
	{
		Deliver(Msg(WIRE_ACK, Nonce(1)));
		Step();
		check(Count(WIRE_GO) == 0 && !Restarting(), "host: an acknowledgement before any START is ignored");
	}
	else
		check(false, "host: an acknowledgement before any START is ignored");

	// H5
	if (HostReady() && CE_LobbyStart() == 1)
	{
		const std::vector<uint8_t> first = StartNonce();
		Step(kStepHostMs - 100);
		Step(200);
		CE_LobbyStatus st;
		CE_LobbyStatusGet(&st);
		check(!Restarting() && strstr(st.notice, "did not answer") != NULL, "host: no acknowledgement within the wait ends the attempt with a notice and no restart");
		Deliver(Msg(WIRE_ACK, first));
		Step();
		check(Count(WIRE_GO) == 0 && !Restarting(), "host: a late acknowledgement of an expired attempt is ignored");
		check(CE_LobbyStart() == 1 && StartNonce() != first && Count(WIRE_START) == 2, "host: Start works again after an expired attempt, with a new start number");
		const std::vector<uint8_t> second = StartNonce();
		Deliver(Msg(WIRE_ACK, first));
		Step();
		check(!Restarting(), "host: the old attempt's acknowledgement does not start the new attempt");
		Deliver(Msg(WIRE_ACK, second));
		Step();
		check(Restarting() && Count(WIRE_GO) == 1, "host: the new attempt's acknowledgement starts it");
	}
	else
		check(false, "host: no acknowledgement within the wait ends the attempt with a notice and no restart");

	// H6
	if (HostReady() && CE_LobbyStart() == 1)
	{
		const std::vector<uint8_t> nonce = StartNonce();
		N.refuseNext = 1;
		Deliver(Msg(WIRE_ACK, nonce));
		Step();
		check(Count(WIRE_GO) == 0 && !Restarting() && !FileExists(HandoffFile()) && N.refused == 1 && L.state == CE_LOBBY_READY,
			  "host: GO refused by the transport: no restart, the start file is removed and the lobby stays open");
		CE_LobbyStatus st;
		CE_LobbyStatusGet(&st);
		check(strlen(st.notice) > 0 && CE_LobbyStart() == 1, "host: the attempt ended with a notice and Start can be tried again");
	}
	else
		check(false, "host: GO refused by the transport: no restart, the start file is removed and the lobby stays open");

	// H7
	if (HostReady() && CE_LobbyStart() == 1)
	{
		const std::vector<uint8_t> nonce = StartNonce();
		unsetenv("BEA_USER_DIR");		// the start file cannot be written
		Deliver(Msg(WIRE_ACK, nonce));
		Step();
		setenv("BEA_USER_DIR", gScratch.c_str(), 1);
		check(Count(WIRE_GO) == 0 && !Restarting(), "host: when the start file cannot be written no GO is sent and the host does not start");
	}
	else
		check(false, "host: when the start file cannot be written no GO is sent and the host does not start");

	// H8
	if (HostReady() && CE_LobbyStart() == 1)
	{
		const std::vector<uint8_t> nonce = StartNonce();
		Deliver(Msg(WIRE_ACK, nonce));
		N.phase = CE_NET_CLOSED;
		N.error = CE_NET_PEER_LEFT;
		Step();
		check(Count(WIRE_GO) == 0 && !Restarting() && !FileExists(HandoffFile()), "host: a guest that has left does not start the host, even with its acknowledgement still queued");
	}
	else
		check(false, "host: a guest that has left does not start the host, even with its acknowledgement still queued");

	// H9
	if (HostReady())
	{
		N.refuseNext = 1;
		const int first = CE_LobbyStart();
		const int second = CE_LobbyStart();
		check(first == 0 && second == 1 && Count(WIRE_START) == 1, "host: a START the transport refused is not an attempt, and Start can be pressed again");
	}
	else
		check(false, "host: a START the transport refused is not an attempt, and Start can be pressed again");
}

// ================================ the guest ================================
void GuestScenarios()
{
	// G1
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		const SentMessage *ack = Last(WIRE_ACK);
		check(ack && ack->payload == Nonce(4) && Count(WIRE_ACK) == 1, "guest: START is acknowledged once with its start number");
		Step(kStepGuestMs - 200);
		check(!Restarting() && !FileExists(HandoffFile()) && L.state == CE_LOBBY_READY, "guest: with the acknowledgement sent and no GO, the guest waits: no restart before the deadline");
	}
	else
		check(false, "guest: START is acknowledged once with its start number");

	// G2
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(100), Nonce(4))));
		Step();
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		CE_Handoff h;
		check(Restarting() && ReadHandoff(&h) && h.role == 2 && h.level == 100 && !memcmp(h.nonce, Nonce(4).data(), 16),
			  "guest: GO with the START's number starts the guest (a start file for the guest, the host's map and the start number)");
	}
	else
		check(false, "guest: GO with the START's number starts the guest (a start file for the guest, the host's map and the start number)");

	// G3
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		Deliver(Msg(WIRE_GO, Nonce(5)));
		Step();
		check(!Restarting() && !FileExists(HandoffFile()), "guest: GO with another start number is ignored");
	}
	else
		check(false, "guest: GO with another start number is ignored");

	// G4
	if (GuestReady())
	{
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		check(!Restarting() && !FileExists(HandoffFile()), "guest: GO with no START is ignored");
	}
	else
		check(false, "guest: GO with no START is ignored");

	// G5
	if (GuestReady())
	{
		N.refuseNext = 3;
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		check(Count(WIRE_ACK) == 0 && !Restarting(), "guest: an acknowledgement the transport refuses is not sent and does not start the guest");
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		check(!Restarting(), "guest: GO before the acknowledgement is queued does not start the guest");
		Step(100);
		Step(100);
		check(Count(WIRE_ACK) == 1 && Last(WIRE_ACK)->payload == Nonce(4) && N.refused == 3, "guest: the refused acknowledgement is asked again until it goes through (one ACK in the end)");
		check(!Restarting(), "guest: and the guest still waits for GO");
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		check(Restarting(), "guest: GO after the acknowledgement went through starts the guest");
	}
	else
		check(false, "guest: an acknowledgement the transport refuses is not sent and does not start the guest");

	// G6
	if (GuestReady())
	{
		N.refuseAll = true;
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		Step(kStepGuestMs - 100);
		Step(200);
		CE_LobbyStatus st;
		CE_LobbyStatusGet(&st);
		check(Count(WIRE_ACK) == 0 && !Restarting() && strlen(st.notice) > 0, "guest: an acknowledgement refused until the deadline never starts the guest; the attempt ends with a notice");
		N.refuseAll = false;
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		check(!Restarting(), "guest: a GO after that attempt ended is ignored");
	}
	else
		check(false, "guest: an acknowledgement refused until the deadline never starts the guest; the attempt ends with a notice");

	// G7
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		Step(kStepGuestMs - 100);
		Step(200);
		CE_LobbyStatus st;
		CE_LobbyStatusGet(&st);
		check(!Restarting() && L.state == CE_LOBBY_READY && strstr(st.notice, "did not start") != NULL, "guest: no GO within the wait ends the attempt with a notice, back in the lobby");
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		check(!Restarting(), "guest: a late GO of an expired attempt is ignored");
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(6))));
		Step();
		Deliver(Msg(WIRE_GO, Nonce(6)));
		Step();
		check(Restarting(), "guest: a new START after an expired attempt is acknowledged and its GO starts the guest");
	}
	else
		check(false, "guest: no GO within the wait ends the attempt with a notice, back in the lobby");

	// G8
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		N.phase = CE_NET_CLOSED;
		N.error = CE_NET_PEER_LEFT;
		Step();
		check(!Restarting() && !FileExists(HandoffFile()) && L.state != CE_LOBBY_STARTING, "guest: the host leaving without GO is a failure, not a start");
	}
	else
		check(false, "guest: the host leaving without GO is a failure, not a start");

	// G9
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		N.phase = CE_NET_FAILED;
		N.error = CE_NET_TIMEOUT;
		Step();
		check(!Restarting() && !FileExists(HandoffFile()), "guest: a transport failure after the acknowledgement is a failure, not a start");
	}
	else
		check(false, "guest: a transport failure after the acknowledgement is a failure, not a start");

	// G10
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		Deliver(Msg(WIRE_GO, Nonce(4)));
		N.phase = CE_NET_CLOSED;
		N.error = CE_NET_PEER_LEFT;
		Step();
		check(Restarting(), "guest: GO that arrived before the host's leave is read before the loss is judged, and starts the guest");
	}
	else
		check(false, "guest: GO that arrived before the host's leave is read before the loss is judged, and starts the guest");

	// G11
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		Deliver(Msg(WIRE_START, Cat(Be32(100), Nonce(5))));
		Step();
		check(Count(WIRE_ACK) == 2 && Last(WIRE_ACK)->payload == Nonce(5), "guest: a newer START replaces the open attempt and is acknowledged with its own number");
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		check(!Restarting(), "guest: the replaced attempt's GO is ignored");
		Deliver(Msg(WIRE_GO, Nonce(5)));
		Step();
		CE_Handoff h;
		check(Restarting() && ReadHandoff(&h) && h.level == 100 && !memcmp(h.nonce, Nonce(5).data(), 16), "guest: the newer attempt's GO starts the guest on the newer map");
	}
	else
		check(false, "guest: a newer START replaces the open attempt and is acknowledged with its own number");

	// G12
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(852), Nonce(4))));
		Step();
		check(Count(WIRE_ACK) == 0 && !Restarting(), "guest: a START for a map that cannot be played is not acknowledged");
	}
	else
		check(false, "guest: a START for a map that cannot be played is not acknowledged");

	// G13: the guest's wait must outlast the host's own wait plus the answer's way back. The host answers an acknowledgement it holds at 2.9 s with a GO, so a GO can still be on its way
	// 3.5 s after the START: it starts the guest. (kStepGuestMs and kStepHostMs above are the numbers of the design, not read from the lobby: a lobby whose guest gives up at 1 s fails this.)
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		Step(3500);
		check(!Restarting() && L.state == CE_LOBBY_READY, "guest: still waiting 3.5 s after the START, longer than the host's own wait");
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		check(Restarting(), "guest: a GO that arrives 3.5 s after the START, after the host's own wait, still starts the guest");
	}
	else
		check(false, "guest: a GO that arrives 3.5 s after the START, after the host's own wait, still starts the guest");

	// G14: and the other edge: a GO that arrives 5.5 s after the START finds the guest's wait over (it gave up at 5 s) and is ignored
	if (GuestReady())
	{
		Deliver(Msg(WIRE_START, Cat(Be32(856), Nonce(4))));
		Step();
		Step(5500);
		Deliver(Msg(WIRE_GO, Nonce(4)));
		Step();
		check(!Restarting() && !FileExists(HandoffFile()), "guest: a GO that arrives 5.5 s after the START, after the guest's own wait, is ignored");
	}
	else
		check(false, "guest: a GO that arrives 5.5 s after the START, after the guest's own wait, is ignored");
}
}

#ifdef CE_LOBBY_CAMPAIGN_FIXTURE
static void CampaignScenarios() {
    check(HostReady(), "campaign host reaches authenticated ready");
    check(CE_LobbyCampaignStart(L.level + 1) == CE_LOBBY_CAMPAIGN_START_STALE_SELECTION && !L.startSent,
          "stale displayed mission never queues launch");
    check(CE_LobbyCampaignStart(L.level) == CE_LOBBY_CAMPAIGN_START_QUEUED,
          "campaign request queues after admission");
    check(CE_LobbyCampaignStart(L.level) == CE_LOBBY_CAMPAIGN_START_NOT_READY, "a pending campaign request refuses duplicate admission");
    check(Count(MSG_CAMPAIGN_START) == 1 && Count(MSG_START) == 0,
          "campaign request uses a distinct packet");
    std::vector<uint8_t> start;
    for (const auto &packet : N.outbox)
        if (packet.type == MSG_CAMPAIGN_START) start = Msg(packet.type, packet.payload);
    std::vector<uint8_t> nonce(L.nonce, L.nonce + sizeof(L.nonce));
    Deliver(Msg(MSG_CAMPAIGN_ACK, nonce));
    Step();
    check(Restarting() && Count(MSG_CAMPAIGN_GO) == 1 && Count(MSG_GO) == 0,
          "authenticated campaign acknowledgement queues restart");
    std::vector<uint8_t> go;
    for (const auto &packet : N.outbox)
        if (packet.type == MSG_CAMPAIGN_GO) go = Msg(packet.type, packet.payload);
    CE_Handoff host = {};
    check(ReadHandoff(&host) && host.campaign.version == 1 && CE_HandoffCampaignValid(&host),
          "host writes a typed nonce-bound campaign handoff");
    check(GuestReady(), "campaign guest reaches authenticated ready");
    Deliver(start);
    Step();
    check(L.campaign && Count(MSG_CAMPAIGN_ACK) == 1 && Count(MSG_ACK) == 0,
          "guest acknowledges campaign without ordinary fallback");
    Deliver(go);
    Step();
    N.phase = CE_NET_CLOSED;
    Step();
    CE_Handoff guest = {};
    check(Restarting() && ReadHandoff(&guest) && guest.campaign.version == 1 &&
        guest.campaign.session == host.campaign.session && guest.campaign.attempt == host.campaign.attempt &&
        guest.campaign.mission == host.campaign.mission && guest.role != host.role,
          "guest restart keeps the same identity and its own role");
    check(HostReady(), "mixed-version host ready");
    check(CE_LobbyCampaignStart(L.level) == CE_LOBBY_CAMPAIGN_START_QUEUED,
          "mixed-version request initially queues");
    nonce.assign(L.nonce, L.nonce + sizeof(L.nonce));
    Deliver(Msg(MSG_ACK, nonce));
    Step();
    check(!Restarting() && !FileExists(HandoffFile()), "ordinary acknowledgement cannot admit campaign restart");
    check(HostReady(), "cancel host ready");
    check(CE_LobbyCampaignStart(L.level) == CE_LOBBY_CAMPAIGN_START_QUEUED, "cancel request queues");
    CE_LobbyLeave();
    check(!L.campaign && !L.startSent && !Restarting() && !FileExists(HandoffFile()),
          "cancel clears campaign and produces no handoff");
    check(GuestReady(), "not-host request fixture ready");
    check(CE_LobbyCampaignStart(L.level) == CE_LOBBY_CAMPAIGN_START_NOT_HOST,
          "guest cannot create campaign launch intent");
    check(HostReady(), "ordinary restart after campaign ready");
    check(CE_LobbyStart() && !L.campaign && Count(MSG_START) == 1 && Count(MSG_CAMPAIGN_START) == 0,
          "ordinary start remains ordinary after campaign attempts");
    CE_LobbyLeave();
}
#endif


void GameBackendScenarios() {
    Reset();
    gGameCreateFail = true; gGameCreateProblem = "fixture relay preparation failed";
    check(!CE_LobbyHost() && gGameCreateCalls == 1, "backend: host uses lobby factory");
    CE_LobbyStatus status = {}; CE_LobbyStatusGet(&status);
    check(gNullProblemCalls == 1 && !strcmp(status.notice, "fixture relay preparation failed"),
        "backend: host creation reports actual backend problem");

    Reset();
    const char *problem = NULL;
    gGameCreateFail = true; gGameCreateProblem = "fixture relay preparation failed";
    check(!CE_LobbyJoin("invalid", "", &problem) && gGameCreateCalls == 0 && gNullProblemCalls == 0,
        "backend: invalid code preserves validation without querying backend");

    Reset();
    check(CE_LobbyHost() != 0, "backend: authored join code prepared");
    CE_LobbyStatusGet(&status); const std::string code = status.code;
    CE_LobbyLeave();
    gGameCreateFail = true; gGameCreateProblem = "fixture join preparation failed";
    check(!CE_LobbyJoin(code.c_str(), "", &problem) && problem &&
        !strcmp(problem, "fixture join preparation failed") && gNullProblemCalls == 1,
        "backend: guest creation reports actual backend problem");

    check(HostReady(), "backend: host route fixture ready");
    gGameRouteProblem = "fixture host relay route failed";
    CE_NetStatus failed = {}; failed.phase = CE_NET_FAILED; failed.error = CE_NET_IO;
    unsigned creates = gGameCreateCalls;
    HostFailure(failed); CE_LobbyStatusGet(&status);
    check(status.state == CE_LOBBY_FAILED && !strcmp(status.notice, "fixture host relay route failed") &&
        gSessionProblemCalls == 1 && gGameCreateCalls == creates && !N.exists,
        "backend: host route error survives destruction without silent retry");

    check(GuestReady(), "backend: guest route fixture ready");
    gGameRouteProblem = "fixture guest relay route failed";
    failed.error = CE_NET_TIMEOUT;
    GuestFailure(failed); CE_LobbyStatusGet(&status);
    check(status.state == CE_LOBBY_FAILED && !strcmp(status.notice, "fixture guest relay route failed") &&
        gSessionProblemCalls == 1 && !N.exists,
        "backend: guest route error survives destruction");

    check(GuestReady(), "backend: authentication fixture ready");
    gGameRouteProblem = "fixture unrelated relay text";
    failed.error = CE_NET_AUTH;
    GuestFailure(failed); CE_LobbyStatusGet(&status);
    check(gSessionProblemCalls == 0 && strcmp(status.notice, "fixture unrelated relay text"),
        "backend: authentication error keeps its own cause");

    check(GuestReady(), "backend: peer leave fixture ready");
    gGameRouteProblem = "fixture unrelated relay text";
    failed.error = CE_NET_PEER_LEFT; failed.phase = CE_NET_CLOSED;
    GuestFailure(failed);
    check(gSessionProblemCalls == 0, "backend: peer leave does not acquire an unrelated route error");
    check(HostReady(), "backend: host protocol fixture ready");
    gGameRouteProblem = "fixture host relay protocol failed";
    failed.phase = CE_NET_FAILED; failed.error = CE_NET_PROTOCOL;
    creates = gGameCreateCalls;
    HostFailure(failed); CE_LobbyStatusGet(&status);
    check(status.state == CE_LOBBY_FAILED && !strcmp(status.notice, "fixture host relay protocol failed") &&
        gSessionProblemCalls == 1 && gGameCreateCalls == creates && !N.exists,
        "backend: host protocol cause survives destruction without silent retry");

    check(GuestReady(), "backend: guest protocol fixture ready");
    gGameRouteProblem = "fixture guest relay protocol failed";
    creates = gGameCreateCalls;
    GuestFailure(failed); CE_LobbyStatusGet(&status);
    check(status.state == CE_LOBBY_FAILED && !strcmp(status.notice, "fixture guest relay protocol failed") &&
        gSessionProblemCalls == 1 && gGameCreateCalls == creates && !N.exists,
        "backend: guest protocol cause survives destruction without silent retry");

    Reset();
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
#ifdef CE_LOBBY_CAMPAIGN_FIXTURE
	CampaignScenarios();
#endif
    GameBackendScenarios();
    HostScenarios();
	GuestScenarios();
	printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures ? 1 : 0;
}

extern "C" int CE_BootCampaignAdmit(const CE_Handoff *handoff) { return CE_HandoffCampaignValid(handoff); }
