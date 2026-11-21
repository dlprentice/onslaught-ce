// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_modspage_model.h: what the Mods page (ce_modspage.cpp) knows and says, apart from drawing it: the rows as the loader
// reports them (PortModsReport, port/platform/common/mods.h), which are on and in what order, the one write that changes that (ce.ini's
// mods= line, PortModsWriteLine), and every sentence the page shows. It has no game type in it, so port/tools/ce_modspage_tests.py runs
// this very file on the host against the actual loader and mod folders made by port/tools/mods.py.
//
// The order of the list is the order the mods load in: a mod lower in the list wins a file that two of them change. The page writes the
// line at once after each change; the game reads it once, at startup, so a change applies at the next start and the page says so.
// Text from a mod's mod.json is arbitrary UTF-8 by a stranger: every text shown passes CeModText, which keeps only the characters the
// game's small face has.
#ifndef CE_MODSPAGE_MODEL_H
#define CE_MODSPAGE_MODEL_H

#include "../platform/common/mods.h"

#include <stdio.h>
#include <string.h>

// Whether the game's small face has the Latin-1 character c (the page gives the font's own answer, a test gives the list of mods.h's
// neighbour ce_frontend.cpp: no $ @ [ \ ] ^ _ ` { | } ~).
typedef int (*CeModShown)(int c);

// One plain sentence for each kind of refusal (PortModWhy): what is wrong, then what to do. The loader's own words stay in the log.
// PORT_MOD_WHY_RENAMED has two: the first names the folder (%s) and is used when the small face can show that name.
static const char *const kCeModWhy[PORT_MOD_WHY_COUNT] = {
	"",
	"This mod is no longer in your mods folder. Put it back, or turn it off.",
	"This mod's folder is a shortcut. Copy the real folder into your mods folder.",
	"This mod's folder name cannot be used. Rename it with small letters, numbers and dashes only.",
	"This mod is still packed. Unpack it into a folder of its own in your mods folder.",
	"This mod's information is missing or damaged. Get the mod again.",
	"This mod's folder has the wrong name. Unpack the mod again and keep the folder's name.",
	"This mod needs a newer Onslaught CE. Update the game to use it.",
	"This mod was made for an older Onslaught CE. Look for a newer copy of the mod.",
	"This mod was not put together correctly. Ask its author for a fixed copy.",
	"This mod changes a file your game does not have. It may be for another version of the game.",
	"Some of this mod's files are missing. Get the mod again.",
	"This mod's files are damaged or were changed. Get the mod again.",
	"The game cannot read one of this mod's files. Ask its author for a fixed copy.",
	"This mod is too large for the game. Ask its author for a smaller copy.",
	"This mod cannot be used. Ask its author for a fixed copy.",
};
static const char kCeModRenamedTo[] = "This mod's folder has the wrong name. Rename the folder to %s.";

// The page's other sentences.
static const char kCeModsEmpty[] = "You have no mods yet. To add one, put its folder in the mods folder in your user folder.";
static const char kCeModsEmptyWhere[] = "Open Mods Folder shows where it is.";
static const char kCeModsNextStart[] = "Applies the next time you start the game.";
static const char kCeModsOff[] = "Mods are off in recordings, replays and network games. None is loaded now.";
static const char kCeModsMore[] = "Only the first 64 mods are shown.";
static const char kCeModsFull[] = "Only 16 mods can be on at once. Turn one off first.";
static const char kCeModsNotSaved[] = "Your settings could not be saved, so this was not changed.";
// The words under the list's other rows, and what Open Mods Folder answers.
static const char kCeModsOpenHint[] = "Shows your mods folder in your file manager.";
static const char kCeModsPageNext[] = "Page %d of %d. Select for the next page.";
static const char kCeModsPageFirst[] = "Page %d of %d. Select for the first page.";
static const char kCeModsFolderOpen[] = "Your mods folder is open in your file manager. It may be behind the game.";
static const char kCeModsNoFileManager[] = "Your file manager could not be opened.";
static const char kCeModsNoLevel[] = "This level could not be started. Turn the mod on and start the game again.";
static const char kCeModsFolderBlocked[] = "Your mods folder could not be made.\nRename or remove the file named mods.";
static const char kCeModsFolderIs[] = "Your mods folder is:";
// The notice on the main menu, once a start, when a mod that is on did not load (%d of them).
static const char kCeModsNoticeOne[] = "1 mod did not load. See Onslaught CE > Mods.";
static const char kCeModsNoticeMany[] = "%d mods did not load. See Onslaught CE > Mods.";

