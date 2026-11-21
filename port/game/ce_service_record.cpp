// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_service_record.cpp: the Service Record (Onslaught CE > Extras > Service Record): every career the memory card
// lists, read without loading it, with its missions (the game's own names, complete or open, the rank letter), its kills by class and
// its Goodies. It is the Companion's Summary page inside the game. Looking changes nothing: the pages read through
// ce_career_view.cpp and write no file, no setting and nothing of the live career.
//
// Four pages (ce_extras.h): the careers in a list box, one career's record as label and value rows, and its missions and its Goodies
// in list boxes. A mission that is not yet open keeps its name hidden, as the game's own level select hides it, and a locked Goodie
// says only that it is locked.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "text.h"
#include "data/MissionScripts/text/text.stf"
#include "ce_career_view.h"
#include "ce_extras.h"
#include "ce_goodie_rules.h"
#include "ce_name_text.h"
#include "../platform/common/ce_backup_name.h"
#ifdef _WIN32
#include "../platform/common/ce_career_backup.h"
const char *PortUserDir();
static CeBackupOrigin sBackupOrigin{};
static bool sHasBackupOrigin;
#endif
#include "../platform/common/portlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
const int	kName = 256;				// a career name as the memory card gives it

// One career of the list: its name, and its record's totals once a row or a hint has asked for them.
struct SCareerRow
{
	WCHAR	mName[kName];
	int		mRead;						// 0 not read yet, 1 read, 2 cannot be read
	int		mComplete, mMissions, mGoodies, mRankS;
};

SCareerRow		*sCareers;				// the list as the page opened it
int				sCareerCount = -1;		// -1: careers cannot be read now
SCeCareerView	sView;					// the career whose record is open
WCHAR sBackupName[kName];
unsigned char sBackupSnapshot[CE_CAREER_SIZE];
WCHAR			sViewName[kName];
int				sShownGoodies[CE_CAREER_GOODIES_SHOWN];		// the wall's slots, in order
int				sShownCount;

void	Copy(WCHAR *out, int n, const WCHAR *in)
{
	int i = 0;
	for (; in[i] && (i < n - 1); i++)
		out[i] = in[i];
	out[i] = 0;
}

void	Append(WCHAR *out, int n, const WCHAR *in)
{
	int at = 0;
	while (out[at] && (at < n - 1))
		at++;
	Copy(out + at, n - at, in);
}

void	Text(WCHAR *out, int n, const char *in)
{
	PortCeExtrasWiden(out, n, in);
}

// A count as the game's own screens write large numbers: digits in threes.
const char	*Thousands(int value, char *out, int n)
{
	char digits[16];
	snprintf(digits, sizeof(digits), "%d", value < 0 ? 0 : value);
	const int length = int(strlen(digits));
	int at = 0;
	for (int i = 0; (i < length) && (at < n - 2); i++)
	{
		if (i && !((length - i) % 3))
			out[at++] = ',';
		out[at++] = digits[i];
	}
	out[at] = 0;
	return out;
}

// The totals of career number index, read once.
SCareerRow	*Totals(int index)
{
	if (!sCareers || (index < 0) || (index >= sCareerCount))
		return NULL;
	SCareerRow *row = &sCareers[index];
	if (!row->mRead)
	{
		static SCeCareerView view;
		if (PortCeCareerRead(row->mName, &view))
		{
			row->mRead = 1;
			row->mComplete = view.mMissionsComplete;
			row->mMissions = view.mMissionCount;
			row->mGoodies = view.mGoodiesFound;
			row->mRankS = view.mRankS;
		}
		else
			row->mRead = 2;
	}
	return row;
}

//*********************************************************************************
// The careers.
void	CareersOpen()
{
	free(sCareers);
	sCareers = NULL;
	sCareerCount = PortCeCareerCount();
	if (sCareerCount <= 0)
		return;
	sCareers = (SCareerRow *)calloc(size_t(sCareerCount), sizeof(SCareerRow));
	if (!sCareers)
	{
		sCareerCount = -1;
		return;
	}
	int kept = 0;
	for (int i = 0; i < sCareerCount; i++)
		if (PortCeCareerName(i, sCareers[kept].mName) && sCareers[kept].mName[0])
			kept++;
	sCareerCount = kept;
	fprintf(stderr, PORT_LOG_PREFIX "extras: the Service Record lists %d career%s\n", kept, kept == 1 ? "" : "s");
}

