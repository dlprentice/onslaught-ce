// ParticleDescriptor.h: the particle descriptors, the particle system editor's building blocks, each loaded from a
// ParticleSystemEd archive (TokenArchive.h). Reconstructed from ParticleSet.obj and ParticleDescriptor.obj; Stuart's
// release has no copy. The class names are RTTI's; the field names are the archive's tokens (each class's Write,
// 004c07f0 onwards). The virtual functions' purposes past Write are not all known (UnknownN is the slot number).

#ifndef PARTICLEDESCRIPTOR_H
#define PARTICLEDESCRIPTOR_H

#include "TokenArchive.h"
#include "ParticleManager.h"	// CFloatRef::GetValue reads the manager's time and the particle

class CParticle;
class CFoR;
class CMesh;
class CDXParticleTexture;

enum EParticleDescriptorType
{
	PD_NONE = 0,
	PD_SIMPLE_SPRITE,
	PD_EMITTER,
	PD_MODIFIER,
	PD_SELECTOR,
	PD_COLOUR_RANGE,
	PD_TIMELINE,
	PD_SHAPE,
	PD_TRAIL,
	PD_MOVER,
	PD_FUNCTION,
	PD_MESH,
	PD_FOR,
	PD_PMESH
};

// What a descriptor refers to, collected for GetReference (0x194 bytes on the stack).
class CPDReferences
{
public:
	CPDReferences() { mCount = 0; }
	void	Add(CParticleDescriptor *pd)
	{
		if (pd)
		{
			mRefs[mCount] = pd;
			mCount++;
		}
	}
	CParticleDescriptor	*Get(SINT index)
	{
		if (index >= mCount)
			return NULL;
		return mRefs[index];
	}

	SINT				mCount;
	CParticleDescriptor	*mRefs[100];
};

class CParticleDescriptor
{
public:
	CParticleDescriptor()								// 004cc850: emitted by ParticleSet.obj, first in Create's batch
		: mNextWithParticles(0), mUnknown40(0), mUnknown48(0), mNewestParticle(0), mOldestParticle(0) {}
	virtual			~CParticleDescriptor() {}
	virtual SINT	GetType() { return PD_NONE; }
	virtual char	*GetTypeName() { return "**NONE**"; }
	virtual char	*GetTag() { return "NONE"; }
	virtual void	RemoveReference(CParticleDescriptor *pd) {}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a) { return NULL; }
	virtual BOOL	Load(CTokenArchive *archive) { return FALSE; }
	virtual void	Write(CTokenArchive *archive) {}
	virtual void	Unknown8(void *a) {}
	virtual void	Unknown9(CParticle *p) = 0;
	virtual BOOL	Unknown10(CParticle *p);			// TRUE when the particle dies
	virtual void	Unknown11(CParticle *p) = 0;
	virtual void	Unknown12(CParticle *p, float a) {}
	virtual void	Unknown13(CParticle *p) {}
	virtual BOOL	Unknown14(CParticle *p) { return TRUE; }
	virtual void	Unknown15(CParticle *p) {}
	virtual CParticleDescriptor	*Unknown16() { return NULL; }	// the descriptor to use instead (a selector's choice)
	virtual SINT	Unknown17() { return 0; }
	virtual void	Unknown18(CParticle *p, SINT a, SINT b) {}
	virtual void	Unknown19(CParticle *p, const FVector *pos, float width) {}
	virtual void	Unknown20(CParticle *p) {}
	virtual void	Unknown21(CParticle *p);
	virtual void	Unknown22();
	virtual void	Unknown23(bool a);
	virtual void	Unknown24() {}

	void			TransmitFoR(CParticle *p, CFoR *f, BOOL transmit);	// 004c0150: share the frame, or move into it
	void			AddParticle(CParticle *p);			// 004c0510
	void			RemoveParticle(CParticle *p);		// 004c0560

	char					mName[0x34];	// +0x04: at most 49 characters (CParticleSet::Create 004cc7e1)
	CParticleDescriptor		*mNext;			// +0x38: the set's list, sorted by name (CParticleSet::FindByName 004cd7a0)
	CParticleDescriptor		*mNextWithParticles;	// +0x3c: the set's list of descriptors with live particles (LinkNode 004cdba0)
	CParticleDescriptor		*mUnknown40;	// +0x40
	CParticleDescriptor		*mNextToUpdate;	// +0x44: the next to update this frame (CParticleManager::Process 004cb29a)
	SINT					mUnknown48;		// +0x48
	SINT					mUnknown4C;		// +0x4c
	CParticle				*mNewestParticle;	// +0x50: the newest of its live particles (AddParticle 004c0510)
	CParticle				*mOldestParticle;	// +0x54: the oldest (Unknown23 004c04c0 walks from it)
	CParticle				*mNewestAtUpdate;	// +0x58: the newest when this frame's update began (Unknown22 004c0490)
};

