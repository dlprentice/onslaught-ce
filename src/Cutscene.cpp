// Cutscene.cpp: reconstructed from Cutscene.obj (0043e7f0..0043fcef); Stuart's release has no copy. A cutscene
// thing plays a .cut file's animation nodes on its CRTCutscene render thing through a movie camera, with sample
// tracks (music and one per language) and optional fades. The allocations' __LINE__s are the retail ones; the
// #lines place them. gAnonymousCPU is unreferenced in retail (no code refers to it); the specimen puts it at 005dacfc,
// the head of this object's .rdata. Its name is ours (an older guess put it in a missing CPU-detection object).
#include "common.h"
#include "SoundManager.h"
#include "PCSoundManager.h"
#include "Cutscene.h"
#include "chunker.h"
#include "membuffer.h"
#include "world.h"
#include "game.h"
#include "engine.h"
#include "Camera.h"
#include "Hud.h"
#include "mesh.h"
#include "eventmanager.h"
#include "DebugLog.h"
extern const char gAnonymousCPU[] = "AnonymousCPU";	// external: VC6 drops an unreferenced static const
// FUNCTION: 0x0043e7f0 init:ZERO_FVECTOR
// FUNCTION: 0x0043e810 init:ID_FMATRIX

CCutscene	*CCutscene::sCurrent = NULL;

char	*CUTSCENE_LANGUAGES[] = { "English", "Japanese" };

//******************************************************************************************
CCutscene::~CCutscene()	// FUNCTION: 0x0043e8e0
{
	Stop();
}

// FUNCTION: 0x0043e9c0 ?GetVelocity@CThing@@UAE?AVFVector@@XZ
// FUNCTION: 0x0043ea40 ?_GetClassName@CCutscene@@UAEPADXZ
// FUNCTION: 0x0043ea50 ?_GetClassID@CCutscene@@UAEHXZ
// FUNCTION: 0x0043ea60 ?SetThingType@CCutscene@@UAEXK@Z
// FUNCTION: 0x0043ea70 ?GetRenderAnimation@CCutscene@@UAE?AW4EAnimMode@@XZ
// FUNCTION: 0x0043ea80 ?GetRealAnimIndex@CCutscene@@UAEHXZ
// FUNCTION: 0x0043ea90 ??_GCCutscene@@UAEPAXI@Z

//******************************************************************************************
void CCutscene::Init(CInitThing *init)	// FUNCTION: 0x0043eab0
{
	CCutsceneInitThing	*ci = (CCutsceneInitThing *)init;

	mPlaying = FALSE;
	mTime = 0;
	mLength = -1;
	mNumTracks = 1;

	SINT	n;
	for (n = 0; n < MAX_CUTSCENE_TRACKS; n++)
	{
		mTracks[n] = NULL;
		mTrackType[n] = CTT_NONE;
		mNumAnims[n] = 0;
		mTrackLanguage[n] = -1;
	}

	mTrackType[0] = CTT_VIDEO;
	strcpy(mTrackName[0], "Video");
	mTrackType[1] = CTT_AUDIO;
	strcpy(mTrackName[1], "Music");
	for (n = 0; n < 2; n++)
	{
		mTrackType[n + 2] = CTT_AUDIO;
		strcpy(mTrackName[n + 2], CUTSCENE_LANGUAGES[n]);
		strcat(mTrackName[n + 2], " audio");
		mTrackLanguage[n + 2] = n;
	}
	mNumTracks = 4;

	strcpy(mLinkToName, ci->mLinkTo);
	mLanguage = 0;
	strcpy(mFileName, ci->mFile);

	Load();

	SUPERTYPE::Init(init);
	StickToGround();
	mFlags |= TF_DONT_RENDER;
}

//******************************************************************************************
void CCutscene::Shutdown()	// FUNCTION: 0x0043eca0
{
	Stop();

	for (SINT n = 0; n < MAX_CUTSCENE_TRACKS; n++)
	{
		delete mTracks[n];
		mTracks[n] = NULL;
	}

	SUPERTYPE::Shutdown();
}

