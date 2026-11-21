// SPDX-License-Identifier: GPL-3.0-only
// Installed-file tests deliberately have no live native sound session.
#include <cstdio>
#include <cstdlib>
#include "../../game/ce_catalogue_sounds.h"
void PortCeCatalogueSoundVisit(CeCatalogueSoundVisitor, void*) {}
bool PortCeCatalogueSoundStart(const char*) { return false; }
void PortCeCatalogueSoundStop() {}
bool PortCeCatalogueSoundActive() { return false; }
bool PortCeCatalogueSpeechStart(const char*, char*, unsigned) { return false; }
const char* PortCeCatalogueLanguage() { return ""; }
bool PortCeCatalogueSpeechLine(const char*, const char*, char*, unsigned) { return false; }
void PortCeCatalogueSpeechLinesEnd() {}
bool PortCeCatalogueGoodieName(int, const char*, char*, unsigned) { return false; }
int PortCeCatalogueGoodieModel(const char*) { return -1; }
// A host check can give the game's words from a file (one per line, CE_CATALOGUE_TEXT_WORDS), to name a real install as the game does.
void PortCeCatalogueTextWords(void (*add)(const char*, void*), void* context) {
    const char* path = std::getenv("CE_CATALOGUE_TEXT_WORDS");
    std::FILE* file = path ? std::fopen(path, "r") : nullptr;
    if (!file) return;
    char word[64];
    while (std::fscanf(file, "%63s", word) == 1) add(word, context);
    std::fclose(file);
}
