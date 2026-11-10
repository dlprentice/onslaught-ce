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
const char *gMessage = "The museum room is not prepared yet.";

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
        gMessage = "The museum room is not prepared yet."; return false;
    }
    for (unsigned char c : manifest) if (c != '\n' && (c < 32 || c > 126)) {
        gMessage = "The museum room description is invalid."; return false;
    }
    std::istringstream input(std::string(manifest.begin(), manifest.end()));
    std::string version, carrierLine, baseLine, realLine, extra;
    Body carrier;
    if (!std::getline(input, version) || version != "Onslaught CE museum cache 1" ||
        !std::getline(input, carrierLine) || !std::getline(input, baseLine) || !std::getline(input, realLine) ||
        std::getline(input, extra) || !Row(carrierLine, "carrier", &carrier, false) ||
        !Row(baseLine, "base", &visit->base, true) || !Row(realLine, "real", &visit->real, true) ||
        carrier.id >= 850 || visit->real.id != carrier.id || visit->base.id == visit->real.id) {
        gMessage = "The museum room description is invalid."; return false;
    }
    std::string actual;
    if (!ReadBounded(Join(PortGameDir(), ArchiveName(carrier.id)), kArchiveLimit, NULL, &actual) || actual != carrier.hash) {
        gMessage = "The museum room was prepared for different game resources."; return false;
    }
    for (Body *body : {&visit->base, &visit->real}) {
        if (!ReadBounded(Join(PortUserDir(), BodyName(body->id)), kBodyLimit, &body->bytes, &actual) ||
            body->bytes.size() != body->expectedSize || actual != body->hash) {
            gMessage = "The museum room data is missing or has changed. Prepare the room again."; return false;
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
    gMessage = "The museum room is unavailable in this build."; return false;
#else
    if (gVisit.active) { gMessage = "The museum room is already opening."; return false; }
    gVisit = Visit();
    Visit candidate;
    try {
        if (!PrepareData(&candidate)) return false;
        gVisit = std::move(candidate);
    } catch (...) { gMessage = "The museum room could not be prepared."; return false; }
    gMessage = "Museum construction test: existing terrain and static exhibits.";
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
    gMessage = "The museum room is unavailable in this build."; return false;
#else
    if (gVisit.active) { gMessage = "The museum room is already opening."; return false; }
    gVisit = Visit();
    if (carrier >= 850) { gMessage = "The museum carrier is unsupported."; return false; }
    try {
        Visit candidate;
        candidate.archivePath = Join(PortGameDir(), ArchiveName(carrier));
        size_t bytes = 0;
        if (!ReadBounded(candidate.archivePath, kArchiveLimit, NULL, &candidate.archiveHash, &bytes) || !bytes) {
            gMessage = "The museum could not read its installed game resources."; return false;
        }
        candidate.level = static_cast<int>(carrier);
        candidate.installed = true;
        gVisit = std::move(candidate);
        gMessage = "Preparing a museum visit from your installed game resources.";
        return true;
    } catch (...) {
        gMessage = "The museum could not prepare its installed resources."; return false;
    }
#endif
}
bool CeMuseumVisit::CheckInstalledSource() {
    if (!gVisit.active || !gVisit.installed || gVisit.failed || gVisit.ended) return false;
    try {
        std::string actual;
        if (ReadBounded(gVisit.archivePath, kArchiveLimit, NULL, &actual) && actual == gVisit.archiveHash) return true;
    } catch (...) {}
    gMessage = "The installed museum resources changed while opening the room.";
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
        gMessage = "Museum prototype: explore the installed static exhibits; exit returns without campaign progress.";
        return true;
    } catch (...) {
        gMessage = "The museum room could not retain its generated worlds.";
        return false;
    }
}
void CeMuseumVisit::Clear() { gVisit = Visit(); }
void CeMuseumVisit::Refuse(const char *message) { gMessage = message; }
extern "C" bool PortCeMuseumVisitActive(void) { return gVisit.active; }
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
        gVisit.failed = true; gMessage = "The museum room cannot load another world."; return -1;
    }
    try {
        if (gVisit.installed && !gVisit.generated && !CeMuseumVisit::GenerateInstalled()) {
            gVisit.failed = true;
            return -1;
        }
        const std::string path = Canonical(name);
        Body *body = path == gVisit.base.logical ? &gVisit.base : path == gVisit.real.logical ? &gVisit.real : NULL;
        if (!body || body->bytes.empty() || body->bytes.size() > kBodyLimit) {
            gVisit.failed = true;
            gMessage = "The museum room requested an unexpected world. The campaign was not loaded."; return -1;
        }
        *bytes = body->bytes.data(); *length = static_cast<unsigned>(body->bytes.size());
        gVisit.readMask |= body == &gVisit.base ? 1u : 2u;
        return 1;
    } catch (...) {
        gVisit.failed = true; gMessage = "The museum world could not be read."; return -1;
    }
}
extern "C" bool PortCeMuseumVisitAfterLoad(void) {
    if (!gVisit.active) return true;
    if (gVisit.readMask != 3 || gVisit.ended || gVisit.failed) {
        gVisit.failed = true; gMessage = "The museum room did not load both prepared worlds."; return false;
    }
    gVisit.loaded = true; return true;
}
extern "C" void PortCeMuseumVisitLevelEnd(void) { if (gVisit.active) gVisit.ended = true; }
