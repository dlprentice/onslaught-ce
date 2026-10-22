#include	"Common.h"	// Stuart's file, with the PC build's changes noted at each function

#include	"FEPGoodies.h"
#include	"Frontend.h"
#include	"Renderinfo.h"
#include	"SpriteRenderer.h"
#include	"data\MissionScripts\text\text.stf"
#include	"cliparams.h"
#include	"chunker.h"
#include	"resourceaccumulator.h"
#include	"meshrenderer.h"
#include	"fmv.h"
#include	"state.h"
#include	"music.h"
#include	"engine.h"
#include	"mouse.h"
#include	"Credits.h"
#include	"tweak.h"
// the number of complete rows of goodies
#define	NUM_GOODIES_X	123

// the number of columns
#define NUM_GOODIES_Y	4

// the number of goodies in the last column
#define NUM_GOODIES_REMAINDER (TOTAL_GOODIES % NUM_GOODIES_Y)

// and the number of complete rows plus optionally an incomplete row
#define NUM_GOODIES_X_INC (NUM_GOODIES_REMAINDER? NUM_GOODIES_X + 1: NUM_GOODIES_X)
// FUNCTION: 0x0045a820 init:ZERO_FVECTOR
#define	DEPTH			FO(305)
// FUNCTION: 0x0045a840 init:ID_FMATRIX
// #define this to repeatedly open random goodies
//#define STRESS_TEST_GOODIES
// FUNCTION: 0x0045a910 init:OLD_GOODIE_MESH_SYSTEM
// Well all the language text is a bit of a mess. Let's make some lookups,
// effectively hard-coding all this bollocksxc
CTweakSLONG	OLD_GOODIE_MESH_SYSTEM("OLD_GOODIE_MESH_SYSTEM", 0);	// 00679860
#define GRADE_S CGrade('S')
#define GRADE_A CGrade('A')
// FUNCTION: 0x0045a930 exit:OLD_GOODIE_MESH_SYSTEM
SINT	COUNT_TOTAL_A_GRADES()
{
	int node_count ;
	int found = 0 ;
	CGrade a_grade = GRADE_A;
	for (node_count=0; node_count< num_nodes; node_count++)
	{
		if (CGrade(CAREER.GetGradeFromRanking(CAREER.GetNode(node_count)->mRanking)) >= a_grade) 
		{
			found++;
		}
	}
	return found;
}

SINT	COUNT_TOTAL_S_GRADES()
{
	int node_count ;
	int found = 0 ;
	CGrade s_grade = GRADE_S;
	for (node_count=0; node_count< num_nodes; node_count++)
	{
		if (CGrade(CAREER.GetGradeFromRanking(CAREER.GetNode(node_count)->mRanking)) == s_grade)
		{
			found++;
		}
	}
	return found;
}

// Use the PC lookup directly: its complete caller matches without the GetString forwarding layer.
void CGoodieData::GetMethod(WCHAR *destbuf) const	// FUNCTION: 0x0045a940
{
	if (Method==-1)
	{
		wcscpy(destbuf,ToWCHAR(""));
		return;
	}

	wcscpy(destbuf, TEXT_DB.GetStringById(Method));

	if(Number >= 0)
	{
		char buf[20];
		sprintf(buf, " %d", Number);
		wcscat(destbuf, ToWCHAR(buf));

		if (mT1 != TK_TOTAL)
		{
			SINT	num;

			switch (mT1)
			{
			case TK_HACK_AGRADES:
				num = COUNT_TOTAL_A_GRADES();
				break;
			case TK_HACK_SGRADES:
				num = COUNT_TOTAL_S_GRADES();
				break;

			default:
				num = CAREER.GetNumKilled(mT1);
				break;

			};

			sprintf(buf, "  (%d)", num);
			wcscat(destbuf, ToWCHAR(buf));
		}
	}

	if (Method2 != -1)
	{
		wcscat(destbuf, ToWCHAR("\n"));
		wcscat(destbuf, TEXT_DB.GetStringById(Method2));
	}

	if (Number2 >= 0)
	{
		char buf[20];
		sprintf(buf, " %d", Number2);
		wcscat(destbuf, ToWCHAR(buf));
	}

		if (mT2 != TK_TOTAL)
		{
			char buf[20];
			SINT	num;

			switch (mT2)
			{
			case TK_HACK_AGRADES:
				num = COUNT_TOTAL_A_GRADES();
				break;
			case TK_HACK_SGRADES:
				num = COUNT_TOTAL_S_GRADES();
				break;

			default:
				num = CAREER.GetNumKilled(mT2);
				break;

			};

			sprintf(buf, "  (%d)", num);
			wcscat(destbuf, ToWCHAR(buf));
		}
}
// FUNCTION: 0x0045ac20 init:goodies
const CGoodieData goodies[] =
{
	CGoodieData(GOODIES_1),
	CGoodieData(GOODIES_2),
	CGoodieData(GOODIES_3, GOODIES_3B),
	CGoodieData(GOODIES_4, GOODIES_4B),
	CGoodieData(GOODIES_5, GOODIES_5B),
	CGoodieData(GOODIES_6, GOODIES_5B),
	CGoodieData(GOODIES_7, GOODIES_7B),
	CGoodieData(GOODIES_8, GOODIES_8B),
	CGoodieData(GOODIES_9),
	CGoodieData(GOODIES_10),
	CGoodieData(GOODIES_11),
	CGoodieData(GOODIES_12),
	CGoodieData(GOODIES_13),
	CGoodieData(GOODIES_14),
	CGoodieData(GOODIES_15),
	CGoodieData(GOODIES_16),
	CGoodieData(GOODIES_17),
	CGoodieData(GOODIES_18),
	CGoodieData(GOODIES_19),
	CGoodieData(GOODIES_20),
	CGoodieData(GOODIES_21),
	CGoodieData(GOODIES_22),
	CGoodieData(GOODIES_23),
	CGoodieData(GOODIES_24),
	CGoodieData(GOODIES_25),
	CGoodieData(GOODIES_26),
	CGoodieData(GOODIES_27),
	CGoodieData(GOODIES_28),
	CGoodieData(GOODIES_29),
	CGoodieData(GOODIES_30),
	CGoodieData(GOODIES_31),
	CGoodieData(GOODIES_32),
	CGoodieData(GOODIES_33),
	CGoodieData(GOODIES_34, -1, 40, -1, TK_INFANTY),
	CGoodieData(GOODIES_35, -1, 160, -1, TK_INFANTY),
	CGoodieData(GOODIES_36, -1, 80, -1, TK_INFANTY),
	CGoodieData(GOODIES_37, -1, 25, -1, TK_AIRCRAFT),
	CGoodieData(GOODIES_38, -1, 100, -1, TK_AIRCRAFT),
	CGoodieData(GOODIES_39, -1, 50, -1, TK_AIRCRAFT),
	CGoodieData(GOODIES_40, -1, 75, -1, TK_AIRCRAFT),
	CGoodieData(GOODIES_41, GOODIES_41B, 25, 80, TK_AIRCRAFT, TK_INFANTY),
	CGoodieData(GOODIES_42, GOODIES_42B, 50, 100, TK_AIRCRAFT, TK_INFANTY),
	CGoodieData(GOODIES_43 ,-1,100, -1, TK_VEHICLES),
	CGoodieData(GOODIES_44 ,-1,400, -1, TK_VEHICLES),
	CGoodieData(GOODIES_45, -1,300, -1, TK_VEHICLES),
	CGoodieData(GOODIES_46, -1,200, -1, TK_VEHICLES),
	CGoodieData(GOODIES_47),
	CGoodieData(GOODIES_48, -1,20, -1, TK_MECHS),
	CGoodieData(GOODIES_49, -1,40, -1, TK_MECHS),
	CGoodieData(GOODIES_50, -1,80, -1, TK_MECHS),
	CGoodieData(GOODIES_51, -1, 25), // the 25 ties in with the scripting
	CGoodieData(GOODIES_52, GOODIES_52B, 40, -1, TK_MECHS),
	CGoodieData(GOODIES_53),
	CGoodieData(GOODIES_54, GOODIES_54B,50,25, TK_EMPLACEMENTS, TK_AIRCRAFT),
	CGoodieData(GOODIES_55,-1,50, -1, TK_EMPLACEMENTS),
	CGoodieData(GOODIES_56,-1,25, -1, TK_EMPLACEMENTS),
	CGoodieData(GOODIES_57, GOODIES_57B,75,100, TK_EMPLACEMENTS, TK_VEHICLES),
	CGoodieData(GOODIES_58, GOODIES_58B,25,25, TK_EMPLACEMENTS, TK_AIRCRAFT),
	CGoodieData(GOODIES_59),
	CGoodieData(GOODIES_60),
	CGoodieData(GOODIES_61),
	CGoodieData(GOODIES_62),
	CGoodieData(GOODIES_63),
	CGoodieData(GOODIES_64, GOODIES_64B,100, -1, TK_AIRCRAFT),
	CGoodieData(GOODIES_65),
	CGoodieData(GOODIES_66),
	CGoodieData(GOODIES_67,-1,26, -1, TK_HACK_AGRADES),
	CGoodieData(GOODIES_68),
	CGoodieData(GOODIES_69),
	CGoodieData(GOODIES_70),
	CGoodieData(GOODIES_71),
	CGoodieData(GOODIES_72),
	CGoodieData(GOODIES_73),
	CGoodieData(GOODIES_74),
	CGoodieData(GOODIES_75, -1, 20, -1, TK_HACK_SGRADES),
	CGoodieData(GOODIES_76, -1, 40, -1, TK_HACK_SGRADES),
	CGoodieData(GOODIES_77, -1, 43, -1, TK_HACK_SGRADES),
	CGoodieData(GOODIES_78, -1, 43, -1, TK_HACK_SGRADES),
	CGoodieData(GOODIES_79),
	
	
	CGoodieData(GOODIES_79),	// Concept art
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(GOODIES_79),
	CGoodieData(),		// Movies
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData(),
	CGoodieData()	
};
// FUNCTION: 0x0045ac30 initbody:goodies
#define TOTAL_GOODIES (sizeof(goodies) / sizeof(goodies[0]))
// FUNCTION: 0x0045c770 ??0CGoodieData@@QAE@HHHHW4EKilledType@@0@Z
static float goodietransition[TOTAL_GOODIES];

