// SPDX-License-Identifier: GPL-3.0-only
// Conservative local-content preflight for an explicitly admitted network contract.
#ifndef CE_NETWORK_COMPATIBILITY_H
#define CE_NETWORK_COMPATIBILITY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// A prospective contract, not an assertion that a released session already uses it.
// Add another explicit tuple when its protocol is admitted; do not accept arbitrary schema numbers.
typedef enum CE_CompatibilityContract {
    CE_COMPAT_LEVEL852_WORLD9_INPUT5_CONTROL4 = 1, // Historical Compute-only prototype.
    CE_COMPAT_LEVEL852_WORLD10_INPUT5_CONTROL5 = 2, // Previous fresh-process schema.
    CE_COMPAT_LEVEL852_WORLD11_INPUT5_CONTROL5 = 3,
    // The two-player lockstep co-op (levels 100, 856 and 858): both peers run the genuine simulation from the same content and compare state hashes.
    CE_COMPAT_LOCKSTEP_PROTOCOL1 = 4
} CE_CompatibilityContract;

// The levels the lockstep contract admits (the defence levels 856 and 858 and the tutorial, 100). One definition for the admission, the boot and the lobby.
static inline int CE_CompatibilityLockstepLevel(uint32_t level) { return level == 100 || level == 856 || level == 858; }

typedef struct CE_CompatibilityPreset {
    uint32_t index;
    char name[96]; // Exact authored name resolved by the caller, not a filename or a free-form profile.
} CE_CompatibilityPreset;

typedef struct CE_CompatibilityRequest {
    CE_CompatibilityContract contract;
    uint32_t level;
    CE_CompatibilityPreset players[2]; // Canonical host player followed by guest player.
} CE_CompatibilityRequest;

typedef struct CE_CompatibilityResult {
    uint8_t digest[32];
    uint32_t files;
    uint64_t bytes;
} CE_CompatibilityResult;

// Read-only: BLAKE2b-256 with the versioned effective-content-v1 transcript (including nested file
// digests). Hashes the complete effective data/ namespace, including unknown extensions, through
// the real port_fopen overlay. Requires absolute launcher-provided BEA_GAME_DIR and BEA_USER_DIR,
// unambiguous ASCII content names, no content links, the admitted PC archives/configurations, and
// absent or empty autoexec.con. Private-looking files inside data/ refuse admission, never disappear
// from the digest silently. No save, options, log, root path or content payload is returned or sent.
// The build must define a common CE_NETWORK_SIMULATION_ID on both platforms; an unset ID refuses.
// Both launcher roots must already exist. Windows additionally requires their original UTF-8 byte
// strings to fit char[MAX_PATH], normal absolute drive syntax, and the process UTF-8 code page, before
// calling the legacy reader (whose Init otherwise truncates roots and calls MakeDirs). The inventory
// uses explicit UTF-8 conversion; it does not repair or create a launcher's folders.
// Normal application file-layer initialization must have completed outside this function. The actual
// cached root pair must match the launcher environment byte-for-byte, otherwise restart is required.
// Run on the initializing/main thread; do not reconfigure roots during preflight or the session.
// Bounds: 65,536 effective files, 32 subdirectories deep, 240-byte logical paths, 4 GiB per file,
// 16 GiB total. A changed inventory or opened-file identity during the scan refuses admission.
// Windows requires a known full FILE_ID_INFO (64-bit volume and 128-bit file ID); unavailable or
// unsupported queries refuse, with no legacy 64-bit file-index fallback.
// This is not an atomic content snapshot or loaded-definition proof. Keep inputs unchanged and
// verify the genuine loaded definitions/topology before START. Caller retains session/option admission.
// Returns 1 on success, 0 on refusal; result is zeroed on refusal. Errors contain no host paths.
int CE_CompatibilityCompute(const CE_CompatibilityRequest *request, CE_CompatibilityResult *result,
                            char *error, size_t error_size);

// Pure prospective read-fence helper, not a patch to any current reader. Size excludes the terminator;
// embedded NUL, traversal, absolute/drive/UNC paths, repeated/trailing separators, private names and
// nonportable components refuse. Either single slash convention is normalized to lower-case data/... .
// Case uniqueness and opened-file identity still require the inventory checks above. A future session
// must wire this fence at all content readers and refuse interactive console mutation before claiming
// arbitrary authored references cannot escape the hashed namespace. The only automatic console entry
// currently identified is autoexec.con, handled separately as absent/empty by Compute.
// Returns 1 with a terminated normalized path, or 0 with an empty destination when one is supplied.
int CE_CompatibilityNormalizeDataPath(const char *path, size_t path_size, char *normalized, size_t normalized_size);

