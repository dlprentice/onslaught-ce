// SPDX-License-Identifier: GPL-3.0-only
// Execute the actual patched bind handler with an authored console and command sink; no game or device.
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

enum { KEY_EVENT_DOWN = 0, KEY_EVENT_UP = 1, CONSOLE_KEYS = 256 };
static std::string dispatched;
static int calls;
struct CConsole {
    char earlier[0x23bc]{};
    char mBinds[CONSOLE_KEYS][2][0x40]{};
    void HandleBind(int key, int event);
    void ExecuteCommand(char *s) { ++calls; dispatched = s; }
};
static_assert(offsetof(CConsole, mBinds) == 0x23bc, "match the named retail member offset");
#ifdef CE_VERSION
bool PortRecording() { const char *v = std::getenv("FAKE_RECORDING"); return v && *v; }
#endif
#include "ce_layer.inc"
#include "handle_bind.inc"

static void check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}
int main(int argc, char **argv) {
    check(argc == 2, "expected corrected or faithful mode");
    bool corrected = std::strcmp(argv[1], "corrected") == 0;
#ifdef CE_VERSION
    check(PortCeConsoleBindKeys() == corrected, "CE admission matches fixture environment");
#else
    check(!corrected, "private build uses faithful handler");
#endif
    CConsole console;
    // Signed byte 0xc0 is -64: the legacy offset is 0x23bc - 64 * 128 = 0x3bc.
    std::strcpy(console.earlier + 0x3bc, "prior-text");
    std::strcpy(console.mBinds[0xc0][0], "grave-down");
    std::strcpy(console.mBinds[0xc0][1], "grave-up");
    std::strcpy(console.mBinds[0x46][0], "ordinary-F");
    console.HandleBind(0x46, KEY_EVENT_DOWN);
    check(dispatched == "ordinary-F", "ordinary key binding is unchanged");
    console.HandleBind(0xc0, KEY_EVENT_DOWN);
    check(dispatched == (corrected ? "grave-down" : "prior-text"), "grave selects the appropriate slot");
    if (corrected) {
        console.HandleBind(0xc0, KEY_EVENT_UP);
        check(dispatched == "grave-up", "release uses the release slot");
        std::strcpy(console.mBinds[0x90][0], "num-lock");
        std::strcpy(console.mBinds[0x91][0], "scroll-lock");
        console.HandleBind(0x90, KEY_EVENT_DOWN);
        check(dispatched == "num-lock", "NumLock stays inside the table");
        console.HandleBind(0x91, KEY_EVENT_DOWN);
        check(dispatched == "scroll-lock", "ScrollLock stays inside the table");
        int before = calls;
        for (int key : {-1, 256, INT_MAX}) console.HandleBind(key, KEY_EVENT_DOWN);
        check(calls == before, "out-of-range input cannot dispatch");
    }
    std::puts(corrected ? "corrected high keys and bounds passed" : "faithful signed-index behavior retained");
}