//*********************************************************************************
static SINT	get_goodie_number(SINT x, SINT y)	// FUNCTION: 0x0045cb80
{
	if (y==0)
	{
		// Row 1
		if (x<8)
			return(x);		// 0-7 Bios 
		else if (x<13)		// 8-12 Race levels
			return((x-8)+66);
		else if (x==13)		// 13... dev stuff
			return(74);
		else if (x==14)
			return(75);
		else if (x==15)
			return(76);
		else if (x==16)
			return(77);

		return(-1);
	}
	else if (y==1)
	{
		// Row 2

		if (x<58)
			return(x+8);	// Units

		return(-1);
	}
	else if (y==2)
	{
		// Row 3
		
		if (x<32)
			return(x+201);	// FMV

		return(-1);
	}
	else if (y==3)
	{
		if (x<123)
			return(x+78);	// Concept art
	}

	return(-1);
}

static BOOL ischeatactive=FALSE;	static BOOL ischeat5active=FALSE;	// 006798b0, 006798b4 (the PC's cheat 5)

//*********************************************************************************
static EGoodieState	get_goodie_state(SINT x, SINT y)
{
	int num=get_goodie_number(x, y);

	if (num==-1)
		return(GS_UNKNOWN);
	if (ischeat5active) return(GS_INSTRUCTIONS);	// the PC build (CFEPGoodies::Render 0045e4a9)
	if (ischeatactive)
		return(GS_OLD);

	return CAREER.GetGoodieState(num);
}

//*********************************************************************************
static void	set_goodie_state(SINT x, SINT y, EGoodieState s)
{
	int num=get_goodie_number(x, y);

	if (num!=-1)
		CAREER.SetGoodieState(get_goodie_number(x, y), s);
}

//*********************************************************************************
//*********************************************************************************
void	CFEPGoodies::ResetGoodyState()
{
	mSelectFade = 1.f;
	mImageZoom = 0.f;
	mImageX = 0;
	mImageY = 0;
	mDisplayGoody = FALSE;
	mGoodyState = NO_GOODY;
	mMeshOri=ID_FMATRIX;
	mMeshDistance=10.0f;
	mLastProcessTime=0.0f;
	mTimeLeft=0.0f;
	mCurrentYaw=0.0f;
	mCurrentPitch=0.0f;
	mCurrentRoll=0.0f;
	mYawDelta=8.0f;
	mTextScrollOffset=-50.0f;
	mTextHeight=0.0f;
	mManualControl=FALSE;
}

//*********************************************************************************
BOOL	CFEPGoodies::Init()	// FUNCTION: 0x0045c7a0
{
	mCX = 0;
	mCY = 0;
	mXPos = 0;
	//EvaluateDest();	// the PC build scrolls in ButtonPressed instead: Init (0045c7a0) stores no mXDest

	mCurrentGoodyTexture = NULL;
	mNumCurrentGoodieTextures =0;

	ResetGoodyState();

	mTimerGoody			= PLATFORM.GetSysTimeFloat();

	for (int i=0;i<TOTAL_GOODIES;i++)
	{
		goodietransition[i]=0.0f;
	}

	return TRUE;
}

