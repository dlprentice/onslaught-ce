// Cutscene.h: in-game cutscenes (Cutscene.obj, 0043e7f0..0043fcef; Stuart's release has no copy).
//
// A CCutscene is a complex thing that plays a .cut file (data\cutscenes\NAME.cut, chunks CUTE, CPOS, TRKS, TDAT,
// SDAT, EFAD): a list of animation nodes per track, played against its CRTCutscene render thing while a movie
// camera looks through it. Layout from the constructor inlined in SpawnThing (004bf561: 0xd74 bytes, the reader
// +0x854 and the render-thing initializer +0x958) and the functions of the object.

#ifndef CUTSCENE_H
#define CUTSCENE_H

#include "thing.h"
#include "activereader.h"

class CSample;
class CCamera;

// The event a playing cutscene schedules for its next frame (HandleEvent 0043fcb4).
enum ECutsceneEvent
{
	CUTSCENE_UPDATE = 3000,
};

#define MAX_CUTSCENE_TRACKS		30		// Init's loop (0043eae2)

// The track kinds (Init stores 1 for "Video" and 2 for the audio tracks; PrepareAnimations loads samples for 2).
enum ECutsceneTrackType
{
	CTT_NONE = 0,
	CTT_VIDEO = 1,
	CTT_AUDIO = 2,
};

// One animation or sample on a track (0x24c bytes, MEMTYPE_CUTSCENE; AddAnimation 0043f210).
class CCutsceneAnimNode
{
public:
	~CCutsceneAnimNode() { delete mSample; mSample = NULL; delete mNext; mNext = NULL; }	// the ??_G 0043ed20 (Shutdown inlines it)

	SINT				mFlags;				// +0x000: bit 0 disables landscape rendering (Update 0043f88a..0043f894)
	SINT				mTrack;				// +0x004
	char				mName[256];			// +0x008: the animation's (or sample's) name
	char				mAnimName[32];		// +0x108: the mesh animation it plays
	SINT				mIndex;				// +0x128: its index among the distinct names (InitRenderThing)
	bool				mShared;			// +0x12c: an earlier node has the same name
	char				mFade[256];			// +0x12d: a fade to start with it ("Fade" is the engine's blur)
	SINT				mUnknown230;		// +0x230 (read from SDAT)
	SINT				mLength;			// +0x234: frames on the timeline; -1 takes the animation's
	SINT				mAnimLength;		// +0x238: the animation's (or sample's) own length in frames
	SINT				mStart;				// +0x23c: first frame on the timeline; -1 follows the previous node
	SINT				mOffset;			// +0x240: frames into the animation at mStart
	CSample				*mSample;			// +0x244
	CCutsceneAnimNode	*mNext;				// +0x248
};

// The cutscene's render thing (vtable 005dea38, 28 slots; RTCutscene.cpp). Init's stores establish the 0x28-byte
// layout. Slots 12 and 13 activate/reset the mesh array; retain the base class's provisional slot names.
class CRTCutscene : public CRenderThing
{
public:
	CRTCutscene();											// 004dbb60
	virtual			~CRTCutscene();							// 004dbc50
	virtual void	Init(CInitRenderThing *init);			// 004dbd80
	virtual void	Render(DWORD flags);					// 004dbec0
	virtual float	GetRTRadius();							// 004dbfb0
	virtual CMESH	*GetRTMesh();							// 004dbf80
	virtual void	Unknown12();							// 004dbe50: activate
	virtual void	Unknown13();							// 004dbe90: reset
	virtual float	GetFrameIncrement(EAnimMode am, int *realindex);	// 004dbfc0
	virtual void	GetMovieCameraPosition(FMatrix *ori, FVector *pos, float *fov, IRenderableThing *thing);	// 004dbff0
	virtual SINT	Unknown27();							// 004dbc20: render-thing class id (out of line: retail folds it with CInfantryUnit::GetImposterFrames, a non-inline function)

	void	SetCurrentIndex(SINT index);		// 004dbf70

	CMESH	**mMeshes;				// +0x14
	SINT	mNumMeshes;				// +0x18
	char	**mNames;				// +0x1c: copies of CInitRenderThing::mExtra
	bool	mActive;				// +0x20
	SINT	mCurrentIndex;			// +0x24
};

DECLARE_THING_CLASS(CCutscene, CComplexThing)
public:
	CCutscene() {}
	~CCutscene();

	virtual void		Init(CInitThing *init);
	virtual void		Shutdown();
	virtual void		HandleEvent(CEvent *event);
	virtual	void		SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_CUTSCENE); }
	virtual void		InitRenderThing(CInitThing *init);
	virtual void		Stop();

	virtual float		GetRenderFrame();
	virtual EAnimMode	GetRenderAnimation() { return mAnimMode; }
	virtual SINT		GetRealAnimIndex() { return mRealIndex; }

	BOOL				Load();
	BOOL				Save();	// RECONSTRUCTED (dropped by /OPT:REF), see Cutscene.cpp
	CCutsceneAnimNode	*AddAnimation(SINT track, char *name, char *anim, SINT start, SINT length);
	void				Start();
	void				Update();
	void				PrepareAnimations();

	static void			SkipCutscene();					// 0043fcd0 (CGame::ReceiveButtonAction 0046f8ce)

	static CCutscene	*sCurrent;						// 0066ea20: the one playing

	CCutsceneAnimNode	*mTracks[MAX_CUTSCENE_TRACKS];		// +0x07c
	SINT				mNumTracks;							// +0x0f4
	SINT				mTrackType[MAX_CUTSCENE_TRACKS];	// +0x0f8
	char				mTrackName[MAX_CUTSCENE_TRACKS][32];	// +0x170
	SINT				mTrackLanguage[MAX_CUTSCENE_TRACKS];	// +0x530: -1 for every language
	char				mFileName[256];						// +0x5a8
	SINT				mRealIndex;							// +0x6a8
	EAnimMode			mAnimMode;							// +0x6ac
	SINT				mUnknown6B0;						// +0x6b0
	float				mFrame;								// +0x6b4
	bool				mPlaying;							// +0x6b8
	SINT				mNumAnims[MAX_CUTSCENE_TRACKS];		// +0x6bc
	CCamera				*mMovieCamera;						// +0x734
	CCamera				*mOldCamera;						// +0x738
	char				mEndFade[256];						// +0x73c
	SINT				mEndFadeUnknown;					// +0x83c
	bool				mCameraSet;							// +0x840
	bool				mChanged;							// +0x841: the nodes need preparing
	SINT				mTime;								// +0x844: the current frame
	SINT				mLength;							// +0x848
	SINT				mLanguage;							// +0x84c
	SINT				mLastTime;							// +0x850
	CActiveReader<CThing>	mLinkTo;						// +0x854
	char				mLinkToName[256];					// +0x858
	CInitRenderThing	mInitRenderThing;					// +0x958
};

#endif
