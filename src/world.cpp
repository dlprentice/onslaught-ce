// world.cpp: reconstructed from world.obj (0050a750..0050d86f); Stuart's release has no copy. The allocations'
// __LINE__ values are the retail ones; the #line directives place them, because the retail file's layout is unknown.
#include "common.h"
#define WORLD_CPP
#include "MissionScript/vm.h"		// before world.h: the script entries' deletes use its virtual destructors
#include "world.h"
#include "thing.h"
#include "DebugLog.h"
#include "engine.h"
#include "console.h"
#include "text.h"
#include "membuffer.h"
#include "chunker.h"
#include "WorldMeshList.h"
#include "BattleEngineConfigurations.h"
#include "InfluenceMap.h"
#include "eventmanager.h"
#include "WaypointManager.h"
#include "Line.h"
#include "map.h"
#include "collisionseekingthing.h"
#include "InitThing.h"
#include "oids.h"
#include "feature.h"
#include "hazard.h"
#include "Career.h"
#include "game.h"
#include "SeedUniformRandomNumberStream.h"
#include "MemoryManager.h"

#include <string.h>
#include <stdio.h>

// What the world reaches outside its own files; declared here until their headers are reconstructed.
class C3DGamut
{
public:
	void	RenderThings(bool imposters, bool shadows, bool blob_shadows);	// 00476fe0
};

// FUNCTION: 0x0050b950 ??1CInfluenceMap@@UAE@XZ
// FUNCTION: 0x0050b930 ??_GCInfluenceMap@@UAEPAXI@Z
extern char				*INFLUENCE_BUFFER_A;	// 0067a07c
extern char				*INFLUENCE_BUFFER_B;	// 0067a078


#include "Occupancy.h"
#include "resourceaccumulator.h"

// FUNCTION: 0x0050a750 init:ZERO_FVECTOR
// FUNCTION: 0x0050a770 init:ID_FMATRIX
// FUNCTION: 0x0050a840 init:WORLD
// FUNCTION: 0x0050a860 exit:WORLD
CWorld	WORLD;

//******************************************************************************************
CWorld::~CWorld()	// FUNCTION: 0x0050a870
{
}

//******************************************************************************************
CWorld::CWorld()	// FUNCTION: 0x0050a9c0
{
	mOccupancy[0] = NULL;
	mOccupancy[1] = NULL;
	mOccupancy[2] = NULL;

	mWorldLevel = -1;
	mBaseWorldLevel = -1;
	mWorldResourcePos = -1;
	mBaseWorldResourcePos = -1;

	for (SINT i = 0; i < NUM_WORLD_COUNTERS; i++)
	{
		mCountersA[i] = 0;
		mCountersB[i] = 0;
	}

	for (SINT j = 0; j < MAX_WORLD_TEXT_SLOTS; j++)
		mTextState[j] = 0;
}

//******************************************************************************************
void CWorld::Render(C3DGamut *gamut, BOOL imposters, BOOL blob_shadows)	// FUNCTION: 0x0050ab60
{
	ENGINE.SetModulate2X(true);
	if (ENGINE.GetDrawDebugStuff() & 4)
		ENGINE.DisableLighting();
	gamut->RenderThings(TRUE, TRUE, blob_shadows != 0);
	ENGINE.SetModulate2X(true);
}

//******************************************************************************************
void CWorld::Shutdown()	// FUNCTION: 0x0050abb0
{
	Clear();
}

//******************************************************************************************
CMissionScriptObjectCode *CWorld::GetCopyOfScript(char *name)	// FUNCTION: 0x0050abc0
{
	CScriptObjectCodeEntry	*entry;
	for (entry = mScripts.First(); entry; entry = mScripts.Next())
	{
		char	*script_name = entry->mName->GetString();
		if (strcmp(script_name, name) == 0)
			return entry->mCode->Clone();
	}

	LOG.AddMessage("FATAL ERROR: Cant find script '%s'", name);
	return NULL;
}

//******************************************************************************************
void CWorld::LoadScripts(CDXMemBuffer *file)	// FUNCTION: 0x0050ac70
{
	SINT	count;
	file->Read(&count, 4);
	for (SINT i = 0; i < count; i++)
	{
#line 192
		CStringDataType				*name = new(MT_SCRIPT) CStringDataType(file);


		CMissionScriptObjectCode	*code = new(MT_SCRIPT) CMissionScriptObjectCode(file);

		mScripts.Append(new(MT_SCRIPT) CScriptObjectCodeEntry(name, code));

		char	pad[10];
		file->Read(pad, 10);
	}
}

