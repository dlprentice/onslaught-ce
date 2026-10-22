// ParticleDescriptor.cpp: reconstructed from ParticleDescriptor.obj (004c0450..004c5b5f); Stuart's release has no copy.
// So far each descriptor's Load and Write; the particles' behaviours are still to come.
#include "common.h"
#define PARTICLEDESCRIPTOR_CPP
#include "ParticleDescriptor.h"
#include "ParticleManager.h"
#include "mesh.h"
#include "ParticleTexture.h"
#include "game.h"
#include "Platform.h"

// How far through its life a particle is (name ours).
static inline float LifeFraction(float left, float life)
{
	return (life - left) / life;
}

// Up to scale either way (name ours). Capture before rand, as the trail/mesh callers do in retail.
static inline float RandomScaled(const float &scale)
{
	float savedScale = scale;
	SINT	r = (rand() & 0xff) - 128;
	return (float)r * savedScale * (1.f / 128.f);
}

//******************************************************************************************
void CParticleDescriptor::Unknown21(CParticle *p)	// FUNCTION: 0x004c0140
{
	p->mLife = 0;
}

//******************************************************************************************
// A particle spawned by one in a frame either shares that frame or is moved out into world space.
// One local more than the plain statements is what retail's bytes need: the term order of the three matrix products moves
// with the function's expression nodes (an unused `int x = 0` has the same effect, two or three of them overshoot), and any
// one read through a reference, pointer or copy of a parameter, member or frame value does it.
void CParticleDescriptor::TransmitFoR(CParticle *p, CFoR *f, BOOL transmit)	// FUNCTION: 0x004c0150
{
	if (transmit)
	{
		p->mFoR = f;
		return;
	}
	if (!f)
		return;

	if (f->mHasOrientation)
	{
		p->mPos = f->mOrientation * p->mPos;
		p->Orientation() = p->Orientation() * f->mOrientation;
		p->mVelocity = f->mOrientation * p->mVelocity;
	}
	FVector &position = p->mPos;
	position += f->mPos;
}

// RECONSTRUCTED (placement; the name is ours): the link follows the particle, through its frame when it has one. A plain
// function here, after TransmitFoR: retail emits CParticleEffectLink's SetPos and SetOrientation right after TransmitFoR
// (004c0370, 004c0450), what this body pulls out of line; its caller inlines it and /OPT:REF drops it. One level down
// it gets (1000 - 167) / 6 = 138 (free sites after it count), which keeps FMatrix's array iterator, FMatrix * FVector,
// op+'s FVector constructor, FMatrix * FMatrix and the link calls out of line in CPDFoR::Unknown10 (004c5567..004c5619).
void FollowParticle(CParticleEffectLink *link, CParticle *p)
{
	CFoR	*f = p->mFoR;
	if (f)
	{
		FMatrix	ori;
		FVector	pos;
		pos = f->mOrientation * p->mPos + f->mPos;
		ori = f->mOrientation * p->Orientation();
		link->SetPos(pos);
		link->SetOrientation(ori);
	}
	else
	{
		link->SetPos(p->mPos);
		link->SetOrientation(p->Orientation());
	}
}

//******************************************************************************************
BOOL CParticleDescriptor::Unknown10(CParticle *p)	// FUNCTION: 0x004c0480
{
	return p->mDead = TRUE;
}

//******************************************************************************************
// Update this frame's particles, newest first, from the one that was newest when the frame began.
void CParticleDescriptor::Unknown22()	// FUNCTION: 0x004c0490
{
	if (mOldestParticle)
	{
		CParticle	*p = mNewestAtUpdate;
		do
		{
			p->mDead |= Unknown10(p);
			p = p->mPDPrev;
		} while (p);
	}
}

//******************************************************************************************
// Draw the particles, oldest first; a is set to skip the dying (unless they only wait, life -2).
void CParticleDescriptor::Unknown23(bool a)	// FUNCTION: 0x004c04c0
{
	for (CParticle *p = mOldestParticle; p; p = p->mPDNext)
		if (!a || !(p->mLife < 0.f) || (p->mLife == -2.f))
			Unknown11(p);
}

//******************************************************************************************
void CParticleDescriptor::AddParticle(CParticle *p)	// FUNCTION: 0x004c0510
{
	if (!mOldestParticle)
	{
		PARTICLE_SET.LinkNode((SParticleSetNode *)this);
		mNewestParticle = p;
		mOldestParticle = p;
		p->mPDPrev = NULL;
		p->mPDNext = NULL;
	}
	else
	{
		mNewestParticle->mPDNext = p;
		p->mPDPrev = mNewestParticle;
		mNewestParticle = p;
	}
}

//******************************************************************************************
void CParticleDescriptor::RemoveParticle(CParticle *p)	// FUNCTION: 0x004c0560
{
	if (mNewestAtUpdate == p)
		mNewestAtUpdate = NULL;

	if (!p->mPDPrev)
		mOldestParticle = p->mPDNext;
	else
		p->mPDPrev->mPDNext = p->mPDNext;
	if (!p->mPDNext)
		mNewestParticle = p->mPDPrev;
	else
		p->mPDNext->mPDPrev = p->mPDPrev;
	p->mPDPrev = NULL;
	p->mPDNext = NULL;

	if (!mOldestParticle)
		PARTICLE_SET.UnlinkNode((SParticleSetNode *)this);
}

//******************************************************************************************
BOOL CPDSimpleSprite::Load(CTokenArchive *archive)	// FUNCTION: 0x004c05c0
{
#line 205
	char	*sval = new(MEMTYPE_SCRATCHPAD) char[1000];
	SINT	token, ival;
	float	fval;

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_RADIUS:
			archive->SetFloatRef(fval, ival, &mRadius);
			break;
		case PST_FINAL_RADIUS:
			mFinalRadius = fval;
			break;
		case PST_LIFE:
			mLife = ival;
			break;
		case PST_LIFE_PCT:
			mLifePct = fval;
			break;
		case PST_GRAVITY:
			mGravity = ival;
			break;
		case PST_BOUNCE:
			mBounce = ival;
			break;
		case PST_FADE_COL:
			mFadeCol = ival;
			break;
		case PST_TEXTURE:
			mTexture = CPARTICLETEXTURE::Get(sval, mBlendMode + 1);
			break;
		case PST_MODIFIER:
			archive->SetFixup(ival, &mModifier);
			break;
		case PST_COLOUR_RANGE:
			archive->SetFixup(ival, &mColourRange);
			break;
		case PST_BLEND_MODE:
			mBlendMode = ival;
			break;
		case PST_TEXTURE_NUMBER:
			mTextureNumber = ival;
			break;
		case PST_TEXTURE_SIZE:
			mTextureSize = ival;
			break;
		case PST_VELOCITY_DAMP:
			mVelocityDamp = fval;
			break;
		case PST_AXIS_ALIGNED:
			mAxisAligned = ival;
			break;
		case PST_ANIM_TYPE:
			mAnimType = ival;
			break;
		case PST_END_FRAME:
			mEndFrame = ival;
			break;
		case PST_ANIM_SPEED:
			mAnimSpeed = fval;
			break;
		case PST_RANDOM_START_FRAME:
			mRandomStartFrame = ival;
			break;
		case PST_LENGTH:
			archive->SetFloatRef(fval, ival, &mLength);
			break;
		case PST_2D:
			m2D = ival;
			break;
		}
	} while (token != PST_SEPARATOR);
	SetUV(mTextureNumber, mTextureSize);
	delete [] sval;
	return TRUE;
}

//******************************************************************************************
void CPDSimpleSprite::Write(CTokenArchive *archive)	// FUNCTION: 0x004c07f0
{
	archive->WriteFloatRef(PST_RADIUS, &mRadius);
	archive->WriteFloat(PST_FINAL_RADIUS, mFinalRadius);
	archive->WriteInt(PST_LIFE, mLife);
	archive->WriteFloat(PST_LIFE_PCT, mLifePct);
	archive->WriteInt(PST_GRAVITY, mGravity);
	archive->WriteInt(PST_BLEND_MODE, mBlendMode);
	if (mTexture)
		archive->WriteString(PST_TEXTURE, mTexture->mName);
	archive->WriteInt(PST_BOUNCE, mBounce);
	archive->WriteInt(PST_FADE_COL, mFadeCol);
	archive->WriteReference(PST_MODIFIER, mModifier);
	archive->WriteReference(PST_COLOUR_RANGE, mColourRange);
	archive->WriteInt(PST_TEXTURE_NUMBER, mTextureNumber);
	archive->WriteInt(PST_TEXTURE_SIZE, mTextureSize);
	archive->WriteFloat(PST_VELOCITY_DAMP, mVelocityDamp);
	archive->WriteInt(PST_AXIS_ALIGNED, mAxisAligned);
	archive->WriteInt(PST_ANIM_TYPE, mAnimType);
	archive->WriteInt(PST_END_FRAME, mEndFrame);
	archive->WriteFloat(PST_ANIM_SPEED, mAnimSpeed);
	archive->WriteInt(PST_RANDOM_START_FRAME, mRandomStartFrame);
	archive->WriteFloatRef(PST_LENGTH, &mLength);
	archive->WriteInt(PST_2D, m2D);
}

