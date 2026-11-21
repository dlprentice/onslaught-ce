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
static void SplitIds(const char *list, std::vector<std::string> *ids) {
    for (const char *p = list; *p;) {
        const char *comma = std::strchr(p, ',');
        std::string id(p, comma ? static_cast<size_t>(comma - p) : std::strlen(p));
        while (!id.empty() && (id.back() == ' ' || id.back() == '\t')) id.pop_back();
        while (!id.empty() && (id[0] == ' ' || id[0] == '\t')) id.erase(0, 1);
        if (!id.empty()) ids->push_back(id);
        p = comma ? comma + 1 : p + std::strlen(p);
    }
}

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
