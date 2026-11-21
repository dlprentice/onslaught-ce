// SPDX-License-Identifier: GPL-3.0-only
#include "../../../platform/common/ce_career_restore.h"

// Cross-compiled with the real Windows career custody backend to confirm every
// entry point compiles cleanly for shipping 32-bit Windows and fails closed on
// an empty, unauthorized request. Full behavioral and Wine execution coverage
// lives in port/tools/ce_career_restore_windows_tests.py.
int main() {
  CeCareerRestoreRequest request;
  std::string stamp = "not returned";
  const bool stamped = CeCareerRestoreStamp(request, stamp);
  const auto backup = CeCareerBackupAll(request);
  const auto restore = CeCareerRestoreAsNew(request);
  const auto take = CeCareerTakeFullSet(request);
  std::string name = "not returned";
  const auto suggest = CeCareerRestoreSuggest(request, name);
  return !stamped && stamp.empty() &&
                 backup.status == CeCareerStorageStatus::Refused &&
                 restore.status == CeCareerStorageStatus::Refused &&
                 take.status == CeCareerStorageStatus::Refused &&
                 take.backupSet.empty() && take.backupStamp.empty() &&
                 backup.backupSet.empty() && restore.newFile.empty() &&
                 suggest == CeCareerStorageStatus::Refused && name.empty()
             ? 0
             : 1;
}
