// ResourceAccumulator.cpp: implementation of the CResourceAccumulator class.
//
//////////////////////////////////////////////////////////////////////

#include "common.h"

#include "ResourceAccumulator.h"
#include "MemBuffer.h"
#include "chunker.h"
#include "console.h"
#include "imposter.h"
#include "staticshadows.h"
#include "gamedisc.h"
#include "engine.h"
#include "vertexshader.h"
#include "platform.h"
#include "cliparams.h"
#include "heightfield.h"
#include "world.h"
#include "surf.h"
#include "text.h"
#include "kempycube.h"
#include "map.h"
#include <stdio.h>
#include "frontend.h"

// an interesting constant.
#if TARGET != PS2
// This is the amount of texture memory we have to leave available on the Xbox that the resource accumulator
// doesn't have any power over.
int UNACCOUNTED_TEXTURE_MEMORY = 5 * 1024 * 1024 + 800 * 1024;
int UNACCOUNTED_TEXTURE_MEMORY_MULTI = 7 * 1024 * 1024; // + 800 * 1024;
int XBOX_TEXTURE_DATA_MEMORY = 16.5 * 1024 * 1024;
#endif

int CResourceAccumulator::mResourceFileHandle=-1;
int CResourceAccumulator::mMasterPageFileHandle=-1;
char CResourceAccumulator::mResourceFileName[256];
SINT CResourceAccumulator::mLastGameLevelLoaded=-100;

// Data used by the resource tools 

char resdata_tag[8]="AYADATA";
int resfile_version = 103;
int resfile_ctexturesize = sizeof(CTEXTURE);
int resfile_cmeshsize = sizeof(CMesh);
int resfile_cmeshpartsize = sizeof(CMeshPart);
int resfile_hfsize = sizeof(CHeightField);
#if TARGET!=PS2
int resfile_vshadersize= sizeof(CVertexShader);
#else
int resfile_vshadersize= 0;
#endif
char resdata_end[4]="END";

// End of resource tool data

CResourceAccumulator::CResourceAccumulator()
{
	int i;
	mNumMeshes = 0;
	for( i=0; i<ACCUMULATOR_MESH_LIMIT; ++i )
	{
		mpMesh[ i ] = NULL;
		mMeshFlags[i]=0;
	}

	mNumTextures = 0;
	for( i=0; i<ACCUMULATOR_TEXTURE_LIMIT; ++i )
	{
		mpTexture[ i ] = NULL;
		mTextureFlags[i]=0;
	}

	mLastGameLevelLoaded=-100;
}

CResourceAccumulator::~CResourceAccumulator()
{
}
#ifdef RESBUILDER
void CResourceAccumulator::AddMesh( CMESH * apMesh,DWORD flags )
{
	// check for duplicate:
	int i;
	for( i=0; i<mNumMeshes; ++i )
	{
		if( strcmp( mpMesh[ i ]->mName, apMesh->mName ) == 0 )
		{
			if (mMeshFlags[i]!=flags)
			{
				char buffer[256];
				sprintf(buffer,"Mesh %s accumulated twice with different flags\n",apMesh->mName);
				TRACE(buffer);
				
				mMeshFlags[i]&=flags;
			}

			return;
		}
	}

	// add to array:
	mpMesh[mNumMeshes]=apMesh;
	mMeshFlags[mNumMeshes]=flags;
	mNumMeshes++;

	// add textures:
	apMesh->AccumulateResources(this,flags);
}

void CResourceAccumulator::AddTexture( CTEXTURE * apTexture, DWORD flags )
{
	if (!apTexture)
		return;

	if (strlen(apTexture->GetName())==0)
	{
		TRACE("Unnamed texture accumulated...!\n");
		return;
	}

	// check for duplicate:
	int i;
	for( i=0; i<mNumTextures; ++i )
	{
		if (mpTexture[i]->Equals(apTexture))
		{
			if (mTextureFlags[i]!=flags)
			{
				char buffer[256];
				sprintf(buffer,"Texture %s accumulated twice with different flags\n",apTexture->GetName());
				TRACE(buffer);

				BOOL baseset=FALSE;

				if (((flags & RES_BASESET)!=0) || ((mTextureFlags[i] & RES_BASESET)!=0))
					baseset=TRUE;
				
				mTextureFlags[i]&=flags;

				if (baseset)
					mTextureFlags[i]|=RES_BASESET;
			}
			
			return;
		}
	}

	// add to array:
	mpTexture[mNumTextures]=apTexture;
	mTextureFlags[mNumTextures]=flags;
	mNumTextures++;
}
#endif
//---------------------------------------------------------------------------

