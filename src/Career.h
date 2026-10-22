// Career.h: interface for the CCareer class.
//
//////////////////////////////////////////////////////////////////////

#ifndef CAREER_INCLUDE
#define CAREER_INCLUDE

#include "membuffer.h"
#include "array.h"
#include "sptrset.h"
#include "player.h"

#define BASE_THINGS_EXISTS_SIZE 288  // must be a multiplier of 32
#define BASE_THINGS_EXISTS_MEM_REQ BASE_THINGS_EXISTS_SIZE >> 5  //(divide by 32)
#define MAX_NODES 100
#define MAX_LINKS MAX_NODES * 2
#define MAX_CAREER_SLOTS 32

extern int CAREER_VERSION ;
class CCareerNode;

#define MAX_NUM_GOODIES 300

#define NUM_LANGUAGES	5

#define OPTIONS_SIZE	0x56

extern	int num_nodes;

class CGrade
{
public:

	CGrade(char g) : grade(g) {}
	CGrade(WCHAR g) { grade = (char)g ; }
	
	BOOL operator >= (const CGrade right) const { if (grade == 'S') return TRUE ;  if (right.grade == 'S') return FALSE ; return grade <= right.grade ; }
	BOOL operator == (const CGrade right) const { return grade == right.grade ; }
	char grade ;
};

enum EGoodieState
{
	GS_UNKNOWN,
	GS_INSTRUCTIONS,
	GS_NEW,
	GS_OLD
};


class CGoodie
{
public:

	CGoodie() : mState(GS_UNKNOWN) {}
	EGoodieState mState	;
};


enum ECNLinkType
{
	CN_NOT_COMPLETE,
	CN_COMPLETE,
	CN_COMPLETE_BROKEN,
};


class CCareerNodeLink
{
public:
	CCareerNodeLink() : mLinkType(CN_NOT_COMPLETE), mToNode(-1) {}
	CCareerNode* GetToNode() ;
	ECNLinkType  mLinkType;
	int mToNode ;
};


class CCareerNode
{
public:

	CCareerNode();
	void	Blank();
	BOOL DoesBaseThingExist(int offset);
	void SetBaseThingExistTo(int offset, BOOL val) ;
	SPtrSet<CCareerNode> GetChildNodes() ;
	SPtrSet<CCareerNodeLink> GetChildLinks();

	SPtrSet<CCareerNodeLink> GetParentLinks() ;

	BOOL mIsStartOfNewIsland ; // not used but left in cos don't want to change career
	BOOL mComplete;

	int mLowerLink ;  
	int mHigherLink ; 
	int mWorldNumber;
	int mBaseThingsExists[BASE_THINGS_EXISTS_MEM_REQ] ;
	int mNumAttempts;

	// Level information
	float	mRanking;		
};

// Level Structure
#define NUM_LEVELS 43
struct	CLevelStructure
{
	int mData[NUM_LEVELS][5];

};


// Base Career Class
class CCareer  
{
public:
	CCareer();
	CCareerNode* GetNodeFromWorldNo(int world_num) ;
	BOOL DoesBaseThingExist(int world_number, int offset);
	BOOL IsWorldLater(SINT inCurrentWorld, SINT inDiesOn);
	BOOL Later(CCareerNode *inDiesOnNode, CCareerNode *inCurrentNode);
	void Update() ;
#if 1	// the retail PC build saves through the console path (CCareer::Load 00421200)
	// Functions to copy career data to and from an external save game buffer
	BOOL	Load(char *source, bool suggest_next_level); // and loading can fail if it's the wrong version number. If suggest_next_level, it sets your next selected level to something sensible
	void	Save(char *dest);
	void	SaveAndMarkInProgress(char *dest);

	// Since we're using an external buffer, we'd better know what size we should make it.
	static int SizeOfSaveGame();
#else
	BOOL	Load(char *source, bool suggest_next_level) {return(FALSE);};
	void	Save(char *dest) {};
	void	Load() ;
	void	Save() ;
	static int SizeOfSaveGame() { return(0); };
#endif
	void Blank() ;
	int	GetNumKilled(EKilledType type) ;
//	void Test() ;
	CCareerNode* GetNode(int num) { if (num < 0) return NULL ; return &mNode[num] ; }
	CCareerNodeLink* GetLink(int num) { if (num <0) return NULL ;return &mNodeLink[num] ; } 
	EGoodieState	GetGoodieState(int goodie_num) { return mGoodies[goodie_num].mState; }
	void			SetGoodieState(int goodie_num, EGoodieState state) { mGoodies[goodie_num].mState = state ; }
	
