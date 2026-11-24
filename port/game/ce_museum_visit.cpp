// SPDX-License-Identifier: GPL-3.0-only
#include "ce_network_stl.h"
#include "ce_museum_visit.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "../updater/sha256.h"
int uncompress(unsigned char *dest, unsigned long *destLen, const unsigned char *source, unsigned long sourceLen);	// zlib, linked for the game's unpacking
}

const char *PortUserDir();
const char *PortGameDir();

namespace {
const size_t kBodyLimit = 16u * 1024u * 1024u;
const size_t kArchiveLimit = 512u * 1024u * 1024u;
struct ReadHandle {
    FILE *value;
    explicit ReadHandle(FILE *f) : value(f) {}
    ~ReadHandle() { if (value) std::fclose(value); }
    FILE *Release() { FILE *f = value; value = NULL; return f; }
};
struct Body {
    unsigned id;
    size_t expectedSize;
    std::string hash, logical;
    std::vector<unsigned char> bytes;
    Body() : id(0), expectedSize(0) {}
};
struct Visit {
    bool active, loaded, ended, failed, installed, generated;
    int level;
    unsigned readMask;
    std::string archivePath, archiveHash;
    Body base, real;
    Visit() : active(false), loaded(false), ended(false), failed(false), installed(false), generated(false), level(-1), readMask(0) {}
};
Visit gVisit;
const char *gMessage = "The museum could not be opened. Your careers are unchanged.";
// What a load says when the generator refused without a reason of its own, and what the Onslaught CE page says for a load that failed elsewhere.
const char kOpenFailed[] = "The museum could not be opened. Your careers are unchanged.";
// What a refused load tells the player on the Onslaught CE page, once, after the front end took the visit's return (a literal, or NULL).
const char *gNotice = NULL;
bool gReturned = false;

std::string Join(const char *root, const std::string &tail) {
    if (!root || !*root) return std::string();
    return std::string(root) + "/" + tail;
}
std::string WorldName(unsigned id) {
    char b[64]; std::snprintf(b, sizeof b, "data/worlds/world%03u.wrd", id);
    return b;
}
std::string BodyName(unsigned id) {
    char b[64]; std::snprintf(b, sizeof b, "museum/world%03u.body", id);
    return b;
}
std::string ArchiveName(unsigned id) {
    char b[80]; std::snprintf(b, sizeof b, "data/resources/%03u_res_PC.aya", id);
    return b;
}
FILE *OpenRead(const std::string &path) {
    if (path.empty()) return NULL;
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, NULL, 0);
    if (count <= 0) return NULL;
    std::vector<wchar_t> wide(static_cast<size_t>(count));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, wide.data(), count)) return NULL;
    return _wfopen(wide.data(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}
bool ReadBounded(const std::string &path, size_t limit,
                 std::vector<unsigned char> *bytes, std::string *hash, size_t *readSize = NULL) {
    FILE *f = OpenRead(path);
    if (!f) return false;
    ReadHandle owned(f);
    SHA256_CTX sha; sha256_init(&sha);
    unsigned char block[32768]; size_t total = 0;
    bool ok = true;
    for (;;) {
        size_t n = std::fread(block, 1, sizeof block, f);
        if (n > limit - total) { ok = false; break; }
        if (n) {
            total += n; sha256_update(&sha, block, n);
            if (bytes) bytes->insert(bytes->end(), block, block + n);
        }
        if (n != sizeof block) { if (std::ferror(f)) ok = false; break; }
    }
    if (std::fclose(owned.Release())) ok = false;
    if (!ok) return false;
    if (readSize) *readSize = total;
    unsigned char digest[32]; sha256_final(&sha, digest);
    if (hash) {
        static const char hex[] = "0123456789abcdef";
        hash->clear(); hash->reserve(64);
        for (unsigned i = 0; i != 32; ++i) {
            hash->push_back(hex[digest[i] >> 4]); hash->push_back(hex[digest[i] & 15]);
        }
    }
    return true;
}
// What the original's reader needs of a packed level archive (CDXMemBuffer::ReadBlock): a run of blocks, each a four-byte length and that many bytes of
// zlib data that unpack to at most one block of the reader's buffer, and no byte after the last. A damaged or cut archive fails the original's own read
// with a fatal error that ends the game, in the middle of a load nothing can step back from, so the museum looks first, while the front end can still
// say why. The whole archive is unpacked and thrown away: the cost is a few hundred milliseconds on the largest level.
const unsigned long kPackBuffer = 0x102927ul, kBlock = 0x100000ul;
bool PackedArchiveReadable(const std::string &path) {
    FILE *f = OpenRead(path);
    if (!f) return false;
    ReadHandle owned(f);
    std::vector<unsigned char> packed(kPackBuffer), unpacked(kPackBuffer);
    unsigned blocks = 0;
    for (;;) {
        unsigned char head[4];
        const size_t got = std::fread(head, 1, sizeof head, f);
        if (got == 0 && !std::ferror(f)) break;			// a clean end, after at least one block
        if (got != sizeof head) return false;
        const unsigned long length = head[0] | head[1] << 8 | head[2] << 16 | static_cast<unsigned long>(head[3]) << 24;
        if (!length || length > kPackBuffer) return false;
        if (std::fread(packed.data(), 1, length, f) != length) return false;
        unsigned long size = kPackBuffer;
        if (uncompress(unpacked.data(), &size, packed.data(), length) != 0 || !size || size > kBlock) return false;
        ++blocks;
    }
    return blocks != 0;
}
bool HashText(const std::string &text) {
    if (text.size() != 64) return false;
    for (char c : text) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
bool Number(const std::string &text, size_t limit, size_t *value) {
    if (text.empty() || text.size() > 10) return false;
    size_t n = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || n > (limit - static_cast<unsigned>(c - '0')) / 10) return false;
        n = n * 10 + static_cast<unsigned>(c - '0');
    }
    *value = n; return true;
}
bool Row(const std::string &line, const char *key, Body *body, bool sized) {
    std::istringstream in(line); std::string tag, id, hash, length, extra;
    if (!(in >> tag >> id >> hash) || tag != key || !HashText(hash)) return false;
    if (sized && !(in >> length)) return false;
    if (in >> extra) return false;
    size_t number = 0, count = 0;
    if (!Number(id, 999, &number)) return false;
    if (sized && (!Number(length, kBodyLimit, &count) || count == 0)) return false;
    body->id = static_cast<unsigned>(number); body->hash = hash;
    body->expectedSize = count; body->logical = WorldName(body->id);
    return true;
}
bool PrepareData(Visit *visit) {
    std::vector<unsigned char> manifest;
    if (!ReadBounded(Join(PortUserDir(), "museum/room.cache"), 512, &manifest, NULL)) {
        gMessage = "The museum could not read your game files. Check that the game is installed, then try again."; return false;
    }
    for (unsigned char c : manifest) if (c != '\n' && (c < 32 || c > 126)) {
        gMessage = "The museum could not read your game files. Check that the game is installed, then try again."; return false;
    }
    std::istringstream input(std::string(manifest.begin(), manifest.end()));
    std::string version, carrierLine, baseLine, realLine, extra;
    Body carrier;
    if (!std::getline(input, version) || version != "Onslaught CE museum cache 1" ||
        !std::getline(input, carrierLine) || !std::getline(input, baseLine) || !std::getline(input, realLine) ||
        std::getline(input, extra) || !Row(carrierLine, "carrier", &carrier, false) ||
        !Row(baseLine, "base", &visit->base, true) || !Row(realLine, "real", &visit->real, true) ||
        carrier.id >= 850 || visit->real.id != carrier.id || visit->base.id == visit->real.id) {
        gMessage = "The museum could not read your game files. Check that the game is installed, then try again."; return false;
    }
    std::string actual;
    if (!ReadBounded(Join(PortGameDir(), ArchiveName(carrier.id)), kArchiveLimit, NULL, &actual) || actual != carrier.hash) {
        gMessage = "The museum could not read your game files. Check that the game is installed, then try again."; return false;
    }
    for (Body *body : {&visit->base, &visit->real}) {
        if (!ReadBounded(Join(PortUserDir(), BodyName(body->id)), kBodyLimit, &body->bytes, &actual) ||
            body->bytes.size() != body->expectedSize || actual != body->hash) {
            gMessage = "The museum could not read your game files. Check that the game is installed, then try again."; return false;
        }
    }
    visit->level = static_cast<int>(carrier.id);
    return true;
}
std::string Canonical(const char *path) {
    if (!path) return std::string();
    size_t n = 0; while (path[n] && n <= 80) ++n;
    if (n > 80) return std::string();
    std::string s(path, n);
    for (char &c : s) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return s;
}
}