// the 3 non-level resource filenames are simple to create.
static void basic_resource_filename(char *dest, char *middle, char *targetname)
{
	// but the main reason this is a separate function is because of the Xbox
#if TARGET == XBOX
	// first we must take into account our base path.
	strcpy(dest, CLIPARAMS.mBasePath);
	strcat(dest, "\\");

	// but then we must sneak it onto the UD.
	dest[0] = 'Z';
#else
	// the paths are relative.
	dest[0] = 0;
#endif
	strcat(dest, middle);
	strcat(dest, targetname);
	strcat(dest, ".aya");
}

void CResourceAccumulator::GetFileName(char *dest,SINT aLevel, int target)	// FUNCTION: 0x004d6f70
{
	// Get target name
	char targetname[16];
	
	if (target==PC)
		strcpy(targetname,"PC");
	else if (target==PS2)
		strcpy(targetname,"PS2");
	else if (target==XBOX)
		strcpy(targetname,"XBOX");

	if (aLevel==-1)
		basic_resource_filename(dest, "data\\Resources\\base_res_", targetname);
	else if (aLevel == -2)
		basic_resource_filename(dest, "data\\Resources\\Frontend_res_",targetname);
	else if (aLevel == -3)
	{
		extern bool pause_for_showing_controls;
		if (pause_for_showing_controls && PLAYABLE_DEMO)
			sprintf(dest,"data\\Resources\\Loading_res_%s_%d.aya",targetname, TEXT_DB.GetLanguage());
		else
			basic_resource_filename(dest, "data\\Resources\\Loading_res_", targetname);
	}
	else if (aLevel >= 0)
		sprintf(dest,"data\\Resources\\%03d_res_%s.aya",aLevel,targetname);
	else
		sprintf(dest, "data\\Resources\\goodie_%02d_res_%s.aya", -aLevel - 1000, targetname);
}

#ifdef RESBUILDER
// i have a go at having uncompressed sky textures if poss.
static void set_kempy_compression(bool onoff, CTEXTURE **mpTexture, int mNumTextures)
{
	char kempynames[NO_KEMPYTEXTURES][100];
	int i;

	for (i = 0; i < NO_KEMPYTEXTURES; i++)
		CKempyCube::GetTextureName(kempynames[i], MAP.GetHFProperties().CubeNo, i);

	for (i = 0; i < mNumTextures; i++)
	{
		for (int j = 0; j < NO_KEMPYTEXTURES; j++)
		{
			if (!stricmp(mpTexture[i]->GetName(), kempynames[j]))
			{
				// it's a kempy texture! Let's decompress it.
				mpTexture[i]->m_bCanCompress = onoff;
			}
		}
	}
}

