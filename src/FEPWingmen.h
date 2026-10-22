// FEPWingmen.h: the wingman page, where the player picks Tara, Billy or Jason for the level (reconstructed from the
// retail PC executable; not in Stuart's release). 0x38 bytes at FRONTEND + 0x2ec, vtable 005dba10; the layout is from
// CFEPWingmen::Init (005216c0), Process (00521c80), ButtonPressed (00521d20) and Load (00521ae0).

#ifndef FEPWINGMEN_H
#define FEPWINGMEN_H

#include "FEPBEConfig.h"

class CFEPWingmen : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);
	void	ActiveNotification(EFrontEndPage from) {}		// folded (00452b60)
	void	DeActiveNotification() {}						// folded (0040c640)
	virtual void	Load(CMEMBUFFER &file);			// 00521ae0: one level's entry, the same format as CFEPBEConfig's

	SBEConfigLevel	*GetCurrentLevel();				// 005230e0: the selected level's entry, or NULL
	static SINT		GetNumWingmen();				// 00521650: the selected level's wingmen

	float			mStartTime;						// +0x04: when the page was entered
	CFrontEndThing	*mThing[3];						// +0x08: Tara's, Billy's and Jason's fighters
	float			mTime;							// +0x14: +0.01 a frame
	SINT			mWingman;						// +0x18: the choice, an index into the level's wingmen
	float			mLeftFlash;						// +0x1c: 1 when the choice moves left, fading by 0.1 a frame
	float			mRightFlash;					// +0x20
	float			mUnknown24;						// +0x24: -1000 after Init
	SPtrSet<SBEConfigLevel>	mLevels;				// +0x28
};

#endif