#ifdef RESBUILDER
static CTEXTURE *get_goodie_texture_hack(SINT goodie_num)
{
	switch (goodie_num)
	{
		default:
		case 0:		return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_hawk.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 1:		return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_tatianna.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 2:		return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_kramer.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 3:		return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_lorenzo.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 4:		return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_tara.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 5:		return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_billy.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 6:		return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_carver.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 7:		return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_surt.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 12:	return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_trooper01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 13:	return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_early_trooper.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 24:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_venturer.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 33:	return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_grunt.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 34:	return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_firebreather.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 35:	return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_commando.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 58:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_thunderhead.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 59:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_warspite.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 60:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_sub.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 61:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_hivev02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 62:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_gill-m.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 63:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_carvers_plane.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 64:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_fenrir.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 65:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_sentinel_01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);

		case 71:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_final01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 72:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_final02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 73:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_bea_battle_pic.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
			
		case 74:	return CTEXTURE::GetTextureByName("goodies\\ashley.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 76:	return CTEXTURE::GetTextureByName("goodies\\team.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 

		case 78:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v1.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 79:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v2_bluprint01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 80:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v2_bluprint02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 81:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v2_pg01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 82:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v2_pg02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 83:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v3.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 84:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v4&5.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 85:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v6.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 86:	return CTEXTURE::GetTextureByName("goodies\\ca_be_aquila\\ca_be_v7.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 

		case 87:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_sentinel_02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
		case 88:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_sentinel_03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 89:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_sentinel_04.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 90:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_hivev02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 91:	return CTEXTURE::GetTextureByName("goodies\\ca_bosses\\ca_boss_kraken.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 

		case 92:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_briefing_room.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 93:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_bunk_room.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 94:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_cell.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 95:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_command_room_interior.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 96:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_docks_exterior.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 97:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_docks_interior.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 98:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_fighter_cockpit.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 99:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_hanger_interior.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 100:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_lifter_cockpit.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 101:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_loader_mech.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 102:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_marshall_bridge.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 103:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_research_compound.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 104:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_f_venturer_bridge.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 105:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_m_cell.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 106:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_m_fenrir_bridge01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 107:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_m_fenrir_bridge02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 108:	return CTEXTURE::GetTextureByName("goodies\\ca_cutscene_locations\\ca_cl_m_staging_area.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 

		case 109:	return CTEXTURE::GetTextureByName("goodies\\ca_f_architecture\\ca_fa_f_buildings01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 110:	return CTEXTURE::GetTextureByName("goodies\\ca_f_architecture\\ca_fa_f_buildings02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 111:	return CTEXTURE::GetTextureByName("goodies\\ca_f_architecture\\ca_fa_f_buildings03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 112:	return CTEXTURE::GetTextureByName("goodies\\ca_f_architecture\\ca_fa_f_buildings04.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 113:	return CTEXTURE::GetTextureByName("goodies\\ca_f_architecture\\ca_fa_f_buildings05.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 114:	return CTEXTURE::GetTextureByName("goodies\\ca_f_architecture\\ca_fa_f_buildings06.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 115:	return CTEXTURE::GetTextureByName("goodies\\ca_f_architecture\\ca_fa_f_buildings07.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 116:	return CTEXTURE::GetTextureByName("goodies\\ca_f_architecture\\ca_fa_f_buildings08.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 

		case 117:	return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_characters.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 118:	return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_general.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 119:	return CTEXTURE::GetTextureByName("goodies\\ca_f_characters\\ca_fc_trooper02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 

		case 120:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\CA_FU_ forseti forces.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 121:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_a15_ground_attack.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 122:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_av6_rhino_apc.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 123:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_early_craft01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 124:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_early_craft02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 125:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_early_craft03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 126:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_early_craft04.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 127:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_early_craft05.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 128:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_early_craft06.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 129:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_f24.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 130:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_heavy_bomber.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 131:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_heavy_lifter.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 132:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_sat_launcher.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 133:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_style.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 134:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_tank_av14.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 135:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_tank_av18.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 136:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_tank_m1.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 137:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_tank_m112.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 138:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_tanks.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 139:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_truck.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 140:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_wildcard_plane.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 141:	return CTEXTURE::GetTextureByName("goodies\\ca_f_units\\ca_fu_xf24-venom_fighter.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
			
		case 142:	return CTEXTURE::GetTextureByName("goodies\\ca_m_architecture\\ca_ma_airfighterfactory.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 143:	return CTEXTURE::GetTextureByName("goodies\\ca_m_architecture\\ca_ma_barracks.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 144:	return CTEXTURE::GetTextureByName("goodies\\ca_m_architecture\\ca_ma_mech_factory.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 145:	return CTEXTURE::GetTextureByName("goodies\\ca_m_architecture\\ca_ma_muspell architecture.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 146:	return CTEXTURE::GetTextureByName("goodies\\ca_m_architecture\\ca_ma_tankfactory.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		
		case 147:	return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_4eyes.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 148:	return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_early_grunt.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 149:	return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_early_surt.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 150:	return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_hunter.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 151:	return CTEXTURE::GetTextureByName("goodies\\ca_m_characters\\ca_mc_muspell characters.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 

		case 152:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu__crab_mech.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 153:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_apc.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 154:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_artillery.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 155:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_asx9_tactical_advanced.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 156:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_bomber.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 157:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_carvers_plane.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 158:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_craft05.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 159:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_early_craft01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 160:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_early_craft02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 161:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_early_craft03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 162:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_early_craft04.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 163:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_early_forces.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 164:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_early_forces01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 165:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_early_sam site.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 166:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_ground_attack.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 167:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_heavy_lifter.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 168:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_interceptor.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 169:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_mech.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 170:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_mobile_outpost.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 171:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_rifle_mech.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 172:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_rocket_launcher.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 173:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_tank.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 174:	return CTEXTURE::GetTextureByName("goodies\\ca_m_units\\ca_mu_truck.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
			
		case 175:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_graphics01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 176:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_graphics02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 177:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_graphics03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 178:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_graphics04.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 179:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_insignia 01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 180:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_insigina 02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 181:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_insigina 03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 182:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_numbers01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 183:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_numbers02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 184:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_numbers03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 185:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_numbers04.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 186:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_ss00.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 187:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_ss01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 188:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_ss02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 189:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_ss03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 190:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_ss04.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 
		case 191:	return CTEXTURE::GetTextureByName("goodies\\ca_m_language\\ca_ml_ss05.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 

		case 192:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_01.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 193:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_02.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 194:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_03.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 195:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_04.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 196:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_05.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 197:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_06.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 198:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_07.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 199:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_08.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1); 			
		case 200:	return CTEXTURE::GetTextureByName("goodies\\ca_f_language\\ca_fl_09.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1);
	};
}

//*********************************************************************************
static CMESH *get_goodie_mesh_hack(SINT goodie_num)
{
	switch (goodie_num)
	{
		default:
		case 8:			return CMESH::GetMesh("f_be1.msh");
		case 9:			return CMESH::GetMesh("f_be1.msh");
		case 10:		return CMESH::GetMesh("f_be1.msh");
		case 11:		return CMESH::GetMesh("f_be1.msh");
		case 12:
		case 13:
		case 14:		return CMESH::GetMesh("fpulsetank.msh");
		case 15:		return CMESH::GetMesh("f_battletank.msh");
		case 16:		return CMESH::GetMesh("fbeamtank.msh");
		case 17:		return CMESH::GetMesh("fplasmatank.msh");
		case 18:		return CMESH::GetMesh("f_truck.msh");
		case 19:		return CMESH::GetMesh("fa_f24.msh");
		case 20:		return CMESH::GetMesh("fa_xf24.msh");
		case 21:		return CMESH::GetMesh("fa_heavy_bomber.msh");
		case 22:		return CMESH::GetMesh("fa_a15b.msh");
		case 23:		return CMESH::GetMesh("f_lifter.msh");
		case 24:		
		case 25:		return CMESH::GetMesh("flander.msh");
		case 26:		return CMESH::GetMesh("fs_marshall.msh");
		case 27:		return CMESH::GetMesh("fs_cargo.msh");
		case 28:		return CMESH::GetMesh("ft_sam.msh");
		case 29:		return CMESH::GetMesh("ft_beam.msh");
		case 30:		return CMESH::GetMesh("ft_pulse.msh");
		case 31:		return CMESH::GetMesh("ft_artillery.msh");
		case 32:		return CMESH::GetMesh("ft_blaster.msh");
		case 33:
		case 34:
		case 35:
		case 36:		return CMESH::GetMesh("muspfite.msh");
		case 37:		return CMESH::GetMesh("m_atf.msh");
		case 38:		return CMESH::GetMesh("m_bomber.msh");
		case 39:		return CMESH::GetMesh("m_groundat.msh");
		case 40:		return CMESH::GetMesh("dropship.msh");
		case 41:		return CMESH::GetMesh("m_dropship.msh");
		case 42:		return CMESH::GetMesh("mtanklight.msh");
		case 43:		return CMESH::GetMesh("mtank.msh");
		case 44:		return CMESH::GetMesh("m_samlauncher.msh");
		case 45:		return CMESH::GetMesh("m_artillery.msh");
		case 46:		return CMESH::GetMesh("m_truck.msh");
		case 47:		return CMESH::GetMesh("gunwalker.msh");
		case 48:		return CMESH::GetMesh("gunwalker2.msh");
		case 49:		return CMESH::GetMesh("guncrab.msh");
		case 50:		return CMESH::GetMesh("gnat.msh");
		case 51:		return CMESH::GetMesh("arachnid.msh");
		case 52:		return CMESH::GetMesh("m_battleship.msh");	
		case 53:		return CMESH::GetMesh("mt_sam.msh");
		case 54:		return CMESH::GetMesh("mt_laser.msh");
		case 55:		return CMESH::GetMesh("mt_machinegun.msh");
		case 56:		return CMESH::GetMesh("mt_artillery.msh");
		case 57:		return CMESH::GetMesh("mt_flak.msh");
		case 76:		return CMESH::GetMesh("panorama.msh");
	};
}

//*********************************************************************************
static CTEXTURE *get_goodie_background_hack(SINT goodie_num)
{
	switch (goodie_num)
	{
		default:
		case 8:			return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 9:			return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 10:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 11:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 14:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 15:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 16:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 17:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 18:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 19:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 20:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 21:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 22:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 23:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 24:
		case 25:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 26:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 27:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 28:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 29:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 30:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 31:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 32:		return(CTEXTURE::GetTextureByName("goodies\\goodies_F_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 33:
		case 34:
		case 35:
		case 36:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 37:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 38:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 39:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 40:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 41:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 42:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 43:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 44:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 45:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 46:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 47:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 48:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 49:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 50:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 51:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 52:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 53:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 54:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 55:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 56:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
		case 57:		return(CTEXTURE::GetTextureByName("goodies\\goodies_M_Bakgrnd.tga",TEXFMT_A8R8G8B8,TEX_NORMAL,1));
	};
}
#endif
//*********************************************************************************
static EGoodieType get_goodie_type_hack(SINT goodie_num)
{
	if (goodie_num<=7)
		return(GT_IMAGE);
	else if (goodie_num==12)
		return(GT_IMAGE);
	else if (goodie_num==13)
		return(GT_IMAGE);
	else if (goodie_num==24)
		return(GT_IMAGE);
	else if (goodie_num==33)
		return(GT_IMAGE);
	else if (goodie_num==34)
		return(GT_IMAGE);
	else if (goodie_num==35)
		return(GT_IMAGE);
	else if (goodie_num<=57)
		return(GT_MESH);
	else if (goodie_num<=65)
		return(GT_IMAGE);
	else if (goodie_num<=70)
		return(GT_LEVEL);
	else if (goodie_num<=73)
		return(GT_IMAGE);
	else if (goodie_num==74)
		return(GT_IMAGE);
	else if (goodie_num==75)
		return(GT_FMV);
	else if (goodie_num==76)
		return(GT_MESH);
	else if (goodie_num==77)
		return(GT_FMV);
	else if (goodie_num>200)
		return(GT_FMV);
	else 
		return(GT_IMAGE);
}

//*********************************************************************************

enum EGoodieTextType
{
	GTT_GENERIC,
	GTT_UNIT,
	GTT_PERSON
};

static EGoodieTextType get_goodie_text_type_hack(SINT goodie_num)	// FUNCTION: 0x0045ff80
{
	if (goodie_num<=7)
		return(GTT_PERSON);
	else if (goodie_num<=65)
		return(GTT_UNIT);
	else
		return(GTT_GENERIC);
}

//*********************************************************************************

static int get_goodie_num_texts(SINT goodie_num)
{
	if (goodie_num>77)
		return 0;

	if (goodie_num>65)
		return(9);

	switch (get_goodie_text_type_hack(goodie_num))
	{
		case GTT_GENERIC:	return(4);
		case GTT_PERSON:	return(12);
		case GTT_UNIT:		return(9);
		default:			return(4);
	};
}

//*********************************************************************************

static void get_goodie_title(WCHAR *buffer,SINT goodie_num)
{
	int stringnumber=0;

	for (int i=0;i<goodie_num;i++)
	{
		stringnumber+=get_goodie_num_texts(i);
	}

	wcscpy(buffer,ToWCHAR(""));

	if (goodie_num>77) // No text for these
		return;	

	if (wcslen(TEXT_DB.GetFollowingString(GOODIE_TEXT_1_TITLE,stringnumber))==0) 
		return; // No text

	wcscpy(buffer,TEXT_DB.GetFollowingString(GOODIE_TEXT_1_TITLE,stringnumber));
}

//*********************************************************************************

static void get_goodie_text(WCHAR *buffer,SINT goodie_num,SINT text_num)
{
	int stringnumber=0;

	for (int i=0;i<goodie_num;i++)
	{
		stringnumber+=get_goodie_num_texts(i);
	}

	stringnumber+=text_num;

	wcscpy(buffer,ToWCHAR(""));

	if (goodie_num>77) // No text for these
		return;

	if (wcslen(TEXT_DB.GetFollowingString(GOODIE_TEXT_1_TITLE,stringnumber))==0) 
		return; // No text

	if (get_goodie_text_type_hack(goodie_num)==GTT_PERSON)
	{
		switch (text_num)
		{
			case 4:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_BORN)); break;
			case 5:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_AGE)); break;
			case 6:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_HEIGHT)); break;
			case 7:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_WEIGHT)); break;
			case 8:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_HAIR)); break;
			case 9:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_EYES)); break;
			case 10:	wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_NATIONALITY)); break;
			case 11:	wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_OCCUPATION)); break;
		};
	}
	else if (get_goodie_text_type_hack(goodie_num)==GTT_UNIT)
	{
		switch (text_num)
		{
			case 4:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_TOPSPEED)); break;
			case 5:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_ARMOUR)); break;
			case 6:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_MOBILITY)); break;
			case 7:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_POWER)); break;
			case 8:		wcscpy(buffer,TEXT_DB.GetString(GOODIE_TEXT_WEAPONRY)); break;
		};
	}

	wcscat(buffer,ToWCHAR(" "));
	wcscat(buffer,TEXT_DB.GetFollowingString(GOODIE_TEXT_1_TITLE,stringnumber));
}

//*********************************************************************************

static void get_goodie_fmv(char *buffer,SINT goodie_num)
{
	if ((goodie_num>=201) && (goodie_num<=232))	// 201-233 = In game FMVs
	{
		if (goodie_num==232)
			MakeCutsceneName(buffer,33);	// the PC build (CFEPGoodies::Process 0045dac3)
		else
			MakeCutsceneName(buffer,goodie_num-200);
	}
	else
	{
		switch (goodie_num)
		{
			default:
			case 75:	strcpy(buffer,"GILL_M_on_a_fork"); break; // Foresti High video
			case 77:	strcpy(buffer,"UsTheMovie"); break; // Dev video
		};
	}
}

//*********************************************************************************

static BOOL get_goodie_fmv_localised(SINT goodie_num)
{
	if ((goodie_num>=201) && (goodie_num<=232))	// 201-233 = In game FMVs
	{
		if ((goodie_num==209) || (goodie_num==212) || (goodie_num==213) || (goodie_num==214) || (goodie_num==215) || (goodie_num==216) || (goodie_num==228) || (goodie_num==231))	// PC: 0045db70, 0045db78
			return(FALSE);

		return(TRUE);
	}
	else
		return(FALSE);
}

//*********************************************************************************