//******************************************************************************************
// STAND-IN: Clear's teardown helpers. Retail calls the grids' scalar deleting destructor (0050d680) and the scripts'
// DeleteAll (0050d7f0) out of line from Clear; VC6 only denies inline expansions below the function's own top level, so
// both sat one call deeper in retail's source. Written out in Clear: without DeleteOccupancyGrids it is 39/63/352 (shape /
// instructions / bytes) and 0050d680 is no longer emitted, without ClearScripts 21/23/283 (0050d7f0 lost), without both
// 45/75/453. The helpers' names and bodies are ours.
static inline void DeleteOccupancyGrids(CWorld *w);

static inline void ClearScripts(CWorld *w)
{
	w->mScripts.DeleteAll();
	w->mObjectiveThings.RemoveAll();
	w->mSet110.RemoveAll();
}

//******************************************************************************************
void CWorld::Clear()	// FUNCTION: 0x0050ada0
{
	delete INFLUENCEMAP;
	INFLUENCEMAP = NULL;

	DeleteOccupancyGrids(this);

	while (GetThingNB().First())
	{
		CThing	*thing = GetThingNB().First();
		thing->Shutdown();
		if (thing == GetThingNB().First())
			GetThingNB().Remove(thing);
	}

	ClearScripts(this);

	CActiveReader<CThing>	*reader;
	while (reader = mBaseWorldThingNB.First())
	{
		mBaseWorldThingNB.Remove(reader);
		delete reader;
	}
	GetThingNB().RemoveAll();

	CMonitor	*m;
	while (m = mOwnedF0.First())
		delete m;
	while (m = mOwned100.First())
		delete m;

	CWaypointManager::DeleteAll();
	UBattleEngineConfigurations::ShutDown();
	CWorldMeshList::Clear();

	delete [] INFLUENCE_BUFFER_A;
	INFLUENCE_BUFFER_A = NULL;
	delete [] INFLUENCE_BUFFER_B;
	INFLUENCE_BUFFER_B = NULL;

	for (SINT i = 0; i < NUM_WORLD_COUNTERS; i++)
	{
		mCountersA[i] = 0;
		mCountersB[i] = 0;
	}

	mBigThings.RemoveAll();
	mSet50.RemoveAll();
	mSet30.RemoveAll();
	mSet20.RemoveAll();

	for (SINT j = 0; j < MAX_WORLD_TEXT_SLOTS; j++)
		mTextState[j] = 0;
}

//******************************************************************************************
CComplexThing *CWorld::FindThingByName(char *name)	// FUNCTION: 0x0050af70
{
	CComplexThing	*thing;
	for (thing = mNamedThings.First(); thing; thing = mNamedThings.Next())
		if (strcmp(name, thing->GetName()) == 0)
			return thing;
	return NULL;
}

//******************************************************************************************
void CWorld::AddToOccupancy(CUnit *unit)	// FUNCTION: 0x0050b010
{
	COccupancy::AddUnit(unit);
}

//******************************************************************************************
void CWorld::RemoveFromOccupancy(CUnit *unit)	// FUNCTION: 0x0050b020
{
	COccupancy::RemoveUnit(unit);
}

// STAND-IN accessors for FindFirstThingToHitLine (below): they spend its inline budget; see its comment.
static inline FVector &LineStart(CLine &l) { return l.mStart; }
static inline FVector &LineEnd(CLine &l) { return l.mEnd; }
static inline ULONG SeekerFlags(CCSPersistentThing *s) { return s->mCollisionFlags; }
static inline void SetNumContacts(CCollisionReport &r, SINT n) { r.mNumContacts = n; }

