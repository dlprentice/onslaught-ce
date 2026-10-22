// mouse.h: the mouse cursor and buttons, kept by the object after mixermap (its window-message handler 005234e0);
// only what matched files need so far. Names are from use.

#ifndef MOUSE_H
#define MOUSE_H

struct SMouseButton
{
	BOOL	mDown;					// +0x00
	BOOL	mReleased;				// +0x04: released since last read
	int		mX;						// +0x08: where it went down (front-end coordinates while the front end runs)
	int		mY;						// +0x0c
	float	mFracX;					// +0x10: the same as a fraction of the window
	float	mFracY;					// +0x14
};

extern int			MOUSE_Y;					// 0089bda4
extern int			MOUSE_X;					// 0089bda8
// STAND-IN for retail's unknown declaration of 0089bdf4..0089bdf7: the cursor-moved flag and the three held flags sit back to
// back (HELD at moved+1, extent of mouse.obj's .bss 180), which two separate definitions do not give (VC6 aligns a lone
// 3-byte array to 4, 188). One array keeps the declaration count of the header it replaces.
extern bool			gMouseFlags[4];				// 0089bdf4: [0] the cursor moved (mouse.cpp's sMouseMoved), [1..3] held
#define MOUSE_HELD		(gMouseFlags + 1)		// 0089bdf5: left, middle, right
extern SMouseButton	MOUSE_BUTTONS[3];			// 0089bdf8: left, middle, right
extern int			MOUSE_WHEEL;				// 0089be48
extern UBYTE		MOUSE_CURSOR_ALPHA;			// 00640054

LRESULT __stdcall	HandleMouseMessage(UINT msg, WPARAM wParam, LPARAM lParam);	// 005234e0 (PCLTShell::MsgProc)
void	ReleaseMouseCursor();					// 00523b30 (CSystem::Shutdown)

void	ResetMouseTransientState();				// 00523db0
void	SetMouseInputActive(BOOL active);			// 005234d0
void	RenderMouseCursor();					// 00523a70 (CFrontEnd::RenderEnd 00468700)
bool	MouseDispatchClickInRect(float tlx, float tly, float brx, float bry, SINT button);	// 00523bc0
bool	MouseInRect(float tlx, float tly, float brx, float bry);			// 00523b50
bool	MouseClickedInRect(float tlx, float tly, float brx, float bry);		// 00523cc0 (FEMessBox::Render)
bool	MouseOverRect(float tlx, float tly, float brx, float bry);			// 00523d40: consumes the hover (name open)

#endif
