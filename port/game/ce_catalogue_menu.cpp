// SPDX-License-Identifier: GPL-3.0-only
#ifdef CE_VERSION	// a CE layer file: a private build (no CE_VERSION) has none of it, as it has none of its callers
#include "ce_asset_catalog.h"
#include "ce_catalogue_menu.h"
#include "ce_catalogue_sounds.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

void PortPathsNamesOnly();
const char* PortGameDir();

namespace {
CeAssetCatalog* catalog;
// Collections 0 and 1 index the installed catalogue. Collection 2 indexes the sound
// controller's snapshot of the samples this session has loaded: on the PC that is the whole
// sound bank of the running language, which the game loads when the language is set
// (CPCSoundManager::LoadSampleBank) and never adds to. Collections 3 and up index the
// installed spoken lines, one collection per language folder, the running language first.
std::vector<size_t> entries[CE_CATALOGUE_GROUPS];
struct Collection {
    std::vector<size_t> matches;
    std::string query;
    unsigned page = 0;
    size_t selected = static_cast<size_t>(-1);
} collection[CE_CATALOGUE_GROUPS];
// The language folder of each speech collection (collection 3 + i), as named on disk.
std::vector<std::string> speech_folders;
// How many of each speech folder's lines are named by their words (the rest by their file).
std::vector<unsigned> speech_worded;
// What a speech row shows: the running language's text for the line, else its file stem.
std::vector<std::string> speech_titles;   // by catalogue index; empty for other kinds
bool SpeechCategory(int c) { return c >= 3 && c < 3 + static_cast<int>(speech_folders.size()); }
const char* SpeechTitle(size_t i) { return i < speech_titles.size() ? speech_titles[i].c_str() : ""; }
int category;
bool opened;
// The selected sound by its full name, so the choice survives a new session snapshot.
std::string sound_selected;

// Search the words the player sees. A space also finds underscores in an asset
// filename (the game font cannot draw those underscores), a bank name's backslash
// and the colon of an effect's "Group: words". Bytes outside ASCII
// remain literal, just as in the installed catalogue's read-only search.
std::string SearchName(const char* text) {
    std::string key;
    bool space = false;
    for (const unsigned char* at = reinterpret_cast<const unsigned char*>(text ? text : ""); *at; ++at) {
        unsigned char c = *at;
        if (c <= ' ' || c == '_' || c == ':' || c == '\\') { space = !key.empty(); continue; }
        if (space) key += ' ';
        space = false;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        key += static_cast<char>(c);
    }
    return key;
}
void Name(const char* utf8, char* out, unsigned size);
std::string EffectTitle(const char* raw);
const char* SoundName(size_t i) {
    const char* name = PortCeCatalogueSoundsName(static_cast<unsigned>(i));
    return name ? name : "";
}
void RememberSound() {
    const size_t i = collection[2].selected;
    sound_selected = i < entries[2].size() ? SoundName(i) : "";
}
void MatchCollection() {
    Collection& c = collection[category];
    c.matches.clear();
    const std::string needle = SearchName(c.query.c_str());
    for (size_t i : entries[category]) {
        if (SpeechCategory(category)) {
            // A spoken line: the words the player sees, and the file's own name.
            const char* path = PortCeAssetCatalogPath(catalog, i);
            const char* base = path ? std::strrchr(path, '/') : nullptr;
            char title[256]; Name(SpeechTitle(i), title, sizeof title);
            if (needle.empty() || SearchName(title).find(needle) != std::string::npos ||
                SearchName(base ? base + 1 : path).find(needle) != std::string::npos)
                c.matches.push_back(i);
            continue;
        }
        if (category == 2) {
            // A loaded sound has no file here: search the title the player sees and
            // the full retained name, so its punctuation and spacing stay findable.
            const std::string title = EffectTitle(SoundName(i));
            if (needle.empty() || SearchName(title.c_str()).find(needle) != std::string::npos ||
                SearchName(SoundName(i)).find(needle) != std::string::npos)
                c.matches.push_back(i);
            continue;
        }
        const char* path = PortCeAssetCatalogPath(catalog, i);
        const char* base = path ? std::strrchr(path, '/') : nullptr;
        if (needle.empty() || SearchName(PortCeAssetCatalogTitle(catalog, i)).find(needle) != std::string::npos ||
            SearchName(base ? base + 1 : path).find(needle) != std::string::npos)
            c.matches.push_back(i);
    }
    const auto found = std::find(c.matches.begin(), c.matches.end(), c.selected);
    c.page = found == c.matches.end() ? 0 : static_cast<unsigned>(found - c.matches.begin()) / CE_CATALOGUE_SLOTS;
    // Keep the last valid identity through an empty result so Clear can restore it.
    if (found == c.matches.end() && !c.matches.empty()) c.selected = c.matches.front();
    if (category == 2) RememberSound();
}

// The native face uses single-byte letters. Preserve Latin-1 names and separate filename words.
void Name(const char* utf8, char* out, unsigned size) {
    if (!size) return;
    unsigned n = 0;
    bool space = false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8 ? utf8 : "");
    while (*p && n + 1 < size) {
        unsigned c = *p++;
        if (c >= 0xc0) {
            const unsigned lead = c;
            unsigned extra = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
            c &= extra == 1 ? 31 : extra == 2 ? 15 : 7;
            while (extra && (*p & 0xc0) == 0x80) { c = c * 64 + (*p++ & 63); --extra; }
            if (extra || lead >= 0xf5) c = '?';
        }
        if (c > 255 || (c >= 127 && c < 160)) c = '?';
        if (c < ' ') c = '?';
        if (c == ' ' || std::strchr("_~\\$@[]^{}|", static_cast<char>(c))) { space = n != 0; continue; }
        if (space && n + 2 >= size) break;
        if (space) out[n++] = ' ';
        space = false;
        out[n++] = static_cast<char>(c);
    }
    out[n] = 0;
    if (!n) std::snprintf(out, size, "%s", "Unnamed item");
}
// A file's name as a player reads it: underscores and path marks become spaces, as in every other name here.
std::string PlainFile(const char* name) {
    char text[100]; Name(name, text, sizeof text);
    return text;
}
// A loaded sound's name is "Folder\N_CODE_words" (or a bare name such as "Hit 1"). The player sees the folder
// and the words: "Atmospheres: building on". The search still covers the full name.
std::string EffectTitle(const char* raw) {
    const std::string name = raw ? raw : "";
    const size_t slash = name.find_last_of('\\');
    std::string folder = slash == std::string::npos ? std::string() : name.substr(0, slash);
    std::string leaf = slash == std::string::npos ? name : name.substr(slash + 1);
    // the sound designers' "N_" and kind code ("N_WP_", "N_HUD_") before the words
    if (leaf.size() > 2 && leaf[0] == 'N' && leaf[1] == '_') {
        size_t at = 2;
        while (at < leaf.size() && leaf[at] >= 'A' && leaf[at] <= 'Z') ++at;
        if (at > 2 && at < leaf.size() && leaf[at] == '_') leaf = leaf.substr(at + 1);
    }
    char words[CE_CATALOGUE_SOUND_NAME_MAX + 1]; Name(leaf.c_str(), words, sizeof words);
    if (folder.empty()) return words;
    char group[CE_CATALOGUE_SOUND_NAME_MAX + 1]; Name(folder.c_str(), group, sizeof group);
    return std::string(group) + ": " + words;
}
size_t Entry(unsigned slot) {
    const Collection& c = collection[category];
    return slot < PortCeCatalogueVisible() ? c.matches[c.page * CE_CATALOGUE_SLOTS + slot] : static_cast<size_t>(-1);
}

