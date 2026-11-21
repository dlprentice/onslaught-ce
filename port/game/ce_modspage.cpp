// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_modspage.cpp: the Onslaught CE page's Mods page (public builds only): every mod in the player's mods folder as a row of
// the game's own options-style list, "Name: On", "Name: Off" or "Name: Can't Load", with what the highlighted mod is under the list (what
// it says of itself, who made it, what it shares with another mod that is on, or why it cannot load and what to do). Choosing a row
// drops down the game's own list of choices, here the things that can be done with the mod: Turn On or Turn Off, Move Up, Move Down
// (the order of the list is the order the mods load in: a lower mod wins a file two of them change) and Details. A change is written
// to ce.ini's mods= line at once and applies at the next start, which the page says while that is so. Nothing else is written, no mod
// file is changed, and the running game keeps the mods it loaded.
//
// What the page knows and every sentence it shows are in ce_modspage_model.h (run on the host by port/tools/ce_modspage_tests.py); this
// file is the drawing and the input, on the game's menu classes, inside the CE page's frame (ce_frontend.cpp) as its other lists are.
//
// Offered only with mods_menu=1 in ce.ini (an unfinished feature, off by default, read once) and only in a build whose mod loader is on,
// and never while the CE menus are off (replays, recordings, the test harness): those runs see exactly the CE page they saw before.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "FEMessBox.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "text.h"
#include "TransitionHelpers.h"
#include "data/MissionScripts/text/text.stf"
#include "ce_modspage.h"
#include "ce_modspage_model.h"

#include <stdio.h>
#include <string.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)

bool			PortCeMenus();								// ce_frontend.cpp
extern "C" int	CE_SettingGetBool(const char *key, int def);	// ce.cpp
extern "C" int	CE_OpenModsFolder(void);					// ce.cpp
bool			PortCeBonusModLevelAvailable(int level);	// ce_bonus.cpp: an active mod supplies the level
bool			PortCeBonusStartModLevel(int level);