static int get_goodie_level(SINT goodie_num)
{
	switch (goodie_num)
	{
		default:
		case 66:	return(901);
		case 67:	return(902);
		case 68:	return(903);
		case 69:	return(904);
		case 70:	return(905);
	};
}

//*********************************************************************************
#ifdef RESBUILDER
void CFEPGoodies::Serialise(CChunker *c, CResourceAccumulator *ra)
{
	int number=-(ra->GetTargetLevel()+1000);

	EGoodieType t=get_goodie_type_hack(number);

	c->Start(MKID("GDIE"));

	c->Start(MKID("GDAT"));
	
	c->Write(&number,sizeof(number),1);
	c->Write(&t,sizeof(t),1);

	if (t==GT_IMAGE)
	{
		// Image goodie

		CTEXTURE *srctexture=get_goodie_texture_hack(number);
		
		if (srctexture==ENGINE.GetDefaultTexture())
		{
			char buf[256];
			sprintf(buf,"Goodie number %d has missing texture!\n",number);
			TRACE(buf);
			SASSERT(0,buf);
		}
		
		int numtextures=(srctexture->GetHeight()+509)/510;
		
		SASSERT(srctexture->mWidth<=1024,"Goodie texture too wide!");

		int srcheight=srctexture->GetHeight();
		
		CTEXTURE *strip;
		
		int y=0;
		
		c->Start(MKID("IMAG"));
		
		c->Write(&numtextures,sizeof(numtextures),1);
		c->Write(&srcheight,sizeof(srcheight),1);
		
		c->End();
		
		IDirect3DSurface8 *surf;
		srctexture->GetTexture()->GetSurfaceLevel(0,&surf);	
		
		for (int i=0;i<numtextures;i++)
		{		
			int height=srctexture->GetHeight()-y;
			
			if (height>512)
				height=512;

			int theight=height;			

			if (theight<16)
				theight=16;
			
			strip=new (MEMTYPE_SCRATCHPAD) CTEXTURE;
			
			strip->InitCustom(srctexture->mWidth,theight,TEXFMT_A8R8G8B8,1,FALSE);
			
			strip->GrabFromSurface(surf,0,y,0,0,srctexture->mWidth-1,height-1,FALSE);
			
			y+=height-2;
			
			c->Start(MKID("TXTR"));
			strip->Serialize(c,ra,0,0);
			c->End();
			
			strip->Release();
			strip->ForceDelete();
		}
		
		surf->Release();
	}
	else if (t==GT_MESH)
	{
		// Mesh goodie

		CMESH *mesh=get_goodie_mesh_hack(number);

		// Count unique textures needed

		BOOL *unique=new (MEMTYPE_SCRATCHPAD) BOOL[mesh->mNumTextures];
		int i;
		int numtextures=0;

		for (i=0;i<mesh->mNumTextures;i++)
		{
			unique[i]=TRUE;
			for (int j=0;j<i;j++)
			{
				if (mesh->mTextures[i].mTexture==mesh->mTextures[j].mTexture)
					unique[i]=FALSE;
			}
			if (unique[i])
				numtextures++;
		}

		// Add 1 texture for the background

		numtextures++;

		c->Start(MKID("MDAT"));		
		c->Write(&numtextures,sizeof(numtextures),1);		
		c->End();

		// Background image

		c->Start(MKID("TXTR"));
		get_goodie_background_hack(number)->Serialize(c,ra,0,0);
		c->End();

		for (i=0;i<mesh->mNumTextures;i++)		
		{
			if (unique[i])
			{
				c->Start(MKID("TXTR"));
				mesh->mTextures[i].mTexture->Serialize(c,ra,0,0);
				c->End();
			}
		}

		SAFE_DELETE(unique);

		c->Start(MKID("MESH"));
		mesh->Serialize(c,ra,0);
		c->End();
	}
	else if (t==GT_FMV)
	{
	}
	else if (t==GT_CHEAT)
	{
	}
	else if (t==GT_LEVEL)
	{
	}
	else
		SASSERT(0,"Unknown goodie type!");

	c->End();
	c->End();
}
#endif
//*********************************************************************************

void CFEPGoodies::Deserialise(CChunkReader *c)	// FUNCTION: 0x0045c870
{
	// The PC build's goodie resource holds one texture for an image and no chunk checks (0045c876..0045c8a3); the
	// allocations are at retail lines 1201 and 1241 (0045c8ae, 0045c941).
	FreeUpGoodyResources();

	c->GetNext();

	int number;

	c->Read(&number,sizeof(number),1);
	c->Read(&mCurrentGoodyType,sizeof(mCurrentGoodyType),1);

	if (mCurrentGoodyType==GT_IMAGE)
	{
		mNumCurrentGoodieTextures=1;
#line 1201
		mCurrentGoodyTexture=new(MEMTYPE_TEXTURE) CTEXTURE *[mNumCurrentGoodieTextures];

		mCurrentGoodyTexture[0]=CTEXTURE::Deserialize(0,c);
		mCurrentGoodyTexture[0]->AddRef();
		mCurrentGoodieHeight=mCurrentGoodyTexture[0]->GetHeight();
	}
	else if (mCurrentGoodyType==GT_MESH)
	{
		c->GetNext();

		c->Read(&mNumCurrentGoodieTextures,sizeof(mNumCurrentGoodieTextures),1);

#line 1241
		mCurrentGoodyTexture=new(MEMTYPE_TEXTURE) CTEXTURE *[mNumCurrentGoodieTextures];

		for (int i=0;i<mNumCurrentGoodieTextures;i++)
		{
			c->GetNext();

			mCurrentGoodyTexture[i]=CTEXTURE::Deserialize(0,c);
			mCurrentGoodyTexture[i]->AddRef();
		}

		// Mesh itself

		c->GetNext();

		mCurrentGoodyMesh=CMESH::Deserialize(c,0);
		mCurrentGoodyMesh->AddRef();
	}
}

#line 1225
//*********************************************************************************
void	CFEPGoodies::Shutdown()	// FUNCTION: 0x0045c9e0
{
	FreeUpGoodyResources();
}

//*********************************************************************************
void	CFEPGoodies::StartLoadingGoody()	// FUNCTION: 0x0045c9f0
{
	// reset some stuff
	mImageX = 0;
	mImageY = 0;

	ASSERT(mCurrentGoodyTexture == NULL);

	int goodie_number = get_goodie_number(mCX, mCY);

	if (goodie_number==-1)
		return;

	// the goodie is in a resource now.
	if (!CLIPARAMS.mBuildResources)
	{
		// We first get the goodies into a membuffer.
		char name[MAX_PATH];

		CResourceAccumulator::GetFileName(name,-1000 - goodie_number, TARGET);

		mCurrentGoodyType=get_goodie_type_hack(goodie_number);

		char buffer[256];
		sprintf(buffer,"Goodie %d, type %d\n",goodie_number,mCurrentGoodyType);
		TRACE(buffer);

		if ((mCurrentGoodyType!=GT_FMV) && (mCurrentGoodyType!=GT_LEVEL) && (mCurrentGoodyType!=GT_CHEAT))
		{
#if TARGET == PS2
			// async my arse
			CResourceAccumulator::ReadResources(-1000 - goodie_number);
			mGoodyState = GOODY_LOADED;
#else
			// it's got some resources. Load them async.
			mGoodyState = GOODY_LOADING;
			mGoodieLoader.LoadFile(name, 5*1024*1024);
#endif
		}
		else
		{
			// don't do anything, just mark the goodie as loaded.
			mGoodyState = GOODY_LOADED;
		}
	}

}

//*********************************************************************************
void	CFEPGoodies::LoadingGoodyPoll()	// FUNCTION: 0x0045cc10
{
	// Let's see if he's done something.
	if (mGoodyState == GOODY_LOADING && !mGoodieLoader.Busy() && mGoodieLoader.mMemBuffer)
	{
		int goodie_number = get_goodie_number(mCX, mCY);

		if (goodie_number==-1)
			return;

		float started = PLATFORM.GetSysTimeFloat();

		// yes! Load the goodie from the resource file in memory.
		mGoodieLoader.GrabMutex();
		CResourceAccumulator::ReadResources(-1000 - goodie_number, mGoodieLoader.mMemBuffer);
		mGoodieLoader.mMemBuffer->Close();
		delete mGoodieLoader.mMemBuffer;
		mGoodieLoader.mMemBuffer = NULL;
		mGoodieLoader.ReleaseMutex();

		mGoodyState = GOODY_LOADED;		

		char buf[100];
		sprintf(buf, "Goody creation took %f s\n", PLATFORM.GetSysTimeFloat() - started);
		TRACE(buf);
	}
}

//*********************************************************************************
void	CFEPGoodies::FreeUpGoodyResources()	// FUNCTION: 0x0045cd10
{
	if (mCurrentGoodyMesh)
	{
		mCurrentGoodyMesh->Release();
		mCurrentGoodyMesh->ForceDelete();
		mCurrentGoodyMesh=NULL;
	}

	if (mCurrentGoodyTexture)
	{
		for (int i=0;i<mNumCurrentGoodieTextures;i++)
		{
			mCurrentGoodyTexture[i]->Release();
#if TARGET != PS2
			mCurrentGoodyTexture[i]->ForceDeleteVBuffer();
#endif
			CTEXTURE::mForceDeleting = TRUE; mCurrentGoodyTexture[i]->ForceDelete(); CTEXTURE::mForceDeleting = FALSE;
			mCurrentGoodyTexture[i] = NULL;
		}

		SAFE_DELETE_ARRAY(mCurrentGoodyTexture);
		mNumCurrentGoodieTextures=0;
	}

	mGoodyState = NO_GOODY;
}

//*********************************************************************************
// The PC build scrolls the grid a column at a time when the cursor passes these screen positions (0045cee4,
// 0045cf7c).
float	GOODIES_SCROLL_LEFT = 90.f;		// 006292e4
float	GOODIES_SCROLL_RIGHT = 570.f;	// 006292e8