bool CeMuseumVisit::Prepare() {
#ifndef CE_VERSION
    gMessage = ""; return false;
#else
    if (gVisit.active) { gMessage = ""; return false; }
    gVisit = Visit();
    Visit candidate;
    try {
        if (!PrepareData(&candidate)) return false;
        gVisit = std::move(candidate);
    } catch (...) { gMessage = kOpenFailed; return false; }
    return true;
#endif
}
bool CeMuseumVisit::Activate() {
    if (gVisit.active || gVisit.level < 0 ||
        (!gVisit.installed && (gVisit.base.bytes.empty() || gVisit.real.bytes.empty()))) return false;
    gVisit.active = true; return true;
}
bool CeMuseumVisit::PrepareInstalled(unsigned carrier) {
#ifndef CE_VERSION
    (void)carrier;
    gMessage = ""; return false;
#else
    if (gVisit.active) { gMessage = ""; return false; }
    gVisit = Visit();
    if (carrier >= 850) { gMessage = ""; return false; }
    try {
        Visit candidate;
        candidate.archivePath = Join(PortGameDir(), ArchiveName(carrier));
        size_t bytes = 0;
        if (!ReadBounded(candidate.archivePath, kArchiveLimit, NULL, &candidate.archiveHash, &bytes) || !bytes) {
            gMessage = "The museum could not read your game files. Check that the game is installed, then try again."; return false;
        }
        if (!PackedArchiveReadable(candidate.archivePath)) {
            gMessage = "The museum could not read your game files. Check that the game is installed, then try again."; return false;
        }
        candidate.level = static_cast<int>(carrier);
        candidate.installed = true;
        gVisit = std::move(candidate);
        return true;
    } catch (...) {
        gMessage = kOpenFailed; return false;
    }
#endif
}
bool CeMuseumVisit::CheckInstalledSource() {
    if (!gVisit.active || !gVisit.installed || gVisit.failed || gVisit.ended) return false;
    try {
        std::string actual;
        if (ReadBounded(gVisit.archivePath, kArchiveLimit, NULL, &actual) && actual == gVisit.archiveHash) return true;
    } catch (...) {}
    gMessage = "Your game files changed while the museum was opening. Try again.";
    return false;
}
bool CeMuseumVisit::AcceptInstalled(unsigned baseId, const void *base, unsigned baseLength,
                                   const void *real, unsigned realLength) {
    if (!gVisit.active || !gVisit.installed || gVisit.generated || gVisit.readMask ||
        gVisit.failed || gVisit.ended || baseId > 999 || baseId == static_cast<unsigned>(gVisit.level) ||
        !base || !real || !baseLength || !realLength || baseLength > kBodyLimit || realLength > kBodyLimit) return false;
    try {
        Body baseBody, realBody;
        baseBody.id = baseId; realBody.id = static_cast<unsigned>(gVisit.level);
        baseBody.logical = WorldName(baseBody.id); realBody.logical = WorldName(realBody.id);
        const unsigned char *a = static_cast<const unsigned char *>(base), *b = static_cast<const unsigned char *>(real);
        baseBody.bytes.assign(a, a + baseLength); realBody.bytes.assign(b, b + realLength);
        if (!CheckInstalledSource()) return false;
        gVisit.base = std::move(baseBody); gVisit.real = std::move(realBody);
        gVisit.generated = true;
        return true;
    } catch (...) {
        gMessage = kOpenFailed;
        return false;
    }
}
void CeMuseumVisit::Clear() { gVisit = Visit(); }
bool CeMuseumVisit::ForcedRefusal(const char *which) {
    if (!gVisit.active || !which) return false;
    const char *forced = std::getenv("BEA_PORT_MUSEUM_REFUSE");
    return forced && !std::strcmp(forced, which);
}
void CeMuseumVisit::MarkReturned() { gReturned = true; }
void CeMuseumVisit::Refuse(const char *message) { gMessage = message; }
extern "C" bool PortCeMuseumVisitActive(void) { return gVisit.active; }
extern "C" bool PortCeMuseumAuthoredSource(void) { return gVisit.active && gVisit.installed; }
extern "C" int PortCeMuseumVisitLevel(void) { return gVisit.active ? gVisit.level : -1; }
extern "C" const char *PortCeMuseumVisitMessage(void) { return gMessage; }
extern "C" int PortCeMuseumVisitReadWorld(const char *name, const void **bytes, unsigned *length) {
    if (bytes) *bytes = NULL;
    if (length) *length = 0;
    if (!gVisit.active) return 0;
    // The pause menu's Retry restarts the level without leaving it, so after a visit that loaded cleanly and then ended the same pair
    // is asked for again. A visit that failed (the latch below) and any other world (the name check) stay refused.
    if (gVisit.ended && gVisit.loaded) { gVisit.ended = false; gVisit.loaded = false; gVisit.readMask = 0; }
    if (!bytes || !length || gVisit.ended || gVisit.failed) {
        gVisit.failed = true; gMessage = kOpenFailed; return -1;
    }
    try {
        if (gVisit.installed && !gVisit.generated && (gMessage = kOpenFailed, !CeMuseumVisit::GenerateInstalled())) {
            gVisit.failed = true;
            return -1;
        }
        const std::string path = Canonical(name);
        Body *body = path == gVisit.base.logical ? &gVisit.base : path == gVisit.real.logical ? &gVisit.real : NULL;
        if (!body || body->bytes.empty() || body->bytes.size() > kBodyLimit || CeMuseumVisit::ForcedRefusal("world")) {
            gVisit.failed = true;
            gMessage = kOpenFailed; return -1;
        }
        *bytes = body->bytes.data(); *length = static_cast<unsigned>(body->bytes.size());
        gVisit.readMask |= body == &gVisit.base ? 1u : 2u;
        return 1;
    } catch (...) {
        gVisit.failed = true; gMessage = kOpenFailed; return -1;
    }
}
extern "C" bool PortCeMuseumVisitAfterLoad(void) {
    if (!gVisit.active) return true;
    if (gVisit.readMask != 3 || gVisit.ended || gVisit.failed || CeMuseumVisit::ForcedRefusal("afterload")) {
        gVisit.failed = true; gMessage = kOpenFailed; return false;
    }
#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
    CeMuseumVisit::ApplyAuthoredIdentity();
#endif
    gVisit.loaded = true; return true;
}
extern "C" void PortCeMuseumVisitLevelEnd(void) {
    const bool wasActive = gVisit.active;
    if (gVisit.active) gVisit.ended = true;
#if defined(CE_VERSION) && !defined(CE_MUSEUM_VISIT_HOST_FIXTURE)
    if (wasActive) CeMuseumVisit::ReleaseRuntimeResources();
#else
    (void)wasActive;
#endif
}

extern "C" bool PortCeMuseumVisitLoadRefused(void) {
    if (!gVisit.active) return false;
    // A load that failed on a refusal already says why (gMessage); any other failed load says the general sentence.
    gNotice = gVisit.failed ? gMessage : kOpenFailed;
    gVisit.failed = true;
    return true;
}
extern "C" bool PortCeMuseumVisitTakeNotice(const char **sentence) {
    if (sentence) *sentence = NULL;
    if (!gReturned) return false;
    gReturned = false;
    if (sentence) *sentence = gNotice;
    gNotice = NULL;
    return true;
}
