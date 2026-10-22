// flexarray.cpp: CFlexArray (not in Stuart's release; rebuilt from the retail executable, 00465530..004655cf).

#include "common.h"
#include "flexarray.h"

const int	CFlexArray::DEFAULT_SIZE = 16;

//******************************************************************************************
CFlexArray::CFlexArray(UINT size)	// FUNCTION: 0x00465530
{
	if (size == 0)
		size = DEFAULT_SIZE;
	mSize = size;
#line 34
	mData = new (MEMTYPE_FLEXARRAY) void*[size];
}

//******************************************************************************************
CFlexArray::~CFlexArray()	// FUNCTION: 0x00465570
{
	delete [] mData;
}

//******************************************************************************************
void	CFlexArray::Resize(UINT size)	// FUNCTION: 0x00465580
{
	void	*old = mData;		// read before the size: retail holds it in eax, the size in ecx (00465588)
	mData = (void **)MEM_MANAGER.ReAlloc(old, size * sizeof(void *));

	if (size > mSize)
	{
		UINT	n = size - mSize;
		void	**p = &mData[mSize];

		while (n--)
			*p++ = NULL;
	}

	mSize = size;
}
