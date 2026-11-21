// SPDX-License-Identifier: GPL-3.0-only
// Synthetic checks for the loaded-sound snapshot controller and its catalogue collection.
// Build with ce_catalogue_sounds.cpp, ce_catalogue_menu.cpp and ce_asset_catalog.cpp. The one
// argument names a game root whose asset folders may be empty. Every sample name below is
// invented, and only the number of checks is printed.
#include "ce_catalogue_menu.h"
#include "ce_catalogue_sounds.h"
#include "ce_extras.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
const char* root;
unsigned checks;
void Check(bool value, const char* why) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

// The controller's bounds: bytes in one name, rows in one snapshot.
const unsigned NAME_LIMIT = CE_CATALOGUE_SOUND_NAME_MAX, ROW_LIMIT = 4096;

// A stand-in sound system: the names it has loaded now, and one preview owner.
struct Fake {
    std::vector<std::string> loaded;
    bool pass_null = false, refuse = false, active = false;
    unsigned visits = 0, starts = 0, stops = 0, overlaps = 0;
    std::string started;
    CeCatalogueSoundVisitor late = nullptr;
    void* late_context = nullptr;
};
Fake fake;
char scratch[512];

std::string SoundName(unsigned index) {
    const char* name = PortCeCatalogueSoundsName(index);
    return name ? name : "(null)";
}
std::vector<std::string> Snapshot() {
    std::vector<std::string> rows;
    for (unsigned i = 0; i < PortCeCatalogueSoundsCount(); ++i) rows.push_back(SoundName(i));
    return rows;
}
bool Loaded(const std::string& name) {
    return std::find(fake.loaded.begin(), fake.loaded.end(), name) != fake.loaded.end();
}
std::string Title(unsigned slot) { char b[160]; PortCeCatalogueTitle(slot, b, sizeof b); return b; }
std::string Details(unsigned slot) { char b[1024]; PortCeCatalogueDetails(slot, b, sizeof b); return b; }
// What a frontend asks for while it draws one page.
void Frame() {
    PortCeCatalogueCount(2); PortCeCatalogueMatches(); PortCeCataloguePageNumber(); PortCeCataloguePageCount();
    PortCeCatalogueSelectedSlot();
    for (unsigned slot = 0; slot < PortCeCatalogueVisible(); ++slot) { Title(slot); PortCeCatalogueIsSound(slot); }
}
// Three names that need cleaning for the font, then enough plain ones for three pages.
// In list order: Chimes\\N_FE_Soft (a bank-style name: folder, kind code, words), deep  hum, pulse_00 .. pulse_26, relay~click[2].
std::vector<std::string> Session() {
    std::vector<std::string> names = {"relay~click[2]", "deep  hum", "Chimes\\N_FE_Soft"};
    for (unsigned i = 0; i < 27; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "pulse_%02u", i);
        names.push_back(name);
    }
    return names;
}
}

extern "C" {
static int RefuseTexture(const unsigned char*, size_t, const CeAssetTextureHeader*, void* context) {
    ++*static_cast<unsigned*>(context);
    return 1;
}
}

// The Extras host's helpers and the CE page's hooks that the sound list box (ce_catalogue_list.cpp) uses.
void PortCeExtrasWiden(WCHAR* out, int n, const char* in) {
    int i = 0;
    for (; in[i] && i < n - 1; ++i) out[i] = static_cast<WCHAR>(static_cast<unsigned char>(in[i]));
    out[i] = 0;
}
std::string list_details;
unsigned list_finds;
int list_shown_page = -1, list_shown_start = -1;
void PortCeCatalogueListDetails(const char* text) { list_details = text; }
void PortCeCatalogueListFind() { ++list_finds; }
void PortCeExtrasShow(int page, int startRow) { list_shown_page = page; list_shown_start = startRow; }

void PortPathsNamesOnly() {}
const char* PortGameDir() { return root; }

void PortCeCatalogueSoundVisit(CeCatalogueSoundVisitor visit, void* context) {
    ++fake.visits;
    fake.late = visit; fake.late_context = context;
    if (fake.pass_null) visit(nullptr, context);
    for (const std::string& name : fake.loaded) {
        // Every name is lent from one buffer that is overwritten once the visitor returns.
        std::memset(scratch, 0, sizeof scratch);
        std::memcpy(scratch, name.data(), std::min(name.size(), sizeof scratch - 1));
        visit(scratch, context);
        std::memset(scratch, '#', sizeof scratch - 1);
    }
}
bool PortCeCatalogueSoundStart(const char* name) {
    ++fake.starts;
    if (fake.refuse) return false;
    if (fake.active) ++fake.overlaps;
    fake.started = name ? name : "";
    fake.active = true;
    return true;
}
void PortCeCatalogueSoundStop() { ++fake.stops; fake.active = false; }
// Spoken lines: a path that names "broken" cannot be decoded; any other starts at once.
std::string speech_started;
unsigned speech_starts;
bool PortCeCatalogueSpeechStart(const char* game_path, char* why, unsigned why_size) {
    ++speech_starts;
    if (fake.active) ++fake.overlaps;
    const std::string path = game_path ? game_path : "";
    if (path.find("broken") != std::string::npos) {
        std::snprintf(why, why_size, "%s", "This sound file could not be decoded, so it was skipped.");
        return false;
    }
    speech_started = path; fake.active = true;
    return true;
}
const char* PortCeCatalogueLanguage() { return "French"; }
// Lines are worded in their own folder's language; a folder named "italian" has no text, as if its file were missing.
unsigned speech_lines_ended = 0;
bool PortCeCatalogueSpeechLine(const char* folder, const char* stem, char* utf8, unsigned size) {
    if (!stem || std::strncmp(stem, "line_", 5) || (folder && !std::strcmp(folder, "italian"))) return false;
    std::snprintf(utf8, size, "Words of %s in %s", stem + 5, folder ? folder : "?");
    return true;
}
void PortCeCatalogueSpeechLinesEnd() { ++speech_lines_ended; }
bool PortCeCatalogueSoundActive() { return fake.active; }

