// SPDX-License-Identifier: GPL-3.0-only
// Fake local interfaces and a fake transport; the real hosting function is
// inserted by the runner. No project headers, SDL, sockets or game are used.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

enum { CE_LOBBY_ADDRESS_CHOICES = 32, CE_LOBBY_HOST_WAITING = 1 };
static const char kVersion[] = "test";
#define LOBBY_LOG ""
struct FakeStatus {
    char code[64], local_version[64], notice[256];
    int host, port;
};
struct FakeLobby {
    FakeStatus st;
    bool host, nobodyTold;
    uint32_t ip;
    unsigned port;
    int state;
    uint64_t waitingAt;
    char bind[16];
} L;

static bool active;
static int forgets;
static int creates;
static int interfaces = 2;
static bool create_succeeds = true;
static const char *backend_problem = "";
static int problem_reads;

static void Forget() { ++forgets; active = false; }
static int CE_LobbyAvailable(char *, size_t) { return 1; }
static int CE_LobbyAddresses(char out[][16], int capacity) {
    const char *values[] = {"10.101.0.2", "10.102.0.2"};
    const int count = interfaces < capacity ? interfaces : capacity;
    for (int i = 0; i < count; ++i) std::snprintf(out[i], 16, "%s", values[i]);
    return count;
}
static bool ParseIp(const char *text, uint32_t *out) {
    if (!std::strcmp(text, "10.101.0.2")) { *out = 1; return true; }
    if (!std::strcmp(text, "10.102.0.2")) { *out = 2; return true; }
    return false;
}
static void Text(char *out, size_t size, const char *fmt,
                 const char *a = "", const char *b = "") {
    std::snprintf(out, size, fmt, a, b);
}
static unsigned CE_LobbyPort() { return 1; }
static uint64_t CE_NetNowMs() { return 1; }
static bool NewCode() { std::snprintf(L.st.code, sizeof L.st.code, "test invite"); return true; }
static bool Create() { ++creates; active = create_succeeds; return create_succeeds; }
static const char *CE_NetGameProblem(void *) { ++problem_reads; return backend_problem; }
static void Dotted(uint32_t, char *out, size_t size) { std::snprintf(out, size, "test"); }
static void CE_CodeWipe(void *out, size_t size) { std::memset(out, 0, size); }

/* INSERT_HOST_FUNCTIONS */

static int failed_if(bool condition, int code) { return condition ? code : 0; }
int main() {
    unsetenv("BEA_NET_LOBBY_BIND");
    active = true;
    L.ip = 123;
    L.state = CE_LOBBY_HOST_WAITING;
    std::snprintf(L.st.code, sizeof L.st.code, "existing invite");

    if (int r = failed_if(CE_LobbyHostAt("not an address") != 0, 1)) return r;
    if (int r = failed_if(!active || forgets || creates || L.ip != 123, 2)) return r;
    if (int r = failed_if(std::strcmp(L.st.code, "existing invite") != 0, 3)) return r;
    if (int r = failed_if(std::strstr(L.st.notice, "address") == NULL, 4)) return r;
    if (int r = failed_if(CE_LobbyHostAt("") != 0 || !active || forgets, 5)) return r;

    interfaces = 1;
    if (int r = failed_if(CE_LobbyHostAt("10.102.0.2") != 0 || !active || forgets, 6)) return r;
    interfaces = 0;
    if (int r = failed_if(CE_LobbyHostAt("10.101.0.2") != 0 || !active || forgets, 7)) return r;

    interfaces = 2;
    if (int r = failed_if(CE_LobbyHostAt("10.102.0.2") == 0, 8)) return r;
    if (int r = failed_if(!active || forgets != 1 || creates != 1 || L.ip != 2, 9)) return r;
    if (int r = failed_if(CE_LobbyHost() == 0, 10)) return r;
    if (int r = failed_if(!active || forgets != 2 || creates != 2 || L.ip != 1, 11)) return r;

    create_succeeds = false;
    backend_problem = "Authored backend could not listen.";
    if (int r = failed_if(CE_LobbyHostAt("10.101.0.2") != 0 || active, 12)) return r;
    if (int r = failed_if(problem_reads != 1 || std::strcmp(L.st.notice, backend_problem) != 0, 13)) return r;
    if (int r = failed_if(L.st.code[0] != 0, 14)) return r;
    backend_problem = "";
    if (int r = failed_if(CE_LobbyHost() != 0 || active || problem_reads != 2, 15)) return r;
    if (int r = failed_if(std::strstr(L.st.notice, "could not listen on UDP port") == NULL || L.st.code[0], 16)) return r;
    return 0;
}