namespace {

// Text colours are authored for a doubling colour stage (ce_frontend.cpp, CCeBack::kGrey): the colour given is half of what is seen.
const DWORD	kCream = 0xff7f6f5f;			// what describes (the colour of the original's Multiplayer description): about 253, 221, 189 on the screen
const DWORD	kGrey = 0xff545454;				// a note: about 168 grey on the screen
const DWORD	kRed = 0xff6e1e1e;				// a failed state's word: about 220, 60, 60 on the screen

// The list's rows: ten mods on one page, or nine and More Mods when there are more; then Open Mods Folder and Back. With twelve items the
// list is centred so that its first row sits just under the title bar, and three lines of hint and the page's note fit between Back and the
// help bar.
const int	kSlots = kCeModsSlots;
const float	kLabelMax = 236.f;				// a row's name and colon end at x 320 and start no further left than the frame's left arc
const float	kHintWidth = 420.f;
const float	kNoteFoot = 449.f;				// the note ends here: the help bar starts at y 455

CeModsModel	sModel;

int		Shown(int c)
{
	return PLATFORM.Font(FONT_SMALL)->CanDisplay((WCHAR)c) ? 1 : 0;
}

// Latin-1 text in the game's 2-byte characters (the model has already kept to the face's characters).
void	Widen(WCHAR *out, int n, const char *in)
{
	int i = 0;
	for (; in[i] && i < n - 1; i++)
		out[i] = (WCHAR)(unsigned char)in[i];
	out[i] = 0;
}

int		Width(const WCHAR *text)
{
	SIZE size;
	PLATFORM.Font(FONT_SMALL)->GetTextExtent((WCHAR *)text, &size);
	return size.cx;
}

// text wrapped to the hint's width, each line centred on the page, from y down, at most `most` lines: a text that needs more ends its
// last line with "..." (the whole of it is in the mod's Details). Returns the number of lines drawn. Only the first kHintChars characters
// are wrapped (the lines under the list hold fewer): the game's word wrap has room for forty lines of a hundred characters and checks
// neither, and the model has already broken every word that is wider than a line (kCeModWord).
const int	kHintChars = 300;

int		Lines(const char *text, float y, DWORD colour, int most)
{
	if (!text || !*text || most <= 0)
		return 0;
	static SWordWrapFill fill;
	static WCHAR buf[kHintChars + 1];
	Widen(buf, kHintChars + 1, text);
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	int n = font->WordWrap(&fill, buf, kHintWidth);
	if (n > most)
	{
		n = most;
		WCHAR *last = fill.c[n - 1];
		int length = 0;
		while (last[length])
			length++;
		if (length > 96)
			length = 96;
		for (;; length--)
		{
			while (length > 0 && last[length - 1] == ' ')
				length--;
			last[length] = last[length + 1] = last[length + 2] = '.';
			last[length + 3] = 0;
			if (length <= 0 || float(Width(last)) < kHintWidth)
				break;
		}
	}
	const float line = float(font->GetHeight());
	for (int l = 0; l < n; l++)
		font->DrawTextWithShadow(320.f - float(Width(fill.c[l]) / 2), y + line * l, colour, fill.c[l], 0, 0, 1.f, 1.f);
	return n;
}

// The number of lines the text takes at the hint's width.
int		LineCount(const char *text)
{
	if (!text || !*text)
		return 0;
	static SWordWrapFill fill;
	static WCHAR buf[kHintChars + 1];
	Widen(buf, kHintChars + 1, text);
	return PLATFORM.Font(FONT_SMALL)->WordWrap(&fill, buf, kHintWidth);
}

// The number of lines a message box takes for the text: each paragraph (a line break starts one, a blank line is one line) wrapped to the box.
int		BoxLines(const char *text)
{
	static SWordWrapFill fill;
	static WCHAR buf[2400];
	int total = 0;
	for (const char *p = text;;)
	{
		const char *end = strchr(p, '\n');
		const int length = end ? int(end - p) : int(strlen(p));
		if (!length)
			total += 1;
		else
		{
			char paragraph[1200];
			snprintf(paragraph, sizeof paragraph, "%.*s", length < 1199 ? length : 1199, p);
			Widen(buf, 2400, paragraph);
			total += PLATFORM.Font(FONT_SMALL)->WordWrap(&fill, buf, kHintWidth);
		}
		if (!end)
			return total;
		p = end + 1;
	}
}

class CCeModsMenu;
CCeModsMenu	*sMenu = NULL;
bool		sActive = false;
bool		sDetailsUp = false;			// the Details box is up: the title bar names the mod
WCHAR		sDetailsTitle[40];

void	Act(int row, int action);		// below: what a row's choice does
void	OpenFolder();

//*********************************************************************************
// A mod's row: the game's own choice item (its label, its value after the colon, and the list that drops down when it is chosen). The
// value is the mod's state; the list holds what can be done with the mod. The item has no value of its own (GetValue is -1, as a cheat
// switch that cannot be read has), so the game's item calls SetValue for whichever line of the list is chosen, by a key or by a click.
// A slot shows the mod of row mRow, or nothing (mRow -1: not selectable; no height either, unless the page has more than one page: then it
// keeps its place so that the list does not move when the last page turns up with fewer mods) when the page has fewer mods than slots.
class CModRow : public CMenuMenuItem
{
public:
	CModRow() : CMenuMenuItem((EPCText)0, 3, false), mRow(-1), mBlank(false), mKeep(false)
	{
		mLabel[0] = 0;
		mValue = mSelection = -1;
	}

	// The row this slot shows from now on: its label is the mod's name and a colon (no space after it, as the game's rows have none), the
	// name cut where a word ends, with "...", where it is too long to end at the list's centre without crossing the frame.
	void	Bind(int row, bool keep)
	{
		mRow = row;
		mKeep = keep;
		mExpanded = false;
		mValue = mSelection = -1;
		mLabel[0] = 0;
		if (row < 0)
			return;
		char name[80];
		CeModName(name, sizeof name, sModel.Mod(row), Shown);
		for (int length = (int)strlen(name);; length--)
		{
			char cut[96], label[100];
			CeModCut(cut, sizeof cut, name, length);
			snprintf(label, sizeof label, "%s:", cut);
			Widen(mLabel, int(sizeof(mLabel) / sizeof(mLabel[0])), label);
			if (length <= 1 || float(Width(mLabel)) <= kLabelMax)
				break;
		}
	}

