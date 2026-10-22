// PCRTID.cpp: reconstructed from PCRTID.obj (00515e40..005166ff); Stuart's release has no copy. The render-data table:
// which render things (by RTID) each object id gets and the mesh each starts from, and the factories that build a
// thing's chain of render things. The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
class CRTTree;class CRTMesh;class CRTBuilding;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include "mathsconst.h"
#include "RenderThing.h"
#include "rtmesh.h"
#include "rttree.h"
#include "RTBuilding.h"
#include "Cutscene.h"
// FUNCTION: 0x00515e40 init:ZERO_FVECTOR
// FUNCTION: 0x00515e60 init:ID_FMATRIX

#define MAX_RENDER_DATA	47

CRenderData	RENDER_DATA[MAX_RENDER_DATA];			// 0088a0f0
// FUNCTION: 0x00515f30 init:RENDER_DATA
// FUNCTION: 0x00515f90 exit:RENDER_DATA
SINT		NUM_RENDER_DATA;						// 00896488

// FUNCTION: 0x00515f60 ??0CRenderData@@QAE@XZ

//******************************************************************************************
// SYSTEM::Init (004eff93). Entries 0, 8 and 22 keep their metadata before the default/empty name copy:
// retail stores their RTID/object id at 00516002, 00516147 and 00516434, respectively.
void	InitRenderData()	// FUNCTION: 0x00515fb0
{
	strcpy(RENDER_DATA[0].mInit[0].mName, "tank1.msh");
	RENDER_DATA[0].mUnknown000 = 0;
	RENDER_DATA[0].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[0].mInit[0].mName, "default.msh");

	RENDER_DATA[1].mUnknown000 = 9;
	RENDER_DATA[1].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[1].mInit[0].mName, "cannon1.msh");

	RENDER_DATA[2].mUnknown000 = 10;
	RENDER_DATA[2].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[2].mInit[0].mName, "radar1.msh");

	RENDER_DATA[3].mUnknown000 = 6;
	RENDER_DATA[3].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[3].mInit[0].mName, "plane1.msh");

	RENDER_DATA[4].mUnknown000 = 3;
	RENDER_DATA[4].mRTID[0] = RTID_CRTMesh;

	RENDER_DATA[5].mUnknown000 = 7;
	RENDER_DATA[5].mRTID[0] = 2;
	strcpy(RENDER_DATA[5].mInit[0].mName, "tree2.msh");

	RENDER_DATA[6].mUnknown000 = 5;
	RENDER_DATA[6].mRTID[0] = -1;
	RENDER_DATA[7].mUnknown000 = 16;
	RENDER_DATA[7].mRTID[0] = -1;
	RENDER_DATA[8].mUnknown000 = 11;
	RENDER_DATA[8].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[8].mInit[0].mName, "");

	RENDER_DATA[9].mUnknown000 = 17;
	RENDER_DATA[9].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[9].mInit[0].mName, "");

	RENDER_DATA[10].mUnknown000 = 12;
	RENDER_DATA[10].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[10].mInit[0].mName, "Enemymech.msh");

	RENDER_DATA[11].mUnknown000 = 14;
	RENDER_DATA[11].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[11].mInit[0].mName, "bloke.msh");

	RENDER_DATA[12].mUnknown000 = 23;
	RENDER_DATA[12].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[12].mInit[0].mName, "EnemyT~1.msh");

	RENDER_DATA[13].mUnknown000 = 20;
	RENDER_DATA[13].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[13].mInit[0].mName, "shell.msh");

	RENDER_DATA[14].mUnknown000 = 21;
	RENDER_DATA[14].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[14].mInit[0].mName, "shell.msh");

	RENDER_DATA[15].mUnknown000 = 10001;
	RENDER_DATA[15].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[15].mInit[0].mName, "cockpit2.msh");

	RENDER_DATA[16].mUnknown000 = 13;
	RENDER_DATA[16].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[16].mInit[0].mName, "carrier.msh");

	RENDER_DATA[17].mUnknown000 = 26;
	RENDER_DATA[17].mRTID[0] = 5;
	strcpy(RENDER_DATA[17].mInit[0].mName, "default.msh");

	RENDER_DATA[18].mUnknown000 = 10002;
	RENDER_DATA[18].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[18].mInit[0].mName, "default.msh");

	RENDER_DATA[19].mUnknown000 = 29;
	RENDER_DATA[19].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[19].mInit[0].mName, "default.msh");

	RENDER_DATA[20].mUnknown000 = 31;
	RENDER_DATA[20].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[20].mInit[0].mName, "default.msh");

	RENDER_DATA[21].mUnknown000 = 33;
	RENDER_DATA[21].mRTID[0] = RTID_CRTMesh;
	strcpy(RENDER_DATA[21].mInit[0].mName, "default.msh");

	NUM_RENDER_DATA = 23;
	RENDER_DATA[22].mRTID[0] = RTID_CRTMesh;
	RENDER_DATA[22].mUnknown000 = 34;
	strcpy(RENDER_DATA[22].mInit[0].mName, "default.msh");
}

