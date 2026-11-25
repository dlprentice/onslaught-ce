// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_career_edit_page.cpp: Extras > Service Record > [Career] > Edit Career.
//
// Edits a 10,004-byte in-memory copy of the selected career (Goodies, Missions/Ranks, Kills by
// class, and Saved Settings) and publishes it as a brand-new .bes file behind CeCareerTakeFullSet.
// The original career file is never altered or overwritten.
#ifdef CE_VERSION

#include "common.h"
#include "ce_career_edit.h"
#include "ce_career_view.h"
#include "ce_extras.h"
#include "ce_name_text.h"
#include "../platform/common/ce_backup_name.h"
#include "../platform/common/ce_career_backup.h"
#include "../platform/common/ce_career_publish.h"
#include "../platform/common/portlog.h"

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

enum RowId
{
	R_GOODIES = 0,
	R_MISSIONS,
	R_KILLS_AIR,
	R_KILLS_VEH,
	R_KILLS_EMP,
	R_KILLS_INF,
	R_KILLS_MECH,
	R_SOUND,
	R_MUSIC,
	R_P1_GOD,
	R_P2_GOD,
	R_FOLDER,
	R_CHOOSE_FOLDER,
	R_SAVE_AS,
	R_SAVE_NEW,
	R_BACK,
	R_COUNT
};

struct DoneRow
{
	char	left[96];
	char	right[32];
	int		tone;
	bool	dim;
	bool	leftName;
	bool	isBack;
};

CeBackupStatus			sFolderView{};
Phase					sPhase = PH_IDLE;
Phase					sShownPhase = PH_IDLE;
bool					sAllowed = false;
bool					sValidSource = false;
bool					sHiddenDirection = false;
bool					sPreviewValid = false;
bool					sReshow = false;
bool					sWorkRows = false;
bool					sDrawn = false;
unsigned				sDrawnAt = 0;
unsigned char			sOrigBytes[CE_CAREER_SIZE];
unsigned char			sPreviewBytes[CE_CAREER_SIZE];
unsigned char			sFrozenBytes[CE_CAREER_SIZE];
SCeCareerView			sOrigView{};
SCeCareerView			sPreviewView{};
SCeCareerEdits			sEdits{};
char					sSourceLeaf[256];
WCHAR					sSourceDisplay[256];
char					sTargetName[80];
char					sPackageDir[4096];
char					sFrozenFolder[4096];
CeCareerPublishStatus	sSuggestStatus = CE_PUBLISH_REFUSED;
CeCareerPublishResult	sResult{};
DoneRow					sDoneRows[8];
int						sDoneCount = 0;

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

bool	editNamePermitted(void *, const char *sourceName, const char *targetName)
{
	return sourceName && targetName && PortCeRestoreNamesCompatible(sourceName, targetName);
}

void	plain(WCHAR *dst, int n, const char *s)
{
	PortCeExtrasWiden(dst, n, s);
}

void	widenUtf8(WCHAR *dst, int n, const char *s)
{
	CeDecodeName(dst, n, s, true);
}