// STAND-IN, two concessions; neither emits code of its own.
// 1. The four file-local accessors above (line start x4, line end, the seeker's flags x2, the contact count) and Stuart's
//    GetCollisionMeshVolume() at both reads are ten inline sites after the report's constructor. They bring its nested
//    budget from 67 to 48, under the 49 that its normals' `vector constructor iterator' needs, so the iterator is called
//    out of line (0050b07a) and a thing that cannot be sought steps to the next entry itself (0050b209), as in retail.
//    Which accessors Stuart's source spent the budget on is not known. With plain member reads: 59/159.
// 2. The brackets around GetBoundingRadius() add one no-code x87 node between the call and the subtraction, so
//    hitGround's reload is the first node ready after the call: it is issued before the fsubr and stays a register load
//    (0050b2ea) instead of folding into the compare. Without them: 9/9. A named radius float gives the same node, but
//    its own size (849) is over the 846 that keeps the nested budget above at 48.
// Natural forms that matter as well: pos0 and tp as const references to thing->GetPos() give retail's operand order in
// the six position sums. own and incoming live in their own block: C2's alias analysis keeps a pointer obtained outside
// a local's scope (volume) from aliasing it, so the stores into them are scheduled among the position sums, as retail's
// are (0050b380, 0050b395). The report's constructor already zeroes mUnknown00, so it is not stored again here.
// FUNCTION: 0x0050b030
ECollisionType CWorld::FindFirstThingToHitLine(CLine line, CThing *ignore,
		CWorldLineColReport *out, BOOL firstOnly, BOOL meshCollision, int level,
		ULONG ignoreTypes, BOOL mapFlag, int allowedTypes)
{
	out->mHitType = kCollideNothing;
	CCollisionReport report;
	SetNumContacts(report, 0);
	report.mUnknownA8 = FALSE;
	report.mStuck = FALSE;
	report.mTestTriangles = 0;
	report.mUnknownD0 = 0;
	SINT part = -1;
	float groundDistance = 99999.0f;
	FVector ground;
	BOOL hitGround = MAP.Unknown490a40(&line, &ground, mapFlag);
	if (hitGround)
	{
		groundDistance = (LineStart(line) - ground).Magnitude();
		out->mHitType = kCollideGround;
		out->mDistToImpact = groundDistance;
	}
	float nearest = groundDistance;
	CThing *hit = NULL;
	CMapWhoEntry *entry = MAP_WHO.GetFirstEntryWithinLine(LineStart(line), LineEnd(line));
	while (entry)
	{
		CThing *thing = entry->GetThing();
		if (thing != ignore && !(thing->GetFlags() & TF_INVISIBLE) &&
			!(thing->GetThingType() & ignoreTypes) && (thing->GetThingType() & allowedTypes))
		{
			CCSPersistentThing *seeker = thing->GetCSPT();
			if (seeker && (SeekerFlags(seeker) & 0xc0))
			{
				// Retail advances here itself and goes straight to the loop test (0050b204..0050b215).
				if (thing->IsDying() && !thing->IsA(THING_TYPE_BUILDING))
				{
					entry = MAP_WHO.GetNextEntryWithinLine();
					continue;
				}
				const FVector &pos0 = thing->GetPos();
				FVector position = seeker->Unknown7()->mPos + pos0;
				if (seeker->Unknown7()->TestAtOffset(&line, -position, &report))
				{
					float distance = (position - LineStart(line)).Magnitude() - (thing->GetBoundingRadius());
					if ((!hitGround || distance < groundDistance) && (!hit || distance < nearest))
					{
						report.mContactParts[0] = -1;
						BOOL accept = TRUE;
						if ((level == 2 || ((SeekerFlags(seeker) & 0xc) == 8 && meshCollision == TRUE)) &&
							seeker->GetCollisionMeshVolume())
						{
							CGeneralVolume *volume = seeker->GetCollisionMeshVolume();
							const FVector &tp = thing->GetPos();
							FVector meshPosition = volume->mPos + tp;
							FVector zero(0, 0, 0);
							{
								CCollisionMove own;
								own.mStopped = FALSE;
								own.mPos = meshPosition;
								CCollisionMove incoming;
								incoming.mStopped = FALSE;
								incoming.mPos = zero;
								accept = FALSE;
								if (volume->CollideWithLine(&own, &incoming, &line, &report))
								{
									distance = (report.mContactPos - LineStart(line)).Magnitude();
									if ((!hitGround || groundDistance >= distance) && distance <= nearest)
										accept = TRUE;
								}
							}
						}
						if (accept)
						{
							// Retail's early-out writes only the enum. Existing pointer/part/distance are left untouched.
							if (firstOnly)
							{
								out->mHitType = kCollideThing;
								return kCollideThing;
							}
							part = report.mContactParts[0];
							hit = thing;
							nearest = distance;
						}
					}
				}
			}
		}
		entry = MAP_WHO.GetNextEntryWithinLine();
	}
	if (hit)
	{
		out->mHitThing = hit;
		out->mUnknown04 = part;
		out->mHitType = kCollideThing;
		out->mDistToImpact = nearest;
	}
	return out->mHitType;
}

