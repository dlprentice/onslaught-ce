// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_netlobby.h: the network game's lobby (port/game/ce_netpages.cpp draws it) and the restart into
// the game process and back.
//
// The lobby runs the existing authenticated transport (port/network) with its own secret, derived from the join code,
// and a compatibility digest of the build's network identity and version, so a different build is refused with both
// version labels before any lobby message. The lobby itself sends four CONTROL messages: INFO (version, map), START
// (level, a fresh start number), the guest's acknowledgement (ACK) and the host's GO. The host chooses the map among the co-op levels; the guest shows the host's choice.
//
// Start: the host sends START, the guest acknowledges, and only when that acknowledgement has reached the host does the host write its handoff, send GO and leave; the guest starts on GO and
// never on silence, a refused or lost acknowledgement, or the host's leave without GO (each side's wait has a deadline, after which the lobby is as it was and Start can be tried again).
// A reliable transport still cannot make the last message certain: if GO is lost after the host has started, the host's game waits for a guest who does not come, and says so when its wait ends.
// Both processes write the private handoff file <user folder>/net-launch (ce_netcode.h) and quit; the process then
// starts itself again with its own arguments and -level N -skipfmv (N: the chosen map), and the fresh process feeds the handoff to the
// existing fresh-process network admission (CE_BootStart, which must run before the memory manager exists). After the
// match (unless the player closed the window) the game process starts the front end again the same way, with
// <user folder>/net-outcome saying what happened; the front end opens the Network game page with it.
#ifndef CE_NETLOBBY_H
#define CE_NETLOBBY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CE_LOBBY_DEFAULT_PORT	27652
#define CE_LOBBY_DEFAULT_LEVEL	856		// the map a new lobby starts on; the co-op levels are 856, 858 and 100 (compatibility.h)
#define CE_LOBBY_HELP_LABEL	"How to connect over the Internet"		// the row of the Network game page that opens the help page, and what the notices call it

enum CE_LobbyState {
	CE_LOBBY_OFF = 0,
	CE_LOBBY_HOST_WAITING,		// host: listening, no one connected
	CE_LOBBY_HOST_CONNECTING,	// host: a player is connecting
	CE_LOBBY_JOIN_CONNECTING,	// guest: connecting
	CE_LOBBY_READY,				// both connected; versions exchanged
	CE_LOBBY_REFUSED,			// guest: a different build; both labels known; Leave only
	CE_LOBBY_FAILED,			// guest: could not join (text says why); back to the Join page
	CE_LOBBY_LEFT,				// guest: the host left (text); back to the Join page
	CE_LOBBY_STARTING			// Start agreed: the handoff is written and the restart is pending
};

typedef struct CE_LobbyStatus {
	int state;
	int host;					// 1 on the host
	char code[32];				// host: the join code, shown with dashes
	char local_version[40], peer_version[40];
	char compat[128];			// "Compatible" or why not
	char notice[320];			// a note for the page (an attempt failed, a new code, the other player left, nobody has answered): a few lines, wrapped by the page
	char endpoint[32];			// guest: where it connects
	uint32_t level;				// the map: the host's choice (the guest shows it)
	int failed;					// host: failed admissions with this code
	uint16_t port;
} CE_LobbyStatus;

// 1 when network games can be offered, else 0 with why (no network identity in this build, no launcher roots).
int CE_LobbyAvailable(char *why, size_t size);
// This PC's IPv4 addresses (loopback last, only when there is no other), as text. Returns the count.
// Keep the chooser and validation bounded to the same local address list.
enum { CE_LOBBY_ADDRESS_CHOICES = 32 };
int CE_LobbyAddresses(char (*out)[16], int max);
uint16_t CE_LobbyPort(void);

int CE_LobbyHost(void);
// NULL keeps the automatic choice; a supplied address must still be local.
int CE_LobbyHostAt(const char *selected_address);			// 1 when listening; else status notice says why
// code: as typed; address: empty, "a.b.c.d" or "a.b.c.d:port" (overrides the code's). Returns 1, or 0 with *problem.
int CE_LobbyJoin(const char *code, const char *address, const char **problem);
void CE_LobbyUpdate(void);		// once a frame while a network page is open
void CE_LobbyStatusGet(CE_LobbyStatus *out);
int CE_LobbyNextLevel(void);	// host: the next co-op map (856, 858, 100); the guest is told. Returns 0 when the map can no longer change
int CE_LobbyStart(void);		// host, READY: sends START; the state becomes STARTING when the guest acknowledges
void CE_LobbyLeave(void);		// closes the lobby (a graceful leave when connected)
// Automatic menu teardown preserves a queued launch; explicit Leave cancels it.
void CE_LobbyShutdown();
int CE_LobbyRestartDue(void);	// 1 once STARTING has written the handoff: the page shows "Starting..." and quits

// Clipboard and keyboard helpers for the pages (SDL).
int CE_LobbyCopy(const char *text);
int CE_LobbyPaste(char *out, size_t size);
int CE_LobbyCtrlHeld(void);

// The outcome the front end was started with (once): returns 1 and fills text, good.
int CE_LobbyTakeOutcome(char *text, size_t size, int *good);
int CE_LobbyOutcomePending(void);
// Called by the game shell as WinMain returns (patch 0140): the exit code and whether the player closed the window.
void CE_LobbyShellExit(int code, int native_quit);
// After the game has shut down (Linux: main, after SDL_Quit; Windows: at exit): starts the process again when due.
// Returns only when nothing is due or the restart failed.
void CE_LobbyAfterExit(void);

typedef enum CE_LobbyCampaignStartResult {
    CE_LOBBY_CAMPAIGN_START_QUEUED,
    CE_LOBBY_CAMPAIGN_START_UNAVAILABLE,
    CE_LOBBY_CAMPAIGN_START_NOT_HOST,
    CE_LOBBY_CAMPAIGN_START_NOT_READY,
    CE_LOBBY_CAMPAIGN_START_STALE_SELECTION,
    CE_LOBBY_CAMPAIGN_START_FAILED
} CE_LobbyCampaignStartResult;

/* Intent only. A queued request still waits for CE_LOBBY_STARTING.
 * The argument is the mission from the last displayed lobby status. */
CE_LobbyCampaignStartResult CE_LobbyCampaignStart(uint32_t displayed_level);

#ifdef __cplusplus
}
#endif
#ifndef CE_NETWORK_OFF
#if defined(__cplusplus) && defined(CE_NETWORK_ICE)
#include "../../game/ce_network_stl.h"
#include "../../network/ice_signaling.h"
#include <memory>
class CeBootSignalAuthority;
// Selection constructs no request. The frontend explicitly discloses network use before
// beginning preflight, then separately asks for pair consent with Accept.
bool CE_LobbySignalingSelect(std::shared_ptr<CeBootSignalAuthority>) noexcept;
void CE_LobbySignalingClear() noexcept;
bool CE_LobbySignalingSelected() noexcept;
const char* CE_LobbySignalingOrigin() noexcept;
bool CE_LobbySignalingBeginPreflight() noexcept;
void CE_LobbySignalingPump() noexcept;
CeIceSignalView CE_LobbySignalingView() noexcept;
bool CE_LobbySignalingAccept() noexcept;
bool CE_LobbySignalingRetry() noexcept;
#endif
#endif
#endif