void	leafOf(char *out, size_t cap, const char *path)
{
	if (!cap)
		return;
	out[0] = 0;
	if (!path || !*path)
		return;
	size_t end = strlen(path);
	while (end > 1 && (path[end - 1] == '/' || path[end - 1] == '\\'))
	{
		if (end == 3 && path[1] == ':')
			break;
		--end;
	}
	if ((end == 1 && (path[0] == '/' || path[0] == '\\')) ||
		(end == 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')))
	{
		snprintf(out, cap, "%.*s", int(end), path);
		return;
	}
	size_t start = end;
	while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\')
		--start;
	if (start == end)
	{
		snprintf(out, cap, "%c", path[0]);
		return;
	}
	snprintf(out, cap, "%.*s", int(end - start), path + start);
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
	widenUtf8(wide, 160, text);
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
	return sFolderView.state == CE_BACKUP_PICKING;
}

void	rebuildPreview()
{
	sPreviewValid = sValidSource &&
					CeCareerEditApply(sOrigBytes, CE_CAREER_SIZE, sEdits, sPreviewBytes, &sPreviewView);
}

void	refreshSuggestion()
{
	sTargetName[0] = 0;
	if (!sValidSource || !sSourceLeaf[0])
		return;
	sSuggestStatus = CeCareerPublishSuggest(CE_PUBLISH_EDIT, PortUserDir(), PortGameDir(),
											sSourceLeaf, editNamePermitted, nullptr,
											sTargetName, sizeof(sTargetName));
}

void	outcomeWords(CeCareerPublishStatus status, const char *&message, const char *&detail)
{
	message = detail = "";
	switch (status)
	{
	case CE_PUBLISH_COMPLETE:
		message = "Saved as a brand-new career.";
		detail = "Find it in Load Game. Your original career was not changed.";
		return;
	case CE_PUBLISH_UNCERTAIN:
		message = "Not confirmed: the new career may already exist.";
		detail = "Check Load Game before trying again. Nothing was removed.";
		return;
	case CE_PUBLISH_OCCUPIED:
		message = "No career created: that name is already taken.";
		detail = "Your original career is unchanged. Go back and try again.";
		return;
	case CE_PUBLISH_BUSY:
		message = "Another backup or save is in progress.";
		detail = "No career was created. Wait, then try again.";
		return;
	case CE_PUBLISH_CHANGED:
		message = "Your saves changed before saving finished.";
		detail = "No career was created. Your original career is unchanged.";
		return;
	case CE_PUBLISH_UNSUPPORTED:
#ifdef _WIN32
		message = "This storage does not support safe career saving.";
		detail = "No career was created. Keep saves and backups on a local NTFS disk.";
#else
		message = "This storage is not safe for saving an edited career.";
		detail = "No career was created. Keep saves and backups on a local disk.";
#endif
		return;
	case CE_PUBLISH_BAD_NAME:
		message = "This career name is treated specially by the game.";
		detail = "A renamed copy would not behave the same. Nothing was written.";
		return;
	case CE_PUBLISH_BAD_CAREER:
		message = "The edited career could not be verified.";
		detail = "No career was created. Your original career is unchanged.";
		return;
	case CE_PUBLISH_NO_FOLDER:
		message = "Choose a backup folder first.";
		detail = "All careers are backed up before saving a new career.";
		return;
	case CE_PUBLISH_AUTHORITY:
		message = "Careers cannot be edited right now.";
		detail = "Leave any editor, museum or recording first.";
		return;
	case CE_PUBLISH_CANCELLED:
		message = "Cancelled. No career was created.";
		detail = "Your original career is unchanged.";
		return;
	default:
		message = "The edited career could not be saved.";
		detail = "No career was created. Your original career is unchanged.";
		return;
	}
}

DoneRow	&addDone(const char *left, const char *right, bool isBack = false)
{
	DoneRow &r = sDoneRows[sDoneCount < 8 ? sDoneCount++ : 7];
	memset(&r, 0, sizeof(r));
	snprintf(r.left, sizeof(r.left), "%s", left ? left : "");
	snprintf(r.right, sizeof(r.right), "%s", right ? right : "");
	r.isBack = isBack;
	return r;
}

void	buildDoneRows()
{
	sDoneCount = 0;
	if (sPhase == PH_WORKING)
	{
		addDone("Working", "Please wait").dim = true;
		char stem[80];
		careerStem(stem, sizeof(stem), sTargetName);
		DoneRow &name = addDone(stem, "");
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
			DoneRow &head = addDone(stem, made ? "Created" : unsure ? "Check" : "Refused");
			head.leftName = true;
			head.tone = made ? CE_TONE_GOOD : CE_TONE_BAD;
		}
		else
		{
			const char *tag = (sResult.status == CE_PUBLISH_OCCUPIED) ? "Taken"
							: (sResult.status == CE_PUBLISH_BUSY)	  ? "Busy"
																	  : "Refused";
			addDone("Not Saved", tag).tone = CE_TONE_BAD;
		}
		if (sResult.possible)
			addDone("Find in Load Game", "");
		if (sResult.backupSet[0])
		{
			char leaf[256], when[96], label[160];
			leafOf(leaf, sizeof(leaf), sResult.backupSet);
			PortCeBackupWhen(when, sizeof(when), leaf, false);
			snprintf(label, sizeof(label), "Set %s", when);
			DoneRow &setRow = addDone(label, "");
			setRow.leftName = true;
			setRow.tone = CE_TONE_DIM;
		}
		addDone("Original Career", unsure ? "Kept" : "Unchanged");
		addDone("Back", "", true);
	}
}

void	focus(int row)
{
	sReshow = true;
	PortCeExtrasShow(XP_EDIT_CAREER, row);
	sReshow = false;
}

void	syncRows()
{
	buildDoneRows();
	if (sPhase != sShownPhase)
	{
		sShownPhase = sPhase;
		if (sPhase == PH_DONE && sDoneCount > 0)
			focus(sDoneCount - 1);
		else if (sPhase == PH_IDLE)
			focus(0);
	}
}

void	openPage()
{
	if (sReshow)
		return;
	sAllowed = permit();
	sPhase = PH_IDLE;
	sShownPhase = PH_IDLE;
	sValidSource = false;
	sHiddenDirection = false;
	sPreviewValid = false;
	sWorkRows = false;
	sDrawn = false;
	sDrawnAt = 0;
	sSourceLeaf[0] = 0;
	sSourceDisplay[0] = 0;
	sTargetName[0] = 0;
	sFrozenFolder[0] = 0;
	sSuggestStatus = CE_PUBLISH_REFUSED;
	memset(&sOrigView, 0, sizeof(sOrigView));
	memset(&sPreviewView, 0, sizeof(sPreviewView));
	memset(&sEdits, 0, sizeof(sEdits));
	memset(&sResult, 0, sizeof(sResult));
	CeCareerPublishPackageDir(sPackageDir, sizeof(sPackageDir));

	const WCHAR *source = PortCeBackupCareerName();
	const unsigned char *snap = PortCeBackupSnapshot();
	const bool resolved = source && CeBackupSelectedName(source, sSourceLeaf, sizeof(sSourceLeaf), sSourceDisplay);
	sHiddenDirection = (source && CeBackupNameHasDirectionMark(source)) ||
					   (resolved && CeBackupNameHasDirectionMark(sSourceDisplay));
	if (sAllowed && resolved && !sHiddenDirection && snap &&
		CeCareerViewBuild(snap, CE_CAREER_SIZE, &sOrigView) && CeCareerViewPlausible(sOrigView))
	{
		memcpy(sOrigBytes, snap, CE_CAREER_SIZE);
		CeCareerEditInit(&sEdits, sOrigView);
		sValidSource = true;
		rebuildPreview();
		refreshSuggestion();
	}

	char folder[4096] = {};
	if (sAllowed && sValidSource)
		CE_SettingGetText("career_backup_folder", folder, sizeof(folder));
	CeBackupEnter(folder);
	CeBackupPoll(&sFolderView);
	buildDoneRows();
}

void	closePage()
{
	CeBackupLeave();
	sPhase = PH_IDLE;
	sShownPhase = PH_IDLE;
	sValidSource = false;
	sPreviewValid = false;
	sWorkRows = false;
	sDrawn = false;
	sDoneCount = 0;
}

void	updatePage()
{
	if (sAllowed && !permit())
	{
		closePage();
		sAllowed = false;
	}
	if (sAllowed)
		CeBackupPoll(&sFolderView);
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
			req.bytes = sFrozenBytes;
			req.size = CE_CAREER_SIZE;
			req.permitted = permitAdapter;
			req.cancelled = nullptr;
			req.namePermitted = editNamePermitted;
			req.owner = nullptr;
			sResult = CeCareerPublishNew(&req);
			if (sResult.status == CE_PUBLISH_COMPLETE || sResult.possible)
				PortCeRecordRefreshCareers();
		}
		fprintf(stderr, PORT_LOG_PREFIX "edit career: outcome %d\n", int(sResult.status));
		sPhase = PH_DONE;
	}
	syncRows();
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
	if (!sAllowed || !sValidSource)
		return 1;
	if (sPhase != PH_IDLE)
		return sDoneCount;
	return R_COUNT;
}

