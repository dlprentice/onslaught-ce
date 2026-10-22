// reconnectinterface.h: the controller-reconnection prompts (RECONNECT_INTERFACE at 0089be50, vtable 005e4a64);
// only what matched files need so far.

#ifndef RECONNECTINTERFACE_H
#define RECONNECTINTERFACE_H

#include "game.h"

class CReconnectInterface : public IController
{
public:
	enum EState
	{
		OK = 0,
		DISCONNECTED,				// asks for the controller to be reconnected
		RECONNECTED,				// asks for START
		RESUMING					// START was pressed: Process lets go of the controller
	};

	virtual void	ReceiveButtonAction(CController* from_controller, int button, float val);
	virtual BOOL	CanBeControlledWhenInPause();
	virtual EControlType	GetControlType();

	EState	GetState() { return mState; }
	bool	Process(CController *controller, bool a);				// 00527c50 (CFrontEnd::Process 00466c67)
	void	Render(class CViewport *viewport, CController *controller, BOOL multiplayer);	// 00527990 (CFrontEnd::Render 00468479)
	void	Reset(int state);										// 00527960 (CFEPIntro::TransitionNotification; name open)

	EState	mState;						// +0x08
};

extern CReconnectInterface	RECONNECT_INTERFACE[2];		// 0089be50

#endif
