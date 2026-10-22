// BattleLine.h: the HUD's battle line, the bar that shows which side holds the front (reconstructed; the PC's
// CDXBattleLine is 0x80 bytes, CHud::Init 004814be, and DXBattleLine.obj holds its code, 00539f00..).

#ifndef BATTLELINE_H
#define BATTLELINE_H

#include "Texture.h"

class CTriangulate;
class CVBuffer;
class CIBuffer;
struct FVector2;

// The texture the battle line's influence colours are drawn into (RTTI CBLTexture, vtable 005e4f64): it redraws
// them whenever the device makes it again (0053a010).
class CBLTexture : public CDXTexture
{
public:
	// its implicit destructor: 0053a140 (a lone jmp to ~CDXTexture)
	virtual HRESULT	InitDeviceObjects();				// 0053a010
	virtual HRESULT	RestoreDeviceObjects();				// 0053a040
};

// a vertex of the battle line mesh (FVF 0x204: pre-transformed, two texture coordinates; 0x20 bytes)
struct SBattleLineVertex
{
	float	X, Y, Z, RHW;
	float	U0, V0, U1, V1;
};

// an overlay point (FVF 0x44: pre-transformed, diffuse; 0x14 bytes)
struct SBattleLinePoint
{
	float	X, Y, Z, RHW;
	DWORD	Colour;
};

class CDXBattleLine
{
public:
	CDXBattleLine();							// 00539f00
	~CDXBattleLine();							// 00539f30
	void	ShutDown();							// 00539f40 (CHud::ShutDown 00481b0e)
	void	Init(int a, int b);					// 0053a050 (CHud::Init passes -7, 14)
	void	InitResources();					// 0053a150 (CHud::InitResources 00481ae2)
	BOOL	PostLoadProcess();					// 0053a280 (CHud::PostLoadProcess 00481af3)
	void	Update();							// 00414cb0 (BattleLine.obj; CHud::RenderBattleline 00488071)
	void	Render();							// 0053abe0
	void	UpdateHeightmap();					// 0053a390: the ground inside the radar circle (name open)
	void	BuildMesh();						// 0053a5e0: triangulates the playing area round the influence points
	void	InitMipLevels();					// 0053a930: the influence colour ramp in every level
	void	UpdateVertexBuffer(float y, bool colours);	// 0053aa40
	void	RenderOverlay();					// 0053b470: the overlay points (name open)
	void	AddOverlayPoint(float x, float y, DWORD colour);	// 0053b5f0 (BattleLine.obj's Update 00414ce2)

	CTriangulate	*mTriangulate;				// +0x00
	CBLTexture		*mInfluenceTexture;			// +0x04
	CDXTexture		*mHeightTexture;			// +0x08
	CDXTexture		*mMarkerTexture;			// +0x0c: hud\marker.tga
	CDXTexture		*mUnknown10;				// +0x10
	CDXTexture		*mEngineMarkerTexture;		// +0x14: hud\V2\BattleEngineMarker.tga
	FVector2		*mPoints;					// +0x18
	int				mNumTriangles;				// +0x1c
	int				mNumPoints;					// +0x20
	UBYTE			mUnknown24;					// +0x24 (PostLoadProcess sets 2)
	UBYTE			mUnknown25[0x48 - 0x25];
	float			mScaleX;					// +0x48: the influence map to the radar
	float			mScaleY;					// +0x4c
	float			mOffsetX;					// +0x50
	float			mOffsetY;					// +0x54
	int				mPulse;						// +0x58
	BOOL			mOverlayLocked;				// +0x5c
	int				mNumOverlayPoints;			// +0x60
	int				mUnknown64;					// +0x64 (Init's first argument)
	int				mUnknown68;					// +0x68 (Init's second)
	UBYTE			mUnknown6c[0x70 - 0x6c];
	CVBuffer		*mVB;						// +0x70
	CIBuffer		*mIB;						// +0x74
	CVBuffer		*mOverlayVB;				// +0x78
	SBattleLinePoint	*mOverlayData;			// +0x7c: the locked overlay points
};

#endif