const char	*goodieLabel(ECeGoodieEdit mode)
{
	switch (mode)
	{
	case CE_EDIT_GOODIES_UNLOCK_ALL:	return "Unlock All";
	case CE_EDIT_GOODIES_LOCK_ALL:		return "Lock All";
	default:							return "Unchanged";
	}
}

const char	*missionLabel(ECeMissionEdit mode)
{
	switch (mode)
	{
	case CE_EDIT_MISSIONS_UNLOCK_ALL:		return "Unlock All";
	case CE_EDIT_MISSIONS_SRANK_COMPLETED:	return "S-Rank Done";
	case CE_EDIT_MISSIONS_SRANK_ALL:		return "S-Rank All";
	default:								return "Unchanged";
	}
}

void	fillRow(int i, SCeListRow *out)
{
	if (!sAllowed || !sValidSource)
	{
		plain(out->mLeft, 80, "Back");
		return;
	}
	if (sPhase != PH_IDLE)
	{
		if (i < 0 || i >= sDoneCount)
			return;
		const DoneRow &r = sDoneRows[i];
		if (r.leftName)
			fitColumn(out->mLeft, 80, r.left);
		else
			plain(out->mLeft, 80, r.left);
		plain(out->mRight, 24, r.right);
		out->mTone = r.tone;
		out->mDim = r.dim;
		if (sPhase == PH_WORKING)
			sWorkRows = true;
		return;
	}

	static const char *const kKillLabels[CE_CAREER_KILL_CLASSES] = {
		"Aircraft Kills", "Vehicle Kills", "Emplacement Kills", "Infantry Kills", "Mech Kills"
	};
	char val[48] = "";
	switch (i)
	{
	case R_GOODIES:
		plain(out->mLeft, 80, "Goodies");
		plain(out->mRight, 24, goodieLabel(sEdits.mGoodies));
		if (sEdits.mGoodies != CE_EDIT_GOODIES_UNCHANGED)
			out->mTone = CE_TONE_GOOD;
		return;
	case R_MISSIONS:
		plain(out->mLeft, 80, "Missions");
		plain(out->mRight, 24, missionLabel(sEdits.mMissions));
		if (sEdits.mMissions != CE_EDIT_MISSIONS_UNCHANGED)
			out->mTone = CE_TONE_GOOD;
		return;
	case R_KILLS_AIR:
	case R_KILLS_VEH:
	case R_KILLS_EMP:
	case R_KILLS_INF:
	case R_KILLS_MECH:
	{
		const int k = i - R_KILLS_AIR;
		plain(out->mLeft, 80, kKillLabels[k]);
		thousands(sEdits.mKills[k], val, int(sizeof(val)));
		plain(out->mRight, 24, val);
		if (sEdits.mKillsChanged[k])
			out->mTone = CE_TONE_GOOD;
		return;
	}
	case R_SOUND:
		plain(out->mLeft, 80, "Sound Volume");
		if (sEdits.mSoundPercent < 0)
			plain(out->mRight, 24, "Unknown");
		else
		{
			snprintf(val, sizeof(val), "%d%%", sEdits.mSoundPercent);
			plain(out->mRight, 24, val);
		}
		if (sEdits.mSoundChanged)
			out->mTone = CE_TONE_GOOD;
		return;
	case R_MUSIC:
		plain(out->mLeft, 80, "Music Volume");
		if (sEdits.mMusicPercent < 0)
			plain(out->mRight, 24, "Unknown");
		else
		{
			snprintf(val, sizeof(val), "%d%%", sEdits.mMusicPercent);
			plain(out->mRight, 24, val);
		}
		if (sEdits.mMusicChanged)
			out->mTone = CE_TONE_GOOD;
		return;
	case R_P1_GOD:
		plain(out->mLeft, 80, "P1 Invincible");
		plain(out->mRight, 24, sEdits.mInvincible[0] ? "On" : "Off");
		if (sEdits.mInvincibleChanged[0])
			out->mTone = CE_TONE_GOOD;
		return;
	case R_P2_GOD:
		plain(out->mLeft, 80, "P2 Invincible");
		plain(out->mRight, 24, sEdits.mInvincible[1] ? "On" : "Off");
		if (sEdits.mInvincibleChanged[1])
			out->mTone = CE_TONE_GOOD;
		return;
	case R_FOLDER:
		plain(out->mLeft, 80, "Backup Folder");
		if (sFolderView.folder[0])
		{
			char leaf[256];
			leafOf(leaf, sizeof(leaf), sFolderView.folder);
			fitColumn(out->mRight, 24, leaf[0] ? leaf : sFolderView.folder);
		}
		else
		{
			plain(out->mRight, 24, "Not chosen");
			out->mTone = CE_TONE_DIM;
		}
		return;
	case R_CHOOSE_FOLDER:
		plain(out->mLeft, 80, "Choose Backup Folder");
		out->mDim = picking();
		return;
	case R_SAVE_AS:
		plain(out->mLeft, 80, "New Name");
		if (sTargetName[0])
		{
			char stem[80];
			careerStem(stem, sizeof(stem), sTargetName);
			fitColumn(out->mRight, 24, stem);
			out->mTone = CE_TONE_GOOD;
		}
		else
		{
			plain(out->mRight, 24, "Unavailable");
			out->mDim = true;
		}
		return;
	case R_SAVE_NEW:
	{
		plain(out->mLeft, 80, "Save as New Career");
		const bool ready = sPreviewValid && sTargetName[0] && sFolderView.folder[0] &&
						   !picking() && sFolderView.state != CE_BACKUP_OTHER;
		out->mDim = !ready;
		return;
	}
	case R_BACK:
		plain(out->mLeft, 80, "Back");
		return;
	}
}