//******************************************************************************************
BOOL CWorld::Load(SINT level, BOOL base)	// FUNCTION: 0x0050b520
{
	char	path[] = "data\\worlds\\";
	char	name[200];
	char	status[256];
	SINT	start = 0;

	sprintf(name, "%sworld%03d.wrd", path, level);

	if (base)
	{
		if ((mBaseWorldLevel == level) && (mBaseWorldResourcePos != -1))
		{
			TRACE("Loading base world from resource file...\n");
			strcpy(name, CResourceAccumulator::GetResourceFileName());
			start = mBaseWorldResourcePos;
		}
		else
		{
			mWorldResourcePos = -1;
			mBaseWorldResourcePos = -1;
		}
	}
	else
	{
		if ((mWorldLevel == level) && (mWorldResourcePos != -1))
		{
			TRACE("Loading world from resource file...\n");
			strcpy(name, CResourceAccumulator::GetResourceFileName());
			start = mWorldResourcePos;
		}
		else
		{
			mWorldResourcePos = -1;
			mBaseWorldResourcePos = -1;
		}
		sprintf(status, "Loading world %d", level);
		CONSOLE.Status(status);
	}

	if (base)
	{
		sprintf(status, "Loading base world %d", level);
		CONSOLE.Status(status);
		mBaseWorldLevel = level;
	}
	else
		mWorldLevel = level;

	CDXMemBuffer	mem;
	if (!mem.InitFromFile(name, MEMTYPE_MEMBUFFER, TRUE, start))
	{
		mem.Close();
		return FALSE;
	}

	BOOL	ok = LoadWorld(&mem, base, !base);
	CONSOLE.StatusDone(status, TRUE);
	mem.Close();
	return ok;
}

//******************************************************************************************
void CWorld::Deserialize(CChunkReader *c)	// FUNCTION: 0x0050b780
{
	CONSOLE.Status("Deserializing world");
	c->GetNext();
	c->GetNext();
	c->Read(&mBaseWorldLevel, 4, 1);
	c->Read(&mWorldLevel, 4, 1);
#line 644
	INFLUENCEMAP = new(MEMTYPE_INFLUENCEMAP) CInfluenceMap;
	INFLUENCE_BUFFER_A = new(MEMTYPE_UNKNOWN) char;
	INFLUENCE_BUFFER_B = new(MEMTYPE_UNKNOWN) char;
	if (mBaseWorldLevel != -1)
	{
		CONSOLE.Status("Deserializing base world position");
		c->GetNext();
		mBaseWorldResourcePos = c->WhereAmI();
		c->Skip();
		CONSOLE.StatusDone("Deserializing base world", TRUE);
	}
	CONSOLE.Status("Deserializing real world");
	c->GetNext();
	mWorldResourcePos = c->WhereAmI();
	c->Skip();
	CONSOLE.StatusDone("Deserializing real world position", TRUE);
	CONSOLE.StatusDone("Deserializing world", TRUE);
}

//******************************************************************************************
// A world file: the header, the scripts, the base world under it, the map and the meshes, then every thing, the trees,
// the influence map, the waypoints, the level's timings and the occupancy grids. The base world keeps only the things
// the career says survive; a thing the career has destroyed leaves a reader for its slot (and a building its craters).
// The version test reads a SINT copy: retail compares the zero-extended word signed (0050bacb..0050badc), where
// a test of the UWORD itself (or a cast) narrows to an unsigned word compare, as the later version tests do.
// Each occupancy branch tests the version for the grid header itself (retail repeats the compare in both arms,
// 0050d34e and 0050d380), and the scripts loop has a block of its own: in this exception-handling function every call
// that may throw counts as a use of the last block scope holding an address-taken local, so with the iterator outside a
// block start_time stayed live to every later call and could not join retail's slot class.
// STAND-IN, two parts (the accessor and the grid local):
// (a) The target test reads init->mTarget through the file-local accessor TargetOf at its three reads. Retail calls
//     GenericSPtrSet::Next out of line in the unit lookup (0050ca8b): the three free inline sites after the unit spawn
//     lower SpawnThing(name)'s nested budget from 149 to 139 (the inline logger), so GetUnitIndex's Next gets 48 of
//     the 50 it needs and stays a call; with plain member reads it gets 58 and is expanded.
// (b) The base-world arm of the occupancy block reads its first grid through a local set by a same-value ternary. The
//     two front-end definitions keep C2's forward substitution from folding the local into the call (the arms fold
//     afterwards), so the grid pointer is an allocator web of the arm's block: 6 counted nodes instead of 5 take the
//     version copy's colouring key from 20 to 24. That ties the unit loop's init (24), the copy wins the tie key
//     (0x80d against 0x6ed), is coloured first and takes esi, and base's piece is left without a register: base is
//     reloaded at the occupancy and start-time tests (0050d313, 0050d3a6) as in retail. The condition makes no
//     difference (it folds with the arms). A plain local, a reference, a local per grid, a NULL initialisation or a
//     self-copy after the version copy are substituted or split the block (2/14 left).
// Without (b): 2/14 (registers); without (a): 9/9 (the expanded Next). Which source Stuart wrote is unknown.
static inline SINT TargetOf(CInitThing *init) { return init->mTarget; }

