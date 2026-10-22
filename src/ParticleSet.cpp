// ParticleSet.cpp: reconstructed from ParticleSet.obj (004cbed0..004cdc2f); Stuart's release has no copy. The
// allocations' __LINE__ values are the retail ones (#line: the retail layout is unknown).
#include "common.h"
#include "ParticleManager.h"	// FUNCTION: 0x004cbed0 init:ZERO_FVECTOR
#include "mathsconst.h"			// FUNCTION: 0x004cbef0 init:ID_FMATRIX
#include "ParticleDescriptor.h"
#include "membuffer.h"
#include "Platform.h"

static CParticleDescriptor	**sLink;			// where Create links the next descriptor (0082b3f8)
// FUNCTION: 0x004cbfc0 init:PARTICLE_SET
// FUNCTION: 0x004cbfe0 exit:PARTICLE_SET
CParticleSet				PARTICLE_SET;
static CParticleDescriptor	*sLast;				// the last one created (0082b450)

//******************************************************************************************
void CParticleSet::DestroyList()	// FUNCTION: 0x004cbff0
{
	while (mList)
	{
		CParticleDescriptor	*next = mList->mNext;
		delete mList;
		mList = next;
	}
}

//******************************************************************************************
// A new descriptor of that type, linked in name order; with append, straight after the last one when its name
// sorts after it. Retail leaves the result unset for an unknown type.
// Scalar CFloatRef assignments reproduce retail's constructor store/load order; original header wording is unknown.
// STAND-IN: consumed snapshots of type, append and name, plus accessors
// for real name/next fields, reproduce the retail inline choices without unused padding. All three snapshots are used
// and all five property sites compute values or addresses used by the original operations. Nothing emits extra code.
// Stock-equivalent logging: own size 952, budget 1904; the first base-ctor share is 57 (<62), Timeline's 43 (<49),
// Function's 205 (>=202); Mesh and FoR have 3 left and stay called. Header correction with direct fields/no snapshots:
// 1,319 differing positions; NameOf alone: 234. Whole section 2,096 bytes, all 93 relocations match retail. The source
// spelling/ownership is unknown; replace these source concessions only when a supported form retains the full match.
static inline char *NameOf(CParticleDescriptor *pd) { return pd->mName; }

static inline CParticleDescriptor *NextOf(CParticleDescriptor*pd){return pd->mNext;}
static inline CParticleDescriptor **NextLinkOf(CParticleDescriptor*pd){return &pd->mNext;}
CParticleDescriptor *CParticleSet::Create(char *name, SINT type, BOOL append)	// FUNCTION: 0x004cc020
{
	SINT descriptor_type=type;
	BOOL requested_append=append;
	char* requested_name=name;
	if (requested_append && sLast && !NextOf(sLast) && stricmp(sLast->mName, requested_name) < 0)
		sLink = NextLinkOf(sLast);
	else if (FindByName(requested_name))
		return NULL;

	CParticleDescriptor	*pd;
	switch (descriptor_type)
	{
#line 95
	case PD_SIMPLE_SPRITE:	pd = new(MEMTYPE_PARTICLE) CPDSimpleSprite;	break;
	case PD_EMITTER:		pd = new(MEMTYPE_PARTICLE) CPDEmitter;		break;
	case PD_MODIFIER:		pd = new(MEMTYPE_PARTICLE) CPDModifier;		break;
	case PD_SELECTOR:		pd = new(MEMTYPE_PARTICLE) CPDSelector;		break;
	case PD_COLOUR_RANGE:	pd = new(MEMTYPE_PARTICLE) CPDColourRange;	break;
	case PD_TIMELINE:		pd = new(MEMTYPE_PARTICLE) CPDTimeline;		break;
	case PD_SHAPE:			pd = new(MEMTYPE_PARTICLE) CPDShape;		break;
	case PD_TRAIL:			pd = new(MEMTYPE_PARTICLE) CPDTrail;		break;
	case PD_MOVER:			pd = new(MEMTYPE_PARTICLE) CPDMover;		break;
	case PD_FUNCTION:		pd = new(MEMTYPE_PARTICLE) CPDFunction;		break;
	case PD_MESH:			pd = new(MEMTYPE_PARTICLE) CPDMesh;			break;
	case PD_FOR:			pd = new(MEMTYPE_PARTICLE) CPDFoR;			break;
	case PD_PMESH:			pd = new(MEMTYPE_PARTICLE) CPDPMesh;		break;
	}

	strncpy(NameOf(pd), requested_name, 49);
	NameOf(pd)[49] = 0;
	CParticleDescriptor	**link = sLink;				// read once: retail links through one register (004cc7e9..004cc7fa)
	*NextLinkOf(pd) = *link;
	*link = pd;
	sLast = pd;
	return pd;
}