int	CareersCount()
{
	return sCareerCount > 0 ? sCareerCount : 0;
}

void CareerDisplayName(WCHAR *out,int n,const WCHAR *name)
{
#ifndef _WIN32
	WCHAR decoded[256]; char leaf[256];
	if (CeBackupSelectedName(name,leaf,sizeof(leaf),decoded)) {
		Copy(out,n,CeBackupNameHasDirectionMark(decoded) ? L"Name With Hidden Marks" : decoded);
		return;
	}
#endif
	Copy(out,n,CeBackupNameHasDirectionMark(name) ? L"Name With Hidden Marks" : name);
}

void	CareersRow(int index, SCeListRow *out)
{
	SCareerRow *row = Totals(index);
	if (!row)
		return;
	CareerDisplayName(out->mLeft, int(sizeof(out->mLeft) / sizeof(WCHAR)), row->mName);
	PortCeExtrasInFont(out->mLeft);
	char text[32];
	if (row->mRead == 1)
		snprintf(text, sizeof(text), "%d of %d", row->mComplete, row->mMissions);
	else
		snprintf(text, sizeof(text), "-");
	Text(out->mRight, int(sizeof(out->mRight) / sizeof(WCHAR)), text);
}

static const char	kCareerNote[] = "Nothing is changed by looking.";

void	CareersHint(int index, WCHAR *help, WCHAR *extra, int n)
{
	SCareerRow *row = Totals(index);
	if (!row)
		return;
	char text[160];
	if (row->mRead != 1)
	{
		Text(help, n, "This career could not be read.");
		return;
	}
	if (row->mGoodies == 1)
		snprintf(text, sizeof(text), "%d of %d missions complete. 1 Goodie found.", row->mComplete, row->mMissions);
	else
		snprintf(text, sizeof(text), "%d of %d missions complete. %d Goodies found.", row->mComplete, row->mMissions, row->mGoodies);
	Text(help, n, text);
	Text(extra, n, kCareerNote);
}

bool			sFromCatalogue;			// the Goodies page was reached from the Goodies Catalogue

// Opens a career's record, or from the catalogue straight to its Goodies.
bool	OpenCareer(int index, bool catalogue)
{
	SCareerRow *row = Totals(index);
	if (!row)
		return false;
#ifdef _WIN32
    sHasBackupOrigin = CeBackupRememberWindows(PortUserDir(),row->mName,sBackupOrigin);
#endif
	if ((row->mRead != 1) || !PortCeCareerReadSnapshot(row->mName, &sView, sBackupSnapshot))
	{
		row->mRead = 2;
		PortCeExtrasMessage("This career could not be read.");
		return true;
	}
	Copy(sBackupName,kName,row->mName);
	CareerDisplayName(sViewName,kName,row->mName);
	PortCeExtrasInFont(sViewName);
	sShownCount = 0;
	for (int slot = 0; slot < CE_CAREER_GOODIE_SLOTS; slot++)
		if (CeGoodieShown(slot))
			sShownGoodies[sShownCount++] = slot;
	char kills[5][16];
	for (int k = 0; k < CE_CAREER_KILL_CLASSES; k++)
		Thousands(sView.mKills[k], kills[k], int(sizeof(kills[k])));
	fprintf(stderr, PORT_LOG_PREFIX "extras: record: %d of %d missions, %d Goodies, rank S on %d, kills %d %d %d %d %d\n",
			sView.mMissionsComplete, sView.mMissionCount, sView.mGoodiesFound, sView.mRankS, sView.mKills[0], sView.mKills[1],
			sView.mKills[2], sView.mKills[3], sView.mKills[4]);
	sFromCatalogue = catalogue;
	PortCeExtrasShow(catalogue ? XP_GOODIES : XP_RECORD);
	return true;
}

bool	CareersSelect(int index)
{
	return OpenCareer(index, false);
}

bool	CatalogueSelect(int index)
{
	return OpenCareer(index, true);
}

const WCHAR	*CareersEmpty()
{
	static WCHAR none[] = L"No careers yet. Start a New Game on the main menu.";
	static WCHAR closed[] = L"Careers are not available right now.";
	return (sCareerCount < 0) ? closed : none;
}