// FUNCTION: 0x0043ed20 ??_GCCutsceneAnimNode@@QAEPAXI@Z

//******************************************************************************************
// RECONSTRUCTED (dropped by /OPT:REF): nothing calls Save, so the linker removed its code, but its first references
// order Cutscene's string literals. Each function emits the literals it references first, in reverse reference
// order, and functions go in source order; retail's run before Load's own literals is the path, "1.00", __FILE__
// (the chunker's new) and the chunk tags CUTE, CPOS, TRKS, EFAD, TDAT, SDAT, first referenced in that order, and
// Load's `#line 203` leaves about 90 original lines before its new. The body is inferred from Load (the same chunks
// and fields, written with CChunker::Start/Write/End), not byte-verified: only its literal references reach the image.
BOOL CCutscene::Save()
{
	char	name[4096];
	ULONG	version;

	sprintf(name, "data\\cutscenes\\%s.cut", mFileName);
	version = MKID("1.00");

	CChunker	*writer = new(MEMTYPE_CHUNKER) CChunker();
	if (!writer->Open(name))
		return FALSE;

	writer->Start(MKID("CUTE"));
	writer->Write(&version, 4, 1);
	writer->End();

	writer->Start(MKID("CPOS"));
	writer->Write(&mPos, sizeof(mPos), 1);
	writer->Write(&mLength, 4, 1);
	writer->End();

	writer->Start(MKID("TRKS"));
	writer->Write(&mNumTracks, 4, 1);
	writer->End();

	writer->Start(MKID("EFAD"));
	writer->Write(mEndFade, 256, 1);
	writer->Write(&mEndFadeUnknown, 4, 1);
	writer->End();

	SINT	track;
	for (track = 0; track < mNumTracks; track++)
	{
		SINT	unused = 0;
		writer->Start(MKID("TDAT"));
		writer->Write(&track, 4, 1);
		writer->Write(mTrackName[track], 32, 1);
		writer->Write(&mTrackType[track], 4, 1);
		writer->Write(&mTrackLanguage[track], 4, 1);
		writer->Write(&unused, 4, 1);
		writer->End();
	}

	for (track = 0; track < mNumTracks; track++)
	{
		for (CCutsceneAnimNode *node = mTracks[track]; node; node = node->mNext)
		{
			writer->Start(MKID("SDAT"));
			writer->Write(&track, 4, 1);
			writer->Write(node->mAnimName, 32, 1);
			writer->Write(node->mFade, 256, 1);
			writer->Write(&node->mUnknown230, 4, 1);
			writer->Write(node->mName, 256, 1);
			writer->Write(&node->mFlags, 4, 1);
			writer->Write(&node->mLength, 4, 1);
			writer->Write(&node->mStart, 4, 1);
			writer->Write(&node->mOffset, 4, 1);
			writer->End();
		}
	}

	writer->Close();
	delete writer;
	return TRUE;
}

