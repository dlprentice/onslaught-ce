// SPDX-License-Identifier: GPL-3.0-only
#ifndef PORT_ANISO_QUALITY_H
#define PORT_ANISO_QUALITY_H

// Main-thread API. Anisotropic filtering level for ordinary Onslaught CE sessions.
// Valid levels are 0 (Off), 4 (4x), 8 (8x), and 16 (16x). Requests apply at the
// next complete render frame boundary so every pass sees one consistent setting.
// Faithful, fenced, and non-CE runs always keep the game's original filtering.
extern "C" int CE_AnisoLevelChoice();
extern "C" int CE_AnisoChoice();
extern "C" int CE_AnisoRequest(int level);
extern "C" int CE_AnisoChoiceToLevel(int choice);
extern "C" int CE_AnisoLevelToChoice(int level);
extern "C" unsigned CE_AnisoHardwareMax();
extern "C" void CeAnisoSetHardwareCaps(unsigned maxAnisotropy, bool supported);

void CeAnisoQualityBeginFrame();
bool CeAnisoQualityActive();
int CeAnisoQualityTargetLevel();
unsigned CeAnisoEffectiveLevel(unsigned retailLevel, unsigned maxHardwareAnisotropy, bool hardwareSupported);
bool CeAnisoUseAnisotropicFilter(unsigned retailLevel, unsigned maxHardwareAnisotropy, bool hardwareSupported);

#endif