	int		Count()
	{
		int actions[kCeModsMaxActions];
		return mRow < 0 ? 0 : sModel.Actions(mRow, actions);
	}

	// The game's item draws its label in a colour that pulses (two seconds, to black) while its value differs from the highlighted line of
	// its list: the cue of an option that has been changed and not yet accepted. Here the item has no value, so the pulse would run the whole
	// time the list is open. While the list is open the item is told that its value is the highlighted line, when it first asks (the start of
	// its drawing), and its value is made nothing again as soon as the list's lines are asked for (GetChoiceText, below) and whenever a key
	// reaches it, so that the label is steady and yet a choice is still a change when Select or a click takes it.
	virtual WCHAR	*GetText() { return mLabel; }
	virtual BOOL	IsEnabled() { return mRow >= 0; }
	virtual int		GetHeight() { return (mRow >= 0 || mKeep) ? 20 : 0; }
	virtual int		GetValue() { return mExpanded ? mSelection : -1; }
	virtual int		GetNumChoices() { return Count(); }
	virtual void	Refresh() {}

	// Closed, the item draws one text after the label, the mod's state; open, the lines of its list.
	virtual WCHAR	*GetChoiceText(int n)
	{
		static WCHAR text[32];
		text[0] = 0;
		if (n == 0 && mExpanded)
			mValue = -1;
		if (mRow < 0 || mBlank)
			return text;
		int actions[kCeModsMaxActions];
		const int count = sModel.Actions(mRow, actions);
		if (mExpanded && n >= 0 && n < count)
			Widen(text, 32, sModel.ActionLabel(mRow, actions[n]));
		else
			Widen(text, 32, sModel.State(mRow));
		return text;
	}

	virtual void	SetValue(int choice)
	{
		int actions[kCeModsMaxActions];
		if (mRow >= 0 && choice >= 0 && choice < sModel.Actions(mRow, actions))
			Act(mRow, actions[choice]);
	}

	// A row that is not highlighted shows "Can't Load" in the failed state's red (highlighted, it is the focus yellow like any row): the
	// game's item draws label and value in one colour, so it is given an empty value and the word is drawn here, where it would be.
	virtual void	Render(float x, float y, BOOL selected)
	{
		if (mRow < 0)
			return;
		mBlank = !selected && !mExpanded && !sModel.CanLoad(mRow);
		CMenuMenuItem::Render(x, y, selected);
		if (mBlank)
		{
			mBlank = false;
			PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(x + 2.f, y, kRed, GetChoiceText(-1), 0, 0, 1.f, 1.f);
		}
	}

	// Select opens the list with its first line highlighted (the game's item would open it with none, as the item has no value), or, for a
	// row whose list would have one line, does that thing. Everything else is the game's item: Left and Right open the list too, Up and
	// Down move in it, Select takes the highlighted line, Back closes it.
	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val)
	{
		if (mRow < 0)
			return;
		mValue = -1;
		if (!mExpanded)
		{
			const int count = Count();
			if (button == BUTTON_FRONTEND_MENU_SELECT)
			{
				CFrontEnd::PlaySound(FES_SELECT);
				if (count < 2)
					SetValue(0);
				else
				{
					mExpanded = true;
					mSelection = 0;
				}
				return;
			}
			if (count < 2)
				return;
		}
		CMenuMenuItem::ReceiveButtonAction(from_controller, button, val);
	}

	int		mRow;
	bool	mBlank;
	bool	mKeep;
	WCHAR	mLabel[96];
};

// A row of the list that is not a mod: its words, and whether it is there.
class CModsLine : public CMenuItem
{
public:
	CModsLine(const WCHAR *text, bool always) : CMenuItem((EPCText)0, 3), mText(text), mShown(always), mAlways(always) {}

