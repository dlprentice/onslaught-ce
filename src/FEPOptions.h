// FEPOptions.h: the PC options page (reconstructed; 0x2c bytes, FRONTEND + 0x40ec, vtable 005db8a8). It shows the
// pause menu in options mode (a CPauseMenu kept at 0089bc30) and remembers the settings it was entered with.

#ifndef FEPOPTIONS_H
#define FEPOPTIONS_H

#include "frontendpage.h"

// The screen position (CAREER.GetScreenPosX/Y), read and written as a pair (0051f470, 0051f490; CFEPScreenPos keeps
// one too).
class CScreenPosition
{
public:
	void	Read();
	void	Write();

	int		mX, mY;
};

class CFEPOptions : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	ButtonPressed(SINT button, float val);
	void	Process(EFEPState state);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	BOOL	IsStateAvailable(int state);

	static void	SaveDefaultOptions(bool from_options);
	static void	WriteDefaultOptionsFile(char *data, int size);
	static void	Cleanup();

	int		mState;								// +0x04
	float	mStartTime;							// +0x08

	// the settings on entry from the main menu
	float	mSoundVolume;						// +0x0c
	float	mMusicVolume;						// +0x10
	struct
	{
		BOOL	mVibration;
		int		mControllerConfigurationNum;
	}		mPlayer[2];							// +0x14
	CScreenPosition	mScreenPosition;			// +0x24
};

#endif