void	fillHint(int i, WCHAR *help, WCHAR *extra, int n)
{
	if (!sAllowed)
	{
		plain(help, n, "Editing careers is unavailable in this session.");
		plain(extra, n, "Nothing was written.");
		return;
	}
	if (sHiddenDirection)
	{
		plain(help, n, CeBackupDirectionReason());
		plain(extra, n, "This career cannot be edited with this name.");
		return;
	}
	if (!sValidSource)
	{
		plain(help, n, "This career could not be verified in your saves.");
		plain(extra, n, "Return to the list and open it again.");
		return;
	}
	if (sPhase == PH_WORKING)
	{
		plain(help, n, "Backing up all careers first.");
		plain(extra, n, "Then the new edited career is written. Your original stays untouched.");
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
		plain(help, n, sFolderView.message);
		plain(extra, n, "You can go back at any time.");
		return;
	}

	char line[192];
	switch (i)
	{
	case R_GOODIES:
		if (sEdits.mGoodies == CE_EDIT_GOODIES_UNLOCK_ALL)
			plain(help, n, "Sets all 230 Goodies to viewed and clears pending popups.");
		else if (sEdits.mGoodies == CE_EDIT_GOODIES_LOCK_ALL)
			plain(help, n, "Locks all 230 Goodies and clears pending popups.");
		else
			plain(help, n, "Keep Goodies as saved. Left, Right or Select cycles options.");
		snprintf(line, sizeof(line), "Preview: %d of %d Goodies found.",
				 sPreviewView.mGoodiesFound, int(CE_CAREER_GOODIES_SHOWN));
		plain(extra, n, line);
		return;
	case R_MISSIONS:
		if (sEdits.mMissions == CE_EDIT_MISSIONS_UNLOCK_ALL)
			plain(help, n, "Opens every route and campaign mission while keeping ranks.");
		else if (sEdits.mMissions == CE_EDIT_MISSIONS_SRANK_COMPLETED)
			plain(help, n, "Sets rank S on every completed mission.");
		else if (sEdits.mMissions == CE_EDIT_MISSIONS_SRANK_ALL)
			plain(help, n, "Completes and S-ranks every campaign mission and route.");
		else
			plain(help, n, "Keep missions as saved. Left, Right or Select cycles options.");
		snprintf(line, sizeof(line), "Preview: %d of %d complete, %d rank S.",
				 sPreviewView.mMissionsComplete, sPreviewView.mMissionCount, sPreviewView.mRankS);
		plain(extra, n, line);
		return;
	case R_KILLS_AIR:
	case R_KILLS_VEH:
	case R_KILLS_EMP:
	case R_KILLS_INF:
	case R_KILLS_MECH:
	{
		const int k = i - R_KILLS_AIR;
		char origNum[32];
		thousands(sOrigView.mKills[k], origNum, int(sizeof(origNum)));
		plain(help, n, "Left or Right steps by 100. Select cycles presets.");
		snprintf(line, sizeof(line), "Original count: %s. Clamped to 999,999.", origNum);
		plain(extra, n, line);
		return;
	}
	case R_SOUND:
		plain(help, n, "Saved sound volume in 10 percent steps.");
		plain(extra, n, "Left or Right adjusts. Select cycles.");
		return;
	case R_MUSIC:
		plain(help, n, "Saved music volume in 10 percent steps.");
		plain(extra, n, "Left or Right adjusts. Select cycles.");
		return;
	case R_P1_GOD:
	case R_P2_GOD:
		plain(help, n, "Toggle saved player invincibility for this career.");
		plain(extra, n, "Left, Right or Select switches between Off and On.");
		return;
	case R_FOLDER:
		plain(help, n, "All careers are backed up here before saving.");
		plain(extra, n, "Choose Backup Folder changes it.");
		return;
	case R_CHOOSE_FOLDER:
		plain(help, n, "Choose a folder outside your game and careers.");
		plain(extra, n, sFolderView.state == CE_BACKUP_ERROR ? sFolderView.message
															 : "Shared with Back Up Career.");
		return;
	case R_SAVE_AS:
		if (sTargetName[0])
		{
			snprintf(line, sizeof(line), "New file: %s.", sTargetName);
			widenUtf8(help, n, line);
			PortCeExtrasInFont(help);
			plain(extra, n, "Select Save as New Career below to write it.");
		}
		else
		{
			plain(help, n, sSuggestStatus == CE_PUBLISH_BAD_NAME ? "This career name is treated specially by the game."
																 : "No free filename could be found.");
			plain(extra, n, "Your original career is untouched.");
		}
		return;
	case R_SAVE_NEW:
		if (!sFolderView.folder[0])
		{
			plain(help, n, "Choose a backup folder first.");
			plain(extra, n, "All careers are backed up before saving.");
		}
		else if (!sTargetName[0])
		{
			plain(help, n, "No valid new career name is available.");
			plain(extra, n, "Your original career stays untouched.");
		}
		else
		{
			snprintf(line, sizeof(line), "Saves edits into %s after backing up all careers.", sTargetName);
			widenUtf8(help, n, line);
			PortCeExtrasInFont(help);
			plain(extra, n, "Your original career is never changed.");
		}
		return;
	case R_BACK:
		plain(help, n, "Return to Service Record without saving.");
		plain(extra, n, "No file has been written.");
		return;
	}
}