//******************************************************************************************
BOOL CCutscene::Load()	// FUNCTION: 0x0043ed80
{
	char	name[4096];
	char	error[256];
	ULONG	version;
	ULONG	file_version;

	LOG.AddMessage("LOADING cutscene !!!");

	sprintf(name, "data\\cutscenes\\%s.cut", mFileName);
	version = MKID("1.00");

#line 203
	CChunkReader	*reader = new(MEMTYPE_CHUNKER) CChunkReader();
	if (!reader->Open(name))
		sprintf(error, "Error loading cutscene file %s!", mFileName);

	strcpy(mEndFade, "Fade");
	mEndFadeUnknown = 5;

	ULONG	chunk;
	while (chunk = reader->GetNext())
	{
		if (chunk == MKID("CUTE"))
		{
			reader->Read(&file_version, 4, 1);
			if (version != MKID("1.00"))	// retail compares the constant it set above, not file_version: the warning never fires
				TRACE("Warning! Unknown cutscene file version!\n");
		}
		else if (chunk == MKID("CPOS"))
		{
			reader->Read(&mPos, sizeof(mPos), 1);
			reader->Read(&mLength, 4, 1);
		}
		else if (chunk == MKID("TRKS"))
		{
			reader->Read(&mNumTracks, 4, 1);
		}
		else if (chunk == MKID("TDAT"))
		{
			SINT	track;
			SINT	unused;
			reader->Read(&track, 4, 1);
			reader->Read(mTrackName[track], 32, 1);
			reader->Read(&mTrackType[track], 4, 1);
			reader->Read(&mTrackLanguage[track], 4, 1);
			reader->Read(&unused, 4, 1);
		}
		else if (chunk == MKID("SDAT"))
		{
			SINT	track;
			reader->Read(&track, 4, 1);
			CCutsceneAnimNode	*node = AddAnimation(track, "", "", 0, 0);
			reader->Read(node->mAnimName, 32, 1);
			reader->Read(node->mFade, 256, 1);
			reader->Read(&node->mUnknown230, 4, 1);
			reader->Read(node->mName, 256, 1);
			reader->Read(&node->mFlags, 4, 1);
			reader->Read(&node->mLength, 4, 1);
			reader->Read(&node->mStart, 4, 1);
			reader->Read(&node->mOffset, 4, 1);
		}
		else if (chunk == MKID("EFAD"))
		{
			reader->Read(mEndFade, 256, 1);
			reader->Read(&mEndFadeUnknown, 4, 1);
		}
		else
		{
			TRACE("Warning! Unknown chunk found in cutscene file!\n");
			reader->Skip();
		}
	}

	reader->Close();
	delete reader;

	mChanged = TRUE;
	return TRUE;
}

//******************************************************************************************
CCutsceneAnimNode *CCutscene::AddAnimation(SINT track, char *name, char *anim, SINT start, SINT length)	// FUNCTION: 0x0043f210
{
#line 307
	CCutsceneAnimNode	*node = new(MEMTYPE_CUTSCENE) CCutsceneAnimNode;

	strcpy(node->mName, name);
	strcpy(node->mAnimName, anim);
	strcpy(node->mFade, "");
	node->mUnknown230 = 0;
	node->mStart = start;
	node->mLength = length;
	node->mNext = NULL;
	node->mIndex = -1;
	node->mShared = FALSE;
	node->mTrack = track;
	node->mSample = NULL;
	node->mOffset = 0;
	node->mFlags = 0;

	CCutsceneAnimNode	*last = mTracks[track];
	if (last)
	{
		while (last->mNext)
			last = last->mNext;
		last->mNext = node;
	}
	else
		mTracks[track] = node;

	mChanged = TRUE;
	mNumAnims[track]++;
	return node;
}

//******************************************************************************************
void CCutscene::Start()	// FUNCTION: 0x0043f340
{
	if (mPlaying)
		return;

	mLastTime = -1;
	mTime = 0;
	mPlaying = TRUE;
	mCameraSet = FALSE;
	sCurrent = this;

	mLinkTo.SetReader(WORLD.FindThingByName(mLinkToName));

	if (mTracks[0] && strlen(mTracks[0]->mFade))
	{
		if (!stricmp(mTracks[0]->mFade, "Fade"))
			ENGINE.SetBlurAlpha(255, false);
		else
			HUD.SetHudComponent(mTracks[0]->mFade, false);
	}

	EVENT_MANAGER.AddEvent(CUTSCENE_UPDATE, this, NEXT_FRAME);
}

//******************************************************************************************
void CCutscene::Stop()	// FUNCTION: 0x0043f420
{
	if (!mPlaying)
		return;

	if (mCameraSet)
	{
		GAME.SetCamera(0, mOldCamera);
		if (mMovieCamera)
			delete mMovieCamera;
		ENGINE.ResetPos((int)mOldCamera->GetPos().X, (int)mOldCamera->GetPos().Y);
		mFlags |= TF_DONT_RENDER;
		mCameraSet = FALSE;
	}

	mPlaying = FALSE;
	sCurrent = NULL;

	if (strlen(mEndFade))
	{
		if (!stricmp(mEndFade, "Fade"))
			ENGINE.SetBlurAlpha(255, true);
		else
			HUD.SetHudComponent(mEndFade, true);
	}
}

