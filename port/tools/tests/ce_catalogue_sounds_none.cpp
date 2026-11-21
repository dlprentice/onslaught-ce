// SPDX-License-Identifier: GPL-3.0-only
// Installed-file tests deliberately have no live native sound session.
#include "../../game/ce_catalogue_sounds.h"
void PortCeCatalogueSoundVisit(CeCatalogueSoundVisitor, void*) {}
bool PortCeCatalogueSoundStart(const char*) { return false; }
void PortCeCatalogueSoundStop() {}
bool PortCeCatalogueSoundActive() { return false; }
bool PortCeCatalogueSpeechStart(const char*, char*, unsigned) { return false; }
const char* PortCeCatalogueLanguage() { return ""; }
bool PortCeCatalogueSpeechLine(const char*, const char*, char*, unsigned) { return false; }
void PortCeCatalogueSpeechLinesEnd() {}