void CResourceAccumulator::WriteResources( SINT aLevel,int target )
{
	mTarget=target;
	mTargetLevel=aLevel;

	// Start resource IDs at 0

	// unless we're a goodie, in which case we get out of the way of the normal resources.
	if (aLevel <= -1000) mResourceID = 10000;
	else				 mResourceID = 0;

	CMEMBUFFER lBuffer;

	// Get target name

	char targetname[16];

	if (target==PC)
		strcpy(targetname,"PC");
	else if (target==PS2)
		strcpy(targetname,"PS2");
	else if (target==XBOX)
		strcpy(targetname,"XBOX");
	
	// Write level resource file

	if (aLevel==-1)
		CONSOLE.Status("Dumping base resources");
	else if (aLevel == -2)
		CONSOLE.Status("Dumping Frontend Resources");
	else if (aLevel == -3)
		CONSOLE.Status("Dumping Loading Resources");
	else if (aLevel >= 0)
		CONSOLE.Status("Dumping level resources");
	else
		CONSOLE.Status("Dumping goodie resources");
	
	char filename[256];
	GetFileName(filename, aLevel, target);
	
	char pagefilename[256];
	strcpy(pagefilename,filename);
	*strstr(pagefilename,".")='\0';
	strcat(pagefilename,".apf");

	// And the info is the same name, but with a ".txt"
	char infofilename[ 256 ];
	char *src, *dest;
	for (src = filename, dest = infofilename; *src != '.'; src++, dest++)
		*dest = *src;

	// write a different extension
	dest[0] = '.';
	dest[1] = 't';
	dest[2] = 'x';
	dest[3] = 't';
	dest[4] =  0 ;

	CMEMBUFFER info;
	info.InitFromMem(infofilename);

	char infobuffer[512];
	UINT start,size;
	if (aLevel==-1)
		sprintf(infobuffer,"Base resources for platform %s\n",targetname);
	else if (aLevel == -2)
		sprintf(infobuffer,"Frontend resources for platform %s\n",targetname);
	else if (aLevel == -3)
		sprintf(infobuffer,"Loading resources for platform %s\n",targetname);
	else if (aLevel >= 0)
		sprintf(infobuffer,"Resource file for level %d, platform %s\n",aLevel,targetname);
	else
		sprintf(infobuffer,"Resource file for goodie %d, platform %s\n", -aLevel - 1000, targetname);

	info.Write(infobuffer,strlen(infobuffer));

	if ((aLevel>=-1) && (target==PS2))
	{
		// Also create a pagefile
		mPageFile=new (MEMTYPE_SCRATCHPAD) CChunker();
		mPageFile->Open(pagefilename);
		mPageFile->Start(MKID("PAGE"));
		mPageFile->End();

		sprintf(infobuffer,"Page file created at %s\n",pagefilename);
		info.Write(infobuffer,strlen(infobuffer));
	}
	else
		mPageFile=NULL;

	CChunker c; 
	c.Open(filename);
	
	c.Start(MKID("LVLR"));
	c.Write(&resfile_version,sizeof(resfile_version),1);

	sprintf(infobuffer,"Resfile version %d\n",resfile_version);
	info.Write(infobuffer,strlen(infobuffer));

	c.End();
	
	c.Start(MKID("TARG"));
	int p=target;
	c.Write(&p,sizeof(p),1);
	c.End();

	c.Start(MKID("AYAD"));
	c.Write(&resfile_ctexturesize,sizeof(resfile_ctexturesize),1);
	c.Write(&resfile_cmeshsize,sizeof(resfile_cmeshsize),1);
	c.Write(&resfile_cmeshpartsize,sizeof(resfile_cmeshpartsize),1);
	c.Write(&resfile_hfsize,sizeof(resfile_hfsize),1);
	c.Write(&resfile_vshadersize,sizeof(resfile_vshadersize),1);
	int	sshd=1;
	if (CLIPARAMS.mNoStaticShadows)
		sshd=0;
	c.Write(&sshd,sizeof(sshd),1);
	c.End();

	CONSOLE.Status("Dumping textures");

	// Figure out which textures we want

	BOOL *writetexture=new (MEMTYPE_SCRATCHPAD) BOOL[mNumTextures];
	int *droppedmipmaps=new (MEMTYPE_SCRATCHPAD) int[mNumTextures];

	int i;

	for (i=0;i<mNumTextures;i++)
	{
		BOOL write=TRUE;
		if (((mTextureFlags[i] & RES_NOTONPS2)!=0) && (target==PS2))
			write=FALSE;
		if (((mTextureFlags[i] & RES_NOTONXBOX)!=0) && (target==XBOX))
			write=FALSE;
		if (aLevel==-1)
		{
			if ((mTextureFlags[i] & RES_BASESET)==0)
				write=FALSE;
		}
		else if (aLevel==-3)
		{
			if ((mTextureFlags[i] & RES_LOADINGSET)==0)
				write=FALSE;
		}
		else
		{
			// game, goodies and frontend resources all pretend to simply be levels so we resourcify all textures without special flags.
			if (((mTextureFlags[i] & RES_BASESET)!=0) || ((mTextureFlags[i] & RES_LOADINGSET)!=0))
				write=FALSE;
		}

		writetexture[i]=write;
		droppedmipmaps[i]=0;
	}

	BOOL done=TRUE;
	DWORD textureramlimit=0;

	// These days we're always limiting texture RAM, just possibly to an extremely huge number
	if (aLevel>=0 && target == XBOX)
	{
		done=FALSE;

		textureramlimit=CLIPARAMS.mTextureRAMLimit;

#if TARGET == XBOX || TARGET == PC
		if (CLIPARAMS.mTextureRAMLimit == 0x7fffffff)
		{
			// we're now getting a little closer to the edge on this.
			if (aLevel >849 && aLevel < 900)
				textureramlimit = XBOX_TEXTURE_DATA_MEMORY - UNACCOUNTED_TEXTURE_MEMORY_MULTI;
			else
				textureramlimit = XBOX_TEXTURE_DATA_MEMORY - UNACCOUNTED_TEXTURE_MEMORY;
		}
#endif
	}

	while (!done)
	{
		// Figure out how much texture RAM all these will use...

		DWORD usedtextureram=0;

		for (i=0;i<mNumTextures;i++)
		{
			if (writetexture[i])
				usedtextureram+=mpTexture[i]->GetTextureDataSize(target,droppedmipmaps[i]);
		}

		sprintf(infobuffer,"Textures now use %dK of texture RAM\n",usedtextureram/1024);
		TRACE(infobuffer);		
		info.Write(infobuffer,strlen(infobuffer));

		if (usedtextureram>textureramlimit)
		{
			// Drop the largest texture's mipmap count

			int best=0;
			int bestn=-1;

			for (i=0;i<mNumTextures;i++)
			{
				// this special case texture is the first to go. It's a fucking huge battle engine texture
				// which on most levels is only seen very small.
				if (!stricmp(mpTexture[i]->GetName(), "meshtex\\be_texb.tga") &&
					mpTexture[i]->GetWidth() > 256 && !droppedmipmaps[i] &&
					(mpTexture[i]->GetActualNumberOfMipmaps()>1))
				{
					best = mpTexture[i]->GetTextureDataSize(target,droppedmipmaps[i]);
					bestn = i;
					break;
				}

				// otherwise we drop the biggest first.
				if ( (mpTexture[i]->GetTextureDataSize(target,droppedmipmaps[i])>best) && 
					 (mpTexture[i]->GetActualNumberOfMipmaps()>(droppedmipmaps[i]+1)) )
				{
					best=mpTexture[i]->GetTextureDataSize(target,droppedmipmaps[i]);
					bestn=i;
				}
			}

			if (bestn==-1)
			{
				sprintf(infobuffer,"Ran out of things to downsample!\n");
				TRACE(infobuffer);
				info.Write(infobuffer,strlen(infobuffer));
				done=TRUE;
			}
			else
			{
				sprintf(infobuffer,"Downsampling texture %s\n",mpTexture[bestn]->GetName());
				TRACE(infobuffer);
				info.Write(infobuffer,strlen(infobuffer));
				droppedmipmaps[bestn]++;
			}
		}
		else
			done=TRUE;
	}

	// now, if we've got any texture memory left, we'll increase the colour depth of the kempy cube
	if (target == XBOX)
	{
		set_kempy_compression(false, mpTexture, mNumTextures);

		// see how much memory we've got.
		DWORD usedtextureram=0;

		for (i=0;i<mNumTextures;i++)
		{
			if (writetexture[i])
				usedtextureram+=mpTexture[i]->GetTextureDataSize(target,droppedmipmaps[i]);
		}

		if (usedtextureram > textureramlimit)
		{
			// oh dear, that's not allowed. Sorry.
			set_kempy_compression(true, mpTexture, mNumTextures);

			TRACE("Had to use compressed sky cube\n");
		}
		else
		{
			TRACE("Using 32 bit sky cube!\n");
		}
	}
	
	// let's tell everyone which textures are gone.
	char fname[200];
	sprintf(fname, "t:\\clobbery\\dropped_%d.html", aLevel);
	FILE *clobber = fopen(fname, "wa");

	for (i=0;i<mNumTextures;i++)
	{
		CONSOLE.StatusPercentage("Dumping textures",i,mNumTextures);
	
		if (writetexture[i])
		{
			int thoughtsize = mpTexture[i]->GetTextureDataSize(target,droppedmipmaps[i]);

			start=c.WhereAmI();
#if TARGET==PC
			c.Start(MKID("TEXT"));
			mpTexture[i]->Serialize(&c,this,mTextureFlags[i],droppedmipmaps[i]);
			c.End();

#endif
			size=c.WhereAmI()-start;

			if (clobber && droppedmipmaps[i])
			{
				fprintf(clobber, "<a href=\"file://p:\\onslaught\\data\\textures\\%s\">%s</a> - %dK (thought it was %dK), lost %d levels and about %dK<br><br>\n",
						mpTexture[i]->GetName(),
						mpTexture[i]->GetName(),
						size/1024,
						thoughtsize/1024,
						droppedmipmaps[i],
						size * 3 / 1024);
			}

			sprintf(infobuffer,"0x%8x Texture %s - %dK\n",start,mpTexture[i]->GetName(),size/1024);
			info.Write(infobuffer,strlen(infobuffer));	
		}
		else
		{
			sprintf(infobuffer,"Skipped texture %s due to RF flags\n",mpTexture[i]->GetName());
			info.Write(infobuffer,strlen(infobuffer));			
		}
	}

	if (clobber) fclose(clobber);

	SAFE_DELETE(droppedmipmaps);
	SAFE_DELETE(writetexture);

	CONSOLE.StatusDone("Dumping textures");	

	CONSOLE.Status("Dumping meshes");

	for (i=0;i<mNumMeshes;i++)
	{
		CONSOLE.StatusPercentage("Dumping meshes",i,mNumMeshes);

		BOOL write=TRUE;
		if (((mMeshFlags[i] & RES_NOTONPS2)!=0) && (target==PS2))
			write=FALSE;
		if (((mMeshFlags[i] & RES_NOTONXBOX)!=0) && (target==XBOX))
			write=FALSE;

		if (aLevel==-1)
		{
			if ((mMeshFlags[i] & RES_BASESET)==0)
				write=FALSE;
		}
		else if (aLevel==-3)
		{
			if ((mMeshFlags[i] & RES_LOADINGSET)==0)
				write=FALSE;
		}
		else
		{
			if (((mMeshFlags[i] & RES_BASESET)!=0) || ((mTextureFlags[i] & RES_LOADINGSET)!=0))
				write=FALSE;
		}
		
		if (write)
		{
			start=c.WhereAmI();
			c.Start(MKID("MESH"));		
			mpMesh[i]->Serialize(&c,this,mMeshFlags[i]);
			c.End();
			size=c.WhereAmI()-start;
			sprintf(infobuffer,"0x%8x Mesh %s - %dK\n",start,mpMesh[i]->mName,size/1024);
			info.Write(infobuffer,strlen(infobuffer));	
		}
		else
		{
			sprintf(infobuffer,"Skipped mesh %s due to RF flags\n",mpMesh[i]->mName);
			info.Write(infobuffer,strlen(infobuffer));			
		}
	}	

	CONSOLE.StatusDone("Dumping meshes");
	
	if (aLevel==-1)
	{
		// Base set stuff
#if TARGET==PC
		if (target==XBOX)
		{
			CONSOLE.Status("Dumping vertex shaders");
			
			start=c.WhereAmI();	
			CVertexShader::SerializeAll(&c,this);
			size=c.WhereAmI()-start;
			sprintf(infobuffer,"0x%8x Vertex shaders - %dK\n",start,size/1024);
			info.Write(infobuffer,strlen(infobuffer));	
			
			CONSOLE.StatusDone("Dumping vertex shaders");
		}
		CONSOLE.Status("Dumping Index Buffers");
		PATCHMANAGER.SerializeAll(&c,this);
		CONSOLE.StatusDone("Dumping Index Buffers");
#endif
		ENGINE.GetLandscape()->GetDamage()->SerializeAll(&c,this);
		PLATFORM.Serialize(&c,this);
	}
	else if (aLevel==-2)
	{
		// Frontend stuff
	}
	else if (aLevel==-3)
	{
		// Loading stuff
	}
	else if (aLevel >= 0)
	{
		// Game level stuff

#if TARGET==PC
		CONSOLE.Status("Dumping imposters");
		
		start=c.WhereAmI();	
		c.Start(MKID("IMPS"));
		CIMPOSTER::SerializeAll(&c,this);
		c.End();
		size=c.WhereAmI()-start;
		sprintf(infobuffer,"0x%8x Imposters - %dK\n",start,size/1024);
		info.Write(infobuffer,strlen(infobuffer));
		
		CONSOLE.StatusDone("Dumping imposters");
		
		CONSOLE.Status("Dumping landscape textures");
		
		start=c.WhereAmI();	
		c.Start(MKID("LNDS"));
		ENGINE.GetLandscape()->Serialize(&c,this);
		c.End();
		size=c.WhereAmI()-start;
		sprintf(infobuffer,"0x%8x Landscape - %dK\n",start,size/1024);
		info.Write(infobuffer,strlen(infobuffer));	
		
		CONSOLE.StatusDone("Dumping landscape textures");

		CONSOLE.Status("Dumping surf");
		
		start=c.WhereAmI();	
		c.Start(MKID("SURF"));
		SURF.Serialize(&c,this);
		c.End();
		size=c.WhereAmI()-start;
		sprintf(infobuffer,"0x%8x Surf - %dK\n",start,size/1024);
		info.Write(infobuffer,strlen(infobuffer));	
		
		CONSOLE.StatusDone("Dumping surf");
#endif

		CONSOLE.Status("Dumping engine resources");
		start=c.WhereAmI();	
		c.Start(MKID("ERES"));
		ENGINE.Serialize(&c,this);
		c.End();
		size=c.WhereAmI()-start;
		sprintf(infobuffer,"0x%8x Engine resources - %dK\n",start,size/1024);
		info.Write(infobuffer,strlen(infobuffer));	
		CONSOLE.StatusDone("Dumping engine resources");

		CONSOLE.Status("Dumping static shadows");
		c.Start(MKID("SSHD"));
		start=c.WhereAmI();	
		STATICSHADOWS.SerializeAll(&c,this);
		size=c.WhereAmI()-start;
		sprintf(infobuffer,"0x%8x Static shadows - %dK\n",start,size/1024);
		info.Write(infobuffer,strlen(infobuffer));	
		c.End();
		CONSOLE.StatusDone("Dumping static shadows");	
		
		CONSOLE.Status("Dumping world resources");
		start=c.WhereAmI();	
		c.Start(MKID("WRES"));
		WORLD.Serialize(&c,this);
		c.End();
		size=c.WhereAmI()-start;
		sprintf(infobuffer,"0x%8x World resources - %dK\n",start,size/1024);
		info.Write(infobuffer,strlen(infobuffer));	
		CONSOLE.StatusDone("Dumping world resources");
	}
	else if (aLevel <= -1000)
	{
		// Goodie resource
		CFEPGoodies::Serialise(&c,this);
	}
	
	c.Close();
	if (mPageFile)
	{
		mPageFile->Close();
		SAFE_DELETE(mPageFile);
	}
	info.Close();	

	if (aLevel==-1)
		CONSOLE.StatusDone("Dumping base resources");
	if (aLevel==-2)
		CONSOLE.StatusDone("Dumping Frontend resources");
	if (aLevel>=0)
		CONSOLE.StatusDone("Dumping level resources");
	else
		CONSOLE.StatusDone("Dumping goodie resources");

#if TARGET==PC
	if (aLevel!=-1)
	{
		PLATFORM.SetRegKey("Result","Success");
	}
#endif
}
#endif
//---------------------------------------------------------------------------

