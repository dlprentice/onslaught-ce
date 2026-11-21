#include "../../../platform/common/ce_career_restore.h"
int main() {
  CeCareerRestoreRequest request;
  const auto backup = CeCareerBackupAll(request);
  const auto restore = CeCareerRestoreAsNew(request);
  const auto take = CeCareerTakeFullSet(request);
  std::string name = "not returned";
  const auto suggest = CeCareerRestoreSuggest(request, name);
  return backup.status == CeCareerStorageStatus::Unsupported &&
                 restore.status == CeCareerStorageStatus::Unsupported &&
                 take.status == CeCareerStorageStatus::Unsupported &&
                 take.backupSet.empty() && take.backupStamp.empty() &&
                 backup.backupSet.empty() && restore.newFile.empty() &&
                 suggest == CeCareerStorageStatus::Unsupported && name.empty()
             ? 0
             : 1;
}
