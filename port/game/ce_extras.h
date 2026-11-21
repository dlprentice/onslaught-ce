// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_extras.h: the Extras pages of the Onslaught CE page (ce_extras.cpp): the Companion's player features inside the
// game. Unfinished, so offered only with the line extras=1 under [Experimental] in ce.ini, and only where the CE menus exist.
#ifndef CE_EXTRAS_H
#define CE_EXTRAS_H

// The seam in the Onslaught CE page (ce_frontend.cpp). Offered: the hub lists its Extras row. Active: an Extras page is showing,
// and the CE page hands it its input, its drawing and its title. Process runs once an update with whether the CE page is the active
// page (anything else closes Extras). Shutdown is the front end going away.
bool	PortCeExtrasOffered();
bool	PortCeExtrasActive();
void	PortCeExtrasOpen();
void	PortCeExtrasButton(int button, float val);
void	PortCeExtrasProcess(bool pageActive);
void	PortCeExtrasDraw();
WCHAR	*PortCeExtrasTitle();
void	PortCeExtrasShutdown();

// The pages. A page is a list of the game's option rows (the Extras list, a career's record) or a list box (ce_scroll_list.h).
enum ECeExtrasPage
{
	XP_HUB = 0,			// Extras: its pages as rows
	XP_CAREERS,			// Service Record: the careers, in a list box
	XP_RECORD,			// one career's record: label and value rows
	XP_MISSIONS,		// its missions, in a list box
	XP_GOODIES,			// its Goodies, in a list box
	XP_MUSIC,			// Music Player: the tracks, in a list box
	XP_CATALOGUE,		// Goodies Catalogue: the careers, then one career's Goodies with the way to earn each
	XP_LORE,			// Lore: the articles, in a list box
	XP_ARTICLE,			// one article, read in a panel
	XP_CONTENTS,	// headings of the open article
	XP_STORED_SETTINGS,	// read-only details of the selected career
	XP_BACKUP,
	XP_BACKUP_PREVIEW,
	XP_CATALOGUE_LIST,	// the Catalogue's lists (ce_catalogue_list.cpp): opened from the Catalogue, Back returns there
	XP_BACKUP_ALL,		// Back Up All Careers (ce_backup_all_page.cpp): one verified set of every career and the options
	XP_IMPORT_CAREER,	// Import Career (ce_import_career_page.cpp): import an external .bes file as a new career
	XP_EDIT_CAREER,		// Edit Career (ce_career_edit_page.cpp): edit a copy of a career and save as a new career
	XP_COUNT
};

// How a row's right-hand word is drawn.
enum ECeRowTone
{
	CE_TONE_ROW = 0,	// as the row: white when highlighted, grey when not
	CE_TONE_GOOD,		// green: a good state, always with its word
	CE_TONE_DIM,		// dark grey: not available yet
	CE_TONE_BAD			// red: a failed state, always with its word
};

struct SCeListRow
{
	WCHAR	mLeft[80];
	WCHAR	mRight[24];
	int		mTone;		// ECeRowTone, for the right-hand word
	bool	mDim;		// the whole row is drawn dark: not yet open, locked
};

// A list box page: what ce_extras.cpp asks of the feature that fills it. index is a row, 0 to Count() - 1.
struct SCeListPage
{
	const WCHAR	*mTitle;
	int			(*mCount)();
	void		(*mRow)(int index, SCeListRow *row);
	// the highlighted row's hint: a white line and a grey one (either may be left empty); n is each buffer's length
	void		(*mHint)(int index, WCHAR *help, WCHAR *extra, int n);
	// Select on a row; it may open another page (PortCeExtrasShow) and says whether it did anything, for the sound
	bool		(*mSelect)(int index);
	// shown in the box when the list is empty
	const WCHAR	*(*mEmpty)();
	// the page is opening (fill the list) and closing (Back, or the Extras pages going away)
	void		(*mOpen)();
	void		(*mClose)();
	// once an update while the page shows (the music player follows its track)
	void		(*mProcess)();
	// extra drawing under the hint lines, from y down (the music player's time and bar); may be left empty
	void		(*mDraw)(float y);
	// one letter width for the whole page (1 when left 0): rows are never each squeezed to their own width, only cut with "..."
	float		mScale;
	// the row highlighted when the page opens
	int			mStart;
	// rows in the smaller face, centred in their rows, for a list of long lines (the Catalogue's spoken lines); false: the larger
	bool		mSmallFace;
	// pixels the box reaches further right than the Load Game page's (its track and bar move with it), for a list of long lines;
	// the Catalogue's 20 keep it clear of the right arc (nearest at x 563 beside the rows); 0 for every other page
	float		mWiden;
	// rows in view (0: the Load Game page's ten) and where the box starts (0: kCeListTop), for a page with a taller box
	int			mRows;
	float		mTopY;
	// right edge of the box when it shares the width with a pane beside it (0: kCeListRight + mWiden)
	float		mBoxRight;
	// true for a group heading row: drawn in the heading colour, never highlighted, skipped by the keys and wheel; may be left empty
	bool		(*mHeading)(int index);
	// drawing beside the box for the highlighted row (the Catalogue's picture and model), in the rectangle given
	void		(*mPane)(int index, float left, float top, float right, float bottom);
	// optional Left (-1) / Right (+1) handler for adjustable rows: 1 adjusted, -1 at limit (consume key), 0 not adjustable (page list)
	int			(*mAdjust)(int index, int delta);
};

