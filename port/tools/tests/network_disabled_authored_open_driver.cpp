// SPDX-License-Identifier: GPL-3.0-only
// Positive control for the actual network-disabled effective reader, with authored inputs only.
#ifndef CE_NETWORK_OFF
#error This fixture must exercise the network-disabled reader.
#endif
#include "handles.h"
#include "../../network/tests/fixture_guard.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
extern "C" FILE *port_fopen(const char *, const char *);

int main() {
    if (!FixtureGuard()) return 2;
    PortPathsInit();
    FILE *file = port_fopen("data/dialogue/fixture.ogg", "rb");
    if (!file || std::fclose(file)) return 1;
    const std::string path = std::string(std::getenv("BEA_GAME_DIR")) + "/data/dialogue/fixture.ogg";
    if (unlink(path.c_str())) return 1;
    errno = 0;
    if (port_fopen("data/dialogue/fixture.ogg", "rb") || errno != ENOENT) return 1;
    errno = 0;
    if (port_fopen("data/dialogue/never-present.ogg", "rb") || errno != ENOENT) return 1;
    std::puts("PASS actual network-disabled reader retains ordinary failed-open behavior");
    return 0;
}
