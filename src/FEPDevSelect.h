// FEPDevSelect.h: the storage-device page, choosing where to save or load (reconstructed; Stuart's file is not in his
// release). 0x18 bytes at FRONTEND + 0x39b8, vtable 005db970. On the PC there is one device, always present.

#ifndef FEPDEVSELECT_H
#define FEPDEVSELECT_H

#include "frontendpage.h"

class CFEPDevSelect : public CFrontEndPage
{
public:
	BOOL	Init();
	// Retail 005db970 slot 1: empty override folded with CFEPMain's body.
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	void	Open();								// 004589f0: checks the device and goes on (name open)
	bool	UseDevice();						// 00458710: on to the name, list or a message; false when it could not
	void	CheckDevice(BOOL unused);			// 00458ce0: finds another device when the chosen one has gone
	void	SetCurrentCard(int card);			// 00459810

	// 004595b0: one device's panel, icon and space bar at (x, y) (name ours)
	void	RenderDevice(float x, float y, float used, float total, float z, float scale, SINT alpha, BOOL selected,
						 BOOL hdd, BOOL formatted, BOOL present);

	float	mStartTime;							// +0x04
	int		mCursor;							// +0x08: the device under the cursor
	int		mState;								// +0x0c: 1 when a device is chosen
	int		mCard;								// +0x10: the chosen device; -1 the hard disk, -2 none
	BOOL	mCardsChanged;						// +0x14: set when MEMORYCARD.Update reports a change
};

#endif
