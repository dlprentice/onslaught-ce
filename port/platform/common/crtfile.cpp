// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/crtfile.cpp: fopen and fclose for game objects as retail's statically linked VC6 runtime
// (LIBCMT) behaves. Its FILE structures are slots in a static table, so fclose on a stream already closed finds the slot
// unused and returns EOF. The host's C library frees a FILE on fclose, so a second fclose is a double free that aborts.
// The game closes twice on a successful save read (CPCMemoryCard::ReadSave, PCMemoryCard.cpp: fclose after the read
// and again before returning MCE_SUCCESS), so loading any career would abort. port/build.py renames game objects'
// fopen/fclose to these; libraries keep the host's.
// Game objects never reference fopen itself: port.h defines fopen as port_fopen (the path layer), so build.py also
// renames their port_fopen to bea_crt_port_fopen, which registers the stream. Without it no game stream was in the
// table and every game fclose returned EOF without closing, so a save's writes reached the file only at process exit,
// after (and over) any later save to the same file.
#include <stdio.h>

extern "C" FILE *port_fopen(const char *name, const char *mode);	// port/platform/<target>/files.cpp

namespace
{
FILE *gOpen[64];	// the game's open streams (retail's table has 20 + more on demand; the game opens few at once)

FILE *Register(FILE *f)
{
	if (f)
	{
		for (FILE *&slot : gOpen)
			if (!slot)
			{
				slot = f;
				return f;
			}
		static bool warned;	// a full table leaves the stream unregistered: its fclose would return EOF and leak it
		if (!warned)
			fprintf(stderr, "bea-port: crtfile: more than 64 game streams open; a stream will not be closed\n");
		warned = true;
	}
	return f;
}
}

extern "C" FILE *bea_crt_fopen(const char *path, const char *mode)
{
	return Register(fopen(path, mode));
}

extern "C" FILE *bea_crt_port_fopen(const char *path, const char *mode)
{
	return Register(port_fopen(path, mode));
}

extern "C" int bea_crt_fclose(FILE *f)
{
	for (FILE *&slot : gOpen)
		if (slot && slot == f)
		{
			slot = nullptr;
			return fclose(f);
		}
	return EOF;	// not open: as LIBCMT's fclose on an unused slot
}
