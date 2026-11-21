// SPDX-License-Identifier: GPL-3.0-only
#include "ce_asset_catalog.h"
#include "ce_catalogue_menu.h"
#include <cstring>

static const char* game_dir;
const char* PortGameDir() { return game_dir; }
void PortPathsNamesOnly() {}

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

static void String(const char* text)
{
    std::cout << '"';
    if (text) for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p == '"' || *p == '\\') std::cout << '\\' << *p;
        else if (*p < 0x20) {
            const char hex[] = "0123456789abcdef";
            std::cout << "\\u00" << hex[*p >> 4] << hex[*p & 15];
        } else std::cout << *p;
    }
    std::cout << '"';
}

static void Index(size_t index)
{
    if (index == SIZE_MAX) std::cout << "null";
    else std::cout << index;
}

struct Captured { unsigned calls; uint32_t signature; size_t bytes; bool allow; };
static int Picture(const unsigned char* data, size_t bytes, const CeAssetTextureHeader* header, void* arg) {
    Captured& c = *static_cast<Captured*>(arg); ++c.calls; c.bytes=bytes;
    if (!header->width || !header->height || bytes <= 128) return 0;
    uint32_t value = 2166136261u;
    for (size_t i=128; i<bytes; ++i) value = (value ^ data[i]) * 16777619u;
    c.signature = value;
    return c.allow;
}

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    if (std::string(argv[1]) == "defaults") {
        bool safe = PortCeAssetCatalogCount(nullptr) == 0 &&
            PortCeAssetCatalogDiagnosticCount(nullptr) == 0 &&
            PortCeAssetCatalogKind(nullptr, 0) == 0 &&
            PortCeAssetCatalogSize(nullptr, 0) == 0 &&
            PortCeAssetCatalogSampleIndex(nullptr, 0) == -1 &&
            !PortCeAssetCatalogTitle(nullptr, 0) &&
            !PortCeAssetCatalogPath(nullptr, 0) &&
            !PortCeAssetCatalogGroup(nullptr, 0) &&
            !PortCeAssetCatalogDiagnosticPath(nullptr, 0) &&
            !PortCeAssetCatalogDiagnosticMessage(nullptr, 0) &&
            PortCeAssetCatalogNext(nullptr, SIZE_MAX, CE_ASSET_ALL, nullptr) == SIZE_MAX &&
            PortCeAssetCatalogFindTexture(nullptr, "anything") == SIZE_MAX;
        PortCeAssetCatalogClose(nullptr);
        char error = 'x';
        safe = safe && !PortCeAssetCatalogOpen(nullptr, &error, 1) && error == '\0';
        safe = safe && !PortCeAssetCatalogOpen("", nullptr, 0);
        std::cout << (safe ? "true\n" : "false\n");
        return safe ? 0 : 1;
    }
    if (argc == 7 && !std::strcmp(argv[1], "picture")) {
        game_dir=argv[2]; PortCeCatalogueOpen(); PortCeCatalogueBrowse(std::atoi(argv[3]));
        for (int i=1; i<std::atoi(argv[4]); ++i) PortCeCatalogueTurn();
        unsigned slot=unsigned(std::atoi(argv[5])); Captured c={0,0,0,std::atoi(argv[6]) != 0};
        bool accepted=PortCeCatalogueReadTexture(slot,Picture,&c);
        std::cout << "[" << int(accepted) << "," << c.calls << "," << c.bytes << "," << c.signature << "]";
        PortCeCatalogueClose(); return 0;
    }
    if (argc == 6 && !std::strcmp(argv[1], "browse")) {
        game_dir = argv[2]; PortCeCatalogueOpen(); PortCeCatalogueBrowse(std::atoi(argv[3]));
        for (int i = 1; i < std::atoi(argv[4]); ++i) PortCeCatalogueTurn();
        char title[35], detail[256]; const unsigned slot = static_cast<unsigned>(std::atoi(argv[5]));
        PortCeCatalogueTitle(slot, title, sizeof title); PortCeCatalogueDetails(slot, detail, sizeof detail);
        std::cout << "{\"page\":" << PortCeCataloguePageNumber() << ",\"pages\":" << PortCeCataloguePageCount()
                  << ",\"visible\":" << PortCeCatalogueVisible() << ",\"name\":";
        String(title); std::cout << ",\"detail\":"; String(detail); std::cout << '}'; PortCeCatalogueClose();
        return 0;
    }
    if (argc < 3) return 2;
    char error[256] = {};
    const bool restricted = argc == 7 && !std::strcmp(argv[1], "scan-kinds");
    CeAssetCatalog* catalog = restricted ? PortCeAssetCatalogOpenKinds(argv[2], std::atoi(argv[6]), error, sizeof(error))
                                        : PortCeAssetCatalogOpen(argv[2], error, sizeof(error));
    std::cout << "{\"opened\":" << (catalog ? "true" : "false") << ",\"error\":";
    String(error);
    std::cout << ",\"assets\":[";
    const size_t count = PortCeAssetCatalogCount(catalog);
    for (size_t i = 0; i < count; ++i) {
        if (i) std::cout << ',';
        std::cout << "{\"kind\":" << PortCeAssetCatalogKind(catalog, i) << ",\"title\":";
        String(PortCeAssetCatalogTitle(catalog, i));
        std::cout << ",\"path\":"; String(PortCeAssetCatalogPath(catalog, i));
        std::cout << ",\"group\":"; String(PortCeAssetCatalogGroup(catalog, i));
        std::cout << ",\"size\":" << PortCeAssetCatalogSize(catalog, i)
                  << ",\"sample\":" << PortCeAssetCatalogSampleIndex(catalog, i) << '}';
    }
    std::cout << "],\"diagnostics\":[";
    for (size_t i = 0; i < PortCeAssetCatalogDiagnosticCount(catalog); ++i) {
        if (i) std::cout << ',';
        std::cout << "{\"path\":"; String(PortCeAssetCatalogDiagnosticPath(catalog, i));
        std::cout << ",\"message\":"; String(PortCeAssetCatalogDiagnosticMessage(catalog, i));
        std::cout << '}';
    }
    std::cout << "],\"matches\":[";
    size_t previous = SIZE_MAX, found;
    int mask = argc > 4 ? std::atoi(argv[4]) : CE_ASSET_ALL;
    const char* query = argc > 3 ? argv[3] : "";
    bool separator = false;
    while ((found = PortCeAssetCatalogNext(catalog, previous, mask, query)) != SIZE_MAX) {
        if (separator) std::cout << ',';
        std::cout << found;
        separator = true;
        if (found >= count || (previous != SIZE_MAX && found <= previous)) return 3;
        previous = found;
    }
    std::cout << "],\"resolve\":";
    Index(PortCeAssetCatalogFindTexture(catalog, argc > 5 ? argv[5] : ""));
    bool safe = PortCeAssetCatalogKind(catalog, count) == 0 &&
        PortCeAssetCatalogSize(catalog, count) == 0 &&
        PortCeAssetCatalogSampleIndex(catalog, count) == -1 &&
        !PortCeAssetCatalogTitle(catalog, count) &&
        PortCeAssetCatalogNext(catalog, count, mask, query) == SIZE_MAX &&
        PortCeAssetCatalogNext(catalog, SIZE_MAX - 1, mask, query) == SIZE_MAX;
    std::cout << ",\"bounds_safe\":" << (safe ? "true" : "false") << "}\n";
    PortCeAssetCatalogClose(catalog);
    return safe ? 0 : 1;
}