// CParticleDescriptor's constructor is inline in its class: retail emits its copy after Create, at the head of the
// batch of inline functions Create needs out of line (the descriptor classes' vtables follow it).
// FUNCTION: 0x004cc850 ??0CParticleDescriptor@@QAE@XZ

//******************************************************************************************
CPDMesh::CPDMesh()	// FUNCTION: 0x004cd290
{
	mMesh = NULL;
	mLife = 100;
	mGravity = 0;
	mBounce = 0;
	mAngularMomentum = 0;
	mTransmitFoR = 0;
	mAutoCentre = 1;
	mImpactSpawnee = NULL;
}

//******************************************************************************************
CPDFoR::CPDFoR()	// FUNCTION: 0x004cd2d0
{
	mInitial = NULL;
	mDeath = NULL;
	mMover = NULL;
}

//******************************************************************************************
CPDPMesh::CPDPMesh()	// FUNCTION: 0x004cd3c0
{
	mType = 0;
	mLife = 100;
	mTexture = NULL;
	mBlendMode = 0;
	mColourRange = NULL;
	mColourRange2 = NULL;
	mTileU = 1.0f;
	mTileV = 1.0f;
	mScrollU = 0;
	mScrollV = 0;
	mCylinderNumPtsAxial = 2;
	mCylinderNumPtsRadial = 10;
	mCylinderRadius = 0.5f;
	mCylinderRadius2 = 1.0f;
	mCylinderLength = 5.0f;
	mSphereNumPtsAx = 10;
	mSphereNumPtsRad = 10;
	mSphereLatitudeStart = 0;
	mSphereLatitudeEnd = 180.0f;
	mSphereLongitudeStart = 0;
	mSphereLongitudeEnd = 360.0f;
	mSphereRadiusTime = 1.0f;
}

//******************************************************************************************
CParticleDescriptor *CParticleSet::FindByName(char *name)	// FUNCTION: 0x004cd7a0
{
	sLink = &mList;
	CParticleDescriptor	*pd;
	for (pd = mList; pd; pd = *sLink)
	{
		SINT	c = stricmp(name, pd->mName);
		if (c == 0)
			break;
		if (c < 0)
			return NULL;
		sLink = &pd->mNext;
	}
	return pd;
}

//******************************************************************************************
// The set file's buffer. Retail's unwind for it (005d4236) calls its own destructor, 004cdb90, a lone jmp to
// ~CDXMemBuffer; every other memory buffer's unwind jumps to ~CDXMemBuffer itself. So this is a class of its
// own with an implicit destructor (the name is ours).
class CParticleSetFile : public CDXMemBuffer
{
};
// FUNCTION: 0x004cdb90 ??1CParticleSetFile@@QAE@XZ

BOOL CParticleSet::ReadData(CDXMemBuffer *file)	// FUNCTION: 0x004cd7f0
{
	sLast = NULL;
	float	start = PLATFORM.GetSysTimeFloat();
	DestroyList();

#line 315
	CTokenArchive	*archive = new(MEMTYPE_SCRATCHPAD) CTokenArchive(file);

	SINT	token;
	if (!archive->ReadToken(&token, NULL, NULL, NULL) && token != PST_FILE_HEADER)
	{
		delete archive;
		return FALSE;
	}
	float	version;
	if (!archive->ReadToken(&token, NULL, &version, NULL) && token != PST_FILE_VERSION)
	{
		delete archive;
		return FALSE;
	}
	SINT	count;
	if (!archive->ReadToken(&token, &count, NULL, NULL) && token != PST_NUM_PARTICLE_DESCRIPTORS)
	{
		delete archive;
		return FALSE;
	}

	for (SINT i = 0; i < count; i++)
	{
#line 348
		char	*name = new(MEMTYPE_SCRATCHPAD) char[1000];
		SINT	type;
		if ((!archive->ReadToken(&token, &type, NULL, NULL) && token != PST_PARTICLE_DESCRIPTOR_TYPE) ||
			(!archive->ReadToken(&token, NULL, NULL, name) && token != PST_PARTICLE_DESCRIPTOR_NAME))
		{
			delete archive;
			delete [] name;
			return FALSE;
		}
		CParticleDescriptor	*pd = Create(name, type, TRUE);
		if (!pd || !pd->Load(archive))
		{
			delete archive;
			delete [] name;
			return FALSE;
		}
		delete [] name;
	}

	float	relocate = PLATFORM.GetSysTimeFloat();
	archive->HandleRelocation(&mList);
	char	text[200];
	sprintf(text, "CTokenArchive::HandleRelocation took %f seconds\nCParticleSet ::ReadData         took %f seconds\n",
			PLATFORM.GetSysTimeFloat() - relocate, PLATFORM.GetSysTimeFloat() - start);
	TRACE(text);
	delete archive;
	return TRUE;
}