// One point of a trail (0x28 bytes; CPDTrail::AllocatePoints 004c35d0).
struct STrailPoint
{
	FVector		mPos;						// +0x00
	FVector		mUnknown10;					// +0x10
	float		mWidth;						// +0x20: 0.2 to start
	SINT		mUnknown24;					// +0x24
};

// One entry of a timeline (its constructor is 004cce20).
class CPDTimelineEntry
{
public:
	CPDTimelineEntry() { mParticleDescriptor = NULL; mTime = -1; mTransmitFoR = 1; }

	CParticleDescriptor		*mParticleDescriptor;	// +0x00: Particle_Descriptor
	SINT					mTime;					// +0x04: Time
	SINT					mTransmitFoR;			// +0x08: Transmit_FoR
};

class CPDSimpleSprite : public CParticleDescriptor
{
public:
	CPDSimpleSprite()
	{
		mRadius = 0.5f;
		mFinalRadius = 0;
		mLife = 100;
		mLifePct = 1.0f;
		mGravity = 0;
		mTexture = NULL;
		mBounce = 0;
		mFadeCol = 1;
		mModifier = NULL;
		mColourRange = NULL;
		mBlendMode = 0;
		mTextureNumber = 0;
		mAxisAligned = 0;
		mVelocityDamp = 1.0f;
		mLength = 1.0f;
		m2D = 0;
		mAnimType = 0;
		mEndFrame = 0;
		mAnimSpeed = 1.0f;
		mTextureSize = 2;
		mRandomStartFrame = 0;
		mV0 = 0;
		mU0 = 0;
		mV1 = 1.0f;
		mU1 = 1.0f;
	}
	virtual SINT	GetType() { return PD_SIMPLE_SPRITE; }
	virtual char	*GetTypeName() { return "Simple Sprite"; }
	virtual char	*GetTag() { return "SPR"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mModifier == pd)
			mModifier = NULL;
		if (mColourRange == pd)
			mColourRange = NULL;
		if (mRadius.mRef == pd)
			mRadius.mRef = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mModifier);
		refs.Add(mColourRange);
		refs.Add(mRadius.mRef);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	void			SetUV(SINT number, SINT size);		// 004c0940: the texture tile's UVs
#ifdef PARTICLEDESCRIPTOR_CPP	// the frame count Unknown9, 10 and 11 use; as an accessor it spends the inline budget that
								// keeps Unknown9 calling Evaluate out of line (004c0bad)
	SINT			GetNumFrames() { return mEndFrame - mTextureNumber + 1; }
#endif
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual BOOL	Unknown10(CParticle *p);
	virtual void	Unknown11(CParticle *p);
	virtual void	Unknown15(CParticle *p);
	virtual void	Unknown22();
	virtual void	Unknown23(bool a);
#ifdef PDSIMPLESPRITE_CPP
	void			RenderSprites(bool a);				// 004c5d50: the particles' quads (name open)
	static void		InitNoiseTable();					// 004c7db0: the fractal noise table, once
#endif

	CFloatRef	mRadius;                             // +0x5c: Radius
	float	mFinalRadius;                            // +0x64: Final_Radius
	SINT	mGravity;                                 // +0x68: Gravity
	CDXParticleTexture	*mTexture;                                // +0x6c: Texture
	SINT	mLife;                                    // +0x70: Life
	float	mLifePct;                                // +0x74: Life_Pct
	SINT	mBounce;                                  // +0x78: Bounce
	SINT	mFadeCol;                                 // +0x7c: Fade_Col
	CParticleDescriptor	*mModifier;                // +0x80: Modifier
	CParticleDescriptor	*mColourRange;             // +0x84: Colour_Range
	SINT	mBlendMode;                               // +0x88: Blend_Mode
	SINT	mTextureNumber;                           // +0x8c: Texture_Number
	SINT	mAxisAligned;                             // +0x90: Axis_Aligned
	float	mVelocityDamp;                           // +0x94: Velocity_Damp
	CFloatRef	mLength;                             // +0x98: Length
	SINT	m2D;                                      // +0xa0: 2D
	SINT	mAnimType;                                // +0xa4: Anim_Type
	SINT	mEndFrame;                                // +0xa8: End_Frame
	SINT	mTextureSize;                             // +0xac: Texture_Size
	float	mAnimSpeed;                              // +0xb0: Anim_Speed
	SINT	mRandomStartFrame;                        // +0xb4: Random_Start_Frame
	float	mU0;                              // +0xb8
	float	mV0;                              // +0xbc
	float	mU1;                              // +0xc0
	float	mV1;                              // +0xc4
	float	mUnknownC8;                              // +0xc8
	float	mUnknownCC;                              // +0xcc
};

