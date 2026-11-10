// SPDX-License-Identifier: GPL-3.0-only
// Authored data and career stand-ins. No installed game content is used.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
static void Need(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); }
}

#if defined(CE_MUSEUM_MESH_FIXTURE)
#include <strings.h>
#define stricmp strcasecmp
class CMesh {
public:
    char mName[32];
    CMesh *mNextMesh;
    bool available;
    static CMesh *FindLoadedUnique(const char *);
};
static CMesh *sFirst;
#include "museum_mesh_query.inc"
int main() {
    CMesh a = {}, b = {}, c = {};
    std::strcpy(a.mName, "authored.one");
    std::strcpy(b.mName, "authored.two");
    std::strcpy(c.mName, "AUTHORED.ONE");
    a.available = true; b.available = true; c.available = false;
    Need(!CMesh::FindLoadedUnique(NULL) && !CMesh::FindLoadedUnique(""), "missing names refused");
    Need(!CMesh::FindLoadedUnique("authored.one"), "empty registry has no match");
    sFirst = &a; a.mNextMesh = &b;
    Need(CMesh::FindLoadedUnique("AUTHORED.ONE") == &a, "unique existing record returned");
    Need(CMesh::FindLoadedUnique("authored.two") == &b, "later unique record returned");
    Need(!CMesh::FindLoadedUnique("authored.absent"), "missing name never loads a file");
    b.mNextMesh = &c;
    Need(!CMesh::FindLoadedUnique("authored.one"), "unavailable duplicate still makes identity ambiguous");
    Need(CMesh::FindLoadedUnique("authored.two") == &b, "other identity remains unique");
    c.mNextMesh = &a;
    Need(!CMesh::FindLoadedUnique("authored.absent"), "cyclic registry is bounded");
    c.mNextMesh = NULL; std::memset(c.mName, 'x', sizeof c.mName);
    Need(!CMesh::FindLoadedUnique("authored.two"), "unterminated loaded identity fails closed");
    std::puts("PASS loaded identity uniqueness, ambiguity and bounded lookup");
    return 0;
}
#elif defined(CE_MUSEUM_GENERATOR_FIXTURE)
#include "ce_museum_visit.h"
#include <limits>
#include <new>
static long allocationLimit = -1;
void *operator new(std::size_t size) {
    if (allocationLimit == 0) throw std::bad_alloc();
    if (allocationLimit > 0) --allocationLimit;
    void *result = std::malloc(size ? size : 1);
    if (!result) throw std::bad_alloc();
    return result;
}
void operator delete(void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
struct CapturedRoom {
    unsigned calls = 0;
    bool refuse = false;
    std::vector<unsigned char> base, real;
};
static bool CaptureRoom(const void *base, unsigned baseLength, const void *real, unsigned realLength, void *opaque) {
    CapturedRoom &result = *static_cast<CapturedRoom *>(opaque);
    ++result.calls;
    Need(base && baseLength && real && realLength, "both complete bodies reach receiver together");
    if (result.refuse) return false;
    const unsigned char *a = static_cast<const unsigned char *>(base);
    const unsigned char *b = static_cast<const unsigned char *>(real);
    std::vector<unsigned char> baseCopy(a, a + baseLength), realCopy(b, b + realLength);
    result.base.swap(baseCopy); result.real.swap(realCopy);
    return true;
}
static void WriteBody(const std::string &path, const std::vector<unsigned char> &bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    Need(static_cast<bool>(out), "authored body output written");
}
struct PrefixInput {
    const std::vector<unsigned char> *bytes;
    unsigned limit, cursor;
};
static bool ReadPrefix(void *out, unsigned length, void *opaque) {
    PrefixInput &input = *static_cast<PrefixInput *>(opaque);
    if (length > input.limit - input.cursor) return false;
    std::memcpy(out, input.bytes->data() + input.cursor, length);
    input.cursor += length;
    return true;
}
static bool ThrowingReader(void *, unsigned, void *) { throw std::bad_alloc(); }
// Authored terrains for the site planner: callbacks only, no engine object. Like the engine's sampler, a cell outside the map is unusable.
struct Terrain { int mode; unsigned calls; int size; };
static bool TerrainHeight(int x, int y, float *height, void *opaque) {
    Terrain &terrain = *static_cast<Terrain *>(opaque);
    ++terrain.calls;
    if (x < 1 || y < 1 || x + 1 >= terrain.size || y + 1 >= terrain.size) return false;
    switch (terrain.mode) {
    case 0: *height = -40.0f; return true;                                 // flat and dry everywhere
    case 1: return false;                                                  // wet everywhere
    case 2:                                                                // a slope, with one flat plateau on it
        *height = (x >= 300 && x <= 420 && y >= 200 && y <= 320) ? -100.0f : -0.5f * x - 0.25f * y;
        return true;
    case 3: if (x < 200 || x > 240) return false; *height = -10.0f; return true;   // only a narrow dry band
    case 4: if (x == 64 && y == 64) return false; *height = -5.0f; return true;      // one unusable cell
    case 5:                                                                // a small dry field with a narrow ridge across it
        if (x < 200 || x > 280 || y < 200 || y > 330) return false;
        *height = (y >= 236 && y <= 239) ? -60.0f : -10.0f;
        return true;
    case 6: if (x >= 100 && x <= 200 && y >= 100 && y <= 200) return false; *height = -10.0f; return true;   // a lake
    }
    return false;
}
static void CheckSite(const CeMuseumVisit::SitePlan &plan, unsigned count, const CeMuseumVisit::ExhibitFootprint *footprints,
                      int width, int height) {
    for (unsigned i = 0; i != count; ++i) {
        Need(plan.centres[i][0] >= 32 && plan.centres[i][0] <= width - 32 && plan.centres[i][1] >= 32 && plan.centres[i][1] <= height - 32,
             "every exhibit stands clear of the map edge");
        for (unsigned j = i + 1; j != count; ++j) {
            const float dx = plan.centres[i][0] - plan.centres[j][0], dy = plan.centres[i][1] - plan.centres[j][1];
            const float needX = (footprints[i].width + footprints[j].width) / 2, needY = (footprints[i].depth + footprints[j].depth) / 2;
            Need((dx >= needX || dx <= -needX) || (dy >= needY || dy <= -needY), "no two exhibit footprints overlap");
        }
        Need(plan.visitor[1] < plan.centres[i][1] - footprints[i].depth / 2, "the visitor stands in front of every exhibit");
    }
}
int main(int argc, char **argv) {
    Need(argc == 2, "generator output directory supplied");
    CeMuseumVisit::ExhibitPlacement exhibits[64] = {};
    for (unsigned i = 0; i != 64; ++i) {
        exhibits[i].definition = i % 2 ? "authored.feature.b" : "authored.feature.a";
        exhibits[i].position[0] = 16.0f + i * 4.0f;
        exhibits[i].position[1] = 24.0f;
        exhibits[i].position[2] = -3.0f;
    }
    CeMuseumVisit::RoomPlan plan = {};
    plan.playerConfiguration = "authored.player";
    plan.baseLevel = 41; plan.mapNumber = 42;
    plan.visitorPosition[0] = 12.0f; plan.visitorPosition[1] = 24.0f; plan.visitorPosition[2] = -3.0f;
    plan.exhibits = exhibits; plan.exhibitCount = 3;
    CapturedRoom room;
    Need(CeMuseumVisit::GenerateRoom(plan, CaptureRoom, &room), "authored room generated");
    Need(room.calls == 1 && room.real.size() > room.base.size(), "one complete publication");
    const auto expectedBase = room.base, expectedReal = room.real;
    PrefixInput prefixInput = {&room.base, static_cast<unsigned>(room.base.size()), 0};
    CeMuseumVisit::CarrierPrefix prefix = {};
    Need(CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, true, &prefix), "base context decoded");
    Need(std::string(prefix.playerConfiguration) == "authored.player" && prefix.parentWorld == -1 && prefix.mapNumber == 42,
         "base context contains only the authored binding");
    const unsigned basePrefixLength = prefixInput.cursor;
    CeMuseumVisit::CarrierPrefix sentinel; std::memset(&sentinel, 0x5a, sizeof sentinel);
    for (unsigned length = 0; length < basePrefixLength; ++length) {
        prefixInput = {&room.base, length, 0}; prefix = sentinel;
        Need(!CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, true, &prefix), "every truncated base prefix refused");
        Need(std::memcmp(&prefix, &sentinel, sizeof prefix) == 0, "partial prefix never published");
    }
    prefixInput = {&room.real, static_cast<unsigned>(room.real.size()), 0};
    Need(CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, false, &prefix), "real context decoded without scripts");
    Need(prefix.parentWorld == 41 && prefix.mapNumber == -1 && prefixInput.cursor < basePrefixLength,
         "real source stops before the campaign script section");
    std::vector<unsigned char> corrupt = room.base;
    corrupt[basePrefixLength - 8] = 1;
    prefixInput = {&corrupt, static_cast<unsigned>(corrupt.size()), 0}; prefix = sentinel;
    Need(!CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, true, &prefix), "script-bearing base is not guessed past");
    Need(std::memcmp(&prefix, &sentinel, sizeof prefix) == 0, "unsupported base leaves result untouched");
    corrupt = room.base; corrupt[0] = 0;
    prefixInput = {&corrupt, static_cast<unsigned>(corrupt.size()), 0};
    Need(!CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, true, &prefix), "unknown world version refused");
    corrupt = room.base; corrupt[2] = 0;
    prefixInput = {&corrupt, static_cast<unsigned>(corrupt.size()), 0};
    Need(!CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, true, &prefix), "unknown header version refused");
    corrupt = room.base; corrupt[18] = 128;
    prefixInput = {&corrupt, static_cast<unsigned>(corrupt.size()), 0};
    Need(!CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, true, &prefix), "oversized configuration refused");
    corrupt = room.base; corrupt[19] = 0;
    prefixInput = {&corrupt, static_cast<unsigned>(corrupt.size()), 0};
    Need(!CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, true, &prefix), "embedded string terminator refused");
    prefix = sentinel;
    Need(!CeMuseumVisit::ReadCarrierPrefix(ThrowingReader, NULL, true, &prefix), "reader exception contained");
    Need(std::memcmp(&prefix, &sentinel, sizeof prefix) == 0, "reader exception leaves result untouched");
    Need(!CeMuseumVisit::ReadCarrierPrefix(NULL, NULL, true, &prefix), "missing reader refused");
    Need(!CeMuseumVisit::ReadCarrierPrefix(ReadPrefix, &prefixInput, true, NULL), "missing result refused");
    WriteBody(std::string(argv[1]) + "/base.body", room.base);
    WriteBody(std::string(argv[1]) + "/real.body", room.real);
    Need(CeMuseumVisit::GenerateRoom(plan, CaptureRoom, &room), "repeat generation works");
    Need(room.base == expectedBase && room.real == expectedReal, "generation deterministic");
    for (unsigned n = 0; n != 40; ++n) {
        CapturedRoom fault;
        allocationLimit = n;
        bool ok = CeMuseumVisit::GenerateRoom(plan, CaptureRoom, &fault);
        allocationLimit = -1;
        if (ok) Need(fault.base == expectedBase && fault.real == expectedReal, "allocation success complete");
        else Need(fault.base.empty() && fault.real.empty(), "allocation refusal has no partial publication");
    }
    auto refuse = [&](const CeMuseumVisit::RoomPlan &bad) {
        CapturedRoom untouched;
        Need(!CeMuseumVisit::GenerateRoom(bad, CaptureRoom, &untouched), "invalid plan refused");
        Need(untouched.calls == 0 && untouched.base.empty() && untouched.real.empty(), "invalid plan never published");
    };
    CeMuseumVisit::RoomPlan bad = plan;
    bad.playerConfiguration = NULL; refuse(bad);
    bad.playerConfiguration = ""; refuse(bad);
    bad.playerConfiguration = "authored\ncontrol"; refuse(bad);
    std::string longName(128, 'a'); bad.playerConfiguration = longName.c_str(); refuse(bad);
    bad = plan; bad.baseLevel = -1; refuse(bad);
    bad = plan; bad.baseLevel = 1000; refuse(bad);
    bad = plan; bad.mapNumber = -1; refuse(bad);
    bad = plan; bad.mapNumber = 1000; refuse(bad);
    bad = plan; bad.exhibitCount = 0; refuse(bad);
    bad = plan; bad.exhibitCount = 65; refuse(bad);
    bad = plan; bad.exhibits = NULL; refuse(bad);
    bad = plan; bad.visitorPosition[0] = std::numeric_limits<float>::quiet_NaN(); refuse(bad);
    bad = plan; bad.visitorPosition[0] = std::numeric_limits<float>::infinity(); refuse(bad);
    bad = plan; bad.visitorPosition[0] = 4097; refuse(bad);
    bad = plan; bad.visitorOrientation[1] = 1001; refuse(bad);
    exhibits[1].definition = ""; refuse(plan);
    exhibits[1].definition = "authored.feature.b";
    exhibits[1].orientation[2] = std::numeric_limits<float>::quiet_NaN(); refuse(plan);
    exhibits[1].orientation[2] = 0;
    Need(!CeMuseumVisit::GenerateRoom(plan, NULL, NULL), "missing receiver refused");
    CapturedRoom refusing; refusing.refuse = true;
    Need(!CeMuseumVisit::GenerateRoom(plan, CaptureRoom, &refusing) && refusing.calls == 1, "receiver refusal propagated");
    bad = plan; bad.exhibitCount = 64;
    CapturedRoom maximum; Need(CeMuseumVisit::GenerateRoom(bad, CaptureRoom, &maximum), "bounded maximum supported");
    {   // The site planner: authored terrain only.
        CeMuseumVisit::ExhibitFootprint six[6];
        for (unsigned i = 0; i != 6; ++i) { six[i].width = 10.0f + i; six[i].depth = 12.0f; six[i].height = 6.0f; }
        CeMuseumVisit::SitePlan site = {}, again = {}, sentinel;
        std::memset(&sentinel, 0x5a, sizeof sentinel);
        Terrain flat = {0, 0, 512};
        Need(CeMuseumVisit::PlanSite(six, 6, 512, 512, TerrainHeight, &flat, &site), "flat dry terrain gets a site");
        CheckSite(site, 6, six, 512, 512);
        Need(site.heightRange == 0.0f && site.centres[0][2] == -40.0f && site.visitor[2] == -40.0f, "flat site reports its height");
        Need(site.visible == 6 && site.dryRing == 48, "a flat open site shows every exhibit and keeps the widest dry ring");
        Need(CeMuseumVisit::PlanSite(six, 6, 512, 512, TerrainHeight, &flat, &again) && std::memcmp(&site, &again, sizeof site) == 0,
             "site planning is deterministic");
        Terrain wet = {1, 0, 512};
        again = sentinel;
        Need(!CeMuseumVisit::PlanSite(six, 6, 512, 512, TerrainHeight, &wet, &again) && std::memcmp(&again, &sentinel, sizeof again) == 0,
             "wet terrain is refused and leaves the result untouched");
        Terrain slope = {2, 0, 512};
        Need(CeMuseumVisit::PlanSite(six, 6, 512, 512, TerrainHeight, &slope, &site), "sloping terrain with a plateau gets a site");
        CheckSite(site, 6, six, 512, 512);
        Need(site.heightRange == 0.0f, "the flattest ground is chosen over a slope");
        for (unsigned i = 0; i != 6; ++i)
            Need(site.centres[i][0] >= 300 && site.centres[i][0] <= 420 && site.centres[i][1] >= 200 && site.centres[i][1] <= 320 &&
                 site.centres[i][2] == -100.0f, "every exhibit stands on the plateau");
        Terrain band = {3, 0, 512};
        Need(!CeMuseumVisit::PlanSite(six, 6, 512, 512, TerrainHeight, &band, &site), "a dry band too narrow for the exhibition is refused");
        CeMuseumVisit::ExhibitFootprint small[2] = {{6.0f, 6.0f, 5.0f}, {6.0f, 6.0f, 5.0f}};
        Need(CeMuseumVisit::PlanSite(small, 2, 512, 512, TerrainHeight, &band, &site), "two small exhibits fit the dry band");
        CheckSite(site, 2, small, 512, 512);
        Need(site.centres[0][0] >= 200 && site.centres[1][0] <= 240, "the small exhibits stand inside the dry band");
        Need(site.dryRing == 0, "the dry ring shrinks to nothing only when the ground leaves no other site");
        Terrain hole = {4, 0, 512};
        Need(CeMuseumVisit::PlanSite(six, 6, 512, 512, TerrainHeight, &hole, &site), "one unusable cell only rules out sites that cover it");
        Terrain lake = {6, 0, 512};
        Need(CeMuseumVisit::PlanSite(six, 6, 512, 512, TerrainHeight, &lake, &site) && site.dryRing == 48, "a lake is avoided by the widest ring when it can be");
        CheckSite(site, 6, six, 512, 512);
        for (unsigned i = 0; i != 6; ++i) {
            const float dx = site.centres[i][0] < 100 ? 100 - site.centres[i][0] : site.centres[i][0] > 200 ? site.centres[i][0] - 200 : 0;
            const float dy = site.centres[i][1] < 100 ? 100 - site.centres[i][1] : site.centres[i][1] > 200 ? site.centres[i][1] - 200 : 0;
            Need(dx >= 40 || dy >= 40, "every exhibit keeps clear of the water");
        }
        Terrain ridge = {5, 0, 512};
        CeMuseumVisit::ExhibitFootprint one[1] = {{6.0f, 6.0f, 5.0f}};
        Need(CeMuseumVisit::PlanSite(one, 1, 512, 512, TerrainHeight, &ridge, &site), "a small field with a ridge gets a site");
        Need(site.visible == 1, "the exhibit is in view of the visitor");
        Need(!(site.visitor[1] < 236 && site.centres[0][1] > 239), "the ridge does not stand between the visitor and the exhibit");
        CeMuseumVisit::ExhibitFootprint large[6];
        for (unsigned i = 0; i != 6; ++i) { large[i].width = 40.0f; large[i].depth = 40.0f; large[i].height = 20.0f; }
        Need(CeMuseumVisit::PlanSite(large, 6, 512, 512, TerrainHeight, &flat, &site), "large exhibits get a site");
        CheckSite(site, 6, large, 512, 512);
        Terrain tiny = {0, 0, 128};
        Need(!CeMuseumVisit::PlanSite(large, 6, 128, 128, TerrainHeight, &tiny, &site), "a map too small for the exhibition is refused");
        CeMuseumVisit::ExhibitFootprint bad[1] = {{0.0f, 5.0f, 5.0f}};
        Need(!CeMuseumVisit::PlanSite(bad, 1, 512, 512, TerrainHeight, &flat, &site), "a zero-width footprint is refused");
        bad[0].width = std::numeric_limits<float>::quiet_NaN();
        Need(!CeMuseumVisit::PlanSite(bad, 1, 512, 512, TerrainHeight, &flat, &site), "a NaN footprint is refused");
        bad[0].width = 5.0f; bad[0].depth = 257.0f;
        Need(!CeMuseumVisit::PlanSite(bad, 1, 512, 512, TerrainHeight, &flat, &site), "an implausibly large footprint is refused");
        bad[0].depth = 5.0f; bad[0].height = 0.0f;
        Need(!CeMuseumVisit::PlanSite(bad, 1, 512, 512, TerrainHeight, &flat, &site), "a flat footprint without height is refused");
        Need(!CeMuseumVisit::PlanSite(six, 0, 512, 512, TerrainHeight, &flat, &site), "no exhibits is refused");
        Need(!CeMuseumVisit::PlanSite(six, 17, 512, 512, TerrainHeight, &flat, &site), "too many exhibits is refused");
        Need(!CeMuseumVisit::PlanSite(NULL, 6, 512, 512, TerrainHeight, &flat, &site), "missing footprints refused");
        Need(!CeMuseumVisit::PlanSite(six, 6, 512, 512, NULL, &flat, &site), "missing sampler refused");
        Need(!CeMuseumVisit::PlanSite(six, 6, 512, 512, TerrainHeight, &flat, NULL), "missing result refused");
        Need(!CeMuseumVisit::PlanSite(six, 6, 5000, 512, TerrainHeight, &flat, &site), "an oversized map is refused");
        Terrain large_map = {0, 0, 4096};
        Need(CeMuseumVisit::PlanSite(six, 6, 4096, 4096, TerrainHeight, &large_map, &site) && large_map.calls < 1000000,
             "the largest map is searched within a bounded number of samples");
    }
    std::puts("PASS authored room writer, bounds, deterministic ownership and allocation refusal");
    return 0;
}
#elif defined(CE_MUSEUM_READER_FIXTURE)
#include <new>
#include <cstdint>
typedef int SINT;
typedef int BOOL;
typedef unsigned UINT;
typedef unsigned DWORD;
typedef void *HANDLE;
enum EMemoryType { MEMTYPE_MEMBUFFER };
#define TRUE 1
#define FALSE 0
#define ASSERT(x) Need(!!(x), "reader assertion")
#define TRACE(...) ((void)0)
static unsigned fileCloses, blockLoads;
static BOOL CloseHandle(HANDLE h) { Need(h != NULL, "owned reader has no file to close"); ++fileCloses; return TRUE; }
void *operator new[](size_t size, EMemoryType) { return ::operator new[](size); }
void operator delete[](void *p, EMemoryType) noexcept { ::operator delete[](p); }
class CDXMemBuffer {
public:
    HANDLE mFile;
    char *mData, *mPtr, *mCRCData, *mCRCDataUpTo;
    FILE *mCRCFile;
    SINT mSize, mDataSize;
    BOOL mReading, mEOF, mLastBlock;
    char mFileName[256];
    UINT mPos;
    EMemoryType mMemType;
    CDXMemBuffer();
    ~CDXMemBuffer() { Close(); }
    BOOL InitOwnedRead(const void *, UINT, EMemoryType = MEMTYPE_MEMBUFFER);
    SINT Read(void *, SINT);
    SINT Skip(SINT);
    void ReadString(char *, SINT);
    BOOL Close();
    void LoadNextBlock() { ++blockLoads; Need(false, "owned reader tried to fetch another file block"); }
    BOOL WriteBlock(const char *, HANDLE, char *, DWORD, DWORD *) { Need(false, "owned reader tried to write"); return FALSE; }
};
#include "museum_reader.inc"
int main() {
    unsigned char source[] = {0x12, 0x34, 0x56, 0x78, 0, 0xa5, 0x5a};
    const unsigned char expected[] = {0x12, 0x34, 0x56, 0x78, 0, 0xa5, 0x5a};
    {
        CDXMemBuffer r;
        Need(!r.InitOwnedRead(NULL, 1), "null input refused");
        Need(!r.InitOwnedRead(source, 0), "empty input refused");
        Need(!r.InitOwnedRead(source, 16u * 1024u * 1024u + 1), "oversize input refused before allocation");
        Need(r.InitOwnedRead(source, sizeof source), "valid owned read initialized");
        std::memset(source, 0, sizeof source);
        Need(!r.InitOwnedRead(expected, sizeof expected), "reinitializing a live reader refused");
        unsigned char destination[9]; std::memset(destination, 0xcc, sizeof destination);
        r.Read(destination, sizeof expected);
        Need(std::memcmp(destination, expected, sizeof expected) == 0, "byte order and independent ownership preserved");
        Need(destination[7] == 0xcc && destination[8] == 0xcc, "exact-end read keeps canaries");
        r.Read(destination + 7, 1);
        Need(destination[7] == 0xcc && r.mEOF, "past-end read does not fetch or overwrite");
        Need(r.Close(), "owned close succeeds");
        r.Close(); // The ordinary reader returns false when already closed.
        Need(!r.mData && fileCloses == 0, "repeated owned close does no file I/O or double free");
    }
    {
        CDXMemBuffer r; Need(r.InitOwnedRead(expected, sizeof expected), "partial read setup");
        unsigned char destination[12]; std::memset(destination, 0xcc, sizeof destination);
        r.Read(destination, sizeof destination);
        Need(std::memcmp(destination, expected, sizeof expected) == 0, "short body copied within bounds");
        for (unsigned i = sizeof expected; i < sizeof destination; ++i) Need(destination[i] == 0xcc, "overread canary intact");
        Need(r.mEOF, "short read marks end");
    }
    {
        CDXMemBuffer r; Need(r.InitOwnedRead(expected, sizeof expected), "skip setup");
        r.Skip(3); unsigned char b[2] = {}; r.Read(b, 2);
        Need(b[0] == expected[3] && b[1] == expected[4], "skip maintains position");
        r.Skip(100); Need(r.mEOF, "oversize skip marks end without another block");
    }
    {
        const char unterminated[] = {'a', 'b', 'c'};
        CDXMemBuffer r; Need(r.InitOwnedRead(unterminated, sizeof unterminated), "malformed string setup");
        char out[8]; std::memset(out, 0x7f, sizeof out); r.ReadString(out, 7);
        Need(out[7] == 0x7f && r.mEOF, "unterminated string stays in owned body and output bound");
    }
    {
        CDXMemBuffer r; r.mData = new char[1]; r.mDataSize = 1;
        r.mReading = TRUE; r.mFile = reinterpret_cast<HANDLE>(1);
        Need(r.Close(), "ordinary file close succeeds");
        Need(fileCloses == 1, "ordinary file still closed exactly once");
    }
    Need(blockLoads == 0 && fileCloses == 1, "owned readers never enter file I/O");
    std::puts("PASS owned reader bounds, byte order, lifetime and ordinary file close");
    return 0;
}
#else
#include <dirent.h>
#include <new>
#include "Career.h"
#include "frontend.h"
#include "ce_museum_visit.h"
static long allocationFailure = -1;
void *operator new(size_t length) {
    if (allocationFailure == 0) throw std::bad_alloc();
    if (allocationFailure > 0) --allocationFailure;
    void *p = std::malloc(length ? length : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void *operator new[](size_t length) { return ::operator new(length); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
static unsigned OpenFiles() {
    DIR *dir = opendir("/proc/self/fd"); Need(dir != NULL, "host file census available");
    unsigned count = 0; while (readdir(dir)) ++count; closedir(dir); return count;
}
CCareer fixtureCareer;
FixtureFrontend fixtureFrontend;
static std::string userRoot, gameRoot;
static bool menus = true, sessionAllowed = true, editor, gallery;
static unsigned nativeGenerationCalls;
static bool refuseNativeGeneration, changeDuringGeneration;
const char *PortUserDir() { return userRoot.c_str(); }
const char *PortGameDir() { return gameRoot.c_str(); }
bool PortCeMenus() { return menus; }
bool PortCeAuthoringSessionAllowed() noexcept { return sessionAllowed; }
extern "C" bool PortCeEditorActive() { return editor; }
extern "C" bool PortCeMuseumActive() { return gallery; }
bool CeMuseumVisit::GenerateInstalled() {
    ++nativeGenerationCalls;
    if (refuseNativeGeneration || !CheckInstalledSource()) return false;
    if (changeDuringGeneration) {
        std::ofstream out((gameRoot + "/data/resources/100_res_PC.aya").c_str(), std::ios::binary);
        out << "authored changed carrier";
    }
    const char base[] = "authored generated base", real[] = "authored generated room";
    return AcceptInstalled(21, base, sizeof base, real, sizeof real);
}
static std::vector<unsigned char> Career() {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(&fixtureCareer);
    return std::vector<unsigned char>(p, p + sizeof fixtureCareer);
}
static void ChangedCareer() { std::memset(&fixtureCareer, 0xcc, sizeof fixtureCareer); }
static int Read(const char *name, const void **bytes, unsigned *length) {
    return PortCeMuseumVisitReadWorld(name, bytes, length);
}
int main(int argc, char **argv) {
    Need(argc == 4, "mode and isolated roots required");
    std::string mode = argv[1]; userRoot = argv[2]; gameRoot = argv[3];
    unsigned char *career = reinterpret_cast<unsigned char *>(&fixtureCareer);
    for (unsigned i = 0; i < sizeof fixtureCareer; ++i) career[i] = static_cast<unsigned char>(i * 17 + 9);
    const std::vector<unsigned char> saved = Career();
    const void *bytes = reinterpret_cast<void *>(1); unsigned length = 99;
    Need(Read("data/worlds/world073.wrd", &bytes, &length) == 0 && !bytes && !length, "inactive never overrides ordinary worlds");
    if (mode == "native" || mode == "native-changed" || mode == "native-refused") {
        if (mode == "native") {
            std::ofstream out((userRoot + "/museum/room.cache").c_str());
            out << "this prepared cache must never be selected by the menu";
        }
        refuseNativeGeneration = mode == "native-refused";
        changeDuringGeneration = mode == "native-changed";
        Need(PortCeMuseumVisitBegin() && PortCeMuseumVisitActive(), "ordinary entry pins installed resources");
        Need(PortCeMuseumVisitLevel() == 100 && nativeGenerationCalls == 0, "ordinary entry defers generation until resources load");
        ChangedCareer();
        if (refuseNativeGeneration || changeDuringGeneration) {
            Need(Read("data/worlds/world100.wrd", &bytes, &length) == -1 && !bytes && !length,
                 "native refusal never falls back to prepared or campaign data");
            Need(!PortCeMuseumVisitAfterLoad() && PortCeMuseumVisitActive(), "failed native load retains career exclusion");
        } else {
            Need(Read("data/worlds/world100.wrd", &bytes, &length) == 1 && bytes && length, "native real world retained");
            Need(Read("data/worlds/world021.wrd", &bytes, &length) == 1 && bytes && length, "native base world retained");
            Need(PortCeMuseumVisitAfterLoad(), "native pair completes loading");
        }
        Need(nativeGenerationCalls == 1, "native generator called once");
        PortCeMuseumVisitLevelEnd();
        Need(PortCeMuseumVisitActive(), "native guard retained through teardown");
        Need(PortCeMuseumVisitTakeReturn() && !PortCeMuseumVisitActive() && Career() == saved, "native return restores original career");
        std::puts("PASS explicit native entry, retained bodies and no fallback"); return 0;
    }
    if (mode == "allocation-failures") {
        unsigned files = OpenFiles(), failures = 0, successes = 0;
        for (long point = 0; point != 96; ++point) {
            allocationFailure = point;
            bool entered = CeMuseumVisit::BeginPreparedCacheForDevelopment();
            allocationFailure = -1;
            if (entered) ++successes; else ++failures;
            PortCeMuseumVisitCancel();
            Need(!PortCeMuseumVisitActive() && Career() == saved, "allocation failure preserves career and mode");
            Need(OpenFiles() == files, "allocation failure closes every owned input handle");
        }
        Need(failures > 10 && successes > 0, "fault injection crosses preparation and success");
        std::puts("PASS allocation failures close inputs and restore state"); return 0;
    }
    if (mode == "guard-menus") menus = false;
    if (mode == "guard-frontend") fixtureFrontend.running = false;
    if (mode == "guard-policy") sessionAllowed = false;
    if (mode == "guard-editor") editor = true;
    if (mode == "guard-gallery") gallery = true;
    if (mode == "refuse" || mode == "default" || mode.find("guard-") == 0) {
        Need(!CeMuseumVisit::BeginPreparedCacheForDevelopment(), "invalid or conflicting visit refused");
        Need(!PortCeMuseumVisitActive() && PortCeMuseumVisitLevel() == -1, "refusal leaves normal mode active");
        Need(Career() == saved, "refusal preserves career");
        Need(!PortCeMuseumVisitTakeReturn(), "failed Begin has no return to consume");
        std::puts("PASS refusal and unchanged career"); return 0;
    }
    Need(CeMuseumVisit::BeginPreparedCacheForDevelopment() && PortCeMuseumVisitActive(), "prepared visit begins");
    Need(PortCeMuseumVisitLevel() == 73, "authored carrier selected");
    Need(Career() == saved, "Begin does not change career");
    ChangedCareer();
    Need(!CeMuseumVisit::BeginPreparedCacheForDevelopment(), "nested Begin refused without replacing snapshot");
    if (mode == "missing-world") {
        Need(Read("data/worlds/world021.wrd", &bytes, &length) == 1, "one body read");
        Need(!PortCeMuseumVisitAfterLoad() && PortCeMuseumVisitActive(), "incomplete load fails with guard retained");
        Need(Read("data/worlds/world073.wrd", &bytes, &length) == -1 && !bytes && !length, "failed load cannot recover into campaign");
    } else if (mode == "restart") {
        Need(Read("DATA\\WORLDS\\WORLD021.WRD", &bytes, &length) == 1 && Read("data/worlds/world073.wrd", &bytes, &length) == 1, "pair read");
        Need(PortCeMuseumVisitAfterLoad(), "pair complete");
        for (unsigned again = 0; again != 3; ++again) {
            PortCeMuseumVisitLevelEnd();
            Need(PortCeMuseumVisitActive(), "guard retained between runs of the level");
            Need(Read("data/worlds/world073.wrd", &bytes, &length) == 1 && bytes && length, "a restart is served the real world again");
            Need(Read("data/worlds/world021.wrd", &bytes, &length) == 1 && bytes && length, "a restart is served the base world again");
            Need(PortCeMuseumVisitAfterLoad(), "a restart completes loading");
        }
        PortCeMuseumVisitLevelEnd();
        Need(Read("data/worlds/world074.wrd", &bytes, &length) == -1 && !bytes && !length, "after the last end an unrelated world is refused");
        Need(Read("data/worlds/world073.wrd", &bytes, &length) == -1, "the refusal stays latched");
        Need(PortCeMuseumVisitActive(), "refusal retains career guard");
    } else if (mode == "restart-after-failure") {
        Need(Read("data/worlds/world021.wrd", &bytes, &length) == 1 && Read("data/worlds/world073.wrd", &bytes, &length) == 1, "pair read");
        Need(PortCeMuseumVisitAfterLoad(), "pair complete");
        Need(Read("data/worlds/world074.wrd", &bytes, &length) == -1, "an unrelated request during the level fails the visit");
        PortCeMuseumVisitLevelEnd();
        Need(Read("data/worlds/world073.wrd", &bytes, &length) == -1 && !bytes && !length, "a failed visit cannot restart even after a clean load");
        Need(Read("data/worlds/world021.wrd", &bytes, &length) == -1, "a failed visit stays failed");
        Need(PortCeMuseumVisitActive(), "failure retains career guard");
    } else if (mode == "restart-unfinished") {
        Need(Read("data/worlds/world021.wrd", &bytes, &length) == 1 && Read("data/worlds/world073.wrd", &bytes, &length) == 1, "pair read");
        Need(PortCeMuseumVisitAfterLoad(), "pair complete");
        PortCeMuseumVisitLevelEnd();
        Need(Read("data/worlds/world073.wrd", &bytes, &length) == 1, "restart read once");
        PortCeMuseumVisitLevelEnd();      // the restart never finished loading
        Need(Read("data/worlds/world073.wrd", &bytes, &length) == -1, "a restart that never completed loading cannot restart again");
        Need(PortCeMuseumVisitActive(), "refusal retains career guard");
    } else if (mode == "wrong-world" || mode == "alias-world" || mode == "long-world" || mode == "null-output") {
        std::string path = mode == "alias-world" ? "data/worlds/../worlds/world073.wrd" : mode == "long-world" ? std::string(200, 'a') : "data/worlds/world074.wrd";
        int status = mode == "null-output" ? Read("data/worlds/world073.wrd", NULL, &length) : Read(path.c_str(), &bytes, &length);
        Need(status == -1, "unexpected world fails closed");
        Need(Read("data/worlds/world073.wrd", &bytes, &length) == -1, "refusal remains latched");
        Need(PortCeMuseumVisitActive(), "load refusal retains career guard");
    } else {
        if (mode == "retained") {
            std::ofstream f(userRoot + "/museum/world073.body", std::ios::binary | std::ios::trunc); f << "changed after Begin";
        }
        Need(Read("DATA\\WORLDS\\WORLD021.WRD", &bytes, &length) == 1, "canonical base path accepted");
        const std::string base = "authored museum base bytes";
        Need(length == base.size() && !std::memcmp(bytes, base.data(), length), "base identity and decoded bytes retained");
        Need(Read("data/worlds/world073.wrd", &bytes, &length) == 1, "real body read");
        const std::string real = "authored museum exhibit bytes";
        Need(length == real.size() && !std::memcmp(bytes, real.data(), length), "real identity and retained snapshot stable");
        Need(PortCeMuseumVisitAfterLoad(), "both prepared worlds complete load");
        PortCeMuseumVisitLevelEnd();
        Need(PortCeMuseumVisitActive(), "teardown retains progression guard");
        Need(Read("data/worlds/world074.wrd", &bytes, &length) == -1, "teardown cannot load an unrelated world");
    }
    Need(PortCeMuseumVisitTakeReturn(), "return consumed once");
    Need(!PortCeMuseumVisitActive() && Career() == saved, "final return restores exact entry career");
    ChangedCareer();
    Need(!PortCeMuseumVisitTakeReturn(), "second return does nothing");
    PortCeMuseumVisitCancel();
    Need(Career() != saved, "repeated cleanup never restores an old snapshot");
    Need(Read("data/worlds/world073.wrd", &bytes, &length) == 0, "ordinary world route restored");
    std::puts("PASS visit identity, refusal, ownership and return");
    return 0;
}
#endif
