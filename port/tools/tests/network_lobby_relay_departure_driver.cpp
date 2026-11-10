// SPDX-License-Identifier: GPL-3.0-only
// Whole production lobby with authored transport boundaries; no game or sockets.
#define CE_NETWORK_ICE 1
#define main ce_lobby_reference_main
#include RELAY_DEPARTURE_HARNESS_INC
#undef main
#include "../../network/ice_exchange.h"
#include <unistd.h>

// Everything the lobby writes to stderr while body runs.
template <class F> static std::string captured(F body) {
    fflush(stderr);
    FILE *file = tmpfile();
    const int saved = dup(2);
    if (!file || saved < 0 || dup2(fileno(file), 2) < 0) return "capture failed";
    body();
    fflush(stderr);
    dup2(saved, 2); close(saved);
    std::string text; char block[256]; size_t count;
    rewind(file);
    while ((count = fread(block, 1, sizeof block, file)) > 0) text.append(block, count);
    fclose(file);
    return text;
}
static const std::string failure_line = "onslaught-ce: network lobby: relay lobby failed: ";
static const std::string state_line = "onslaught-ce: network lobby: state ";

static unsigned factory_calls;
static const char *backend_problem = "";
static const char *creation_problem = "";
extern "C" CE_NetSession *CE_NetCreateLobby(const CE_NetConfig *config) {
    ++factory_calls;
    if (CE_NetGameIceRequested()) {
        // Model only the factory's same-context refusal; observe whether the
        // actual lobby attempts an unintended recreation. No files are read.
        creation_problem = "private relay exchange is unavailable; use a fresh session directory";
        return nullptr;
    }
    return CE_NetCreate(config);
}
extern "C" CE_NetSession *CE_NetCreateGame(const CE_NetConfig *config) {
    return CE_NetCreateLobby(config);
}
extern "C" const char *CE_NetGameProblem(const CE_NetSession *session) {
    return session ? backend_problem : creation_problem;
}
extern "C" int CE_BootCampaignAdmit(const CE_Handoff *) { return 0; }
// The lobby's signaling hooks (CE_NETWORK_ICE) reach the real boot authority (linked beside the driver) and the
// selected-directory calls; this check selects no signaling, so these never act.
extern "C" int CE_NetSignalingDirectorySelected(void) { return 0; }
bool CE_NetSelectSignalingDirectory(const char *) noexcept { return true; }
bool CeIceExchange::ContextForConfig(const CE_NetConfig *, uint8_t context[32]) noexcept { memset(context, 1, 32); return true; }

static const char *recovery =
    "Ask the operator to prepare fresh private lobby and game folders. "
    "Relaunch both games and repeat the private code and file exchange.";

static void prepare(bool relay, bool connected, int phase, uint32_t error) {
    if (relay) setenv("BEA_NET_ICE_DIRECTORY", "authored-unused-directory", 1);
    else unsetenv("BEA_NET_ICE_DIRECTORY");
    unsetenv("BEA_NET_ICE_GAME_DIRECTORY");
    L = Lobby(); N = Net();
    L.host = true; L.net = &gSession; L.connected = connected;
    L.state = connected ? CE_LOBBY_READY : CE_LOBBY_HOST_CONNECTING;
    L.sentInfo = L.gotInfo = connected;
    L.st.failed = 4; L.key = 123;
    L.ip = 0x7f000001; L.port = CE_LOBBY_DEFAULT_PORT;
    memset(L.secret, 7, sizeof L.secret);
    Text(L.st.code, sizeof L.st.code, "authored-code");
    N.exists = true; N.role = CE_NET_HOST; N.phase = phase; N.error = error;
    Text(N.peerVersion, sizeof N.peerVersion, kVersion);
    factory_calls = 0; backend_problem = creation_problem = "";
}