void	CareersClose()
{
	free(sCareers);
	sCareers = NULL;
	sCareerCount = -1;
}

const SCeListPage	kCareersPage = {L"Service Record", CareersCount, CareersRow, CareersHint, CareersSelect, CareersEmpty, CareersOpen,
								CareersClose, NULL, NULL, 0.f, 0};
const SCeListPage	kCataloguePage = {L"Goodies Catalogue", CareersCount, CareersRow, CareersHint, CatalogueSelect, CareersEmpty, CareersOpen,
								CareersClose, NULL, NULL, 0.f, 0};

//*********************************************************************************
// One career's missions.
int	MissionsCount()
{
	return sView.mMissionCount;
}

void	MissionsRow(int index, SCeListRow *out)
{
	const SCeMissionView &mission = sView.mMissions[index];
	const int n = int(sizeof(out->mLeft) / sizeof(WCHAR));
	if (mission.mState == CE_MISSION_NOT_YET_OPEN)
	{
		// the level select hides a level that is not open yet; its number is no secret, written as the game writes it, and nothing
		// stands where a rank would
		char code[16];
		snprintf(code, sizeof(code), "%d.%02d", mission.mWorld / 100, mission.mWorld % 100);
		Text(out->mLeft, n, code);
		out->mDim = true;
		return;
	}
	Copy(out->mLeft, n, FET.GetLevelName(mission.mWorld));		// the level select's own form: the number, a dash and the name
	char rank[4] = "-";
	if ((mission.mState == CE_MISSION_COMPLETE) && mission.mRank)
	{
		rank[0] = mission.mRank;
		rank[1] = 0;
	}
	Text(out->mRight, int(sizeof(out->mRight) / sizeof(WCHAR)), rank);
	if (mission.mState != CE_MISSION_COMPLETE)
		out->mTone = CE_TONE_DIM;
}

void	MissionsHint(int index, WCHAR *help, WCHAR *extra, int n)
{
	const SCeMissionView &mission = sView.mMissions[index];
	char text[96];
	if (mission.mState == CE_MISSION_COMPLETE)
	{
		if (mission.mRank)
			snprintf(text, sizeof(text), "Complete. Rank %c.", mission.mRank);
		else
			snprintf(text, sizeof(text), "Complete.");
	}
	else if (mission.mState == CE_MISSION_OPEN)
		snprintf(text, sizeof(text), "Open. Not completed yet.");
	else
		snprintf(text, sizeof(text), "Not yet open.");
	Text(help, n, text);
	snprintf(text, sizeof(text), "%d of %d missions complete.", sView.mMissionsComplete, sView.mMissionCount);
	Text(extra, n, text);
}

bool	NoSelect(int)
{
	return false;
}

const WCHAR	*NoneEmpty()
{
	static WCHAR none[] = L"Nothing to show.";
	return none;
}

// Mission names and Goodie titles are the game's own and some are long: the one letter width of these two pages is a little narrower
// than the other lists'.
const SCeListPage	kMissionsPage = {L"Missions", MissionsCount, MissionsRow, MissionsHint, NoSelect, NoneEmpty, NULL, NULL, NULL, NULL, 0.80f, 0};

//*********************************************************************************
// One career's Goodies: the 230 the game's wall shows, in its order of slots.
int	GoodiesCount()
{
	return sShownCount;
}

// What a Goodie slot is, from the game's own table of the Goodies wall (the rows of its pages): the first slot of each kind.
struct SGoodieKind
{
	int			mFirst;
	const char	*mName;
};
const SGoodieKind	kGoodieKinds[] = {{0, "Biography"}, {8, "Unit"}, {66, "Race Level"}, {74, "Extra"}, {78, "Concept Art"}, {201, "Cutscene"}};

// The title the game's wall prints for a Goodie it has (the first slots only), found the way the wall finds it: each earlier slot
// owns a fixed run of texts, twelve for a biography and nine for the rest; later slots have no text.
const WCHAR	*GoodieTitle(int slot)
{
	if ((slot < 0) || (slot > 77))
		return NULL;
	int at = 0;
	for (int i = 0; i < slot; i++)
		at += (i <= 7) ? 12 : 9;
	const WCHAR *title = TEXT_DB.GetFollowingString(GOODIE_TEXT_1_TITLE, at);
	return (title && title[0]) ? title : NULL;
}