namespace {
void TestSnapshot() {
    fake = Fake();
    fake.pass_null = true;
    fake.loaded = {"zeta", "Alpha", "", "beta", "ALPHA", "   ", "\t\n", "alpha", "Beta2", "zeta",
                   "\xE9" "clat", "bell\x01ring"};
    const std::vector<std::string> expected = {"ALPHA", "bell\x01ring", "beta", "Beta2", "zeta", "\xE9" "clat"};
    PortCeCatalogueSoundsOpen();
    Check(fake.visits == 1 && !fake.starts && !fake.stops, "a snapshot asks the backend once and plays or stops nothing");
    Check(PortCeCatalogueSoundsCount() == 6, "null, empty and blank names are dropped and equivalent names merged");
    Check(Snapshot() == expected, "names are owned copies in ASCII case-insensitive order with their bytes unchanged");
    std::reverse(fake.loaded.begin(), fake.loaded.end());
    PortCeCatalogueSoundsOpen();
    Check(Snapshot() == expected, "the list does not depend on the order the backend reports names");
    fake.late("intruder", fake.late_context);
    Check(Snapshot() == expected, "a name reported outside a snapshot is ignored");

    const std::string edge(NAME_LIMIT, 'e'), over = edge + "x";
    const std::vector<std::string> without = {"anchor"}, with = {"anchor", edge};
    fake.pass_null = false;
    fake.loaded = {over, "anchor", std::string(300, 'w')};
    PortCeCatalogueSoundsOpen();
    Check(Snapshot() == without, "an overlong name is refused, never listed in a shortened form");
    fake.loaded.push_back(edge);
    PortCeCatalogueSoundsOpen();
    Check(Snapshot() == with, "a name exactly at the length bound is kept whole");
    Check(PortCeCatalogueSoundsPlay(1) && fake.started == edge, "the longest name is played in full");

    fake.loaded.clear();
    for (unsigned i = 5000; i-- > 0;) {
        char name[16];
        std::snprintf(name, sizeof name, "n%04u", i); fake.loaded.push_back(name);
        std::snprintf(name, sizeof name, "N%04u", i); fake.loaded.push_back(name);
    }
    PortCeCatalogueSoundsOpen();
    Check(!fake.active, "a new snapshot ends the preview of the old one");
    bool ordered = PortCeCatalogueSoundsCount() == ROW_LIMIT;
    for (unsigned i = 0; ordered && i < ROW_LIMIT; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "N%04u", i);
        ordered = SoundName(i) == name;
    }
    Check(ordered, "the snapshot is bounded and keeps the first names in list order, one spelling each");
    Check(PortCeCatalogueSoundsPlay(ROW_LIMIT - 1) && fake.started == "N4095" && !PortCeCatalogueSoundsPlay(ROW_LIMIT) &&
          fake.started == "N4095", "the last row inside the bound plays and the next one does not exist");
    PortCeCatalogueSoundsClose();
    Check(!fake.active && !PortCeCatalogueSoundsCount(), "close ends the preview and empties the snapshot");
}

