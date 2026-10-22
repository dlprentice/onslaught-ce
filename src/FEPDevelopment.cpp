// FEPDevelopment.cpp: the developer's level list (reconstructed from the retail PC executable; not in Stuart's
// release): every data\worlds\world???.wrd, sorted; SELECT quits the front end with that level's number.

#include	"Common.h"

#include	"FEPDevelopment.h"
#include	"Frontend.h"
#include	"ltshell.h"
#include	"RenderInfo.h"
#include	"ParticleManager.h"

//*********************************************************************************
int CompareWorldNames(const void *a, const void *b)	// FUNCTION: 0x00458050
{
	return strcmp(*(char **)a, *(char **)b);
}

//*********************************************************************************
BOOL	CFEPDevelopment::Init()	// FUNCTION: 0x00458090
{
	mLastFirework = 0;
	mSelected = 0;

	char			path[] = "data\\worlds\\world???.wrd";
	WIN32_FIND_DATA	fd;
	HANDLE			h;

	h = FindFirstFile(ToTCHAR(path), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				mNumWorlds ++;
		} while (FindNextFile(h, &fd));
		FindClose(h);
	}

#line 53
	mWorlds = new (MEMTYPE_UNKNOWN) char *[mNumWorlds];
	if (!mWorlds)
		return FALSE;

	int		c0;
	for (c0 = 0; c0 < mNumWorlds; c0 ++)
#line 60
		mWorlds[c0] = new (MEMTYPE_UNKNOWN) char[100];

	h = FindFirstFile(ToTCHAR(path), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		c0 = 0;
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				strncpy(mWorlds[c0 ++], FromTCHAR(fd.cFileName), 99);
		} while (FindNextFile(h, &fd));
		FindClose(h);
	}

	qsort(mWorlds, mNumWorlds, sizeof(char *), CompareWorldNames);

	return TRUE;
}

//*********************************************************************************
void	CFEPDevelopment::Shutdown()	// FUNCTION: 0x004581e0
{
	for (int c0 = 0; c0 < mNumWorlds; c0 ++)
		delete [] mWorlds[c0];

	delete [] mWorlds;
	mWorlds = NULL;
	mNumWorlds = 0;
}

//*********************************************************************************
void	CFEPDevelopment::Process(EFEPState state)	// FUNCTION: 0x00458230
{
	if (state != FEPS_INACTIVE)
	{
		// fireworks
		if ((mLastFirework < FRONTEND.GetCounter() - 40.f) && ((rand() % 10000) < 100))
		{
			CParticleDescriptor *pd = PARTICLE_SET.FindByName("Firework Effect");
			PARTICLE_MANAGER.CreateEffect(pd, NULL, FVector(float((rand() & 255) - 128) * 0.078125f, 0.f, 8.f), 0, 0);
			mLastFirework = SINT(FRONTEND.GetCounter());
		}

		if (PLATFORM.KeyOnce(DIK_ESCAPE))
			FRONTEND.SetQuit(-1);
	}
}

//*********************************************************************************
void	CFEPDevelopment::ButtonPressed(SINT button, float val)	// FUNCTION: 0x00458320
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_UP:
		mSelected = (mNumWorlds + mSelected - 1) % mNumWorlds;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		mSelected = (mSelected + 1) % mNumWorlds;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		{
			// "world123.wrd": cut the extension and read the number
			int len = strlen(mWorlds[mSelected]);
			mWorlds[mSelected][len - 4] = 0;
			int level;
			sscanf(mWorlds[mSelected] + len - 7, "%d", &level);
			FRONTEND.PlaySound(FES_SELECT);
			FRONTEND.SetQuit(level);
		}
		break;
	}
}

//*********************************************************************************
void	CFEPDevelopment::RenderWorldList()	// FUNCTION: 0x004583c0
{
	float	x = 50.f;
	float	y = 100.f;
	int		row = 0;

	for (int c0 = 0; c0 < mNumWorlds; c0 ++)
	{
		DWORD col = 0xffffaf3f;

		if (c0 == mSelected)
		{
			SINT c = SINT(sinf(float(mLastFirework) / 10.0f) * 63.f + 192.f);
			col = c * 0x010101 + 0xff000000;
		}

		PLATFORM.Font(FONT_DEBUG)->DrawTextDynamic(x, y, 0.f, 1.f, 1.f, col, ToWCHAR(mWorlds[c0]), float(mLastFirework - c0 * 2) / 20.0f, 0, 0);

		if (++ row == 15)
		{
			x += 120.f;
			row = 0;
			y = 100.f;
		}
		else
			y += 20.f;
	}
}

//*********************************************************************************
void	CFEPDevelopment::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x004584d0
{
	LT.SetModulate(0);
	RENDERINFO.SetUnknown2ed(true);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetUnknown2e8(0xffffffff);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

	RenderWorldList();
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x00457f60 init:ZERO_FVECTOR
// FUNCTION: 0x00457f80 init:ID_FMATRIX
