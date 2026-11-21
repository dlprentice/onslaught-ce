// SPDX-License-Identifier: GPL-3.0-only
// A snapshot of sounds already available in the current native session.
#ifndef CE_CATALOGUE_SOUNDS_H
#define CE_CATALOGUE_SOUNDS_H
enum { CE_CATALOGUE_SOUND_NAME_MAX = 255 };
void PortCeCatalogueSoundsOpen();
void PortCeCatalogueSoundsClose();
// Stop the owned preview while preserving the current session snapshot.
void PortCeCatalogueSoundsStop();
unsigned PortCeCatalogueSoundsCount();
const char* PortCeCatalogueSoundsName(unsigned index);
bool PortCeCatalogueSoundsPlay(unsigned index);
bool PortCeCatalogueSoundsPlaying();
// An installed spoken line, by the path the game itself builds for it (MessageBox::StartMessage:
// data\\sounds\\<language>\\messagebox\\<name>.ogg). Stops the owned preview first. False when the
// file could not be opened or decoded; PortCeCatalogueSoundsProblem then says why in plain words.
bool PortCeCatalogueSoundsPlayFile(const char* game_path);
const char* PortCeCatalogueSoundsProblem();
// The native adapter enumerates names without loading additional assets. Playback
// uses a dedicated owner; Stop never stops another system's sound events.
typedef void (*CeCatalogueSoundVisitor)(const char* name, void* context);
void PortCeCatalogueSoundVisit(CeCatalogueSoundVisitor visit, void* context);
bool PortCeCatalogueSoundStart(const char* name);
void PortCeCatalogueSoundStop();
bool PortCeCatalogueSoundActive();
// Native: the spoken line is read and decoded by the game's own .ogg reader into memory, made into a
// sample as the radio messages make theirs and played by the same dedicated owner; Stop and Active
// above cover it too. 'why' receives a sentence for the player when nothing could be played.
bool PortCeCatalogueSpeechStart(const char* game_path, char* why, unsigned why_size);
// Native: the running language's folder name as the game spells it ("english"), or "" for none.
const char* PortCeCatalogueLanguage();
// Native: the words of the spoken line of this file stem, as UTF-8, in the language of its folder
// ("french"), read from that language's own text in the game data; false when it names no line for it.
bool PortCeCatalogueSpeechLine(const char* folder, const char* stem, char* utf8, unsigned size);
// Frees the other languages' text the titles were read from.
void PortCeCatalogueSpeechLinesEnd();
// Native: the title the game's Goodies give the model (kind 0) or picture (kind 1) in this resource file, as UTF-8; false when
// the file is not a goodie's (patch 0251 hands the Goodies page's own table to the Catalogue).
bool PortCeCatalogueGoodieName(int kind, const char* file, char* utf8, unsigned size);
// Native: each word (letters only, three or more, lower case) of the game's own text in the running language, once or more;
// the Catalogue splits a run-together asset name only into words like these.
void PortCeCatalogueTextWords(void (*add)(const char* word, void* context), void* context);
// Native: the goodie whose model is this resource file (the Goodies page can show it), or -1.
int PortCeCatalogueGoodieModel(const char* file);
#endif