void TestPreview() {
    fake = Fake();
    fake.loaded = {"gamma", "delta", "epsilon"}; // listed as delta, epsilon, gamma
    PortCeCatalogueSoundsOpen();
    Check(!PortCeCatalogueSoundsPlaying() && !fake.starts && !fake.stops, "a new snapshot has no preview");
    Check(!PortCeCatalogueSoundsPlay(3) && !PortCeCatalogueSoundsPlay(UINT_MAX) && !fake.starts, "a row past the end starts nothing");
    Check(SoundName(3).empty() && SoundName(UINT_MAX).empty(), "a row past the end has the empty name");
    Check(PortCeCatalogueSoundsPlay(1) && fake.started == "epsilon" && PortCeCatalogueSoundsPlaying(), "a row plays its own full name");
    Check(!PortCeCatalogueSoundsPlay(3) && fake.starts == 1 && !fake.stops && PortCeCatalogueSoundsPlaying(),
          "a rejected row leaves the running preview alone");
    Check(PortCeCatalogueSoundsPlay(1) && fake.starts == 2 && fake.stops == 1 && !fake.overlaps && PortCeCatalogueSoundsPlaying(),
          "repeating a row stops it and starts it again");
    Check(PortCeCatalogueSoundsPlay(2) && fake.started == "gamma" && fake.stops == 2 && !fake.overlaps,
          "the previous preview stops before another row starts");
    fake.active = false; // the sample ran out
    Check(!PortCeCatalogueSoundsPlaying(), "a finished preview is not reported as playing");
    Check(PortCeCatalogueSoundsPlay(0) && fake.started == "delta" && PortCeCatalogueSoundsPlaying() && !fake.overlaps,
          "a row plays again after a preview has finished");
    fake.refuse = true;
    Check(!PortCeCatalogueSoundsPlay(1) && fake.starts == 5 && !fake.active && !PortCeCatalogueSoundsPlaying(),
          "a backend refusal is reported and leaves nothing playing");
    const unsigned stops = fake.stops;
    fake.active = true; // sound the controller did not start
    Check(!PortCeCatalogueSoundsPlaying(), "playback the controller does not own is never reported");
    PortCeCatalogueSoundsClose();
    Check(fake.stops == stops && fake.active, "close with no owned preview stops nothing");
    PortCeCatalogueSoundsOpen();
    Check(fake.stops == stops && fake.active, "open with no owned preview stops nothing");
    fake.active = false; fake.refuse = false;

    Check(PortCeCatalogueSoundsPlay(0) && fake.started == "delta" && fake.active, "a refused backend can play later");
    PortCeCatalogueSoundsClose();
    Check(fake.stops == stops + 1 && !fake.active && !PortCeCatalogueSoundsPlaying(), "close ends the owned preview");
    fake.active = true;
    Check(!PortCeCatalogueSoundsPlaying() && !PortCeCatalogueSoundsCount() && !PortCeCatalogueSoundsPlay(0) && fake.starts == 6,
          "a closed snapshot reports no playback and has no row to play");
    fake.active = false;

    PortCeCatalogueSoundsOpen();
    Check(PortCeCatalogueSoundsPlay(0) && fake.started == "delta", "row 0 plays the first listed name");
    fake.loaded = {"gamma", "epsilon", "aardvark"}; // the session moved on
    const std::vector<std::string> now = {"aardvark", "epsilon", "gamma"};
    PortCeCatalogueSoundsOpen();
    Check(!fake.active && !PortCeCatalogueSoundsPlaying(), "reopening ends the previous preview");
    Check(Snapshot() == now, "reopening lists the session as it is now");
    Check(PortCeCatalogueSoundsPlay(0) && fake.started == "aardvark" && !fake.overlaps,
          "a row plays its current name, not the one it held before");
    PortCeCatalogueSoundsClose();
}

