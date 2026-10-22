// OPtrSet.h: an ordered set of pointers on a CFlexArray (not in Stuart's release; rebuilt from the retail
// executable). Like SPtrSet it is a thin template over a generic class, whose members are OPtrSet.obj
// (004241a0..0042435b, then its maths pair). The event manager keeps its overflow events in time order in one.

#ifndef OPTRSET_H
#define OPTRSET_H

#include "flexarray.h"

class GenericOPtrSet : public CFlexArray
{
public:
	GenericOPtrSet(int size = CFlexArray::DEFAULT_SIZE, int grow = 1);	// 004241a0
	void			reset();											// 004241e0
	GenericOPtrSet	&append(void *item);								// 004241f0
	GenericOPtrSet	&insert_at(int index, void *item);					// 00424260
	GenericOPtrSet	&remove(int from, int to);							// 00424360

	int				size() { return mCount; }

protected:
	// room for another item: grows by mGrow items, or by -mGrow times the size when mGrow is negative
	BOOL			MakeRoom(UINT size)
	{
		if (mSize < size && mGrow == 0)
			return FALSE;
		while (mSize < size && mData)
			Resize(mGrow > 0 ? mSize + mGrow : -(mGrow * (int)mSize));
		return mData != NULL;
	}

	int				mCount;					// +0x08
	int				mGrow;					// +0x0c
};

// template part
template <class T> class OPtrSet : public GenericOPtrSet
{
public:
	OPtrSet(int size = CFlexArray::DEFAULT_SIZE, int grow = 1) : GenericOPtrSet(size, grow) {}

	T			*at(int index) { return (T *)mData[index]; }
	OPtrSet<T>	&append(T *item) { GenericOPtrSet::append(item); return *this; }
	OPtrSet<T>	&insert_at(int index, T *item) { GenericOPtrSet::insert_at(index, item); return *this; }
	OPtrSet<T>	&remove(int from, int to) { GenericOPtrSet::remove(from, to); return *this; }
};

#endif