static void terminal(const char *cause, const char *label, const char *logged = nullptr) {
    const std::string told = captured([] { CE_LobbyUpdate(); });
    const auto page = Page();
    const std::string expected = std::string(cause) + " " + recovery;
    check(page.state == CE_LOBBY_FAILED && !L.net && !N.exists && factory_calls == 0,
          "relay host retires without recreating its one-use exchange");
    check(page.host && expected == page.notice && !page.peer_version[0], label);
    bool kept = L.key == 123 && !strcmp(page.code, "authored-code");
    for (const auto byte : L.secret) kept = kept && byte == 7;
    check(kept && L.st.failed == 4, "relay departure does not rotate identity or consume another admission");
    // The log line carries the lobby's own fixed words (never the other build's label), then the new state.
    check(told == failure_line + (logged ? logged : cause) + "\n" + state_line + "failed\n",
          "relay host failure prints the lobby's own sentence, then the new state");
    const std::string later = captured([] { CE_LobbyUpdate(); });
    check(Page().state == CE_LOBBY_FAILED && factory_calls == 0 && expected == Page().notice,
          "later lobby polls do not revive the ended relay attempt");
    check(later.empty(), "later lobby polls print nothing");
}

static const char *guest_recovery =
    "Relaunch both games with fresh private lobby and game folders; repeat the code and file exchange.";

static void prepare_guest(bool relay, bool ready, int phase, uint32_t error) {
    prepare(relay, ready, phase, error);
    L.host = false;
    L.state = ready ? CE_LOBBY_READY : CE_LOBBY_JOIN_CONNECTING;
    N.role = CE_NET_GUEST;
}

static void guest_terminal(int state, const char *cause, const char *label) {
    const std::string told = captured([] { CE_LobbyUpdate(); });
    const auto page = Page();
    const std::string expected = std::string(cause) + " " + guest_recovery;
    check(page.state == state && !page.host && expected == page.notice, label);
    check(!L.net && !N.exists && factory_calls == 0,
          "guest relay failure retires without recreating its exchange");
    check(told == state_line + (state == CE_LOBBY_LEFT ? "left" : "failed") + "\n" + failure_line + cause + "\n",
          "relay guest failure prints the new state, then the lobby's own sentence");
    const std::string later = captured([] { CE_LobbyUpdate(); });
    check(Page().state == state && factory_calls == 0 && expected == Page().notice,
          "later guest polls retain the terminal relay cause and recovery");
    check(later.empty(), "later guest polls print nothing");
}

// The lobby's state trace is read by people (the harness does not parse it): each state prints its own name once, when
// the lobby enters it, and setting the state to the one the lobby is already in prints nothing.
static void state_trace() {
    L = Lobby();
    static const struct { int state; const char *name; } states[] = {
        {CE_LOBBY_HOST_WAITING, "host_waiting"}, {CE_LOBBY_HOST_CONNECTING, "host_connecting"},
        {CE_LOBBY_JOIN_CONNECTING, "join_connecting"}, {CE_LOBBY_READY, "ready"}, {CE_LOBBY_REFUSED, "refused"},
        {CE_LOBBY_FAILED, "failed"}, {CE_LOBBY_LEFT, "left"}, {CE_LOBBY_STARTING, "starting"}, {CE_LOBBY_OFF, "off"}};
    bool named = true;
    for (const auto &entry : states) {
        const std::string told = captured([&] { L.state = entry.state; });
        named = named && told == state_line + entry.name + "\n" && L.state == entry.state;
    }
    check(named, "every lobby state prints its own name when the lobby enters it");
    const std::string again = captured([] { L.state = CE_LOBBY_OFF; L.state = CE_LOBBY_OFF; });
    check(again.empty() && L.state == CE_LOBBY_OFF, "setting the state to the one the lobby is in prints nothing");
}

// A lobby that is not on the relay route never prints the relay failure line.
static void direct_update() {
    const std::string told = captured([] { CE_LobbyUpdate(); });
    check(told.find("relay lobby failed") == std::string::npos, "a direct lobby never prints the relay failure line");
}

