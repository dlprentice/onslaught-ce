// console.h: the in-game console (console.cpp, 004294b0..0042d150). Not in Stuart's release: reconstructed from
// retail; the layout is CConsole::Init's (00429bc0) and the functions that read it.

#ifndef CONSOLE_H
#define CONSOLE_H

// Console variable types (CRTMesh::Init registers bools as 3 and floats as 4, 004dc3b8, 004dc3d2); the other names
// are the type names CConsole::GetTypeName prints (0042a540).
enum ECVarType
{
	CVar_int = 0,				// CBattleEngineWalkerPart's constructor registers its ints as 0 (00412c8a)
	CVar_DWORD = 1,
	CVar_string = 2,
	CVar_bool = 3,
	CVar_float = 4,
	CVar_fvector = 5,
	CVar_fmatrix = 6
};

// A menu of the console's debug menus (vtable 005d974c; constructor 0042c420). Not in Stuart's release: the
// layout is the constructor's, the slots are CMusicMenu's (005e495c).
class CConsoleMenu
{
public:
					CConsoleMenu();								// 0042c420 (console.cpp)
	virtual void	GetName(char *name) = 0;
	virtual int		GetNumEntries() = 0;
	virtual void	GetEntry(int num, char *text) = 0;
	virtual void	OnClick(int num) = 0;
	virtual bool	GetShowSubmenus() = 0;
	virtual void	LinkChildAtHead(CConsoleMenu *child);		// 0042c440
	virtual void	UnlinkChild(CConsoleMenu *child);			// 0042c460
	void			AddSubmenu(CConsoleMenu *menu) { LinkChildAtHead(menu); }	// Stuart's name (CSoundManager::Init 004e01d6)

	CConsoleMenu	*mFirstChild;								// +0x04
	CConsoleMenu	*mNext;										// +0x08
	CConsoleMenu	*mParent;									// +0x0c
	int				mNumChildren;								// +0x10
};

// A console command (0xac bytes, console.cpp line 805).
struct CConsoleCommand
{
	char			mName[0x20];								// +0x00
	char			mHelp[0x80];								// +0x20
	void			(*mFunction)(char *);						// +0xa0: called with the whole command line
	bool			mFlag;										// +0xa4
	CConsoleCommand	*mNext;										// +0xa8
};

// A console variable (0xb0 bytes, console.cpp line 830).
struct CConsoleVar
{
	char			mName[0x20];								// +0x00
	char			mHelp[0x80];								// +0x20
	ECVarType		mType;										// +0xa0
	void			*mData;										// +0xa4
	bool			mReadOnly;									// +0xa8
	bool			mUnknownA9;									// +0xa9
	CConsoleVar		*mNext;										// +0xac
};

#define CONSOLE_LINES		60
#define CONSOLE_HISTORY		10
#define CONSOLE_KEYS		256

class CPlayer;
class CDXTexture;

class CConsole
{
public:
	CConsole()															// CONSOLE's initializer (004295a0)
	{
		mInitialised = TRUE;
		mRootMenu = NULL;
		mLastLoadingRender = -1000.f;
	}

	void	Init();														// 00429bc0
	void	InitKeyNames();												// 0042bcf0
	void	ResetLayout();												// 0042a410: sizes the console to the window
	void	Shutdown();													// 0042ae70
	// Shows a localized fatal error and exits unless bNonFatal is set (0042d080).
	void	FatalError(bool bNonFatal, SINT aStringId, SINT aParam);
	static void	FatalError(char *text, SINT aParam);					// 0042cfa0: never returns
	void	SetTrace(BOOL on);
	void	InitDefaultCommands();										// 00429ef0
	void	RegisterCommand(char *name, char *help, void (*command)(char *), bool flag = false);	// 0042af80
	void	Print(char *fmt, ...);										// 0042b840
	void	Render();													// empty on the PC: folded into 0040c640 (CDXEngine::PostRender 0053efb2)
	void	RenderDiscFailureTextAndHang();								// Stuart's (DXMemBuffer.cpp's data_bad)
	BOOL	MenuUp();													// 0042ba90
	BOOL	MenuDown();													// 0042bac0
	BOOL	MenuSelect();												// 0042bb30
	void	Exec(char *filename);										// 0042ad30: runs a console script
	void	ExecuteCommand(char *line);									// 0042b9c0
	void	ProcessKey(char key);										// 0042b120: the console's own keys while it is open
	void	RenderLoadingScreen(BOOL a = FALSE, char mode = 0);			// 0042c810 (the first argument is unused)
	void	HandleBind(int key, int event);								// 0042a4f0
	void	ListBinds();												// 0042a460
	void	SetLoading(bool on, BOOL a = TRUE, BOOL b = TRUE);			// 0042bbc0 (game.cpp's calls push x, 1, 1; CFrontEnd::Init passes 1, 1, 0 and 0, 1, 0)
	void	SetLoadingRange(float from, float to);						// 0042cf40
	void	SetLoadingFraction(float fraction);							// 0042cf70
	void	ClearCommandsAndVariables();								// 0042af20
	void	RegisterVariable(char *name, char *help, ECVarType type, void *var, bool read_only = false, bool b = false);	// 0042b040
	void	FatalFileError(char *name, SINT a);							// 0042c750 (UPhysicsManager::Reload 00510a51)
	void	FatalResourceFileError(char *name);							// 0042d0b0: does not return (CMesh::Deserialize 004aad6c)
	void	Status(char *text);											// 0042b500 (CWorld::Deserialize 0050b7a0)
	void	StatusUpdateLine(char *text, char *result);					// 0042b650
	void	StatusDone(char *text, bool ok = TRUE);						// 0042b800 (CWorld::Deserialize 0050b8c1)
	CConsoleCommand	*FindCommand(char *name);							// 0042a770
	void	SetVariable(char *name, char *value);						// 0042a7b0
	void	GetTypeName(CConsoleVar *var, char *text);					// 0042a540
	void	GetValueString(CConsoleVar *var, char *text);				// 0042a5f0