// What a row can do, in the order its list shows them.
enum ECeModAction
{
	CEMA_NONE = 0,
	CEMA_TOGGLE,
	CEMA_UP,
	CEMA_DOWN,
	CEMA_DETAILS,
	CEMA_PLAY					// Play, or Play Level 1: CEMA_PLAY + k starts the k-th level the mod adds (at most kCeModsPlayLevels of them)
};
static const int kCeModsPlayLevels = 3;											// the levels of one mod the page starts
static const int kCeModsMaxActions = 4 + kCeModsPlayLevels;						// Turn On or Off, Move Up, Move Down, Details, the levels

// What a change answered.
enum ECeModResult
{
	CEMR_DONE = 0,
	CEMR_FULL,			// 16 mods are on already
	CEMR_NOT_SAVED,		// ce.ini could not be written: nothing changed
	CEMR_NOTHING		// not a thing this row can do
};

// text (UTF-8, anything) in the characters the small face has, as Latin-1 in out: typographic quotes, dashes and the ellipsis as their
// plain forms, an underscore as a space and the brackets the face lacks as round ones, a line break or tab as a space, and any other
// character it cannot show as one question mark for each run of them; blanks are collapsed and trimmed. Malformed UTF-8 is a question mark.
// A run of more than kCeModWord characters without a blank is given one: the game's word wrap (CDXBitmapFont::WordWrap) puts a word wider
// than its line on a line of its own letter by letter and writes past its forty lines, so no text of a mod reaches it with such a word.
static const int kCeModWord = 28;

static inline void CeModText(char *out, int n, const char *in, CeModShown shown)
{
	int o = 0, run = 0;
	char last = ' ';
	const unsigned char *p = (const unsigned char *)(in ? in : "");
	while (*p && o < n - 8)
	{
		unsigned cp = *p;
		int length = 1;
		if (cp >= 0x80)
		{
			const int want = cp >= 0xf0 ? 4 : cp >= 0xe0 ? 3 : cp >= 0xc0 ? 2 : 0;
			unsigned value = want == 4 ? cp & 0x07 : want == 3 ? cp & 0x0f : cp & 0x1f;
			bool good = want != 0 && cp < 0xf8;
			for (int i = 1; good && i < want; i++)
			{
				if ((p[i] & 0xc0) != 0x80)
					good = false;
				else
					value = (value << 6) | (p[i] & 0x3f);
			}
			if (good && value >= (want == 4 ? 0x10000u : want == 3 ? 0x800u : 0x80u))
			{
				cp = value;
				length = want;
			}
			else
				cp = 0xfffd;
		}
		p += length;
		const char *as = NULL;
		char one[2] = {0, 0};
		switch (cp)
		{
		case 0x2018: case 0x2019: case 0x201a: case 0x2032: as = "'"; break;
		case 0x201c: case 0x201d: case 0x201e: case 0x2033: as = "\""; break;
		case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: case '~': as = "-"; break;
		case 0x2026: as = "..."; break;
		case 0x00a0: case 0x2002: case 0x2003: case 0x2009: case '_': case '\t': case '\n': case '\r': as = " "; break;
		case '[': case '{': as = "("; break;
		case ']': case '}': as = ")"; break;
		case '\\': case '|': as = "/"; break;
		case '`': as = "'"; break;
		default:
			if (cp < 0x20 || cp == 0x7f)
				as = " ";
			else if (cp < 0x100 && shown((int)cp))
			{
				one[0] = (char)cp;
				as = one;
			}
			else
				as = "?";
		}
		for (; *as; as++)
		{
			if ((*as == ' ' || *as == '?') && last == *as)
				continue;
			if (*as != ' ' && run >= kCeModWord)
			{
				out[o++] = ' ';
				run = 0;
			}
			run = *as == ' ' ? 0 : run + 1;
			out[o++] = last = *as;
		}
	}
	while (o > 0 && out[o - 1] == ' ')
		o--;
	out[o] = 0;
	// nothing left but marks (a text in a script the face lacks): it says nothing, so it is empty
	bool word = false;
	for (int i = 0; i < o; i++)
		word = word || (out[i] >= '0' && out[i] <= '9') || ((out[i] | 0x20) >= 'a' && (out[i] | 0x20) <= 'z') || (unsigned char)out[i] >= 0xc0;
	if (!word)
		out[0] = 0;
}

