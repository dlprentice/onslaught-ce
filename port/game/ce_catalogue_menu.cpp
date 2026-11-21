// SPDX-License-Identifier: GPL-3.0-only
#ifdef CE_VERSION	// a CE layer file: a private build (no CE_VERSION) has none of it, as it has none of its callers
#include "ce_asset_catalog.h"
#include "ce_catalogue_menu.h"
#include "ce_catalogue_english.h"
#include "ce_catalogue_sounds.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
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
std::string AssetRow(size_t i);
std::string Tidy(const std::string& raw);
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
        if (needle.empty() || SearchName(AssetRow(i).c_str()).find(needle) != std::string::npos ||
            SearchName(PortCeAssetCatalogTitle(catalog, i)).find(needle) != std::string::npos ||
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
// Models and textures as a player reads them: a group and a name, made from the file's own name by fixed rules (the
// game's folders and the makers' prefixes), never shown as the file. A goodie's title from the game's text replaces the
// made name when the game has one for that file (PortCeCatalogueGoodieName).
struct SPrefix { const char* code; const char* group; };
// Texture folders, as the game's resources keep them.
const SPrefix kTextureFolders[] = {
    {"meshtex", "Model skins"}, {"goodies", "Goodies"}, {"cubes", "Sky boxes"}, {"frontend", "Front end"},
    {"particle", "Particles"}, {"hud", "HUD"}, {"messagebox", "Message box"}, {"mixers", "Terrain blends"},
    {"messagelog", "Message log"}, {"atmospherics", "Weather"}, {"screenfx", "Screen effects"}, {"pausemenu", "Pause menu"},
    {"interface", "Interface"}, {"clouds", "Clouds"}};
// Model name prefixes, longest first: the makers' codes for side and kind.
const SPrefix kModelPrefixes[] = {
    {"fa", "Forseti aircraft"}, {"fb", "Forseti buildings"}, {"fs", "Forseti ships"}, {"ft", "Forseti turrets"},
    {"mt", "Muspell turrets"}, {"mb", "Muspell buildings"}, {"boss", "Bosses"}, {"be", "Battle Engine"}, {"f", "Forseti"}, {"m", "Muspell"}};
// Words of a goodies subfolder ("ca_f_units"): the side letters spelled out.
const SPrefix kSideWords[] = {{"f", "Forseti"}, {"m", "Muspell"}, {"be", "Battle Engine"}};

bool SameWord(const char* a, size_t n, const char* b) {
    if (std::strlen(b) != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
// A word of letters then digits ("av14", "xf24", "a15b") is a designation: capitals and a hyphen ("AV-14").
std::string Word(const char* w, size_t n) {
    size_t letters = 0;
    while (letters < n && std::isalpha(static_cast<unsigned char>(w[letters]))) ++letters;
    const bool code = letters >= 1 && letters <= 3 && letters < n && std::isdigit(static_cast<unsigned char>(w[letters]));
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        if (code && i == letters) out += '-';
        out += code ? static_cast<char>(std::toupper(static_cast<unsigned char>(w[i]))) : w[i];
    }
    return out;
}
// The words of a name part: split at underscores, hyphens between letters stay; leading codes dropped as asked.
std::vector<std::string> Words(const std::string& part) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < part.size()) {
        while (at < part.size() && (part[at] == '_' || part[at] == ' ')) ++at;
        size_t end = at;
        while (end < part.size() && part[end] != '_' && part[end] != ' ') ++end;
        if (end > at) out.push_back(part.substr(at, end - at));
        at = end;
    }
    return out;
}
// The words of every model and texture file's name, lower case, with the game's own words: known words for splitting run-together ones.
std::set<std::string> vocabulary;
std::set<std::string> text_words;		// words of the game's own text in the running language
void AddTextWord(const char* word, void*) { text_words.insert(word); vocabulary.insert(word); }
bool AllAlpha(const std::string& w) {
    for (char c : w) if (!std::isalpha(static_cast<unsigned char>(c))) return false;
    return !w.empty();
}
std::string Lower(std::string w) { for (char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return w; }
std::string Upper(std::string w) { for (char& c : w) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return w; }
bool English(const std::string& low) {
    static const std::set<std::string> words(std::begin(kCeCatalogueEnglish), std::end(kCeCatalogueEnglish));
    return words.count(low) > 0;
}
// A known word: one of the game's own words or file-name words (vocabulary), or an ordinary English word; three letters or more.
bool Known(const std::string& low) { return low.size() >= 3 && (vocabulary.count(low) || English(low)); }
// A plain word: the game's own text or the English list, never a file name's word; `least` letters or more.
bool Plain(const std::string& low, size_t least) { return low.size() >= least && (text_words.count(low) || English(low)); }
// low as exactly `pieces` words of `least` letters or more, the longest first piece first; cuts gets where each later piece
// starts. Plain: only the game's text words and English words count, not a file name's own words.
bool Segment(const std::string& low, int pieces, std::vector<size_t>* cuts, size_t least, bool plain) {
    auto ok = [&](const std::string& w) { return plain ? Plain(w, least) : w.size() >= least && Known(w); };
    if (pieces == 1) return ok(low);
    for (size_t cut = low.size() >= least ? low.size() - least : 0; cut >= least; --cut) {
        if (!ok(low.substr(0, cut))) continue;
        std::vector<size_t> rest;
        if (!Segment(low.substr(cut), pieces - 1, &rest, least, plain)) continue;
        cuts->push_back(cut);
        for (size_t r : rest) cuts->push_back(cut + r);
        return true;
    }
    return false;
}
// low as `fewest` to three words, the fewest pieces that read. Two pieces may be any known words; three, or any beside a code,
// only plain ones.
bool KnownWords(const std::string& low, int fewest, std::vector<size_t>* cuts, bool beside_code) {
    for (int n = fewest; n <= 3; ++n) {
        cuts->clear();
        if (Segment(low, n, cuts, beside_code ? 4 : 3, beside_code || n > 2)) return true;
    }
    return false;
}
// Codes are read only in model files' names: their stems are the makers' run-together codes, where textures' names are mostly
// ordinary English that a short list cannot know ("spark" is no "S Park"). AssetName sets it while it names a model.
bool sModelCodes = false;
// A run-together word as the pieces it reads as. Every piece is a known word of three letters or more; failing that, known words
// with the makers' own code of one to three letters before or after them, kept as written in capitals ("Thunder B", "HC Bridge")
// and never read as a meaning. An ordinary English ending is not a code. A word the game's text or the English list knows whole
// is left whole.
bool SplitWord(const std::string& p, std::vector<std::string>* out) {
    const std::string low = Lower(p);
    if (!AllAlpha(p) || text_words.count(low) || English(low)) return false;
    std::vector<size_t> cuts;
    if (low.size() >= 6 && KnownWords(low, 2, &cuts, false)) {
        // each piece read again: a file name's own word may itself be a word and a code ("mtank" in "mtanklight")
        cuts.push_back(p.size());
        size_t at = 0;
        for (size_t c : cuts) {
            const std::string piece = p.substr(at, c - at);
            std::vector<std::string> again;
            if (SplitWord(piece, &again)) out->insert(out->end(), again.begin(), again.end());
            else out->push_back(piece);
            at = c;
        }
        return true;
    }
    static const char* const endings[] = {"s", "es", "ed", "er", "ers", "ly", "al", "en", "ic", "y", "ing", "ion", "ner", "ter", "est", "ies",
                                          "ous", "ful", "ity", "ist", "ism", "per", "ted", "ent", "age", "ish", "let"};
    static const char* const starts[] = {"a", "un", "re", "de", "in", "ex", "pre", "dis", "non", "sub", "mis", "out", "over", "anti"};
    if (!sModelCodes || low.size() < 4) return false;
    for (size_t total = 1; total <= 6; ++total)				// the shortest code first
        for (size_t head = 0; head <= 3 && head <= total; ++head) {
            const size_t tail = total - head;
            if (tail > 3 || head + tail + 3 > low.size()) continue;
            const std::string front = low.substr(0, head), back = low.substr(low.size() - tail), middle = low.substr(head, low.size() - head - tail);
            bool plain = true;
            for (const char* e : endings) plain = plain && !(tail && back == e);
            for (const char* s : starts) plain = plain && !(head && front == s);
            // a code at one end; at both only a single letter before and one or two after ("F Boss BD"); a lone vowel is no code
            if (head && tail && (head != 1 || tail > 2)) continue;
            if (tail == 1 && std::strchr("aeiou", back[0])) continue;
            int vowels = 0;									// a code of three letters is a cluster, not a syllable ("ura")
            for (char c : front + back) vowels += std::strchr("aeiou", c) != nullptr;
            if ((head == 3 || tail == 3) && vowels > 1) continue;
            if (!plain || (head && Known(front)) || (tail && Known(back))) continue;
            std::vector<size_t> mid;
            if (!KnownWords(middle, 1, &mid, true)) continue;
            if (head) out->push_back(Upper(p.substr(0, head)));
            size_t at = 0;
            for (size_t c : mid) { out->push_back(p.substr(head + at, c - at)); at = c; }
            out->push_back(p.substr(head + at, middle.size() - at));
            if (tail) out->push_back(Upper(p.substr(p.size() - tail)));
            return true;
        }
    return false;
}
// One word's pieces: camel case split ("LeftArm"), and a trailing counter split off a longer word ("craft05" -> "craft", "5").
void Pieces(const std::string& w, std::vector<std::string>* out) {
    std::string piece;
    for (size_t i = 0; i < w.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(w[i]);
        if (i && std::isupper(c) && std::islower(static_cast<unsigned char>(w[i - 1]))) { out->push_back(piece); piece.clear(); }
        piece += w[i];
    }
    if (piece.empty()) return;
    size_t letters = 0;
    while (letters < piece.size() && std::isalpha(static_cast<unsigned char>(piece[letters]))) ++letters;
    bool digits = letters < piece.size();
    for (size_t i = letters; i < piece.size(); ++i) digits = digits && std::isdigit(static_cast<unsigned char>(piece[i]));
    if (digits && letters >= 4) {
        out->push_back(piece.substr(0, letters));
        std::string number = piece.substr(letters);
        while (number.size() > 1 && number[0] == '0') number.erase(0, 1);
        out->push_back(number);
        return;
    }
    out->push_back(piece);
}
// A name made by rule, as the game writes its own titles: words in title case (the small ones lower), designations in
// capitals ("AV-14"), a hyphen kept only beside a letter or two ("Gill-M"), run-together words split by the vocabulary.
std::string Tidy(const std::string& raw) {
    static const char* const small[] = {"a", "an", "and", "at", "for", "in", "of", "on", "the", "to"};
    std::vector<std::string> words;
    std::string token;
    auto flush = [&]() {
        if (token.empty()) return;
        // hyphenated parts: joined when one side is a letter or two, else separate words
        std::vector<std::string> parts;
        size_t at = 0;
        while (true) { const size_t dash = token.find('-', at); parts.push_back(token.substr(at, dash == std::string::npos ? std::string::npos : dash - at)); if (dash == std::string::npos) break; at = dash + 1; }
        std::string joined;
        bool joined_short = false;	// the word so far already took a short part ("gill-m")
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i].empty()) continue;
            const bool short_part = parts[i].size() <= 2 && AllAlpha(parts[i]);
            if (!joined.empty() && short_part && !joined_short) { joined += "-" + parts[i]; joined_short = true; continue; }
            if (!joined.empty()) words.push_back(joined);
            joined = parts[i];
            joined_short = false;
        }
        if (!joined.empty()) words.push_back(joined);
        token.clear();
    };
    for (char c : raw) { if (c == ' ' || c == '_') flush(); else token += c; }
    flush();
    std::vector<std::string> split;
    for (const std::string& w : words) {
        if (w.find('-') != std::string::npos) { split.push_back(w); continue; }
        std::vector<std::string> pieces;
        Pieces(w, &pieces);
        for (const std::string& p : pieces) {
            std::vector<std::string> shown;
            if (SplitWord(p, &shown)) split.insert(split.end(), shown.begin(), shown.end());
            else split.push_back(p);
        }
    }
    std::string out;
    for (size_t i = 0; i < split.size(); ++i) {
        std::string w = Word(split[i].c_str(), split[i].size());
        bool lower = false;
        for (const char* s : small) lower = lower || (i && Lower(w) == s && w.size() > 1 && w != Upper(w));	// "AT", a code, stays
        if (lower) w = Lower(w);
        else {
            // each hyphenated part capitalised ("Gill-M"); the rest of the word as written
            for (size_t k = 0; k < w.size(); ++k)
                if (k == 0 || w[k - 1] == '-') w[k] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[k])));
        }
        if (!out.empty()) out += ' ';
        out += w;
    }
    return out;
}
std::string Join(const std::vector<std::string>& words, size_t from) {
    std::string raw;
    for (size_t i = from; i < words.size(); ++i) { if (!raw.empty()) raw += ' '; raw += words[i]; }
    return Tidy(raw);
}
// A goodies subfolder: "ca_f_units" -> "Forseti units", "ca_be_aquila" -> "Battle Engine aquila" -> "Battle Engine Aquila".
std::string Subfolder(const std::string& part) {
    std::vector<std::string> words = Words(part);
    size_t from = (!words.empty() && SameWord(words[0].c_str(), words[0].size(), "ca")) ? 1 : 0;
    std::string out;
    for (size_t i = from; i < words.size(); ++i) {
        const std::string& w = words[i];
        if (w.size() >= 2 && (w[0] == 'v' || w[0] == 'V') && std::isdigit(static_cast<unsigned char>(w[1]))) continue;	// a version folder
        std::string shown = Word(w.c_str(), w.size());
        for (const SPrefix& side : kSideWords) if (SameWord(w.c_str(), w.size(), side.code)) shown = side.group;
        if (!out.empty()) out += ' ';
        out += shown;
    }
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return Tidy(out);
}
// A texture file's stem: no ".aya", no "(0)A8R8G8B8", no ".tga", no "mustbe_" (the game's mark for textures it keeps).
std::string TextureStem(const char* file) {
    std::string s = file ? file : "";
    if (s.size() > 4 && SameWord(s.c_str() + s.size() - 4, 4, ".aya")) s.resize(s.size() - 4);
    const size_t open = s.rfind('(');
    if (open != std::string::npos && s.find(')', open) != std::string::npos) s.resize(open);
    if (s.size() > 4 && (SameWord(s.c_str() + s.size() - 4, 4, ".tga") || SameWord(s.c_str() + s.size() - 4, 4, ".bmp"))) s.resize(s.size() - 4);
    if (s.size() > 7 && SameWord(s.c_str(), 7, "mustbe_")) s = s.substr(7);
    return s;
}
void AssetName(int kind, const char* file, bool interface_texture, std::string* group, std::string* name) {
    if (kind == 0) {
        // "m_" marks every model file; then the makers' side and kind prefix, if any
        std::string s = file ? file : "";
        if (s.size() > 8 && SameWord(s.c_str() + s.size() - 8, 8, ".msh.aya")) s.resize(s.size() - 8);
        if (s.size() > 2 && SameWord(s.c_str(), 2, "m_")) s = s.substr(2);
        std::vector<std::string> words = Words(s);
        *group = "Models";
        size_t from = 0;
        if (!words.empty()) {
            std::string first = words[0];
            const size_t dash = first.find('-');
            const std::string code = dash == std::string::npos ? first : first.substr(0, dash);
            for (const SPrefix& p : kModelPrefixes)
                if (SameWord(code.c_str(), code.size(), p.code)) {
                    *group = p.group;
                    if (dash == std::string::npos) from = 1;
                    else words[0] = first.substr(dash + 1);
                    break;
                }
        }
        sModelCodes = true;
        *name = Join(words, from);
        if (name->empty()) *name = Join(words, 0);
        sModelCodes = false;
        return;
    }
    std::string stem = TextureStem(file);
    std::vector<std::string> parts;
    size_t at = 0;
    while (true) {
        const size_t sep = stem.find('%', at);
        parts.push_back(stem.substr(at, sep == std::string::npos ? std::string::npos : sep - at));
        if (sep == std::string::npos) break;
        at = sep + 1;
    }
    *group = interface_texture ? "Interface" : "Textures";
    size_t first = 0;
    if (parts.size() > 1) {
        *group = Join(Words(parts[0]), 0);
        for (const SPrefix& f : kTextureFolders) if (SameWord(parts[0].c_str(), parts[0].size(), f.code)) *group = f.group;
        first = 1;
    }
    std::string sub;
    for (size_t i = first; i + 1 < parts.size(); ++i) {
        const std::string s = Subfolder(parts[i]);
        if (s.empty()) continue;
        if (!sub.empty()) sub += ", ";
        sub += s;
    }
    std::vector<std::string> words = Words(parts.back());
    size_t from = 0;
    // a goodies picture's own codes repeat its folder ("ca_fu_tank_av14" in "ca_f_units"): leave them out
    if (!sub.empty()) {
        static const char* const codes[] = {"ca", "fu", "mu", "fc", "mc", "fa", "ma", "fl", "ml", "be", "bea", "boss", "loc"};
        while (from + 1 < words.size()) {
            bool code = false;
            for (const char* c : codes) code = code || SameWord(words[from].c_str(), words[from].size(), c);
            if (!code) break;
            ++from;
        }
    }
    const std::string leaf = Join(words, from);
    auto SameSingular = [](const std::string& a, const std::string& b) {
        if (a.size() == b.size() && SameWord(a.c_str(), a.size(), b.c_str())) return true;
        const std::string& longer = a.size() > b.size() ? a : b;
        const std::string& shorter = a.size() > b.size() ? b : a;
        if (longer.size() == shorter.size() + 1 && (longer.back() == 's' || longer.back() == 'S') &&
            SameWord(longer.c_str(), shorter.size(), shorter.c_str())) return true;
        if (longer.size() == shorter.size() + 2 && SameWord(longer.c_str() + shorter.size(), 2, "es") &&
            SameWord(longer.c_str(), shorter.size(), shorter.c_str())) return true;
        return false;
    };
    if (!sub.empty() && !leaf.empty() && SameSingular(leaf, sub)) { *name = leaf; return; }
    *name = sub.empty() ? leaf : leaf.empty() ? sub : sub + ", " + leaf;
    if (name->empty()) *name = leaf.empty() ? std::string("Unnamed item") : leaf;
}
// "Group: name" for a row, and the parts for the details; the game's own title for a goodie wins over the made name.
void AssetTitle(size_t i, std::string* group, std::string* name) {
    const char* path = PortCeAssetCatalogPath(catalog, i);
    const char* base = path ? std::strrchr(path, '/') : nullptr;
    const char* file = base ? base + 1 : path;
    const char* catalog_group = PortCeAssetCatalogGroup(catalog, i);
    const int kind = PortCeAssetCatalogKind(catalog, i) == CE_ASSET_TEXTURE ? 1 : 0;
    AssetName(kind, file, catalog_group && !std::strcmp(catalog_group, "Interface textures"), group, name);
    char words[160];
    if (file && PortCeCatalogueGoodieName(kind, file, words, sizeof words)) {
        char shown[160]; Name(words, shown, sizeof shown);
        *name = shown;
        if (kind == 0) *group = "Units";		// a goodie's model is one of the game's units, whatever its file's prefix
    }
    char clean[200]; Name(name->c_str(), clean, sizeof clean); *name = clean;
}
std::string AssetRow(size_t i) {
    std::string group, name;
    AssetTitle(i, &group, &name);
    return group + ": " + name;
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
    if (folder.empty()) return Tidy(words);
    char group[CE_CATALOGUE_SOUND_NAME_MAX + 1]; Name(folder.c_str(), group, sizeof group);
    return Tidy(group) + ": " + Tidy(words);
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
    vocabulary.clear();
    text_words.clear();
    PortCeCatalogueTextWords(AddTextWord, nullptr);
    for (size_t i = 0; i < PortCeAssetCatalogCount(catalog); ++i) {
        const int kind = PortCeAssetCatalogKind(catalog, i);
        if (kind != CE_ASSET_MODEL && kind != CE_ASSET_TEXTURE) continue;
        const char* path = PortCeAssetCatalogPath(catalog, i);
        const char* base = path ? std::strrchr(path, '/') : nullptr;
        std::string word;
        for (const char* p = base ? base + 1 : (path ? path : ""); ; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c && std::isalpha(c) && !(std::isupper(c) && !word.empty() && std::islower(static_cast<unsigned char>(word.back())))) { word += static_cast<char>(c); continue; }
            if (word.size() >= 3) vocabulary.insert(Lower(word));
            word.clear();
            if (!c) break;
            if (std::isalpha(c)) word += static_cast<char>(c);
        }
    }
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
    else if (SpeechCategory(category)) Name(SpeechTitle(i), out, size);
    else std::snprintf(out, size, "%s", AssetRow(i).c_str());
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
    // the name, its group and place; for a texture its size, and its format on the last line
    std::string group, name;
    AssetTitle(i, &group, &name);
    const unsigned place = collection[category].page * CE_CATALOGUE_SLOTS + slot + 1;
    if (category) {
        unsigned w = 0, h = 0; bool alpha = false;
        char size_line[96];
        if (PortCeCatalogueTextureSize(slot, &w, &h, &alpha))
            std::snprintf(size_line, sizeof size_line, "%u x %u pixels%s", w, h, alpha ? ", with transparency" : "");
        else std::snprintf(size_line, sizeof size_line, "Its size could not be read.");
        std::snprintf(out, size, "%s\n%s, item %u of %u.\n%s", name.c_str(), group.c_str(), place, PortCeCatalogueMatches(), size_line);
        return;
    }
    std::snprintf(out, size, "%s\n%s, item %u of %u.", name.c_str(), group.c_str(), place, PortCeCatalogueMatches());
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
void PortCeCatalogueItemParts(unsigned slot, char* group, unsigned group_size, char* name, unsigned name_size) {
    if (group_size) group[0] = 0;
    if (name_size) name[0] = 0;
    const size_t i = Entry(slot);
    if (i == static_cast<size_t>(-1) || !group_size || !name_size) return;
    if (category == 2) {
        // an effect's "Group: words"
        const std::string title = EffectTitle(SoundName(i));
        const size_t colon = title.find(": ");
        if (colon == std::string::npos) std::snprintf(name, name_size, "%s", title.c_str());
        else { std::snprintf(group, group_size, "%s", title.substr(0, colon).c_str()); std::snprintf(name, name_size, "%s", title.substr(colon + 2).c_str()); }
        return;
    }
    if (SpeechCategory(category)) { Name(SpeechTitle(i), name, name_size); return; }
    std::string g, n;
    AssetTitle(i, &g, &n);
    std::snprintf(group, group_size, "%s", g.c_str());
    std::snprintf(name, name_size, "%s", n.c_str());
}
int PortCeCatalogueModelGoodie(unsigned slot) {
    const size_t i = Entry(slot);
    if (category != 0 || i == static_cast<size_t>(-1)) return -1;
    const char* path = PortCeAssetCatalogPath(catalog, i);
    const char* base = path ? std::strrchr(path, '/') : nullptr;
    return PortCeCatalogueGoodieModel(base ? base + 1 : path);
}
bool PortCeCatalogueTextureSize(unsigned slot, unsigned* width, unsigned* height, bool* transparency) {
    const size_t i = Entry(slot);
    CeAssetTextureHeader header;
    if (category != 1 || i == static_cast<size_t>(-1) || !PortCeAssetCatalogTextureHeader(catalog, i, &header)) return false;
    *width = header.width; *height = header.height;
    // formats that keep an alpha channel: the "A..." pixel layouts and DXT2 to DXT5
    const char* f = header.format;
    *transparency = f[0] == 'A' || (!std::strncmp(f, "DXT", 3) && f[3] >= '2' && f[3] <= '5');
    return true;
}
bool PortCeCatalogueBrowsingSounds() { return opened && (category == 2 || SpeechCategory(category)); }
#endif