class CPDEmitter : public CParticleDescriptor
{
public:
	CPDEmitter()
	{
		mEmitPerTurn = 10.0f;
		mLife = 10;
		mParticleDescriptor = NULL;
		mInitialVelocityX = 0;
		mInitialVelocityY = 0;
		mInitialVelocityZ = -0.1f;
		mVelocityRandomness = 0.1f;
		mTransmitLife = 0;
		mTransmitFoR = 0;
		mTransmitVelocity = 0;
		mInterpolatedEmission = 0;
		mShape = NULL;
		mPassNumParticles = 0;
		mOutwardVelocity = 0;
		mMover = NULL;
	}
	virtual SINT	GetType() { return PD_EMITTER; }
	virtual char	*GetTypeName() { return "Emitter"; }
	virtual char	*GetTag() { return "EMIT"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mParticleDescriptor == pd)
			mParticleDescriptor = NULL;
		if (mShape == pd)
			mShape = NULL;
		if (mMover == pd)
			mMover = NULL;
		if (mEmitPerTurn.mRef == pd)
			mEmitPerTurn.mRef = NULL;
		if (mInitialVelocityX.mRef == pd)
			mInitialVelocityX.mRef = NULL;
		if (mInitialVelocityY.mRef == pd)
			mInitialVelocityY.mRef = NULL;
		if (mInitialVelocityZ.mRef == pd)
			mInitialVelocityZ.mRef = NULL;
		if (mVelocityRandomness.mRef == pd)
			mVelocityRandomness.mRef = NULL;
		if (mTransmitVelocity.mRef == pd)
			mTransmitVelocity.mRef = NULL;
		if (mOutwardVelocity.mRef == pd)
			mOutwardVelocity.mRef = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mParticleDescriptor);
		refs.Add(mShape);
		refs.Add(mMover);
		refs.Add(mEmitPerTurn.mRef);
		refs.Add(mInitialVelocityX.mRef);
		refs.Add(mInitialVelocityY.mRef);
		refs.Add(mInitialVelocityZ.mRef);
		refs.Add(mVelocityRandomness.mRef);
		refs.Add(mTransmitVelocity.mRef);
		refs.Add(mOutwardVelocity.mRef);
		return refs.Get(index);
	}
	virtual void	Unknown8(void *a)
	{
		if (mParticleDescriptor)
			mParticleDescriptor->Unknown8(a);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual BOOL	Unknown10(CParticle *p);
	virtual void	Unknown11(CParticle *p);

	CFloatRef	mEmitPerTurn;                        // +0x5c: Emit_Per_Turn
	SINT	mLife;                                    // +0x64: Life
	CParticleDescriptor	*mParticleDescriptor;      // +0x68: Particle_Descriptor
	CFloatRef	mInitialVelocityX;                   // +0x6c: Initial_Velocity_X
	CFloatRef	mInitialVelocityY;                   // +0x74: Initial_Velocity_Y
	CFloatRef	mInitialVelocityZ;                   // +0x7c: Initial_Velocity_Z
	CFloatRef	mVelocityRandomness;                 // +0x84: Velocity_Randomness
	SINT	mTransmitLife;                            // +0x8c: Transmit_Life
	SINT	mTransmitFoR;                             // +0x90: Transmit_FoR
	CFloatRef	mTransmitVelocity;                   // +0x94: Transmit_Velocity
	SINT	mInterpolatedEmission;                    // +0x9c: Interpolated_Emission
	CParticleDescriptor	*mShape;                   // +0xa0: Shape
	SINT	mPassNumParticles;                        // +0xa4: Pass_Num_Particles
	CFloatRef	mOutwardVelocity;                    // +0xa8: Outward_Velocity
	CParticleDescriptor	*mMover;                   // +0xb0: Mover
};

class CPDModifier : public CParticleDescriptor
{
public:
	CPDModifier() {}
	virtual SINT	GetType() { return PD_MODIFIER; }
	virtual char	*GetTypeName() { return "Modifier"; }
	virtual char	*GetTag() { return "MOD"; }
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive) {}
	virtual void	Unknown9(CParticle *p);
	virtual void	Unknown11(CParticle *p);
	virtual void	Unknown13(CParticle *p);

};

class CPDSelector : public CParticleDescriptor
{
public:
	CPDSelector()
	{
		for (SINT i = 0; i < 4; i++)
		{
			mParticleDescriptor[i] = NULL;
			mProbability[i] = 0;
		}
	}
	virtual SINT	GetType() { return PD_SELECTOR; }
	virtual char	*GetTypeName() { return "Selector"; }
	virtual char	*GetTag() { return "SEL"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		for (SINT i = 0; i < 4; i++)
			if (mParticleDescriptor[i] == pd)
			{
				mParticleDescriptor[i] = NULL;
				mProbability[i] = 0;
			}
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mParticleDescriptor[0]);
		refs.Add(mParticleDescriptor[1]);
		refs.Add(mParticleDescriptor[2]);
		refs.Add(mParticleDescriptor[3]);
		return refs.Get(index);
	}
	virtual void	Unknown8(void *a)
	{
		for (SINT i = 0; i < 4; i++)
			if (mParticleDescriptor[i])
				mParticleDescriptor[i]->Unknown8(a);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual void	Unknown11(CParticle *p);
	virtual CParticleDescriptor	*Unknown16();

	CParticleDescriptor	*mParticleDescriptor[4];   // +0x5c: Particle_Descriptor_0..3
	SINT				mProbability[4];                       // +0x6c: Probability_0..3
};