	virtual WCHAR	*GetText() { return (WCHAR *)mText; }
	virtual BOOL	IsEnabled() { return mShown; }
	virtual int		GetHeight() { return mShown ? 20 : 0; }
	virtual void	Render(float x, float y, BOOL selected)
	{
		if (mShown)
			CMenuItem::Render(x, y, selected);
	}

	const WCHAR	*mText;
	bool		mShown;
	bool		mAlways;
};

//*********************************************************************************
class CCeModsMenu : public CPauseMenu
{
public:
	CCeModsMenu();

	int		PerPage() const { return CeModsPerPage(sModel.mCount); }
	int		Pages() const { return CeModsPages(sModel.mCount); }

	// The row the list's highlighted item shows, or -1 (More Mods, Open Mods Folder, Back).
	int		SelectedRow() const
	{
		return mList->mSelected < kSlots ? mRows[mList->mSelected]->mRow : -1;
	}

	// The slots as the model and the page stand, the list centred for the number of items it shows, the highlight on an item that is there.
	void	Bind()
	{
		if (mPageIndex >= Pages())
			mPageIndex = Pages() - 1;
		const bool paged = Pages() > 1;
		int shown = 0;
		for (int slot = 0; slot < kSlots; slot++)
		{
			const int row = mPageIndex * PerPage() + slot;
			const bool there = slot < PerPage() && row < sModel.mCount;
			mRows[slot]->Bind(there ? row : -1, paged && slot < PerPage());
			shown += there;
		}
		mMore->mShown = paged;
		// MenuOptionsList.cpp's Render centres its items of 20 pixels on mY: with n of them the first is at mY - 10n - 15 and Back, the last,
		// at mY + 10n - 35. A short list is centred on 300 as the CE page's other lists are; a longer one is raised so that Back is no lower
		// than 325, which leaves three lines of hint and the page's note between it and the help bar (with twelve items the first row is at
		// 105, CE Settings' place). A page of a list of pages has the same number of items on every page, so that nothing moves when it turns.
		const int items = (paged ? PerPage() + 1 : shown) + 2;
		const float raised = 368.f - 10.f * float(items);
		mList->mY = raised < 300.f ? raised : 300.f;
		if (!mList->mItems.At(mList->mSelected)->IsEnabled())
			mList->mSelected = shown ? 0 : kSlots + 1;
	}

	// The highlight on the row, on whichever page it is.
	void	Focus(int row)
	{
		if (row < 0)
			row = 0;
		mPageIndex = row / PerPage();
		mList->mSelected = row % PerPage();
		Bind();
	}

	// Reads the mods again and shows the first page (the page has just been opened).
	void	Open()
	{
		sModel.Read();
		mPageIndex = 0;
		mList->mSelected = 0;
		Bind();
		mTurned = true;
	}

	void	Close()
	{
		sActive = false;
		CFrontEnd::PlaySound(FES_BACK);
	}