// The name a row shows for a mod: mod.json's name in the face's characters; the folder's name when it has none, or when more than half
// of it is characters the face cannot show (a name in another script).
static inline void CeModName(char *out, int n, const PortModInfo &mod, CeModShown shown)
{
	CeModText(out, n, mod.name, shown);
	int marks = 0, length = 0;
	for (const char *c = out; *c; c++, length++)
		marks += *c == '?';
	if (!length || 2 * marks > length)
		CeModText(out, n, mod.folder, shown);
	if (!out[0])
		snprintf(out, n, "Unnamed Mod");
}

// text cut to at most `length` characters where a word ends, and "..." after it: never in the middle of a word unless the first word alone is
// longer, never after a comma, colon, dash or an open bracket that nothing closes ("Flagship duel (level 888," is "Flagship duel..."). A text
// of `length` characters or fewer is copied as it is.
static inline void CeModCut(char *out, int n, const char *text, int length)
{
	const int whole = (int)strlen(text);
	if (whole <= length)
	{
		snprintf(out, n, "%s", text);
		return;
	}
	int cut = length;
	while (cut > 0 && text[cut] != ' ')
		cut--;
	if (cut <= 0)
		cut = length;
	for (int i = cut; i-- > 0;)
	{
		if (text[i] == ')')
			break;
		if (text[i] == '(')
		{
			cut = i;
			break;
		}
	}
	while (cut > 0 && strchr(" ,;:-/(", text[cut - 1]))
		cut--;
	if (cut <= 0)
		cut = length;
	snprintf(out, n, "%.*s...", cut, text);
}

// A name cut to kCeModShort characters so that a sentence that names it keeps to one line under the list.
static const int kCeModShort = 22;

static inline void CeModShort(char *name)
{
	char cut[96];
	CeModCut(cut, sizeof cut, name, kCeModShort);
	strcpy(name, cut);
}

// A sentence ends in a full stop, unless its last word is already a cut name's "...".
static inline void CeModStop(char *text, int n)
{
	const size_t length = strlen(text);
	if (length + 2 < (size_t)n && (length < 3 || strcmp(text + length - 3, "...")))
		strcpy(text + length, ".");
}

// The pages of the list. Up to ten mods are one page of up to ten rows; more are pages of nine rows and More Mods, shared out evenly (the
// fewest pages, then the fewest rows each page needs), so that the last page is never a few rows with a large gap above More Mods.
static const int kCeModsSlots = 10;
static inline int CeModsPages(int count)
{
	return count <= kCeModsSlots ? 1 : (count + kCeModsSlots - 2) / (kCeModsSlots - 1);
}
static inline int CeModsPerPage(int count)
{
	const int pages = CeModsPages(count);
	return count <= kCeModsSlots ? kCeModsSlots : (count + pages - 1) / pages;
}

struct CeModsModel
{
	PortModInfo	mMods[PORT_MODS_ROWS];
	PortModsRun	mRun;
	int			mTotal;						// the entries the report held
	int			mCount;						// the rows: the entries still listed (an entry that only ce.ini's line named goes with its name)
	int			mOrder[PORT_MODS_ROWS];		// row -> index in mMods: the mods that are on first, in the order they load in, then the others by name
	bool		mOn[PORT_MODS_ROWS];		// by index in mMods: named on ce.ini's mods= line as the page last read or wrote it
	int			mBase;						// the mods that are on and cannot load, as this process first saw them (-1 before a read)
	int			mAt;						// the row the last change left its mod in

	CeModsModel() : mTotal(0), mCount(0), mBase(-1), mAt(0) {}

	// Reads everything again: the mods of ce.ini's line first, in its order, then the rest of the mods folder by name.
	void	Read()
	{
		mTotal = mCount = PortModsReport(mMods, PORT_MODS_ROWS, &mRun);
		for (int i = 0; i < mCount; i++)
		{
			mOrder[i] = i;
			mOn[i] = mMods[i].place > 0;
		}
		if (mBase < 0)
			mBase = Unloadable();
	}

