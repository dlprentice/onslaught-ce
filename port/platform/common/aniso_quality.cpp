// SPDX-License-Identifier: GPL-3.0-only
#include "aniso_quality.h"
extern "C" int PortCeRecordFeaturesAllowed(void) __attribute__((weak));
#include <stdlib.h>

extern bool PortCeLayer();
extern "C" int CE_SettingGetText(const char *key, char *out, int n);

namespace {
int admitted = -1;
int requested = -1;
int activeLevel = 4;
bool active = false;
unsigned hardwareMax = 16;
bool hardwareSupported = true;
bool hardwareReported = false;

bool Allowed()
{
    if (admitted >= 0) return admitted != 0;
    admitted = 0;
#ifdef CE_VERSION
    if (!(PortCeRecordFeaturesAllowed && PortCeRecordFeaturesAllowed())) {
        const char *const fences[] = {
            "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"
        };
        for (const char *name : fences) {
            const char *value = getenv(name);
            if (value && *value) return false;
        }
    }
    if (!PortCeLayer()) return false;
    admitted = 1;
#endif
    return admitted != 0;
}

bool ValidLevel(int level)
{
    return level == 0 || level == 4 || level == 8 || level == 16;
}

int ParseSavedLevel()
{
    char text[32] = {};
    if (!CE_SettingGetText("anisotropic_filtering", text, static_cast<int>(sizeof(text))))
        return -1;
    const char *p = text;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) return -1;
    char *end = nullptr;
    const long parsed = strtol(p, &end, 10);
    if (end == p) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
    if (*end) return -1;
    const int level = static_cast<int>(parsed);
    return ValidLevel(level) ? level : -1;
}

void InitialChoice()
{
    if (requested >= 0) return;
    const int saved = ParseSavedLevel();
    requested = saved >= 0 ? saved : 16;
}
}

extern "C" int CE_AnisoChoiceToLevel(int choice)
{
    switch (choice) {
    case 0: return 0;
    case 1: return 4;
    case 2: return 8;
    case 3: return 16;
    default: return -1;
    }
}

extern "C" int CE_AnisoLevelToChoice(int level)
{
    switch (level) {
    case 0: return 0;
    case 4: return 1;
    case 8: return 2;
    case 16: return 3;
    default: return -1;
    }
}

extern "C" int CE_AnisoLevelChoice()
{
    if (!Allowed()) return 4;
    InitialChoice();
    return requested;
}

extern "C" int CE_AnisoChoice()
{
    return CE_AnisoLevelToChoice(CE_AnisoLevelChoice());
}

extern "C" int CE_AnisoRequest(int level)
{
    if (!ValidLevel(level) || !Allowed()) return 0;
    requested = level;
    return 1;
}

extern "C" unsigned CE_AnisoHardwareMax()
{
    if (!hardwareReported) return 16;
    if (!hardwareSupported || hardwareMax < 2) return 1;
    return hardwareMax;
}

extern "C" void CeAnisoSetHardwareCaps(unsigned maxAnisotropy, bool supported)
{
    hardwareReported = true;
    hardwareSupported = supported && maxAnisotropy >= 2;
    hardwareMax = hardwareSupported ? maxAnisotropy : 1;
}

void CeAnisoQualityBeginFrame()
{
    if (!Allowed()) {
        active = false;
        activeLevel = 4;
        return;
    }
    InitialChoice();
    active = true;
    activeLevel = requested;
}

bool CeAnisoQualityActive()
{
    return active;
}

int CeAnisoQualityTargetLevel()
{
    return active ? activeLevel : 4;
}

unsigned CeAnisoEffectiveLevel(unsigned retailLevel, unsigned maxHardwareAnisotropy, bool supported)
{
    if (!active) return retailLevel;
    if (!supported || maxHardwareAnisotropy < 2 || activeLevel <= 0) return 1;
    const unsigned target = static_cast<unsigned>(activeLevel);
    return target < maxHardwareAnisotropy ? target : maxHardwareAnisotropy;
}

bool CeAnisoUseAnisotropicFilter(unsigned retailLevel, unsigned maxHardwareAnisotropy, bool supported)
{
    if (!active) return retailLevel > 1;
    return CeAnisoEffectiveLevel(retailLevel, maxHardwareAnisotropy, supported) >= 2;
}