int	adjustRow(int i, int delta)
{
	if (!sAllowed || !sValidSource || sPhase != PH_IDLE || picking())
		return 0;
	switch (i)
	{
	case R_GOODIES:
		sEdits.mGoodies = ECeGoodieEdit((int(sEdits.mGoodies) + delta + int(CE_EDIT_GOODIES_COUNT)) %
										int(CE_EDIT_GOODIES_COUNT));
		rebuildPreview();
		return 1;
	case R_MISSIONS:
		sEdits.mMissions = ECeMissionEdit((int(sEdits.mMissions) + delta + int(CE_EDIT_MISSIONS_COUNT)) %
										  int(CE_EDIT_MISSIONS_COUNT));
		rebuildPreview();
		return 1;
	case R_KILLS_AIR:
	case R_KILLS_VEH:
	case R_KILLS_EMP:
	case R_KILLS_INF:
	case R_KILLS_MECH:
	{
		const int k = i - R_KILLS_AIR;
		const int next = CeCareerEditStepKills(sEdits.mKills[k], delta);
		if (next == sEdits.mKills[k])
			return -1;
		sEdits.mKills[k] = next;
		sEdits.mKillsChanged[k] = (sEdits.mKills[k] != CeCareerEditClampKills(sOrigView.mKills[k]));
		rebuildPreview();
		return 1;
	}
	case R_SOUND:
	{
		const int next = CeCareerEditStepVolume(sEdits.mSoundPercent, delta, false);
		if (next == sEdits.mSoundPercent)
			return -1;
		const int origSound = CeStoredVolumePercent(sOrigView.mSoundBits);
		sEdits.mSoundPercent = next;
		sEdits.mSoundChanged = (origSound < 0) || (sEdits.mSoundPercent != origSound);
		rebuildPreview();
		return 1;
	}
	case R_MUSIC:
	{
		const int next = CeCareerEditStepVolume(sEdits.mMusicPercent, delta, false);
		if (next == sEdits.mMusicPercent)
			return -1;
		const int origMusic = CeStoredVolumePercent(sOrigView.mMusicBits);
		sEdits.mMusicPercent = next;
		sEdits.mMusicChanged = (origMusic < 0) || (sEdits.mMusicPercent != origMusic);
		rebuildPreview();
		return 1;
	}
	case R_P1_GOD:
	case R_P2_GOD:
	{
		const int p = i - R_P1_GOD;
		sEdits.mInvincible[p] = !sEdits.mInvincible[p];
		sEdits.mInvincibleChanged[p] = (sEdits.mInvincible[p] != sOrigView.mInvincible[p]);
		rebuildPreview();
		return 1;
	}
	default:
		return 0;
	}
}