//******************************************************************************************
void CCutscene::InitRenderThing(CInitThing *init)	// FUNCTION: 0x0043f510
{
	CCutsceneAnimNode	*node = mTracks[0];

	mInitRenderThing.mThing = this;
	mInitRenderThing.mNumExtra = 0;

	// number the distinct animation names
	while (node)
	{
		node->mIndex = -1;
		for (CCutsceneAnimNode *other = mTracks[0]; other != node; other = other->mNext)
		{
			if (!stricmp(node->mName, other->mName))
			{
				node->mIndex = other->mIndex;
				node->mShared = TRUE;
			}
		}
		if (node->mIndex == -1)
		{
			node->mShared = FALSE;
			node->mIndex = mInitRenderThing.mNumExtra;
			mInitRenderThing.mNumExtra++;
		}
		node = node->mNext;
	}

#line 491
	mInitRenderThing.mExtra = new(MEMTYPE_CUTSCENE) char *[mInitRenderThing.mNumExtra];

	SINT	n = 0;
	node = mTracks[0];
	while (node)
	{
		if (!node->mShared)
		{
#line 501
			mInitRenderThing.mExtra[n] = new(MEMTYPE_CUTSCENE) char[256];
			strcpy(mInitRenderThing.mExtra[n], node->mName);
			n++;
		}
		node = node->mNext;
	}

	mRenderThing = CreateRenderThing(5);
	if (mRenderThing)
		mRenderThing->Init(&mInitRenderThing);
}

//******************************************************************************************
float CCutscene::GetRenderFrame()	// FUNCTION: 0x0043f680
{
	return mFrame;
}

//******************************************************************************************
void CCutscene::Update()	// FUNCTION: 0x0043f690
{
	if (!mPlaying)
		return;

	if (mLinkTo.ToRead())
		mPos = mLinkTo->GetPos();

	CRTCutscene	*rt = (CRTCutscene *)mRenderThing;
	if (mChanged)
	{
		rt->Unknown12();
		PrepareAnimations();
		mChanged = FALSE;
	}

	if (!mCameraSet)
	{
#line 549
		mMovieCamera = new(MEMTYPE_CUTSCENE) CMovieCamera(this);
		mOldCamera = GAME.GetCamera(0);
		GAME.SetCamera(0, mMovieCamera);
		mFlags &= ~TF_DONT_RENDER;
		mCameraSet = TRUE;
	}

	mTime++;
	if (mTime < 0)
		mTime = 0;
	if (mTime >= mLength)
		mTime = mLength - 1;

	SINT	time = mTime;
	CCutsceneAnimNode	*node;
	for (node = mTracks[0]; node; node = node->mNext)
	{
		if (node->mStart <= time && node->mStart + node->mLength - 1 >= time)
			break;
	}
	if (!node)
	{
		Stop();
		return;
	}

	mFrame = (float(time) - node->mStart + node->mOffset) / node->mAnimLength;
	rt->SetCurrentIndex(node->mIndex);
	EAnimMode	am = rt->GetRTMesh()->GetAnimModeByName(node->mAnimName);
	if (am == AM_NONE || am == -1)
	{
		char	text[256];
		sprintf(text, "Warning : Could not find animation %s in cutscene!", node->mAnimName);
		TRACE(text);
	}
	mAnimMode = am;
	GetRenderThingFrameIncrement(am, &mRealIndex);

	ENGINE.mRenderLandscape = (node->mFlags & 1) != 1;

	if (mTime != mLastTime)
	{
		for (SINT t = 0; t < mNumTracks; t++)
		{
			if (mTrackType[t] == CTT_AUDIO && (mTrackLanguage[t] == -1 || mTrackLanguage[t] == mLanguage))
			{
				time = mTime;
				CCutsceneAnimNode	*snode;
				for (snode = mTracks[t]; snode; snode = snode->mNext)
				{
					if (snode->mStart <= time && snode->mStart + snode->mLength - 1 >= time)
						break;
				}
				if (snode)
				{
					float	offset = float(snode->mOffset);
					float	from = (float(time) - snode->mStart + offset) / 20.0f;
					float	to = (offset + snode->mLength) / 20.0f;
					if (to > from)
						SOUND.PlaySample(snode->mSample, NULL, 0.7f, ST_NOTRACKING, true, 0, from, to, false, 1.0f, false, false, ST_GAME_SOUND);
				}
			}
		}
	}
	else
		SOUND.KillAllSamples();

	if (mTime == node->mStart && strlen(node->mFade) && mTime == 0)	// retail tests mTime == 0, so the mTime != 0 below is always false
	{
		if (!stricmp(node->mFade, "Fade"))
			ENGINE.SetBlurAlpha(255, mTime != 0);
		else
			HUD.SetHudComponent(node->mFade, mTime != 0);
	}

	mLastTime = mTime;
	EVENT_MANAGER.AddEvent(CUTSCENE_UPDATE, this, NEXT_FRAME);
}

