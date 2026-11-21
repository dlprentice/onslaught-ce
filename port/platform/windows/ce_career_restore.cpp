#include "../common/ce_career_restore.h"

// Career publication remains unavailable until the native Windows custody gate
// passes. Building the backend or running file operations under Wine is not it.
CeCareerStorageResult CeCareerBackupAll(const CeCareerStorageRequest &) {
  CeCareerStorageResult out;
  out.status = CeCareerStorageStatus::Unsupported;
  return out;
}
CeCareerStorageResult CeCareerTakeFullSet(const CeCareerStorageRequest &) {
  CeCareerStorageResult out;
  out.status = CeCareerStorageStatus::Unsupported;
  return out;
}
CeCareerStorageResult CeCareerRestoreAsNew(const CeCareerRestoreRequest &) {
  CeCareerStorageResult out;
  out.status = CeCareerStorageStatus::Unsupported;
  return out;
}

bool CeCareerRestoreStamp(const CeCareerRestoreRequest &, std::string &stamp) {
  stamp.clear();
  return false;
}

CeCareerStorageStatus CeCareerRestoreSuggest(const CeCareerRestoreRequest &,
                                             std::string &targetName) {
  targetName.clear();
  return CeCareerStorageStatus::Unsupported;
}