	CConsoleVar	*FindVariable(char *name)
	{
		for (CConsoleVar *v = mVariables; v; v = v->mNext)
			if (!stricmp(v->mName, name))
				return v;
		return NULL;
	}
	CConsoleCommand	*GetCommand(int n)
	{
		int i = 0;
		CConsoleCommand *c;
		for (c = mCommands; c; c = c->mNext, i++)
			if (n == i)
				break;
		return c;
	}
	CConsoleVar	*GetVariable(int n)
	{
		int i = 0;
		CConsoleVar *v;
		for (v = mVariables; v; v = v->mNext, i++)
			if (n == i)
				break;
		return v;
	}
	void	Toggle();													// console.cpp: opens or closes the console

	CPlayer	*GetPlayer() { return mPlayer; }						// inline (con_remotecameraon 0046befe)
	void	SetPlayer(CPlayer *player) { mPlayer = player; }			// inline (CGame::LoadLevel 0046cfdd)

	CConsoleMenu	*GetRootMenu() { return mRootMenu; }			// Stuart's (CSoundManager::Init 004e01c5)

	CDXTexture		*mLoadingScreen;								// +0x0000
	char			mLines[CONSOLE_LINES][0x80];					// +0x0004: the newest first
	char			mInputLine[0x80];								// +0x1e04
	char			mHistory[CONSOLE_HISTORY][0x80];				// +0x1e84
	int				mUnknown2384;									// +0x2384: the window height
	int				mUnknown2388;									// +0x2388: the window height less 256
	bool			mActive;										// +0x238c: the console is open
	bool			mMenuActive;									// +0x238d
	int				mAlpha;											// +0x2390: cg_consolealpha
	CConsoleCommand	*mCommands;										// +0x2394
	CConsoleVar		*mVariables;									// +0x2398
	CConsoleMenu	*mCurrentMenu;									// +0x239c
	CConsoleMenu	*mCommandMenu;									// +0x23a0
	CConsoleMenu	*mVariableMenu;									// +0x23a4
	int				mMenuSelection;									// +0x23a8
	int				mUnknown23ac;									// +0x23ac
	CConsoleMenu	*mRootMenu;										// +0x23b0 (00665848)
	int				mTabLength;										// +0x23b4: the typed part being completed, -1 when none
	int				mTabCount;										// +0x23b8
	char			mBinds[CONSOLE_KEYS][2][0x40];					// +0x23bc: pressed and released
	char			mKeyNames[CONSOLE_KEYS][0x10];					// +0xa3bc
	CPlayer	*mPlayer;													// +0xb3bc (0066e854)
	int				mHistoryPos;									// +0xb3c0
	bool			mLoading;										// +0xb3c4
	bool			mInitialised;									// +0xb3c5
	bool			mRenderingLoadingScreen;						// +0xb3c6
	int				mStatusDepth;									// +0xb3c8
	int				mUnknownB3CC;									// +0xb3cc: the window height / 8
	BOOL			mTrace;											// +0xb3d0
	float			mLastLoadingRender;								// +0xb3d4
	float			mLoadingFrom;									// +0xb3d8
	float			mLoadingTo;										// +0xb3dc
	float			mLoadingFraction;								// +0xb3e0
};

extern CConsole CONSOLE;

#endif