	// One Select reaches the page as two actions in one update, the front end's and the pause menu's, in the order of the input's bindings
	// (ce_frontend.cpp, CCeMenu). The front end's is the rows' (the list hands it to the highlighted item); the pause menu's chooses More
	// Mods, Open Mods Folder or Back. So that the front end's select of the update that opened the page, or turned it, is not taken by the
	// row the highlight lands on, the page takes none until the next update (mTurned, cleared by Process); and, like every page, none in its
	// first 0.4 s.
	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val)
	{
		if (FEMESSBOX.BeingDisplayed() || (PLATFORM.GetSysTimeFloat() - mOpenTime < 0.4f))
			return;
		if ((button == BUTTON_FRONTEND_MENU_SELECT) && mTurned)
			return;
		if (mList->PassToSelected(from_controller, button, val))
			return;
		switch (button)
		{
		default:
			mList->ReceiveButtonAction(from_controller, button, val);
			break;
		case BUTTON_PAUSE_MENU_SELECT:
		{
			CMenuItem *item = mList->mItems.At(mList->mSelected);
			if (item == mMore)
			{
				mPageIndex = (mPageIndex + 1) % Pages();
				mList->mSelected = 0;
				Bind();
				mList->mSelected = kSlots;			// the highlight stays on More Mods: Select, Select, Select walks the pages
				mTurned = true;
				CFrontEnd::PlaySound(FES_SELECT);
			}
			else if (item == mOpen)
			{
				mTurned = true;
				CFrontEnd::PlaySound(FES_SELECT);
				OpenFolder();
			}
			else if (item->mText == GI_BACK)
				Close();
			break;
		}
		case BUTTON_FRONTEND_MENU_BACK:
			Close();
			break;
		}
	}

	// Under the list: what the highlighted row is, in a block that has the same shape on every row (at most three lines: what it says of
	// itself, what it shares with another mod, who made it) and then the page's note. Drawn by Back, the list's last item, after itself (as
	// the CE page's hints are). What describes is cream, who made it and the note are grey.
	void	DrawHints(float backY)
	{
		CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
		const float line = float(font->GetHeight());
		const char *note = sModel.Note();
		const float noteTop = kNoteFoot - line * float(LineCount(note));
		if (note)
			Lines(note, noteTop, kGrey, 2);
		float y = backY + 20.f + line * 0.5f;
		if (!sModel.mCount)
		{
			// no mod at all: how to add one
			y += line * float(Lines(kCeModsEmpty, y, kCream, 3)) + 4.f;
			Lines(kCeModsEmptyWhere, y, kGrey, 2);
			return;
		}
		CMenuItem *item = mList->mItems.At(mList->mSelected);
		if (item == mMore)
		{
			char text[80];
			snprintf(text, sizeof text, Pages() > mPageIndex + 1 ? kCeModsPageNext : kCeModsPageFirst,
					 mPageIndex + 1, Pages());
			Lines(text, y, kCream, 1);
			return;
		}
		if (item == mOpen)
		{
			Lines(kCeModsOpenHint, y, kCream, 1);
			return;
		}
		const int row = SelectedRow();
		if (row < 0)
			return;
		char text[1100], by[200], clash[320];
		sModel.ByLine(row, by, sizeof by, Shown);
		if (!sModel.CanLoad(row))
		{
			// what is wrong and what to do, then whose mod it is
			sModel.Why(row, text, sizeof text, Shown);
			const int used = Lines(text, y, kCream, 2);
			Lines(by, y + line * float(used), kGrey, 1);
			return;
		}
		sModel.Description(row, text, sizeof text, Shown);
		sModel.Clash(row, clash, sizeof clash, Shown);
		int used = Lines(text, y, kCream, clash[0] ? 1 : 2);
		used += Lines(clash, y + line * float(used), kCream, 1);
		Lines(by, y + line * float(used), kGrey, 1);
	}

	// CPauseMenu::Render for an options-style page, as the CE page draws its own (ce_frontend.cpp): the render states, then the list.
	void	Draw()
	{
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		RENDERINFO.SetFogEnabled(false);
		RENDERINFO.SetUnknown2ed(false);
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
		LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
		LT.SRS(D3DRS_ALPHAREF, 8);
		LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
		LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
		LT.SRS(D3DRS_ZENABLE, FALSE);
		RENDERINFO.Apply();
		mList->Render(this);
		LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
		LT.SRS(D3DRS_ZENABLE, TRUE);
	}

	CMenuOptionsList	*mList;
	CModRow				*mRows[kSlots];
	CModsLine			*mMore, *mOpen;
	int					mPageIndex;
	bool				mTurned;
};

// The page's Back item, the list's last: after itself it draws the highlighted row's hint and the page's note. Nothing is drawn while a
// message box is shown over the list.
class CModsBack : public CMenuItem
{
public:
	CModsBack(CCeModsMenu *menu) : CMenuItem((DWORD)GI_BACK, 3), mMenu(menu) {}

	virtual void	Render(float x, float y, BOOL selected)
	{
		CMenuItem::Render(x, y, selected);
		if (!FEMESSBOX.BeingDisplayed())
			mMenu->DrawHints(y);
	}