//******************************************************************************************
// The UVs of tile number of a texture holding size (0..4: 16 down to 1) tiles a side. The tile count is a local of its own
// (retail keeps it in the home slot `size` frees), and a size above 4 leaves shift unset, where retail's code reads size.
void CPDSimpleSprite::SetUV(SINT number, SINT size)	// FUNCTION: 0x004c0940
{
	SINT	shift;
	SINT	count;
	switch (size)
	{
	case 0:	count = 16;	shift = 4;	break;
	case 1:	count = 8;	shift = 3;	break;
	case 2:	count = 4;	shift = 2;	break;
	case 3:	count = 2;	shift = 1;	break;
	case 4:	count = 1;	shift = 0;	break;
	}

	if (mTexture && mTextureNumber != -1)
	{
		float	tiles = (float)count;
		float	u, v;
		mU0 = u = (float)(number & (count - 1)) / tiles;
		mV0 = v = (float)(number >> shift) / tiles;
		float	step = 1.0f / tiles;
		mU1 = step + u;
		mV1 = step + v;
	}
	else
	{
		mV0 = 0;
		mU0 = 0;
		mV1 = 1.0f;
		mU1 = 1.0f;
	}
}

//******************************************************************************************
// Tiny sprites far from the camera die at once.
void CPDSimpleSprite::Unknown15(CParticle *p)	// FUNCTION: 0x004c0a50
{
	if (!p->mFoR)
	{
		float	d = PARTICLE_MANAGER.GetDistanceSq(p);
		if (((mRadius.mValue * mRadius.mValue / d) * 102400.f < 0.5f) && !m2D)
			Unknown21(p);
	}
}

//******************************************************************************************
static SINT	sSpriteCount = 0;							// 00829e50: staggers the sprites' animation

void CPDSimpleSprite::Unknown9(CParticle *p)	// FUNCTION: 0x004c0ab0
{
	float	spread = (1.f - mLifePct) * 0.5f;
	SINT	random = (rand() & 0xff) - 128;
	float	centre = (1.f - mLifePct) * 0.5f;
	p->mLife = (centre + (float)random * spread * (1.f / 128.f) + mLifePct) * mLife;
	p->mRadius = mRadius.GetValue(0.f, p);
	p->mFrame = 0;
	if (mRandomStartFrame)
	{
		float	frames = (float)GetNumFrames() * 0.5f;
		p->mFrame = (RandomScaled(1.f) + 1.f) * frames;
	}
	p->mU0 = mU0;
	p->mU1 = mU1;
	p->mV0 = mV0;
	p->mV1 = mV1;
	p->mUnknown7E = (sSpriteCount & 0xf) + 0xc0;
	sSpriteCount++;
}

//******************************************************************************************
// CPDFunction::Evaluate (004c0c70) is an inline in ParticleDescriptor.h; this object emits its out-of-line copy.

//******************************************************************************************
// Grow toward the final radius over the sprite's life; fall, slow down, change and animate.
BOOL CPDSimpleSprite::Unknown10(CParticle *p)	// FUNCTION: 0x004c14f0
{
	float	r = mRadius.GetValueByAge(p->mLife, (float)mLife, p);
	if (!mLife)
		p->mRadius = r;
	else
		p->mRadius = r + (r * mFinalRadius - r) * ((float)mLife - p->mLife) / (float)mLife;

	if (mGravity)
		p->mVelocity.Z += PARTICLE_MANAGER.GetFrameLength() * 0.003f;

	if ((mVelocityDamp != 1.f) && PARTICLE_MANAGER.IsFirstFrame())
		p->mVelocity *= mVelocityDamp;

	if (mModifier)
		mModifier->Unknown13(p);

	if (mAnimType)
	{
		p->mFrame += PARTICLE_MANAGER.GetFrameLength() * mAnimSpeed;
		if (!(p->mFrame < (float)GetNumFrames()))
			p->mFrame = 0;
	}
	return FALSE;
}

//******************************************************************************************
// Update this frame's sprites, newest first: radius, gravity, damping, the modifier and the animation.
void CPDSimpleSprite::Unknown22()	// FUNCTION: 0x004c15f0
{
	CParticle	*p = mNewestAtUpdate;
	if (!p)
		return;

	float	t = PARTICLE_MANAGER.GetFrameLength();
	float	gravity = mGravity ? PARTICLE_MANAGER.GetFrameLength() * 0.003f : 0.f;
	float	damp = ((mVelocityDamp != 1.f) && PARTICLE_MANAGER.IsFirstFrame()) ? mVelocityDamp : 1.f;
	float	scale = 0.f;
	if (mLife)
		scale = 1.f;
	float	inv = 1.f / (float)mLife;

	for (; p; p = p->mPDPrev)
	{
		float	r = mRadius.GetValueByAge(p->mLife, (float)mLife, p);
		p->mRadius = ((r * mFinalRadius - r) * ((float)mLife - p->mLife) * inv + r) * scale;
		p->mVelocity.Z += gravity;
		p->mVelocity *= damp;
	}

	if (mModifier)
	{
		p = mNewestAtUpdate;
		do
		{
			mModifier->Unknown13(p);
			p = p->mPDPrev;
		} while (p);
	}

	if (mAnimType)
	{
		float	step = t * mAnimSpeed;
		CParticle	*q = mNewestAtUpdate;
		float	frames = (float)GetNumFrames();
		do
		{
			q->mFrame += step;
			if (!(q->mFrame < frames))
				q->mFrame = 0;
			q = q->mPDPrev;
		} while (q);
	}
}



//******************************************************************************************
BOOL CPDEmitter::Load(CTokenArchive *archive)	// FUNCTION: 0x004c1810
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_EMIT_PER_TURN:
			archive->SetFloatRef(fval, ival, &mEmitPerTurn);
			break;
		case PST_LIFE:
			mLife = ival;
			break;
		case PST_PARTICLE_DESCRIPTOR:
			archive->SetFixup(ival, &mParticleDescriptor);
			break;
		case PST_INITIAL_VELOCITY_X:
			archive->SetFloatRef(fval, ival, &mInitialVelocityX);
			break;
		case PST_INITIAL_VELOCITY_Y:
			archive->SetFloatRef(fval, ival, &mInitialVelocityY);
			break;
		case PST_INITIAL_VELOCITY_Z:
			archive->SetFloatRef(fval, ival, &mInitialVelocityZ);
			break;
		case PST_VELOCITY_RANDOMNESS:
			archive->SetFloatRef(fval, ival, &mVelocityRandomness);
			break;
		case PST_TRANSMIT_LIFE:
			mTransmitLife = ival;
			break;
		case PST_TRANSMIT_FOR:
			mTransmitFoR = ival;
			break;
		case PST_TRANSMIT_VELOCITY:
			archive->SetFloatRef(fval, ival, &mTransmitVelocity);
			break;
		case PST_INTERPOLATED_EMISSION:
			mInterpolatedEmission = ival;
			break;
		case PST_SHAPE:
			archive->SetFixup(ival, &mShape);
			break;
		case PST_PASS_NUM_PARTICLES:
			mPassNumParticles = ival;
			break;
		case PST_OUTWARD_VELOCITY:
			archive->SetFloatRef(fval, ival, &mOutwardVelocity);
			break;
		case PST_MOVER:
			archive->SetFixup(ival, &mMover);
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDEmitter::Write(CTokenArchive *archive)	// FUNCTION: 0x004c1970
{
	archive->WriteFloatRef(PST_EMIT_PER_TURN, &mEmitPerTurn);
	archive->WriteInt(PST_LIFE, mLife);
	archive->WriteReference(PST_PARTICLE_DESCRIPTOR, mParticleDescriptor);
	archive->WriteFloatRef(PST_INITIAL_VELOCITY_X, &mInitialVelocityX);
	archive->WriteFloatRef(PST_INITIAL_VELOCITY_Y, &mInitialVelocityY);
	archive->WriteFloatRef(PST_INITIAL_VELOCITY_Z, &mInitialVelocityZ);
	archive->WriteFloatRef(PST_VELOCITY_RANDOMNESS, &mVelocityRandomness);
	archive->WriteInt(PST_TRANSMIT_LIFE, mTransmitLife);
	archive->WriteInt(PST_TRANSMIT_FOR, mTransmitFoR);
	archive->WriteFloatRef(PST_TRANSMIT_VELOCITY, &mTransmitVelocity);
	archive->WriteInt(PST_INTERPOLATED_EMISSION, mInterpolatedEmission);
	archive->WriteReference(PST_SHAPE, mShape);
	archive->WriteInt(PST_PASS_NUM_PARTICLES, mPassNumParticles);
	archive->WriteFloatRef(PST_OUTWARD_VELOCITY, &mOutwardVelocity);
	archive->WriteReference(PST_MOVER, mMover);
}

