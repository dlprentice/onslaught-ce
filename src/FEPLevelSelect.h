// FEPLevelSelect.h: the level select, the career's levels in columns by episode with the links between them
// (reconstructed; Stuart's file is not in his release). 0x347c bytes at FRONTEND + 0x360, vtable 005db584. The
// constructor builds the grid from the career's level structure; the E3 show demo's page is a copy of this one.

#ifndef FEPLEVELSELECT_H
#define FEPLEVELSELECT_H

#include "frontendpage.h"

#define LS_COLUMNS		50
#define LS_ROWS			6
#define LS_LINKS		4

struct SLevelSelectLink
{
	SINT	mColumn;
	SINT	mRow;
};

class CFEPLevelSelect : public CFrontEndPage
{
public:
	CFEPLevelSelect();									// 00460140
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);
	void	ActiveNotification(EFrontEndPage from);

	void	SetCurrentLevelToHighestAvailable();		// 00460a40
	void	SyncToSelectedLevel();						// 00460310 (name open)
	void	FindSelectedLevel();						// its body, inlined into Init (004603d0)
	BOOL	IsWorldUnlocked(SINT level);				// 00461a50: cheat 1 opens them all

	// the cursor's row, kept inside its column (inlined into Render: 00460ba1, 00461040, 0046140a; name ours)
	SINT	ClampedRow()
	{
		SINT	row = mRow;
		if (row >= mColumnLength[mColumn])
			row = mColumnLength[mColumn] - 1;
		else if (row < 0)
			row = 0;
		return row;
	}

	SINT				mColumnLength[LS_COLUMNS];					// +0x04
	SINT				mLevels[LS_COLUMNS][LS_ROWS];				// +0xcc
	float				mTransition[LS_COLUMNS][LS_ROWS];			// +0x57c
	SINT				mNumLinks[LS_COLUMNS][LS_ROWS];				// +0xa2c
	SLevelSelectLink	mLinks[LS_COLUMNS][LS_ROWS][LS_LINKS];		// +0xedc
	SINT				mNumColumns;								// +0x345c
	float				mPos, mPosDest;								// +0x3460, +0x3464
	SINT				mColumn;									// +0x3468
	SINT				mRow;										// +0x346c
	float				mEpisodeTime;								// +0x3470: when the level's hundreds changed
	float				mCursorTime;								// +0x3474: when the cursor moved
	float				mStartTime;									// +0x3478
};

#endif
