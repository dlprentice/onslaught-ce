// OPtrSet.cpp: GenericOPtrSet (not in Stuart's release; rebuilt from the retail executable, 004241a0..0042435b).

#include "common.h"
#include "OPtrSet.h"
#include "debuglog.h"

//******************************************************************************************
GenericOPtrSet::GenericOPtrSet(int size, int grow) : CFlexArray(size)	// FUNCTION: 0x004241a0
{
	mCount = 0;
	if (grow > 0)
		mGrow = CFlexArray::DEFAULT_SIZE * grow;
	else if (grow < 0)
		mGrow = grow - 1;
}

//******************************************************************************************
void	GenericOPtrSet::reset()	// FUNCTION: 0x004241e0
{
	mCount = 0;
}

//******************************************************************************************
GenericOPtrSet	&GenericOPtrSet::append(void *item)	// FUNCTION: 0x004241f0
{
	if ((mCount >= (int)mSize) && !MakeRoom(mSize + 1))
		return *this;
	mData[mCount++] = item;
	return *this;
}

//******************************************************************************************
GenericOPtrSet	&GenericOPtrSet::insert_at(int index, void *item)	// FUNCTION: 0x00424260
{
	if (index == mCount)
		return append(item);

	if ((mCount >= (int)mSize) && !MakeRoom(mSize + 1))
		return *this;

	void	**dst = &mData[mCount];
	void	**src = dst - 1;
	void	**stop = &mData[index];

	while (src >= stop)
		*dst-- = *src--;

	mData[index] = item;
	mCount++;
	return *this;
}

//******************************************************************************************
// `end` is declared first and the valid-range block returns on its own: retail's epilogue is duplicated and `end` and `dst` take
// edi and esi in that order.
GenericOPtrSet	&GenericOPtrSet::remove(int from, int to)	// FUNCTION: 0x00424360
{
	if ((from >= 0) && (from <= to) && (to < mCount))
	{
		if (to == mCount - 1)
			mCount = from;
		else
		{
			void	**end = &mData[mCount];
			void	**src = &mData[to + 1];
			void	**dst = &mData[from];

			while (src < end)
				*dst++ = *src++;

			mCount += from - to - 1;
		}
		return *this;
	}
	return *this;
}