//******************************************************************************************
// SYSTEM::Shutdown (004f010b): every entry's extra names
void	ShutdownRenderData()	// FUNCTION: 0x00516450
{
	for (SINT c0 = 0; c0 < MAX_RENDER_DATA; c0++)
	{
		CInitRenderThing	&init = RENDER_DATA[c0].mInit[0];
		for (SINT c1 = 0; c1 < init.mNumExtra; c1++)
			SAFE_DELETE(init.mExtra[c1]);
		SAFE_DELETE_ARRAY(init.mExtra);
	}
}

//******************************************************************************************
// a thing's render things, the last one first, each the child of the one made after it. It returns the last
// CreateRenderThing result (esi, 00516568), not the chain's head: NULL if that one failed, and with no render things
// n is never set (retail returns whatever esi held, 0051656e). Both are retail's bugs, kept on purpose.
CRenderThing	*CreateAndGetRenderThing(int oid, IRenderableThing *thing)	// FUNCTION: 0x005164b0
{
	CRenderThing	*rt = NULL;
	for (SINT c0 = 0; c0 < NUM_RENDER_DATA; c0++)
	{
		if (RENDER_DATA[c0].mUnknown000 == oid)
		{
			if (RENDER_DATA[c0].mRTID[0] == -1)
				return NULL;
			CRenderThing	*n;
			for (SINT c1 = RENDER_DATA[c0].mNumRenderThings - 1; c1 >= 0; c1--)
			{
				n = CreateRenderThing(RENDER_DATA[c0].mRTID[c1]);
				if (n)
				{
					RENDER_DATA[c0].mInit[c1].mThing = thing;
					n->Init(&RENDER_DATA[c0].mInit[c1]);
					n->mChild = rt;
					rt = n;
				}
			}
			return n;
		}
	}
	return NULL;
}

//******************************************************************************************
CRenderThing	*CreateRenderThing(int rtid)	// FUNCTION: 0x00516580
{
	switch (rtid)
	{
#line 17
	case RTID_CRTMesh:	return new (MEMTYPE_RENDERTHING) CRTMesh;
	case 2:				return new (MEMTYPE_RENDERTHING) CRTTree;
	case 3:				break;
	case 4:				return new (MEMTYPE_RENDERTHING) CRTBuilding;
	case 5:				return new (MEMTYPE_RENDERTHING) CRTCutscene;
	}
	return NULL;
}

// The second pair of maths constants, after the code (see mathsconst2.h).
#include "mathsconst2.h"	// FUNCTION: 0x005166c0 init:ZERO_FVECTOR2
						// FUNCTION: 0x005166e0 init:ID_FMATRIX2
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_PCRTID[8] = { 0 };
