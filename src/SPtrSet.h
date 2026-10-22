#ifndef SMALL_PTR_SET_INCLUDE
#define SMALL_PTR_SET_INCLUDE

//******************************************************************************************
// SRG
// Simple linked list class. Very fast if the list
// doesn't get to big.  If it does use PtrSet instead.
//******************************************************************************************


// linked list node class
class GenericSPtrSet ;
class SPtrSetNode
{
friend class GenericSPtrSet ;
friend class GenericListIterator;
private:

	void* mItem ;
	SPtrSetNode* mNext ;
};


// parent generic part
class GenericSPtrSet
{

public:
	friend class GenericListIterator;
	GenericSPtrSet();
	GenericSPtrSet(const GenericSPtrSet& copy) ;
	~GenericSPtrSet() { RemoveAll(); }
	GenericSPtrSet& operator = (GenericSPtrSet& copy) ; 
	void	Add(void* ptr);
	void    Append(void* ptr);
	void    Remove(void* ptr);
	void	RemoveAll();
	BOOL	Contains(void* ptr);
	int		Size()				{ return mSize; }
	void*	First() { if (mIterator = mFirst) {return mIterator->mItem;} return NULL ; }
	void*	Next() { if (mIterator = mIterator->mNext){ return mIterator->mItem;} return NULL;}
	void*   At(int val) ;
	void*	Last() { return mLast->mItem ;}
	static	void Shutdown() ;
	static  void ClearAnyDynamicCreatedNodes() ;
	static  void Init(int size);

private:
	SPtrSetNode* mFirst;
	SPtrSetNode* mLast;
	SPtrSetNode* mIterator;
	int			 mSize;

	// free list
	static SPtrSetNode* mFreeList;
	static SPtrSetNode* mBlock;
	static int			mBlockSize ;

};



#define FOR_ALL_ITEMS_IN(A,B) for((B) = (A).First(); (B) != NULL; (B) = (A).Next()) 


// template part
template <class T> class SPtrSet : public GenericSPtrSet
{
public:
	SPtrSet() : GenericSPtrSet()  {}
	SPtrSet(const SPtrSet<T>& copy) :GenericSPtrSet(copy) {}
	SPtrSet<T>&	operator = (SPtrSet<T>& copy) { GenericSPtrSet::operator = (copy) ; return *this;}
	void	Add(T* ptr) 		{ GenericSPtrSet::Add(ptr); }
	void	Append(T* ptr)		{ GenericSPtrSet::Append(ptr); }
	void	Remove(T* ptr)      { GenericSPtrSet::Remove(ptr); }
    void    RemoveAll()			{ GenericSPtrSet::RemoveAll();}
	void	DeleteAll()			{ T* item; FOR_ALL_ITEMS_IN((*this), item){delete item;} RemoveAll();} 
	BOOL	Contains(T* ptr)	{ return GenericSPtrSet::Contains(ptr) ; }
	int		Size()				{ return GenericSPtrSet::Size();}
	T*		At(int val)         { return (T*) GenericSPtrSet::At(val); }
	T*		First()				{ return (T*) GenericSPtrSet::First(); }
	T*		Next()				{ return (T*) GenericSPtrSet::Next(); }
	T*		Last()				{ return (T*) GenericSPtrSet::Last(); }
};





class GenericListIterator
{
public:
	GenericListIterator(GenericSPtrSet* list) :mForList(list) {}
	void*	First() { if (mCurrent = mForList->mFirst) {return mCurrent->mItem;} return NULL ; }
	void*	Next() { if (mCurrent = mCurrent->mNext){ return mCurrent->mItem;} return NULL;}
private:
	SPtrSetNode* mCurrent;
	GenericSPtrSet* mForList;
};



template <class T> class ListIterator : public GenericListIterator
{
public:
			ListIterator(SPtrSet<T>* list) : GenericListIterator(list) {}
			T*		First()				{ return (T*) GenericListIterator::First(); }
			T*      Next()				{ return (T*) GenericListIterator::Next() ; }
} ;

#endif 