// Original CE cinema catalogue adapter. The media and file-system stay in the host.
#ifndef CE_MUSEUM_CINEMA_CATALOGUE_H
#define CE_MUSEUM_CINEMA_CATALOGUE_H

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace CeMuseumCinemaCatalogue {

enum { kCapacity = 128, kStemCapacity = 128, kTitleCapacity = 160 };

struct Entry {
    unsigned sourceIndex;
    char stem[kStemCapacity];
    char title[kTitleCapacity];
};

struct List {
    Entry entries[kCapacity];
    unsigned count;
    bool truncated;
    List() : count(0), truncated(false) {}
};

typedef bool (*NameReader)(void *, unsigned, char *, unsigned);
typedef bool (*Available)(void *, const char *);
typedef bool (*StartFilm)(void *, const Entry &);

inline bool SafeStem(const char *text, unsigned capacity) {
    if (!text || !capacity || !text[0]) return false;
    unsigned size = 0;
    for (; size < capacity && text[size]; ++size) {
        const unsigned char c = static_cast<unsigned char>(text[size]);
        if (c < 32 || c > 126 || c == '/' || c == '\\' || c == ':') return false;
    }
    if (size == capacity) return false;
    return std::strcmp(text, ".") && std::strcmp(text, "..");
}

inline void DisplayTitle(const char *stem, unsigned ordinal, char *out, unsigned capacity) {
    if (!out || !capacity) return;
    out[0] = 0;
    unsigned used = 0;
    bool space = false;
    for (unsigned i = 0; stem && stem[i] && used + 1 < capacity; ++i) {
        const unsigned char c = static_cast<unsigned char>(stem[i]);
        if (c == '_' || c == ' ' || c == '\t') {
            space = used != 0;
            continue;
        }
        if (c < 32 || c > 126) continue;
        if (space && used + 2 < capacity) out[used++] = ' ';
        space = false;
        out[used++] = static_cast<char>(c);
    }
    out[used] = 0;
    if (!used) std::snprintf(out, capacity, "Film %u", ordinal + 1);
}

// The reader enumerates the original catalogue; unavailable entries do not become
// controls. Store the original index explicitly rather than confusing it with the
// visitor's compact selection index.
inline List Build(NameReader read, Available available, void *context, unsigned sourceLimit) {
    List out;
    if (!read || !available || !sourceLimit) return out;
    for (unsigned source = 0; source < sourceLimit; ++source) {
        char stem[kStemCapacity] = {};
        if (!read(context, source, stem, sizeof(stem))) return out;
        if (!SafeStem(stem, sizeof(stem)) || !available(context, stem)) continue;
        if (out.count == kCapacity) { out.truncated = true; return out; }
        Entry &entry = out.entries[out.count];
        entry.sourceIndex = source;
        std::memcpy(entry.stem, stem, sizeof(stem));
        DisplayTitle(stem, out.count, entry.title, sizeof(entry.title));
        ++out.count;
    }
    out.truncated = true;
    return out;
}

inline const Entry *At(const List &list, int selected) {
    return selected >= 0 && static_cast<unsigned>(selected) < list.count &&
           static_cast<unsigned>(selected) < kCapacity ? &list.entries[selected] : 0;
}

// An initial Play retains the projector's first-playable policy when a present
// file cannot be decoded. An explicit visitor selection never silently switches
// to another film. A successful result is the actual compact playing index.
inline int Play(const List &list, int requested, bool firstPlayable,
                StartFilm start, void *context) {
    if (!start || !At(list, requested)) return -1;
    if (!firstPlayable)
        return start(context, *At(list, requested)) ? requested : -1;
    for (unsigned i = 0; i < list.count && i < kCapacity; ++i)
        if (start(context, list.entries[i])) return static_cast<int>(i);
    return -1;
}

} // namespace CeMuseumCinemaCatalogue
#endif