BOOL CWorld::LoadWorld(CDXMemBuffer *file, BOOL base, BOOL real)	// FUNCTION: 0x0050b9c0
{
	SINT	base_level = -1;
	SINT	old_a = 1;
	SINT	old_b = 1;
	float	old_c = 0.5f;
	float	old_d = 0.5f;
	SINT	old_e = 0;
	SINT	map = -1;

	if (real)
	{
		SAFE_DELETE(INFLUENCEMAP);
		SAFE_DELETE(INFLUENCE_BUFFER_A);
		SAFE_DELETE(INFLUENCE_BUFFER_B);
		InitOccupancy();
	}

	// what each thing in the file became, and the world it dies on
	CThing		*things[1000];
	CInitThing	*inits[1000];
	SINT		dies_on[1000];
	memset(dies_on, 0xff, sizeof(dies_on));
	memset(inits, 0, sizeof(inits));
	memset(things, 0, sizeof(things));
	SINT	num = 0;

	UWORD	version;
	file->Read(&version, 2);
	SINT	v = version;
	if ((v < 43) || (v > 50))
		return FALSE;

	LoadHeader(file, base);

	if (real)
	{
		if (!INFLUENCEMAP)
		{
#line 757
			INFLUENCEMAP = new(MEMTYPE_INFLUENCEMAP) CInfluenceMap;
		}
		if (!INFLUENCE_BUFFER_A)
		{
#line 759
			INFLUENCE_BUFFER_A = new(MEMTYPE_UNKNOWN) char;
		}
		if (!INFLUENCE_BUFFER_B)
		{
#line 761
			INFLUENCE_BUFFER_B = new(MEMTYPE_UNKNOWN) char;
		}
	}

	file->Read(&base_level, 4);
	WORLD.LoadScripts(file);
	if ((!base) && (base_level != -1))
		Load(base_level, TRUE);

	file->Read(&map, 4);
	MAP.AlreadyLoaded(map, (base) || (base_level == -1), TRUE);
	ENGINE.LoadAllNamedMeshes(*file);

	UWORD	num_things;
	file->Read(&num_things, 2);
	CONSOLE.Status("Loading units");
	for (; num_things; num_things--)
	{
		SINT	type;
		file->Read(&type, 4);
		if ((type == OID_CUnit) || (type == OID_CBuilding))
		{
			// a unit: a squad if its behaviour wants one
			CUnitInitThing	init;
			init.Load(version, *file);
			char	len;
			file->Read(&len, 1);
			char	name[256];
			file->Read(name, len);
			name[len] = 0;
			file->Read(&dies_on[num], 4);
			init.mStats = UPhysicsManager::GetUnitData(name);
			if (init.mStats)
			{
				CSquad	*squad = UPhysicsManager::SpawnSquad(init.mStats->mBehaviour);
				if (squad)
				{
					CInitThing	*sinit = SpawnInitThing(OID_CSquad, FALSE);
					inits[num] = sinit;
					sinit->Copy(&init);
					things[num] = squad;
					strcpy(((CSquadInitThing *)sinit)->mUnitName, name);
					((CSquadInitThing *)sinit)->mAmount = 1;
					((CSquad *)things[num])->mUnknown84 = TRUE;
				}
				else
				{
					things[num] = UPhysicsManager::SpawnThing(name);
					inits[num] = SpawnInitThing(OID_CUnit, FALSE);
					inits[num]->Copy(&init);
				}
				CWorldMeshList::Add(name);
				num++;
			}
		}
		else if (type == OID_CSquad)
		{
			CInitThing	*init = SpawnInitThing(type, FALSE);
			inits[num] = init;
			if (init)
			{
				init->Load(version, *file);
				char	len;
				file->Read(&len, 1);
				file->Read(((CSquadInitThing *)init)->mUnitName, len);
				((CSquadInitThing *)init)->mUnitName[len] = 0;
				CUnitData	*data = UPhysicsManager::GetUnitData(((CSquadInitThing *)init)->mUnitName);
				file->Read(&dies_on[num], 4);
				if (data)
					things[num] = UPhysicsManager::SpawnSquad(data->mBehaviour);
				if (things[num])
					num++;
				else
				{
					delete init;
					inits[num] = NULL;
				}
			}
		}
		else if (type == OID_CFeature)
		{
			CInitThing	*init = SpawnInitThing(type, FALSE);
			inits[num] = init;
			if (init)
			{
				init->Load(version, *file);
				char	len;
				file->Read(&len, 1);
				char	name[256];
				file->Read(name, len);
				name[len] = 0;
				if (version >= 40)
					file->Read(&dies_on[num], 4);
				((CFeatureInitThing *)init)->mData = UPhysicsManager::GetFeatureData(name);
				things[num] = UPhysicsManager::SpawnFeature(name);
				if (things[num])
					num++;
				else
				{
					char	text[200];
					sprintf(text, "** Unable to find thing in physics: %s\n", name);
					TRACE(text);
					delete init;
					inits[num] = NULL;
				}
			}
		}
		else if (type == OID_CHazard)
		{
			CInitThing	*init = SpawnInitThing(type, FALSE);
			inits[num] = init;
			if (init)
			{
				init->Load(version, *file);
				char	len;
				file->Read(&len, 1);
				char	name[256];
				file->Read(name, len);
				name[len] = 0;
				if (version >= 40)
					file->Read(&dies_on[num], 4);
				((CHazardInitThing *)init)->mData = UPhysicsManager::GetHazardData(name);
				things[num] = UPhysicsManager::SpawnHazard(name);
				if (things[num])
					num++;
				else
				{
					char	text[200];
					sprintf(text, "** Unable to find thing in physics: %s\n", name);
					TRACE(text);
					delete init;
					inits[num] = NULL;
				}
			}
		}
		else if (type == OID_CStart)
		{
			// the base world keeps no starts
			things[num] = SpawnThing(OID_CStart, FALSE);
			if (things[num])
			{
				inits[num] = SpawnInitThing(type, FALSE);
				if (inits[num])
				{
					inits[num]->Load(version, *file);
					num++;
					if (base)
					{
						num--;
						delete things[num];
						delete inits[num];
						things[num] = NULL;
						inits[num] = NULL;
					}
				}
				else
					delete things[num];
			}
		}
		else
		{
			things[num] = SpawnThing(type, FALSE);
			if (things[num])
			{
				inits[num] = SpawnInitThing(type, FALSE);
				if (inits[num])
				{
					inits[num]->Load(version, *file);
					num++;
				}
				else
					delete things[num];
			}
		}
	}
	CONSOLE.StatusDone("Loading units", TRUE);

	// trees: groups scattered over a rectangle from a seed, then single trees
	CONSOLE.Status("Loading trees");
	short	num_groups;
	file->Read(&num_groups, 2);
	for (; num_groups; num_groups--)
	{
		SINT	seed = 0;
		float	y0, x0, y1, x1;
		SINT	count;
		file->Read(&y0, 4);
		file->Read(&x0, 4);
		file->Read(&y1, 4);
		file->Read(&x1, 4);
		file->Read(&count, 4);
		CTreeInitThing	tree;
		char	len;
		file->Read(&len, 1);
		char	n;
		for (n = 0; n < len; n++)
			file->Read(&tree.mTreeType[n], 1);
		tree.mTreeType[n] = 0;
		tree.mTreeTypeComplete = FALSE;
		file->Read(&seed, 4);
		if ((base) || (base_level == -1))
		{
			CSeedUniformRandomNumberStream	random(seed);
			for (; count; count--)
			{
				CThing	*thing = SpawnThing(OID_CTree, FALSE);
				float	fx = ((float)(random.Next() % 65536)) / 65536.0f;
				float	fy = ((float)(random.Next() % 65536)) / 65536.0f;
				tree.mPos.X = x0 + (x1 - x0) * fx;
				tree.mPos.Y = y0 + (y1 - y0) * fy;
				thing->Init(&tree);
			}
		}
	}

	SINT	num_types;
	file->Read(&num_types, 4);
	for (; num_types; num_types--)
	{
		CTreeInitThing	tree;
		char	len;
		file->Read(&len, 1);
		char	name[256];
		char	n;
		for (n = 0; n < len; n++)
			file->Read(&name[n], 1);
		name[n] = 0;
		tree.mTreeTypeComplete = TRUE;
		SINT	count;
		file->Read(&count, 4);
		for (; count; count--)
		{
			SINT	variant;
			file->Read(&tree.mPos.X, 4);
			file->Read(&tree.mPos.Y, 4);
			file->Read(&variant, 4);
			if ((base) || (base_level == -1))
			{
				strcpy(tree.mTreeType, name);
				char	number[2];
				sprintf(number, "%d", variant);
				strcat(tree.mTreeType, number);
				if ((strnicmp(tree.mTreeType, "fern", 4)) && (strnicmp(tree.mTreeType, "bush", 4)))
					SpawnThing(OID_CTree, FALSE)->Init(&tree);
			}
		}
	}
	CONSOLE.StatusDone("Loading trees", TRUE);

	if ((!base) && (base_level != -1))
		INFLUENCEMAP->Skip(file);
	else
		INFLUENCEMAP->Load(file);

	// start the things, keeping only what the career says survives in the base world
	for (SINT i = 0; i < num; i++)
	{
		CInitThing	*init = inits[i];
		if ((TargetOf(init) >= 0) && (TargetOf(init) < num))	// STAND-IN (a): the accessor (see above)
			init->mTarget = (SINT)things[TargetOf(init)];
		else
			init->mTarget = 0;

		BOOL	keep = TRUE;
		if (base)
		{
			if (!CAREER.DoesBaseThingExist(GAME.GetCurrentLevel(), i))
				keep = FALSE;
			else if ((dies_on[i] != -1) && (CAREER.IsWorldLater(GAME.GetCurrentLevel(), dies_on[i])))
				keep = FALSE;
		}
		if (keep)
		{
			things[i]->Init(init);
			if (base)
			{
#line 1131
				CActiveReader<CThing>	*reader = new(MEMTYPE_ACTIVE_READER) CActiveReader<CThing>;
				reader->SetReader(things[i]);
				mBaseWorldThingNB.Append(reader);
			}
		}
		else
		{
			things[i]->SetThingType(0);
#line 1157
			mBaseWorldThingNB.Append(new(MEMTYPE_ACTIVE_READER) CActiveReader<CThing>);
			if (things[i]->IsA(THING_TYPE_BUILDING))
			{
				FVector	pos = init->mPos;
				for (SINT c = 0; c < 10; c++)
					ENGINE.AddDamage(pos.X + (GAME.FloatRandom() - 0.5f) * 5.0f, pos.Y + (GAME.FloatRandom() - 0.5f) * 5.0f, 6);
			}
			things[i]->IncreaseThingCounter(init);
			delete things[i];
			things[i] = NULL;
		}
	}

	CWaypointManager::Load(file, version, things);

	// the level's timings and scores
	float	old_f, old_g, old_h;
	float	pre_run, pan, time_limit, full_score_time, percentage_score_time, score_percentage;
	SINT	s_grade, d_grade;
	file->Read(&old_f, 4);
	file->Read(&old_g, 4);
	file->Read(&old_h, 4);
	file->Read(&pre_run, 4);
	file->Read(&pan, 4);
	file->Read(&s_grade, 4);
	file->Read(&d_grade, 4);
	file->Read(&time_limit, 4);
	file->Read(&full_score_time, 4);
	file->Read(&percentage_score_time, 4);
	file->Read(&score_percentage, 4);
	if ((version >= 45) && (version < 48))
	{
		file->Read(&old_a, 4);
		file->Read(&old_b, 4);
		file->Read(&old_e, 4);
		file->Read(&old_c, 4);
		file->Read(&old_d, 4);
	}
	if ((!base) || (base_level == -1))
	{
		GAME.SetPreRunTime(pre_run);
		GAME.SetPanTime(pan);
		GAME.SetSGradeScore(s_grade);
		GAME.SetDGradeScore(d_grade);
		GAME.SetTimeLimit(time_limit);
		GAME.SetFullScoreTime(full_score_time);
		GAME.SetPercentageScoreTime(percentage_score_time);
		GAME.SetScorePercentage(score_percentage);
	}

	if (version >= 47)
	{
		if ((!base) && (base_level != -1))
		{
			SINT	ver = version;
			COccupancyGrid	*grid = (base) ? WORLD.mOccupancy[0] : WORLD.mOccupancy[0];	// STAND-IN (b): the same grid (see above)
			grid->SkipLegacy(*file);
			WORLD.mOccupancy[1]->SkipLegacy(*file);
			WORLD.mOccupancy[2]->SkipLegacy(*file);
			if (ver < 50)
				COccupancyGrid::SkipHeader(*file);
		}
		else
		{
			SINT	ver = version;
			WORLD.mOccupancy[0]->Load(*file);
			WORLD.mOccupancy[1]->Load(*file);
			WORLD.mOccupancy[2]->Load(*file);
			if (ver < 50)
				COccupancyGrid::SkipHeader(*file);
		}
	}

	if (version >= 49)
	{
		float	start_time;
		file->Read(&start_time, 4);
		if (!base)
			GAME.SetLevelStartTime(start_time);
	}

	for (SINT j = 0; j < 1000; j++)
	{
		delete inits[j];
		inits[j] = NULL;
	}

	{
		ListIterator<CScriptObjectCodeEntry>	scripts(&mScripts);
		for (CScriptObjectCodeEntry *entry = scripts.First(); entry; entry = scripts.Next())
			if (entry->mCode)
				entry->mCode->LoadSpawnedMeshes();
	}

	if (!base)
	{
		CONSOLE.SetLoadingFraction(0.25f);
		CWorldMeshList::SpawnUnused();
		INFLUENCEMAP->Build(FALSE);
		INFLUENCEMAP->Update();
		if (version < 47)
			COccupancy::ClearSteep();
		CONSOLE.SetLoadingFraction(0.5f);
		if (GAME.IsRestarting())
			COccupancy::ApplyStatic();
		else
			COccupancy::BuildStatic();
		CONSOLE.SetLoadingFraction(1.0f);
	}
	return TRUE;
}