void TestMenu() {
    fake = Fake();
    fake.loaded = Session();
    PortCeCatalogueOpen();
    Check(PortCeCatalogueCount(1) == 1, "the synthetic sound guard control has one coexisting texture");
    Check(fake.visits == 1 && !fake.starts, "opening the catalogue takes one snapshot and plays nothing");
    Check(PortCeCatalogueCount(2) == 30 && !PortCeCatalogueCount(3) && !PortCeCatalogueCount(-1), "the third collection counts the snapshot");
    const unsigned models = PortCeCatalogueCount(0), textures = PortCeCatalogueCount(1);
    Check(!PortCeCatalogueIsSound(0) && !PortCeCataloguePlaySound(0), "the model collection has no sound rows");
    PortCeCatalogueBrowse(1);
    Check(!PortCeCatalogueIsSound(0) && !PortCeCataloguePlaySound(0) && !fake.starts, "the texture collection has no sound rows");

    PortCeCatalogueBrowse(2);
    Check(PortCeCatalogueMatches() == 30 && PortCeCataloguePageCount() == 3 && PortCeCataloguePageNumber() == 1 &&
          PortCeCatalogueVisible() == 12, "loaded sounds page like the other collections");
    Check(Title(0) == "Chimes: Soft" && Title(1) == "deep hum" && Title(11) == "pulse 09" && Title(12).empty(),
          "rows show cleaned titles in list order");
    Check(PortCeCatalogueIsSound(0) && PortCeCatalogueIsSound(11) && !PortCeCatalogueIsSound(12) && !PortCeCatalogueIsTexture(0),
          "visible rows are sounds and not textures");
    unsigned read = 0;
    Check(!PortCeCatalogueReadTexture(0, RefuseTexture, &read) && !PortCeCatalogueReadTexture(11, RefuseTexture, &read) && !read,
          "texture access rejects sound rows");
    Check(PortCeCatalogueSelectedSlot() == 0 && PortCeCataloguePlaySound(0) && fake.started == "Chimes\\N_FE_Soft" &&
          PortCeCatalogueSoundsPlaying(), "the selected row plays under its full retained name");
    Check(!PortCeCataloguePlaySound(3) && !PortCeCataloguePlaySound(12) && !PortCeCataloguePlaySound(UINT_MAX) &&
          fake.starts == 1 && fake.started == "Chimes\\N_FE_Soft" && PortCeCatalogueSoundsPlaying(),
          "an unselected or missing row starts nothing and leaves the preview alone");
    PortCeCatalogueSelect(3);
    Check(PortCeCataloguePlaySound(3) && fake.started == "pulse_01" && !fake.overlaps, "a newly selected row replaces the preview");
    PortCeCatalogueTurn();
    Check(PortCeCataloguePageNumber() == 2 && PortCeCatalogueSelectedSlot() == 0 && Title(0) == "pulse 10" &&
          PortCeCataloguePlaySound(0) && fake.started == "pulse_10", "the second page plays its own rows");
    PortCeCatalogueTurn();
    PortCeCatalogueSelect(5);
    Check(PortCeCatalogueVisible() == 6 && Title(5) == "relay click 2" && PortCeCataloguePlaySound(5) &&
          fake.started == "relay~click[2]" && !PortCeCatalogueIsSound(6) && !PortCeCataloguePlaySound(6) &&
          fake.started == "relay~click[2]", "the short last page is bounded");

    const std::string detail = Details(5);
    Check(detail == "relay click 2\nSound effects, item 30 of 30.",
          "details give the effect's name and place in plain words, with no encoding marks");
    Check(detail.find("Installed") == std::string::npos && detail.find("resources") == std::string::npos,
          "details do not present a loaded sound as an installed file");
    Check(Details(6).empty(), "no details past the last row");

    PortCeCatalogueFilter("relay click 2");
    Check(PortCeCatalogueMatches() == 1 && Title(0) == "relay click 2", "search finds the title as displayed");
    PortCeCatalogueFilter("~CLICK[2]");
    Check(PortCeCatalogueMatches() == 1 && Title(0) == "relay click 2", "search finds raw-name punctuation without case");
    PortCeCatalogueFilter("deep hum");
    const unsigned spaced = PortCeCatalogueMatches();
    PortCeCatalogueFilter("DEEP  HUM");
    Check(spaced == 1 && PortCeCatalogueMatches() == 1 && Title(0) == "deep hum", "search finds a name through either spacing");
    PortCeCatalogueFilter("chimes soft");
    Check(PortCeCatalogueMatches() == 1 && Title(0) == "Chimes: Soft", "search finds an effect by its group and words");
    PortCeCatalogueFilter("n_fe_soft");
    Check(PortCeCatalogueMatches() == 1 && Title(0) == "Chimes: Soft", "search still finds a raw bank name");
    PortCeCatalogueFilter("pulse");
    Check(PortCeCatalogueMatches() == 27 && PortCeCataloguePageCount() == 3 && PortCeCataloguePageNumber() == 1,
          "a filtered collection still pages");
    PortCeCatalogueTurn();
    PortCeCatalogueSelect(2);
    PortCeCatalogueFilter("pulse 1");
    Check(PortCeCatalogueMatches() == 10 && PortCeCatalogueSelectedSlot() == 4 && Title(4) == "pulse 14",
          "the selected sound keeps its identity through a narrower search");
    Check(PortCeCataloguePlaySound(4) && fake.started == "pulse_14" && !fake.overlaps, "a filtered row plays the sound it shows");
    unsigned starts = fake.starts;
    PortCeCatalogueFilter("no such tone");
    Check(!PortCeCatalogueMatches() && !PortCeCatalogueVisible() && PortCeCataloguePageCount() == 1 &&
          PortCeCataloguePageNumber() == 1, "a search with no match has no phantom rows");
    Check(Title(0).empty() && Details(0).empty() && !PortCeCatalogueIsSound(0) && !PortCeCataloguePlaySound(0) &&
          fake.starts == starts, "an empty result plays nothing");
    PortCeCatalogueTurn();
    PortCeCatalogueFilter("");
    Check(PortCeCataloguePageNumber() == 2 && PortCeCatalogueSelectedSlot() == 4 && Title(4) == "pulse 14",
          "clearing a search with no match returns to the selected sound");
    Check(fake.visits == 1, "browsing, paging and searching never re-read the session list");

    PortCeCatalogueFilter("pulse 1");
    Check(PortCeCatalogueSoundsPlaying(), "the preview is still the one started above");
    const unsigned snapshot_before_leave = PortCeCatalogueCount(2);
    fake.loaded.insert(fake.loaded.begin(), "aaa_new_tone"); // loaded since the snapshot; sorts first
    PortCeCatalogueBrowse(0);
    Check(!fake.active && !PortCeCatalogueSoundsPlaying(), "leaving the sound collection ends its preview");
    starts = fake.starts;
    Check(!PortCeCatalogueIsSound(0) && !PortCeCataloguePlaySound(0) && PortCeCatalogueCount(0) == models,
          "the model collection is unchanged and plays nothing");
    PortCeCatalogueBrowse(1);
    Check(!PortCeCatalogueIsSound(0) && !PortCeCataloguePlaySound(0) && PortCeCatalogueCount(1) == textures && fake.starts == starts,
          "the texture collection is unchanged and plays nothing");
    Check(PortCeCatalogueCount(2) == snapshot_before_leave, "leaving a collection preserves its snapshot until reopen");
    PortCeCatalogueBrowse(2);
    Check(std::string(PortCeCatalogueQuery()) == "pulse 1" && PortCeCatalogueMatches() == 10 && PortCeCatalogueSelectedSlot() == 4 &&
          Title(4) == "pulse 14", "returning keeps the collection's own search and selected sound although rows moved");
    Check(!fake.active && fake.starts == starts, "returning does not resume a preview");
    Check(PortCeCataloguePlaySound(4) && fake.started == "pulse_14", "the selected sound still plays under its own name");

    fake.loaded.erase(std::find(fake.loaded.begin(), fake.loaded.end(), std::string("pulse_14"))); // no longer loaded
    PortCeCatalogueOpen();
    Check(!fake.active && !PortCeCatalogueSoundsPlaying(), "reopening the catalogue ends the preview");
    PortCeCatalogueBrowse(2);
    Check(PortCeCatalogueCount(2) == 30 && !*PortCeCatalogueQuery() && PortCeCataloguePageNumber() == 1 &&
          PortCeCatalogueSelectedSlot() == 0 && Title(0) == "aaa new tone", "a reopened catalogue lists the session as it is now");
    starts = fake.starts;
    PortCeCatalogueFilter("pulse 14");
    Check(!PortCeCatalogueMatches() && !PortCeCataloguePlaySound(0) && fake.starts == starts,
          "a sound that is no longer loaded is neither listed nor playable");
    PortCeCatalogueFilter("");

    std::vector<std::string> played;
    bool each = true;
    for (int page = 0; page < PortCeCataloguePageCount(); ++page) {
        for (unsigned slot = 0; slot < PortCeCatalogueVisible(); ++slot) {
            PortCeCatalogueSelect(slot);
            each = each && PortCeCataloguePlaySound(slot) && Loaded(fake.started) && PortCeCatalogueSoundsPlaying();
            played.push_back(fake.started);
        }
        PortCeCatalogueTurn();
    }
    std::sort(played.begin(), played.end());
    Check(each && played.size() == 30 && std::unique(played.begin(), played.end()) == played.end() && !fake.overlaps,
          "every row plays one distinct sound that is loaded now, one at a time");

    const bool playing = fake.active;
    PortCeCatalogueClose();
    Check(playing && !fake.active && !PortCeCatalogueSoundsPlaying() && !PortCeCatalogueSoundsCount(),
          "leaving the catalogue ends the preview and drops the snapshot");
    const unsigned visits = fake.visits;
    starts = fake.starts;
    PortCeCatalogueBrowse(2);
    Frame();
    Check(!PortCeCatalogueCount(2) && !PortCeCatalogueMatches() && !PortCeCatalogueVisible() && Title(0).empty() &&
          !PortCeCatalogueIsSound(0) && !PortCeCataloguePlaySound(0) && fake.visits == visits && fake.starts == starts,
          "a closed catalogue lists, reads and plays nothing");
    PortCeCatalogueBrowse(0);
}