//******************************************************************************************
void CPDEmitter::Unknown9(CParticle *p)	// FUNCTION: 0x004c1a60
{
	p->mLife = (float)mLife;
	p->mFrame = 0;
	p->mRadius = 1.f;
	p->mUnknown7E = (rand() % 20) - 20;
}

//******************************************************************************************
// Emit this frame's share of particles (fewer the further the emitter is from the camera), each placed by the
// shape, moved into the emitter's orientation and given its velocities.
// Matched retail's 1,584-byte section and 35 relocations. The existing GetLife read preserves the child's
// life-product load order; RandomScaled snapshots its argument before rand; TransmitFoR borrows the position
// it updates. These are reconstructed source interfaces, not recovered original spelling or runtime acceptance.
BOOL CPDEmitter::Unknown10(CParticle *p)	// FUNCTION: 0x004c1a90
{
	if (mMover)
		mMover->Unknown12(p, LifeFraction(p->mLife, (float)mLife));

	if (mParticleDescriptor && PARTICLE_MANAGER.IsFirstFrame())
	{
		float	rate = mEmitPerTurn.GetValueByAge(p->mLife, (float)mLife, p);
		p->mUnknown7E--;
		if (p->mUnknown7E < 0)
		{
			p->mUnknown7E += 20;
			float	d = ((float)sqrt(PARTICLE_MANAGER.GetDistanceSq(p)) - 30.f) * (1.f / 120.f);
			if (d < 0.f)
				d = 0.f;
			else if (d > 1.f)
				d = 1.f;
			p->mRadius = 1.f - d * 0.8f;
		}

		p->mFrame += rate * p->mRadius;
		SINT	n = (SINT)floor(p->mFrame);
		p->mFrame -= (float)n;

		SINT	count = n;
		if (!mPassNumParticles)
			count = -1;
		if (mShape && !mPassNumParticles)
			n = mShape->Unknown17();

		for (SINT i = 0; i < n; i++)
		{
			CParticleDescriptor	*pd = mParticleDescriptor;
			while (pd && (pd->GetType() == PD_SELECTOR))
				pd = pd->Unknown16();

			CParticle	*np = PARTICLE_MANAGER.AllocateParticle(pd, p->mFoR && (p->mFoR->mFlags | 1));
			if (!np)
				continue;

			if (mShape)
			{
				mShape->Unknown18(np, i, count);
				float	out = mOutwardVelocity.GetValueByAge(p->mLife, (float)mLife, p);
				if (out != 0.f)
				{
					const FVector &position = np->mPos;
					np->mVelocity += (p->mOrientation * position) * out;
				}
			}

			np->mPos = p->mOrientation * np->mPos;
			{
				FVector &position = np->mPos;
				position += p->mPos;
			}

			FVector	velocity;
			velocity.X = mInitialVelocityX.GetValueByAge(p->mLife, (float)mLife, p);
			velocity.Y = mInitialVelocityY.GetValueByAge(p->mLife, (float)mLife, p);
			velocity.Z = mInitialVelocityZ.GetValueByAge(p->mLife, (float)mLife, p);
			float	r = mVelocityRandomness.GetValueByAge(p->mLife, (float)mLife, p);
			np->mVelocity += FVector(RandomScaled(r), RandomScaled(r), RandomScaled(r)) + velocity;

			if (mTransmitLife)
			{
				np->mLife = np->GetLife() * p->mLife / mLife;
				if (np->mLife <= 0.f)
					np->mLife = 0.01f;
			}

			float	tv = mTransmitVelocity.GetValueByAge(p->mLife, (float)mLife, p);
			if (tv != 0.f)
				np->mVelocity += p->mVelocity * tv;

			TransmitFoR(np, p->mFoR, mTransmitFoR);
			PARTICLE_MANAGER.ProcessParticle(np);
			np->mPD->Unknown15(np);
		}
	}
	return FALSE;
}

//******************************************************************************************
BOOL CPDModifier::Load(CTokenArchive *archive)	// FUNCTION: 0x004c20c0
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
		archive->ReadToken(&token, &ival, &fval, sval);
	while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDModifier::Unknown13(CParticle *p)	// FUNCTION: 0x004c2100
{
	p->mVelocity.Y += ((float)cos(p->mPos.Y * 10.f) * 0.01f + 0.003f) * PARTICLE_MANAGER.mFrameLength;
}

//******************************************************************************************
BOOL CPDSelector::Load(CTokenArchive *archive)	// FUNCTION: 0x004c2130
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_PARTICLE_DESCRIPTOR_0:
			archive->SetFixup(ival, &mParticleDescriptor[0]);
			break;
		case PST_PARTICLE_DESCRIPTOR_1:
			archive->SetFixup(ival, &mParticleDescriptor[1]);
			break;
		case PST_PARTICLE_DESCRIPTOR_2:
			archive->SetFixup(ival, &mParticleDescriptor[2]);
			break;
		case PST_PARTICLE_DESCRIPTOR_3:
			archive->SetFixup(ival, &mParticleDescriptor[3]);
			break;
		case PST_PROBABILITY_0:
			mProbability[0] = ival;
			break;
		case PST_PROBABILITY_1:
			mProbability[1] = ival;
			break;
		case PST_PROBABILITY_2:
			mProbability[2] = ival;
			break;
		case PST_PROBABILITY_3:
			mProbability[3] = ival;
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDSelector::Write(CTokenArchive *archive)	// FUNCTION: 0x004c2220
{
	archive->WriteReference(PST_PARTICLE_DESCRIPTOR_0, mParticleDescriptor[0]);
	archive->WriteReference(PST_PARTICLE_DESCRIPTOR_1, mParticleDescriptor[1]);
	archive->WriteReference(PST_PARTICLE_DESCRIPTOR_2, mParticleDescriptor[2]);
	archive->WriteReference(PST_PARTICLE_DESCRIPTOR_3, mParticleDescriptor[3]);
	archive->WriteInt(PST_PROBABILITY_0, mProbability[0]);
	archive->WriteInt(PST_PROBABILITY_1, mProbability[1]);
	archive->WriteInt(PST_PROBABILITY_2, mProbability[2]);
	archive->WriteInt(PST_PROBABILITY_3, mProbability[3]);
}

//******************************************************************************************
// One of the four, at random by their probabilities.
CParticleDescriptor *CPDSelector::Unknown16()	// FUNCTION: 0x004c22a0
{
	SINT	i, total = 0;
	for (i = 0; i < 4; i++)
		if (mProbability[i] > 0)
			total += mProbability[i];

	SINT	r = rand() % total;
	SINT	sum = 0;
	for (i = 0; i < 4; i++)
		if (mProbability[i] > 0)
		{
			sum += mProbability[i];
			if (r <= sum)
				return mParticleDescriptor[i];
		}
	return NULL;
}

//******************************************************************************************
BOOL CPDColourRange::Load(CTokenArchive *archive)	// FUNCTION: 0x004c2300
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_START_RED:
			archive->SetFloatRef(fval, ival, &mStartRed);
			break;
		case PST_START_GREEN:
			archive->SetFloatRef(fval, ival, &mStartGreen);
			break;
		case PST_START_BLUE:
			archive->SetFloatRef(fval, ival, &mStartBlue);
			break;
		case PST_END_RED:
			archive->SetFloatRef(fval, ival, &mEndRed);
			break;
		case PST_END_GREEN:
			archive->SetFloatRef(fval, ival, &mEndGreen);
			break;
		case PST_END_BLUE:
			archive->SetFloatRef(fval, ival, &mEndBlue);
			break;
		case PST_TRANSITION_RED:
			archive->SetFloatRef(fval, ival, &mTransitionRed);
			break;
		case PST_TRANSITION_GREEN:
			archive->SetFloatRef(fval, ival, &mTransitionGreen);
			break;
		case PST_TRANSITION_BLUE:
			archive->SetFloatRef(fval, ival, &mTransitionBlue);
			break;
		case PST_USE_END:
			mUseEnd = ival;
			break;
		case PST_USE_TRANSITION:
			mUseTransition = ival;
			break;
		case PST_TRANSITION_POINT:
			mTransitionPoint = fval;
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDColourRange::Write(CTokenArchive *archive)	// FUNCTION: 0x004c2400
{
	archive->WriteFloatRef(PST_START_RED, &mStartRed);
	archive->WriteFloatRef(PST_START_GREEN, &mStartGreen);
	archive->WriteFloatRef(PST_START_BLUE, &mStartBlue);
	archive->WriteFloatRef(PST_END_RED, &mEndRed);
	archive->WriteFloatRef(PST_END_GREEN, &mEndGreen);
	archive->WriteFloatRef(PST_END_BLUE, &mEndBlue);
	archive->WriteFloatRef(PST_TRANSITION_RED, &mTransitionRed);
	archive->WriteFloatRef(PST_TRANSITION_GREEN, &mTransitionGreen);
	archive->WriteFloatRef(PST_TRANSITION_BLUE, &mTransitionBlue);
	archive->WriteInt(PST_USE_END, mUseEnd);
	archive->WriteInt(PST_USE_TRANSITION, mUseTransition);
	archive->WriteFloat(PST_TRANSITION_POINT, mTransitionPoint);
}