class CPDColourRange : public CParticleDescriptor
{
public:
	CPDColourRange()
	{
		mStartRed = 1.0f;
		mStartGreen = 1.0f;
		mStartBlue = 1.0f;
		mEndRed = 0;
		mEndGreen = 0;
		mEndBlue = 0;
		mTransitionRed = 1.0f;
		mTransitionGreen = 0;
		mTransitionBlue = 0;
		mUseEnd = 1;
		mUseTransition = 0;
		mTransitionPoint = 0.5f;
	}
	virtual SINT	GetType();						// ParticleDescriptor.cpp (the shared 004dbc20)
	virtual char	*GetTypeName() { return "ColourRange"; }
	virtual char	*GetTag() { return "COL"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mStartRed.mRef == pd)
			mStartRed.mRef = NULL;
		if (mStartGreen.mRef == pd)
			mStartGreen.mRef = NULL;
		if (mStartBlue.mRef == pd)
			mStartBlue.mRef = NULL;
		if (mEndRed.mRef == pd)
			mEndRed.mRef = NULL;
		if (mEndGreen.mRef == pd)
			mEndGreen.mRef = NULL;
		if (mEndBlue.mRef == pd)
			mEndBlue.mRef = NULL;
		if (mTransitionRed.mRef == pd)
			mTransitionRed.mRef = NULL;
		if (mTransitionGreen.mRef == pd)
			mTransitionGreen.mRef = NULL;
		if (mTransitionBlue.mRef == pd)
			mTransitionBlue.mRef = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mStartRed.mRef);
		refs.Add(mStartGreen.mRef);
		refs.Add(mStartBlue.mRef);
		refs.Add(mEndRed.mRef);
		refs.Add(mEndGreen.mRef);
		refs.Add(mEndBlue.mRef);
		refs.Add(mTransitionRed.mRef);
		refs.Add(mTransitionGreen.mRef);
		refs.Add(mTransitionBlue.mRef);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual void	Unknown11(CParticle *p);

	CFloatRef	mStartRed;                           // +0x5c: Start_Red
	CFloatRef	mStartGreen;                         // +0x64: Start_Green
	CFloatRef	mStartBlue;                          // +0x6c: Start_Blue
	CFloatRef	mEndRed;                             // +0x74: End_Red
	CFloatRef	mEndGreen;                           // +0x7c: End_Green
	CFloatRef	mEndBlue;                            // +0x84: End_Blue
	CFloatRef	mTransitionRed;                      // +0x8c: Transition_Red
	CFloatRef	mTransitionGreen;                    // +0x94: Transition_Green
	CFloatRef	mTransitionBlue;                     // +0x9c: Transition_Blue
	SINT	mUseEnd;                                  // +0xa4: Use_End
	SINT	mUseTransition;                           // +0xa8: Use_Transition
	float	mTransitionPoint;                        // +0xac: Transition_Point
};

class CPDTimeline : public CParticleDescriptor
{
public:
	CPDTimeline()
	{
		mNumEntries = 0;
	}
	virtual SINT	GetType() { return PD_TIMELINE; }
	virtual char	*GetTypeName() { return "Timeline"; }
	virtual char	*GetTag() { return "TIME"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		for (SINT i = 0; i < mNumEntries; i++)
			if (mEntries[i].mParticleDescriptor == pd)
				mEntries[i].mParticleDescriptor = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		for (SINT i = 0; i < mNumEntries; i++)
			refs.Add(mEntries[i].mParticleDescriptor);
		return refs.Get(index);
	}
	virtual void	Unknown8(void *a)
	{
		for (SINT i = 0; i < mNumEntries; i++)
			if (mEntries[i].mParticleDescriptor)
				mEntries[i].mParticleDescriptor->Unknown8(a);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual BOOL	Unknown10(CParticle *p);
	virtual void	Unknown11(CParticle *p);

	SINT	mNumEntries;                              // +0x5c: Num_Entries
	CPDTimelineEntry	mEntries[10];                 // +0x60: Particle_Descriptor, Time, Transmit_FoR
};

class CPDShape : public CParticleDescriptor
{
public:
	CPDShape()
	{
		mType = 0;
		mRingAxis = 2;
		mHemisphere = 0;
		mNumParticles = 10;
		mRadius = 1.0f;
		mHollow = 1;
		mRandomSX = 1.0f;
		mRandomSY = 1.0f;
		mRandomSZ = 1.0f;
	}
	virtual SINT	GetType() { return PD_SHAPE; }
	virtual char	*GetTypeName() { return "Shape"; }
	virtual char	*GetTag() { return "SHP"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mRadius.mRef == pd)
			mRadius.mRef = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mRadius.mRef);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual void	Unknown11(CParticle *p);
	virtual SINT	Unknown17();
	virtual void	Unknown18(CParticle *p, SINT i, SINT n);