//******************************************************************************************
BOOL CParticleSet::LoadAllFromDisk(EParticleSet set)	// FUNCTION: 0x004cda60
{
	DestroyList();

#line 437
	char	*filename = new(MEMTYPE_SCRATCHPAD) char[200];
	switch (set)
	{
	case PARTSET_INGAME:
		strcpy(filename, "data\\ParticleSets\\MainSet.par");
		break;
	case PARTSET_UNKNOWN2:
		strcpy(filename, "data\\ParticleSets\\MainSet.par");
		break;
	default:
		strcpy(filename, "data\\ParticleSets\\Frontend.par");
		break;
	}

	CParticleSetFile	mem;
	if (mem.OpenFile(filename))
	{
		ReadData(&mem);
		mem.CloseFile();
	}
	delete [] filename;
	return TRUE;
}

//******************************************************************************************
void CParticleSet::LinkNode(SParticleSetNode *node)	// FUNCTION: 0x004cdba0
{
	if (!mLastNode)
	{
		mLastNode = node;
		mFirstNode = node;
		node->mPrev = NULL;
		node->mNext = NULL;
	}
	else
	{
		mLastNode->mNext = node;
		node->mPrev = mLastNode;
		mLastNode = node;
	}
}

//******************************************************************************************
void CParticleSet::UnlinkNode(SParticleSetNode *node)	// FUNCTION: 0x004cdbe0
{
	if (node->mPrev == NULL)
		mFirstNode = node->mNext;
	else
		node->mPrev->mNext = node->mNext;
	if (node->mNext == NULL)
		mLastNode = node->mPrev;
	else
		node->mNext->mPrev = node->mPrev;
	node->mPrev = NULL;
	node->mNext = NULL;
}
// FUNCTION: 0x004cc880 ?GetTypeName@CPDSimpleSprite@@UAEPADXZ
// FUNCTION: 0x004cc890 ?GetTag@CPDSimpleSprite@@UAEPADXZ
// FUNCTION: 0x004cc8a0 ?RemoveReference@CPDSimpleSprite@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004cc8e0 ?GetReference@CPDSimpleSprite@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004cc960 ?GetTypeName@CPDEmitter@@UAEPADXZ
// FUNCTION: 0x004cc970 ?GetTag@CPDEmitter@@UAEPADXZ
// FUNCTION: 0x004cc980 ?RemoveReference@CPDEmitter@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004cca10 ?GetReference@CPDEmitter@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004ccb20 ?Unknown8@CPDEmitter@@UAEXPAX@Z
// FUNCTION: 0x004ccb40 ??_GCPDMover@@UAEPAXI@Z
// FUNCTION: 0x004ccb60 ?GetTypeName@CPDModifier@@UAEPADXZ
// FUNCTION: 0x004ccb70 ?GetTag@CPDModifier@@UAEPADXZ
// FUNCTION: 0x004ccb80 ?GetTypeName@CPDSelector@@UAEPADXZ
// FUNCTION: 0x004ccb90 ?GetTag@CPDSelector@@UAEPADXZ
// FUNCTION: 0x004ccba0 ?RemoveReference@CPDSelector@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004ccbd0 ?GetReference@CPDSelector@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004ccc50 ?Unknown8@CPDSelector@@UAEXPAX@Z
// FUNCTION: 0x004ccc80 ?GetTypeName@CPDColourRange@@UAEPADXZ
// FUNCTION: 0x004ccc90 ?GetTag@CPDColourRange@@UAEPADXZ
// FUNCTION: 0x004ccca0 ?RemoveReference@CPDColourRange@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004ccd20 ?GetReference@CPDColourRange@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004cce40 ?GetTypeName@CPDTimeline@@UAEPADXZ
// FUNCTION: 0x004cce50 ?GetTag@CPDTimeline@@UAEPADXZ
// FUNCTION: 0x004cce60 ?RemoveReference@CPDTimeline@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004cce90 ?GetReference@CPDTimeline@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004ccef0 ?Unknown8@CPDTimeline@@UAEXPAX@Z
// FUNCTION: 0x004ccf30 ?GetTypeName@CPDShape@@UAEPADXZ
// FUNCTION: 0x004ccf40 ?GetTag@CPDShape@@UAEPADXZ
// FUNCTION: 0x004ccf50 ?RemoveReference@CPDShape@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004ccf70 ?GetReference@CPDShape@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004ccfc0 ?GetTypeName@CPDTrail@@UAEPADXZ
// FUNCTION: 0x004ccfd0 ?GetTag@CPDTrail@@UAEPADXZ
// FUNCTION: 0x004ccfe0 ?RemoveReference@CPDTrail@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004cd030 ?GetReference@CPDTrail@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004cd0c0 ?GetTypeName@CPDMover@@UAEPADXZ
// FUNCTION: 0x004cd0d0 ?GetTag@CPDMover@@UAEPADXZ
// FUNCTION: 0x004cd0e0 ?RemoveReference@CPDMover@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004cd110 ?GetReference@CPDMover@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004cd190 ?GetTypeName@CPDFunction@@UAEPADXZ
// FUNCTION: 0x004cd1a0 ?GetTag@CPDFunction@@UAEPADXZ
// FUNCTION: 0x004cd1b0 ?RemoveReference@CPDFunction@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004cd1f0 ?GetReference@CPDFunction@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004cd300 ?GetTypeName@CPDFoR@@UAEPADXZ
// FUNCTION: 0x004cd310 ?GetTag@CPDFoR@@UAEPADXZ
// FUNCTION: 0x004cd320 ?RemoveReference@CPDFoR@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004cd350 ?GetReference@CPDFoR@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004cd540 ?GetTypeName@CPDPMesh@@UAEPADXZ
// FUNCTION: 0x004cd550 ?GetTag@CPDPMesh@@UAEPADXZ
// FUNCTION: 0x004cd560 ?RemoveReference@CPDPMesh@@UAEXPAVCParticleDescriptor@@@Z
// FUNCTION: 0x004cd630 ?GetReference@CPDPMesh@@UAEPAVCParticleDescriptor@@HH@Z
// FUNCTION: 0x004cc870 ??1CPDMover@@UAE@XZ
// FUNCTION: 0x004cce20 ??0CPDTimelineEntry@@QAE@XZ
#define PARTICLESET_CPP
//******************************************************************************************
// STAND-IN (layout): the descriptors' per-particle steps, defined inline HERE rather than in their class bodies.
// Retail links them as INLINE functions (the inline survivor 0x004014c0; the out-of-line twin of the same `ret 4` body
// is 0x00452b60): VC6's /OPT:ICF folds the two classes separately (stage-3 link research, link order notes
// 3b/3c). An inline virtual is emitted where its class's table is, and only this object emits the descriptors' tables
// (Create's constructors), so retail's own definitions were very likely in ParticleDescriptor.h. That form loses
// CPDTimeline 004c2640 (4 bytes; measured 2026-10-15 on bdebcc70 + this batch). Move them back into the classes once
// the witness's real header context is known.