	const PortModInfo	&Mod(int row) const { return mMods[mOrder[row]]; }
	bool	On(int row) const { return mOn[mOrder[row]]; }
	bool	CanLoad(int row) const { return Mod(row).why == PORT_MOD_WHY_NONE; }

	// The word after a row's name.
	const char	*State(int row) const { return !CanLoad(row) ? "Can't Load" : On(row) ? "On" : "Off"; }

	// A row can be turned on when its mod passes the checks, and off whenever ce.ini's line names it (a mod that no longer loads too, and an
	// entry of a line edited by hand that is no mod id at all).
	bool	CanToggle(int row) const { return On(row) || (Mod(row).id[0] && CanLoad(row)); }

	// The mods that are on and have an id the line can hold: what a write would hold.
	int		OnCount() const
	{
		int count = 0;
		for (int row = 0; row < mCount; row++)
			count += On(row) && Mod(row).id[0];
		return count;
	}

	// The rows that are on and cannot load.
	int		Unloadable() const
	{
		int count = 0;
		for (int row = 0; row < mCount; row++)
			count += On(row) && !CanLoad(row);
		return count;
	}

	// The levels of the row's mod the page can start: it adds some (an archive for a level from 850 to 899), it is on, and this process loaded it at
	// start (the game has its files now; a mod turned on since gives them at the next start).
	int		PlayLevels(int row) const
	{
		const PortModInfo &mod = Mod(row);
		if (!On(row) || !CanLoad(row) || mRun.off)
			return 0;
		const int n = mod.level_count < PORT_MOD_LEVELS ? mod.level_count : PORT_MOD_LEVELS;
		return n < kCeModsPlayLevels ? n : kCeModsPlayLevels;
	}

	// What the row's list offers, in order, `out` holding kCeModsMaxActions; returns their number (Details always, so at least 1). The mods that
	// are on are the top of the list in the order they load in, so a mod moves only past another that is on. A level to play comes last, so
	// that Select, Select is still Turn On or Turn Off.
	int		Actions(int row, int *out) const
	{
		int n = 0;
		if (CanToggle(row))
			out[n++] = CEMA_TOGGLE;
		if (On(row) && CanLoad(row))
		{
			if (row > 0 && On(row - 1))
				out[n++] = CEMA_UP;
			if (row < mCount - 1 && On(row + 1))
				out[n++] = CEMA_DOWN;
		}
		out[n++] = CEMA_DETAILS;
		for (int k = 0; k < PlayLevels(row); k++)
			out[n++] = CEMA_PLAY + k;
		return n;
	}

	// The number of the level a CEMA_PLAY action starts (a level from 850 to 899), or 0.
	int		PlayLevel(int row, int action) const
	{
		const int k = action - CEMA_PLAY;
		return (action >= CEMA_PLAY && k < PlayLevels(row)) ? Mod(row).levels[k] : 0;
	}

	const char	*ActionLabel(int row, int action) const
	{
		switch (action)
		{
		case CEMA_TOGGLE:	return On(row) ? "Turn Off" : "Turn On";
		case CEMA_UP:		return "Move Up";
		case CEMA_DOWN:		return "Move Down";
		case CEMA_DETAILS:	return "Details";
		default:
		{
			static const char *const several[kCeModsPlayLevels] = {"Play Level 1", "Play Level 2", "Play Level 3"};
			const int k = action - CEMA_PLAY;
			return Mod(row).level_count == 1 ? "Play" : several[k < 0 ? 0 : k >= kCeModsPlayLevels ? kCeModsPlayLevels - 1 : k];
		}
		}
	}