	SINT	mType;                                    // +0x5c: Type
	SINT	mRingAxis;                                // +0x60: Ring_Axis
	SINT	mHemisphere;                              // +0x64: Hemisphere
	SINT	mNumParticles;                            // +0x68: Num_Particles
	CFloatRef	mRadius;                             // +0x6c: Radius
	SINT	mHollow;                                  // +0x74: Hollow
	float	mRandomSX;                               // +0x78: RandomSX
	float	mRandomSY;                               // +0x7c: RandomSY
	float	mRandomSZ;                               // +0x80: RandomSZ
};

class CPDTrail : public CParticleDescriptor
{
public:
	CPDTrail()
	{
		mTexture = NULL;
		mTextureNumber = 0;
		mBlendMode = 0;
		mNumPoints = 10;
		mWidth = 1.0f;
		mStartWidth = 0;
		mColourRange = NULL;
		mWiggleFactor = 0;
		mDisperseRate = 0;
		mGravity = 0;
		mLife = -2;
		mTaperStart = 0;
		mWidthWithLife = 0;
		mType = 0;
		mImpactSpawnee = NULL;
		mTransmitFoR = 1;
		mFadePoint = 0;
		mUseSegmentLength = 0;
		mSegmentLength = 10.0f;
		mV0 = 0;
		mU0 = 0;
		mV1 = 1.0f;
		mU1 = 1.0f;
		mManualWiggleEnabled = 0;
		mWiggleLength = 0;
		mYawLength = 0;
		mFlat = 0;
	}
	virtual SINT	GetType() { return PD_TRAIL; }
	virtual char	*GetTypeName() { return "Trail"; }
	virtual char	*GetTag() { return "TRL"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mColourRange == pd)
			mColourRange = NULL;
		if (mImpactSpawnee == pd)
			mImpactSpawnee = NULL;
		if (mWiggleLength.mRef == pd)
			mWiggleLength.mRef = NULL;
		if (mYawLength.mRef == pd)
			mYawLength.mRef = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mColourRange);
		refs.Add(mImpactSpawnee);
		refs.Add(mWiggleLength.mRef);
		refs.Add(mYawLength.mRef);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual BOOL	Unknown10(CParticle *p);
	virtual void	Unknown11(CParticle *p);
	virtual void	Unknown19(CParticle *p, const FVector *pos, float width);
	virtual void	Unknown20(CParticle *p);
	virtual void	Unknown21(CParticle *p);
	void			AllocatePoints(CParticle *p);		// 004c35d0
#ifdef PDSIMPLESPRITE_CPP
	DWORD			GetPointColour(SINT i, float alpha, CParticle *p);	// 004c8060 (name ours)
#endif

	CDXParticleTexture	*mTexture;                                // +0x5c: Texture
	SINT	mTextureNumber;                           // +0x60: Texture_Number
	SINT	mBlendMode;                               // +0x64: Blend_Mode
	SINT	mNumPoints;                               // +0x68: Num_Points
	float	mWidth;                                  // +0x6c: Width
	float	mStartWidth;                             // +0x70: Start_Width
	CParticleDescriptor	*mColourRange;             // +0x74: Colour_Range
	float	mWiggleFactor;                           // +0x78: Wiggle_Factor
	float	mDisperseRate;                           // +0x7c: Disperse_Rate
	SINT	mGravity;                                 // +0x80: Gravity
	SINT	mLife;                                    // +0x84: Life
	SINT	mTaperStart;                              // +0x88: Taper_Start
	SINT	mWidthWithLife;                           // +0x8c: Width_With_Life
	SINT	mType;                                    // +0x90: Type
	CParticleDescriptor	*mImpactSpawnee;           // +0x94: Impact_Spawnee
	SINT	mTransmitFoR;                             // +0x98: Transmit_FoR
	SINT	mFadePoint;                               // +0x9c: Fade_Point
	SINT	mUseSegmentLength;                        // +0xa0: Use_Segment_Length
	float	mSegmentLength;                          // +0xa4: SegmentLength
	SINT	mManualWiggleEnabled;                     // +0xa8: Manual_Wiggle_Enabled
	CFloatRef	mWiggleLength;                       // +0xac: Wiggle_Length
	CFloatRef	mYawLength;                          // +0xb4: Yaw_Length
	SINT	mFlat;                                    // +0xbc: Flat
	float	mU0;                              // +0xc0
	float	mV0;                              // +0xc4
	float	mU1;                              // +0xc8
	float	mV1;                              // +0xcc
};