//******************************************************************************************
void CWorld::LoadHeader(CDXMemBuffer *file, BOOL skip)	// FUNCTION: 0x0050d4c0
{
	SINT	version, unused1, unused2;
	file->Read(&version, 4);
	file->Read(&unused1, 4);
	file->Read(&unused2, 4);

	if (skip)
		UBattleEngineConfigurations::Skip(*file);
	else
		UBattleEngineConfigurations::Load(*file);

	if (version >= 2)
	{
		SINT	unused3, unused4, unused5;
		file->Read(&unused3, 4);
		file->Read(&unused4, 4);
		file->Read(&unused5, 4);
	}

	EWorldType	type = kSinglePlayer;
	if (version >= 3)
		file->Read(&type, 4);

	if (!skip)
		mType = type;
}

//******************************************************************************************
#line 1331
void CWorld::InitOccupancy()	// FUNCTION: 0x0050d580
{

	mOccupancy[0] = new(MEMTYPE_NAVIGATION) COccupancyGrid(35.0f);
	mOccupancy[1] = new(MEMTYPE_NAVIGATION) COccupancyGrid(45.0f);
	mOccupancy[2] = new(MEMTYPE_NAVIGATION) COccupancyGrid(60.0f);
}

// FUNCTION: 0x0050d680 ??_GCOccupancyGrid@@QAEPAXI@Z

