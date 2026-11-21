// Fixture modes and a reader for a disposable external backup set.
#include "../../../platform/common/ce_backup_preview.h"
#include "../../../platform/common/ce_backup_verify.h"
#include "../../../game/ce_career_view.h"
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char **argv) {
    if (argc == 2 && (!std::strcmp(argv[1], "--authored-flag") ||
                      !std::strcmp(argv[1], "--authored-progress"))) {
        using namespace ce_career_view_detail;
        std::vector<unsigned char> bytes(CE_CAREER_SIZE);
        if (std::fread(bytes.data(), 1, bytes.size(), stdin) != bytes.size() ||
            std::fgetc(stdin) != EOF) return 3;
        const size_t index = !std::strcmp(argv[1], "--authored-flag") ?
            kInvincibleOffset : kMissionOffset + kMissionComplete;
        bytes[index] = !bytes[index];
        return std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size() ? 0 : 3;
    }
    if (argc != 6 && (argc != 7 || std::strcmp(argv[6], "--selected-payload"))) return 3;
    CePreviewSet set;
    std::string error;
    if (!CePreviewRead(argv[1], argv[2], argv[3], argv[4], set, error)) return 2;
    for (const auto &career : set.careers) if (career.name == argv[5]) {
        const std::string bytes(career.bytes.begin(), career.bytes.end());
        std::printf("%s %s %d %zu\n", set.identity.c_str(),
                    ce_backup_verify::digest(bytes).c_str(), set.members, set.careers.size());
        if (argc == 7 && std::fwrite(bytes.data(), 1, bytes.size(), stdout) != bytes.size()) return 3;
        return 0;
    }
    return 4;
}
