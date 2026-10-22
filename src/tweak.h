// tweak.h: console-tweakable values (only what matched files need). A CTweak (vtable 005e4a94, one pure slot) links
// itself into a global list at 0089c018 and keeps its name (constructor 00528690, destructor 005286b0).
// CTweakSLONG (005e4a9c) holds an integer, set from a float with rounding (00528ad0); its constructor
// (00528aa0) takes the name and the initial value. CRenderMethod (005e4a80) is another kind, built with a count.

#ifndef TWEAK_H
#define TWEAK_H

class CTweak
{
public:
					CTweak(char *name);						// 00528690
					~CTweak();								// 005286b0
	virtual void	Set(float value) = 0;

	static CTweak	*mFirst;					// 0089c018: every tweak, newest first

	CTweak			*mNext;						// +0x04
	char			*mName;						// +0x08
};

class CTweakSLONG : public CTweak
{
public:
					CTweakSLONG(char *name, int value);		// 00528aa0 (called out of line by every initializer)
	virtual void	Set(float value);				// 00528ad0
	void			SetDefault(int value);			// 00528b00: sets the value only while it is unset (-1)
	bool			IsSet();						// 00528af0: the value is not -1

	int				mValue;						// +0x0c
};

// A float tweak (vtable 005e4aa4)
class CTweakFLOAT : public CTweak
{
public:
					CTweakFLOAT(char *name, float value);	// 00528b20
	virtual void	Set(float value);				// 00528b50 (folded with CEngine::SetNumViewpoints)

	float			mValue;						// +0x0c
	float			Get() { return mValue; }	// an inline read puts the scale first (CRenderInfo::SetDepthBias 005514bc)
};

// A choice between a renderer's methods (vtable 005e4a80): it starts at the last (count - 1), each pass asks Try(n)
// for its own, and a pass the device rejects steps down to the next (Validate); Set picks one from the console.
struct CRenderMethod : public CTweak
{
public:
					CRenderMethod(char *name, int count);	// 00527c90
	virtual void	Set(float value);				// 00527d00
	bool			Try(int method);				// 00527cc0: the current method is this one (logs it the first time)
	bool			Validate();						// 00527d20: the device takes the current method, or it steps down
	void			Accept();						// 00527da0: the current method works (logs it once)
	int				GetMethod();					// 00527dd0
	void			BeginVBufCheck();				// 00527de0: arms CVBufTexture's validation while unaccepted
	bool			CheckVBufValid();				// 00527e00: a failed buffer check steps down (method 0 accepts)
	// 00528b50: stores mMethod, folded with CTweakFLOAT::Set (the water calls it on SURF_METHOD, 0055bacc;
	// CDXLandscape::RenderTerrain on LANDSCAPE_METHOD, 0054565c). Leaves mAccepted unchanged; name ours.
	void			SetMethod(int method);

	int				mMethod;					// +0x0c
	BOOL			mAccepted;					// +0x10
};

#endif