//******************************************************************************************
BOOL CPDTimeline::Load(CTokenArchive *archive)	// FUNCTION: 0x004c24c0
{
	SINT	i;
	for (i = 0; i < 10; i++)
	{
		mEntries[i].mParticleDescriptor = NULL;
		mEntries[i].mTime = 0;
		mEntries[i].mTransmitFoR = 1;
	}

	SINT	descriptors = 0, times = 0, transmits = 0;
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_NUM_ENTRIES:
			mNumEntries = ival;
			break;
		case PST_PARTICLE_DESCRIPTOR:
			archive->SetFixup(ival, &mEntries[descriptors++].mParticleDescriptor);
			break;
		case PST_TIME:
			mEntries[times++].mTime = ival;
			break;
		case PST_TRANSMIT_FOR:
			mEntries[transmits++].mTransmitFoR = ival;
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDTimeline::Write(CTokenArchive *archive)	// FUNCTION: 0x004c25c0
{
	archive->WriteInt(PST_NUM_ENTRIES, mNumEntries);
	for (SINT i = 0; i < mNumEntries; i++)
	{
		archive->WriteReference(PST_PARTICLE_DESCRIPTOR, mEntries[i].mParticleDescriptor);
		archive->WriteInt(PST_TIME, mEntries[i].mTime);
		archive->WriteInt(PST_TRANSMIT_FOR, mEntries[i].mTransmitFoR);
	}
}

//******************************************************************************************
void CPDTimeline::Unknown9(CParticle *p)	// FUNCTION: 0x004c2620
{
	p->mUnknown7E = 0;
	p->mUnknown80 = 0;
	p->mLife = 9999999.f;
}

//******************************************************************************************
// Fire the entries due at this particle's step; TRUE once past the last. With the extra GeForce FX features
// on, the pulse-bolt explosions leave out their emitters and flash (the shader draws them instead).
// The allocation's frame is a local (read only after its null test) and the descriptor is read through a reference: the descriptor
// is a variable the selector loop above rewrites, and a reference keeps retail's register order in the inlined TransmitFoR (a plain
// local copy of it gives shape 94; a pointer to it works too). TransmitFoR's own one-local nudge (see there) sets the variable
// numbering this depends on.
BOOL CPDTimeline::Unknown10(CParticle *p)	// FUNCTION: 0x004c2640
{
	bool	fx;
	if ((mName[1] == 'e') && (mName[5] == 'P') && EXTRA_GEFORCE_FX_FEATURES.mValue && !GAME.IsMultiplayer() &&
		!PLATFORM.KeyOn(DIK_F1) && ((mName[26] == 'L') || (mName[26] == 'A')) &&
		(!strcmp(mName, "Mech Pulse Bolt Explosion Large") || !strcmp(mName, "Mech Pulse Bolt Explosion Aug")))
		fx = true;
	else
		fx = false;

	if (p->mLife <= 0.f)
		return TRUE;

	while ((mEntries[p->mUnknown80].mTime == p->mUnknown7E) && (p->mUnknown80 < mNumEntries))
	{
		CParticleDescriptor	*pd = mEntries[p->mUnknown80].mParticleDescriptor;
		while (pd && (pd->GetType() == PD_SELECTOR))
			pd = pd->Unknown16();

		if (pd && (!fx || (strcmp(pd->mName, "Plasma Explosion Aug Emitter") && strcmp(pd->mName, "Plasma Explosion Aug Emitter 2") &&
					strcmp(pd->mName, "Mech Pulse Bolt Explosion Large Emitter") && strcmp(pd->mName, "Flash"))))
		{
			CParticleDescriptor * const &descriptor = pd;
			CFoR	*frame = p->mFoR;
			CParticle	*np = PARTICLE_MANAGER.AllocateParticle(descriptor, frame && (frame->mFlags | 1));
			if (np)
			{
				np->mPos = p->mPos;
				TransmitFoR(np, p->mFoR, mEntries[p->mUnknown80].mTransmitFoR);
				PARTICLE_MANAGER.ProcessParticle(np);
				np->mPD->Unknown15(np);
			}
		}
		p->mUnknown80++;
	}

	if (p->mUnknown80 >= mNumEntries)
		return TRUE;
	p->mUnknown7E++;
	return FALSE;
}

//******************************************************************************************
BOOL CPDShape::Load(CTokenArchive *archive)	// FUNCTION: 0x004c2b70
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_TYPE:
			mType = ival;
			break;
		case PST_RING_AXIS:
			mRingAxis = ival;
			break;
		case PST_HEMISPHERE:
			mHemisphere = ival;
			break;
		case PST_NUM_PARTICLES:
			mNumParticles = ival;
			break;
		case PST_RADIUS:
			archive->SetFloatRef(fval, ival, &mRadius);
			break;
		case PST_HOLLOW:
			mHollow = ival;
			break;
		case PST_RANDOMSX:
			mRandomSX = fval;
			break;
		case PST_RANDOMSY:
			mRandomSY = fval;
			break;
		case PST_RANDOMSZ:
			mRandomSZ = fval;
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDShape::Write(CTokenArchive *archive)	// FUNCTION: 0x004c2ca0
{
	archive->WriteInt(PST_TYPE, mType);
	archive->WriteInt(PST_RING_AXIS, mRingAxis);
	archive->WriteInt(PST_HEMISPHERE, mHemisphere);
	archive->WriteInt(PST_NUM_PARTICLES, mNumParticles);
	archive->WriteFloatRef(PST_RADIUS, &mRadius);
	archive->WriteInt(PST_HOLLOW, mHollow);
	archive->WriteFloat(PST_RANDOMSX, mRandomSX);
	archive->WriteFloat(PST_RANDOMSY, mRandomSY);
	archive->WriteFloat(PST_RANDOMSZ, mRandomSZ);
}

//******************************************************************************************
// Place the i'th of n particles (n -1: the shape's own count) on a ring, in a sphere or in a box.
void CPDShape::Unknown18(CParticle *p, SINT i, SINT n)	// FUNCTION: 0x004c2d30
{
	if (n == -1)
		n = mNumParticles;
	float	r = mRadius.GetValue(1.f, p);

	switch (mType)
	{
	case 0:
		{
			float	a = (float)i * PI_M2 / n;
			float	s = (float)sin(a);
			float	c = (float)cos(a);
			switch (mRingAxis)
			{
			case 0:
				p->mPos = FVector(0.f, s * r, -(c * r));
				break;
			case 1:
				p->mPos = FVector(s * r, 0.f, -(c * r));
				break;
			case 2:
				p->mPos = FVector(s * r, -(c * r), 0.f);
				break;
			}
		}
		break;

	case 1:
		{
			SINT	z = (rand() & 0xff) - 128;
			SINT	y = (rand() & 0xff) - 128;
			SINT	x = (rand() & 0xff) - 128;
			p->mPos = FVector((float)x * (1.f / 128.f), (float)y * (1.f / 128.f), (float)z * (1.f / 128.f));
		}
		p->mPos.Normalise();
		if (mHollow)
			p->mPos *= r;
		else
			p->mPos *= (float)((rand() & 0xff) - 128) * r * (1.f / 128.f);
		if (mHemisphere)
		{
			float	z = p->mPos.Z;
			if (!(z > 0.f))
				z = -z;
			p->mPos.Z = -z;
		}
		break;

	case 2:
		{
			SINT	z = (rand() & 0xff) - 128;
			SINT	y = (rand() & 0xff) - 128;
			SINT	x = (rand() & 0xff) - 128;
			p->mPos = FVector((float)x * mRandomSX * r * (1.f / 128.f), (float)y * mRandomSY * r * (1.f / 128.f), (float)z * mRandomSZ * r * (1.f / 128.f));
		}
		break;
	}
}

//******************************************************************************************
BOOL CPDTrail::Load(CTokenArchive *archive)	// FUNCTION: 0x004c3120
{
#line 1258
	char	*sval = new(MEMTYPE_SCRATCHPAD) char[1000];
	SINT	token, ival;
	float	fval;

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_TEXTURE:
			mTexture = CPARTICLETEXTURE::Get(sval, mBlendMode + 1);
			break;
		case PST_BLEND_MODE:
			mBlendMode = ival;
			break;
		case PST_TEXTURE_NUMBER:
			mTextureNumber = ival;
			break;
		case PST_WIDTH:
			mWidth = fval;
			break;
		case PST_START_WIDTH:
			mStartWidth = fval;
			break;
		case PST_NUM_POINTS:
			mNumPoints = ival;
			break;
		case PST_COLOUR_RANGE:
			archive->SetFixup(ival, &mColourRange);
			break;
		case PST_WIGGLE_FACTOR:
			mWiggleFactor = fval;
			break;
		case PST_DISPERSE_RATE:
			mDisperseRate = fval;
			break;
		case PST_GRAVITY:
			mGravity = ival;
			break;
		case PST_LIFE:
			mLife = ival;
			break;
		case PST_TAPER_START:
			mTaperStart = ival;
			break;
		case PST_WIDTH_WITH_LIFE:
			mWidthWithLife = ival;
			break;
		case PST_TYPE:
			mType = ival;
			break;
		case PST_IMPACT_SPAWNEE:
			archive->SetFixup(ival, &mImpactSpawnee);
			break;
		case PST_TRANSMIT_FOR:
			mTransmitFoR = ival;
			break;
		case PST_FADE_POINT:
			mFadePoint = ival;
			break;
		case PST_USE_SEGMENT_LENGTH:
			mUseSegmentLength = ival;
			break;
		case PST_SEGMENTLENGTH:
			mSegmentLength = fval;
			break;
		case PST_MANUAL_WIGGLE_ENABLED:
			mManualWiggleEnabled = ival;
			break;
		case PST_WIGGLE_LENGTH:
			archive->SetFloatRef(fval, ival, &mWiggleLength);
			break;
		case PST_YAW_LENGTH:
			archive->SetFloatRef(fval, ival, &mYawLength);
			break;
		case PST_FLAT:
			mFlat = ival;
			break;
		}
	} while (token != PST_SEPARATOR);

	if (mTexture && mTextureNumber != -1)
	{
		float	u, v;
		mU0 = u = (float)(mTextureNumber % 4) * 0.25f;
		mV0 = v = (float)(mTextureNumber >> 2) * 0.25f;
		mU1 = u + 0.25f;
		mV1 = v + 0.25f;
	}
	else
	{
		mV0 = 0;
		mU0 = 0;
		mV1 = 1.0f;
		mU1 = 1.0f;
	}
	delete [] sval;
	return TRUE;
}