static void guest_cases() {
    prepare_guest(true, true, CE_NET_CLOSED, CE_NET_PEER_LEFT);
    guest_terminal(CE_LOBBY_LEFT, "The host left the game.",
                   "authenticated relay host departure keeps the guest leave cause");
    prepare_guest(true, false, CE_NET_CLOSED, CE_NET_PEER_LEFT);
    guest_terminal(CE_LOBBY_LEFT, "The host closed the game.",
                   "authenticated relay close before ready keeps the guest cause");
    prepare_guest(true, true, CE_NET_FAILED, CE_NET_PEER_LEFT);
    guest_terminal(CE_LOBBY_FAILED, "The relay connection ended.",
                   "unproven relay host disconnect is not a voluntary guest leave");
    prepare_guest(true, true, CE_NET_FAILED, CE_NET_TIMEOUT);
    guest_terminal(CE_LOBBY_FAILED, "The relay connection ended.",
                   "ready relay guest timeout is terminal without inventing departure");
    prepare_guest(true, false, CE_NET_FAILED, CE_NET_AUTH);
    guest_terminal(CE_LOBBY_FAILED, "The relay connection attempt ended.",
                   "guest relay admission failure gives fresh setup advice");
    prepare_guest(true, false, CE_NET_FAILED, CE_NET_PEER_LEFT);
    guest_terminal(CE_LOBBY_FAILED, "The relay connection attempt ended.",
                   "unadmitted relay guest disconnect is a failed attempt");

    for (bool ready : {false, true}) {
        prepare_guest(true, ready, CE_NET_FAILED, CE_NET_TIMEOUT);
        backend_problem = "authored relay grant rejected; begin a fresh setup";
        const std::string told = captured([] { CE_LobbyUpdate(); });
        check(Page().state == CE_LOBBY_FAILED && !strcmp(Page().notice, backend_problem) &&
              !L.net && !N.exists && factory_calls == 0,
              "guest backend diagnostic takes precedence over generic relay recovery");
        check(told == state_line + "failed\n" + failure_line + backend_problem + "\n",
              "backend failure on the relay guest prints the state, then its sentence");
    }

    prepare_guest(true, false, CE_NET_FAILED, CE_NET_INCOMPATIBLE);
    Text(N.peerVersion, sizeof N.peerVersion, "9.9.9");
    direct_update();
    char expected_compat[sizeof L.st.compat];
    snprintf(expected_compat, sizeof expected_compat,
             "Different Onslaught CE version: yours %s, theirs 9.9.9", kVersion);
    check(Page().state == CE_LOBBY_REFUSED && !strcmp(Page().peer_version, "9.9.9") &&
          !strcmp(Page().compat, expected_compat) && !strcmp(Page().notice, guest_recovery) &&
          !L.net && factory_calls == 0,
          "relay guest version refusal preserves labels and fresh setup advice");
    prepare_guest(true, false, CE_NET_FAILED, CE_NET_INCOMPATIBLE);
    direct_update();
    snprintf(expected_compat, sizeof expected_compat,
             "Different Onslaught CE build (both say %s): both need the same download", kVersion);
    check(Page().state == CE_LOBBY_REFUSED && !strcmp(Page().peer_version, kVersion) &&
          !strcmp(Page().compat, expected_compat) && !strcmp(Page().notice, guest_recovery) &&
          !L.net && factory_calls == 0,
          "relay guest build refusal preserves labels and fresh setup advice");

    prepare_guest(false, false, CE_NET_FAILED, CE_NET_AUTH);
    direct_update();
    check(Page().state == CE_LOBBY_FAILED && strstr(Page().notice, "No game answered at ") &&
          strstr(Page().notice, "router or firewall") && !strstr(Page().notice, "private lobby") &&
          !L.net && factory_calls == 0, "direct guest admission retains its address guidance");
    prepare_guest(false, true, CE_NET_FAILED, CE_NET_TIMEOUT);
    direct_update();
    check(Page().state == CE_LOBBY_FAILED &&
          !strcmp(Page().notice, "The connection to the host was lost. Check that the host is still waiting, then join again.") &&
          !L.net && !N.exists && N.destroyCalls == 1 && N.closeCalls == 0 && factory_calls == 0,
          "direct ready guest timeout reports lost connection and retires its transport");
    prepare_guest(false, false, CE_NET_CLOSED, CE_NET_PEER_LEFT);
    direct_update();
    check(Page().state == CE_LOBBY_LEFT && !strcmp(Page().notice, "The host closed the game."),
          "direct guest closed connection behavior is unchanged");
    prepare_guest(false, false, CE_NET_FAILED, CE_NET_INCOMPATIBLE);
    Text(N.peerVersion, sizeof N.peerVersion, "9.9.9");
    direct_update();
    check(Page().state == CE_LOBBY_REFUSED && !strcmp(Page().peer_version, "9.9.9") &&
          strstr(Page().compat, "theirs 9.9.9") && !Page().notice[0],
          "direct guest version refusal gains no relay instructions");
}

