// flexarray.h: a growable array of pointers (flexarray.cpp, 00465530..004655cf). Not in Stuart's release; rebuilt
// from its bytes. OPtrSet (OPtrSet.h) builds an ordered pointer list on it.

#ifndef FLEXARRAY_H
#define FLEXARRAY_H

class CFlexArray
{
public:
	static const int	DEFAULT_SIZE;		// 005db5f8 (16): the pointer sets load it from memory

	CFlexArray(UINT size = 0);				// 00465530: DEFAULT_SIZE entries when size is 0
	~CFlexArray();							// 00465570
	void	Resize(UINT size);				// 00465580: new entries are NULL

	void	**mData;						// +0x00
	UINT	mSize;							// +0x04: the capacity
};

#endif
