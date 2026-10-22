// WaitingThread.cpp: CWaitingThread, a worker thread that sleeps until it is given its job (not in Stuart's
// release; rebuilt from the retail object 00528b60..00528dcb, which has no maths initializers; the file name is
// the header's). The Bink opener, the voice loader and the async cache derive from it. A caller takes the lock
// (Acquire or Lock), sets up the job, then StartAsync runs it on the thread or RunSync runs it at once.

#include "common.h"
#include "WaitingThread.h"

CWaitingThread	*CWaitingThread::mFirst;			// 0089c01c

//******************************************************************************************
// The lock is released through the inline Unlock (00528dc0): calling ReleaseMutex directly swaps ReleaseMutex
// and SetEvent between ebx and ebp.
DWORD WINAPI	CWaitingThread::ThreadProc(LPVOID param)	// FUNCTION: 0x00528b60
{
	CWaitingThread	*thread = (CWaitingThread *)param;

	while (1)
	{
		WaitForSingleObject(thread->mStartEvent, INFINITE);
		if (thread->mQuit)
			break;
		WaitForSingleObject(thread->mMutex, INFINITE);
		thread->Run();
		thread->mBusy = false;
		thread->Unlock();
		SetEvent(thread->mDoneEvent);
	}
	return 0;
}

//******************************************************************************************
CWaitingThread::CWaitingThread()	// FUNCTION: 0x00528bc0
{
	mThread = INVALID_HANDLE_VALUE;
	mMutex = INVALID_HANDLE_VALUE;
	mStartEvent = INVALID_HANDLE_VALUE;
	mDoneEvent = INVALID_HANDLE_VALUE;
	mQuit = false;
	mBusy = false;
	mNext = mFirst;
	mFirst = this;
}

//******************************************************************************************
CWaitingThread::~CWaitingThread()	// FUNCTION: 0x00528bf0
{
	if (mStartEvent != INVALID_HANDLE_VALUE)
	{
		mQuit = true;
		SetEvent(mStartEvent);
		WaitForSingleObject(mThread, INFINITE);
		CloseHandle(mThread);
		mThread = INVALID_HANDLE_VALUE;
		CloseHandle(mMutex);
		mMutex = INVALID_HANDLE_VALUE;
		CloseHandle(mStartEvent);
		mStartEvent = INVALID_HANDLE_VALUE;
		CloseHandle(mDoneEvent);
		mDoneEvent = INVALID_HANDLE_VALUE;
	}

	CWaitingThread	**link = &mFirst;
	for (CWaitingThread *t = mFirst; t; t = t->mNext)
	{
		if (t == this)
		{
			*link = mNext;
			return;
		}
		link = &t->mNext;
	}
}

//******************************************************************************************
BOOL	CWaitingThread::Init()	// FUNCTION: 0x00528c70
{
	if (mThread == INVALID_HANDLE_VALUE)
	{
		DWORD	id;

		mMutex = CreateMutex(NULL, FALSE, NULL);
		mStartEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		mDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		mThread = CreateThread(NULL, 0, ThreadProc, this, 0, &id);
	}

	if ((mThread == INVALID_HANDLE_VALUE) || !mThread)
		return FALSE;
	if ((mMutex == INVALID_HANDLE_VALUE) || !mMutex)
		return FALSE;
	if ((mStartEvent == INVALID_HANDLE_VALUE) || !mStartEvent)
		return FALSE;
	if ((mDoneEvent == INVALID_HANDLE_VALUE) || !mDoneEvent)
		return FALSE;
	return TRUE;
}

//******************************************************************************************
void	CWaitingThread::Acquire()	// FUNCTION: 0x00528d10
{
	if (Init())
	{
		while (mBusy)
			Sleep(0);
		WaitForSingleObject(mMutex, INFINITE);
	}
}

//******************************************************************************************
void	CWaitingThread::StartAsync()	// FUNCTION: 0x00528d50
{
	mBusy = true;
	ReleaseMutex(mMutex);
	SetEvent(mStartEvent);
}

//******************************************************************************************
void	CWaitingThread::RunSync()	// FUNCTION: 0x00528d70
{
	Run();
	ReleaseMutex(mMutex);
}

//******************************************************************************************
BOOL	CWaitingThread::IsBusy()	// FUNCTION: 0x00528d90
{
	return mBusy;
}

//******************************************************************************************
void	CWaitingThread::Lock()	// FUNCTION: 0x00528da0
{
	Init();
	WaitForSingleObject(mMutex, INFINITE);
}

//******************************************************************************************
void	CWaitingThread::Unlock()	// FUNCTION: 0x00528dc0
{
	ReleaseMutex(mMutex);
}
