#include "../../game/ce_network_boot.h"
#include "../../game/ce_network_campaign_return.h"
// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_netlobby.cpp: the network game's lobby and the restart into the game process and back
// (ce_netlobby.h). Public builds offer it from the CE page (port/game/ce_netpages.cpp); nothing here runs otherwise,
// except reading a handoff the process was explicitly restarted with (BEA_NET_HANDOFF, set only by this file).
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ce_netcode.h"
#include "ce_netlobby.h"
#include "../../network/network.h"
#include "../../network/ice_game.h"
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
#include "../../game/ce_network_boot_authority.h"
#include <limits.h>
#endif
#endif
#include "../../network/compatibility.h"

extern "C" int CE_SettingGet(const char *key, int def);	// ce.cpp
extern "C" const char *CE_BootError(void);					// port/game/ce_network_boot.cpp
extern "C" const char *CE_SessionError(void);				// port/game/ce_network_session.cpp
#ifdef CE_VERSION
bool PortCeQuitRequested();									// ltshell.cpp (patch 0059): the window's close button
#else
static bool PortCeQuitRequested() { return false; }
#endif

#ifndef CE_NETWORK_SIMULATION_ID
#define CE_NETWORK_SIMULATION_ID ""
#endif
#ifdef CE_VERSION
static const char kVersion[] = CE_VERSION;
#else
static const char kVersion[] = "private build";
#endif

#define LOBBY_LOG "onslaught-ce: network lobby: "

#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
struct CeLobbySignalingTransition {
    static bool Authorize(CeBootSignalAuthority& owner, CE_NetRole role, uint32_t level,
                          const uint8_t secret[32], uint64_t utc, uint64_t mono) noexcept {
        const CeAuthenticatedLobbyStart start(role, level, secret, utc, mono);
        return owner.Authorize(start);
    }
};
#endif
#endif
namespace {
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
std::shared_ptr<CeBootSignalAuthority> gSignalAuthority;
CeSignalAuthorityFd gSignalDescriptor;
bool gSignalSelected = false, gSignalPreflight = false, gSignalClosed = false;
bool gSignalProviderSelected = false;
bool gSignalBootRequired = false;
uint32_t gSignalBootLevel = 0;
CE_NetConfig gSignalBootConfig{};
void SetEnv(const char*, const char*);
void SignalTerminal() {
    if (!gSignalSelected || gSignalClosed) return;
    gSignalClosed = true; gSignalPreflight = false;
    if (gSignalProviderSelected) CE_NetSignalingClosePair();
    gSignalDescriptor.Reset();
    if (gSignalAuthority) gSignalAuthority->Revoke();
    CE_CodeWipe(&gSignalBootConfig, sizeof gSignalBootConfig);
}
bool SignalPrepareStart(const CE_Handoff& handoff) {
    if (!gSignalSelected) return true;
    if (!gSignalAuthority || gSignalClosed) return false;
    uint64_t mono = CE_NetNowMs();
    CE_NetRole role = handoff.role == 1 ? CE_NET_HOST : CE_NET_GUEST;
    if (!CeLobbySignalingTransition::Authorize(*gSignalAuthority, role, handoff.level,
            handoff.secret, uint64_t(handoff.created), mono)) return false;
    if (!gSignalAuthority->SetLaunch(handoff)) return false;
    gSignalDescriptor = gSignalAuthority->Seal(uint64_t(handoff.created), mono);
    return bool(gSignalDescriptor);
}
void SignalCaptureDescriptor() {
    const char* value = getenv("BEA_NET_SIGNAL_AUTHORITY_FD");
    if (!value) return;
    unsigned descriptor = 0; size_t digits = 0; bool valid = *value != 0;
    for (const char* at = value; *at; ++at) {
        if (++digits > 10 || *at < '0' || *at > '9' || descriptor > (unsigned(INT_MAX) - unsigned(*at - '0')) / 10) { valid = false; break; }
        descriptor = descriptor * 10 + unsigned(*at - '0');
    }
    SetEnv("BEA_NET_SIGNAL_AUTHORITY_FD", NULL);
    gSignalSelected = true; gSignalBootRequired = true;
    if (valid && descriptor >= 3) gSignalAuthority = CeBootSignalAuthority::Consume(int(descriptor), uint64_t(CE_HandoffNow()), CE_NetNowMs());
    gSignalClosed = !gSignalAuthority;
}
bool SignalAdmitLaunch(const CE_Handoff& handoff) {
    if (!gSignalBootRequired) return true;
    CE_NetRole role = handoff.role == 1 ? CE_NET_HOST : CE_NET_GUEST;
    if ((handoff.role != 1 && handoff.role != 2) || !gSignalAuthority ||
        !gSignalAuthority->MatchesStart(role, handoff.level, handoff.secret)) return false;
    gSignalBootLevel = handoff.level; gSignalBootConfig.role = role;
    memcpy(gSignalBootConfig.secret, handoff.secret, sizeof gSignalBootConfig.secret);
    return true;
}
#endif
#endif
const uint32_t kMagic = 0x43454c31;		// "CEL1"
enum { MSG_INFO = 1, MSG_START = 2, MSG_ACK = 3, MSG_GO = 4 };
const int kMaxFailed = 5;
// The host waits kStartWaitMs for the guest's acknowledgement; the guest waits kGuestWaitMs from the START (longer: the host answers an acknowledgement at once, so the guest's
// window covers the host's wait and the answer's way back) for the host's GO. Neither starts the game on silence.
const uint64_t kStartWaitMs = 3000, kGuestWaitMs = 5000;
// A host that has waited this long without anybody reaching it is told what it cannot know: the game cannot see the other player's typing, nor the routers and firewalls between them.
const uint64_t kNobodyWaitMs = 45000;

static const uint8_t MSG_CAMPAIGN_START = 101;
static const uint8_t MSG_CAMPAIGN_ACK = 102;
static const uint8_t MSG_CAMPAIGN_GO = 103;

// The lobby's state names, for the private trace (stderr, fixed labels only).
const char *StateName(int state)
{
	switch (state)
	{
	case CE_LOBBY_OFF: return "off";
	case CE_LOBBY_HOST_WAITING: return "host_waiting";
	case CE_LOBBY_HOST_CONNECTING: return "host_connecting";
	case CE_LOBBY_JOIN_CONNECTING: return "join_connecting";
	case CE_LOBBY_READY: return "ready";
	case CE_LOBBY_REFUSED: return "refused";
	case CE_LOBBY_FAILED: return "failed";
	case CE_LOBBY_LEFT: return "left";
	case CE_LOBBY_STARTING: return "starting";
	}
	return "unknown";
}

// The lobby's state: giving it a value that differs from the current one prints the new state's name.
struct LobbyState
{
	int value = CE_LOBBY_OFF;
	LobbyState() {}
	LobbyState(int initial) : value(initial) {}
	LobbyState &operator=(int next)
	{
		if (next != value)
		{
			value = next;
			fprintf(stderr, LOBBY_LOG "state %s\n", StateName(next));
		}
		return *this;
	}
	operator int() const { return value; }
};

struct Lobby
{
    bool campaign = false;
	LobbyState state;
	bool host = false;
	CE_NetSession *net = NULL;
	uint64_t key = 0;
	uint8_t secret[32] = {};
	uint8_t nonce[16] = {};
	uint32_t ip = 0;				// host: the address in the code; guest: where it connects (host order)
	uint16_t port = 0;
	char bind[16] = {};				// host: the address it listens on
	bool sentInfo = false, gotInfo = false;
	uint32_t level = CE_LOBBY_DEFAULT_LEVEL;	// the map: the host's choice, told to the guest in INFO and START
	bool startSent = false;			// host: START queued; guest: START received. Either way an attempt is open until the start, a failure or its deadline
	bool ackQueued = false;			// guest: the acknowledgement of this attempt's START is queued (retried while the transport pushes back)
	uint64_t startAt = 0;
	uint64_t waitingAt = 0;			// host: when it began (or went back to) waiting for a player
	bool nobodyTold = false;		// host: the note that nobody has reached it is showing
	bool connected = false;			// host: the current attempt reached the lobby
	CE_LobbyStatus st = {};
};
Lobby L;

// The restart: what is due when the process exits.
enum { RESTART_NONE = 0, RESTART_LAUNCH, RESTART_RETURN };
int gRestart = RESTART_NONE;
uint32_t gStartLevel = CE_LOBBY_DEFAULT_LEVEL;	// the map this process was told to start (the restart's -level)
bool gLaunched = false;				// this process was started from a launch handoff
char gReturnArgs[1024];				// the front end's arguments, kept by a launched game process
bool gShellExited = false;
int gShellCode = 0, gShellNativeQuit = 0;
bool gLevelToSystem = false;	// the last -level run ended by quitting to the system (the window closed)
bool gOutcomeReady = false;
CE_Handoff gOutcome;
bool OwnArgs(char *out, size_t n);

void Text(char *out, size_t n, const char *fmt, const char *a = "", const char *b = "")
{
	snprintf(out, n, fmt, a, b);
}

const char *UserDir()
{
	const char *u = getenv("BEA_USER_DIR");
	return u && *u ? u : NULL;
}

bool HandoffPath(char *out, size_t n, const char *name)
{
	const char *u = UserDir();
	if (!u)
		return false;
	int k = snprintf(out, n, "%s/%s", u, name);
	return k > 0 && (size_t)k < n;
}

// Each restart owns a fresh entry. The owner receipt crosses only that restart.
CE_CampaignLaunch gContinuation = {};
uint64_t gContinuationAttempt = 0;
char gHandoffName[128] = {};
char gHandoffPath[4096] = {};
char gHandoffReceipt[512] = {};
CE_HandoffOwner gHandoffOwner = {};
char gIncomingHandoffName[128] = {};
char gIncomingHandoffReceipt[512] = {};

void SetEnv(const char *key, const char *value);

void ClearPendingHandoff() {
    if (gHandoffPath[0])
        CE_HandoffOwnedClear(gHandoffPath, &gHandoffOwner, NULL);
    CE_CodeWipe(&gHandoffOwner, sizeof(gHandoffOwner));
    CE_CodeWipe(gHandoffReceipt, sizeof(gHandoffReceipt));
    gHandoffName[0] = gHandoffPath[0] = 0;
    SetEnv("BEA_NET_HANDOFF_FILE", NULL);
    SetEnv("BEA_NET_HANDOFF_OWNER", NULL);
}

bool WriteOwnedHandoff(const char *kind, const CE_Handoff *handoff,
                       const char **error = NULL) {
    ClearPendingHandoff();
    uint8_t random[CE_NET_SECRET_BYTES] = {};
    if (!CE_NetGenerateSecret(random)) {
        if (error) *error = "cannot allocate a fresh handoff";
        return false;
    }
    char hex[CE_NET_SECRET_BYTES * 2 + 1] = {};
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(random); ++i) {
        hex[2 * i] = digits[random[i] >> 4];
        hex[2 * i + 1] = digits[random[i] & 15];
    }
    CE_CodeWipe(random, sizeof(random));
    snprintf(gHandoffName, sizeof(gHandoffName), "ce-%s-%s.handoff", kind, hex);
    CE_CodeWipe(hex, sizeof(hex));
    if (!HandoffPath(gHandoffPath, sizeof(gHandoffPath), gHandoffName) ||
        !CE_HandoffOwnedWrite(gHandoffPath, handoff, &gHandoffOwner, error)) {
        // Retain a failed artifact. A retry always chooses a fresh entry.
        CE_CodeWipe(&gHandoffOwner, sizeof(gHandoffOwner));
        gHandoffName[0] = gHandoffPath[0] = 0;
        return false;
    }
    if (!CE_HandoffOwnerFormat(&gHandoffOwner, gHandoffReceipt,
                               sizeof(gHandoffReceipt))) {
        ClearPendingHandoff();
        if (error) *error = "cannot encode handoff ownership";
        return false;
    }
    return true;
}