//******************************************************************************************
void CPDTrail::Write(CTokenArchive *archive)	// FUNCTION: 0x004c3440
{
	archive->WriteInt(PST_BLEND_MODE, mBlendMode);
	if (mTexture)
		archive->WriteString(PST_TEXTURE, mTexture->mName);
	archive->WriteInt(PST_TEXTURE_NUMBER, mTextureNumber);
	archive->WriteFloat(PST_WIDTH, mWidth);
	archive->WriteFloat(PST_START_WIDTH, mStartWidth);
	archive->WriteInt(PST_NUM_POINTS, mNumPoints);
	archive->WriteReference(PST_COLOUR_RANGE, mColourRange);
	archive->WriteFloat(PST_WIGGLE_FACTOR, mWiggleFactor);
	archive->WriteFloat(PST_DISPERSE_RATE, mDisperseRate);
	archive->WriteInt(PST_GRAVITY, mGravity);
	archive->WriteInt(PST_LIFE, mLife);
	archive->WriteInt(PST_TAPER_START, mTaperStart);
	archive->WriteInt(PST_WIDTH_WITH_LIFE, mWidthWithLife);
	archive->WriteInt(PST_TYPE, mType);
	archive->WriteReference(PST_IMPACT_SPAWNEE, mImpactSpawnee);
	archive->WriteInt(PST_TRANSMIT_FOR, mTransmitFoR);
	archive->WriteInt(PST_FADE_POINT, mFadePoint);
	archive->WriteInt(PST_USE_SEGMENT_LENGTH, mUseSegmentLength);
	archive->WriteFloat(PST_SEGMENTLENGTH, mSegmentLength);
	archive->WriteInt(PST_MANUAL_WIGGLE_ENABLED, mManualWiggleEnabled);
	archive->WriteFloatRef(PST_WIGGLE_LENGTH, &mWiggleLength);
	archive->WriteFloatRef(PST_YAW_LENGTH, &mYawLength);
	archive->WriteInt(PST_FLAT, mFlat);
}

//******************************************************************************************
void CPDTrail::Unknown9(CParticle *p)	// FUNCTION: 0x004c35b0
{
	p->mLife = (float)mLife;
	p->mUnknown80 = 0;
}

//******************************************************************************************
// The trail's points: a fixed number, or one per segment length of the way its frame has come.
void CPDTrail::AllocatePoints(CParticle *p)	// FUNCTION: 0x004c35d0
{
	if (!mType)
		p->mUnknown80 = mNumPoints;
	else
	{
		CFoR	*f = p->mFoR;
		if (!f)
			return;
		if (mUseSegmentLength)
		{
			FVector	d = f->mPos - f->mUnknown90;
			// Retail adds the Y/Z squares before X; keep that grouping explicit.
			p->mUnknown80 = SINT((float)sqrt(d.X * d.X + (d.Y * d.Y + d.Z * d.Z)) / mSegmentLength) + 3;
		}
		else
			p->mUnknown80 = mNumPoints;
	}

	p->AllocateData(p->mUnknown80 * sizeof(STrailPoint));
	STrailPoint	*points = (STrailPoint *)p->mData;
	for (SINT i = 0; i < p->mUnknown80; i++)
	{
		points[i].mUnknown24 = 0;
		points[i].mWidth = 0.2f;
	}
}

//******************************************************************************************
// A trail particle's point count (file-local accessor; name ours): see the stand-in note on CPDTrail::Unknown10.
static inline SINT NumPoints(CParticle *p) { return p->mUnknown80; }