void TestFrontendLeave() {
    fake = Fake();
    fake.loaded = Session();
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    PortCeCatalogueTurn();
    PortCeCatalogueSelect(4);
    Check(PortCeCataloguePlaySound(4) && fake.started == "pulse_14" && fake.active, "a sound on the second page is playing");
    PortCeCatalogueSoundsClose(); // the frontend's own call when the player leaves the sound
    Check(!fake.active && !PortCeCatalogueSoundsPlaying(), "the frontend's close ends the preview");
    Check(PortCeCataloguePageNumber() == 2 && PortCeCatalogueSelectedSlot() == 4 && Title(4) == "pulse 14" &&
          PortCeCatalogueCount(2) == 30, "the open catalogue lists the session again with the same sound selected");
    for (int frame = 0; frame < 50; ++frame) Frame();
    Check(fake.visits == 2 && fake.starts == 1 && !fake.active, "that took one new snapshot, not one per frame, and resumed nothing");
    Check(PortCeCataloguePlaySound(4) && fake.started == "pulse_14", "the selected sound plays again under its own name");

    fake.loaded = {"solo_tone"}; // the session changed while the sound page was left
    PortCeCatalogueSoundsClose();
    Check(!fake.active, "the frontend's close ends the second preview");
    Check(PortCeCatalogueVisible() == 1 && PortCeCataloguePageNumber() == 1 && PortCeCatalogueSelectedSlot() == 0 &&
          Title(0) == "solo tone" && !PortCeCatalogueIsSound(1) && !PortCeCataloguePlaySound(4), "rows of the old snapshot are gone");
    Check(PortCeCataloguePlaySound(0) && fake.started == "solo_tone", "only a sound of the current session plays");

    fake.loaded.clear();
    PortCeCatalogueSoundsClose();
    const unsigned visits = fake.visits, starts = fake.starts;
    for (int frame = 0; frame < 50; ++frame) Frame();
    Check(!PortCeCatalogueCount(2) && !PortCeCatalogueVisible() && Title(0).empty() && !PortCeCatalogueIsSound(0) &&
          !PortCeCataloguePlaySound(0) && fake.starts == starts && fake.visits == visits + 1,
          "an emptied session shows an empty list after one re-read");

    PortCeCatalogueClose();
    const unsigned stops = fake.stops;
    PortCeCatalogueSoundsClose();
    Check(fake.stops == stops && fake.visits == visits + 1 && !fake.active, "a second close on final leave does nothing more");
}

