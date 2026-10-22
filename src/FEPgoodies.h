// FEPgoodies.h: the goodies page. Stuart's header; CFEPGoodies lays out exactly as his (0x1dc bytes: Init 0045c7a0
// stores mCX +0x13c and mCY +0x140, the loader is at +0x0c, mGoodyState at +0x1d8).

#ifndef FEPGOODIES_H
#define FEPGOODIES_H

#include	"FrontEndPage.h"
#include	"Platform.h"
#include	"asynccache.h"

#include	"Texture.h"
#include	"mesh.h"
#include	"Player.h"

enum EGoodieType
{
	GT_IMAGE = 0,
	GT_MESH,
	GT_FMV,
	GT_LEVEL,
	GT_CHEAT
};

class CGoodieData
{
public:
	CGoodieData(SINT method=-1, SINT method2 = -1, SINT number = -1, SINT number2 = -1, EKilledType t1 = TK_TOTAL, EKilledType t2 = TK_TOTAL)
	{
		Method  = method ;
		Method2 = method2;
		Number  = number ;
		Number2 = number2;

		mT1 = t1;
		mT2 = t2;
	}

	void GetMethod(WCHAR *destbuf) const ;
	SINT GetNumber() const { return Number ;}
	SINT GetNumber2() const { return Number2 ;} 

private:
	SINT Method; // what you have to do to release the goodie
	SINT Method2; // some more you may have to do.
	SINT Number; // sometimes a goodie will have a number of whatevers that you need to complete it
	SINT Number2;

	EKilledType mT1;
	EKilledType mT2;
};

extern const CGoodieData goodies[] ;

// The PC's mouse scrolling at the screen edges (0045d730; CFEPLevelSelect::Process calls it, 00460693).
void	FEMouseEdgeSlide(EFEPState state, float *pos, float max);

// The cutscene file name for a number, falling back to the small video (0046d810, in game.cpp's object).
#include "fmv.h"						// MakeCutsceneName (0046d810)

class	CFEPGoodies : public CFrontEndPage
{
public:
	virtual	BOOL	Init();
	virtual	void	Shutdown();

	virtual	void	Process(EFEPState state);
	virtual	void	RenderPreCommon(float transition, EFrontEndPage dest);
	virtual	void	Render(float transition, EFrontEndPage dest);

	virtual	void	ButtonPressed(SINT button, float val);

	virtual void	TransitionNotification(EFrontEndPage from);

	// this does all the resources, one for each goodie.
	static void BuildAllResources(int iPlatform);
	static void Serialise(class CChunker *c, class CResourceAccumulator *ra);
			void	Deserialise(class CChunkReader *c);

protected:
	void	EvaluateDest();

	void	ResetGoodyState();

	void	StartLoadingGoody();
	void	LoadingGoodyPoll();
	void	FreeUpGoodyResources();

	float	mStartTime, mTimerGoody;

	CASYNCCACHE mGoodieLoader;

	float	mXPos, mXDest;
	SINT	mCX, mCY;

	// screen coords for goodies when they're in the selectable grid
	float	GoodieX(int which);
	float	GoodieY(int which);

	CTEXTURE	**mCurrentGoodyTexture;
	CMESH		*mCurrentGoodyMesh;
	int			mNumCurrentGoodieTextures;
	int			mCurrentGoodieHeight;
	EGoodieType	mCurrentGoodyType;

	BOOL		mManualControl;
	float		mCurrentYaw,mCurrentPitch,mCurrentRoll;
	float		mTargetYaw,mTargetPitch,mTargetRoll;
	float		mYawDelta,mPitchDelta,mRollDelta;
	float		mTimeLeft;
	float		mLastProcessTime;
	float		mTextScrollOffset;
	float		mTextHeight;

	float		mSelectFade;
	float		mImageZoom;
	float		mImageX, mImageY;
	FMatrix		mMeshOri;
	float		mMeshDistance;

	BOOL		mDisplayGoody;

	enum		EGoodyState
	{
		NO_GOODY,
		GOODY_LOADING,
		GOODY_LOADED
	};

	EGoodyState mGoodyState;
};

#endif
