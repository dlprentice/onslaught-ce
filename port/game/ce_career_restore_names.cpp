#if defined(CE_VERSION) || !defined(PORT_H)	// a CE layer file: a private game build (port.h forced in, no CE_VERSION) has none of it, as it has none of its callers; host test drivers compile it as it is
// The native filename predicate is evaluated only on the game thread.
#include "common.h"
#include "frontend.h"
#include "CLIParams.h"
#include "../platform/common/save_names.h"
#include <cstring>

extern bool gPortCheatNameOnly;
extern char gAllCheatsEnabled;
extern bool PortCeProjectCareerAllowed() noexcept;

namespace {
// Keep this domain equal to the native name-key table; the focused test checks it.
constexpr unsigned kNativeNameChecks = 6;
struct NameScope {
    CFEPSaveGame &page;
    WCHAR saved[sizeof(page.mSaveGameName) / sizeof(WCHAR)];
    bool namesOnly;
    explicit NameScope(CFEPSaveGame &p) : page(p), namesOnly(gPortCheatNameOnly) {
        std::memcpy(saved, page.mSaveGameName, sizeof(saved));
        gPortCheatNameOnly = true;
    }
    ~NameScope() {
        std::memcpy(page.mSaveGameName, saved, sizeof(saved));
        gPortCheatNameOnly = namesOnly;
    }
};
}

bool PortCeRestoreNamesCompatible(const char *sourceName, const char *targetName) {
    // These original bypasses conceal filename effects, so do not approve a copy.
    if (!sourceName || !targetName || !PortCeProjectCareerAllowed() ||
        gAllCheatsEnabled || CLIPARAMS.mAutoConfigTest)
        return false;
    CFEPSaveGame &page = FRONTEND.mFEPSaveGame;
    constexpr unsigned capacity = sizeof(page.mSaveGameName) / sizeof(WCHAR);
    WCHAR source[capacity] = {}, target[capacity] = {};
    // The ordinary save-name seam validates full .bes leaves and removes the suffix.
    if (PortSaveNameUtf16(sourceName, source, capacity) != 0 ||
        PortSaveNameUtf16(targetName, target, capacity) != 0)
        return false;
    NameScope restore(page);
    bool original[kNativeNameChecks];
    std::memcpy(page.mSaveGameName, source, sizeof(source));
    for (unsigned i = 0; i < kNativeNameChecks; ++i)
        original[i] = page.IsCheatActive(static_cast<int>(i));
    std::memcpy(page.mSaveGameName, target, sizeof(target));
    for (unsigned i = 0; i < kNativeNameChecks; ++i)
        if (original[i] != !!page.IsCheatActive(static_cast<int>(i)))
            return false;
    return true;
}
#endif
