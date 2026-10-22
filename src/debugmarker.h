// debugmarker.h: the debug markers (only what matched files need so far).

#ifndef DEBUGMARKER_H
#define DEBUGMARKER_H

#ifdef DEBUGMARKER_CPP
class CDebugMarker;

// the markers (0066ffb0): the list's head
class CDebugMarkers
{
public:
	void	Initialise();						// 00441e40
	void	Shutdown();							// 00441e50
	void	Render();							// 00441ea0 (CDXEngine::Render 0053e826)

	CDebugMarker	*mFirst;					// +0x00
};

extern CDebugMarkers DEBUGMARKERS;

// A marker drawn in the world (0x198 bytes): a cuboid at mPos/mOrientation within the frame mFramePos/mFrameOri,
// with its text on the screen over it.
class CDebugMarker
{
public:
			CDebugMarker();						// 004422d0
			~CDebugMarker();					// 00442380: unlinks it
	void	SetColour(DWORD colour) { mColour = colour; }
	void	SetSize(FVector size) { mSize = size; }
	void	SetPos(FVector pos) { mPos = pos; }
	void	SetText(char *text) { strncpy(mText, text, sizeof(mText) - 1); mText[sizeof(mText) - 1] = 0; }
	FVector	GetPos() { return mPos; }
	FMatrix	GetOrientation() { return mOrientation; }
	FVector	GetFramePos() { return mFramePos; }
	FMatrix	GetFrameOri() { return mFrameOri; }
	FVector	GetSize() { return mSize; }

	CDebugMarker	*mNext;						// +0x00
	FVector	mPos;								// +0x04 (004e1d6e)
	FMatrix	mOrientation;						// +0x14
	FVector	mFramePos;							// +0x44 (Render's SetWorld, 00441fca)
	FMatrix	mFrameOri;							// +0x54
	FVector	mSize;								// +0x84 (004e1cf9)
	DWORD	mColour;							// +0x94 (004e1cb9)
	char	mText[256];							// +0x98 (004e1d91)
};
#else
class CDebugMarkers
{
public:
	void	Initialise();						// 00441e40
	void	Shutdown();							// 00441e50
	void	Render();							// 00441ea0 (CDXEngine::Render 0053e826)
};

extern CDebugMarkers DEBUGMARKERS;

// A marker drawn in the world (0x198 bytes; CSoundManager::UpdateStatus 004e1c82 allocates one and fills it inline).
class CDebugMarker
{
public:
			CDebugMarker();						// 004422d0
			~CDebugMarker();					// 00442380: unlinks it
	void	SetColour(DWORD colour) { mColour = colour; }
	void	SetSize(FVector size) { mSize = size; }
	void	SetPos(FVector pos) { mPos = pos; }
	void	SetText(char *text) { strncpy(mText, text, sizeof(mText) - 1); mText[sizeof(mText) - 1] = 0; }

	UBYTE	mUnknown00[4];
	FVector	mPos;								// +0x04 (004e1d6e)
	UBYTE	mUnknown14[0x84 - 0x14];
	FVector	mSize;								// +0x84 (004e1cf9)
	DWORD	mColour;							// +0x94 (004e1cb9)
	char	mText[256];							// +0x98 (004e1d91)
};				// 0066ffb0

#endif

#endif