int main() {
    state_trace();
    prepare(true, true, CE_NET_CLOSED, CE_NET_PEER_LEFT);
    terminal("The other player left.", "authenticated relay departure retains its cause and fresh-attempt action");
    prepare(true, true, CE_NET_FAILED, CE_NET_PEER_LEFT);
    terminal("The relay connection ended.", "unproven relay disconnect is not called voluntary departure");
    prepare(true, true, CE_NET_FAILED, CE_NET_TIMEOUT);
    terminal("The relay connection ended.", "relay silence does not imply the other player left");
    prepare(true, false, CE_NET_FAILED, CE_NET_AUTH);
    terminal("The relay connection attempt ended.", "unadmitted relay failure has a distinct cause");
    prepare(true, false, CE_NET_CLOSED, CE_NET_PEER_LEFT);
    terminal("The other player left.", "authenticated departure does not depend on an earlier UI poll");
    prepare(true, false, CE_NET_FAILED, CE_NET_INCOMPATIBLE);
    Text(N.peerVersion, sizeof N.peerVersion, "9.9.9");
    terminal("A player using Onslaught CE 9.9.9 tried to join. Both players need the same version.",
             "relay version refusal survives without recreating the exchange",
             "A player using another Onslaught CE version tried to join.");
    prepare(true, false, CE_NET_FAILED, CE_NET_INCOMPATIBLE);
    terminal("The joining player has a different Onslaught CE build.",
             "relay build refusal survives without recreating the exchange");

    prepare(true, true, CE_NET_FAILED, CE_NET_TIMEOUT);
    backend_problem = "authored relay credentials expired; prepare a fresh attempt";
    const std::string told = captured([] { CE_LobbyUpdate(); });
    check(Page().state == CE_LOBBY_FAILED && !strcmp(Page().notice, backend_problem) &&
          factory_calls == 0 && !L.net, "specific backend failure takes precedence over generic departure");
    check(told == state_line + "failed\n" + failure_line + backend_problem + "\n",
          "backend failure on the relay host prints the state, then its sentence");

    prepare(false, true, CE_NET_CLOSED, CE_NET_PEER_LEFT);
    direct_update();
    check(Page().state == CE_LOBBY_HOST_WAITING && L.net && N.exists && factory_calls == 1 &&
          !strcmp(Page().notice, "The other player left.") && !strcmp(Page().code, "authored-code") &&
          L.key == 123, "direct host still listens again with the same code after departure");
    prepare(false, false, CE_NET_FAILED, CE_NET_AUTH);
    direct_update();
    check(Page().state == CE_LOBBY_HOST_WAITING && L.net && factory_calls == 1 &&
          !strcmp(Page().notice, "Five attempts failed, so this game has a new code. Give the other player the new code.") &&
          L.key != 123, "direct host retains its bounded failed-admission retry and code rotation");
    guest_cases();
    std::printf("RELAY_DEPARTURE_COMPLETE failures=%d\n", gFailures);
    return gFailures ? 1 : 0;
}
