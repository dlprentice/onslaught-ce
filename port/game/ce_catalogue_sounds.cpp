// SPDX-License-Identifier: GPL-3.0-only
#ifdef CE_VERSION	// a CE layer file: a private build (no CE_VERSION) has none of it, as it has none of its callers
#include "ce_catalogue_sounds.h"

#include <stddef.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {
// One snapshot holds at most SOUND_ROWS names of at most SOUND_NAME_BYTES bytes each.
// A longer name is refused whole: shortened, it could pass for a different sample.
// Past the row bound the first names in list order are kept, whatever the visit order.
const size_t SOUND_NAME_BYTES = CE_CATALOGUE_SOUND_NAME_MAX, SOUND_ROWS = 4096;

// Copies owned by this snapshot, in list order, one per equivalent name. Nothing here
// points into the sound system; a row is only ever a name it reported.
std::vector<std::string> names;
bool collecting; // the visitor accepts names only while Open is asking for them
bool started;    // this controller started a preview and has not stopped it since

unsigned char Fold(char c) {
    return static_cast<unsigned char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
}
// List order: ASCII letters without case, every other byte by its own value.
int Order(const std::string& a, const std::string& b) {
    const size_t shared = std::min(a.size(), b.size());
    for (size_t i = 0; i < shared; ++i) {
        const unsigned char x = Fold(a[i]), y = Fold(b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
}
bool Before(const std::string& a, const std::string& b) { return Order(a, b) < 0; }

void Collect(const char* name, void* context) {
    if (!collecting || context != &names || !name) return;
    // Read no further than one byte past the bound. A name with nothing to show
    // (empty, or only spaces and control bytes) cannot be told apart in a list.
    size_t length = 0;
    bool visible = false;
    while (length <= SOUND_NAME_BYTES && name[length]) {
        const unsigned char c = static_cast<unsigned char>(name[length++]);
        if (c > ' ' && c != 127) visible = true;
    }
    if (length > SOUND_NAME_BYTES || !visible) return;
    const std::string copy(name, length);
    const size_t at = static_cast<size_t>(std::lower_bound(names.begin(), names.end(), copy, Before) - names.begin());
    if (at < names.size() && !Order(names[at], copy)) {
        // Spellings that differ only in ASCII case are one row. Keep the bytewise
        // first so the retained spelling does not depend on the visit order.
        if (copy < names[at]) names[at] = copy;
        return;
    }
    if (names.size() >= SOUND_ROWS) {
        if (at == names.size()) return;
        names.pop_back();
    }
    names.insert(names.begin() + static_cast<ptrdiff_t>(at), copy);
}

// Stop only what this controller started. A preview that ran out is still released
// here, so a backend that reports "finished" early cannot leave one behind.
void StopOwned() {
    if (!started) return;
    started = false;
    PortCeCatalogueSoundStop();
}
}

void PortCeCatalogueSoundsOpen() {
    StopOwned();
    names.clear();
    collecting = true;
    PortCeCatalogueSoundVisit(Collect, &names);
    collecting = false;
}
void PortCeCatalogueSoundsStop() { StopOwned(); }

void PortCeCatalogueSoundsClose() {
    StopOwned();
    std::vector<std::string>().swap(names);
}
unsigned PortCeCatalogueSoundsCount() { return static_cast<unsigned>(names.size()); }
// A row outside the snapshot has the empty name, which no listed row can have.
const char* PortCeCatalogueSoundsName(unsigned index) {
    return index < names.size() ? names[index].c_str() : "";
}
bool PortCeCatalogueSoundsPlay(unsigned index) {
    if (index >= names.size()) return false;
    StopOwned();
    started = PortCeCatalogueSoundStart(names[index].c_str());
    return started;
}
// Live playback of the owned preview: never the remembered result of a start.
bool PortCeCatalogueSoundsPlaying() { return started && PortCeCatalogueSoundActive(); }
namespace { char problem[160]; }
bool PortCeCatalogueSoundsPlayFile(const char* game_path) {
    StopOwned();
    problem[0] = 0;
    if (!game_path || !*game_path) { snprintf(problem, sizeof problem, "%s", "This sound has no file to play."); return false; }
    started = PortCeCatalogueSpeechStart(game_path, problem, sizeof problem);
    if (!started && !problem[0]) snprintf(problem, sizeof problem, "%s", "This sound file could not be played.");
    if (started) problem[0] = 0;
    return started;
}
const char* PortCeCatalogueSoundsProblem() { return problem; }
#endif
