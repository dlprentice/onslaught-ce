// imposter.h: imposters (only what matched files need so far).

#ifndef IMPOSTER_H
#define IMPOSTER_H

#ifdef IMPOSTER_FULL	// DXImposter.cpp's view; game.cpp keeps the short one (its header-composition tie)
class CChunkReader;
class CDXTexture;
class CVBufTexture;
class IRenderableThing;
class CMesh;

// One view of an imposter in the shared texture (0x18 bytes): its texture rectangle and its size.
struct SImposterFrame
{
	float	mU0, mU1, mV0, mV1;					// +0x00 (CDXImposter::Render passes them as u0, v0, u1, v1: 00543845..00543857)
	float	mWidth, mHeight;					// +0x10
};
#endif

// A mesh drawn as textured quads (0x4c bytes; imposter.cpp builds them, DXImposter.cpp draws them). Every imposter
// of a level shares one texture; Deserialize reads each record whole and fixes its pointers.
class CDXImposter
{
public:
#ifdef IMPOSTER_FULL
	CDXImposter();								// 00542a30 (DXImposter.cpp; FindOrCreate calls it, 0048899f)
	~CDXImposter();								// inline, defined in DXImposter.cpp (ShutdownAll deletes)

	void	Render(IRenderableThing *thing, SINT alpha, SINT frame);	// 00543300
	void	AddQuad(FVector *pos, FVector *right, FVector *up, DWORD col, DWORD col2, float u0, float v0, float u1, float v1, BOOL alt);	// 00542f90
	void	Link();								// 00488a70 (imposter.cpp)
	static CDXImposter	*Create(CChunkReader *c);	// 00543f50
	// 004888f0 (imposter.cpp): the imposter of a mesh with these settings, made if there is none
	static CDXImposter	*FindOrCreate(class CInitRenderThing *init, CMesh *mesh, SINT views, SINT b, SINT frames, SINT mode, SINT alt);
	float	GetFrameWidth(class CThing *thing);	// 00488aa0 (imposter.cpp; CDXTrees::BuildTreeGeometry 0055a843)

	CDXImposter		*mNext;						// +0x00
	char			mName[0x20];				// +0x04: the render thing's mesh name
	CMesh			*mMesh;						// +0x24
	UBYTE			mUnknown28[0x30 - 0x28];
	SINT			mUnknown30;					// +0x30
	BOOL			mAlt;						// +0x34: drawn from the second buffer
	SINT			mUnknown38;					// +0x38: nothing is drawn while it is 0
	SImposterFrame	*mFrames;					// +0x3c: mViews per frame, mNumFrames frames
	SINT			mViews;						// +0x40
	SINT			mNumFrames;					// +0x44
	BOOL			mAnimated;					// +0x48

	static CDXImposter	*sFirst;				// 0067a678
	static BOOL			sLoaded;				// 0067a67c
	static float		sAngle;					// 0067a680
	static CVBufTexture	*sVB;					// 008aa8b4
	static CDXTexture		*sTexture;				// 008aa8b8
	static SINT			sCount;					// 008aa8bc
	static SINT			sUnknown8c0, sUnknown8c4;	// 008aa8c0 (read by DeserializeAll)
	static SINT			sUnknown8c8;			// 008aa8c8: cleared without vertex shaders
	static CVBufTexture	*sVB2;					// 008aa8cc
	static bool			sShowTexture;			// 008aa8d0
#endif
#if defined(TREE_CPP) && !defined(IMPOSTER_FULL)	// CTree::GetRenderOrientation reads the angle (004f658c)
	static float		sAngle;					// 0067a680
#endif
	static BOOL	InitAll();						// 005428d0
	static void	ShutdownAll();					// 00542990
	static void	DeserializeAll(class CChunkReader *c);	// 00543d90 (CResourceAccumulator::ReadResources 004d7706)
	static void	SetBillBoardAngle(float angle);	// 00542a50 (CDXEngine::Render 0053e5ae)
	static void	RenderAllImposters();			// 005438c0
};
#define CIMPOSTER CDXImposter

#endif