// Rebuild the sound rows from the controller's current snapshot. The search text
// stays, and the selected sound stays if the session still has that exact name.
void AdoptSounds() {
    const unsigned count = PortCeCatalogueSoundsCount();
    entries[2].clear();
    collection[2].selected = static_cast<size_t>(-1);
    for (unsigned i = 0; i < count; ++i) {
        entries[2].push_back(i);
        if (!sound_selected.empty() && sound_selected == SoundName(i)) collection[2].selected = i;
    }
    const int browsing = category;
    category = 2; MatchCollection(); category = browsing;
}
// Reopening the snapshot is the controller's way to end its preview and keep a list.
void RefreshSounds() { PortCeCatalogueSoundsOpen(); AdoptSounds(); }
// The frontend also closes the sound controller when the player leaves a sound. While
// this catalogue stays open, rows never outlive that snapshot: the next use of the
// collection takes one new snapshot. Nothing is re-read while the two agree.
void SyncSounds() {
    if (!opened || entries[2].size() == PortCeCatalogueSoundsCount()) return;
    if (!PortCeCatalogueSoundsCount()) PortCeCatalogueSoundsOpen();
    AdoptSounds();
}
}

void PortCeCatalogueClose() {
    // Leaving the catalogue ends the owned preview and drops the session snapshot.
    PortCeCatalogueSoundsClose(); opened = false; sound_selected.clear();
    PortCeAssetCatalogClose(catalog); catalog = nullptr;
    for (int g = 0; g < CE_CATALOGUE_GROUPS; ++g) { entries[g].clear(); collection[g] = Collection(); }
    speech_folders.clear(); speech_worded.clear(); std::vector<std::string>().swap(speech_titles); category = 0;
}
void PortCeCatalogueOpen() {
    PortCeCatalogueClose(); PortPathsNamesOnly();
    char error[128]; catalog = PortCeAssetCatalogOpenKinds(PortGameDir(), CE_ASSET_MODEL | CE_ASSET_TEXTURE | CE_ASSET_SPEECH, error, sizeof error);
    // Speech collections: the running language's folder first, the others in the catalogue's order.
    const std::string running = SearchName(PortCeCatalogueLanguage());
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < PortCeAssetCatalogCount(catalog); ++i) {
            if (PortCeAssetCatalogKind(catalog, i) != CE_ASSET_SPEECH) continue;
            const std::string folder = PortCeAssetCatalogGroup(catalog, i) ? PortCeAssetCatalogGroup(catalog, i) : "";
            if ((SearchName(folder.c_str()) == running) != (pass == 0)) continue;
            if (std::find(speech_folders.begin(), speech_folders.end(), folder) == speech_folders.end() &&
                speech_folders.size() < CE_CATALOGUE_GROUPS - 3) speech_folders.push_back(folder);
        }
    speech_titles.assign(PortCeAssetCatalogCount(catalog), std::string());
    speech_worded.assign(speech_folders.size(), 0);
    for (size_t i = 0; i < PortCeAssetCatalogCount(catalog); ++i) {
        const int kind = PortCeAssetCatalogKind(catalog, i);
        if (kind == CE_ASSET_MODEL || kind == CE_ASSET_TEXTURE) { entries[kind == CE_ASSET_TEXTURE].push_back(i); continue; }
        if (kind != CE_ASSET_SPEECH) continue;
        const char* group = PortCeAssetCatalogGroup(catalog, i);
        const auto at = std::find(speech_folders.begin(), speech_folders.end(), std::string(group ? group : ""));
        if (at == speech_folders.end()) continue;
        entries[3 + (at - speech_folders.begin())].push_back(i);
        char line[256];
        const char* stem = PortCeAssetCatalogTitle(catalog, i);
        if (PortCeCatalogueSpeechLine(group, stem, line, sizeof line)) { speech_titles[i] = line; ++speech_worded[at - speech_folders.begin()]; }
        else speech_titles[i] = stem ? stem : "";
    }
    PortCeCatalogueSpeechLinesEnd();
    for (int g = 0; g < CE_CATALOGUE_GROUPS; ++g) if (g != 2) { category = g; MatchCollection(); }
    category = 0;
    opened = true; RefreshSounds();
}
bool PortCeCatalogueGroupWorded(int group) {
    return !SpeechCategory(group) || speech_worded[group - 3] != 0;
}
void PortCeCatalogueRowTag(unsigned slot, char* out, unsigned size) {
    if (!size) return;
    const size_t i = Entry(slot); out[0] = 0;
    if (i == static_cast<size_t>(-1)) return;
    if (category == 2) {
        // An effect's own short name in the bank, kind code and all ("N HUD ring"): what tells two effects of the same words apart.
        const std::string name = SoundName(i);
        const size_t slash = name.find_last_of('\\');
        Name(slash == std::string::npos ? name.c_str() : name.c_str() + slash + 1, out, size);
        return;
    }
    // The file's own name, without its extension: what tells two rows of the same words apart.
    std::string stem = PortCeAssetCatalogPath(catalog, i) ? PortCeAssetCatalogPath(catalog, i) : "";
    const size_t slash = stem.find_last_of('/');
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot) stem = stem.substr(0, dot);
    Name(stem.c_str(), out, size);
}
unsigned PortCeCatalogueGroups() { return 3 + static_cast<unsigned>(speech_folders.size()); }
void PortCeCatalogueGroupLabel(int group, char* out, unsigned size) {
    if (!size) return;
    if (group == 0) std::snprintf(out, size, "%s", "Models");
    else if (group == 1) std::snprintf(out, size, "%s", "Textures");
    else if (group == 2) std::snprintf(out, size, "%s", "Sound effects");
    else if (SpeechCategory(group)) {
        std::string language = speech_folders[group - 3];
        if (!language.empty() && language[0] >= 'a' && language[0] <= 'z') language[0] = static_cast<char>(language[0] - 'a' + 'A');
        char name[64]; Name(language.c_str(), name, sizeof name);
        std::snprintf(out, size, "Speech: %s", name);
    } else out[0] = 0;
}
unsigned PortCeCatalogueCount(int group) {
    if (group == 2) SyncSounds();
    return group >= 0 && group < static_cast<int>(PortCeCatalogueGroups()) ? static_cast<unsigned>(entries[group].size()) : 0;
}
void PortCeCatalogueBrowse(int group) {
    const int next = group >= 0 && group < static_cast<int>(PortCeCatalogueGroups()) ? group : 0;
    // A preview belongs to its sound collection and never follows the player out of it.
    if (opened && category >= 2 && next != category) PortCeCatalogueSoundsStop();
    category = next;
}
const char* PortCeCatalogueQuery() { return collection[category].query.c_str(); }
unsigned PortCeCatalogueMatches() {
    if (category == 2) SyncSounds();
    return static_cast<unsigned>(collection[category].matches.size());
}
void PortCeCatalogueFilter(const char* text) {
    if (category == 2) SyncSounds();
    std::string query = std::string(text ? text : "").substr(0, CE_CATALOGUE_QUERY_MAX);
    const size_t begin = query.find_first_not_of(" \t\r\n"), end = query.find_last_not_of(" \t\r\n");
    collection[category].query = begin == std::string::npos ? "" : query.substr(begin, end-begin+1);
    MatchCollection();
}
void PortCeCatalogueSelect(unsigned slot) {
    const size_t item = Entry(slot);
    if (item != static_cast<size_t>(-1)) collection[category].selected = item;
    if (category == 2) RememberSound();
}
unsigned PortCeCatalogueSelectedSlot() {
    for (unsigned slot = 0; slot < PortCeCatalogueVisible(); ++slot)
        if (Entry(slot) == collection[category].selected) return slot;
    return 0;
}
int PortCeCataloguePageNumber() {
    if (category == 2) SyncSounds();
    return collection[category].page + 1;
}
int PortCeCataloguePageCount() { return std::max(1U, (PortCeCatalogueMatches() + CE_CATALOGUE_SLOTS - 1) / CE_CATALOGUE_SLOTS); }
void PortCeCatalogueTurn() {
    collection[category].page = (collection[category].page + 1) % PortCeCataloguePageCount();
    PortCeCatalogueSelect(0);
}
unsigned PortCeCatalogueVisible() {
    // Count first: a sound snapshot taken here may also move the page.
    const unsigned matches = PortCeCatalogueMatches();
    const unsigned first = collection[category].page * CE_CATALOGUE_SLOTS;
    return first < matches ? std::min<unsigned>(CE_CATALOGUE_SLOTS, matches - first) : 0;
}
void PortCeCatalogueTitle(unsigned slot, char* out, unsigned size) {
    if (!size) return;
    const size_t i = Entry(slot); out[0] = 0;
    if (i == static_cast<size_t>(-1)) return;
    if (category == 2) std::snprintf(out, size, "%s", EffectTitle(SoundName(i)).c_str());
    else Name(SpeechCategory(category) ? SpeechTitle(i) : PortCeAssetCatalogTitle(catalog, i), out, size);
}
void PortCeCatalogueDetails(unsigned slot, char* out, unsigned size) {
    if (!size) return;
    const size_t i = Entry(slot); out[0] = 0;
    if (i == static_cast<size_t>(-1)) return;
    if (SpeechCategory(category)) {
        // The installed file, its language and the words of the line when the running language has them.
        const char* path = PortCeAssetCatalogPath(catalog, i);
        const char* base = path ? std::strrchr(path, '/') : nullptr;
        char title[100]; Name(SpeechTitle(i), title, sizeof title);
        char group[48]; PortCeCatalogueGroupLabel(category, group, sizeof group);
        std::snprintf(out, size, "%s\n%s, item %u of %u.\nFile: %s", title, group,
                      collection[category].page * CE_CATALOGUE_SLOTS + slot + 1, PortCeCatalogueMatches(),
                      PlainFile(base ? base + 1 : path).c_str());
        return;
    }
    if (category == 2) {
        // The effect's group and words, and where it sits in the list.
        std::snprintf(out, size, "%s\nSound effects, item %u of %u.", EffectTitle(SoundName(i)).c_str(),
                      collection[category].page * CE_CATALOGUE_SLOTS + slot + 1, PortCeCatalogueMatches());
        return;
    }
    char name[100]; Name(PortCeAssetCatalogTitle(catalog, i), name, sizeof name);
    const char* group = PortCeAssetCatalogGroup(catalog, i);
    const char* type = !category ? "Installed model" : group && !std::strcmp(group, "Interface textures") ? "Installed interface texture" : "Installed game texture";
    if (category) {
        const char* path = PortCeAssetCatalogPath(catalog, i);
        const char* base = path ? std::strrchr(path, '/') : nullptr;
        const std::string filename = PlainFile(base ? base + 1 : path);
        char details[90];
        CeAssetTextureHeader header;
        if (PortCeAssetCatalogTextureHeader(catalog, i, &header))
            std::snprintf(details, sizeof details, "%u x %u pixels\nDDS: %s, %u mip level%s",
                          unsigned(header.width), unsigned(header.height), header.format, unsigned(header.levels), header.levels == 1 ? "" : "s");
        else std::snprintf(details, sizeof details, "Texture header unavailable.");
        std::snprintf(out, size, "%s\nSource: %s\n%s\n%s", name,
                      group && !std::strcmp(group, "Interface textures") ? "resources/textures" : "resources/dxtntextures", filename.c_str(), details);
        return;
    }
    std::snprintf(out, size, "%s\n\n%s.\nItem %u of %u in %s.", name, type,
                  collection[category].page * CE_CATALOGUE_SLOTS + slot + 1, PortCeCatalogueMatches(), category ? "Textures" : "Models");
}

