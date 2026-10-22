// FEPLanguageTest.h: the developers' page that steps through the message-box strings and plays each one's spoken
// sample (reconstructed; not in Stuart's release). 0x118 bytes at FRONTEND + 0xbcc8, vtable 005db7e0.

#ifndef FEPLANGUAGETEST_H
#define FEPLANGUAGETEST_H

#include "frontendpage.h"

class CSample;

class CFEPLanguageTest : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	void	PlaySample();

	float		mStartTime;					// +0x04 (TransitionNotification, folded with CFEPSaveGame's 00464b10)
	SINT		mCurrent;					// +0x08: the string shown
	float		mScroll;					// +0x0c: the analogue scroll's fraction
	SINT		mDetail;					// +0x10: 0..40
	CSample		*mSample;					// +0x14
	char		mSampleName[256];			// +0x18
};

#endif