class CPDMover : public CParticleDescriptor
{
public:
	CPDMover()
	{
		mGravityPC = 0;
		mYaw = 0;
		mPitch = 0;
		mRoll = 0;
		mYawFunction = NULL;
		mPitchFunction = NULL;
		mRollFunction = NULL;
	}
	virtual SINT	GetType() { return PD_MOVER; }
	virtual char	*GetTypeName() { return "Mover"; }
	virtual char	*GetTag() { return "MOV"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mGravityPC.mRef == pd)
			mGravityPC.mRef = NULL;
		if (mYawFunction == pd)
			mYawFunction = NULL;
		if (mPitchFunction == pd)
			mPitchFunction = NULL;
		if (mRollFunction == pd)
			mRollFunction = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mGravityPC.mRef);
		refs.Add(mYawFunction);
		refs.Add(mPitchFunction);
		refs.Add(mRollFunction);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual void	Unknown11(CParticle *p);
	virtual void	Unknown12(CParticle *p, float a);

	CFloatRef	mGravityPC;                          // +0x5c: GravityPC
	float	mYaw;                                    // +0x64: Yaw
	float	mPitch;                                  // +0x68: Pitch
	float	mRoll;                                   // +0x6c: Roll
	CParticleDescriptor	*mYawFunction;             // +0x70: Yaw_Function
	CParticleDescriptor	*mPitchFunction;           // +0x74: Pitch_Function
	CParticleDescriptor	*mRollFunction;            // +0x78: Roll_Function
};

class CPDFunction : public CParticleDescriptor
{
public:
	CPDFunction()
	{
		mParamA = 1.0f;
		mParamB = 1.0f;
		mParamC = 0;
		mParamD = 0;
		mParamFunction = 1;
		mClip = 0;
		mValueType = 0;
		mGameturnScale = 20.0f;
		mOffsetGameturn = 0;
	}
	virtual SINT	GetType() { return PD_FUNCTION; }
	virtual char	*GetTypeName() { return "Function"; }
	virtual char	*GetTag() { return "FN"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mParamA.mRef == pd)
			mParamA.mRef = NULL;
		if (mParamB.mRef == pd)
			mParamB.mRef = NULL;
		if (mParamC.mRef == pd)
			mParamC.mRef = NULL;
		if (mParamD.mRef == pd)
			mParamD.mRef = NULL;
		if (mGameturnScale.mRef == pd)
			mGameturnScale.mRef = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mParamA.mRef);
		refs.Add(mParamB.mRef);
		refs.Add(mParamC.mRef);
		refs.Add(mParamD.mRef);
		refs.Add(mGameturnScale.mRef);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual void	Unknown11(CParticle *p);

	// a(x) * f(b(x) * x + c(x)) + d(x), clipped (004c0c70)
	static float	Evaluate(float x, CFloatRef *a, CFloatRef *b, CFloatRef *c, CFloatRef *d, SINT func, SINT clip, CParticle *p);
	float			GetValue(float x, CParticle *p);	// inline, below

	CFloatRef	mParamA;                             // +0x5c: Param_A
	CFloatRef	mParamB;                             // +0x64: Param_B
	CFloatRef	mParamC;                             // +0x6c: Param_C
	CFloatRef	mParamD;                             // +0x74: Param_D
	SINT	mParamFunction;                           // +0x7c: Param_Function
	SINT	mClip;                                    // +0x80: Clip
	SINT	mValueType;                               // +0x84: Value_Type
	CFloatRef	mGameturnScale;                      // +0x88: Gameturn_Scale
	SINT	mOffsetGameturn;                          // +0x90: Offset_Gameturn
};