bool PortCeCatalogueIsTexture(unsigned slot) {
    return category == 1 && Entry(slot) != static_cast<size_t>(-1);
}
bool PortCeCatalogueReadTexture(unsigned slot, CeAssetTextureReader reader, void* context) {
    // A sound row counts into the session snapshot, never into the installed catalogue.
    return category != 2 && !SpeechCategory(category) && PortCeAssetCatalogReadTexture(catalog, Entry(slot), reader, context) != 0;
}

bool PortCeCatalogueIsSound(unsigned slot) {
    return (category == 2 || SpeechCategory(category)) && Entry(slot) != static_cast<size_t>(-1);
}
bool PortCeCataloguePlaySound(unsigned slot) {
    const size_t i = Entry(slot);
    if (i == static_cast<size_t>(-1) || i != collection[category].selected) return false;
    if (category == 2) return PortCeCatalogueSoundsPlay(static_cast<unsigned>(i));
    if (!SpeechCategory(category)) return false;
    // The path the game builds for a radio message's line, with this file's stem.
    const char* stem = PortCeAssetCatalogTitle(catalog, i);
    const std::string path = std::string("data\\sounds\\") + speech_folders[category - 3] + "\\messagebox\\" + (stem ? stem : "") + ".ogg";
    return PortCeCatalogueSoundsPlayFile(path.c_str());
}
const char* PortCeCatalogueSoundProblem() { return PortCeCatalogueSoundsProblem(); }
void PortCeCatalogueShowItem(unsigned index) {
    if (category == 2) SyncSounds();
    Collection& c = collection[category];
    if (index < c.matches.size()) c.page = index / CE_CATALOGUE_SLOTS;
}
int PortCeCatalogueSelectedIndex() {
    if (category == 2) SyncSounds();
    const Collection& c = collection[category];
    const auto at = std::find(c.matches.begin(), c.matches.end(), c.selected);
    return at == c.matches.end() ? -1 : static_cast<int>(at - c.matches.begin());
}
int PortCeCatalogueCategory() { return category; }
bool PortCeCatalogueBrowsingSounds() { return opened && (category == 2 || SpeechCategory(category)); }
#endif
