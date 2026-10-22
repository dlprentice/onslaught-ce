#ifndef MEMBUFFER_H
#define MEMBUFFER_H

class	CThing;

#define	MKID(foo) UINT( ((foo)[0]) + ((foo)[1]<<8) + ((foo)[2]<<16) + ((foo)[3]<<24))
#define	END_OF_DATA 0x12345678  // well, why not..

// mem buffer interface

class	IMemBuffer  // well, i'd like it to be an interface with = 0 's but,
					// sadly that'd mean it'd have to use virtual functions.
{
public:
	void	Read(void *data, SINT size) {ASSERT(0);}
	void	Write(void *data, SINT size) {ASSERT(0);}

	void	DeclareInvalidData(CThing *t) {ASSERT(0);}
};

#include	"DXMemBuffer.h"

#endif