void CaptureHandoffEnvironment() {
    gIncomingHandoffName[0] = gIncomingHandoffReceipt[0] = 0;
    const char *name = getenv("BEA_NET_HANDOFF_FILE");
    const char *owner = getenv("BEA_NET_HANDOFF_OWNER");
    if (name && owner && strlen(name) < sizeof(gIncomingHandoffName) &&
        strlen(owner) < sizeof(gIncomingHandoffReceipt)) {
        snprintf(gIncomingHandoffName, sizeof(gIncomingHandoffName), "%s", name);
        snprintf(gIncomingHandoffReceipt, sizeof(gIncomingHandoffReceipt), "%s", owner);
    }
    SetEnv("BEA_NET_HANDOFF_FILE", NULL);
    SetEnv("BEA_NET_HANDOFF_OWNER", NULL);
}

bool ReadOwnedHandoff(const char *kind, int64_t now, CE_Handoff *handoff,
                      const char **error) {
    if (error) *error = NULL;
    char prefix[32] = {};
    snprintf(prefix, sizeof(prefix), "ce-%s-", kind);
    size_t prefixSize = strlen(prefix);
    const size_t suffixSize = sizeof(".handoff") - 1;
    size_t nameSize = strlen(gIncomingHandoffName);
    bool nameOk = nameSize == prefixSize + CE_NET_SECRET_BYTES * 2 + suffixSize &&
        strncmp(gIncomingHandoffName, prefix, prefixSize) == 0 &&
        strcmp(gIncomingHandoffName + nameSize - suffixSize, ".handoff") == 0;
    for (size_t i = prefixSize; nameOk && i < nameSize - suffixSize; ++i) {
        char c = gIncomingHandoffName[i];
        nameOk = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    CE_HandoffOwner owner = {};
    char path[4096] = {};
    bool ok = nameOk &&
        CE_HandoffOwnerParse(gIncomingHandoffReceipt,
                            strlen(gIncomingHandoffReceipt), &owner) &&
        HandoffPath(path, sizeof(path), gIncomingHandoffName) &&
        CE_HandoffOwnedRead(path, &owner, now, handoff, error);
    if (!ok) {
        if (handoff) memset(handoff, 0, sizeof(*handoff));
        if (error && !*error) *error = "handoff ownership is missing or invalid";
    }
    CE_CodeWipe(&owner, sizeof(owner));
    CE_CodeWipe(gIncomingHandoffReceipt, sizeof(gIncomingHandoffReceipt));
    gIncomingHandoffName[0] = 0;
    return ok;
}

void ResumeCampaignFromOutcome(const CE_Handoff &outcome) {
    gContinuation = {};
    gContinuationAttempt = 0;
    if (outcome.kind != CE_HANDOFF_OUTCOME || outcome.campaign.version != 1)
        return;
    gContinuation = outcome.campaign;
    gContinuationAttempt = outcome.campaign.attempt;
}

bool ContinueCampaignNonce(uint8_t nonce[16]) {
    if (!gContinuation.version) return true;
    if (!gContinuation.session || gContinuationAttempt == UINT64_MAX) return false;
    uint64_t attempt = ++gContinuationAttempt;
    for (unsigned i = 0; i < 8; ++i) {
        nonce[i] = (uint8_t)(gContinuation.session >> ((7 - i) * 8));
        nonce[i + 8] = (uint8_t)(attempt >> ((7 - i) * 8));
    }
    return true;
}

void AttachCampaignReturn(CE_Handoff *handoff, bool good) {
    CeCampaignReturn completed = {};
    if (!good || !ce_campaign_return_get(&completed)) return;
    handoff->campaign = {1, completed.identity.session, completed.identity.attempt,
                         completed.identity.mission};
    handoff->good = completed.outcome == CE_CAMPAIGN_OUTCOME_WIN;
    snprintf(handoff->text, sizeof(handoff->text), "%s",
             handoff->good ? "Campaign victory agreed. Choose the next mission in the lobby."
                           : "Campaign defeat agreed. Retry this mission in the lobby.");
}



void SetEnv(const char *name, const char *value)
{
#ifdef _WIN32
	char both[1200];
	snprintf(both, sizeof both, "%s=%s", name, value ? value : "");
	_putenv(both);
#else
	if (value)
		setenv(name, value, 1);
	else
		unsetenv(name);
#endif
}

void Hex(const uint8_t *p, size_t n, char *out)
{
	static const char d[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++)
	{
		out[2 * i] = d[p[i] >> 4];
		out[2 * i + 1] = d[p[i] & 15];
	}
	out[2 * n] = 0;
}

void Dotted(uint32_t ip, char *out, size_t n)
{
	snprintf(out, n, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

bool ParseIp(const char *s, uint32_t *ip)
{
	unsigned a, b, c, d;
	char tail;
	if (!s || sscanf(s, "%3u.%3u.%3u.%3u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
		return false;
	*ip = (a << 24) | (b << 16) | (c << 8) | d;
	return true;
}

bool Private(uint32_t ip)
{
	return (ip >> 24) == 10 || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8;
}

// Only the test harness, offscreen: the host writes its code to this new private file so a second process can type it.
void HarnessCodeOut(const char *code)
{
	const char *path = getenv("BEA_NET_LOBBY_CODE_OUT");
	const char *require = getenv("BEA_PORT_REQUIRE_OFFSCREEN");
	const char *driver = SDL_GetCurrentVideoDriver();
	if (!path || !*path || !require || strcmp(require, "1") || !driver || strcmp(driver, "offscreen"))
		return;
#ifndef _WIN32
	FILE *f = NULL;
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd >= 0)
		f = fdopen(fd, "w");
#else
	FILE *f = fopen(path, "wx");
#endif
	if (f)
	{
		fprintf(f, "%s\n", code);
		fclose(f);
		fprintf(stderr, LOBBY_LOG "join code written for the test harness (offscreen run)\n");
	}
}

void Close(bool graceful)
{
	if (L.net)
	{
		if (graceful)
		{
			CE_NetClose(L.net);
			CE_NetPoll(L.net, CE_NetNowMs(), 16);
		}
		CE_NetDestroy(L.net);
		L.net = NULL;
	}
	L.sentInfo = L.gotInfo = L.startSent = L.ackQueued = L.connected = false;
}

void Forget()
{
	Close(false);
	CE_CodeWipe(L.secret, sizeof L.secret);
	CE_CodeWipe(&L.key, sizeof L.key);
	CE_CodeWipe(L.st.code, sizeof L.st.code);
	L = Lobby();
    if (gContinuation.version) L.level = gContinuation.mission;
}

bool Create()
{
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    if (gSignalSelected && CE_LobbySignalingView().status != CeIceSignalStatus::Admitted) return false;
#endif
#endif
    L.campaign = false;
	CE_NetConfig c;
	memset(&c, 0, sizeof c);
	c.role = L.host ? CE_NET_HOST : CE_NET_GUEST;
	char ip[16];
	if (L.host)
		snprintf(ip, sizeof ip, "%s", L.bind);
	else
		Dotted(L.ip, ip, sizeof ip);
	c.ipv4 = ip;
	c.port = L.port;
	memcpy(c.secret, L.secret, 32);
	CE_CodeLobbyDigest(CE_NETWORK_SIMULATION_ID, kVersion, c.compatibility);
	c.handshake_ms = 5000;
	c.silence_ms = 10000;
	snprintf(c.version, sizeof c.version, "%s", kVersion);
	L.net = CE_NetCreateLobby(&c);
	CE_CodeWipe(c.secret, sizeof c.secret);
	L.sentInfo = L.gotInfo = L.startSent = L.ackQueued = L.connected = false;
	return L.net != NULL;
}

const char *CreateProblem() { return CE_NetGameProblem(NULL); }


bool GameRouteProblem(const CE_NetStatus &status, char *problem, size_t size) {
    if (!L.net ||
        (status.error != CE_NET_IO && status.error != CE_NET_TIMEOUT && status.error != CE_NET_BAD_CONFIG && status.error != CE_NET_PROTOCOL))
        return false;
    const char *backend_problem = CE_NetGameProblem(L.net);
    if (!backend_problem || !backend_problem[0]) return false;
    Text(problem, size, backend_problem);
    return true;
}


bool NewCode()
{
	if (!CE_CodeNewKey(&L.key))
		return false;
	CE_CodeLobbySecret(L.key, L.secret);
	CE_JoinCode code = {L.key, L.ip, L.port};
	bool ok = CE_CodeEncode(&code, L.st.code, sizeof L.st.code) != 0;
	CE_CodeWipe(&code, sizeof code);
	L.st.failed = 0;
	if (ok)
		HarnessCodeOut(L.st.code);
	return ok;
}

bool Send(uint8_t type, const uint8_t *extra, uint32_t n)
{
	uint8_t m[128];
	CE_NetWriter w;
	CE_NetWriterInit(&w, m, sizeof m);
	CE_NetWriteU32(&w, kMagic);
	CE_NetWriteU8(&w, type);
	if (extra)
		CE_NetWriteBytes(&w, extra, n);
	return !w.error && CE_NetSend(L.net, CE_NET_CONTROL, 0, m, w.size) == 1;
}

bool SendInfo()
{
	uint8_t extra[CE_NET_VERSION_BYTES + 4] = {};
	memcpy(extra, kVersion, strlen(kVersion) < CE_NET_VERSION_BYTES ? strlen(kVersion) : CE_NET_VERSION_BYTES - 1);
	uint32_t level = L.level;
	extra[CE_NET_VERSION_BYTES] = uint8_t(level >> 24);
	extra[CE_NET_VERSION_BYTES + 1] = uint8_t(level >> 16);
	extra[CE_NET_VERSION_BYTES + 2] = uint8_t(level >> 8);
	extra[CE_NET_VERSION_BYTES + 3] = uint8_t(level);
	return Send(MSG_INFO, extra, sizeof extra);
}

// The guest's acknowledgement of the START it holds. A transport that pushes back refuses it (CE_NetSend returns 0): that is not an acknowledgement, so the attempt stays open
// and CE_LobbyUpdate asks again until the attempt's deadline.
void SendAck()
{
	if (L.net && L.startSent && !L.ackQueued && Send(L.campaign ? MSG_CAMPAIGN_ACK : MSG_ACK, L.nonce, 16))
		L.ackQueued = true;
}

void Label(const uint8_t *p, char *out, size_t n)
{
	size_t i = 0;
	for (; i < CE_NET_VERSION_BYTES && i + 1 < n && p[i]; i++)
		out[i] = (p[i] >= 0x20 && p[i] < 0x7f) ? char(p[i]) : '?';
	out[i] = 0;
}

// Both processes write the handoff, then the page quits the game and the exit starts the game process. The guest calls this when the host's GO arrives. The host calls it when the guest's
// acknowledgement has arrived: it writes its handoff, tells the guest to start (GO) and only then leaves (a graceful leave, which delivers GO first). A failure before GO does not start the guest.
// If GO cannot be queued, the host withdraws its handoff and permits another Start attempt.
void BeginRestart()
{
	CE_Handoff h;
	memset(&h, 0, sizeof h);
	h.kind = CE_HANDOFF_LAUNCH;
	h.role = L.host ? 1 : 2;
	char ip[16];
	if (L.host)
		snprintf(h.endpoint, sizeof h.endpoint, "%s:%u", L.bind, (unsigned)L.port);
	else
	{
		Dotted(L.ip, ip, sizeof ip);
		snprintf(h.endpoint, sizeof h.endpoint, "%s:%u", ip, (unsigned)L.port);
	}
	CE_CodeStartSecret(L.secret, L.nonce, h.secret);
	memcpy(h.nonce, L.nonce, 16);
	h.level = L.level;
	gStartLevel = L.level;
	h.created = CE_HandoffNow();
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    bool ok = OwnArgs(h.args, sizeof h.args) && ((!L.campaign || CE_HandoffCampaignSet(&h)) && (gSignalSelected ? SignalPrepareStart(h) : WriteOwnedHandoff("launch", &h)));
#else
	bool ok = OwnArgs(h.args, sizeof h.args) && ((!L.campaign || CE_HandoffCampaignSet(&h)) && WriteOwnedHandoff("launch", &h));
#endif
#else
	bool ok = OwnArgs(h.args, sizeof h.args) && ((!L.campaign || CE_HandoffCampaignSet(&h)) && WriteOwnedHandoff("launch", &h));
#endif
	CE_CodeWipe(&h, sizeof h);
	if (!ok)
	{
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
        SignalTerminal();
#endif
#endif
		Close(false);
		if (CE_NetGameIceRequested())
		{
			// Closing retains the one-use exchange files. This context cannot listen again.
			L.state = CE_LOBBY_FAILED;
			Text(L.st.notice, sizeof L.st.notice, "Could not write the private start file. Relaunch both games with fresh private lobby and game folders and repeat the code and file exchange.");
			return;
		}
		Text(L.st.notice, sizeof L.st.notice, "The game could not be started: its private start file could not be written.");
		L.state = L.host ? CE_LOBBY_HOST_WAITING : CE_LOBBY_FAILED;
		if (L.host && !Create())
		{
			L.state = CE_LOBBY_FAILED;
			Text(L.st.notice, sizeof L.st.notice, "The game could not be started, and this PC could not listen again.");
		}
		return;
	}
	if (L.host && !Send(L.campaign ? MSG_CAMPAIGN_GO : MSG_GO, L.nonce, 16))
	{
		// The guest cannot be told to start, so it will not: this process does not start either.
		ClearPendingHandoff();
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
        if (gSignalSelected) {
            SignalTerminal(); Close(false); L.state = CE_LOBBY_FAILED;
            Text(L.st.notice, sizeof L.st.notice, "Online start did not complete. Leave before trying again.");
            return;
        }
#endif
#endif
		L.startSent = false;
		Text(L.st.notice, sizeof L.st.notice, "The other player could not be told to start. Try Start again.");
		return;
	}
	Close(L.host);
	fprintf(stderr, LOBBY_LOG "start agreed (level %u, %s); restarting into the game\n", (unsigned)L.level,
			L.host ? "host" : "guest");
	L.state = CE_LOBBY_STARTING;
	gRestart = RESTART_LAUNCH;
}

void Receive()
{
	CE_NetMessage m;
	uint8_t buf[256];
	int r;
	while (L.net && (r = CE_NetReceive(L.net, &m, buf, sizeof buf)) != 0)
	{
		if (r < 0 || m.kind != CE_NET_CONTROL || m.size < 5)
			continue;
		CE_NetReader rd;
		CE_NetReaderInit(&rd, buf, m.size);
		if (CE_NetReadU32(&rd) != kMagic)
			continue;
		uint8_t type = CE_NetReadU8(&rd);
		if (type == MSG_INFO && m.size == 5 + CE_NET_VERSION_BYTES + 4)
		{
			uint8_t label[CE_NET_VERSION_BYTES];
			CE_NetReadBytes(&rd, label, sizeof label);
			uint32_t theirs = CE_NetReadU32(&rd);
			if (!L.host && CE_CompatibilityLockstepLevel(theirs))
				L.level = theirs;			// the guest shows the host's map
			Label(label, L.st.peer_version, sizeof L.st.peer_version);
			L.gotInfo = true;
		}
		else if ((type == MSG_START || type == MSG_CAMPAIGN_START) && !L.host && m.size == 5 + 4 + 16 && L.state == CE_LOBBY_READY)
		{
			uint32_t level = CE_NetReadU32(&rd);
			uint8_t nonce[16];
			CE_NetReadBytes(&rd, nonce, 16);
			if (!CE_CompatibilityLockstepLevel(level))
				continue;
			L.level = level;
			memcpy(L.nonce, nonce, 16);		// a newer START replaces an open attempt (the host's own wait ran out and it pressed Start again)
			L.campaign = type == MSG_CAMPAIGN_START;
        L.startSent = true;				// guest: an attempt is open: acknowledge, then wait for the host's GO; silence never starts the game
			L.ackQueued = false;
			L.startAt = CE_NetNowMs();
			L.st.notice[0] = 0;
			SendAck();
		}
		else if (type == (L.campaign ? MSG_CAMPAIGN_ACK : MSG_ACK) && L.host && L.startSent && m.size == 5 + 16)
		{
			uint8_t nonce[16];
			CE_NetReadBytes(&rd, nonce, 16);
			if (!memcmp(nonce, L.nonce, 16))
			{
				BeginRestart();				// the handoff, then GO, then the graceful leave
				return;
			}
		}
		else if (type == (L.campaign ? MSG_CAMPAIGN_GO : MSG_GO) && !L.host && L.startSent && L.ackQueued && m.size == 5 + 16)
		{
			uint8_t nonce[16];
			CE_NetReadBytes(&rd, nonce, 16);
			if (!memcmp(nonce, L.nonce, 16))
			{
				BeginRestart();				// the host has received the acknowledgement and starts: so does the guest
				return;
			}
		}
	}
}

// A relay lobby that ends in failure says why on stderr, with the sentence the lobby itself shows (fixed text only:
// never a join code, address, record, grant or service text).
void RelayLobbyFailed(const char *sentence)
{
	fprintf(stderr, LOBBY_LOG "relay lobby failed: %s\n", sentence);
}

void HostFailure(const CE_NetStatus &s)
{
    char route_problem[sizeof L.st.notice] = {};
    const bool route_failed = GameRouteProblem(s, route_problem, sizeof route_problem);

	char peer[CE_NET_VERSION_BYTES + 1] = {};
	bool versions = s.error == CE_NET_INCOMPATIBLE && CE_NetPeerVersion(L.net, peer, sizeof peer);
	bool connected = L.connected;		// Close clears it
	Close(false);
    if (route_failed) {
        L.state = CE_LOBBY_FAILED;
        Text(L.st.notice, sizeof L.st.notice, route_problem);
        RelayLobbyFailed(route_problem);
        return;
    }

    if (CE_NetGameIceRequested()) {
        // This exchange keeps its one-use records after close. Recreating it
        // with the same context cannot admit another player and masks the cause.
        char cause[160] = {};
        const char *logged = cause;		// the line below never carries the other build's label
        if (s.phase == CE_NET_CLOSED && s.error == CE_NET_PEER_LEFT)
            Text(cause, sizeof cause, "The other player left.");
        else if (versions && strcmp(peer, kVersion)) {
            snprintf(cause, sizeof cause, "A player using Onslaught CE %s tried to join. Both players need the same version.", peer);
            logged = "A player using another Onslaught CE version tried to join.";
        }
        else if (versions)
            Text(cause, sizeof cause, "The joining player has a different Onslaught CE build.");
        else
            Text(cause, sizeof cause, connected ? "The relay connection ended." : "The relay connection attempt ended.");
        RelayLobbyFailed(logged);
        snprintf(L.st.notice, sizeof L.st.notice,
                 "%s Ask the operator to prepare fresh private lobby and game folders. Relaunch both games and repeat the private code and file exchange.", cause);
        L.st.peer_version[0] = 0;
        L.state = CE_LOBBY_FAILED;
        return;
    }

	if (connected)
	{
		if (s.error == CE_NET_PEER_LEFT && s.phase == CE_NET_CLOSED)
		{
			Text(L.st.notice, sizeof L.st.notice, "The other player left.");
			fprintf(stderr, LOBBY_LOG "the other player left; waiting for another with the same code\n");
		}
		else
		{
			Text(L.st.notice, sizeof L.st.notice, "The connection to the other player was lost. Waiting for another player with the same code.");
			fprintf(stderr, LOBBY_LOG "connection lost; waiting for another with the same code\n");
		}
	}
	else
	{
		L.st.failed++;
		if (versions && strcmp(peer, kVersion))
			snprintf(L.st.notice, sizeof L.st.notice, "A player with Onslaught CE %s tried to join: both players need the same version.", peer);
		else if (versions)
			snprintf(L.st.notice, sizeof L.st.notice, "A player with another Onslaught CE build (both say %s) tried to join: both players need the same download.", kVersion);
		else
			snprintf(L.st.notice, sizeof L.st.notice, "A connection attempt failed (%d of %d). A wrong code, or the player gave up.",
					 L.st.failed, kMaxFailed);
		fprintf(stderr, LOBBY_LOG "an admission attempt failed (%d of %d)\n", L.st.failed, kMaxFailed);
		if (L.st.failed >= kMaxFailed)
		{
			if (NewCode())
			{
				Text(L.st.notice, sizeof L.st.notice, "Five attempts failed, so this game has a new code. Give the other player the new code.");
				fprintf(stderr, LOBBY_LOG "five failed admissions: a new code was made\n");
			}
		}
	}
	L.st.peer_version[0] = 0;
	L.state = CE_LOBBY_HOST_WAITING;
	L.waitingAt = CE_NetNowMs();
	L.nobodyTold = false;
	if (!Create())
	{ const char *backend_problem = CreateProblem();
		if (backend_problem && backend_problem[0]) Text(L.st.notice, sizeof L.st.notice, backend_problem); else Text(L.st.notice, sizeof L.st.notice, "This PC could not listen for players again.");
		L.state = CE_LOBBY_OFF;
	}
}

void GuestFailure(const CE_NetStatus &s)
{
    char route_problem[sizeof L.st.notice] = {};
    const bool route_failed = GameRouteProblem(s, route_problem, sizeof route_problem);

	char peer[CE_NET_VERSION_BYTES + 1] = {}, here[16];
	Dotted(L.ip, here, sizeof here);
	bool versions = s.error == CE_NET_INCOMPATIBLE && CE_NetPeerVersion(L.net, peer, sizeof peer);
	bool was = L.state == CE_LOBBY_READY;
	Close(false);
    if (route_failed) {
        L.state = CE_LOBBY_FAILED;
        Text(L.st.notice, sizeof L.st.notice, route_problem);
        RelayLobbyFailed(route_problem);
        return;
    }

	const bool relay = CE_NetGameIceRequested() != 0;
	const char *relay_recovery = "Relaunch both games with fresh private lobby and game folders; repeat the code and file exchange.";
	if (s.error == CE_NET_INCOMPATIBLE)
	{
		L.state = CE_LOBBY_REFUSED;
		snprintf(L.st.peer_version, sizeof L.st.peer_version, "%s", versions ? peer : "unknown");
		if (versions && strcmp(peer, kVersion))
			snprintf(L.st.compat, sizeof L.st.compat, "Different Onslaught CE version: yours %s, theirs %s", kVersion, peer);
		else
			snprintf(L.st.compat, sizeof L.st.compat, "Different Onslaught CE build (both say %s): both need the same download", kVersion);
		if (relay) Text(L.st.notice, sizeof L.st.notice, relay_recovery);
		fprintf(stderr, LOBBY_LOG "refused: a different build (yours %s, theirs %s)\n", kVersion, versions ? peer : "unknown");
		return;
	}
	if (relay)
	{
		const bool departed = s.phase == CE_NET_CLOSED && s.error == CE_NET_PEER_LEFT;
		L.state = departed ? CE_LOBBY_LEFT : CE_LOBBY_FAILED;
		const char *cause = departed ? (was ? "The host left the game."
		                                    : "The host closed the game.") :
			(was ? "The relay connection ended." : "The relay connection attempt ended.");
		snprintf(L.st.notice, sizeof L.st.notice, "%s %s", cause, relay_recovery);
		RelayLobbyFailed(cause);
		return;
	}
	if (s.error == CE_NET_PEER_LEFT && s.phase == CE_NET_CLOSED)
	{
		L.state = CE_LOBBY_LEFT;
		Text(L.st.notice, sizeof L.st.notice, was ? "The host left the game." : "The host closed the game.");
		fprintf(stderr, LOBBY_LOG "the host left\n");
		return;
	}
	if (was)
	{
		L.state = CE_LOBBY_FAILED;
		Text(L.st.notice, sizeof L.st.notice, "The connection to the host was lost. Check that the host is still waiting, then join again.");
		fprintf(stderr, LOBBY_LOG "connection to the host lost\n");
		return;
	}
	L.state = CE_LOBBY_FAILED;
	snprintf(L.st.notice, sizeof L.st.notice, "No game answered at %s, port %u. The game cannot tell why: the code or address may be wrong, the host may not be waiting "
			 "(a host makes a new code after five failed attempts), or a router or firewall may be blocking UDP port %u. See " CE_LOBBY_HELP_LABEL ".", here, (unsigned)L.port, (unsigned)L.port);
	fprintf(stderr, LOBBY_LOG "could not join (transport error %u)\n", s.error);
}

#ifdef _WIN32
int gWsa;
#endif
}

extern "C" {

int CE_LobbyAvailable(char *why, size_t size)
{
	const char *problem = NULL;
	const char *game = getenv("BEA_GAME_DIR");
	const char *co_op_host = getenv("BEA_LOCKSTEP_HOST"), *co_op_join = getenv("BEA_LOCKSTEP_JOIN");
	if (!*CE_NETWORK_SIMULATION_ID)
		problem = "Network games need an official Onslaught CE download: this build has no network identity.";
	else if ((co_op_host && *co_op_host) || (co_op_join && *co_op_join))
		problem = "Network game is unavailable while a co-op test session is selected.";
	else if (!UserDir() || !game || !*game)
		problem = "Network games need Onslaught CE started from its launcher (onslaught-ce), which names the game and "
				  "user folders.";
	if (why && size)
		snprintf(why, size, "%s", problem ? problem : "");
	return problem ? 0 : 1;
}

uint16_t CE_LobbyPort(void)
{
	int p = CE_SettingGet("net_port", CE_LOBBY_DEFAULT_PORT);
	return (p >= 1024 && p <= 65535) ? (uint16_t)p : (uint16_t)CE_LOBBY_DEFAULT_PORT;
}

int CE_LobbyAddresses(char (*out)[16], int max)
{
	uint32_t found[16];
	int n = 0;
#ifdef _WIN32
	WSADATA wd;
	if (!gWsa && WSAStartup(MAKEWORD(2, 2), &wd) == 0)
		gWsa = 1;
	char name[256];
	if (gWsa && gethostname(name, sizeof name) == 0)
	{
		addrinfo hints = {}, *res = NULL;
		hints.ai_family = AF_INET;
		if (getaddrinfo(name, NULL, &hints, &res) == 0)
		{
			for (addrinfo *a = res; a && n < 16; a = a->ai_next)
				found[n++] = ntohl(((sockaddr_in *)a->ai_addr)->sin_addr.s_addr);
			freeaddrinfo(res);
		}
	}
#else
	ifaddrs *list = NULL;
	if (getifaddrs(&list) == 0)
	{
		for (ifaddrs *a = list; a && n < 16; a = a->ifa_next)
			if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && (a->ifa_flags & IFF_UP))
				found[n++] = ntohl(((sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
		freeifaddrs(list);
	}
#endif
	// private LAN addresses first, then other addresses, loopback only when there is nothing else
	int count = 0;
	for (int pass = 0; pass < 3; pass++)
		for (int i = 0; i < n && count < max; i++)
		{
			uint32_t ip = found[i];
			bool loop = (ip >> 24) == 127, priv = Private(ip);
			if ((pass == 0 && priv) || (pass == 1 && !priv && !loop && ip) || (pass == 2 && loop && count == 0))
			{
				bool dup = false;
				for (int k = 0; k < count; k++)
				{
					uint32_t have = 0;
					ParseIp(out[k], &have);
					dup |= have == ip;
				}
				if (!dup)
					Dotted(ip, out[count++], 16);
			}
		}
	return count;
}

static int LobbyHost(const char *selected_address) {
	uint32_t selected_ip = 0;
    if (selected_address) {
        char choices[CE_LOBBY_ADDRESS_CHOICES][16];
        const int count = CE_LobbyAddresses(choices, CE_LOBBY_ADDRESS_CHOICES);
        int available = 0;
        for (int index = 0; index < count; ++index) {
            if (strcmp(choices[index], selected_address) == 0) {
                available = 1;
                break;
            }
        }
        if (!available || !ParseIp(selected_address, &selected_ip)) {
            Text(L.st.notice, sizeof(L.st.notice),
                 "Selected network address is unavailable. Choose another.");
            return 0;
        }
    }
    Forget();
	L.host = true;
	L.port = CE_LobbyPort();
	snprintf(L.st.local_version, sizeof L.st.local_version, "%s", kVersion);
	char why[200];
	if (!CE_LobbyAvailable(why, sizeof why))
	{
		snprintf(L.st.notice, sizeof L.st.notice, "%s", why);
		return 0;
	}
	// The address in the code: BEA_NET_LOBBY_BIND (a chosen interface, or loopback for tests), else this PC's first LAN
	// address; the host listens on that interface, or on all of them.
	const char *bind = getenv("BEA_NET_LOBBY_BIND");
	uint32_t ip = 0;
	if (selected_address) {
        ip = selected_ip;
    } else if (bind && *bind)
	{
		if (!ParseIp(bind, &ip) || !ip)
		{
			Text(L.st.notice, sizeof L.st.notice, "BEA_NET_LOBBY_BIND is not an IPv4 address.");
			return 0;
		}
		Dotted(ip, L.bind, sizeof L.bind);
	}
	else
	{
		char addr[1][16];
		if (CE_LobbyAddresses(addr, 1) < 1 || !ParseIp(addr[0], &ip))
		{
			Text(L.st.notice, sizeof L.st.notice, "This PC has no network address.");
			return 0;
		}
		snprintf(L.bind, sizeof L.bind, "0.0.0.0");
	}
	L.ip = ip;
	if (!NewCode())
	{
		Text(L.st.notice, sizeof L.st.notice, "No random numbers were available for a code.");
		return 0;
	}
	if (!Create())
	{ const char *backend_problem = CreateProblem();
		if (backend_problem && backend_problem[0]) Text(L.st.notice, sizeof L.st.notice, backend_problem); else snprintf(L.st.notice, sizeof L.st.notice, "This PC could not listen on UDP port %u: is another game using it?",
				 (unsigned)L.port);
		CE_CodeWipe(L.st.code, sizeof L.st.code);
		return 0;
	}
	L.st.host = 1;
	L.st.port = L.port;
	L.state = CE_LOBBY_HOST_WAITING;
	L.waitingAt = CE_NetNowMs();
	L.nobodyTold = false;
	fprintf(stderr, LOBBY_LOG "hosting on UDP port %u\n", (unsigned)L.port);
	return 1;
}

int CE_LobbyHost() { return LobbyHost(NULL); }

int CE_LobbyHostAt(const char *selected_address) {
    return LobbyHost(selected_address);
}

int CE_LobbyJoin(const char *code, const char *address, const char **problem)
{
	static char why[200];
	Forget();
	L.host = false;
	snprintf(L.st.local_version, sizeof L.st.local_version, "%s", kVersion);
	if (!CE_LobbyAvailable(why, sizeof why))
	{
		*problem = why;
		return 0;
	}
	CE_JoinCode c;
	int r = CE_CodeDecode(code, &c);
	if (r != CE_CODE_OK)
	{
		*problem = CE_CodeProblem(r);
		return 0;
	}
	L.key = c.key;
	L.ip = c.ipv4;
	L.port = c.port;
	CE_CodeWipe(&c, sizeof c);
	if (address && *address)
	{
		char host[32];
		snprintf(host, sizeof host, "%s", address);
		char *colon = strchr(host, ':');
		unsigned port = L.port;
		char tail;
		if (colon)
		{
			*colon = 0;
			if (sscanf(colon + 1, "%5u%c", &port, &tail) != 1 || !port || port > 65535)
			{
				*problem = "The address's port must be a number from 1 to 65535.";
				return 0;
			}
		}
		uint32_t ip;
		if (!ParseIp(host, &ip) || !ip)
		{
			*problem = "The address must be four numbers with dots, for example 203.0.113.5.";
			return 0;
		}
		L.ip = ip;
		L.port = (uint16_t)port;
	}
	CE_CodeLobbySecret(L.key, L.secret);
	if (!Create())
	{ const char *backend_problem = CreateProblem();
		*problem = (backend_problem && backend_problem[0] ? backend_problem : "The connection could not be set up on this PC.");
		return 0;
	}
	Dotted(L.ip, L.st.endpoint, sizeof L.st.endpoint);
	snprintf(L.st.endpoint + strlen(L.st.endpoint), sizeof L.st.endpoint - strlen(L.st.endpoint), ":%u", (unsigned)L.port);
	L.st.port = L.port;
	L.state = CE_LOBBY_JOIN_CONNECTING;
	fprintf(stderr, LOBBY_LOG "joining\n");
	*problem = NULL;
	return 1;
}

void CE_LobbyUpdate(void)
{
	if (!L.net)
		return;
	CE_NetPoll(L.net, CE_NetNowMs(), 64);
	CE_NetStatus s;
	CE_NetGetStatus(L.net, &s);
	if (s.phase == CE_NET_FAILED || s.phase == CE_NET_CLOSED)
	{
		if (L.host)
			HostFailure(s);		// a queued acknowledgement is not read: a player who has left will not start
		else
		{
			// The host starts by sending GO and leaving, so what the transport still holds is read before the loss is judged: GO among it means the host has started.
			// A loss without GO is a failure, whether or not the guest had acknowledged.
			if (L.startSent && L.ackQueued)
			{
				Receive();
				if (!L.net)
					return;			// the restart began
			}
			GuestFailure(s);
		}
		return;
	}
	if (L.host && L.state == CE_LOBBY_HOST_WAITING && s.phase == CE_NET_AUTHENTICATING)
	{
		L.state = CE_LOBBY_HOST_CONNECTING;
		if (L.nobodyTold)
			L.st.notice[0] = 0;			// somebody is reaching this PC after all
		L.nobodyTold = false;
	}
	if (s.phase == CE_NET_ACTIVE)
	{
		if (!L.sentInfo)
			L.sentInfo = SendInfo();
		L.connected = true;
		Receive();
		if (!L.net)
			return;
		if (L.gotInfo && L.state != CE_LOBBY_READY && L.state != CE_LOBBY_STARTING)
		{
			L.state = CE_LOBBY_READY;
			L.st.notice[0] = 0;
			Text(L.st.compat, sizeof L.st.compat, "Compatible");
			fprintf(stderr, LOBBY_LOG "both players are in the lobby (%s, %s)\n", kVersion, L.st.peer_version);
		}
	}
	uint64_t now = CE_NetNowMs();
	if (L.host && L.state == CE_LOBBY_HOST_WAITING && !L.nobodyTold && !L.st.failed && !L.st.notice[0] && now - L.waitingAt >= kNobodyWaitMs)
	{
		L.nobodyTold = true;
		snprintf(L.st.notice, sizeof L.st.notice, "Nobody has reached this PC yet. The game cannot tell why: the other player may not have joined yet, may have typed the code or address "
				 "wrongly, or a router or firewall may be blocking UDP port %u. See " CE_LOBBY_HELP_LABEL ".", (unsigned)L.port);
		fprintf(stderr, LOBBY_LOG "nobody has reached this PC in %u s\n", (unsigned)(kNobodyWaitMs / 1000));
	}
	if (L.host && L.startSent && now - L.startAt > kStartWaitMs)
	{
		L.startSent = false;
		Text(L.st.notice, sizeof L.st.notice, "The other player did not answer the start. Try Start again.");
	}
	if (!L.host && L.startSent)
	{
		SendAck();
		if (now - L.startAt > kGuestWaitMs)
		{
			L.startSent = L.ackQueued = false;
			Text(L.st.notice, sizeof L.st.notice, "The host did not start the game. Wait for the host to try again.");
		}
	}
}

void CE_LobbyStatusGet(CE_LobbyStatus *out)
{
	L.st.state = L.state;
	L.st.host = L.host ? 1 : 0;
	L.st.level = L.level;
	if (!L.st.local_version[0])
		snprintf(L.st.local_version, sizeof L.st.local_version, "%s", kVersion);
	*out = L.st;
}

int CE_LobbyNextLevel(void)
{
	static const uint32_t levels[] = {856, 100};		// the co-op maps the lobby offers: the original ships the building defence twice (856 and 858, the same map), so one entry; the gates admit 858 too (compatibility.h)
	if (!L.host || L.startSent || L.state == CE_LOBBY_OFF || L.state == CE_LOBBY_STARTING)
		return 0;
	size_t i = 0;
	while (i < sizeof levels / sizeof levels[0] && levels[i] != L.level)
		i++;
	L.level = levels[(i + 1) % (sizeof levels / sizeof levels[0])];
	if (L.net && L.state == CE_LOBBY_READY)
		SendInfo();
	return 1;
}

static int LobbyStart(bool campaign)
{
	if (!L.host || L.state != CE_LOBBY_READY || L.startSent || !L.net)
		return 0;
	L.campaign = campaign;
    uint8_t r[32];
	if (!CE_NetGenerateSecret(r))
		return 0;
	memcpy(L.nonce, r, 16);
	CE_CodeWipe(r, sizeof r);
    if (campaign && !ContinueCampaignNonce(L.nonce)) {
        Text(L.st.notice, sizeof(L.st.notice), "Campaign continuation cannot start another attempt.");
        return 0;
    }
	uint8_t extra[20];
	uint32_t level = L.level;
	extra[0] = uint8_t(level >> 24); extra[1] = uint8_t(level >> 16); extra[2] = uint8_t(level >> 8); extra[3] = uint8_t(level);
	memcpy(extra + 4, L.nonce, 16);
	if (!Send(L.campaign ? MSG_CAMPAIGN_START : MSG_START, extra, sizeof extra))
		return 0;
	L.startSent = true;
	L.startAt = CE_NetNowMs();
	L.st.notice[0] = 0;
	return 1;
}

void CE_LobbyLeave(void)
{
    ClearPendingHandoff();
    gRestart = RESTART_NONE;

#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    const bool signal_selected = gSignalSelected;
    SignalTerminal();
    if (L.state == CE_LOBBY_STARTING && !signal_selected) return;
#else
    if (L.state == CE_LOBBY_STARTING) return;
#endif
#else
	if (L.state == CE_LOBBY_STARTING)
		return;
#endif
	bool connected = L.net && (L.state == CE_LOBBY_READY);
	Close(connected);
    memset(&gContinuation, 0, sizeof gContinuation);
    gContinuationAttempt = 0;
    Forget();
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    if (signal_selected) {
        gSignalAuthority.reset(); gSignalDescriptor.Reset(); gSignalBootRequired = false;
        if (!gSignalProviderSelected || CE_NetSelectSignaling({})) gSignalProviderSelected = false;
        CE_NetSelectSignalingDirectory(nullptr);
    }
#endif
#endif
}

void CE_LobbyShutdown() {
    if (L.state == CE_LOBBY_STARTING) return;
    CE_LobbyLeave();
}

int CE_LobbyRestartDue(void)
{
	return gRestart == RESTART_LAUNCH;
}

int CE_LobbyCopy(const char *text)
{
	return SDL_SetClipboardText(text) ? 1 : 0;
}

int CE_LobbyPaste(char *out, size_t size)
{
	char *t = SDL_GetClipboardText();
	if (!t)
		return 0;
	snprintf(out, size, "%s", t);
	SDL_free(t);
	return *out != 0;
}

int CE_LobbyCtrlHeld(void)
{
	return (SDL_GetModState() & SDL_KMOD_CTRL) ? 1 : 0;
}

int CE_LobbyOutcomePending(void)
{
	return gOutcomeReady ? 1 : 0;
}

int CE_LobbyTakeOutcome(char *text, size_t size, int *good)
{
	if (!gOutcomeReady)
		return 0;
	gOutcomeReady = false;
	snprintf(text, size, "%s", gOutcome.text);
	*good = gOutcome.good;
	return 1;
}

void CE_LobbyShellExit(int code, int native_quit)
{
	gShellExited = true;
	gShellCode = code;
	gShellNativeQuit = native_quit;
}

}

//*********************************************************************************
// Arguments and the restart.
namespace {
// This process's own arguments, joined by spaces as the Linux entry point joins them for the game's parser.
bool OwnArgs(char *out, size_t n)
{
	out[0] = 0;
#ifdef _WIN32
	LPWSTR line = GetCommandLineW();
	if (!line)
		return false;
	// skip the program name (quoted or not), as the C runtime does
	LPWSTR p = line;
	if (*p == L'"')
	{
		p++;
		while (*p && *p != L'"')
			p++;
		if (*p)
			p++;
	}
	else
		while (*p && *p != L' ' && *p != L'\t')
			p++;
	while (*p == L' ' || *p == L'\t')
		p++;
	return WideCharToMultiByte(CP_UTF8, 0, p, -1, out, (int)n, NULL, NULL) > 0;
#else
	FILE *f = fopen("/proc/self/cmdline", "rb");
	if (!f)
		return false;
	char buf[4096];
	size_t got = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[got] = 0;
	size_t i = strlen(buf) + 1, k = 0;		// after argv[0]
	while (i < got)
	{
		size_t len = strlen(buf + i);
		if (k && k + 1 < n)
			out[k++] = ' ';
		if (k + len + 1 > n)
			return false;
		memcpy(out + k, buf + i, len);
		k += len;
		out[k] = 0;
		i += len + 1;
	}
	return true;
#endif
}

// args without -level N and -skipfmv (any case), then extra appended.
void Rebuild(const char *args, const char *extra, char *out, size_t n)
{
	char copy[1024];
	snprintf(copy, sizeof copy, "%s", args);
	out[0] = 0;
	size_t k = 0;
	char *save = NULL;
	bool skipNext = false;
	for (char *t = strtok_r(copy, " \t", &save); t; t = strtok_r(NULL, " \t", &save))
	{
		if (skipNext)
		{
			skipNext = false;
			continue;
		}
		if (!SDL_strcasecmp(t, "-level"))
		{
			skipNext = true;
			continue;
		}
		if (!SDL_strcasecmp(t, "-skipfmv"))
			continue;
		int w = snprintf(out + k, n - k, "%s%s", k ? " " : "", t);
		if (w < 0 || (size_t)w >= n - k)
			break;
		k += (size_t)w;
	}
	snprintf(out + k, n - k, "%s%s", k ? " " : "", extra);
}

// Variables the next process must not inherit: test input, one-shot test switches, any network endpoint or secret.
void ScrubEnvironment(const char *handoff)
{
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    SetEnv("BEA_NET_SIGNAL_AUTHORITY_FD", NULL);
#endif
#endif
	static const char *const names[] = {"BEA_PORT_FAKE_KEYS", "BEA_PORT_FAKE_CLICK", "BEA_PORT_SHOT", "BEA_PORT_QUIT",
		"BEA_NET_LOBBY_CODE_OUT", "BEA_NET_HOST", "BEA_NET_JOIN", "BEA_NET_SECRET", "BEA_NET_COMPAT",
		"BEA_LOCKSTEP_HOST", "BEA_LOCKSTEP_JOIN", "BEA_LOCKSTEP_SECRET", "BEA_LOCKSTEP_DELAY", "BEA_LOCKSTEP_HASH_EVERY",
		"BEA_LOCKSTEP_SKEW_AT", "BEA_LOCKSTEP_DUMP", "BEA_LOCKSTEP_QUIT_AT", "BEA_LOCKSTEP_STALL_EVERY", "BEA_LOCKSTEP_STALL_AT",
		"BEA_LOCKSTEP_STALL_MS", "BEA_LOCKSTEP_NO_LOSS", "BEA_LOCKSTEP_NO_PARTICLES", "BEA_LOCKSTEP_EXTRA_RENDERS",
		"BEA_LOCKSTEP_STALL_FROM", "BEA_LOCKSTEP_FLIP_CW_AT", "BEA_LOCKSTEP_NO_CW_FENCE", "BEA_LOCKSTEP_POISON_STACK",
		"BEA_LOCKSTEP_TEST_UNINIT_READ"};
	for (const char *n : names)
		SetEnv(n, NULL);
	SetEnv("BEA_NET_HANDOFF", handoff);

    SetEnv("BEA_NET_HANDOFF_FILE", gHandoffName[0] ? gHandoffName : NULL);
    SetEnv("BEA_NET_HANDOFF_OWNER", gHandoffReceipt[0] ? gHandoffReceipt : NULL);
}

// Starts this program again with args. Linux: replaces this process (same process id, so the launcher waiting for it and
// its log pipe carry on). Windows: starts the new process with the same standard handles, waits for it and exits with its
// code, for the same reason. Returns only on failure.
void Exec(const char *args)
{
	fflush(NULL);
#ifdef _WIN32
	WCHAR exe[MAX_PATH * 2], wargs[2048], line[4096];
	DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
	if (!len || len >= MAX_PATH * 2 || !MultiByteToWideChar(CP_UTF8, 0, args, -1, wargs, 2048))
		return;
	_snwprintf(line, 4096, L"\"%ls\" %ls", exe, wargs);
	line[4095] = 0;
	STARTUPINFOW si;
	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	PROCESS_INFORMATION pi;
	WCHAR dir[MAX_PATH * 2];
	DWORD dl = GetCurrentDirectoryW(MAX_PATH * 2, dir);
	if (!CreateProcessW(exe, line, NULL, NULL, TRUE, 0, NULL, (dl && dl < MAX_PATH * 2) ? dir : NULL, &si, &pi))
	{
		fprintf(stderr, LOBBY_LOG "could not restart the game (error %lu)\n", (unsigned long)GetLastError());
		return;
	}
	CloseHandle(pi.hThread);
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD rc = 1;
	GetExitCodeProcess(pi.hProcess, &rc);
	ExitProcess(rc);
#else
	char exe[1024];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (len <= 0)
		return;
	exe[len] = 0;
	char copy[2048];
	snprintf(copy, sizeof copy, "%s", args);
	char *argv[128];
	int argc = 0;
	argv[argc++] = exe;
	char *save = NULL;
	for (char *t = strtok_r(copy, " \t", &save); t && argc < 127; t = strtok_r(NULL, " \t", &save))
		argv[argc++] = t;
	argv[argc] = NULL;
	execv(exe, argv);
	fprintf(stderr, LOBBY_LOG "could not restart the game\n");
#endif
}

void ReturnToFrontEnd(bool good, const char *text)
{
	CE_Handoff h;
	memset(&h, 0, sizeof h);
	h.kind = CE_HANDOFF_OUTCOME;
	h.good = good ? 1 : 0;
	h.created = CE_HandoffNow();
	snprintf(h.text, sizeof h.text, "%s", text);
	for (char *c = h.text; *c; c++)
		if ((unsigned char)*c < 0x20 || *c == 0x7f)
			*c = ' ';
    AttachCampaignReturn(&h, good);
	if (!true || !WriteOwnedHandoff("outcome", &h))
		return;
	char args[1200];
	Rebuild(gReturnArgs, "-skipfmv", args, sizeof args);
	fprintf(stderr, LOBBY_LOG "returning to the front end (%s); the player is told: %s\n", good ? "the game ended" : "the game stopped", h.text);
	ScrubEnvironment("outcome");
	Exec(args);
    ClearPendingHandoff();
}

// A process started with BEA_NET_HANDOFF: takes the handoff before anything reads the environment.
struct Boot
{
	Boot()
	{
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
        SignalCaptureDescriptor();
#endif
#endif
        CaptureHandoffEnvironment();
		const char *kind = getenv("BEA_NET_HANDOFF");
		if (!kind || !*kind)
			return;
		char which[16];
		snprintf(which, sizeof which, "%s", kind);
		SetEnv("BEA_NET_HANDOFF", NULL);
		const char *why = NULL;
		CE_Handoff h;
		if (!strcmp(which, "outcome"))
		{
			if ((ReadOwnedHandoff("outcome", CE_HandoffNow(), &h, &why) ) &&
				h.kind == CE_HANDOFF_OUTCOME)
			{
				gOutcome = h;
            ResumeCampaignFromOutcome(h);
				gOutcomeReady = true;
			}
			else
				fprintf(stderr, LOBBY_LOG "the outcome of the network game was not read: %s\n", why ? why : "wrong kind");
			return;
		}
		if (strcmp(which, "launch"))
			return;
		gLaunched = true;
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
		bool ok = (gSignalBootRequired ? (gSignalAuthority && gSignalAuthority->CopyLaunch(&h)) : ReadOwnedHandoff("launch", CE_HandoffNow(), &h, &why)) &&
			(h.kind == CE_HANDOFF_LAUNCH && CE_BootCampaignAdmit(&h));
#else
		bool ok = ReadOwnedHandoff("launch", CE_HandoffNow(), &h, &why) &&
			(h.kind == CE_HANDOFF_LAUNCH && CE_BootCampaignAdmit(&h));
#endif
#else
		bool ok = ReadOwnedHandoff("launch", CE_HandoffNow(), &h, &why) &&
			(h.kind == CE_HANDOFF_LAUNCH && CE_BootCampaignAdmit(&h));
#endif
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
        if (ok) ok = SignalAdmitLaunch(h);
#endif
#endif
		if (ok)
		{
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
        if (!gSignalSelected) {
			char secret[65];
			Hex(h.secret, 32, secret);
			SetEnv(h.role == 1 ? "BEA_NET_HOST" : "BEA_NET_JOIN", h.endpoint);
			SetEnv("BEA_NET_SECRET", secret);
			CE_CodeWipe(secret, sizeof secret);
        } else {
			SetEnv(h.role == 1 ? "BEA_NET_HOST" : "BEA_NET_JOIN", h.endpoint);
        }
#else
			char secret[65];
			Hex(h.secret, 32, secret);
			SetEnv(h.role == 1 ? "BEA_NET_HOST" : "BEA_NET_JOIN", h.endpoint);
			SetEnv("BEA_NET_SECRET", secret);
			CE_CodeWipe(secret, sizeof secret);
#endif
#else
			char secret[65];
			Hex(h.secret, 32, secret);
			SetEnv(h.role == 1 ? "BEA_NET_HOST" : "BEA_NET_JOIN", h.endpoint);
			SetEnv("BEA_NET_SECRET", secret);
			CE_CodeWipe(secret, sizeof secret);
#endif
			snprintf(gReturnArgs, sizeof gReturnArgs, "%s", h.args);
			fprintf(stderr, LOBBY_LOG "starting the network game as the %s\n", h.role == 1 ? "host" : "joining player");
			CE_CodeWipe(&h, sizeof h);
			return;
		}
		// No admission without a valid handoff: back to the front end before the game starts.
		char text[256];
		snprintf(text, sizeof text, "The network game could not start: %s.", why ? why : "the start file was not a launch");
		fprintf(stderr, LOBBY_LOG "%s\n", text);
		char own[1024];
		if (OwnArgs(own, sizeof own))
			snprintf(gReturnArgs, sizeof gReturnArgs, "%s", own);
		ReturnToFrontEnd(false, text);
		_exit(1);
	}
};
Boot gBoot;

#ifdef _WIN32
void AtExit() { CE_LobbyAfterExit(); }
struct RegisterAtExit { RegisterAtExit() { atexit(AtExit); } } gRegisterAtExit;
#endif
}

extern "C" void CE_LobbyAfterExit(void)
{
	static bool once;
	if (once)
		return;
	once = true;
	if (gRestart == RESTART_LAUNCH)
	{
		char own[1024], args[1200];
		if (!OwnArgs(own, sizeof own))
			return;
		char level[40];
		snprintf(level, sizeof level, "-level %u -skipfmv", (unsigned)gStartLevel);
		Rebuild(own, level, args, sizeof args);
		ScrubEnvironment("launch");
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
        if (gSignalSelected) {
            int descriptor = gSignalDescriptor.PrepareExec();
            if (descriptor < 3) { SignalTerminal(); ClearPendingHandoff(); return; }
            char identifier[16]; snprintf(identifier, sizeof identifier, "%d", descriptor);
            SetEnv("BEA_NET_SIGNAL_AUTHORITY_FD", identifier);
        }
#endif
#endif
#ifdef _WIN32
		SDL_Quit();
#endif
		Exec(args);
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
        SetEnv("BEA_NET_SIGNAL_AUTHORITY_FD", NULL); SignalTerminal();
#endif
#endif
    ClearPendingHandoff();
		return;
	}
	if (!gLaunched)
		return;
#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
    SignalTerminal();
#endif
#endif
	if ((gShellExited && gShellNativeQuit) || gLevelToSystem || PortCeQuitRequested())
	{
		fprintf(stderr, LOBBY_LOG "the window was closed: not returning to the front end\n");
		return;
	}
	const char *error = CE_SessionError();
	char text[256];
	bool good = false;
	if (!gShellExited)
	{
		const char *boot = CE_BootError();
		snprintf(text, sizeof text, "The network game could not start: %s.", boot && *boot ? boot : "the game stopped early");
	}
	else if (error && *error)
		snprintf(text, sizeof text, "The network game stopped: %s.", error);
	else
	{
		good = gShellCode == 0;
		snprintf(text, sizeof text, good ? "The network game has ended." : "The network game ended with an error.");
	}
#ifdef _WIN32
	SDL_Quit();
#endif
	ReturnToFrontEnd(good, text);
}

// The game shell's last line (patch 0140, ltshell.cpp WinMain): records the exit, returns the code unchanged.
int PortLobbyShellExit(int code, int native_quit)
{
	CE_LobbyShellExit(code, native_quit);
	return code;
}

// The -level loop (patch 0142, System.cpp CSystem::Run): records whether the level ended by quitting to the system (the
// player closed the window, or chose to quit the game), which ends a network game's process instead of returning.
int PortLobbyLevelQuit(int quit, int to_system)
{
	gLevelToSystem = quit == to_system;
	return quit;
}


#ifndef CE_NETWORK_OFF
#if defined(CE_NETWORK_ICE)
bool CE_LobbySignalingSelect(std::shared_ptr<CeBootSignalAuthority> authority) noexcept
{
    if (L.net || L.state == CE_LOBBY_STARTING) return false;
    if (gSignalSelected) return !gSignalClosed && (authority == gSignalAuthority && gSignalProviderSelected);
    gSignalSelected = true; gSignalPreflight = false; gSignalClosed = false;
    gSignalProviderSelected = false; gSignalBootRequired = false;
    gSignalDescriptor.Reset(); gSignalAuthority = std::move(authority);
#if defined(CE_NETWORK_SIGNALING)
    if (!gSignalAuthority) return false;
    if (!CE_NetSelectSignalingDirectory(gSignalAuthority->Directory())) return false;
    // The configuration/startup owner initializes HTTPS before this call. Constructing the
    // provider makes no request; an unavailable factory cannot fall back to manual exchange.
    auto provider = CeCreateSignalingProvider(gSignalAuthority->Origin(), gSignalAuthority);
    if (!provider || !CE_NetSelectSignaling(std::move(provider))) return false;
    gSignalProviderSelected = true; return true;
#else
    gSignalClosed = true;
    if (gSignalAuthority) gSignalAuthority->Revoke();
    return false;
#endif
}
void CE_LobbySignalingClear() noexcept
{
    CE_LobbyLeave();
    if (gSignalProviderSelected || CE_NetSignalingDirectorySelected()) return;
    gSignalSelected = false; gSignalPreflight = false; gSignalClosed = false;
    gSignalAuthority.reset(); gSignalDescriptor.Reset(); gSignalBootRequired = false;
}
bool CE_LobbySignalingSelected() noexcept { return gSignalSelected; }
const char* CE_LobbySignalingOrigin() noexcept
{
    return gSignalProviderSelected && gSignalAuthority ? gSignalAuthority->Origin() : "";
}
CeIceSignalView CE_LobbySignalingView() noexcept
{
    CeIceSignalView out{};
    if (gSignalClosed) { out.status = CeIceSignalStatus::Closed; return out; }
    if (!gSignalSelected || !gSignalAuthority || !gSignalProviderSelected) return out;
    return CE_NetSignalingView();
}
bool CE_LobbySignalingBeginPreflight() noexcept
{
    if (!gSignalSelected || !gSignalAuthority || !gSignalProviderSelected || gSignalClosed || gSignalAuthority->Started()) return false;
    gSignalPreflight = true; CE_LobbySignalingPump(); return true;
}
void CE_LobbySignalingPump() noexcept
{
    if (gSignalPreflight && gSignalAuthority && gSignalProviderSelected && !gSignalClosed && !gSignalAuthority->Started())
        CE_NetSignalingPreflight(uint64_t(CE_HandoffNow()), CE_NetNowMs());
}
bool CE_LobbySignalingAccept() noexcept
{
    if (!gSignalPreflight || !gSignalAuthority || gSignalClosed || gSignalAuthority->Started() ||
        CE_LobbySignalingView().status != CeIceSignalStatus::NeedsConsent) return false;
    return CE_NetSignalingAccept();
}
bool CE_LobbySignalingRetry() noexcept
{
    if (!gSignalAuthority || gSignalClosed || gSignalAuthority->Started()) return false;
    CeIceSignalStatus status = CE_LobbySignalingView().status;
    if (status == CeIceSignalStatus::IssuanceUnknown || status == CeIceSignalStatus::Closed ||
        status == CeIceSignalStatus::Expired || status == CeIceSignalStatus::SetupExpired ||
        status == CeIceSignalStatus::GrantExpired) return false;
    return CE_LobbySignalingBeginPreflight();
}
extern "C" int CE_LobbySignalingGameSecret(uint32_t role, uint32_t level, uint8_t secret[32])
{
    if (!gSignalSelected && !gSignalBootRequired) return 0;
    if (secret) CE_CodeWipe(secret, 32);
    if (!secret || gSignalClosed || gSignalBootRequired || !gSignalAuthority || (role != 1 && role != 2)) return -1;
    const CE_NetRole selected = role == 1 ? CE_NET_HOST : CE_NET_GUEST;
    return gSignalAuthority->CopyGameSecret(selected, level, secret) ? 1 : -1;
}
extern "C" int CE_LobbySignalingBootPrepared(const uint8_t compatibility[32], uint32_t level)
{
    if (!gSignalSelected && !gSignalBootRequired) return 1;
#if defined(CE_NETWORK_SIGNALING)
    if (!gSignalBootRequired || !gSignalAuthority || gSignalClosed || level != gSignalBootLevel || !compatibility) {
        SignalTerminal(); return 0;
    }
    if (!CE_NetSelectSignalingDirectory(gSignalAuthority->Directory())) { SignalTerminal(); return 0; }
    memcpy(gSignalBootConfig.compatibility, compatibility, sizeof gSignalBootConfig.compatibility);
    CeIceSignalGamePlan plan{};
    bool bound = gSignalAuthority->BindBoot(gSignalBootConfig, level, uint64_t(CE_HandoffNow()), CE_NetNowMs(), &plan);
    CE_CodeWipe(&gSignalBootConfig, sizeof gSignalBootConfig);
    if (!bound) { SignalTerminal(); return 0; }
    auto provider = CeCreateSignalingProvider(gSignalAuthority->Origin(), gSignalAuthority);
    if (!provider || !CE_NetSelectSignaling(std::move(provider))) { SignalTerminal(); return 0; }
    gSignalProviderSelected = true;
    bool restored = CE_NetRestoreSignalingGamePlan(plan);
    CE_CodeWipe(&plan, sizeof plan);
    if (!restored) { SignalTerminal(); return 0; }
    // Rebind only the expected existing pair/current. The prior explicit consent is checked
    // by that service view; boot never silently calls Accept or refreshes the START window.
    CE_NetSignalingPreflight(uint64_t(CE_HandoffNow()), CE_NetNowMs());
    gSignalBootRequired = false;
    return 1;
#else
    (void)compatibility; (void)level;
    SignalTerminal();
    return 0;
#endif
}
#endif
#endif
int CE_LobbyStart(void) {
    return LobbyStart(false);
}

CE_LobbyCampaignStartResult CE_LobbyCampaignStart(uint32_t displayed_level) {
    char why[160];
    if (!CE_LobbyAvailable(why, sizeof(why)))
        return CE_LOBBY_CAMPAIGN_START_UNAVAILABLE;
    if (!L.host) return CE_LOBBY_CAMPAIGN_START_NOT_HOST;
    if (L.state != CE_LOBBY_READY || !L.net || !L.gotInfo || L.startSent)
        return CE_LOBBY_CAMPAIGN_START_NOT_READY;
    if (displayed_level != L.level)
        return CE_LOBBY_CAMPAIGN_START_STALE_SELECTION;
    CE_NetStatus status = {};
    CE_NetGetStatus(L.net, &status);
    if (status.phase != CE_NET_ACTIVE)
        return CE_LOBBY_CAMPAIGN_START_NOT_READY;
    return LobbyStart(true) ? CE_LOBBY_CAMPAIGN_START_QUEUED :
        CE_LOBBY_CAMPAIGN_START_FAILED;
}