//******************************************************************************************
// A streaming trail (type 0) adds a point each frame while it is wide enough; a stretched trail (type 1) lays its
// points from the particle to its frame's origin, wiggled, and dies with its frame.
// The vector forms are Stuart's (Camera.cpp's `float l = v.Magnitude(); if (l > 0.001f) v /= l;`), the points
// are re-read per iteration as retail does (004c371d), and each point's position is `wiggle + at` written at both
// uses: VC6 keeps the three sums from the first (004c3e74, 004c3e93, 004c3eb0) and builds the spawn's vector from
// them (004c3fc3), retail's 0xf0 frame. The points pointer is read after `step` (004c3895) and the result is
// returned as Stuart writes it (thing.h's IsDying: `if (...) return TRUE; else return FALSE;`).
// STAND-IN (concession): the impact test reads the point count through NumPoints, a file-local accessor we made
// up. Why: retail calls FVector(x,y,z) out of line inside `side * ...`, the across sum and
// `wiggle * mDisperseRate` (004c3ce0, 004c3d22, 004c3efa) and expands the one in the spawn's `wiggle + at`.
// By the decoded inline rule (the inline-budget simulator) the last two need one more inline call of size 40 or less between
// them, whatever the function's size: without one, the constructor in `wiggle * mDisperseRate` gets budget 57
// (expanded) and the spawn's gets 28 (called); with it, 43 and 53. How: the accessor is that call, a free site
// that emits no code of its own. Plain form (`i < p->mUnknown80 - 1`): those two decisions swap, shape 92. What
// Stuart called there is unknown; an existing accessor does the same at either read of p->mFoR in the
// AllocateParticle argument (CParticle::GetFoR, now compiled for PDSimpleSprite.cpp only).
BOOL CPDTrail::Unknown10(CParticle *p)	// FUNCTION: 0x004c36b0
{
	BOOL	fresh = FALSE;
	if (!p->mData)
	{
		AllocatePoints(p);
		fresh = TRUE;
	}

	if (mGravity)
		p->mVelocity.Z += PARTICLE_MANAGER.GetFrameLength() * 0.003f;

	if (mDisperseRate != 0.f)
	{
		for (SINT i = 0; i < p->mUnknown80; i++)
		{
			STrailPoint	*points = (STrailPoint *)p->mData;
			if (points[i].mUnknown24)
				points[i].mPos += points[i].mUnknown10 * PARTICLE_MANAGER.GetFrameLength();
		}
	}

	switch (mType)
	{
	case 0:
		if (PARTICLE_MANAGER.IsFirstFrame())
		{
			float	width = p->mRadius;
			if ((width > 0.001f) && ((p->mLife < 0.f) || ((float)p->mUnknown80 < p->mLife)))
				Unknown19(p, &p->mPos, width);
			else
				Unknown20(p);
		}
		break;

	case 1:
		if ((p->mRadius > 0.001f) && ((p->mLife < -1.f) || ((float)mFadePoint < p->mLife)) && p->mFoR)
		{
			CFoR	*f = p->mFoR;
			FVector	at = p->mPos;
			if (f->mHasOrientation)
				at = f->mOrientation * at;
			at += f->mPos;

			FVector	step = (f->mUnknown90 - at) / (float)(p->mUnknown80 - 2);
			STrailPoint	*points = (STrailPoint *)p->mData;

			FVector	side, up;
			if (mManualWiggleEnabled)
			{
				side = step ^ FVector(0.f, 0.f, -1.f);	// 004c3910: retain the cross product's signed-zero subtractions.
				float	m = side.Magnitude();
				if (m > 0.0001f)
					side /= m;	// 004c398b: divide stored components by the live x87 magnitude.
				else
					side = FVector(1.f, 0.f, 0.f);

				up = step ^ side;
				m = up.Magnitude();
				if (m > 0.0001f)
					up /= m;	// 004c3a74: the same in-place boundary for the second basis vector.
				else
					up = FVector(0.f, 1.f, 0.f);
			}

			p->mUnknown7E = 0;
			for (SINT i = 0; i < p->mUnknown80; i++)
			{
				FVector	wiggle;
				if (mManualWiggleEnabled)
				{
					float	t = (float)i / (float)(p->mUnknown80 - 1);
					float	length = mWiggleLength.GetValue(t, p);
					float	yaw = mYawLength.GetValue(t, p);
					FVector	across = up * ((float)sin(yaw) * length);
					wiggle = side * ((float)cos(yaw) * length) + across;
				}
				else if ((i == 1) || (i == p->mUnknown80 - 1))
					wiggle = ZERO_FVECTOR;
				else
					wiggle = FVector(RandomScaled(mWiggleFactor), RandomScaled(mWiggleFactor), RandomScaled(mWiggleFactor));

				points[i].mUnknown24 = 1;
				points[i].mPos = wiggle + at;
				points[i].mWidth = p->mRadius;
				points[i].mUnknown10 = wiggle * mDisperseRate;

				if (fresh && mImpactSpawnee && (i > 1) && (i < NumPoints(p) - 1))	// stand-in: see above
				{
					CParticleDescriptor	*pd = mImpactSpawnee;
					while (pd && (pd->GetType() == PD_SELECTOR))
						pd = pd->Unknown16();
					if (pd)
					{
						CParticle	*np = PARTICLE_MANAGER.AllocateParticle(pd, p->mFoR && (p->mFoR->mFlags | 1));
						if (np)
						{
							np->mPos = wiggle + at;
							PARTICLE_MANAGER.ProcessParticle(np);
							np->mPD->Unknown15(np);
						}
					}
				}

				if (i)
					at += step;
			}
			p->mUnknown7E = p->mUnknown80 - 1;
		}
		break;
	}

	if ((mType == 1) && !p->mFoR)
		return TRUE;
	else
		return FALSE;
}

//******************************************************************************************
// Add a point at pos (in the frame's space) with that width, wiggled and dispersing.
void CPDTrail::Unknown19(CParticle *p, const FVector *pos, float width)	// FUNCTION: 0x004c4100
{
	STrailPoint	*points = (STrailPoint *)p->mData;
	SINT		last = points[p->mUnknown7E].mUnknown24;

	p->mUnknown7E++;
	if (p->mUnknown7E >= p->mUnknown80)
		p->mUnknown7E = 0;

	FVector	wiggle;
	if (last)
		wiggle = FVector(RandomScaled(mWiggleFactor), RandomScaled(mWiggleFactor), RandomScaled(mWiggleFactor));	// retail draws all three first
	else
		wiggle = ZERO_FVECTOR;

	FVector	at = *pos;
	CFoR	*f = p->mFoR;
	if (f)
	{
		if (f->mHasOrientation)
			at = f->mOrientation * at;
		at.X += f->mPos.X;
		at.Y += f->mPos.Y;
		at.Z += f->mPos.Z;
	}

	points[p->mUnknown7E].mUnknown24 = 1;
	points[p->mUnknown7E].mPos = wiggle + at;
	points[p->mUnknown7E].mWidth = width;
	points[p->mUnknown7E].mUnknown10 = wiggle * mDisperseRate;
	p->mUnknown7C = p->mUnknown7E;
}

//******************************************************************************************
void CPDTrail::Unknown20(CParticle *p)	// FUNCTION: 0x004c43a0
{
	p->mUnknown7E++;
	if (p->mUnknown7E >= p->mUnknown80)
		p->mUnknown7E = 0;
	((STrailPoint *)p->mData)[p->mUnknown7E].mUnknown24 = 0;
}

//******************************************************************************************
void CPDTrail::Unknown21(CParticle *p)	// FUNCTION: 0x004c43e0
{
	if (!mType)
		p->mLife = (float)(p->mUnknown80 + 2);
	else
		p->mLife = (float)mFadePoint;
}

//******************************************************************************************
BOOL CPDMover::Load(CTokenArchive *archive)	// FUNCTION: 0x004c4420
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_GRAVITYPC:
			archive->SetFloatRef(fval, ival, &mGravityPC);
			break;
		case PST_YAW:
			mYaw = fval;
			break;
		case PST_PITCH:
			mPitch = fval;
			break;
		case PST_ROLL:
			mRoll = fval;
			break;
		case PST_YAW_FUNCTION:
			archive->SetFixup(ival, &mYawFunction);
			break;
		case PST_PITCH_FUNCTION:
			archive->SetFixup(ival, &mPitchFunction);
			break;
		case PST_ROLL_FUNCTION:
			archive->SetFixup(ival, &mRollFunction);
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDMover::Write(CTokenArchive *archive)	// FUNCTION: 0x004c44f0
{
	archive->WriteFloatRef(PST_GRAVITYPC, &mGravityPC);
	archive->WriteFloat(PST_YAW, mYaw);
	archive->WriteFloat(PST_PITCH, mPitch);
	archive->WriteFloat(PST_ROLL, mRoll);
	archive->WriteReference(PST_YAW_FUNCTION, mYawFunction);
	archive->WriteReference(PST_PITCH_FUNCTION, mPitchFunction);
	archive->WriteReference(PST_ROLL_FUNCTION, mRollFunction);
}

//******************************************************************************************
// Gravity, and the orientation from the yaw, pitch and roll (each plus its function at x, as a fraction of pi).
void CPDMover::Unknown12(CParticle *p, float x)	// FUNCTION: 0x004c4560
{
	float	g = mGravityPC.GetValue(x, p);
	p->mVelocity.Z += PARTICLE_MANAGER.GetFrameLength() * g * 0.003f;

	float	yaw = mYaw, pitch = mPitch, roll = mRoll;
	if (mYawFunction)
		yaw += ((CPDFunction *)mYawFunction)->GetValue(x, NULL) * PI;
	if (mPitchFunction)
		pitch += ((CPDFunction *)mPitchFunction)->GetValue(x, NULL) * PI;
	if (mRollFunction)
		roll += ((CPDFunction *)mRollFunction)->GetValue(x, NULL) * PI;
	p->Orientation().MakeRotationF(yaw, pitch, roll);
}

//******************************************************************************************
BOOL CPDFunction::Load(CTokenArchive *archive)	// FUNCTION: 0x004c4840
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_PARAM_A:
			archive->SetFloatRef(fval, ival, &mParamA);
			break;
		case PST_PARAM_B:
			archive->SetFloatRef(fval, ival, &mParamB);
			break;
		case PST_PARAM_C:
			archive->SetFloatRef(fval, ival, &mParamC);
			break;
		case PST_PARAM_D:
			archive->SetFloatRef(fval, ival, &mParamD);
			break;
		case PST_PARAM_FUNCTION:
			mParamFunction = ival;
			break;
		case PST_CLIP:
			mClip = ival;
			break;
		case PST_VALUE_TYPE:
			mValueType = ival;
			break;
		case PST_GAMETURN_SCALE:
			archive->SetFloatRef(fval, ival, &mGameturnScale);
			break;
		case PST_OFFSET_GAMETURN:
			mOffsetGameturn = ival;
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDFunction::Write(CTokenArchive *archive)	// FUNCTION: 0x004c4920
{
	archive->WriteFloatRef(PST_PARAM_A, &mParamA);
	archive->WriteFloatRef(PST_PARAM_B, &mParamB);
	archive->WriteFloatRef(PST_PARAM_C, &mParamC);
	archive->WriteFloatRef(PST_PARAM_D, &mParamD);
	archive->WriteInt(PST_PARAM_FUNCTION, mParamFunction);
	archive->WriteInt(PST_CLIP, mClip);
	archive->WriteInt(PST_VALUE_TYPE, mValueType);
	archive->WriteFloatRef(PST_GAMETURN_SCALE, &mGameturnScale);
	archive->WriteInt(PST_OFFSET_GAMETURN, mOffsetGameturn);
}