	// ---- the one write ---------------------------------------------------------------------------------------------------------------
	// The list as a change would leave it: `order` and `on` are the new order (row -> index) and states (by index), `count` the rows. The
	// mods that are on keep their places at the top; an entry that is on but has no id the line can hold is taken off, and so is any mod
	// beyond the sixteenth (the loader refuses those too); the others follow by name; an entry that only the line named is gone. Then
	// ce.ini's line is written. On a failed write nothing here changes.
	bool	Settle(const int *order, const bool *on, int count, int *at)
	{
		bool state[PORT_MODS_ROWS];
		memcpy(state, on, sizeof state);
		int top[PORT_MODS_ROWS], rest[PORT_MODS_ROWS], tops = 0, rests = 0, written = 0;
		for (int i = 0; i < count; i++)
		{
			const int index = order[i];
			if (state[index] && (!mMods[index].id[0] || written >= PORT_MODS_ON))
				state[index] = false;
			written += state[index];
		}
		for (int i = 0; i < count; i++)
			(state[order[i]] ? top[tops++] : rest[rests++]) = order[i];
		for (int i = 1; i < rests; i++)			// by name, as the mods folder is listed
		{
			const int index = rest[i];
			int j = i;
			for (; j > 0 && Before(index, rest[j - 1]); j--)
				rest[j] = rest[j - 1];
			rest[j] = index;
		}
		const char *ids[PORT_MODS_ON] = {};
		int n = 0;
		for (int i = 0; i < tops && n < PORT_MODS_ON; i++)
			if (mMods[top[i]].id[0])
				ids[n++] = mMods[top[i]].id;
		if (!PortModsWriteLine(ids, n))
			return false;
		int kept = 0;
		int placed[PORT_MODS_ROWS];
		for (int i = 0; i < tops; i++)
			placed[kept++] = top[i];
		for (int i = 0; i < rests; i++)
			if (mMods[rest[i]].why != PORT_MOD_WHY_MISSING)
				placed[kept++] = rest[i];
		for (int i = 0; i < kept; i++)
			mOrder[i] = placed[i];
		memcpy(mOn, state, sizeof mOn);
		mCount = kept;
		if (at)
		{
			int found = -1;
			for (int i = 0; i < kept; i++)
				if (placed[i] == *at)
					found = i;
			*at = found;
		}
		return true;
	}

	// Whether the mod at index a sorts before the one at b by folder name, letters of either case together.
	bool	Before(int a, int b) const
	{
		for (const char *x = mMods[a].folder, *y = mMods[b].folder;; x++, y++)
		{
			const int cx = (*x >= 'A' && *x <= 'Z') ? *x + 32 : (unsigned char)*x, cy = (*y >= 'A' && *y <= 'Z') ? *y + 32 : (unsigned char)*y;
			if (cx != cy)
				return cx < cy;
			if (!cx)
				return strcmp(mMods[a].folder, mMods[b].folder) < 0;
		}
	}

	// Turns the row's mod on (it joins the end of the mods that are on) or off, and writes the line; on a failed write everything is as it was.
	// mAt is the row the mod is in afterwards (where its place would be if the entry is gone).
	int		Toggle(int row)
	{
		if (row < 0 || row >= mCount || !CanToggle(row))
			return CEMR_NOTHING;
		const int index = mOrder[row];
		const bool turn_on = !mOn[index];
		if (turn_on && OnCount() >= PORT_MODS_ON)
			return CEMR_FULL;
		int order[PORT_MODS_ROWS], n = 0;
		bool on[PORT_MODS_ROWS];
		memcpy(on, mOn, sizeof on);
		on[index] = turn_on;
		if (turn_on)
		{
			for (int i = 0; i < mCount; i++)		// the end of the mods that are on
				if (i != row && mOn[mOrder[i]])
					order[n++] = mOrder[i];
			order[n++] = index;
			for (int i = 0; i < mCount; i++)
				if (i != row && !mOn[mOrder[i]])
					order[n++] = mOrder[i];
		}
		else
			for (int i = 0; i < mCount; i++)
				order[n++] = mOrder[i];
		int at = index;
		if (!Settle(order, on, n, &at))
			return CEMR_NOT_SAVED;
		mAt = at >= 0 ? at : (row < mCount ? row : mCount - 1);
		return CEMR_DONE;
	}

	// Moves the row one place up (-1) or down (+1), past another mod that is on; the line is written. mAt is the row's place afterwards.
	int		Move(int row, int delta)
	{
		const int other = row + delta;
		if (row < 0 || row >= mCount || other < 0 || other >= mCount || (delta != 1 && delta != -1) || !On(row) || !On(other))
			return CEMR_NOTHING;
		int order[PORT_MODS_ROWS];
		for (int i = 0; i < mCount; i++)
			order[i] = mOrder[i];
		order[row] = mOrder[other];
		order[other] = mOrder[row];
		int at = mOrder[row];
		if (!Settle(order, mOn, mCount, &at))
			return CEMR_NOT_SAVED;
		mAt = at >= 0 ? at : other;
		return CEMR_DONE;
	}

