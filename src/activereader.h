#ifndef ACTIVE_READER_INCLUDE
#define ACTIVE_READER_INCLUDE


class CThing ;
class CEvent ;

#include "Monitor.h"


class CGenericActiveReader 
{
public:

	void HandleEvent(CEvent* event) ;
	void SetReader(CMonitor* to_read) ; 
    ~CGenericActiveReader() { if (mToRead) { mToRead->RemoveDeletionEvent(this) ; }}

	void ToReadDied() { mToRead = NULL ; }

protected:

	CMonitor* mToRead ; 
	
};



template <class T> class CActiveReader : public CGenericActiveReader
{
public:
	CActiveReader() { mToRead = NULL ; }
	CActiveReader(T* to_read) {mToRead = (CMonitor*)to_read ;  if (mToRead) { mToRead->AddDeletionEvent(this); } }
	CActiveReader(CActiveReader<T>& copy) { mToRead = (CMonitor*)copy.mToRead ; if (mToRead) { mToRead->AddDeletionEvent(this); } }
	void	SetReader(T* to_read) { CGenericActiveReader::SetReader((CMonitor*)to_read);}
	BOOL	operator ==(T* right_term) { return mToRead == right_term; }
	BOOL    operator !=(T* right_term) { return mToRead != right_term; }
	void	operator =(T* right_term) { SetReader(right_term); }
	T*		operator ->() { return (T*)mToRead ; }
	T*		ToRead() { return (T*)mToRead ; }
};


#endif
