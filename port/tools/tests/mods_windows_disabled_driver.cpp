// The runner inserts the actual loader prefix and its platform guard.  The
// settings and log are local fakes; this program never opens a mod or game file.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(TEST_AS_WINDOWS)
#define _WIN32 1
#endif

static int configured;
static int entered_loader;
static int notices;
static const char *gFence;
static int CE_SettingGetText(const char *, char *out, size_t size) {
    if (!configured) return 0;
    std::snprintf(out, size, "%s", "example");
    return 1;
}
static const char *Fence() { return NULL; }
static void Log(const char *, ...) { ++notices; }
static bool gCheck;
static const std::vector<std::string> *gCheckIds;

/* INSERT_PLATFORM_OFF */

static void TestLoad() {
/* INSERT_LOAD_PREFIX */
    ++entered_loader;
}

int main() {
    configured = 0;
    TestLoad();
    if (entered_loader || notices) return 1;

    configured = 1;
    TestLoad();
#if (defined(TEST_AS_WINDOWS) && !defined(PORT_MODS_WINDOWS_ON)) || defined(PORT_MODS_PLATFORM_OFF)
    if (entered_loader || !notices) return 2;
#else
    if (entered_loader != 1) return 3;
#endif
    return 0;
}