	// For the Debriefing Screen
	SINT			GetAndResetGoodieNewCount();
	BOOL			GetAndResetFirstGoodie();
	SINT			CountGoodies();

	void			Log();
	CLevelStructure *GetLevelStructure();
	WCHAR GetGradeFromRanking(float f);

	// Check if a career is in progress (i.e. at least one completed level)
	BOOL InProgress() { return( mCareerInProgress ); }
	void SetInProgress() {mCareerInProgress = TRUE;}

	CSArray<int, MAX_CAREER_SLOTS>&	GetSlots()	{ return mSlots; }

	BOOL GetSlot(int num);
	void SetSlot(int num, BOOL val);

	BOOL  GetIsGod(int player) { return mIsGod[player] ;}
	void  SetIsGod(int player, BOOL val) { mIsGod[player] = val ; }



	float GetSoundVolume() { return mSoundVolume ; }
	void  SetSoundVolume(float val) { mSoundVolume = val ; }

	float GetMusicVolume() { return mMusicVolume ; }
	void  SetMusicVolume(float val) { mMusicVolume = val ; }

	int  GetControllerConfigurationNum(int player) { return mControllerConfigurationNum[player] ;}
	void  SetControllerConfigurationNum(int player, int val) { mControllerConfigurationNum[player] = val ; }


	BOOL  GetInvertYAxis(int player) { return mInvertYAxis[player] ; }
	BOOL  GetInvertJetYAxis(int player) { return mInvertJetYAxis[player] ; }	// retail PC
	void  SetInvertYAxis(int player, BOOL val) { mInvertYAxis[player] = val ; }
	void  SetInvertJetYAxis(int player, BOOL val) { mInvertJetYAxis[player] = val ; }	// retail PC (the controls screen's toggle, 004560b7)
	BOOL  GetVibration(int player) { return mVibration[player] ; }
	void  SetVibration(int player, BOOL val) { mVibration[player] = val ; }
	BOOL  IsEpisodeAvailable(SINT ep) ;

	// The screen position (CFEPScreenPos) is kept in the top bytes of the first two kill counters, biased by 128.
	// Which counter holds which axis is by convention (CFEPOptions fills an x, y pair from them), not proven.
	int		GetScreenPosX();
	int		GetScreenPosY();
	void	SetScreenPosX(int x);
	void	SetScreenPosY(int y);

	// the PC options written after the career and key bindings (OPTIONS_SIZE bytes)
	char	*WriteOptions(char *dest);
	char	*ReadOptions(char *source);

	SINT  mPendingExtraGoodies;
protected:



	void UpdateBaseWorldExistsStuffForNode(CCareerNode* node) ;
	void UpdateGoodieStates();
	void ReCalcLinks();
	void PrintNodesStatus();
	void UpdateThingsKilled();

	CSArray<CCareerNode, MAX_NODES> mNode ;
	CSArray<CCareerNodeLink, MAX_LINKS> mNodeLink ;
	CSArray<CGoodie, MAX_NUM_GOODIES> mGoodies ;
	CSArray<int, TK_TOTAL>	mKilledThings;
	CSArray<int, MAX_CAREER_SLOTS>	mSlots;

	BOOL	mCareerInProgress;

	float	mSoundVolume;
	float   mMusicVolume;

	CSArray<BOOL,2> mIsGod;
	CSArray<BOOL,2>	mInvertJetYAxis;	// +0x249c, retail PC (name open): CPlayer::ReceiveButtonAction inverts the pitch with it
									// when the walker is off the ground (004d31ec); also the controls screen and mouse look
	CSArray<BOOL,2> mInvertYAxis;
	CSArray<BOOL,2> mVibration;
	CSArray<int, 2>	mControllerConfigurationNum;		// not used yet but might be later
};


extern CCareer CAREER ;

#endif