// Defined here, after InitOccupancy: the deleting destructor Clear calls through it comes out after this definition, as
// retail's 0050d680 follows InitOccupancy (a deferred copy waits for the definition point of the body that uses it).
static inline void DeleteOccupancyGrids(CWorld *w)
{
	delete w->mOccupancy[0];
	w->mOccupancy[0] = NULL;
	delete w->mOccupancy[1];
	w->mOccupancy[1] = NULL;
	delete w->mOccupancy[2];
	w->mOccupancy[2] = NULL;
}

//******************************************************************************************
// A script's text on screen: the first free slot takes it (IScript::InitVariable 0053624d).
void CWorld::AddText(DWORD id, SINT state)	// FUNCTION: 0x0050d6a0
{
	for (SINT i = 0; i < MAX_WORLD_TEXT_SLOTS; i++)
	{
		if (mTextState[i] == 0)
		{
			mText[i] = TEXT.GetStringById(id);
			mTextId[i] = id;
			mTextState[i] = state;
			mTextTime[i] = 0;
			mTextDuration[i] = 0;
			mTextStart[i] = EVENT_MANAGER.GetTime();
			return;
		}
	}
}

//******************************************************************************************
void CWorld::SetTextTime(DWORD id, float time, float duration)	// FUNCTION: 0x0050d720
{
	for (SINT i = 0; i < MAX_WORLD_TEXT_SLOTS; i++)
	{
		if (mTextId[i] == id)
		{
			if (mTextState[i] == 3)
				mTextTime[i] = EVENT_MANAGER.GetTime() + time;
			else
			{
				mTextTime[i] = time;
				mTextDuration[i] = duration;
			}
		}
	}
}

//******************************************************************************************
float CWorld::GetTextTime(SINT slot)	// FUNCTION: 0x0050d760
{
	if (mTextState[slot] == 3)
	{
		float	t = mTextTime[slot] - EVENT_MANAGER.GetTime();
		if (t < 0)
			return 0;
		return t;
	}
	return mTextTime[slot];
}

//******************************************************************************************
void CWorld::RemoveText(DWORD id)	// FUNCTION: 0x0050d7a0
{
	for (SINT i = 0; i < MAX_WORLD_TEXT_SLOTS; i++)
		if (mTextId[i] == id)
			mTextState[i] = 0;
}

//******************************************************************************************
BOOL CWorld::IsMultiplayer()	// FUNCTION: 0x0050d7d0
{
	return (mType == kCooperativeMultiplayer || mType == kVersusMultiplayer);
}

// FUNCTION: 0x0050d7f0 ?DeleteAll@?$SPtrSet@VCScriptObjectCodeEntry@@@@QAEXXZ