void CResourceAccumulator::ReadResources(SINT level, CMEMBUFFER *membuffer, BOOL meshesonly)	// FUNCTION: 0x004d7200
{
	bool coming_in_from_a_membuffer = (membuffer != NULL);

	float start_reading = PLATFORM.GetSysTimeFloat();

	if (level==-1)
	{
		CONSOLE.Status("Loading base resources");
		TRACE("Loading base resources\n");
	}
	else if (level==-2)
	{
		CONSOLE.Status("Loading Frontend resources");
		TRACE("Loading Frontend resources\n");
	}
	else if (level==-3)
	{
		CONSOLE.Status("Loading loading resources");
		TRACE("Loading loading resources\n");
	//	ASSERT(0);
		return;		// retail PC returns here (004d7299)
	}
	else if (level >= 0)
	{
		CONSOLE.Status("Loading level resources");
		TRACE("Loading level resources\n");
	}
	else
	{
		CONSOLE.Status("Loading goodie resources");
		TRACE("Loading goodie resources\n");
	}


	// get the name of the resource file.
	char *filename = new( MEMTYPE_SCRATCHPAD ) char[256];
	GetFileName(filename, level, TARGET);

#if TARGET == XBOX
	if (filename[0] == 'Z')
	{
		filename[0] = 'D';
		PLATFORM.CacheFile(filename);
		filename[0] = 'Z';
	}
#endif

	if (level>=0)
	{
#if TARGET==PS2
		if (mResourceFileHandle>=0)
		{
			GAMEDISC.FClose(mResourceFileHandle);
			mResourceFileHandle=-1;
		}
		
		// Open the resource file for paging

		char pagefilename[256];
		strcpy(pagefilename,filename);
		*strstr(pagefilename,".")='\0';
		strcat(pagefilename,".apf");
		
		mResourceFileHandle=GAMEDISC.FOpen(pagefilename);
		
		if (mResourceFileHandle<0)
			TRACE("Warning : unable to open resource page file!\n");
#endif	

		strcpy(mResourceFileName,filename);
	}

#if TARGET==PS2
	// Open master page file

	if (mMasterPageFileHandle<0)
	{
		mMasterPageFileHandle=GAMEDISC.FOpen("data\\resources\\pagefile.mpf");
		if (mMasterPageFileHandle<0)
			TRACE("Warning : Unable to open master page file!\n");
	}
#endif

	CChunkReader *reader = new(MEMTYPE_UNKNOWN) CChunkReader;
		
	if (membuffer)
	{
		reader->Open(membuffer);
	}
	else
	{
		if (!reader->Open(filename))
		{
			if (TARGET != PC)
			{
				// We really ought to have resources.
				CONSOLE.RenderDiscFailureTextAndHang();
			}

			if (level==-1)
				CONSOLE.StatusDone("Loading base resources",FALSE);
			else if (level==-2)
				CONSOLE.StatusDone("Loading Frontend resources",FALSE);
			else if (level==-3)
				CONSOLE.StatusDone("Loading loading resources",FALSE);
			else if (level>=0)
				CONSOLE.StatusDone("Loading level resources",FALSE);
			else
				CONSOLE.StatusDone("Loading goodie resources",FALSE);

			delete reader;
			delete [] filename;
			return;
		}
	}

	// it used to be a cchunkreader declared right here, now we take the pointer.
	CChunkReader &c = *reader;

	if (level >= 0)
		mLastGameLevelLoaded=level;

	UINT tag;
/*
	if (level>=0)
	{
		// Skip file
		while (!c.GetMemBuffer()->EndOfFile())
		{
			c.GetMemBuffer()->Skip(1*1024);
		}
//		while ((tag=c.GetNext())!=0)
//			c.Skip();
	}	*/

	while ((tag=c.GetNext())!=0)
	{
		if (tag==MKID("LVLR"))
		{
			int v;
			c.Read(&v,sizeof(v),1);

			if (v!=resfile_version)
			{
				SASSERT(0,"Resource file version mismatch!");
			}

			c.Skip();
		}
		else if (tag==MKID("TARG"))
		{
			int p;
			c.Read(&p,sizeof(p),1);
			if (p!=TARGET)
			{
				SASSERT(0,"Resource file target mismatch!");
			}
		}
		else if (tag==MKID("AYAD"))
		{
			int p;
			c.Read(&p,sizeof(p),1);
#if TARGET!=PS2
			if (p!=resfile_ctexturesize)
			{
				char buf[256];
				sprintf(buf,"Resource file does not match code (CTexture size changed)!\nCode size=%d\nResource file=%d",resfile_ctexturesize,p);
				SASSERT(0,buf);
			}
#endif
			c.Read(&p,sizeof(p),1);
			if (p!=resfile_cmeshsize)
			{
				char buf[256];
				sprintf(buf,"Resource file does not match code (CMesh size changed)!\nCode size=%d\nResource file=%d",resfile_cmeshsize,p);
				SASSERT(0,buf);
			}
			c.Read(&p,sizeof(p),1);

#if TARGET==PS2
			// Correct for alignment on PS2
			while ((p & 0xF)!=0)
				p++;
#endif

			if (p!=resfile_cmeshpartsize)
			{
				char buf[256];
				sprintf(buf,"Resource file does not match code (CMeshPart size changed)!\nCode size=%d\nResource file=%d",resfile_cmeshpartsize,p);
				SASSERT(0,buf);			
			}

			c.Read(&p,sizeof(p),1);
			if (p!=resfile_hfsize)
			{
				char buf[256];
				sprintf(buf,"Resource file does not match code (CHeightfield size changed)!\nCode size=%d\nResource file=%d",resfile_hfsize,p);
				SASSERT(0,buf);			
			}

			c.Read(&p,sizeof(p),1);
#if TARGET!=PS2
			if (p!=resfile_vshadersize)
			{
				char buf[256];
				sprintf(buf,"Resource file does not match code (CVertexShader size changed)!\nCode size=%d\nResource file=%d",resfile_vshadersize,p);
				SASSERT(0,buf);			
			}
#endif

			c.Skip(); // Skip any extra data we don't know about
		}
		else if (tag==MKID("MESH")) 	// retail PC: meshes come before textures (004d75d3)
		{
			CMESH::Deserialize(&c);
		}
		else if (meshesonly)			// retail PC (004d7612)
		{
			c.Skip();
		}
		else if (tag==MKID("TEXT"))
		{
			CTEXTURE::Deserialize(meshesonly, &c);
		}
		else if (tag==MKID("ERES"))
		{
			ENGINE.Deserialize(&c);
		}
		else if (tag==MKID("WRES"))
		{
			WORLD.Deserialize(&c);
		}
		else if (tag==MKID("IMPS"))
		{
#if TARGET==PS2
			CIMPOSTER::DeserializeAll(&c);
			//c.Skip();
#else
			CIMPOSTER::DeserializeAll(&c);
#endif
		}
		else if (tag==MKID("LNDS"))
		{
#if TARGET==PS2
			ENGINE.GetLandscape()->Deserialize(&c);
#else
			c.Skip();
#endif
		}
		else if (tag==MKID("VSDS")) 
		{
#if TARGET==XBOX || TARGET==PC	// retail PC reads it (004d7776)
			CVertexShader::DeserializeAll(&c);
#else
			c.Skip();
#endif
		}
		else if (tag==MKID("PLAT"))
		{
			PLATFORM.Deserialize(&c);
			//	c.Skip();
		}
		else if (tag==MKID("SURF"))
		{
			SURF.Deserialize(&c);
		}
		else if (tag==MKID("SSHD"))
		{
			STATICSHADOWS.DeserializeAll(&c);
		}
		else if(tag==MKID("PMIB"))
		{
#ifdef _DIRECTX
			PATCHMANAGER.DeserializeAll(&c);
#else
			c.Skip();
#endif
		}
		else if(tag==MKID("DMKR"))
		{
			ENGINE.GetLandscape()->GetDamage()->DeserializeAll(&c);
		}
		else if(tag==MKID("GDIE"))
		{
			FRONTEND.mFEPGoodies.Deserialise(&c);
		}
		else
		{
			char buffer[64];
			char cid[5];
			
			cid[0]=tag & 0xFF;
			cid[1]=(tag>>8) & 0xFF;
			cid[2]=(tag>>16) & 0xFF;
			cid[3]=(tag>>24) & 0xFF;
			cid[4]=0;
						
			sprintf(buffer,"Unknown chunk ID %s in resource file!\n",cid);
			TRACE(buffer);
			c.Skip();
		}			

		// if we're loading from a membuffer, that's because we're async so the loading's pretending
		// it's not happening, if you see what i mean.
		if (!coming_in_from_a_membuffer)
			CONSOLE.SetLoadingFraction( 0 );	// retail PC (004d794f)
	}
	
	c.Close();	

	if (level==-1)
		CONSOLE.StatusDone("Loading base resources",TRUE);
	else if (level==-2)
		CONSOLE.StatusDone("Loading Frontend resources",TRUE);
	else if (level==-3)
		CONSOLE.StatusDone("Loading loading resources",TRUE);
	else
		CONSOLE.StatusDone("Loading level resources",TRUE);

	delete [] filename;

	char buf[100];
	sprintf(buf, "CResourceAccumulator::ReadResources took %f seconds\n", PLATFORM.GetSysTimeFloat() - start_reading);
	TRACE(buf);

	delete reader;
}
// STAND-IN (link layout): retail's 16 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_ResourceAccumulator[16] = { 0 };


// FUNCTION: 0x004d6e80 init:ZERO_FVECTOR
// FUNCTION: 0x004d6ea0 init:ID_FMATRIX