//******************************************************************************************
CPDMesh::~CPDMesh()	// FUNCTION: 0x004c49b0
{
	if (mMesh)
	{
		mMesh->Release();
		mMesh = NULL;
	}
}

//******************************************************************************************
BOOL CPDMesh::Load(CTokenArchive *archive)	// FUNCTION: 0x004c4b00
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_LIFE:
			mLife = ival;
			break;
		case PST_GRAVITY:
			mGravity = ival;
			break;
		case PST_BOUNCE:
			mBounce = ival;
			break;
		case PST_ANGULAR_MOMENTUM:
			mAngularMomentum = fval;
			break;
		case PST_TRANSMIT_FOR:
			mTransmitFoR = ival;
			break;
		case PST_AUTO_CENTRE:
			mAutoCentre = ival;
			break;
		case PST_MESH:
			{
				char	*name = strrchr(sval, '\\');
				if (name)
					mMesh = CMesh::GetMesh(name + 1, TRUE);
				else
					mMesh = CMesh::GetMesh(sval, TRUE);
			}
			break;
		case PST_IMPACT_SPAWNEE:
			archive->SetFixup(ival, &mImpactSpawnee);
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDMesh::Write(CTokenArchive *archive)	// FUNCTION: 0x004c4c70
{
	if (mMesh)
		archive->WriteString(PST_MESH, mMesh->mName);
	archive->WriteInt(PST_GRAVITY, mGravity);
	archive->WriteInt(PST_LIFE, mLife);
	archive->WriteInt(PST_BOUNCE, mBounce);
	archive->WriteFloat(PST_ANGULAR_MOMENTUM, mAngularMomentum);
	archive->WriteReference(PST_IMPACT_SPAWNEE, mImpactSpawnee);
	archive->WriteInt(PST_TRANSMIT_FOR, mTransmitFoR);
	archive->WriteInt(PST_AUTO_CENTRE, mAutoCentre);
}

//******************************************************************************************

void CPDMesh::Unknown9(CParticle *p)	// FUNCTION: 0x004c4cf0
{
	p->mLife = (float)mLife;
	// retail draws all three randoms (each with its own copy of Angular_Momentum) before the products, and copies
	// the radius through the FPU (004c4d01..004c4dae)
	FVector	spin(RandomScaled(mAngularMomentum), RandomScaled(mAngularMomentum), RandomScaled(mAngularMomentum));
	p->mU0 = spin.X;
	p->mU1 = spin.Y;
	p->mV0 = spin.Z;
	p->mUnknown7E = 0;
	if (mMesh)
		p->mRadius = mMesh->Radius();
}

//******************************************************************************************
// Tumble and fall until the mesh reaches the ground; then settle there and spawn the impact effect.
// This function sits at the edge of the inline budget: retail calls FVector's += and -= and FMatrix::Transpose out of line
// and inlines the first matrix-vector product, and a few expression nodes either way (a local copy of p->mFoR, a bare
// statement where retail has braces) move those sites. `ground` is declared at the top, as a frame of its own.
BOOL CPDMesh::Unknown10(CParticle *p)	// FUNCTION: 0x004c4dc0
{
	FVector	ground;
	if (!(p->mUnknown7C & 1))
	{
		if (PARTICLE_MANAGER.IsFirstFrame())
		{
			FMatrix	spin;
			spin.MakeRotationF(p->mU0, p->mU1, p->mV0);
			p->mOrientation = p->mOrientation * spin;
		}
		if (mGravity)
			p->mVelocity.Z += PARTICLE_MANAGER.GetFrameLength() * 0.003f;
	}

	if (mBounce && !(p->mUnknown7C & 1))
	{
		FVector	pos; pos = p->mPos;
		if (p->mFoR)
		{
			if (p->mFoR->mHasOrientation)
				pos = p->mFoR->mOrientation * pos;
			pos += p->mFoR->mPos;
		}

		if (PARTICLE_MANAGER.OnGround(pos, p->mRadius / 10.0f, ground))
		{
			if (p->mFoR)
			{
				ground -= p->mFoR->mPos;
				if (p->mFoR->mHasOrientation)
				{
					ground = p->mFoR->mOrientation.Transpose() * ground;
				}
			}
			p->mPos = ground;
			p->mVelocity = ZERO_FVECTOR;
			p->mUnknown7C |= 1;

			CParticleDescriptor	*pd = mImpactSpawnee;
			while (pd && (pd->GetType() == PD_SELECTOR))
				pd = pd->Unknown16();
			if (pd)
			{
				CParticle	*np = PARTICLE_MANAGER.AllocateParticle(pd, p->mFoR && (p->mFoR->mFlags | 1));
				if (np)
				{
					np->mPos = p->mPos;
					TransmitFoR(np, p->mFoR, mTransmitFoR);
					PARTICLE_MANAGER.ProcessParticle(np);
					np->mPD->Unknown15(np);
				}
			}
		}
	}
	return FALSE;
}

//******************************************************************************************
BOOL CPDFoR::Load(CTokenArchive *archive)	// FUNCTION: 0x004c5330
{
	SINT	token, ival;
	float	fval;
	char	sval[1000];

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_DEATH:
			archive->SetFixup(ival, &mDeath);
			break;
		case PST_INITIAL:
			archive->SetFixup(ival, &mInitial);
			break;
		case PST_MOVER:
			archive->SetFixup(ival, &mMover);
			break;
		}
	} while (token != PST_SEPARATOR);
	return TRUE;
}

//******************************************************************************************
void CPDFoR::Write(CTokenArchive *archive)	// FUNCTION: 0x004c53b0
{
	archive->WriteReference(PST_INITIAL, mInitial);
	archive->WriteReference(PST_DEATH, mDeath);
	archive->WriteReference(PST_MOVER, mMover);
}

//******************************************************************************************
void CPDFoR::Unknown9(CParticle *p)	// FUNCTION: 0x004c53f0
{
	p->mLife = -2.f;
	p->mUnknown7E = 0;
}

//******************************************************************************************
// The link's effect takes the particle's frame flags, when it has an effect (as SetPos and SetOrientation test it).
static inline void SetLinkFlags(CParticleEffectLink *link, UWORD flags)
{
	if (link->mEffect)
		link->mEffect->mFlags = flags;
}

// The frame's last-moved time, through an accessor: retail copies it through the FPU (fld/fstp at 004c562c).
static inline float UnknownAC(CFoR *f)
{
	return f->mUnknownAC;
}

//******************************************************************************************
// The frame's own effect follows the particle; once that effect has ended the particle dies, spawning its death
// effect where it was.
// The empty death list returns the prepared result; a visited list returns TRUE directly after its walk. This
// reconstruction reproduces the retail TRUE load at 004c5647, both selector-loop exits and the final epilogue.
// STAND-IN: the flags argument is `p->mData ? flags : flags`, two identical arms. Retail's flag
// copy (004c544f..004c545d: the effect pointer in ecx, the flags in ax) needs the frame (p->mFoR, live from the first
// test to the flags read) to leave the allocator's queue before the effect pointer (link->mEffect): plain, the effect
// is 24 against the frame's 23, takes eax first, and the flags get dx. The conditional reaches the allocator as a branch
// between the flags read and the effect test, so the frame scores 19 against the effect's 18 and gives the flags its
// eax preference; C2 merges the identical arms afterwards (0x10758522) and drops the test, which repeats the link test,
// so it emits no code. The `link` or `p->mFoR` variables as the condition are folded first and do nothing; a value not
// tested before keeps its load and compare (p->mPD). Plain form (SetLinkFlags(link,
// p->mFoR->mFlags)): twelve differing byte positions. 37 natural respellings leave the allocator's input unchanged
// (README); the original source is unknown.
BOOL CPDFoR::Unknown10(CParticle *p)	// FUNCTION: 0x004c5410
{
	CParticleEffectLink	*link;
	CParticleDescriptor	*pd;

	if (p->mFoR && (link = (CParticleEffectLink *)p->mData))
		SetLinkFlags(link, p->mData ? p->mFoR->mFlags : p->mFoR->mFlags);

	if (mMover)
		mMover->Unknown12(p, 1.f);

	if (!p->mUnknown7E)
	{
		p->mUnknown7E = 1;
#line 2025
		link = new(MEMTYPE_PARTICLE) CParticleEffectLink;
		p->mData = link;
		pd = mInitial;
		while (pd && (pd->GetType() == PD_SELECTOR))
			pd = pd->Unknown16();
		if (pd)
			PARTICLE_MANAGER.CreateEffect(pd, link, ZERO_FVECTOR, 0, p->mFoR && (p->mFoR->mFlags | 1));
	}

	link = (CParticleEffectLink *)p->mData;
	FollowParticle(link, p);

	if (p->mFoR && link->Full())
		link->mEffect->mUnknownAC = UnknownAC(p->mFoR);

	BOOL result;
	if (!link->Full())
	{
		result = TRUE;
		pd = mDeath;
		if (pd)
		{
			while (pd && (pd->GetType() == PD_SELECTOR))
				pd = pd->Unknown16();
			if (pd)
			{
				CParticle	*np = PARTICLE_MANAGER.AllocateParticle(pd, p->mFoR && (p->mFoR->mFlags | 1));
				np->mPos = p->mPos;
				np->Orientation() = p->Orientation();
				np->mFoR = p->mFoR;
			}
			return TRUE;
		}
	}
	else
		result = FALSE;
	return result;
}

