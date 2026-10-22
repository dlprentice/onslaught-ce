// state.h: the PC render-state defaults (State.cpp; not in Stuart's release; reconstructed from the retail executable).
// The class name is open; the instance is at 0083d248.

#ifndef STATE_H_STUB
#define STATE_H_STUB

class	CState
{
public:
	CState();									// 004eb9a0: the two default materials
	D3DMATERIAL9	*GetDefaultMaterial() { return &mDefaultMaterial; }	// Stuart's (DXEngine.cpp); the material is at +0x00 (0053e69c)
	void	UseDefault();						// 004eb1e0: every render state back to its default
	void	UpdateClipPlane(bool force);		// 004eba30: the water clip plane during a mirrored pass

	D3DMATERIAL9	mDefaultMaterial;			// +0x00: white
	D3DMATERIAL9	mLandscapeMaterial;			// +0x44 (0083d28c): black, ambient 0.8; the landscape's passes set it (CDXLandscape)
};

extern	CState	STATE;							// 0083d248

#endif
