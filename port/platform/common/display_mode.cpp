// SPDX-License-Identifier: GPL-3.0-only
// A CE preference for the next process, not a live SDL/D3D mode switch. Original and recorded runs ignore it.
#include <stdlib.h>
bool PortCeLayer();
extern "C" int CE_SettingGetBool(const char *,int);
extern "C" int CE_SettingSet(const char *,int);

namespace { int saved = -1; bool preference_start = false; }

// BEA_PORT_CE_LAYER=1 is allowed to draw CE screens on a tape. It must never let a display preference affect that
// run (or open/write its ce.ini), so check the harness flags independently of the drawing switch.
extern "C" int CE_DisplayModeAvailable()
{
#ifdef CE_VERSION
    const char *names[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"};
    for (const char *name : names) {
        const char *value = getenv(name);
        if (value && *value) return 0;
    }
    return PortCeLayer() ? 1 : 0;
#else
    return 0;
#endif
}
int CE_DisplayModeAvailableBridge() { return CE_DisplayModeAvailable(); }

extern "C" int CE_DisplayModeChoice(int current_windowed)
{
#ifdef CE_VERSION
    if (!CE_DisplayModeAvailable()) return current_windowed ? 1 : 0;
    if (saved < 0) saved = CE_SettingGetBool("windowed",current_windowed ? 1 : 0);
    return saved;
#else
    return current_windowed ? 1 : 0;
#endif
}

extern "C" int CE_DisplayModeStartup(int force_windowed, int restricted)
{
    preference_start = false;
    // Explicit launch arguments win; do not even open ce.ini on the override, demo, test or faithful path.
    if (force_windowed) return 1;
    if (restricted || !CE_DisplayModeAvailable()) return 0;
    preference_start = CE_DisplayModeChoice(0) != 0;
    return preference_start ? 1 : 0;
}

// The legacy parser declares this C++ bridge locally; no shared game header or class layout changes.
int CE_DisplayModeStartupBridge(int force_windowed, int restricted) { return CE_DisplayModeStartup(force_windowed, restricted); }

// Only the persisted windowed preference uses the selected startup mode as the client size. An explicit
// -forcewindowed keeps its existing 640x480/-res semantics and faithful runs do not consult this preference.
extern "C" int CE_DisplayModeUseSavedSize() { return preference_start && CE_DisplayModeAvailable(); }
int CE_DisplayModeUseSavedSizeBridge() { return CE_DisplayModeUseSavedSize(); }

extern "C" int CE_DisplayModeStore(int windowed)
{
#ifdef CE_VERSION
    if (!CE_DisplayModeAvailable() || (windowed != 0 && windowed != 1)) return 0;
    if (!CE_SettingSet("windowed",windowed)) return 0;
    saved = windowed;
    return 1;
#else
    (void)windowed;
    return 0;
#endif
}
