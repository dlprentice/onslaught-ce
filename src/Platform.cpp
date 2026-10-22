#include	"Common.h"

#include	"Platform.h"
#include	"console.h"

#if TARGET == PC
#include "DX.h"
#elif TARGET == PS2
#include "PS2DMAList.h"
#endif

#include	<string.h>

// retail PC additions (Platform.obj 004d2580..004d268d): the display reset a menu asks for, and CreateDirectoryPath
#include	<direct.h>
#include	"ltshell.h"
#include	"Career.h"
#include	"FEPOptions.h"

char			gDisplayResetState = 0;			// 0082b5b0: 1 or 2 asks for the display to be reset (2 also saves the options)

void CPlatform::Flip(BOOL in_game)	// FUNCTION: 0x004d2580
{
	// retail PC: no wait on CONSOLE.mStopEverything; after the flip, a requested display reset
	PLATFORM.DeviceFlip(in_game);

	if (gDisplayResetState)
	{
		bool save_options = (gDisplayResetState == 2);
		gDisplayResetState = 0;
		LT.Reset3DEnvironment(TRUE, TRUE);

		if (save_options)
		{
			int size = CCareer::SizeOfSaveGame();
#line 41
			char *data = new( MEMTYPE_SCRATCHPAD ) char[size];
			CAREER.Save(data);
			CFEPOptions::WriteDefaultOptionsFile(data, size);
			delete [] data;
		}
	}
}

void CPlatform::CreateDirectoryPath(char *path, BOOL strip_file_name)	// FUNCTION: 0x004d2600
{
	char	buf[MAX_PATH];

	strcpy(buf, path);
	if (strip_file_name)
		strrchr(buf, '\\')[1] = 0;

	char	*p = strchr(buf, '\\');
	while (p)
	{
		*p = 0;
		_mkdir(buf);
		*p = '\\';
		p = strchr(p + 1, '\\');
	}
}
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_Platform[8] = { 0 };

// FUNCTION: 0x004d2490 init:ZERO_FVECTOR
// FUNCTION: 0x004d24b0 init:ID_FMATRIX