	CCeModsMenu	*mMenu;
};

CCeModsMenu::CCeModsMenu() : CPauseMenu(PORT_CE_NO_LISTS), mPageIndex(0), mTurned(false)
{
	static WCHAR title[] = L"Mods";
	static const WCHAR more[] = L"More Mods", open[] = L"Open Mods Folder";
	mList = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 300.f, FALSE, 0xb0000000);
	for (int slot = 0; slot < kSlots; slot++)
		mList->AddItem(mRows[slot] = new (MEMTYPE_MENUITEM) CModRow());
	mList->AddItem(mMore = new (MEMTYPE_MENUITEM) CModsLine(more, false));
	mList->AddItem(mOpen = new (MEMTYPE_MENUITEM) CModsLine(open, true));
	mList->AddItem(new (MEMTYPE_MENUITEM) CModsBack(this));
	mLists.Append(mList);
}

// What a row's choice does. A change is written at once (the model); one that cannot be made says why in the game's message box.
void	Act(int row, int action)
{
	if (!sMenu)
		return;
	int result = CEMR_DONE;
	switch (action)
	{
	case CEMA_TOGGLE:
	case CEMA_UP:
	case CEMA_DOWN:
		result = action == CEMA_TOGGLE ? sModel.Toggle(row) : sModel.Move(row, action == CEMA_UP ? -1 : 1);
		if (result == CEMR_DONE)
			sMenu->Focus(sModel.mAt);		// the highlight goes with the mod, to the place the change left it in, so it can be moved again
		else
			sMenu->Bind();
		break;
	case CEMA_DETAILS:
	{
		// the mod's name is the title bar's while its Details are up, so the box holds the by-line first; what it says of itself is cut so
		// that the box (16 lines at most) stays below the title bar and above the help bar
		static char text[2400];
		for (int chars = 400;; chars -= 40)
		{
			sModel.Details(row, text, sizeof text, Shown, chars);
			if (BoxLines(text) <= 16 || chars <= 40)
				break;
		}
		char name[80], cut[96];
		CeModName(name, sizeof name, sModel.Mod(row), Shown);
		CeModCut(cut, sizeof cut, name, 18);
		Widen(sDetailsTitle, int(sizeof(sDetailsTitle) / sizeof(sDetailsTitle[0])), cut);
		sDetailsUp = true;
		PortCeShowMessage(text, 122.f);
		break;
	}
	}
	if (action >= CEMA_PLAY)
	{
		// a level the mod adds: out of the CE page and into retail's two-player start page, as the bonus levels go; it comes back here
		const int level = sModel.PlayLevel(row, action);
		if (!level || !PortCeBonusModLevelAvailable(level))
			PortCeShowMessage(kCeModsNoLevel);
		else if (PortCeBonusStartModLevel(level))
		{
			PortCeLeaveCePage();
			sActive = false;
		}
		else
			PortCeShowMessage(kCeModsNoLevel);
		return;
	}
	if (result == CEMR_FULL)
		PortCeShowMessage(kCeModsFull);
	else if (result == CEMR_NOT_SAVED)
		PortCeShowMessage(kCeModsNotSaved);
}

// Open Mods Folder: the desktop shows the folder (made first when it is not there); where it cannot, the box says where the folder is, or,
// when something that is not a folder is named mods, that it must be renamed or removed.
void	OpenFolder()
{
	const int opened = CE_OpenModsFolder();
	if (opened > 0)
		PortCeShowMessage(kCeModsFolderOpen);
	else if (opened < 0)
		PortCeShowMessage(kCeModsFolderBlocked);
	else
	{
		char folder[1100], text[1300];
		PortCeFolderText(folder, sizeof folder, "mods");
		snprintf(text, sizeof text, "%s %s\n%s", kCeModsNoFileManager, kCeModsFolderIs, folder);
		PortCeShowMessage(text);
	}
}

}

//*********************************************************************************
// The CE page's side (ce_frontend.cpp).