void	CFEPGoodies::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0045cde0
{
	if ((!mDisplayGoody) && (mImageZoom==0.0f))
	{
		SINT	oldcx = mCX;
		SINT	oldcy = mCY;

		switch (button)
		{
		case BUTTON_FRONTEND_MENU_UP:
			{
				SINT ny = mCY - 1;

				while (ny >= 0)
				{
					if (get_goodie_number(mCX, ny) != -1)
						break;
					ny --;
				};

				if (ny != -1)
					mCY = ny;
			}


			FRONTEND.PlaySound(FES_MOVE);
			break;

		case BUTTON_FRONTEND_MENU_DOWN:
			{
				SINT ny = mCY + 1;

				while (ny < NUM_GOODIES_Y)
				{
					if (get_goodie_number(mCX, ny) != -1)
						break;
					ny ++;
				};

				if (ny != NUM_GOODIES_Y)
					mCY = ny;
			}
			FRONTEND.PlaySound(FES_MOVE);
			break;

		case BUTTON_FRONTEND_MENU_LEFT:
			mCX --;
			if (mCX < 0)
				mCX = 0;
			FRONTEND.PlaySound(FES_MOVE);

			if (GoodieX(mCX) < GOODIES_SCROLL_LEFT)
				mXPos -= 45.f;
			break;

		case BUTTON_FRONTEND_MENU_RIGHT:
			mCX ++;
			if (mCX > NUM_GOODIES_X_INC - 1)
				mCX = NUM_GOODIES_X_INC - 1;

			while (get_goodie_number(mCX, mCY)==-1)
				mCX--;

			FRONTEND.PlaySound(FES_MOVE);

			if (GoodieX(mCX) > GOODIES_SCROLL_RIGHT)
				mXPos += 45.f;
			break;

		case BUTTON_FRONTEND_MENU_SELECT:
			FRONTEND.PlaySound(FES_SELECT);
			ResetGoodyState();
			if (get_goodie_state(mCX, mCY) == GS_NEW || get_goodie_state(mCX, mCY) == GS_OLD)
			{
				// has the previous one gone yet?
				if (mGoodyState == NO_GOODY)
				{
					mDisplayGoody = TRUE;
					StartLoadingGoody(); // load it
					set_goodie_state(mCX, mCY, GS_OLD); // we've seen it now!
				}
			}
			break;

		case BUTTON_FRONTEND_MENU_BACK:
			FRONTEND.PlaySound(FES_BACK);
			FreeUpGoodyResources();  // just in case they're still here...
			CFEPOptions::SaveDefaultOptions(false);
			break;
		};

		if ((mCX != oldcx) || (mCY != oldcy))
			mTimerGoody = PLATFORM.GetSysTimeFloat();
	}
	else
	{
		switch (button)
		{

		case BUTTON_FRONTEND_MENU_BACK:
			FRONTEND.PlaySound(FES_BACK);
			mDisplayGoody = FALSE;
			break;

		default:
			break;
		};

			// Analogue button controls?
		if (mImageZoom >= 1.f)
		{
			if (mCurrentGoodyType == GT_IMAGE)
			{
				float dx = 0, dy = 0, dz = 0;

				switch (button)
				{
					case BUTTON_CAMERA_STRAFE_LEFT:		dx += val; break;
					case BUTTON_CAMERA_STRAFE_RIGHT:	dx += val; break;
					case BUTTON_CAMERA_MOVE_FORAWRD:	dy += val; break;
					case BUTTON_CAMERA_MOVE_BACKWARDS:	dy += val; break;
					case BUTTON_CAMERA_PITCH_UP:		dz += val; break;
					case BUTTON_CAMERA_PITCH_DOWN:		dz += val; break;
				};

				mImageX += dx * 0.05f;
				mImageY += dy * 0.05f;
				mImageZoom -= dz * 0.1f;

				Clamp(mImageX, -0.8f, 0.8f);
				Clamp(mImageY, -0.8f, 0.8f);
				Clamp(mImageZoom, 1.f, 5.f);
			}
			else if (mCurrentGoodyType == GT_MESH)
			{
				float dx = 0, dy = 0, dz = 0;

				switch (button)
				{
					case BUTTON_CAMERA_STRAFE_LEFT:		if (mManualControl) dx += val; break;
					case BUTTON_CAMERA_STRAFE_RIGHT:	if (mManualControl) dx += val; break;
					case BUTTON_CAMERA_MOVE_FORAWRD:	if (mManualControl) dy += val; break;
					case BUTTON_CAMERA_MOVE_BACKWARDS:	if (mManualControl) dy += val; break;
					case BUTTON_CAMERA_PITCH_UP:		if (mManualControl) dz += val; break;
					case BUTTON_CAMERA_PITCH_DOWN:		if (mManualControl) dz += val; break;
					case BUTTON_START:					mManualControl=!mManualControl; break;
				};

				if ((dx!=0) || (dy!=0))
				{
					FMatrix delta;
					delta.MakeRotationF(dx/10.0f,dy/10.0f,0);
					mMeshOri=delta * mMeshOri;
				}

				mMeshDistance+=dz*0.1f;

				if (mCurrentGoodyMesh)
					Clamp(mMeshDistance, mCurrentGoodyMesh->mBoundingBox->mRadius, mCurrentGoodyMesh->mBoundingBox->mRadius*3.0f);
				else
					Clamp(mMeshDistance, 0.1f, 50.0f);
			}
		}

	}
}

//*********************************************************************************
void	FEMouseEdgeSlide(EFEPState state, float *pos, float max)	// FUNCTION: 0x0045d730
{
	if (FEMESSBOX.IsInteractive())
		return;

	if (state != FEPS_ACTIVE)
		return;

	float	speed = 0;

	if ((MOUSE_X > 540) && (MOUSE_Y < 400))
		speed = float(MOUSE_X - 540);
	else if ((MOUSE_X < 100) && (MOUSE_Y < 400))
		speed = float(MOUSE_X - 100);

	float step = gFrontEndFrameStep;
	*pos += step * speed * speed * speed * 0.002f;

	if (*pos < 0)
		*pos = 0;
	if (*pos > max)
		*pos = max;
}