void TestDetailsAndEmpty() {
    fake = Fake();
    std::string full;
    for (unsigned i = 0; i < NAME_LIMIT; ++i) full += static_cast<char>('a' + i % 26);
    fake.loaded = {full, full + "z"};
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    Check(PortCeCatalogueCount(2) == 1 && PortCeCatalogueVisible() == 1, "only the name inside the length bound is listed");
    // The front end's message box breaks a word wider than the box (ce_frontend.cpp WrapLines), so the name is whole here.
    Check(Details(0) == full + "\nSound effects, item 1 of 1.", "details give the complete name and its place in the list");
    Check(PortCeCataloguePlaySound(0) && fake.started == full, "the longest listed name plays in full");
    PortCeCatalogueClose();
    Check(!fake.active, "closing ends the long name's preview");

    fake = Fake();
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    for (int frame = 0; frame < 5; ++frame) Frame();
    PortCeCatalogueTurn();
    PortCeCatalogueSelect(0);
    Check(!PortCeCatalogueCount(2) && !PortCeCatalogueVisible() && PortCeCataloguePageCount() == 1 && PortCeCataloguePageNumber() == 1 &&
          PortCeCatalogueSelectedSlot() == 0, "a session with no loaded sounds is an empty collection");
    PortCeCatalogueFilter("tone");
    Check(!PortCeCatalogueMatches() && Title(0).empty() && Details(0).empty() && !PortCeCatalogueIsSound(0) &&
          !PortCeCataloguePlaySound(0) && !fake.starts && !fake.stops && fake.visits == 1,
          "an empty collection searches, plays and stops nothing");
    PortCeCatalogueClose();
}
}

static void TestStopOnly() {
    fake = Fake();
    fake.loaded = {"Owned Alpha", "Owned Beta"};
    PortCeCatalogueSoundsOpen();
    const std::vector<std::string> saved = Snapshot();
    const unsigned visits = fake.visits;
    Check(PortCeCatalogueSoundsPlay(0), "stop-only control starts an owned preview");
    PortCeCatalogueSoundsStop();
    Check(!PortCeCatalogueSoundsPlaying() && Snapshot() == saved && fake.visits == visits,
          "stop-only preserves names and never enumerates the session again");
    const unsigned stops = fake.stops;
    PortCeCatalogueSoundsStop();
    Check(fake.stops == stops, "a repeated preview stop has no extra backend effect");
    Check(PortCeCatalogueSoundsPlay(1), "another row remains playable after stopping details");
    PortCeCatalogueSoundsClose();
    Check(!PortCeCatalogueSoundsCount() && !PortCeCatalogueSoundsPlaying(), "final close releases playback and the snapshot");
}

// Spoken lines from installed folders: one category per language folder that has a MessageBox
// folder (any letter case), the running language first; titles from the language's text.
static void TestSpeech() {
    namespace fs = std::filesystem;
    char label_italian[48];
    const fs::path base = fs::path(root) / "data/sounds";
    fs::remove_all(base);                     // the folders exist only for this check
    auto touch = [](const fs::path& file) { fs::create_directories(file.parent_path()); std::ofstream(file) << "x"; };
    touch(base / "english/MessageBox/line_one.ogg");
    touch(base / "english/MessageBox/plain_two.ogg");
    touch(base / "english/MessageBox/broken_three.ogg");
    touch(base / "english/MessageBox/notes.txt");
    touch(base / "french/messagebox/line_one.ogg");
    touch(base / "german/other/line_one.ogg");
    touch(base / "italian/MessageBox/line_one.ogg");
    fake = Fake(); speech_starts = 0; speech_started.clear();
    const unsigned ended = speech_lines_ended;
    PortCeCatalogueOpen();
    Check(PortCeCatalogueGroups() == 6, "three language folders with spoken lines make three more categories");
    Check(speech_lines_ended == ended + 1, "the other languages' text is let go once the titles are read");
    PortCeCatalogueGroupLabel(5, label_italian, sizeof label_italian);
    Check(!std::strcmp(label_italian, "Speech: Italian") && !PortCeCatalogueGroupWorded(5) && PortCeCatalogueGroupWorded(3) &&
          PortCeCatalogueGroupWorded(4) && PortCeCatalogueGroupWorded(2), "a language without text is flagged once, for its page hint");
    PortCeCatalogueBrowse(5);
    Check(Title(0) == "line one", "its lines are named by their files");
    char label[48];
    PortCeCatalogueGroupLabel(3, label, sizeof label);
    Check(!std::strcmp(label, "Speech: French"), "the running language comes first, its folder matched without letter case");
    PortCeCatalogueGroupLabel(4, label, sizeof label);
    Check(!std::strcmp(label, "Speech: English"), "the other language follows");
    PortCeCatalogueGroupLabel(2, label, sizeof label);
    Check(!std::strcmp(label, "Sound effects"), "the session's bank is the effects category");
    Check(PortCeCatalogueCount(3) == 1 && PortCeCatalogueCount(4) == 3, "each category counts its folder's .ogg files and nothing else");
    PortCeCatalogueBrowse(4);
    Check(PortCeCatalogueVisible() == 3, "every spoken line of the folder is listed");
    std::vector<std::string> titles;
    for (unsigned slot = 0; slot < 3; ++slot) titles.push_back(Title(slot));
    Check(std::find(titles.begin(), titles.end(), "Words of one in english") != titles.end() &&
          std::find(titles.begin(), titles.end(), "plain two") != titles.end(),
          "a line shows the language's words when it has them, else its readable file name");
    unsigned good = UINT_MAX, broken = UINT_MAX;
    for (unsigned slot = 0; slot < 3; ++slot) {
        const std::string details = Details(slot);
        if (details.find("plain") != std::string::npos) good = slot;
        if (details.find("broken") != std::string::npos) broken = slot;
    }
    Check(good != UINT_MAX && broken != UINT_MAX && Details(good).find("Speech: English") != std::string::npos,
          "details name the file and its language");
    Check(PortCeCatalogueIsSound(good) && !PortCeCatalogueIsTexture(good), "a spoken line is a sound row");
    PortCeCatalogueSelect(good);
    Check(PortCeCataloguePlaySound(good) && speech_started == "data\\sounds\\english\\messagebox\\plain_two.ogg",
          "a line plays from the path the game builds for it");
    Check(!*PortCeCatalogueSoundProblem() && PortCeCatalogueSoundsPlaying(), "a started line is playing and has no problem");
    PortCeCatalogueSelect(broken);
    const unsigned stops = fake.stops;
    Check(!PortCeCataloguePlaySound(broken) && fake.stops == stops + 1 && !PortCeCatalogueSoundsPlaying(),
          "another line stops the first; a file that cannot be decoded plays nothing");
    Check(std::strstr(PortCeCatalogueSoundProblem(), "could not be decoded") != nullptr, "the problem is said in plain words");
    PortCeCatalogueSelect(good);
    Check(PortCeCataloguePlaySound(good) && !*PortCeCatalogueSoundProblem(), "a good line still plays after a bad one");
    PortCeCatalogueBrowse(3);
    Check(!fake.active, "moving to another speech category stops the preview");
    Check(PortCeCatalogueVisible() == 1 && PortCeCatalogueSelectedSlot() == 0, "a category opens on its first line");
    Check(Title(0) == "Words of one in french", "each language's lines are worded in that language's own text");
    Check(PortCeCataloguePlaySound(0) && speech_started == "data\\sounds\\french\\messagebox\\line_one.ogg", "the running language's line plays");
    PortCeCatalogueBrowse(0);
    Check(!fake.active, "leaving for models stops the preview");
    PortCeCatalogueBrowse(4); PortCeCatalogueSelect(0);
    char texture = 0; unsigned called = 0;
    Check(!PortCeCatalogueReadTexture(0, RefuseTexture, &called) && !called, "a spoken line is never read as a texture");
    (void)texture;
    PortCeCatalogueFilter("words");
    Check(PortCeCatalogueMatches() == 1, "find searches the words the player sees");
    PortCeCatalogueFilter("plain two");
    Check(PortCeCatalogueMatches() == 1, "find searches the file name too");
    PortCeCatalogueClose();
    Check(!fake.active && PortCeCatalogueGroups() == 3, "closing ends the preview and drops the speech categories");
    fs::remove_all(base);
}

