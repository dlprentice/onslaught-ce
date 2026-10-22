// FEPE3LevelSelect.h: the E3 show demo's level select, a strip of up to 50 columns of up to six levels
// (reconstructed; Stuart's file is not in his release). 0x3480 bytes at FRONTEND + 0x8848, vtable 005db4fc; the PC
// build fills in four levels (110, 200, 231, 311) and returns to the title after 750 idle frames.

#ifndef FEPE3LEVELSELECT_H
#define FEPE3LEVELSELECT_H

#include "frontendpage.h"

#define E3_COLUMNS		50
#define E3_ROWS			6
#define E3_LINKS		4

// A line from one level to a level in a later column (CFEPE3LevelSelect::Render 0045a444..0045a4db).
struct SE3Link
{
	SINT	mColumn;
	SINT	mRow;
};

class CFEPE3LevelSelect : public CFrontEndPage
{
public:
	CFEPE3LevelSelect();								// 00459920
	BOOL	Init();
	void	Shutdown();									// 00460490, folded with CFEPLevelSelect
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);
	void	ActiveNotification(EFrontEndPage from);

	SINT	mColumnLength[E3_COLUMNS];					// +0x04
	SINT	mLevels[E3_COLUMNS][E3_ROWS];				// +0xcc
	float	mTransition[E3_COLUMNS][E3_ROWS];			// +0x57c
	SINT	mNumLinks[E3_COLUMNS][E3_ROWS];			// +0xa2c: zeroed by the constructor; none on the PC
	SE3Link	mLinks[E3_COLUMNS][E3_ROWS][E3_LINKS];		// +0xedc
	SINT	mNumColumns;								// +0x345c
	float	mPos, mPosDest;								// +0x3460, +0x3464
	SINT	mColumn;									// +0x3468
	SINT	mRow;										// +0x346c
	float	mEpisodeTime;								// +0x3470: when the level's hundreds changed
	float	mCursorTime;								// +0x3474: when the cursor moved
	float	mStartTime;									// +0x3478
	SINT	mIdleFrames;								// +0x347c
};

#endif