//*********************************************************************************
void	CFEPGoodies::Process(EFEPState state)	// FUNCTION: 0x0045d7e0
{
	static int	lastmousex = -1;	// 006292ec
	static int	lastmousey = -1;	// 006292f0

	if (FRONTEND.mFEPSaveGame.IsCheatActive(0))
		ischeatactive = TRUE;
	else
		ischeatactive = FALSE;

	if (FRONTEND.mFEPSaveGame.IsCheatActive(5))
		ischeat5active = TRUE;
	else
		ischeat5active = FALSE;

	if (mDisplayGoody)
	{
		LoadingGoodyPoll();

		SmoothResize(mSelectFade, 0, 0.1f, 0.001f);

		// if image loaded
		if (mGoodyState == GOODY_LOADED)
		{
			if (mImageZoom < 1.f)
			{
				SmoothResize(mImageZoom, 1.f, 0.3f, 0.005f);
			}

			if (!mManualControl)
			{
				// Update YPR stuff

				float time=PLATFORM.GetSysTimeFloat();

				float timed=time-mLastProcessTime;

				mTimeLeft-=timed;
				mLastProcessTime=time;

				mCurrentYaw+=mYawDelta*timed;
				mCurrentRoll+=mRollDelta*timed;
				mCurrentPitch+=mPitchDelta*timed;

				mMeshOri.MakeRotationF(((mCurrentYaw*3.14f)/180.0f),((mCurrentPitch*3.14f)/180.0f),((mCurrentRoll*3.14f)/180.0f));
			}

			mTextScrollOffset+=gFrontEndFrameStep/0.1f;

			if (mTextScrollOffset>mTextHeight+10.0f)
				mTextScrollOffset=-100.0f;

			if ((mCurrentGoodyType==GT_FMV) && (mImageZoom==1.0f))
			{
				// FMV

				int goodienum = get_goodie_number(mCX, mCY);

				char fname[256];
				get_goodie_fmv(fname,goodienum);

				if (CLIPARAMS.mMusic)
					MUSIC.Stop();
				FRONTEND.mFEPCommon.StopVideo();
				FMV.PlayFullscreen(fname,FALSE,get_goodie_fmv_localised(goodienum));
				FRONTEND.mFEPCommon.StartVideo(TRUE);
				if (CLIPARAMS.mMusic)
					MUSIC.PlaySelection(MUS_FRONTEND);

				mDisplayGoody = FALSE;
			}

			if ((mCurrentGoodyType==GT_LEVEL) && (mImageZoom==1.0f))
			{
				// Level

				int goodienum = get_goodie_number(mCX, mCY);

				FRONTEND.SetQuit(get_goodie_level(goodienum));
				FRONTEND.mSelectedLevel = get_goodie_level(goodienum);
				mDisplayGoody = FALSE;
			}

			if (mCurrentGoodyType==GT_CHEAT)
			{
				// Cheat

				int goodienum = get_goodie_number(mCX, mCY);

				mDisplayGoody = FALSE;
			}

			// the PC's mouse: drag to turn a mesh or move an image, and the wheel zooms an image
			if (mImageZoom >= 1.f)
			{
				switch (mCurrentGoodyType)
				{
				case GT_MESH:
					if (MOUSE_HELD[0])
					{
						mManualControl = TRUE;

						if (lastmousex != -1)
						{
							ButtonPressed(BUTTON_CAMERA_STRAFE_LEFT, float(MOUSE_X - lastmousex) * 0.15f);
							ButtonPressed(BUTTON_CAMERA_MOVE_FORAWRD, float(MOUSE_Y - lastmousey) * -0.15f);

							Clamp(mImageX, -0.8f, 0.8f);
							Clamp(mImageY, -0.8f, 0.8f);
						}

						lastmousex = MOUSE_X;
						lastmousey = MOUSE_Y;
					}
					else
						lastmousex = -1;
					break;

				case GT_IMAGE:
					if (MOUSE_HELD[0])
					{
						if (lastmousex != -1)
						{
							mImageX -= float(MOUSE_X - lastmousex) / (mImageZoom * 256.f);
							mImageY -= float(MOUSE_Y - lastmousey) / (mImageZoom * 256.f);

							Clamp(mImageX, -0.8f, 0.8f);
							Clamp(mImageY, -0.8f, 0.8f);
						}

						lastmousex = MOUSE_X;
						lastmousey = MOUSE_Y;
					}
					else
						lastmousex = -1;

					if (MOUSE_WHEEL < 0)
					{
						ButtonPressed(BUTTON_CAMERA_PITCH_DOWN, float(MOUSE_WHEEL) * 0.025f);
						MOUSE_WHEEL /= 2;
					}
					else if (MOUSE_WHEEL > 0)
					{
						ButtonPressed(BUTTON_CAMERA_PITCH_UP, float(MOUSE_WHEEL) * 0.025f);
						MOUSE_WHEEL /= 2;
					}
					break;
				}
			}
		}
	}
	else
	{
		SmoothResize(mSelectFade, 1.f, 0.1f, 0.001f);
		SmoothResize(mImageZoom, 0.f, 0.4f, 0.005f);

		if (mSelectFade == 1.f || mImageZoom == 0.f)
		{
			// delete resources
			FreeUpGoodyResources();

			FEMouseEdgeSlide(state, &mXPos, 5100.f);
		}
	}

	// the right mouse button goes back
	if ((state == FEPS_ACTIVE) && (MOUSE_BUTTONS[2].mDown))
	{
		MOUSE_BUTTONS[2].mDown = FALSE;
		ButtonPressed(BUTTON_FRONTEND_MENU_BACK, 1.f);
	}
}
#line 1717
//*********************************************************************************
void	CFEPGoodies::RenderPreCommon(float transition, EFrontEndPage dest)
{
/*	if (transition > 0.75f)
	{
		float	tr = 2.f;

		SINT	alpha = 255;

		float	scale = (tr * tr * tr);
		scale = 1.f + ((scale - 1.f) * 0.87f);

		float	x, y;

		float	magic_x;
		float	magic_y;

		magic_x = MAGIC_GOODIES_X;
		magic_y = MAGIC_GOODIES_Y;

		x = (PLATFORM.GetScreenWidth()  / 2.f) + (magic_x);
		y = (PLATFORM.GetScreenHeight() / 2.f) + (magic_y);

		x -= magic_x * (scale);
		y -= magic_y * (scale);

		CSPRITERENDERER::DrawColouredSprite(x, y,
											FO(970), FED.GetTexture(FET2_BATTLELINE_1),
											0xffffffff,
											scale, scale, SA_CENTRE);
	}*/

	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

#define	Y_CENTRE		320.f
#define	X_SEPARATION	45.f
#define	Y_SEPARATION	45.f
#define	X_OFFSET		148.f
#define	X_WIDTH			412.f

//*********************************************************************************
float	CFEPGoodies::GoodieX(int which)
{
	return X_OFFSET - mXPos + which * X_SEPARATION;
}

//*********************************************************************************
float	CFEPGoodies::GoodieY(int which)
{
	float cy = Y_CENTRE - (Y_SEPARATION * (NUM_GOODIES_Y - 1)) / 2;
	return cy + which * Y_SEPARATION;
}

//*********************************************************************************
#ifdef RESBUILDER
void CFEPGoodies::BuildAllResources(int iPlatform)
{
	for (int i = 0; i < TOTAL_GOODIES; i++)
	{
		CResourceAccumulator lAccumulator;

		// and write to disk.
		lAccumulator.WriteResources(-1000 - i, iPlatform);
	}
}
#endif

//*********************************************************************************

#if TARGET==PS2
sceVu0FMATRIX goodies_matProjSave;

void	goodies_prepare_projection(float zoom)
{
	RENDERINFO.GetProjection(&goodies_matProjSave);

	float	far_plane = 700.f;
	float	near_plane = 0.2f;

	RENDERINFO.SetProjection(near_plane, far_plane, near_plane*zoom, near_plane*zoom*0.75f);
}

void	goodies_restore_projection()
{
	RENDERINFO.SetProjection(&goodies_matProjSave);
}

#else
static D3DMATRIX	goodies_matProjSave;			// 00677ec0

static inline void	goodies_prepare_projection(float zoom)
{
	RENDERINFO.GetProjection(&goodies_matProjSave);

	float	far_plane = 700.f;
	float	near_plane = 0.2f;

	RENDERINFO.SetProjection(near_plane, far_plane, near_plane*zoom, near_plane*zoom*0.75f);
}

// the PC restores the saved matrix directly (CFEPGoodies::Render 0045f746)
static inline void	goodies_restore_projection()
{
	RENDERINFO.mProjection = *(SRenderMatrix *)&goodies_matProjSave;
	RENDERINFO.mProjectionChanged = true;
}
#endif

// the mesh renderer's untextured mode (CMeshRenderer's static at 00704e48; CFEPGoodies::Render 0045f237)
#define	R_NoTexture		((ERenderMode)8)

// the help line Render asks for (CFEPCommon::RenderHelpText's argument, 0045fef6; Stuart's FEH_GO_BACK)
#define	FEH_GO_BACK		((EFEHelp)1)

// the grid scrolls with the selection: columns outside GOODIES_SCROLL_LEFT/RIGHT (90, 570; .data, so retail rereads
// them after each call) push it back on screen (0045e1e7)

void	CFEPGoodies::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x0045e0d0
{
	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	RENDERINFO.SetFogEnabled(false);

	//*************************************************************************
	//**  Draw Goodies
	SINT	c0, c1;

	if (mSelectFade > 0)
	{
		SINT	select_alpha = MakeAlpha(mSelectFade);

		SINT alpha = SINT(RangeTransition(transition, 0.75f, 1.f) * 255.f);

		if (select_alpha<alpha)
			alpha=select_alpha;

		for (c0 = 0; c0 < NUM_GOODIES_X_INC; c0 ++)
		{
			float cx = GoodieX(c0);

			if (cx > 620.f)
				break;

			if (cx < 10.f)
				continue;

			// keep the selection on screen
			if (cx > GOODIES_SCROLL_RIGHT)
			{
				if (mCX >= c0)
					mCX = c0 - 1;
			}
			else if (cx < GOODIES_SCROLL_LEFT)
			{
				if (mCX <= c0)
				{
					mCX = c0 + 1;
					while (get_goodie_number(mCX, mCY) == -1)
						mCY = (mCY + 1) & 3;
				}
			}

			if (cx < 0.f)
				continue;

			for (c1 = 0; c1 < NUM_GOODIES_Y; c1 ++)
			{
				int goodienum=get_goodie_number(c0, c1);

				if (goodienum == -1)
				{
					// we're off the end
					continue;
				}

				DWORD col = 0xffffffff;

				if (alpha > 0)
				{
					float cy = GoodieY(c1);

					// the mouse
					if (mDisplayGoody != TRUE)
					{
						if ((cx >= GOODIES_SCROLL_LEFT) && (cx <= GOODIES_SCROLL_RIGHT))
						{
							if (FRONTEND.GetCursorStateInRect(cx - 22.5f, cy - 20.f, cx + 22.5f, cy + 20.f))
							{
								mCX = c0;
								mCY = c1;
							}

							if ((cx >= GOODIES_SCROLL_LEFT) && (cx <= GOODIES_SCROLL_RIGHT) && FRONTEND.MouseClickedInRect(cx - 22.5f, cy - 20.f, cx + 22.5f, cy + 20.f))
							{
								mCX = c0;
								mCY = c1;
								ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
							}
						}
					}

					// Draw Mask

					RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_ZERO);
					RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
					RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
					RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
					RENDERINFO.Apply();

					CSPRITERENDERER::DrawColouredSprite(cx, cy, DEPTH + FO(5), FED.GetTexture(FET_LEVEL_SELECT_MASK),BlendAlpha2(0xffffffff, alpha), 1.f, 1.f, SA_CENTRE);

					RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
					RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
					RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
					RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
					RENDERINFO.Apply();

					DWORD col;
					CTEXTURE	*gtex = FED.GetTexture(FET_LEVEL_SELECT_GLOW_BLUE);

					switch (get_goodie_state(c0, c1))
					{
						case GS_NEW:			col=0xFFFFFFFF;
												gtex = FED.GetTexture(FET_LEVEL_SELECT_GLOW_ORANGE);
												break;
						case GS_OLD:			col=0xFFFFFFFF; break;
						case GS_INSTRUCTIONS:	//col=0xFF808080; break;
						case GS_UNKNOWN:
						default:				col=0xFF404040; break;
					};

					float	val = float(FRONTEND.GetCounter()) * 0.02f + (float(c0 * 11) + float(c1 * 7)) * 0.114f;
					CSPRITERENDERER::DrawColouredSprite(cx, cy, DEPTH + FO(10), gtex,BlendAlpha2(col, alpha), 1.f, 1.f, SA_CENTRE, SPT_SCROLL_RIGHT, val);

					if ((goodietransition[goodienum]>0.0f) || ((c0 == mCX) && (c1 == mCY)))
					{
						SINT	alpha2 = SINT (RangeTransition(goodietransition[goodienum], 0, 0.5f) * 255.f);
						alpha2 = (alpha * alpha2) / 255;

						if ((c0 == mCX) && (c1 == mCY))
							goodietransition[goodienum]+=0.1f;
						else
							goodietransition[goodienum]-=0.1f;
						if (goodietransition[goodienum]>1.0f)
							goodietransition[goodienum]=1.0f;
						if (goodietransition[goodienum]<0.0f)
							goodietransition[goodienum]=0.0f;
					}

					switch (get_goodie_state(c0, c1))
					{
						case GS_NEW:			col=0xFFFF2020; break; // Overridden later
						case GS_OLD:			col=0xFFFFFFFF; break;
						case GS_INSTRUCTIONS:	col=0xFFFFFFFF; break;
						case GS_UNKNOWN:
						default:				col=0xFF808080; break;
					};

					if (get_goodie_state(c0, c1)==GS_NEW)
					{
						int v=128-(int) (fabsf(sinf(PLATFORM.GetSysTimeFloat()*2.0f))*-120.0f);

						col=0xFFFF8000 | ((v & 0xfe) << 7) | v;
					}

					if (true)
					{
						if (alpha > 0)
						{
							// Draw two metal rings
							BlendAlpha(col, alpha);

							float yaw		= goodietransition[goodienum] * PI * 0.25f;
							float scale   = 1.f + (goodietransition[goodienum] * 0.25f);

							CSPRITERENDERER::DrawColouredSprite(cx, cy, DEPTH, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET02),col, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f,  yaw);
							CSPRITERENDERER::DrawColouredSprite(cx, cy, DEPTH, FED.GetTexture(FET_SELECT_LEVEL_RING_BRACKET01),col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, -yaw);
						}
					}
					else	// Stuart's dead branch, kept: its temporaries number the text loop's y += height sums (0045fe68)
					{
						// draw the transitioning rings
						if (transition > 0.0f)
						{
							float tr = 1.f - transition;

							float yaw = (PI * 0.67f * transition);
							float scale = (1.6f - 0.1f) * tr + 0.1f;

							float x = cx + (285.f - cx) * tr;
							float y = cy + (225.f - cy) * tr;

							CSPRITERENDERER::DrawColouredSprite(x, y, DEPTH, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2),col, scale        , scale        , SA_CENTRE, SPT_NONE, 1.f, -yaw);
							CSPRITERENDERER::DrawColouredSprite(x, y, DEPTH, FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL1),col, scale * 1.75f, scale * 1.75f, SA_CENTRE, SPT_NONE, 1.f,  yaw);
						}
					}
				}
			}
		}
	}

	//*************************************************************************
	//**  Draw goodie state
