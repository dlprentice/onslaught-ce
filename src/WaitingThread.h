// WaitingThread.h: a worker thread that waits for jobs (vtable 005e4aac, one pure slot; WaitingThread.obj,
// 00528b60..00528dcf; the file name is ours). Every one is on a list at 0089c01c. The thread takes the lock, runs
// Run and releases it for each start signal; the front-end video opens Bink files on one (CBinkOpenThread).

#ifndef WAITINGTHREAD_H
#define WAITINGTHREAD_H

class CWaitingThread
{
public:
	CWaitingThread();						// 00528bc0
	~CWaitingThread();						// 00528bf0: stops the thread and closes the handles
	virtual void	Run() = 0;				// 0: the job, run under the lock

	BOOL			Init();					// 00528c70: makes the handles and the thread once
	void			Acquire();				// 00528d10: waits until no job is running, then takes the lock
	void			StartAsync();			// 00528d50: the thread runs the job and releases the lock
	void			RunSync();				// 00528d70: the job here, then releases the lock
	BOOL			IsBusy();				// 00528d90
	void			Lock();					// 00528da0
	void			Unlock();				// 00528dc0

	static DWORD WINAPI	ThreadProc(LPVOID param);	// 00528b60: waits for each start, runs the job under the lock

	HANDLE			mThread;				// +0x04
	HANDLE			mMutex;					// +0x08
	HANDLE			mStartEvent;			// +0x0c
	HANDLE			mDoneEvent;				// +0x10
	bool			mQuit;					// +0x14
	char			mBusy;					// +0x15: a char (IsBusy sign-extends it, 00528d90)
	CWaitingThread	*mNext;					// +0x18

	static CWaitingThread	*mFirst;		// 0089c01c: every waiting thread
};

#endif