namespace {
std::string Narrow(const WCHAR* text) { std::string out; for (; *text; ++text) out += static_cast<char>(*text); return out; }
SCeListRow ListRow(const SCeListPage* page, int index) { SCeListRow row; std::memset(&row, 0, sizeof row); page->mRow(index, &row); return row; }
std::string ListHint(const SCeListPage* page, int index) {
    WCHAR help[200] = {}, extra[200] = {};
    page->mHint(index, help, extra, 200);
    return Narrow(help) + " | " + Narrow(extra);
}

// The sound list box: whole titles, Find at the end, the playing row marked, look-alike rows told apart, a long list whole.
static void TestList() {
    fake = Fake();
    fake.loaded = {"Bells\\N_A_ring", "Bells\\N_HUD_ring", "Chimes\\N_FE_Soft"};
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    PortCeCatalogueListTitle("Sound effects");
    const SCeListPage* page = PortCeCatalogueSoundsListPage();
    Check(page && Narrow(page->mTitle) == "Sound effects" && page->mCount() == 4, "the list has every effect and Find");
    Check(Narrow(ListRow(page, 3).mLeft) == "Find by name", "Find is the row after the items");
    const SCeListRow first = ListRow(page, 0), second = ListRow(page, 1), third = ListRow(page, 2);
    Check(Narrow(first.mLeft) == "Bells: ring" && Narrow(second.mLeft) == "Bells: ring" && Narrow(third.mLeft) == "Chimes: Soft",
          "rows show whole titles");
    Check(Narrow(first.mRight) == "N A ring" && Narrow(second.mRight) == "N HUD ring" && first.mTone == CE_TONE_DIM && !third.mRight[0],
          "only rows that read the same carry a grey tag, and it tells them apart");
    Check(page->mSelect(0) && fake.started == "Bells\\N_A_ring" && fake.active &&
          list_details == "Bells: ring\nSound effects, item 1 of 3.", "choosing a row plays it and shows its details");
    Check(Narrow(ListRow(page, 0).mRight) == "Playing" && ListRow(page, 0).mTone == CE_TONE_GOOD, "the playing row is marked");
    Check(ListHint(page, 0) == "Bells: ring | Item 1 of 3. Playing: choose another, or go back to stop it.",
          "the hint gives the whole line, its place in the list and that it is playing");
    Check(page->mSmallFace && page->mWiden == 20.f, "the rows are in the smaller face, in a box widened toward the right arc");
    const unsigned stops = fake.stops;
    Check(page->mSelect(2) && fake.started == "Chimes\\N_FE_Soft" && fake.stops == stops + 1 && !fake.overlaps,
          "choosing another stops the first");
    Check(Narrow(ListRow(page, 2).mRight) == "Playing" && Narrow(ListRow(page, 0).mRight) == "N A ring", "the mark follows the sound");
    page->mClose();
    Check(!fake.active, "leaving the list stops its sound");
    const unsigned finds = list_finds;
    Check(page->mSelect(0) && fake.active, "a row plays before Find is opened");
    Check(page->mSelect(3) && list_finds == finds + 1 && !fake.active, "Find by name opens the page's Find and stops the sound");
    PortCeCatalogueFilter("bells");
    Check(page->mCount() == 4 && Narrow(ListRow(page, 3).mLeft) == "Show all" && ListHint(page, 0).find("Item 1 of 2 found.") != std::string::npos,
          "a search lists what it found, then Find and Show all");
    list_shown_page = -1; list_shown_start = -1;
    Check(page->mSelect(3) && *PortCeCatalogueQuery() == 0 && page->mCount() == 4 &&
          list_shown_page == XP_SOUNDS && list_shown_start == 0, "Show all ends the search and resets cursor");
    fake.refuse = true;
    Check(page->mSelect(1) && list_details.find("Bells: ring\nSound effects, item 2 of 3.\n") == 0 && !fake.active,
          "a sound that does not start says so in its details");
    fake.refuse = false;
    PortCeCatalogueClose();

    // A list as long as a language's speech, and longer: every row is reachable and named, without pages.
    fake = Fake();
    for (unsigned i = 0; i < 700; ++i) { char name[24]; std::snprintf(name, sizeof name, "Long\\N_A_line_%03u", i); fake.loaded.push_back(name); }
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    Check(page->mCount() == 701 && Narrow(ListRow(page, 650).mLeft) == "Long: line 650" && ListHint(page, 699).find("Item 700 of 700.") != std::string::npos,
          "a 700-row list keeps every row and its place");
    Check(page->mSelect(650) && fake.started == "Long\\N_A_line_650" && Narrow(ListRow(page, 650).mRight) == "Playing", "a row far down plays and is marked");
    page->mClose();
    PortCeCatalogueClose();

    // Long titles that begin the same keep their start and their own end, so that each row reads differently.
    fake = Fake();
    fake.loaded = {"Calls\\N_A_muspell_ground_forces_are_closing_in_on_the_east_of_the_base",
                   "Calls\\N_A_muspell_ground_forces_are_closing_in_on_the_west_of_the_base", "Calls\\N_A_short"};
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    const std::string east = Narrow(ListRow(page, 0).mLeft), west = Narrow(ListRow(page, 1).mLeft), brief = Narrow(ListRow(page, 2).mLeft);
    Check(east == "Calls: muspell... the east of the base" && west == "Calls: muspell... the west of the base" &&
          east.size() <= 42 && brief == "Calls: short" && !ListRow(page, 0).mRight[0],
          "long rows that begin the same show their own ends; short rows are whole");
    Check(ListHint(page, 1).rfind("Calls: muspell ground forces are closing in on the west of the base |", 0) == 0,
          "the hint has the highlighted row's whole line");
    PortCeCatalogueClose();
    fake = Fake();
    fake.loaded = {"Calls\\N_A_enemy_forces_are_breaching_the_eastern_defences_now", "Calls\\N_A_other"};
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    Check(Narrow(ListRow(page, 0).mLeft) == "Calls: enemy forces are breaching the...", "a long row is cut at a space, then marked");
    PortCeCatalogueClose();
    // Titles that differ only after the cut (the word that tells them apart would be dropped), and a sentence's own stop.
    fake = Fake();
    fake.loaded = {"Calls\\N_A_muspell_still_attacking_the_eastern_defences", "Calls\\N_A_muspell_still_attacking_the_southern_defences",
                   "Calls\\N_A_muspell_still_attacking_the_western_defences", "Calls\\N_B_you_have_two_primary_controls. Movementandfirestogether"};
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    const std::string r0 = Narrow(ListRow(page, 0).mLeft), r1 = Narrow(ListRow(page, 1).mLeft), r2 = Narrow(ListRow(page, 2).mLeft);
    Check(r0 != r1 && r1 != r2 && r0 != r2 && r0.find("eastern") != std::string::npos && r1.find("southern") != std::string::npos &&
          r2.find("western") != std::string::npos, "rows that would read the same after the cut keep the word that tells them apart");
    const std::string r3 = Narrow(ListRow(page, 3).mLeft);
    Check(r3 == "Calls: you have two primary controls...", "a cut after a sentence's stop shows three dots, not four");
    PortCeCatalogueClose();
    // Regression: a short title matching a neighbour's EndCut must not underflow size_t or read past its end.
    fake = Fake();
    fake.loaded = {"a...", "a........................................................end"};
    PortCeCatalogueOpen();
    PortCeCatalogueBrowse(2);
    Check(Narrow(ListRow(page, 0).mLeft) == "a..." && Narrow(ListRow(page, 1).mLeft).find("end") != std::string::npos,
          "a short title matching a long neighbour's cut stays whole without underflow");
    PortCeCatalogueClose();
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    root = argv[1];
    TestSnapshot();
    TestPreview();
    TestStopOnly();
    TestMenu();
    TestFrontendLeave();
    TestDetailsAndEmpty();
    TestSpeech();
    TestList();
    std::printf("%u/%u loaded-sound controller and catalogue checks passed\n", checks, checks);
    return 0;
}