#define	STATE_NAME_X	130.f
#define STATE_NAME_Y	120.f
#define	DESCRIPTION_NAME_X	130.f
#define DESCRIPTION_NAME_Y	145.f

	SINT alpha = SINT(RangeTransition(transition, 0.75f, 1.f) * 255.f);
	DWORD col = 0xffffffff;
	WCHAR		title_text[200];
	WCHAR		body_text[200];

	int which_goodie = get_goodie_number(mCX, mCY);

	BlendAlpha(col, alpha);
	col = 0xffffffff;
	BlendAlpha(col, alpha);

	if (get_goodie_state(mCX, mCY) >= GS_NEW)
		wcscpy(title_text,FET.GetText(FEX_GOODY_UNLOCKED));
	else
		wcscpy(title_text,FET.GetText(FEX_GOODY_TO_UNLOCK));

	if (which_goodie<78)
	{
		if (get_goodie_state(mCX, mCY) == GS_INSTRUCTIONS)
		{
			goodies[which_goodie].GetMethod(body_text);
		}
		else
		{
			// Title
			get_goodie_title(body_text,which_goodie);
		}
	}
	else
	{
		if (get_goodie_state(mCX, mCY) < GS_NEW) // No "to unlock:"
			wcscpy(title_text,ToWCHAR(""));
		wcscpy(body_text,ToWCHAR("")); // No text for later ones
	}

	if (get_goodie_state(mCX,mCY)!=GS_UNKNOWN)
	{
		// draw the "to unlock" or "unlocked"
		PLATFORM.Font( FONT_NORMAL )->DrawTextDynamic(STATE_NAME_X, STATE_NAME_Y  , FO(150), 0.7f, 1.f, col, title_text, PLATFORM.GetSysTimeFloat() - mTimerGoody);

		// and underneath we either get what we have to do to unlock or the title
		PLATFORM.Font( FONT_NORMAL )->DrawTextDynamic(DESCRIPTION_NAME_X, DESCRIPTION_NAME_Y  , FO(150), 0.7f, 1.f, col, body_text, PLATFORM.GetSysTimeFloat() - mTimerGoody);
	}

	//*************************************************************************
	//**  Draw whatever the goody is

	if ((mImageZoom > 0.f) && (mGoodyState == GOODY_LOADED))
	{
		switch (mCurrentGoodyType)
		{
		case GT_IMAGE:

			// Image

			if (mCurrentGoodyTexture)
			{
				DWORD col = 0xffffffff;

				float scale = mImageZoom * 0.5f;

				int i;

				float x = 320 - ((mCurrentGoodyTexture[0]->mWidth  * 0.5f) * mImageX * scale);
				float y = 240 - (mCurrentGoodieHeight * mImageY * scale * 0.5f);

				// but if mImageZoom < 1.f, we're actually just bringing the goodie up rather than interacting with it.
				// So let's blend the position towards where the selector square was in the selector grid.
				if (mImageZoom < 1.f)
				{
					float select_x = GoodieX(mCX);
					float select_y = GoodieY(mCY);

					x = x * mImageZoom + select_x * (1.f - mImageZoom);
					y = y * mImageZoom + select_y * (1.f - mImageZoom);
				}

				RENDERINFO.SetFogEnabled(false);

				int heightdone=0;

				x-=mCurrentGoodyTexture[0]->mWidth*scale*0.5f;
				y-=mCurrentGoodieHeight*scale*0.5f;
				for (i=0;i<mNumCurrentGoodieTextures;i++)
				{
					float v1,v2;
					if (i==0)
					{
						v1=0.0f;
						v2=1.0f;
					}
					else
					{
						v1=0.5f/mCurrentGoodyTexture[i]->GetHeight();
						v2=1.0f;
					}

					float xscale=scale;
					float yscale=scale;

					if (i==mNumCurrentGoodieTextures-1)
					{
						// Last texture may be larger than needed
						yscale*=(mCurrentGoodieHeight-heightdone)/mCurrentGoodyTexture[i]->GetHeight();
						v2*=(mCurrentGoodieHeight-heightdone)/mCurrentGoodyTexture[i]->GetHeight();
					}

					CSPRITERENDERER::DrawSprite(x,y,FO(50),mCurrentGoodyTexture[i],SA_TOP_LEFT,SPT_NONE,1.0f,0.0f,col,scale,scale,0.0f,1.0f,v1,v2);
					y+=(mCurrentGoodyTexture[i]->GetHeight()-2)*scale;
					heightdone+=mCurrentGoodyTexture[i]->GetHeight()-2;
				}
			}

			break;
		case GT_MESH:

			// Mesh

			if (mCurrentGoodyMesh)
			{
				// Background image

				DWORD col = 0xffffffff;

				float x=320;
				float y=240;
				float xscale=(mImageZoom*640.0f)/mCurrentGoodyTexture[0]->mWidth;
				float yscale=(mImageZoom*480.0f)/mCurrentGoodyTexture[0]->GetHeight();

				// but if mImageZoom < 1.f, we're actually just bringing the goodie up rather than interacting with it.
				// So let's blend the position towards where the selector square was in the selector grid.
				if (mImageZoom < 1.f)
				{
					float select_x = GoodieX(mCX);
					float select_y = GoodieY(mCY);

					x = x * mImageZoom + select_x * (1.f - mImageZoom);
					y = y * mImageZoom + select_y * (1.f - mImageZoom);
				}

				RENDERINFO.SetFogEnabled(false);

				// Punch back Z-Buffer and draw shadowed background

				RENDERINFO.SRS(D3DRS_ZFUNC,D3DCMP_ALWAYS);
				RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE,FALSE);

				CSPRITERENDERER::DrawSprite(x,y,1.0f,mCurrentGoodyTexture[0],SA_CENTRE,SPT_NONE,1.0f,0.0f,0xff808080,xscale,yscale,0.0f,1.0f,0.0f,1.0f);

				RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE,TRUE);
				RENDERINFO.SRS(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);

				if (mImageZoom>=0.8f)
				{
					// Draw mesh

					if (OLD_GOODIE_MESH_SYSTEM.mValue)
						STATE.UseDefault();

					mMeshDistance=mCurrentGoodyMesh->mBoundingBox->mRadius*1.5f;

					if (which_goodie==76)
					{
						FED.mCamera.mPos = FVector(0.0f,0.0f,0.0f);
						FED.mCamera.mOrientation.MakeRotationF(0.0f,0.0f,0.0f);
						goodies_prepare_projection(0.5f);
					}
					else
					{
						FED.mCamera.mPos = FVector(0.0f,-mMeshDistance,-(mMeshDistance*0.7f));
						FED.mCamera.mOrientation.MakeRotationF(0.0f,0.5f,0.0f);
						goodies_prepare_projection(1.0f);
					}
					FRONTEND.UpdateCamera();

					float r=PLATFORM.GetSysTimeFloat()/10.0f;

					FVector pos=ZERO_FVECTOR;
					pos.X=-mCurrentGoodyMesh->mBoundingBox->mOrigin.X;
					pos.Y=-mCurrentGoodyMesh->mBoundingBox->mOrigin.Y;
					pos.Z=-mCurrentGoodyMesh->mBoundingBox->mOrigin.Z;
					FMatrix ori=mMeshOri;

					// Draw drop-shadow

					FVector dropshadow=FVector(0.1f*mMeshDistance,0.02f*mMeshDistance,0.0f);

					RENDERINFO.SetAmbient(0x00000000);
					RENDERINFO.SetLightEnable(0,TRUE);
					RENDERINFO.SetLightEnable(1,FALSE);
					CLight		blacklight;

					blacklight.mDiffuseR=0;
					blacklight.mDiffuseG=0;
					blacklight.mDiffuseB=0;
					blacklight.mDir.Set(1.0f,0.0f,0.0f);
					blacklight.mType=LIGHT_DIRECTIONAL;
					RENDERINFO.SetLight(0,&blacklight);

					FED.mCamera.mPos+=dropshadow;
					FRONTEND.UpdateCamera();

					CMESHRENDERER::SetRenderMode(R_NoTexture);
					// Just write to Z-Buffer
					LT.STS(1, D3DTSS_COLOROP,	D3DTOP_SELECTARG1);
					LT.STS(1, D3DTSS_COLORARG1,	D3DTA_CURRENT);
					LT.STS(1, D3DTSS_ALPHAOP,	D3DTOP_MODULATE);
					LT.STS(1, D3DTSS_ALPHAARG1,	D3DTA_TFACTOR);
					LT.STS(1, D3DTSS_ALPHAARG2,	D3DTA_CURRENT);
					LT.SRS(D3DRS_TEXTUREFACTOR,	0);
					RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE,TRUE);
					RENDERINFO.SRS(D3DRS_ALPHATESTENABLE,FALSE);
					CMESHRENDERER::Render(&pos,&ori,mCurrentGoodyMesh,NULL,NULL,0,FALSE,0);
					RENDERINFO.SRS(D3DRS_ALPHATESTENABLE,TRUE);
					LT.STS(1, D3DTSS_COLOROP,	D3DTOP_DISABLE);
					LT.STS(1, D3DTSS_ALPHAOP,	D3DTOP_DISABLE);
					LT.STS(1, D3DTSS_COLORARG1,	D3DTA_TEXTURE);
					LT.STS(1, D3DTSS_COLORARG2,	D3DTA_CURRENT);
					LT.STS(1, D3DTSS_ALPHAARG1,	D3DTA_TEXTURE);
					LT.STS(1, D3DTSS_ALPHAARG2,	D3DTA_CURRENT);
					CMESHRENDERER::SetRenderMode(R_Normal);

					// Draw real background

					RENDERINFO.SetLightingEnabled(FALSE);
					RENDERINFO.SRS(D3DRS_ZENABLE,TRUE);

					CSPRITERENDERER::DrawSprite(x,y,1.0f,mCurrentGoodyTexture[0],SA_CENTRE,SPT_NONE,1.0f,0.0f,0xFFFFFFFF,xscale,yscale,0.0f,1.0f,0.0f,1.0f);

					// Punch back Z-Buffer (again!)

					RENDERINFO.SRS(D3DRS_ZFUNC,D3DCMP_ALWAYS);
					RENDERINFO.SRS(D3DRS_ALPHATESTENABLE,FALSE);
					RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE,TRUE);

					CSPRITERENDERER::DrawSprite(x,y,1.0f,mCurrentGoodyTexture[0],SA_CENTRE,SPT_NONE,1.0f,0.0f,0x00000000,xscale,yscale,0.0f,1.0f,0.0f,1.0f);

					RENDERINFO.SRS(D3DRS_ALPHATESTENABLE,TRUE);
					RENDERINFO.SRS(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);

					// Light Setup
					CLight		light;

					RENDERINFO.SetAmbient(0x00808080);

					FVector sunlight = FVector( -5000, 5000, 1500.0f );
					sunlight.Normalise();

					RENDERINFO.SetAmbient(0);

					FVector3	col1 = FVector3(0.8f, 0.4f, 0.2f);
					FVector3	col2 = FVector3(0.2f, 0.4f, 0.8f);
					FVector3	col3 = FVector3(0.4f, 0.4f, 0.4f);

					col1 *= 0.6f;
					col2 *= 0.6f;
					col3 *= 0.6f;

					light.Init( LIGHT_DIRECTIONAL, sunlight, col1.X, col1.Y, col1.Z);
					RENDERINFO.SetLight( 0, &light );
					RENDERINFO.SetLightEnable( 0, TRUE );

					sunlight.X = -sunlight.X;
					light.Init( LIGHT_DIRECTIONAL, sunlight, col2.X, col2.Y, col2.Z);
					RENDERINFO.SetLight( 1, &light );
					RENDERINFO.SetLightEnable( 1, TRUE );

					sunlight = FVector(0, 1000.f, 200.f);
					sunlight.Normalise();
					light.Init( LIGHT_DIRECTIONAL, sunlight, col3.X, col3.Y, col3.Z);
					RENDERINFO.SetLight( 2, &light );
					RENDERINFO.SetLightEnable( 2, TRUE );
					RENDERINFO.SetLightingEnabled(TRUE);

					FED.mCamera.mPos-=dropshadow;
					FRONTEND.UpdateCamera();

					CMESHRENDERER::Render(&pos,&ori,mCurrentGoodyMesh,NULL,NULL,0,FALSE,0);

					RENDERINFO.SetLightingEnabled(FALSE);
					RENDERINFO.SRS( D3DRS_ALPHABLENDENABLE, TRUE );
					RENDERINFO.SRS( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
					RENDERINFO.SRS( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );

					goodies_restore_projection();
				}

				// Put "real" Z-Buffer in place over the top

				RENDERINFO.SRS(D3DRS_ZFUNC,D3DCMP_ALWAYS);
				RENDERINFO.SRS(D3DRS_ALPHATESTENABLE,FALSE);
				RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE,TRUE);

				// Fade mesh in

				if ((mImageZoom>=0.8f) && (mImageZoom<1.0f))
				{
					int alpha=(int) (255-((mImageZoom-0.8f)*255)/0.2f);
					col=(alpha<<24) | 0x00FFFFFF;
				}
				else
					col=0x00000000;

				CSPRITERENDERER::DrawSprite(x,y,FO(50),mCurrentGoodyTexture[0],SA_CENTRE,SPT_NONE,1.0f,0.0f,col,xscale,yscale,0.0f,1.0f,0.0f,1.0f);

				RENDERINFO.SRS(D3DRS_ALPHATESTENABLE,TRUE);
				RENDERINFO.SRS(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
			}


			break;

		case GT_FMV:

			// FMV

			{
				int alpha=(int) (mImageZoom*255.0f);

				DWORD col = alpha<<24;

				float scale = mImageZoom * 0.5f;

				CTEXTURE *tex=FED.GetTexture(FET2_WHITE);

				float x = 320;
				float y = 240;

				float xscale=640.0f/tex->mWidth;
				float yscale=480.0f/tex->mHeight;

				// but if mImageZoom < 1.f, we're actually just bringing the goodie up rather than interacting with it.
				// So let's blend the position towards where the selector square was in the selector grid.
				if (mImageZoom < 1.f)
				{
					float select_x = GoodieX(mCX);
					float select_y = GoodieY(mCY);

					x = x * mImageZoom + select_x * (1.f - mImageZoom);
					y = y * mImageZoom + select_y * (1.f - mImageZoom);
				}

				RENDERINFO.SetFogEnabled(false);

				CSPRITERENDERER::DrawSprite(x,y,FO(0),tex,SA_CENTRE,SPT_NONE,1.0f,0.0f,col,xscale,yscale,0.0f,1.0f,0.0f,1.0f);
			}

		};

		if ((mCurrentGoodyType!=GT_FMV) && (mCurrentGoodyTexture) && (get_goodie_num_texts(which_goodie)>0))
		{
			// Draw text box

			float clampedzoom=mImageZoom;

			if (clampedzoom>1.0f)
				clampedzoom=1.0f;

			float x=250;
			float y=50;
			float xsize=320;
			float ysize=100;

			alpha=(int) (clampedzoom*128.0f);
			col=(alpha<<24);
			float xoffset=100-(sinf(clampedzoom*3.14f*0.5f)*100);

			CTEXTURE *tex=FED.GetTexture(FET2_WHITE);

			x+=xoffset;

			CSPRITERENDERER::DrawSprite(x,y,FO(0),tex,SA_TOP_LEFT,SPT_NONE,1.0f,0.0f,col,(float) xsize/(float) tex->mWidth,(float) ysize/(float) tex->mHeight,0.0f,1.0f,0.0f,1.0f);

			x+=4;
			y+=4;

			float textfadetop=y;
			float textfadebottom=y+ysize-8;

			y-=mTextScrollOffset;

			mTextHeight=0;

			// Draw each paragraph of text

			for (int i=0;i<get_goodie_num_texts(which_goodie);i++)
			{
				// Word-wrap text

				SWordWrapFill fill;

				WCHAR buffer[8192];

				get_goodie_text(buffer,which_goodie,i);

				EFontType font=FONT_SMALL;

				if (i==0)
					font=FONT_NORMAL;

				if (wcslen(buffer)>0)
				{
					int rows=PLATFORM.Font(font)->WordWrap(&fill,buffer,xsize-8);
					int height=PLATFORM.Font(font)->GetHeight();

					// Draw

					for (int j=0;j<rows;j++)
					{
						alpha=(int) (clampedzoom*255.0f);

						float alphafade=0.0f;
						if ((y-height)<textfadetop)
							alphafade=((float) (textfadetop-(y-height)))/(float) height;
						if ((y+(height*2))>textfadebottom)
							alphafade=((float) ((y+(height*2))-textfadebottom))/(float) height;

						if (alphafade<0.0f)
							alphafade=0.0f;
						if (alphafade>1.0f)
							alphafade=1.0f;

						alpha=(int) ((float) alpha*(1-alphafade));

						col=(alpha<<24) | 0x00FFFFFF;

						PLATFORM.Font(font)->DrawText(x,y,col,fill.c[j],0x10); // 0045fe3d: keep fractional Y (0054018c skips floor).
						y+=height;
						mTextHeight+=height;
					}

					y+=height; // Paragraph break
					mTextHeight+=height;
				}
			}
		}

	}

	//***********************************************
	//** Title Bar

	if (mGoodyState != GOODY_LOADED)
		FRONTEND.DrawTitleBar(FET.GetText(FEX_GOODIES2), transition, dest);

	//***********************************************
	//** Help Text
	FRONTEND.GetCommonPage()->RenderHelpText(FEH_GO_BACK, transition);

	//*************************************************************************
	// BEA logo
	float tr = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(tr);
}

void	CFEPGoodies::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x0045ffa0
{
	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;

	ResetGoodyState();
}

//*********************************************************************************
void	CFEPGoodies::EvaluateDest()
{
	float slide_factor = floorf(X_WIDTH / float(NUM_GOODIES_X_INC - 1));
	
	mXDest = float(mCX) * X_SEPARATION - (float(mCX) * slide_factor);
}