//******************************************************************************************
// The particle's data is its link to the effect it spawned; stop that and let the manager keep nothing.
BOOL CPDFoR::Unknown14(CParticle *p)	// FUNCTION: 0x004c5700
{
	CParticleEffectLink	*link = (CParticleEffectLink *)p->mData;
	if (link)
	{
		link->Stop(TRUE);
		delete link;
	}
	return FALSE;
}

//******************************************************************************************
BOOL CPDPMesh::Load(CTokenArchive *archive)	// FUNCTION: 0x004c5730
{
#line 2109
	char	*sval = new(MEMTYPE_SCRATCHPAD) char[1000];
	SINT	token, ival;
	float	fval;

	do
	{
		archive->ReadToken(&token, &ival, &fval, sval);
		switch (token)
		{
		case PST_TYPE:
			mType = ival;
			break;
		case PST_LIFE:
			mLife = ival;
			break;
		case PST_BLEND_MODE:
			mBlendMode = ival;
			break;
		case PST_TEXTURE:
			mTexture = CPARTICLETEXTURE::Get(sval, mBlendMode + 1);
			break;
		case PST_COLOUR_RANGE:
			archive->SetFixup(ival, &mColourRange);
			break;
		case PST_COLOUR_RANGE2:
			archive->SetFixup(ival, &mColourRange2);
			break;
		case PST_TILE_U:
			archive->SetFloatRef(fval, ival, &mTileU);
			break;
		case PST_TILE_V:
			archive->SetFloatRef(fval, ival, &mTileV);
			break;
		case PST_SCROLL_U:
			archive->SetFloatRef(fval, ival, &mScrollU);
			break;
		case PST_SCROLL_V:
			archive->SetFloatRef(fval, ival, &mScrollV);
			break;
		case PST_CYLINDER_NUMPTSAXIAL:
			mCylinderNumPtsAxial = ival;
			break;
		case PST_CYLINDER_NUMPTSRADIAL:
			mCylinderNumPtsRadial = ival;
			break;
		case PST_CYLINDER_RADIUS:
			archive->SetFloatRef(fval, ival, &mCylinderRadius);
			break;
		case PST_CYLINDER_RADIUS2:
			archive->SetFloatRef(fval, ival, &mCylinderRadius2);
			break;
		case PST_CYLINDER_LENGTH:
			archive->SetFloatRef(fval, ival, &mCylinderLength);
			break;
		case PST_SPHERE_NUMPTSAX:
			mSphereNumPtsAx = ival;
			break;
		case PST_SPHERE_NUMPTSRAD:
			mSphereNumPtsRad = ival;
			break;
		case PST_SPHERE_RADIUSTIME:
			archive->SetFloatRef(fval, ival, &mSphereRadiusTime);
			break;
		case PST_SPHERE_LATITUDE_START:
			archive->SetFloatRef(fval, ival, &mSphereLatitudeStart);
			break;
		case PST_SPHERE_LATITUDE_END:
			archive->SetFloatRef(fval, ival, &mSphereLatitudeEnd);
			break;
		case PST_SPHERE_LONGITUDE_START:
			archive->SetFloatRef(fval, ival, &mSphereLongitudeStart);
			break;
		case PST_SPHERE_LONGITUDE_END:
			archive->SetFloatRef(fval, ival, &mSphereLongitudeEnd);
			break;
		}
	} while (token != PST_SEPARATOR);
	delete [] sval;
	return TRUE;
}

//******************************************************************************************
void CPDPMesh::Write(CTokenArchive *archive)	// FUNCTION: 0x004c59e0
{
	archive->WriteInt(PST_TYPE, mType);
	archive->WriteInt(PST_LIFE, mLife);
	archive->WriteInt(PST_BLEND_MODE, mBlendMode);
	if (mTexture)
		archive->WriteString(PST_TEXTURE, mTexture->mName);
	archive->WriteReference(PST_COLOUR_RANGE, mColourRange);
	archive->WriteReference(PST_COLOUR_RANGE2, mColourRange2);
	archive->WriteFloatRef(PST_TILE_U, &mTileU);
	archive->WriteFloatRef(PST_TILE_V, &mTileV);
	archive->WriteFloatRef(PST_SCROLL_U, &mScrollU);
	archive->WriteFloatRef(PST_SCROLL_V, &mScrollV);
	archive->WriteInt(PST_CYLINDER_NUMPTSAXIAL, mCylinderNumPtsAxial);
	archive->WriteInt(PST_CYLINDER_NUMPTSRADIAL, mCylinderNumPtsRadial);
	archive->WriteFloatRef(PST_CYLINDER_RADIUS, &mCylinderRadius);
	archive->WriteFloatRef(PST_CYLINDER_RADIUS2, &mCylinderRadius2);
	archive->WriteFloatRef(PST_CYLINDER_LENGTH, &mCylinderLength);
	archive->WriteInt(PST_SPHERE_NUMPTSAX, mSphereNumPtsAx);
	archive->WriteInt(PST_SPHERE_NUMPTSRAD, mSphereNumPtsRad);
	archive->WriteFloatRef(PST_SPHERE_RADIUSTIME, &mSphereRadiusTime);
	archive->WriteFloatRef(PST_SPHERE_LATITUDE_START, &mSphereLatitudeStart);
	archive->WriteFloatRef(PST_SPHERE_LATITUDE_END, &mSphereLatitudeEnd);
	archive->WriteFloatRef(PST_SPHERE_LONGITUDE_START, &mSphereLongitudeStart);
	archive->WriteFloatRef(PST_SPHERE_LONGITUDE_END, &mSphereLongitudeEnd);
}

//******************************************************************************************
void CPDPMesh::Unknown9(CParticle *p)	// FUNCTION: 0x004c5b40
{
	p->mLife = (float)mLife;
	p->mRadius = 1.f;
}

//******************************************************************************************
// The descriptors' per-particle steps that no code above defined. Each compiles to a body another function already
// has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address
// each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x00452b60
void CPDSimpleSprite::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004d6b20
BOOL CPDPMesh::Unknown10(CParticle *p)
{
	return FALSE;
}

// FUNCTION: 0x004c49e0 ?GetTypeName@CParticleDescriptor@@UAEPADXZ
// FUNCTION: 0x004c49f0 ?GetTag@CParticleDescriptor@@UAEPADXZ
// FUNCTION: 0x004c4a00 ?GetReference@CParticleDescriptor@@UAEPAV1@HH@Z
// FUNCTION: 0x004c4a10 ??_GCParticleDescriptor@@UAEPAXI@Z
// FUNCTION: 0x004c4a40 ?GetType@CPDMesh@@UAEHXZ
// FUNCTION: 0x004c4a50 ?GetTypeName@CPDMesh@@UAEPADXZ
// FUNCTION: 0x004c4a60 ?GetTag@CPDMesh@@UAEPADXZ
// FUNCTION: 0x004c4a70 ?RemoveReference@CPDMesh@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004c4a90 ?GetReference@CPDMesh@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004c4ae0 ??_GCPDMesh@@UAEPAXI@Z

// FUNCTION: 0x004c0050 init:ZERO_FVECTOR
// FUNCTION: 0x004c0070 init:ID_FMATRIX

// Out of line: retail folds this `return 5` body into 004dbc20 with CInfantryUnit::GetImposterFrames and
// CRTCutscene::Unknown27, and LINK folds an out-of-line body only with out-of-line ones.
SINT CPDColourRange::GetType()
{
	return PD_COLOUR_RANGE;
}
