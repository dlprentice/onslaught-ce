#ifndef FEPSAVEGAME_H
#define FEPSAVEGAME_H

#include "frontendpage.h"
#include "FrontEndText.h"

class	CFEPSaveGame : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	
	void	Process(EFEPState state);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	
	void	ButtonPressed(SINT button,float val);
	
	BOOL	IsCheatActive(int cheatno);
	
	void	TransitionNotification(EFrontEndPage from);

	// some code is shared by other modules too so exposed.
	static void RemovedMUWhinge(enum EFETextHack reason);
	void AskIfYouWantToDelete(BOOL career_in_progress, BOOL because_4096, BOOL no_space_for_bea);
	
protected:
	void	CreateSave();
	
protected:
	float			mStartTime;
	SINT			mCX, mCY;
	
	// Save Game specific variables
	BOOL				mCareerWasInProgress; // does the game being saved have no content to speak of? We'll give a different success message.
	
public:
	// people want to hack into this.
	WCHAR				mSaveGameName[256];
	int					mExistingSave;
	WCHAR				mExistingSaveName[256];

#if TARGET == XBOX
	// Some error messages require us to refer to the memory card that we saved to, "card xxx was removed during saving"
	WCHAR				mSavedToCardName[256];
#endif
};

#endif
