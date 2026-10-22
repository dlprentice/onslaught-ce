// FEPMultiplayerStart.h: the PC's two-player start page, where each player picks a Battle Engine configuration and
// both confirm (reconstructed from the retail PC executable; not in Stuart's release). 0x34 bytes at FRONTEND + 0x40b8.

#ifndef FEPMULTIPLAYERSTART_H
#define FEPMULTIPLAYERSTART_H

#include "frontendpage.h"

class CFrontEndThing;

class CFEPMultiplayerStart : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	void	ChangeConfig(SINT button, SINT player);			// 0051ddd0: up or down through the level's configurations
	int		GetPlayer1ControllerPort();						// 0051f370 (Stuart's name)

	char			mPort[2];						// +0x04: each player's controller port
	char			mCountdown;						// +0x06: frames left once both are ready
	BOOL			mReady[2];						// +0x08
	CFrontEndThing	*mThing[2];						// +0x10: each player's Battle Engine
	SINT			mConfig[2];						// +0x18: each player's configuration, an index into the level's
	float			mUpFlash[2];					// +0x20: 1 when the configuration goes up, fading by 0.1 a frame
	float			mDownFlash[2];					// +0x28
	float			mTime;							// +0x30: +0.01 a frame
};

#endif