// a(x) * f(b(x) * x + c(x)) + d(x), clipped: inline everywhere; ParticleDescriptor.obj emits the out-of-line copy.
// b and c read x itself (in keeps it for a and d), so the two copies of x live apart, and each fmod result is taken
// before the 1 is subtracted: PDSimpleSprite.obj's out-of-line GetValue (004c10c0) and GetValueByAge (004c7950) then
// store x twice and schedule the clip's return tails as retail does.
// FUNCTION: 0x004c0c70
inline float CPDFunction::Evaluate(float x, CFloatRef *a, CFloatRef *b, CFloatRef *c, CFloatRef *d, SINT func, SINT clip, CParticle *p)
{
	float	in = x;
	x = b->GetValue(x, p) * x + c->GetValue(x, p);
	switch (func)
	{
	case 1:
		x = x * x;
		break;
	case 2:
		x = (float)exp(x);
		break;
	case 3:
		x = (float)sin(x);
		break;
	case 4:
		x = (float)cos(x);
		break;
	case 5:
		if (x != 0.f)
			x = 1.f / x;
		break;
	case 6:
		x = (float)log(x);
		break;
	case 10:
		x = (float)((rand() & 0xff) - 128) * (1.f / 128.f);
		break;
	}

	float	r = a->GetValue(in, p) * x + d->GetValue(in, p);
	switch (clip)
	{
	case 0:
		if (r > 1.f)
			r = 1.f;
		else if (r < -1.f)
			r = -1.f;
		break;
	case 1:
		if (r > -1.f)
		{
			r = (float)fmod(r + 1.f, 2.0);
			r -= 1.f;
		}
		else
		{
			r = -r;
			r = (float)fmod(r + 1.f, 2.0);
			r = -(r - 1.f);
		}
		break;
	}
	return r;
}

// The function's value at x (at the game time, wrapped by its scale, for a time function): inline everywhere.
inline float CPDFunction::GetValue(float x, CParticle *p)
{
	if (!mValueType)
		return Evaluate(x, &mParamA, &mParamB, &mParamC, &mParamD, mParamFunction, mClip, p);

	float	t = PARTICLE_MANAGER.GetTime();
	float	scale = mGameturnScale.GetValue(x, p);		// x, where CFloatRef::GetValue passes the time (004c4687)
	if (mOffsetGameturn && p)
		t -= p->mBirthTime;
	return Evaluate((float)fmod(t, scale) / scale, &mParamA, &mParamB, &mParamC, &mParamD, mParamFunction, mClip, p);
}

#if 1	// was: #ifndef PDSIMPLESPRITE_CPP
// The out-of-line copy is 004c10c0, emitted after CPDFunction::Evaluate, its first caller.
// FUNCTION: 0x004c10c0
inline float CFloatRef::GetValue(float x, CParticle *p)
{
	CPDFunction	*f = (CPDFunction *)mRef;
	if (!f)
		return mValue;

	if (!f->mValueType)
		return CPDFunction::Evaluate(x, &f->mParamA, &f->mParamB, &f->mParamC, &f->mParamD, f->mParamFunction, f->mClip, p) * mValue;

	float	t = PARTICLE_MANAGER.GetTime();
	float	scale = f->mGameturnScale.GetValue(t, p);
	if (f->mOffsetGameturn && p)
		t -= p->mBirthTime;
	return CPDFunction::Evaluate((float)fmod(t, scale) / scale, &f->mParamA, &f->mParamB, &f->mParamC, &f->mParamD, f->mParamFunction, f->mClip, p) * mValue;
}
#endif

// The value at that point of a particle's life; PDSimpleSprite.obj's streaks call it out of line (004c7950).
inline float CFloatRef::GetValueByAge(float left, float life, CParticle *p)
{
	return GetValue((life - left) / life, p);
}

class CPDMesh : public CParticleDescriptor
{
public:
	CPDMesh();
	virtual			~CPDMesh();					// 004c49b0 (ParticleDescriptor.cpp)
	virtual SINT	GetType() { return PD_MESH; }
	virtual char	*GetTypeName() { return "Mesh"; }
	virtual char	*GetTag() { return "MESH"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mImpactSpawnee == pd)
			mImpactSpawnee = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mImpactSpawnee);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual BOOL	Unknown10(CParticle *p);
	virtual void	Unknown11(CParticle *p);

	CMesh	*mMesh;                                   // +0x5c: Mesh
	SINT	mGravity;                                 // +0x60: Gravity
	SINT	mLife;                                    // +0x64: Life
	SINT	mBounce;                                  // +0x68: Bounce
	SINT	mTransmitFoR;                             // +0x6c: Transmit_FoR
	float	mAngularMomentum;                        // +0x70: Angular_Momentum
	SINT	mAutoCentre;                              // +0x74: Auto_Centre
	CParticleDescriptor	*mImpactSpawnee;           // +0x78: Impact_Spawnee
};

class CPDFoR : public CParticleDescriptor
{
public:
	CPDFoR();
	virtual SINT	GetType() { return PD_FOR; }
	virtual char	*GetTypeName() { return "Frame of Reference"; }
	virtual char	*GetTag() { return "FoR"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mInitial == pd)
			mInitial = NULL;
		if (mDeath == pd)
			mDeath = NULL;
		if (mMover == pd)
			mMover = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mInitial);
		refs.Add(mDeath);
		refs.Add(mMover);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual BOOL	Unknown10(CParticle *p);
	virtual void	Unknown11(CParticle *p);
	virtual BOOL	Unknown14(CParticle *p);

	CParticleDescriptor	*mInitial;                 // +0x5c: Initial
	CParticleDescriptor	*mDeath;                   // +0x60: Death
	CParticleDescriptor	*mMover;                   // +0x64: Mover
};