// Experimental fresh-process level-852 admission. Prepare is called once, after genuine platform
// file-layer initialization and before any authored read or nonempty console operation. It retains
// the actual effective inventory and every file digest; it never accepts a caller-provided digest
// or preset claim. The immutable context includes the compiled common simulation ID, exact schema
// tuple, level and policy1 (both genuine configuration indices0, uninverted sides). Loaded objects/settings still require the
// actual game adapter's checks; these APIs do not infer them from disk bytes.
// Call this read-only root guard BEFORE normal platform initialization: Windows' legacy Init
// would otherwise truncate an overlong root and call MakeDirs on the wrong path. It performs no
// initialization, game-reader open or creation. Prepare additionally requires the real cached pair.
int CE_CompatibilityValidateLauncherRoots(char *error, size_t error_size);
int CE_CompatibilityPrepare(CE_CompatibilityContract contract, uint32_t level,
                            CE_CompatibilityResult *result, char *error, size_t error_size);
int CE_CompatibilityPrepared(CE_CompatibilityResult *result, char *error, size_t error_size);
// Phase-only observation under the admission mutex. Does not recheck roots, roles,
// the retained digest or file bytes; callers must still use the boundary checks.
int CE_CompatibilityAdmissionLive(char *error, size_t error_size);
int CE_CompatibilityCheckBoundary(char *error, size_t error_size);

// Actual reader hooks call Observe before any authored open/write side effect, then Validate on
// every successful read open before handing bytes to the game. Missing normalized optional data
// probes may fail normally; a newly appearing successful open cannot pass the retained inventory.
// Exactly one leading ./ or .\ is admitted for the stock sample-bank spelling; traversal, absolute
// paths and other out-of-data reads refuse. Local-only files and known-empty automatic scripts
// need separate, explicit owning-callsite policies, never a generic outside-data bypass.
// Before Prepare these hooks allow ordinary offline operation but permanently record prior reads.
// While armed any refusal is sticky, including writes refused before copy-on-write/truncation.
int CE_CompatibilityObserveAuthoredRead(const char *logical, int for_write, char *error, size_t error_size);
// After a failed effective read-open, latch a failure if the retained inventory
// contained that regular file. An originally absent optional probe stays allowed.
// The caller preserves its own errno or platform error around this notification.
int CE_CompatibilityAuthoredOpenFailed(const char *logical, char *error, size_t error_size);
// For a dormant/unsupported reader whose returned bytes cannot be validated by these APIs.
// Offline operation records history; an armed admission refuses instead of assuming closure.
int CE_CompatibilityUnsupportedReader(char *error, size_t error_size);
int CE_CompatibilityValidateOpenedFile(const char *logical, FILE *file, char *error, size_t error_size);
// Linux passes its actual POSIX fd before constructing PortFile; Windows passes the actual HANDLE
// cast to intptr_t. Validation never closes/repositions the caller's handle or changes FILE state.
// Windows hashes an independently reopened buffered handle to the same object, not a logical path.
int CE_CompatibilityValidateOpenedNative(const char *logical, intptr_t handle, char *error, size_t error_size);

typedef enum CE_CompatibilityConsoleKind {
    CE_COMPAT_CONSOLE_EXECUTE = 1,
    CE_COMPAT_CONSOLE_SET_VARIABLE = 2,
    CE_COMPAT_CONSOLE_SCRIPT = 3
} CE_CompatibilityConsoleKind;
// Call before the actual command/direct variable mutation, including bound/nested/script paths.
// Only an empty ExecuteCommand is a no-op. Prior nonempty operations permanently dirty history;
// armed operations refuse and latch failure. No command text is stored, logged or hashed.
int CE_CompatibilityConsoleOperation(CE_CompatibilityConsoleKind kind, const char *text, size_t text_size,
                                     char *error, size_t error_size);
// Actual non-console mutation owners (for example CE cheat setters) use this before changing or
// persisting state. No private setting/value is stored; the same sticky history rule applies.
int CE_CompatibilityRuntimeMutation(char *error, size_t error_size);

// Call only after the actual world is destroyed. Ordinary offline hooks may then proceed, but no
// subsequent Prepare is possible in this process. The process-owned context is not freed here:
// concurrent media callbacks cannot observe a dangling admission. Its one retained allocation,
// mutex and maps have no C++ static destructor (a static Bink worker may join later). The OS reclaims
// them at process exit. One internal mutex serializes all scans/validation/state transitions.
// Private thread-local scopes cover only this module's own preflight reader calls, not a public
// or environment-controlled bypass. Run Prepare/CheckBoundary at the initializing/main barrier.
// Byte/count/depth bounds remain those above; filesystem scans are not atomic session snapshots.
void CE_CompatibilityRetire(void);

#ifdef __cplusplus
}
#endif
#endif
