// Array.h: interface for the CArray class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ARRAY_INCLUDE
#define ARRAY_INCLUDE



// array class uses heap to store items
template <class T> class CArray
{
public:
#ifdef VANILLA_MEMORYMANAGER
	CArray(int sz) :mSize(sz) { mItems = (T*) malloc(sizeof(T)*sz); }
	~CArray() { free(mItems); }
#else
	CArray(int sz) :mSize(sz) { mItems = new (MEMTYPE_ARRAY) T[sz] ; }
	~CArray() { delete [] mItems ; }
#endif
	CArray<T>&	operator =(CArray<T>& copy) {int i =0; for (i =0;i< mSize;i++) mItems[i] = copy[i] ; return *this; }
				void  SetAll(T val) { int i =0 ; for (i =0;i< mSize;i++) mItems[i] = val ; }
				int	  Size() { return mSize ;}
#ifndef PARTICLE_ED
#ifndef EDITORBUILD2
#ifndef VANILLA_MEMORYMANAGER
				void  ReSize(int size) { mItems = (T*)MEM_MANAGER.ReAlloc(mItems, (sizeof(T))*size) ; mSize = size;}
#else
				void  ReSize(int size) { mItems = (T*)realloc(mItems, (sizeof(T))*size) ; mSize = size;}
#endif
#else
				void  ReSize(int size) { mItems = (T*)realloc(mItems, (sizeof(T))*size) ; mSize = size;}
#endif
#else
				void  ReSize(int size) { mItems = (T*)realloc(mItems, (sizeof(T))*size) ; mSize = size;}
#endif

#ifdef _DEBUG
	T& operator [] (int item) ;
private:

#else
	T& operator [] (int item) { return (T&)mItems[item] ; } 
#endif

protected:

	T* mItems ;
	int mSize ;
};


// array class uses stack to store items
template <class T, int size> class CSArray
{
public:

	CSArray<T, size>& operator =(CSArray<T, size>& copy) {int i =0; for (i =0;i< size;i++) mItems[i] = copy[i] ; return *this; }
				void  SetAll(T val) { int i =0 ; for (i =0;i< size;i++) mItems[i] = val ; }
				int	  Size() { return size ; } 

#ifdef _DEBUG
	T& operator [] (int item) ;
#else
	T& operator [] (int item) { return mItems[item] ; } 
#endif


private:
	T mItems[size] ;
};



template <class T> class COSet : public CArray<T>
{
public:
				COSet(int initial_size = 1) : CArray<T>(initial_size), mEntries(0) {}
				COSet<T>&	operator =(COSet<T>& copy) { if (mSize != copy.mSize) ReSize(copy.mSize) ; CArray<T>::operator =(copy) ; mEntries = copy.mEntries; mIterator = copy.mIterator; return *this;}
	int			Add(const T& item) { mEntries++; if (mEntries>mSize) ReSize(mEntries*2); mItems[mEntries-1] = item ; return mEntries-1 ; }
	T*			First()	{ mIterator = 0 ; if (mEntries==0) return NULL ;return &mItems[mIterator] ; }
	T*			Next()	{ mIterator++; if (mIterator >=mEntries) return NULL ; return &mItems[mIterator] ; }
	void		Finalise() { ReSize(mEntries) ;}
	int			Size()	{ return mEntries ; }

protected:

	int	mEntries;
	int mIterator;
};



// These operators are used in debug build and do bounds checking

#ifdef _DEBUG

#ifndef SIMPLE_MESHES
#include "debuglog.h"
#endif


template <class T> T& CArray<T>::operator [] (int item) 
{
	if (item < 0 || item >= mSize)
	{
		LOG.AddMessage("FATAL ERROR: Outside array bounds (index = %d, size = %d)", item, mSize) ;
		ASSERT(0);
		return mItems[0] ;
	}

	return mItems[item] ;
}



template <class T, int size> T& CSArray<T,size>::operator [] (int item) 
{ 
	if (item < 0 || item >= size)
	{
		LOG.AddMessage("FATAL ERROR: Outside array bounds (index = %d, size = %d)", item, size) ;
		ASSERT(0);
		return mItems[0] ;
	}

	return mItems[item] ; 
}







#endif

#endif