class CPDPMesh : public CParticleDescriptor
{
public:
	CPDPMesh();
	virtual SINT	GetType() { return PD_PMESH; }
	virtual char	*GetTypeName() { return "Proc. Mesh"; }
	virtual char	*GetTag() { return "PMESH"; }
	virtual void	RemoveReference(CParticleDescriptor *pd)
	{
		if (mColourRange == pd)
			mColourRange = NULL;
		if (mColourRange2 == pd)
			mColourRange2 = NULL;
		if (mTileU.mRef == pd)
			mTileU.mRef = NULL;
		if (mTileV.mRef == pd)
			mTileV.mRef = NULL;
		if (mScrollU.mRef == pd)
			mScrollU.mRef = NULL;
		if (mScrollV.mRef == pd)
			mScrollV.mRef = NULL;
		if (mCylinderRadius.mRef == pd)
			mCylinderRadius.mRef = NULL;
		if (mCylinderRadius2.mRef == pd)
			mCylinderRadius2.mRef = NULL;
		if (mCylinderLength.mRef == pd)
			mCylinderLength.mRef = NULL;
		if (mSphereLatitudeStart.mRef == pd)
			mSphereLatitudeStart.mRef = NULL;
		if (mSphereLatitudeEnd.mRef == pd)
			mSphereLatitudeEnd.mRef = NULL;
		if (mSphereLongitudeStart.mRef == pd)
			mSphereLongitudeStart.mRef = NULL;
		if (mSphereLongitudeEnd.mRef == pd)
			mSphereLongitudeEnd.mRef = NULL;
		if (mSphereRadiusTime.mRef == pd)
			mSphereRadiusTime.mRef = NULL;
	}
	virtual CParticleDescriptor	*GetReference(SINT index, SINT a)
	{
		CPDReferences	refs;
		refs.Add(mColourRange);
		refs.Add(mColourRange2);
		refs.Add(mTileU.mRef);
		refs.Add(mTileV.mRef);
		refs.Add(mScrollU.mRef);
		refs.Add(mScrollV.mRef);
		refs.Add(mCylinderRadius.mRef);
		refs.Add(mCylinderRadius2.mRef);
		refs.Add(mCylinderLength.mRef);
		refs.Add(mSphereLatitudeStart.mRef);
		refs.Add(mSphereLatitudeEnd.mRef);
		refs.Add(mSphereLongitudeStart.mRef);
		refs.Add(mSphereLongitudeEnd.mRef);
		refs.Add(mSphereRadiusTime.mRef);
		return refs.Get(index);
	}
	virtual BOOL	Load(CTokenArchive *archive);
	virtual void	Write(CTokenArchive *archive);
	virtual void	Unknown9(CParticle *p);
	virtual BOOL	Unknown10(CParticle *p);
	virtual void	Unknown11(CParticle *p);

	SINT	mType;                                    // +0x5c: Type
	SINT	mLife;                                    // +0x60: Life
	CDXParticleTexture	*mTexture;                                // +0x64: Texture
	SINT	mBlendMode;                               // +0x68: Blend_Mode
	CParticleDescriptor	*mColourRange;             // +0x6c: Colour_Range
	CParticleDescriptor	*mColourRange2;            // +0x70: Colour_Range2
	CFloatRef	mTileU;                              // +0x74: Tile_U
	CFloatRef	mTileV;                              // +0x7c: Tile_V
	CFloatRef	mScrollU;                            // +0x84: Scroll_U
	CFloatRef	mScrollV;                            // +0x8c: Scroll_V
	SINT	mCylinderNumPtsAxial;                     // +0x94: Cylinder_NumPtsAxial
	SINT	mCylinderNumPtsRadial;                    // +0x98: Cylinder_NumPtsRadial
	CFloatRef	mCylinderRadius;                     // +0x9c: Cylinder_Radius
	CFloatRef	mCylinderRadius2;                    // +0xa4: Cylinder_Radius2
	CFloatRef	mCylinderLength;                     // +0xac: Cylinder_Length
	SINT	mSphereNumPtsAx;                          // +0xb4: Sphere_NumPtsAx
	SINT	mSphereNumPtsRad;                         // +0xb8: Sphere_NumPtsRad
	CFloatRef	mSphereLatitudeStart;                // +0xbc: Sphere_Latitude_Start
	CFloatRef	mSphereLatitudeEnd;                  // +0xc4: Sphere_Latitude_End
	CFloatRef	mSphereLongitudeStart;               // +0xcc: Sphere_Longitude_Start
	CFloatRef	mSphereLongitudeEnd;                 // +0xd4: Sphere_Longitude_End
	CFloatRef	mSphereRadiusTime;                   // +0xdc: Sphere_RadiusTime
};

#endif