// A reading page: one article in a panel (ce_reading.h). The page draws itself, including the hint under its panel; the host hands it the
// keys and plays the move sound when it says the view moved.
struct SCeReadPage
{
	const WCHAR	*(*mTitle)();
	void		(*mOpen)();
	void		(*mClose)();
	void		(*mDraw)();
	bool		(*mButton)(int button);
};

// The hint lines under a panel or a list box: a white line (or two) and a grey one, wrapped to the hint area and centred, from y down.
float	PortCeExtrasHint(const WCHAR *help, const WCHAR *extra, float y);

// Opens a page with its first row highlighted (Back returns to the row it was opened from).
void	PortCeExtrasShow(int page, int startRow = -1);
// Opens the host straight onto one page, whatever the Extras switch says (the Catalogue has its own), for a page whose Back
// returns to the Onslaught CE page; Hide hands the input back to that page without closing the page (it is shown again later).
void	PortCeExtrasOpenPage(int page, int startRow);
void	PortCeExtrasHide();
void	PortCeExtrasReturnToReader();
// Text helpers shared by the pages: Latin-1 to the game's characters (for the pages' own text); a file name's bytes to its characters
// (UTF-8 on Linux, the ANSI page on Windows); a name in the characters the font has ('-' for the rest).
void	PortCeExtrasWiden(WCHAR *out, int n, const char *in);
void	PortCeExtrasWidenName(WCHAR *out, int n, const char *in);
void	PortCeExtrasInFont(WCHAR *text);

// Careers without loading them (ce_career_view.cpp): the memory card's list (-1 when careers cannot be read now), a career's
// name as Load Game lists it (256 characters), and its record (ce_career_view.h), checked against the game's own accessors.
struct SCeCareerView;
int		PortCeCareerCount();
bool	PortCeCareerName(int index, WCHAR *name);
bool	PortCeCareerRead(const WCHAR *name, SCeCareerView *view);

// The game's message box with one or two sentences, as the CE page shows its own.
void	PortCeExtrasMessage(const char *text);

// The Music Player (ce_jukebox.cpp).
const SCeListPage	*PortCeJukeboxPage();

// The Lore (ce_lore.cpp): the articles as a list, and one article as a reading page.
const SCeListPage	*PortCeLoreListPage();
const SCeListPage *PortCeLoreContentsPage();
const SCeReadPage	*PortCeLoreReadPage();

// The Service Record (ce_service_record.cpp).
const SCeListPage	*PortCeRecordCareersPage();
const SCeListPage	*PortCeRecordMissionsPage();
const SCeListPage	*PortCeRecordGoodiesPage();
const SCeListPage *PortCeRecordSettingsPage();
const SCeListPage *PortCeRecordBackupPage();
const SCeListPage *PortCeBackupPreviewPage();
void PortCeBackupPreviewBack();
int PortCeBackupPreviewStepBack();
const SCeListPage *PortCeBackupAllPage();
void PortCeBackupAllBack();
const SCeListPage *PortCeImportCareerPage();
void PortCeImportCareerBack();
int PortCeImportCareerStepBack();
const SCeListPage *PortCeCareerEditPage();
void PortCeCareerEditBack();
int PortCeCareerEditStepBack();
void PortCeRecordRefreshCareers();
void PortCeRecordBackupBack();
const WCHAR *PortCeBackupCareerName();
const unsigned char *PortCeBackupSnapshot();
#ifdef _WIN32
struct CeBackupOrigin;
const CeBackupOrigin *PortCeBackupWindowsOrigin();
#endif
bool PortCeCareerReadSnapshot(const WCHAR *name, SCeCareerView *view, unsigned char *copy);
const SCeListPage	*PortCeCataloguePage();
// The Catalogue's lists (ce_catalogue_list.cpp) and their title (a group label such as "Models" or "Speech: French").
const SCeListPage	*PortCeCatalogueListPage();
void	PortCeCatalogueListTitle(const char *label);
int		PortCeCatalogueListRowOf(int match);
bool	PortCeGoodiesFromCatalogue();					// the Goodies page was opened from the catalogue, so Back returns there
enum { CE_RECORD_ROWS = 8 };						// the record page's label and value rows
const WCHAR	*PortCeRecordLabel(int row);
const WCHAR	*PortCeRecordValue(int row);
const WCHAR	*PortCeRecordHelp(int row);
const WCHAR	*PortCeRecordExtra(int row);

// Plain reads of what the host and its two backup pages show, for the test kit (ce_extras_testkit.cpp). They change nothing.
class CMenuOptionsList;
int		PortCeExtrasPageShown();					// ECeExtrasPage
const SCeListPage	*PortCeExtrasListShown();		// the shown page's list box, or NULL
int		PortCeExtrasListCursor();					// its highlighted row, or -1
CMenuOptionsList	*PortCeExtrasMenuRows();		// the hub's or a record's rows, or NULL
int		PortCeBackupViewState();					// Back Up Career (ce_backup_page.cpp): 0 idle .. 5 error
bool	PortCeRestoreShown();						// Backup Preview (ce_backup_preview.cpp) is open
int		PortCeRestorePhase();						// its Restore as New flow: 0 idle .. 4 done
int		PortCeRestoreOutcome();						// 0 none, 1 complete .. 11 failed
const char	*PortCeRestoreTarget();					// the new career's file name, or ""
bool	PortCePreviewVerified();					// the selected backup has been read and checked

#endif