	// The mods that are on and can load, in the list's order: what the next start loads. Returns their number.
	int		Next(const char **ids, int *rows = NULL) const
	{
		int n = 0;
		for (int row = 0; row < mCount && n < PORT_MODS_ON; row++)
			if (On(row) && CanLoad(row))
			{
				if (rows)
					rows[n] = row;
				ids[n++] = Mod(row).id;
			}
		return n;
	}

	// Whether the next start loads other mods, or the same ones in another order, than this process did, or leaves out one that was refused.
	bool	Pending() const
	{
		const char *ids[PORT_MODS_ON];
		const int n = Next(ids);
		if (n != mRun.loaded_count)
			return true;
		for (int i = 0; i < n; i++)
			if (strcmp(ids[i], mRun.loaded[i]))
				return true;
		return mBase >= 0 && Unloadable() != mBase;
	}

	// The page's standing note, or NULL: mods off for this run, else a change that waits for the next start, else more mods than rows.
	const char	*Note() const
	{
		if (mRun.off == 1)
			return kCeModsOff;
		if (Pending())
			return kCeModsNextStart;
		return mRun.more ? kCeModsMore : NULL;
	}

	int		ReturnRow(int level) const
	{
		const char *id = PortModLevelOwner(level);
		return id ? RowOf(id) : -1;
	}

	int		RowOf(const char *id) const
	{
		for (int row = 0; row < mCount; row++)
			if (!strcmp(Mod(row).id, id))
				return row;
		return -1;
	}

	// What the row's mod shares with the others that are on (false: nothing, or it is off or cannot load), and the names involved, cut short.
	bool	Shared(int row, PortModClash *clash, char *other, char *winner, int n, CeModShown shown) const
	{
		other[0] = winner[0] = 0;
		if (!On(row) || !CanLoad(row))
			return false;
		const char *ids[PORT_MODS_ON];
		int rows[PORT_MODS_ON];
		const int count = Next(ids, rows);
		int index = -1;
		for (int i = 0; i < count; i++)
			if (rows[i] == row)
				index = i;
		if (index < 0 || !PortModsClash(ids, count, index, clash) || !clash->shared)
			return false;
		if (clash->other[0] && RowOf(clash->other) >= 0)
			CeModName(other, n, Mod(RowOf(clash->other)), shown);
		if (clash->winner[0] && RowOf(clash->winner) >= 0)
			CeModName(winner, n, Mod(RowOf(clash->winner)), shown);
		CeModShort(other);
		CeModShort(winner);
		return true;
	}

	// For a mod that is on and shares files with others that are on: one short sentence for the line under the list. Empty otherwise.
	void	Clash(int row, char *out, int n, CeModShown shown) const
	{
		out[0] = 0;
		PortModClash clash;
		char other[80], winner[80], files[32];
		if (!Shared(row, &clash, other, winner, sizeof other, shown))
			return;
		if (clash.shared == 1)
			snprintf(files, sizeof files, "a file");
		else
			snprintf(files, sizeof files, "%d files", clash.shared);
		if (!clash.lost)
		{
			if (other[0])
				snprintf(out, n, "Wins %s over %s", files, other);
			else
				snprintf(out, n, "Wins %s over %d other mods", files, clash.others);
		}
		else if (clash.lost == clash.shared && winner[0])
			snprintf(out, n, "Loses %s to %s", files, winner);
		else
			snprintf(out, n, "Shares %s with %d other mods", files, clash.others);
		CeModStop(out, n);
	}

	// The same for the Details box, in full sentences that do not end on a name.
	void	ClashLong(int row, char *out, int n, CeModShown shown) const
	{
		out[0] = 0;
		PortModClash clash;
		char other[80], winner[80];
		if (!Shared(row, &clash, other, winner, sizeof other, shown))
			return;
		const bool one = clash.shared == 1;
		char files[48];
		snprintf(files, sizeof files, one ? "the same file" : "%d of the same files", clash.shared);
		if (other[0])
		{
			if (!clash.lost)
				snprintf(out, n, "This mod and %s change %s.\nThis mod wins.", other, files);
			else
				snprintf(out, n, "%s changes %s, and wins.", other, files);
		}
		else if (!clash.lost)
			snprintf(out, n, "%d other mods change %s.\nThis mod wins.", clash.others, one ? "the same file" : "some of the same files");
		else if (winner[0])
			snprintf(out, n, "%s wins %s this mod shares with %d other mods.", winner, one ? "the file" : "every file", clash.others);
		else
			snprintf(out, n, "%d other mods change some of the same files. The lowest of them in the list wins each file.", clash.others);
	}

