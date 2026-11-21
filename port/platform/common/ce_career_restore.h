#ifndef CE_CAREER_RESTORE_H
#define CE_CAREER_RESTORE_H

#include <string>

// Synchronous storage only. No menu invokes these operations. An owner must
// establish the front-end, open-card, main-thread, no-session/replay gates and
// hold that authority throughout the call. A missing authority refuses.
struct CeCareerStorageRequest {
  std::string user, game, package, backupRoot;
  bool (*permitted)(void *) = nullptr;
  bool (*cancelled)(void *) = nullptr;
  void *owner = nullptr;
};

struct CeCareerRestoreRequest : CeCareerStorageRequest {
  std::string sourceSet, sourceIdentity, sourceName, sourceStamp;
  // Exact full filename shown at confirmation, including .bes. It is never
  // silently replaced with another suggestion if the name becomes occupied.
  std::string targetName;
  // The game owner checks filename-dependent semantics on both complete names.
  // Missing approval refuses; the backend does not guess cheat-name behavior.
  bool (*namePermitted)(void *, const std::string &,
                        const std::string &) = nullptr;
};

enum class CeCareerStorageStatus {
  Complete,
  Refused,
  Busy,
  Cancelled,
  Changed,
  Occupied,
  Uncertain,
  Unsupported
};

struct CeCareerStorageResult {
  CeCareerStorageStatus status = CeCareerStorageStatus::Refused;
  std::string backupSet;
  std::string backupIdentity;
  std::string backupStamp;
  std::string newFile;
  // Uncertain means the named backup or new file may exist. Preserve it and
  // inspect; never retry, replace, or remove it automatically.
};

// Read-only selection custody: bind the existing preview identity to the set
// directory and each member identity. Call when selecting; pass the returned
// stamp unchanged to RestoreAsNew. An empty or stale stamp refuses publication.
bool CeCareerRestoreStamp(const CeCareerRestoreRequest &request,
                          std::string &stamp);

// Full effective card namespace (user precedence), plus the user's options if
// present. Unrepresentable, ambiguous, linked or unsupported members refuse the
// whole operation; they are never silently dropped. Existing files are
// read-only.
// Its manifest's Reason is "All careers" (older sets keep theirs). The set is
// staged under a name the preview never lists and verified there by the
// preview reader before it is renamed, without replacement, to a listed name.
CeCareerStorageResult CeCareerBackupAll(const CeCareerStorageRequest &request);

// Take a verified full set or refuse: the call every later CE write makes
// first, with the same authority. It makes a fresh CeCareerBackupAll set and
// binds it again. Complete is the only status to write behind, and it always
// carries the set's folder (backupSet), its identity (the preview identity)
// and its stamp. Any other status is a refusal with no identity or stamp;
// only Uncertain keeps backupSet, naming a set that may exist, to inspect.
// Nothing calls it yet.
CeCareerStorageResult CeCareerTakeFullSet(const CeCareerStorageRequest &request);

// Read-only readable proposal, using both current save locations. It is not a
// reservation or semantic approval. No game callback is called here. The caller
// validates filename semantics on the game thread, shows that exact name, and
// returns it as targetName on
// confirmation. Repeated restored copies receive a bounded numeric suffix.
CeCareerStorageStatus
CeCareerRestoreSuggest(const CeCareerRestoreRequest &request,
                       std::string &targetName);

// Rereads the selected existing preview identity, makes a fresh complete set in
// this call, stages and publishes an intact new career with no replacement.
// The new basename is numeric, case-invariant and within the existing name
// seam. Unrelated writers need not take our lock for exact-name exclusion to
// hold.
CeCareerStorageResult
CeCareerRestoreAsNew(const CeCareerRestoreRequest &request);
#endif