// FUNCTION: 0x004014f0 ?Unknown17@CPDShape@@UAEHXZ
inline SINT CPDShape::Unknown17()
{
	return mNumParticles;
}

// FUNCTION: 0x004014c0 ?Unknown9@CPDColourRange@@UAEXPAVCParticle@@@Z
inline void CPDColourRange::Unknown9(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown9@CPDFunction@@UAEXPAVCParticle@@@Z
inline void CPDFunction::Unknown9(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown9@CPDModifier@@UAEXPAVCParticle@@@Z
inline void CPDModifier::Unknown9(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown9@CPDMover@@UAEXPAVCParticle@@@Z
inline void CPDMover::Unknown9(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown9@CPDSelector@@UAEXPAVCParticle@@@Z
inline void CPDSelector::Unknown9(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown9@CPDShape@@UAEXPAVCParticle@@@Z
inline void CPDShape::Unknown9(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDColourRange@@UAEXPAVCParticle@@@Z
inline void CPDColourRange::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDEmitter@@UAEXPAVCParticle@@@Z
inline void CPDEmitter::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDFoR@@UAEXPAVCParticle@@@Z
inline void CPDFoR::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDFunction@@UAEXPAVCParticle@@@Z
inline void CPDFunction::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDModifier@@UAEXPAVCParticle@@@Z
inline void CPDModifier::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDMover@@UAEXPAVCParticle@@@Z
inline void CPDMover::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDSelector@@UAEXPAVCParticle@@@Z
inline void CPDSelector::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDShape@@UAEXPAVCParticle@@@Z
inline void CPDShape::Unknown11(CParticle *p)
{
}

// FUNCTION: 0x004014c0 ?Unknown11@CPDTimeline@@UAEXPAVCParticle@@@Z
inline void CPDTimeline::Unknown11(CParticle *p)
{
}

// The object's last initializer, after its code (.CRT$XCU order).
// FUNCTION: 0x004cdc30 init:ZERO_FVECTOR2
#include "mathsconst2.h"