	// "By AUTHOR. Version V.", with what mod.json gave; empty when it gave neither.
	void	ByLine(int row, char *out, int n, CeModShown shown) const
	{
		char author[80], version[48];
		CeModText(author, sizeof author, Mod(row).author, shown);
		CeModText(version, sizeof version, Mod(row).version, shown);
		if (author[0] && version[0])
			snprintf(out, n, "By %s. Version %s.", author, version);
		else if (author[0])
			snprintf(out, n, "By %s.", author);
		else if (version[0])
			snprintf(out, n, "Version %s.", version);
		else
			out[0] = 0;
	}

	// Why the row's mod cannot load and what to do about it; empty for a mod that can.
	void	Why(int row, char *out, int n, CeModShown shown) const
	{
		const PortModInfo &mod = Mod(row);
		const int why = mod.why > PORT_MOD_WHY_NONE && mod.why < PORT_MOD_WHY_COUNT ? mod.why : mod.why ? PORT_MOD_WHY_OTHER : PORT_MOD_WHY_NONE;
		if (why == PORT_MOD_WHY_RENAMED && mod.wanted[0])
		{
			bool whole = true;		// only a name the face can show exactly is named
			for (const char *c = mod.wanted; *c; c++)
				if (!shown((unsigned char)*c))
					whole = false;
			if (whole)
			{
				snprintf(out, n, kCeModRenamedTo, mod.wanted);
				return;
			}
		}
		snprintf(out, n, "%s", kCeModWhy[why]);
	}

	// What the mod does, as mod.json describes it; empty when it says nothing.
	void	Description(int row, char *out, int n, CeModShown shown) const
	{
		CeModText(out, n, Mod(row).description, shown);
	}

	// What the mod changes, in words and counts: "Replaces 12 game files. Changes 1 level. Adds 1 level."
	void	Contents(int row, char *out, int n) const
	{
		const PortModInfo &mod = Mod(row);
		int o = 0;
		out[0] = 0;
		if (mod.replaced)
			o += snprintf(out + o, n - o, "Replaces %d game file%s.", mod.replaced, mod.replaced == 1 ? "" : "s");
		if (mod.worlds && o < n)
			o += snprintf(out + o, n - o, "%sChanges %d level%s.", o ? " " : "", mod.worlds, mod.worlds == 1 ? "" : "s");
		if (mod.archives && o < n)
			o += snprintf(out + o, n - o, "%sAdds %d level%s.", o ? " " : "", mod.archives, mod.archives == 1 ? "" : "s");
	}

	// The text of the row's Details box (its name is the page's title while it is up): who made it, what it says of itself (cut to `chars`
	// characters at the end of a word), what it changes, and its state.
	void	Details(int row, char *out, int n, CeModShown shown, int chars = 400) const
	{
		char by[200], text[1100], cut[1100], contents[160], clash[320], why[200];
		ByLine(row, by, sizeof by, shown);
		Description(row, text, sizeof text, shown);
		CeModCut(cut, sizeof cut, text, chars);
		Contents(row, contents, sizeof contents);
		ClashLong(row, clash, sizeof clash, shown);
		Why(row, why, sizeof why, shown);
		int o = 0;
		out[0] = 0;
		const char *const parts[] = {by, NULL, cut, NULL, CanLoad(row) ? contents : "", clash, why, NULL, !CanLoad(row) ? "" : !On(row) ? "This mod is off." :
									 Mod(row).loaded && !Pending() ? "This mod is on." : "This mod is on from the next start."};
		bool gap = false;
		for (const char *part : parts)
		{
			if (!part)
				gap = true;			// a blank line before the next part that has text
			else if (*part && o < n)
			{
				o += snprintf(out + o, n - o, "%s%s", o ? (gap ? "\n\n" : "\n") : "", part);
				gap = false;
			}
		}
	}
};

#endif
