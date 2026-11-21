// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_career_publish.h: plain game/platform boundary for picking external
// career files (.bes) and safely publishing new careers behind CeCareerTakeFullSet.
//
// Only plain POD structs, fixed arrays and function pointers cross this boundary so game units
// (compiled with the game's allocator) and platform units (compiled with the host allocator) stay
// cleanly separated. Every write ALWAYS creates a brand-new .bes file and NEVER replaces or alters
// an existing career.
#ifndef CE_CAREER_PUBLISH_H
#define CE_CAREER_PUBLISH_H

#include <stddef.h>

enum CeImportPickState
{
	CE_IMPORT_PICK_IDLE = 0,
	CE_IMPORT_PICK_OPEN,
	CE_IMPORT_PICK_READY,
	CE_IMPORT_PICK_CANCELLED,
	CE_IMPORT_PICK_ERROR
};

struct CeImportPickStatus
{
	CeImportPickState	state;
	char				path[4096];
	char				leaf[256];
	char				message[192];
};

enum CeImportReadStatus
{
	CE_IMPORT_READ_OK = 0,
	CE_IMPORT_READ_BAD_PATH,
	CE_IMPORT_READ_UNSAFE_FILE,
	CE_IMPORT_READ_IO_ERROR,
	CE_IMPORT_READ_BAD_SIZE,
	CE_IMPORT_READ_BAD_CAREER
};

enum CeCareerPublishKind
{
	CE_PUBLISH_IMPORT = 0,
	CE_PUBLISH_EDIT = 1
};

enum CeCareerPublishStatus
{
	CE_PUBLISH_COMPLETE = 0,
	CE_PUBLISH_REFUSED,
	CE_PUBLISH_BUSY,
	CE_PUBLISH_CANCELLED,
	CE_PUBLISH_CHANGED,
	CE_PUBLISH_OCCUPIED,
	CE_PUBLISH_UNCERTAIN,
	CE_PUBLISH_UNSUPPORTED,
	CE_PUBLISH_BAD_NAME,
	CE_PUBLISH_BAD_CAREER,
	CE_PUBLISH_NO_FOLDER,
	CE_PUBLISH_AUTHORITY
};

typedef bool (*CeCareerPublishPermitFn)(void *owner);
typedef bool (*CeCareerPublishNamePermitFn)(void *owner, const char *sourceName, const char *targetName);

struct CeCareerPublishRequest
{
	const char					*userDir;
	const char					*gameDir;
	const char					*packageDir;
	const char					*backupRoot;
	const char					*sourceName;
	const char					*targetName;
	const unsigned char			*bytes;
	size_t						size;
	CeCareerPublishPermitFn		permitted;
	CeCareerPublishPermitFn		cancelled;
	CeCareerPublishNamePermitFn	namePermitted;
	void						*owner;
};

struct CeCareerPublishResult
{
	CeCareerPublishStatus	status;
	char					backupSet[4352];
	char					newFile[80];
	int						possible;
};

// External .bes file picker (async SDL3 dialog, page-generation safe).
void	CeCareerImportPickEnter();
void	CeCareerImportPickLeave();
void	CeCareerImportPickOpen();
void	CeCareerImportPickPoll(CeImportPickStatus *out);

// Safe read-only validation of an external .bes file. Refuses symlinks/reparse points, hard links,
// files inside user/game/package, wrong size, wrong version, or implausible career data.
CeImportReadStatus	CeCareerImportRead(const char *filePath, const char *userDir, const char *gameDir,
									   const char *packageDir, unsigned char *bytesOut, size_t bytesCap,
									   char *leafOut, size_t leafCap, char *errorOut, size_t errorCap);

// Resolves the package directory (trimmed SDL_GetBasePath) for game pages without SDL headers.
void	CeCareerPublishPackageDir(char *out, size_t cap);

// Read-only suggestion of a clean non-colliding .bes filename within the native 31-character limit,
// checked against both save directories and validated through namePermitted.
CeCareerPublishStatus	CeCareerPublishSuggest(CeCareerPublishKind kind, const char *userDir,
											   const char *gameDir, const char *sourceName,
											   CeCareerPublishNamePermitFn namePermitted, void *owner,
											   char *targetOut, size_t targetCap);

// Synchronous non-destructive publish-as-new: takes a verified full backup set first via
// CeCareerTakeFullSet, then stages and publishes a brand-new .bes file without replacing anything.
CeCareerPublishResult	CeCareerPublishNew(const CeCareerPublishRequest *request);

#endif
