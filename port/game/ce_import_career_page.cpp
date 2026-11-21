// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_import_career_page.cpp: Extras > Import Career: pick an external .bes career file,
// inspect its missions, S-ranks, Goodies and kills, and copy it into saves as a brand-new career
// after taking a verified full-set backup via CeCareerTakeFullSet. Existing careers stay untouched.
#ifdef CE_VERSION

#include "common.h"
#include "ce_career_view.h"
#include "ce_extras.h"
#include "../platform/common/ce_career_backup.h"
#include "../platform/common/ce_career_publish.h"
#include "../platform/common/portlog.h"
#include "../platform/common/save_names.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int		CE_SettingGetText(const char *, char *, int);
extern "C" unsigned	PortPresentCount(void);
void				PortCeBackupWhen(char *out, size_t cap, const char *text, bool dateOnly);
const char			*PortUserDir();
const char			*PortGameDir();
bool				PortCeMenus();
bool				PortCeProjectCareerAllowed() noexcept;
bool				PortCeRestoreNamesCompatible(const char *sourceName, const char *targetName);

namespace
{
enum Phase
{
	PH_IDLE = 0,
	PH_WORKING,
	PH_DONE
};

enum Act
{
	A_NONE = 0,
	A_CHOOSE_FILE,
	A_CHOOSE_FOLDER,
	A_IMPORT,
	A_BACK,
	A_DONE
};

struct RowItem
{
	char	left[96];
	char	right[48];
	int		tone;
	int		act;
	bool	dim;
	bool	leftName;
	bool	rightName;
};

CeBackupStatus			sFolderView{};
CeImportPickStatus		sPickView{};
Phase					sPhase = PH_IDLE;
bool					sAllowed = false;
bool					sLoaded = false;
bool					sWorkRows = false;
bool					sDrawn = false;
unsigned				sDrawnAt = 0;
unsigned char			sImportBytes[CE_CAREER_SIZE];
SCeCareerView			sImportView{};
char					sLoadedPath[4096];
char					sSourceLeaf[256];
char					sTargetName[80];
char					sReadError[192];
char					sPackageDir[4096];
char					sFrozenFolder[4096];
CeCareerPublishStatus	sSuggestStatus = CE_PUBLISH_REFUSED;
CeCareerPublishResult	sResult{};
RowItem					sRows[12];
int						sRowCount = 0;

bool	permit()
{
	const char *r = getenv("BEA_PORT_RECORD"), *t = getenv("BEA_PORT_TAPE");
	return PortCeMenus() && PortCeProjectCareerAllowed() && PortCeExtrasActive() &&
		   (!r || !*r) && (!t || !*t);
}

bool	permitAdapter(void *)
{
	return permit();
}

bool	importNamePermitted(void *, const char *sourceName, const char *targetName)
{
	if (!sourceName || !targetName)
		return false;
	if (PortSaveNameUtf16(sourceName, nullptr, 0) == 0)
		return PortCeRestoreNamesCompatible(sourceName, targetName);
	// External filenames longer than the native 31-character save limit cannot match any native cheat name;
	// verify the shortened target name itself introduces no native cheat semantics.
	return PortCeRestoreNamesCompatible("Career.bes", targetName);
}

void	plain(WCHAR *dst, int n, const char *s)
{
	PortCeExtrasWiden(dst, n, s);
}

const char	*leafOf(const char *path)
{
	const char *l = path;
	for (const char *c = path; *c; ++c)
		if (*c == '/' || *c == '\\')
			l = c + 1;
	return l;
}

void	careerStem(char *out, size_t cap, const char *file)
{
	size_t n = strlen(file);
	if (n > 4 && file[n - 4] == '.' && (file[n - 3] | 0x20) == 'b' &&
		(file[n - 2] | 0x20) == 'e' && (file[n - 1] | 0x20) == 's')
		n -= 4;
	snprintf(out, cap, "%.*s", int(n), file);
}

void	thousands(int value, char *out, int n)
{
	char digits[16];
	snprintf(digits, sizeof(digits), "%d", value < 0 ? 0 : value);
	const int length = int(strlen(digits));
	int at = 0;
	for (int i = 0; (i < length) && (at < n - 2); ++i)
	{
		if (i && !((length - i) % 3))
			out[at++] = ',';
		out[at++] = digits[i];
	}
	out[at] = 0;
}

void	fitColumn(WCHAR *out, int cap, const char *text)
{
	WCHAR wide[160];
	PortCeExtrasWidenName(wide, 160, text);
	PortCeExtrasInFont(wide);
	int len = 0;
	while (wide[len])
		++len;
	if (len < cap)
	{
		for (int k = 0; k <= len; ++k)
			out[k] = wide[k];
	}
	else if (cap >= 4)
	{
		for (int k = 0; k < cap - 4; ++k)
			out[k] = wide[k];
		out[cap - 4] = L'.';
		out[cap - 3] = L'.';
		out[cap - 2] = L'.';
		out[cap - 1] = 0;
	}
	else if (cap > 0)
		out[0] = 0;
}

bool	picking()
{
	return sFolderView.state == CE_BACKUP_PICKING || sPickView.state == CE_IMPORT_PICK_OPEN;
}

RowItem	&addRow(const char *left, const char *right, int act = A_NONE)
{
	RowItem &r = sRows[sRowCount < 12 ? sRowCount++ : 11];
	memset(&r, 0, sizeof(r));
	snprintf(r.left, sizeof(r.left), "%s", left ? left : "");
	snprintf(r.right, sizeof(r.right), "%s", right ? right : "");
	r.act = act;
	return r;
}

void	outcomeWords(CeCareerPublishStatus status, const char *&message, const char *&detail)
{
	message = detail = "";
	switch (status)
	{
	case CE_PUBLISH_COMPLETE:
		message = "The career was imported as a new save.";
		detail = "Find it in Load Game. Existing careers were not changed.";
		return;
	case CE_PUBLISH_UNCERTAIN:
		message = "Not confirmed: the new career may already exist.";
		detail = "Check Load Game before trying again. Nothing was removed.";
		return;
	case CE_PUBLISH_OCCUPIED:
		message = "No career created: that name is already taken.";
		detail = "Existing careers were not changed. Try again for a free name.";
		return;
	case CE_PUBLISH_BUSY:
		message = "Another backup or save is in progress.";
		detail = "No career was created. Wait, then try again.";
		return;
	case CE_PUBLISH_CHANGED:
		message = "Your saves changed before the import finished.";
		detail = "No career was created. Check your saves and try again.";
		return;
	case CE_PUBLISH_UNSUPPORTED:
#ifdef _WIN32
		message = "This storage does not support safe career importing.";
		detail = "No career was created. Keep saves and backups on a local NTFS disk.";
#else
		message = "This storage is not safe for importing careers.";
		detail = "No career was created. Keep saves and backups on a local disk.";
#endif
		return;
	case CE_PUBLISH_BAD_NAME:
		message = "This career name cannot be imported safely.";
		detail = "The game treats it specially. No career was created.";
		return;
	case CE_PUBLISH_BAD_CAREER:
		message = "The chosen file is not a valid career.";
		detail = "No career was created. Existing careers stay untouched.";
		return;
	case CE_PUBLISH_NO_FOLDER:
		message = "Choose a backup folder first.";
		detail = "All existing careers are backed up before importing.";
		return;
	case CE_PUBLISH_AUTHORITY:
		message = "Careers cannot be imported right now.";
		detail = "Leave any editor, museum or recording first.";
		return;
	case CE_PUBLISH_CANCELLED:
		message = "Import cancelled. No career was created.";
		detail = "Existing careers stay untouched.";
		return;
	default:
		message = "The career could not be imported.";
		detail = "No career was created. Existing careers stay untouched.";
		return;
	}
}

void	refreshSuggestion()
{
	sTargetName[0] = 0;
	if (!sLoaded || !sSourceLeaf[0])
		return;
	sSuggestStatus = CeCareerPublishSuggest(CE_PUBLISH_IMPORT, PortUserDir(), PortGameDir(),
											sSourceLeaf, importNamePermitted, nullptr,
											sTargetName, sizeof(sTargetName));
}

void	tryLoadPicked()
{
	if (sPickView.state != CE_IMPORT_PICK_READY || !sPickView.path[0])
		return;
	if (strcmp(sPickView.path, sLoadedPath) == 0)
		return;
	snprintf(sLoadedPath, sizeof(sLoadedPath), "%s", sPickView.path);
	sLoaded = false;
	sSourceLeaf[0] = 0;
	sTargetName[0] = 0;
	sReadError[0] = 0;
	memset(&sImportView, 0, sizeof(sImportView));

	const CeImportReadStatus st = CeCareerImportRead(sPickView.path, PortUserDir(), PortGameDir(), sPackageDir,
													 sImportBytes, sizeof(sImportBytes),
													 sSourceLeaf, sizeof(sSourceLeaf),
													 sReadError, sizeof(sReadError));
	if (st == CE_IMPORT_READ_OK && CeCareerViewBuild(sImportBytes, CE_CAREER_SIZE, &sImportView) &&
		CeCareerViewPlausible(sImportView))
	{
		sLoaded = true;
		refreshSuggestion();
		fprintf(stderr, PORT_LOG_PREFIX "import career: loaded external file (%d of %d missions, %d Goodies)\n",
				sImportView.mMissionsComplete, sImportView.mMissionCount, sImportView.mGoodiesFound);
	}
}

void	buildRows()
{
	sRowCount = 0;
	if (!sAllowed)
	{
		addRow("Back", "", A_BACK);
		return;
	}
	if (sPhase == PH_WORKING)
	{
		addRow("Working", "Please wait").dim = true;
		char stem[80];
		careerStem(stem, sizeof(stem), sTargetName);
		RowItem &name = addRow(stem, "");
		name.leftName = true;
		name.dim = true;
		return;
	}
	if (sPhase == PH_DONE)
	{
		const bool made = sResult.status == CE_PUBLISH_COMPLETE;
		const bool unsure = sResult.status == CE_PUBLISH_UNCERTAIN;
		if (sResult.newFile[0])
		{
			char stem[80];
			careerStem(stem, sizeof(stem), sResult.newFile);
			RowItem &head = addRow(stem, made ? "Created" : unsure ? "Check" : "Refused");
			head.leftName = true;
			head.tone = made ? CE_TONE_GOOD : CE_TONE_BAD;
		}
		else
		{
			const char *tag = (sResult.status == CE_PUBLISH_OCCUPIED) ? "Taken"
							: (sResult.status == CE_PUBLISH_BUSY)	  ? "Busy"
																	  : "Refused";
			addRow("Not Imported", tag).tone = CE_TONE_BAD;
		}
		if (sResult.possible)
			addRow("Find in Load Game", "");
		if (sResult.backupSet[0])
		{
			char when[96], label[160];
			PortCeBackupWhen(when, sizeof(when), leafOf(sResult.backupSet), false);
			snprintf(label, sizeof(label), "Set %s", when);
			RowItem &setRow = addRow(label, "");
			setRow.leftName = true;
			setRow.tone = CE_TONE_DIM;
		}
		addRow("Existing Careers", unsure ? "Kept" : "Unchanged");
		addRow("Back", "", A_DONE);
		return;
	}

	// PH_IDLE
	char fileRight[80] = "None";
	if (sLoaded && sSourceLeaf[0])
		careerStem(fileRight, sizeof(fileRight), sSourceLeaf);
	else if (sPickView.state == CE_IMPORT_PICK_OPEN)
		snprintf(fileRight, sizeof(fileRight), "Choosing...");
	RowItem &chooseFile = addRow("Choose Career File", fileRight, A_CHOOSE_FILE);
	chooseFile.rightName = sLoaded;
	chooseFile.dim = picking();

	if (sLoaded)
	{
		char val[32];
		snprintf(val, sizeof(val), "%d of %d", sImportView.mMissionsComplete, sImportView.mMissionCount);
		addRow("Missions", val);
		snprintf(val, sizeof(val), "%d", sImportView.mRankS);
		addRow("S-Ranks", val);
		snprintf(val, sizeof(val), "%d of %d", sImportView.mGoodiesFound, int(CE_CAREER_GOODIES_SHOWN));
		addRow("Goodies", val);
		int totalKills = 0;
		for (int k = 0; k < CE_CAREER_KILL_CLASSES; ++k)
			totalKills += sImportView.mKills[k];
		thousands(totalKills, val, int(sizeof(val)));
		addRow("Kills", val);
	}

	const char *folderLeaf = sFolderView.folder[0] ? leafOf(sFolderView.folder) : "Not chosen";
	RowItem &folderRow = addRow("Backup Folder", folderLeaf);
	folderRow.rightName = sFolderView.folder[0] != 0;
	if (!sFolderView.folder[0])
		folderRow.tone = CE_TONE_DIM;

	addRow("Choose Backup Folder", "", A_CHOOSE_FOLDER).dim = picking();

	char newRight[80] = "-";
	if (sLoaded && sTargetName[0])
		careerStem(newRight, sizeof(newRight), sTargetName);
	else if (sLoaded)
		snprintf(newRight, sizeof(newRight), "Unavailable");
	RowItem &nameRow = addRow("New Name", newRight);
	nameRow.rightName = sLoaded && sTargetName[0];
	nameRow.dim = !sLoaded || !sTargetName[0];
	if (sLoaded && sTargetName[0])
		nameRow.tone = CE_TONE_GOOD;

	const bool canImport = sLoaded && sTargetName[0] && sFolderView.folder[0] &&
						   !picking() && sFolderView.state != CE_BACKUP_OTHER;
	addRow("Import as New", "", A_IMPORT).dim = !canImport;
	addRow("Back", "", A_BACK);
}

void	sync()
{
	CeBackupPoll(&sFolderView);
	CeCareerImportPickPoll(&sPickView);
	tryLoadPicked();
	buildRows();
}

void	openPage()
{
	sAllowed = permit();
	sPhase = PH_IDLE;
	sLoaded = false;
	sWorkRows = false;
	sDrawn = false;
	sDrawnAt = 0;
	sLoadedPath[0] = 0;
	sSourceLeaf[0] = 0;
	sTargetName[0] = 0;
	sReadError[0] = 0;
	sFrozenFolder[0] = 0;
	sSuggestStatus = CE_PUBLISH_REFUSED;
	memset(&sImportView, 0, sizeof(sImportView));
	memset(&sResult, 0, sizeof(sResult));
	CeCareerPublishPackageDir(sPackageDir, sizeof(sPackageDir));
	char folder[4096] = {};
	if (sAllowed)
		CE_SettingGetText("career_backup_folder", folder, sizeof(folder));
	CeBackupEnter(folder);
	CeCareerImportPickEnter();
	sync();
}

void	closePage()
{
	CeCareerImportPickLeave();
	CeBackupLeave();
	sPhase = PH_IDLE;
	sLoaded = false;
	sWorkRows = false;
	sDrawn = false;
	sRowCount = 0;
}

void	updatePage()
{
	if (sAllowed && !permit())
	{
		closePage();
		sAllowed = false;
	}
	if (sAllowed && sPhase == PH_WORKING && sDrawn && unsigned(PortPresentCount() - sDrawnAt) != 0u)
	{
		sDrawn = false;
		if (!permit())
		{
			memset(&sResult, 0, sizeof(sResult));
			sResult.status = CE_PUBLISH_AUTHORITY;
		}
		else
		{
			CeCareerPublishRequest req{};
			req.userDir = PortUserDir();
			req.gameDir = PortGameDir();
			req.packageDir = sPackageDir;
			req.backupRoot = sFrozenFolder;
			req.sourceName = sSourceLeaf;
			req.targetName = sTargetName;
			req.bytes = sImportBytes;
			req.size = CE_CAREER_SIZE;
			req.permitted = permitAdapter;
			req.cancelled = nullptr;
			req.namePermitted = importNamePermitted;
			req.owner = nullptr;
			sResult = CeCareerPublishNew(&req);
		}
		fprintf(stderr, PORT_LOG_PREFIX "import career: outcome %d\n", int(sResult.status));
		sPhase = PH_DONE;
	}
	sync();
}

void	drawPage(float)
{
	if (sAllowed && sPhase == PH_WORKING && sWorkRows && !sDrawn)
	{
		sDrawn = true;
		sDrawnAt = PortPresentCount();
	}
	sWorkRows = false;
}

int	rowCount()
{
	return sRowCount;
}

void	fillRow(int i, SCeListRow *out)
{
	if (i < 0 || i >= sRowCount)
		return;
	const RowItem &r = sRows[i];
	if (r.leftName)
		fitColumn(out->mLeft, 80, r.left);
	else
		plain(out->mLeft, 80, r.left);
	if (r.rightName)
		fitColumn(out->mRight, 24, r.right);
	else
		plain(out->mRight, 24, r.right);
	out->mTone = r.tone;
	out->mDim = r.dim;
	if (sPhase == PH_WORKING)
		sWorkRows = true;
}

void	fillHint(int i, WCHAR *help, WCHAR *extra, int n)
{
	if (!sAllowed)
	{
		plain(help, n, "Importing careers is unavailable in this session.");
		plain(extra, n, "Nothing was written.");
		return;
	}
	if (sPhase == PH_WORKING)
	{
		plain(help, n, "Backing up your careers first.");
		plain(extra, n, "Then the new career is added. Existing careers stay untouched.");
		return;
	}
	if (sPhase == PH_DONE)
	{
		const char *msg = "", *det = "";
		outcomeWords(sResult.status, msg, det);
		plain(help, n, msg);
		plain(extra, n, det);
		return;
	}
	if (picking())
	{
		plain(help, n, sPickView.state == CE_IMPORT_PICK_OPEN ? sPickView.message : sFolderView.message);
		plain(extra, n, "You can go back at any time.");
		return;
	}
	if (i < 0 || i >= sRowCount)
		return;
	const RowItem &r = sRows[i];
	if (r.act == A_CHOOSE_FILE)
	{
		if (sReadError[0])
		{
			plain(help, n, sReadError);
			plain(extra, n, "Choose a valid 10,004-byte .bes career file.");
		}
		else if (sPickView.state == CE_IMPORT_PICK_ERROR)
		{
			plain(help, n, sPickView.message);
			plain(extra, n, "Select to choose a local .bes career file.");
		}
		else
		{
			plain(help, n, "Choose an external .bes career file to inspect and import.");
			plain(extra, n, "The chosen file is only read. Nothing is changed yet.");
		}
	}
	else if (r.act == A_CHOOSE_FOLDER)
	{
		plain(help, n, "Choose a folder outside your game and careers.");
		plain(extra, n, sFolderView.state == CE_BACKUP_ERROR ? sFolderView.message
															 : "All existing careers are backed up here first.");
	}
	else if (r.act == A_IMPORT)
	{
		if (!sLoaded)
		{
			plain(help, n, "Choose a valid career file first.");
			plain(extra, n, "No career has been written.");
		}
		else if (!sFolderView.folder[0])
		{
			plain(help, n, "Choose a backup folder first.");
			plain(extra, n, "All existing careers are backed up before importing.");
		}
		else if (!sTargetName[0])
		{
			plain(help, n, sSuggestStatus == CE_PUBLISH_BAD_NAME ? "This career name cannot be imported safely."
																 : "No free career name could be found.");
			plain(extra, n, "No career has been written.");
		}
		else
		{
			char line[192];
			snprintf(line, sizeof(line), "Adds %s after backing up all existing careers.", sTargetName);
			PortCeExtrasWidenName(help, n, line);
			PortCeExtrasInFont(help);
			plain(extra, n, "Existing careers are never replaced or changed.");
		}
	}
	else if (r.act == A_BACK)
	{
		plain(help, n, "Return to Extras without importing a career.");
	}
	else if (!strcmp(r.left, "Backup Folder"))
	{
		plain(help, n, "Your pre-import full backup goes into this folder.");
		plain(extra, n, "Choose Backup Folder changes it.");
	}
	else if (!strcmp(r.left, "New Name"))
	{
		if (sLoaded && sTargetName[0])
		{
			char line[192];
			snprintf(line, sizeof(line), "Will be saved as %s.", sTargetName);
			PortCeExtrasWidenName(help, n, line);
			PortCeExtrasInFont(help);
			plain(extra, n, "Existing careers stay untouched.");
		}
		else
		{
			plain(help, n, "Shows the new career filename once a file is chosen.");
		}
	}
	else if (!strcmp(r.left, "Kills"))
	{
		char detail[192];
		snprintf(detail, sizeof(detail), "Air %d, Veh %d, Emp %d, Inf %d, Mech %d.",
				 sImportView.mKills[0], sImportView.mKills[1], sImportView.mKills[2],
				 sImportView.mKills[3], sImportView.mKills[4]);
		plain(help, n, "Total enemies defeated in the selected career.");
		plain(extra, n, detail);
	}
	else
	{
		plain(help, n, "Read from the selected external career file.");
		plain(extra, n, "Nothing has been written to your saves yet.");
	}
}

bool	selectRow(int i)
{
	if (i < 0 || i >= sRowCount)
		return false;
	const int act = sRows[i].act;
	if (act == A_BACK)
	{
		PortCeImportCareerBack();
		return true;
	}
	if (act == A_DONE)
	{
		sPhase = PH_IDLE;
		PortCeImportCareerBack();
		return true;
	}
	if (!sAllowed || sPhase != PH_IDLE || picking())
		return false;
	if (act == A_CHOOSE_FILE)
	{
		sLoadedPath[0] = 0;
		CeCareerImportPickOpen();
		sync();
		return true;
	}
	if (act == A_CHOOSE_FOLDER)
	{
		CeBackupChoose();
		sync();
		return true;
	}
	if (act == A_IMPORT)
	{
		CeBackupPoll(&sFolderView);
		if (!sLoaded)
			return false;
		refreshSuggestion();
		if (!sFolderView.folder[0])
		{
			memset(&sResult, 0, sizeof(sResult));
			sResult.status = CE_PUBLISH_NO_FOLDER;
			sPhase = PH_DONE;
			sync();
			return true;
		}
		if (sFolderView.state == CE_BACKUP_OTHER)
		{
			memset(&sResult, 0, sizeof(sResult));
			sResult.status = CE_PUBLISH_BUSY;
			sPhase = PH_DONE;
			sync();
			return true;
		}
		if (!sTargetName[0])
		{
			memset(&sResult, 0, sizeof(sResult));
			sResult.status = sSuggestStatus;
			sPhase = PH_DONE;
			sync();
			return true;
		}
		snprintf(sFrozenFolder, sizeof(sFrozenFolder), "%s", sFolderView.folder);
		memset(&sResult, 0, sizeof(sResult));
		sPhase = PH_WORKING;
		sWorkRows = false;
		sDrawn = false;
		sDrawnAt = 0;
		sync();
		return true;
	}
	return false;
}

const WCHAR	*emptyText()
{
	return L"";
}

const SCeListPage	kPage = {L"Import Career", rowCount, fillRow, fillHint, selectRow, emptyText,
							 openPage, closePage, updatePage, drawPage, 0.78f, 0};
} // namespace

const SCeListPage	*PortCeImportCareerPage()
{
	return &kPage;
}

int	PortCeImportCareerStepBack()
{
	if (!sAllowed)
		return 0;
	if (sPhase == PH_WORKING)
		return -1;
	if (sPhase == PH_DONE)
	{
		sPhase = PH_IDLE;
		refreshSuggestion();
		sync();
		return 1;
	}
	return 0;
}

#endif