// A Goodie by its kind and number, as the game's wall counts them: "Biography 8", "Unit 5".
void	GoodieKindName(int slot, char *out, int n)
{
	int kind = 0;
	while ((kind + 1 < int(sizeof(kGoodieKinds) / sizeof(kGoodieKinds[0]))) && (slot >= kGoodieKinds[kind + 1].mFirst))
		kind++;
	snprintf(out, size_t(n), "%s %d", kGoodieKinds[kind].mName, slot - kGoodieKinds[kind].mFirst + 1);
}

// A Goodie that is found shows its title, when the wall has one, and otherwise its kind and number; one that is not found shows its kind
// and number alone. The state is the right-hand word.
void	GoodiesRow(int index, SCeListRow *out)
{
	const int slot = sShownGoodies[index];
	const int state = sView.mGoodie[slot];
	const int n = int(sizeof(out->mLeft) / sizeof(WCHAR));
	const bool found = (state == CE_GOODIE_NEW) || (state == CE_GOODIE_VIEWED);
	const WCHAR *title = found ? GoodieTitle(slot) : NULL;
	if (title)
		Copy(out->mLeft, n, title);
	else
	{
		char text[48];
		GoodieKindName(slot, text, int(sizeof(text)));
		Text(out->mLeft, n, text);
	}
	const char *word = (state == CE_GOODIE_NEW) ? "New" : (state == CE_GOODIE_VIEWED) ? "Viewed" : "Locked";
	Text(out->mRight, int(sizeof(out->mRight) / sizeof(WCHAR)), word);
	if (!found)
	{
		out->mDim = true;
		out->mTone = CE_TONE_DIM;
	}
}

void	GoodiesHint(int index, WCHAR *help, WCHAR *extra, int n)
{
	const int slot = sShownGoodies[index];
	const int state = sView.mGoodie[slot];
	char name[48], text[128];
	GoodieKindName(slot, name, int(sizeof(name)));
	const char *what = (state == CE_GOODIE_NEW) ? "Found, not viewed yet." : (state == CE_GOODIE_VIEWED) ? "Found and viewed." : "Locked.";
	snprintf(text, sizeof(text), "%s. %s", name, what);
	Text(help, n, text);
	// how it is earned (ce_goodie_rules.h)
	const SCeGoodieRule &rule = kCeGoodieRules[slot];
	snprintf(text, sizeof(text), "%s", rule.mRule);
	Text(extra, n, text);
	// a rule that starts by unlocking another Goodie names it as the list does when the career has it: by its title
	int length = 0;
	const int first = CeUnlockedFirst(text, &length);
	const int firstState = ((first >= 0) && (first < CE_CAREER_GOODIE_SLOTS)) ? sView.mGoodie[first] : CE_GOODIE_LOCKED;
	const WCHAR *firstTitle = ((firstState == CE_GOODIE_NEW) || (firstState == CE_GOODIE_VIEWED)) ? GoodieTitle(first) : NULL;
	if (firstTitle)
	{
		static WCHAR joined[200];
		const int title = int(wcslen(firstTitle));
		Copy(joined, int(sizeof(joined) / sizeof(WCHAR)), extra);
		Copy(joined + 7, int(sizeof(joined) / sizeof(WCHAR)) - 7, firstTitle);
		Copy(joined + 7 + title, int(sizeof(joined) / sizeof(WCHAR)) - 7 - title, extra + 7 + length);
		Copy(extra, n, joined);
	}
}

const WCHAR *const kSettingLabels[] = {L"Sound Volume", L"Music Volume", L"Player 1 Invincibility", L"Player 2 Invincibility"};

int SettingsCount() { return int(sizeof(kSettingLabels) / sizeof(kSettingLabels[0])); }

void SettingsRow(int index, SCeListRow *out)
{
	if (index < 0 || index >= SettingsCount())
		return;
	Copy(out->mLeft, int(sizeof(out->mLeft) / sizeof(WCHAR)), kSettingLabels[index]);
	const int room = int(sizeof(out->mRight) / sizeof(WCHAR));
	if (index < 2)
	{
		const int percent = CeStoredVolumePercent(index ? sView.mMusicBits : sView.mSoundBits);
		if (percent < 0)
			Copy(out->mRight, room, L"Unknown");
		else
		{
			char value[16];
			snprintf(value, sizeof(value), "%d%%", percent);
			Text(out->mRight, room, value);
		}
	}
	else
		Copy(out->mRight, room, sView.mInvincible[index - 2] ? L"On" : L"Off");
}