// Whether the hub lists the Mods row: the CE menus are on, ce.ini's mods_menu line is 1 (read once: a later edit applies at the next
// start) and this build's mod loader is on (a build with it off could change nothing a game would use).
bool	PortCeModsPageOffered()
{
	static int on = -1;
	if (on < 0)
		on = (PortCeMenus() && !PortModsPlatformOff() && CE_SettingGetBool("mods_menu", 0) == 1) ? 1 : 0;
	return on == 1;
}

void	PortCeModsPageOpen()
{
	if (!PortCeModsPageOffered())
		return;
	if (!sMenu)
		sMenu = new (MEMTYPE_MENUITEM) CCeModsMenu();
	CPauseMenu::mOptionsPage = true;
	sMenu->Activate(0);
	sMenu->Open();
	sActive = true;
}

static bool	Active()
{
	return sActive && sMenu;
}

// The page's input while the Mods page is open; false when it is not, and the CE page's own menu takes the input.
bool	PortCeModsPageButton(SINT button, float val)
{
	if (!Active())
		return false;
	sMenu->ReceiveButtonAction(NULL, button, val);
	return true;
}

// A new update: the page takes the front end's select again (CCeModsMenu::mTurned).
void	PortCeModsPageProcess()
{
	if (sMenu)
		sMenu->mTurned = false;
}

// Draws the Mods page in the CE page's frame, as CFEPCe::Render draws its own lists: the borders, the list (not behind a message box,
// which is translucent), the title bar (with the mod's name while its Details are up), the help line and the small logo. False when the
// Mods page is not open: nothing was drawn.
bool	PortCeModsPageRender(float transition, EFrontEndPage dest)
{
	if (!Active())
		return false;
	static WCHAR title[] = L"Mods";
	if (!FEMESSBOX.BeingDisplayed())
		sDetailsUp = false;
	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);
	if (!FEMESSBOX.BeingDisplayed())
		sMenu->Draw();
	FRONTEND.DrawTitleBar(sDetailsUp ? sDetailsTitle : title, transition, dest);
	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(RangeTransition(transition, 0.75f, 1.f));
	return true;
}

// The front end is back from a level a mod adds that the Mods page started (ce_frontend.cpp, as the CE page opens): the page is open again, that
// mod's row highlighted. False when the page is not offered.
bool	PortCeModsPageReturn(int level)
{
	if (!PortCeModsPageOffered())
		return false;
	PortCeModsPageOpen();
	const int row = sModel.ReturnRow(level);
	if (row >= 0)
		sMenu->Focus(row);
	return true;
}

// The CE page is going: so is this one.
void	PortCeModsPageShutdown()
{
	SAFE_DELETE(sMenu);
	sActive = false;
	sDetailsUp = false;
}

// The main menu is being drawn (ce_frontend.cpp, PortCeMainRows). Once in a process, a second after the main menu first shows and with no
// other message up: when a mod that ce.ini enables did not load, the game's message box says how many and where to see why, so that a mod
// is never quietly left out. Only where the Mods page is offered (the box names it); nothing is asked of the loader anywhere else.
void	PortCeModsPageMainMenu()
{
	static int state = 0;			// 0 not seen, 1 waiting for its moment, 2 done
	static float since = 0.f;
	if (state == 2)
		return;
	if (state == 0)
	{
		state = PortCeModsPageOffered() ? 1 : 2;
		since = PLATFORM.GetSysTimeFloat();
		return;
	}
	if ((PLATFORM.GetSysTimeFloat() - since < 1.f) || FEMESSBOX.BeingDisplayed() || (FRONTEND.GetCurrentPage() != FEP_MAIN))
		return;
	state = 2;
	const int refused = PortModsRefusedAtStart();
	if (refused <= 0)
		return;
	char text[120];
	if (refused == 1)
		snprintf(text, sizeof text, "%s", kCeModsNoticeOne);
	else
		snprintf(text, sizeof text, kCeModsNoticeMany, refused);
	PortCeShowMessage(text);
	CFrontEnd::PlaySound(FES_SELECT);
}

#endif
