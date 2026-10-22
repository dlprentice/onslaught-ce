// ResourceAccumulator.h: interface for the CResourceAccumulator class.
//
//////////////////////////////////////////////////////////////////////

#ifndef RESOURCEACCUMULATOR_H
#define RESOURCEACCUMULATOR_H

#include "Mesh.h"
#include "Texture.h"

#define ACCUMULATOR_MESH_LIMIT		100
#define ACCUMULATOR_TEXTURE_LIMIT	1000

#define SERIALIZE_CLEAR(x) { if (x) *((DWORD *) &(x))=0x0000DEAD; }

// Resource flags

#define RES_ALLOWPAGING				(1<<0)
#define RES_NOTONPS2				(1<<1)
#define RES_NOTONXBOX				(1<<2)
#define RES_BASESET					(1<<3)
#define RES_CROPSKYTEXTURE			(1<<4)
#define RES_DONT_COMPRESS			(1<<5)
#define RES_DONTFIXALPHA			(1<<6)
#define RES_OPTIONAL				(1<<7)
#define RES_DOWNSAMPLE				(1<<8)
#define RES_LOADINGSET				(1<<9)
#define RES_SHADOWPALETTE			(1<<10)
#define RES_DONT_SWIZZLE			(1<<11)
#define RES_INHIBITPAGING			(1<<12)
#define RES_NOTONCONSOLE			(RES_NOTONPS2 | RES_NOTONXBOX)

class CResFileHeader
{	
public:
	char	mID[16];
	float	mVersion;
	UINT	mCount;
};
/*
class CResourceFileStat
{
public:
	CResourceFileStat()
	{
		strcpy(mName,"");
		mSize=0;
	}

	static void
	static void AddItem(char )
	static void Clear();
	
	char				mName[4096];
	UINT				mSize;
	CResourceFileStat	*mNext;
private:
	static CResourceFileStat *mFirst;
	static char				 mCurrentPath[4096];
};
*/
class CResourceAccumulator  
{
public:
	CResourceAccumulator();
	~CResourceAccumulator();

#ifdef RESBUILDER
	void AddMesh(CMESH *apMesh,DWORD flags=0);
	void AddTexture(CTEXTURE *apTexture,DWORD flags=0);
	//void AddJCLTexture( const CJCLTexture * apJCLTexture );
	void WriteResources(SINT aLevel,int target=TARGET);
#endif
	// you can read them out of a membuffer now.
	// retail PC: a third argument; when set only the version, target, size and mesh chunks are read (004d7612),
	// which CFEPGoodies uses (0045cc80).
	static void ReadResources(SINT level, CMEMBUFFER *membuffer = NULL, BOOL meshesonly = FALSE);	// 004d7200

	UINT GetResourceID() { return(mResourceID++); };
	
	int GetTargetPlatform() { return(mTarget); };

	int	GetTargetLevel() { return(mTargetLevel); };

	static void GetFileName(char *dest,SINT aLevel, int target);	// 004d6f70

	static int GetResourceFileHandle() { return(mResourceFileHandle); };
	static int GetMasterPageFileHandle() { return(mMasterPageFileHandle); };
	static char *GetResourceFileName() { return(mResourceFileName); };
	
	static SINT		GetLastGameLevelLoaded()			{ return mLastGameLevelLoaded; } // this ignores loading of loading or base resources.

	class CChunker *GetOutputPageFile() { return(mPageFile); };

protected:
	SINT			mNumMeshes;
	SINT			mNumTextures;
	int				mResourceID;
	int				mTarget;
	int				mTargetLevel;
	static int		mResourceFileHandle;
	static int		mMasterPageFileHandle;
	static char		mResourceFileName[256];
	static SINT		mLastGameLevelLoaded;		// 006317cc (CGame::IsRunningResources 00472650)
	class CChunker  *mPageFile;
	
	CMESH		  *	mpMesh[ACCUMULATOR_MESH_LIMIT];
	CTEXTURE	  *	mpTexture[ACCUMULATOR_TEXTURE_LIMIT];
	DWORD			mMeshFlags[ACCUMULATOR_MESH_LIMIT];
	DWORD			mTextureFlags[ACCUMULATOR_TEXTURE_LIMIT];
	//CJCLTexture   *	mpJCLTexture[ ACCUMULATOR_JCLTEXTURE_LIMIT ];
};

#endif // RESOURCEACCUMULATOR_H