void SettingsHint(int index, WCHAR *help, WCHAR *extra, int n)
{
	Copy(help, n, sViewName);
	Copy(extra, n, index < 2 ? L"Loading this career keeps your current volume." :
		L"This is the setting saved for this player.");
}

const SCeListPage kSettingsPage = {L"Saved Settings", SettingsCount, SettingsRow, SettingsHint, NoSelect, NoneEmpty,
	NULL, NULL, NULL, NULL, 1.0f, 0};

const SCeListPage	kGoodiesPage = {L"Goodies", GoodiesCount, GoodiesRow, GoodiesHint, NoSelect, NoneEmpty, NULL, NULL, NULL, NULL, 0.80f, 0};

//*********************************************************************************
// One career's record: label and value rows, the game's option rows. The labels end in a colon and nothing after it, as the options pages'
// labels do; the kill classes carry the Companion's names.
struct SRecordRow
{
	const WCHAR	*mLabel;
	const WCHAR	*mHelp;
};

const SRecordRow	kRecordRows[CE_RECORD_ROWS] = {
	{L"Career:", L"As it was last saved."},
	{L"Missions Complete:", L"Missions you have won, of all the missions in the campaign."},
	{L"Goodies Found:", L"Goodies you have earned, of all the Goodies on the Goodies page."},
	{L"Aircraft Destroyed:", L"Enemy aircraft you have destroyed."},
	{L"Vehicles Destroyed:", L"Enemy vehicles you have destroyed."},
	{L"Emplacements Destroyed:", L"Enemy gun emplacements you have destroyed."},
	{L"Infantry Defeated:", L"Enemy infantry you have defeated."},
	{L"Mechs Destroyed:", L"Enemy mechs you have destroyed."},
};
}

const WCHAR *PortCeBackupCareerName() { return sBackupName; }
const unsigned char *PortCeBackupSnapshot() { return sBackupSnapshot; }
void PortCeRecordRefreshCareers() { if (sCareers) CareersOpen(); }
#ifdef _WIN32
const CeBackupOrigin *PortCeBackupWindowsOrigin() { return sHasBackupOrigin ? &sBackupOrigin : nullptr; }
#endif

const SCeListPage	*PortCeRecordCareersPage() { return &kCareersPage; }
const SCeListPage	*PortCeRecordMissionsPage() { return &kMissionsPage; }
const SCeListPage	*PortCeRecordGoodiesPage() { return &kGoodiesPage; }
const SCeListPage *PortCeRecordSettingsPage() { return &kSettingsPage; }
const SCeListPage	*PortCeCataloguePage() { return &kCataloguePage; }
bool				PortCeGoodiesFromCatalogue() { return sFromCatalogue; }

const WCHAR	*PortCeRecordLabel(int row)
{
	return ((row >= 0) && (row < CE_RECORD_ROWS)) ? kRecordRows[row].mLabel : L"";
}

const WCHAR	*PortCeRecordHelp(int row)
{
	return ((row >= 0) && (row < CE_RECORD_ROWS)) ? kRecordRows[row].mHelp : L"";
}

const WCHAR	*PortCeRecordExtra(int)
{
	static WCHAR note[64];
	if (!note[0])
		Text(note, 64, kCareerNote);
	return note;
}

const WCHAR	*PortCeRecordValue(int row)
{
	static WCHAR value[kName];
	char text[48] = "";
	switch (row)
	{
	case 0:
		return sViewName;
	case 1:
		snprintf(text, sizeof(text), "%d of %d", sView.mMissionsComplete, sView.mMissionCount);
		break;
	case 2:
		snprintf(text, sizeof(text), "%d of %d", sView.mGoodiesFound, int(CE_CAREER_GOODIES_SHOWN));
		break;
	default:
		if ((row >= 3) && (row < 3 + CE_CAREER_KILL_CLASSES))
			Thousands(sView.mKills[row - 3], text, int(sizeof(text)));
		break;
	}
	Text(value, kName, text);
	return value;
}

#endif