bool	selectRow(int i)
{
	if (!sAllowed || !sValidSource)
	{
		PortCeCareerEditBack();
		return true;
	}
	if (sPhase == PH_WORKING)
		return false;
	if (sPhase == PH_DONE)
	{
		if (i >= 0 && i < sDoneCount && sDoneRows[i].isBack)
		{
			sPhase = PH_IDLE;
			refreshSuggestion();
			syncRows();
			return true;
		}
		return false;
	}
	if (i == R_BACK)
	{
		PortCeCareerEditBack();
		return true;
	}
	if (picking())
		return false;
	switch (i)
	{
	case R_GOODIES:
	case R_MISSIONS:
	case R_P1_GOD:
	case R_P2_GOD:
		return adjustRow(i, 1) > 0;
	case R_KILLS_AIR:
	case R_KILLS_VEH:
	case R_KILLS_EMP:
	case R_KILLS_INF:
	case R_KILLS_MECH:
	{
		const int k = i - R_KILLS_AIR;
		sEdits.mKills[k] = CeCareerEditCycleKillsPreset(sEdits.mKills[k]);
		sEdits.mKillsChanged[k] = (sEdits.mKills[k] != CeCareerEditClampKills(sOrigView.mKills[k]));
		rebuildPreview();
		return true;
	}
	case R_SOUND:
	{
		const int origSound = CeStoredVolumePercent(sOrigView.mSoundBits);
		sEdits.mSoundPercent = CeCareerEditStepVolume(sEdits.mSoundPercent, 1, true);
		sEdits.mSoundChanged = (origSound < 0) || (sEdits.mSoundPercent != origSound);
		rebuildPreview();
		return true;
	}
	case R_MUSIC:
	{
		const int origMusic = CeStoredVolumePercent(sOrigView.mMusicBits);
		sEdits.mMusicPercent = CeCareerEditStepVolume(sEdits.mMusicPercent, 1, true);
		sEdits.mMusicChanged = (origMusic < 0) || (sEdits.mMusicPercent != origMusic);
		rebuildPreview();
		return true;
	}
	case R_CHOOSE_FOLDER:
		CeBackupChoose();
		CeBackupPoll(&sFolderView);
		return true;
	case R_SAVE_NEW:
	{
		CeBackupPoll(&sFolderView);
		rebuildPreview();
		refreshSuggestion();
		if (!sFolderView.folder[0])
		{
			memset(&sResult, 0, sizeof(sResult));
			sResult.status = CE_PUBLISH_NO_FOLDER;
			sPhase = PH_DONE;
			syncRows();
			return true;
		}
		if (sFolderView.state == CE_BACKUP_OTHER)
		{
			memset(&sResult, 0, sizeof(sResult));
			sResult.status = CE_PUBLISH_BUSY;
			sPhase = PH_DONE;
			syncRows();
			return true;
		}
		if (!sPreviewValid)
		{
			memset(&sResult, 0, sizeof(sResult));
			sResult.status = CE_PUBLISH_BAD_CAREER;
			sPhase = PH_DONE;
			syncRows();
			return true;
		}
		if (!sTargetName[0])
		{
			memset(&sResult, 0, sizeof(sResult));
			sResult.status = sSuggestStatus;
			sPhase = PH_DONE;
			syncRows();
			return true;
		}
		memcpy(sFrozenBytes, sPreviewBytes, CE_CAREER_SIZE);
		snprintf(sFrozenFolder, sizeof(sFrozenFolder), "%s", sFolderView.folder);
		memset(&sResult, 0, sizeof(sResult));
		sPhase = PH_WORKING;
		sWorkRows = false;
		sDrawn = false;
		sDrawnAt = 0;
		syncRows();
		return true;
	}
	case R_BACK:
		PortCeCareerEditBack();
		return true;
	default:
		return false;
	}
}

const WCHAR	*emptyText()
{
	return L"";
}

const SCeListPage	kPage = {L"Edit Career", rowCount, fillRow, fillHint, selectRow, emptyText,
							 openPage, closePage, updatePage, drawPage, 0.78f, 0,
							 false, 0.f, 0, 0.f, 0.f, NULL, NULL, adjustRow};
} // namespace

const SCeListPage	*PortCeCareerEditPage()
{
	return &kPage;
}

int	PortCeCareerEditStepBack()
{
	if (!sAllowed)
		return 0;
	if (sPhase == PH_WORKING)
		return -1;
	if (sPhase == PH_DONE)
	{
		sPhase = PH_IDLE;
		refreshSuggestion();
		syncRows();
		return 1;
	}
	return 0;
}

#endif