//******************************************************************************************
void CCutscene::PrepareAnimations()	// FUNCTION: 0x0043fa70
{
	SINT	start = 0;
	SINT	total = 0;
	CRTCutscene	*rt = (CRTCutscene *)mRenderThing;
	CCutsceneAnimNode	*node = mTracks[0];

	for (SINT n = 0; n < mNumAnims[0]; n++)
	{
		if (node->mIndex == -1)
		{
			for (SINT i = 0; i < mInitRenderThing.mNumExtra; i++)
			{
				if (!stricmp(node->mName, mInitRenderThing.mExtra[i]))
				{
					node->mIndex = i;
					node->mShared = TRUE;
				}
			}
		}

		rt->SetCurrentIndex(node->mIndex);
		EAnimMode	am = rt->GetRTMesh()->GetAnimModeByName(node->mAnimName);
		if (am != AM_NONE && am != -1)
		{
			CAnimMode	*anim = rt->GetRTMesh()->GetAnimModePtr(am);
			if (node->mLength == -1)
				node->mLength = anim->mLast - anim->mStart;
			node->mAnimLength = anim->mLast - anim->mStart;
			if (node->mStart == -1)
				node->mStart = start;
			start += node->mLength;
			total += node->mLength;
		}
		node = node->mNext;
	}

	if (mLength == -1)
		mLength = total;
	if (mLength < 1)
		mLength = 1;

	for (SINT t = 0; t < mNumTracks; t++)
	{
		if (mTrackType[t] == CTT_AUDIO)
		{
			for (node = mTracks[t]; node; node = node->mNext)
			{
				char	name[256];
				if (mTrackLanguage[t] == -1)
					sprintf(name, "Cutscenes\\Music\\%s\\%s", mFileName, node->mName);
				else
					sprintf(name, "Cutscenes\\%s\\%s\\%s", CUTSCENE_LANGUAGES[mTrackLanguage[t]], mFileName, node->mName);

				node->mSample = SOUND.GetSample(name);
				if (!node->mSample)
					node->mAnimLength = 10;
				else
					node->mAnimLength = SINT(SOUND.GetSampleLength(node->mSample) * GAME_FR);
			}
		}
	}
}

//******************************************************************************************
void CCutscene::HandleEvent(CEvent *event)	// FUNCTION: 0x0043fcb0
{
	switch (event->GetEventNum())
	{
	case CUTSCENE_UPDATE:
		Update();
		break;
	default:
		SUPERTYPE::HandleEvent(event);
	}
}

//******************************************************************************************
void CCutscene::SkipCutscene()	// FUNCTION: 0x0043fcd0
{
	if (sCurrent)
		sCurrent->Stop();
}
