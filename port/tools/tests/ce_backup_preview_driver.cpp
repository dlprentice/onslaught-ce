// SPDX-License-Identifier: GPL-3.0-only
#include "../../game/ce_career_view.h"
#include "../../platform/common/ce_backup_preview.h"
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) {
  if (argc == 3 && !std::strcmp(argv[1], "fixture")) {
    std::vector<unsigned char> b(CE_CAREER_SIZE);
    b[0] = CE_CAREER_VERSION & 255;
    b[1] = CE_CAREER_VERSION >> 8;
    using namespace ce_career_view_detail;
    b[kMissionOffset + kMissionWorld] = 120;
    b[kMissionOffset + kMissionComplete] = 1;
    FILE *f = std::fopen(argv[2], "wb");
    if (!f)
      return 2;
    bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    return std::fclose(f) || !ok ? 2 : 0;
  }
  if (argc == 8 && !std::strcmp(argv[5], "list")) {
    std::vector<std::string> names;
    std::string error;
    bool ok = CePreviewList(argv[1], argv[2], argv[3], argv[4], names, error);
    if (std::atoi(argv[6]) < 0)
      return !ok && names.empty() ? 0 : 6;
    return ok && int(names.size()) == std::atoi(argv[6]) &&
                   (names.empty() || names.front() == argv[7])
               ? 0
               : 7;
  }
  if (argc != 8)
    return 2;
  CePreviewSet value;
  std::string error;
  bool ok = CePreviewRead(argv[1], argv[2], argv[3], argv[4], value, error);
  if (!std::strcmp(argv[5], "deny"))
    return !ok && value.careers.empty() && !error.empty() ? 0 : 3;
  if (!ok || value.careers.empty() || value.careers[0].name != argv[5] ||
      value.careers[0].proposal != argv[6] ||
      value.members != std::atoi(argv[7]))
    return 4;
  SCeCareerView v{};
  return CeCareerViewBuild(value.careers[0].bytes.data(),
                           int(value.careers[0].bytes.size()), &v) &&
                 v.mMissionsComplete == 1
             ? 0
             : 5;
}
