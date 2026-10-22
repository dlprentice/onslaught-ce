#ifndef FEPLOADGAME_H
#define FEPLOADGAME_H

#include "frontendpage.h"

class	CFEPLoadGame : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	
	void	Process(EFEPState state);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	
	void	ButtonPressed(SINT button,float val);
	
	void	TransitionNotification(EFrontEndPage from);
	
protected:
	void	StartLoad();
protected:
	float	mStartTime;
	
	float	mXPos, mXDest;
	float	mYPos, mYDest;
	
	BOOL	mMUInserted[8];
	
	SINT	mCX, mCY;

	// Other people need to poke into this.
public:
	int		mSaveGameNumber;
	WCHAR	mSaveGameName[MAX_PATH];
};

#endif
