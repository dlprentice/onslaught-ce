// console.cpp: the in-game console, its commands, variables, key bindings, debug menus, status lines and the
// loading screen. Not in Stuart's release: reconstructed from retail (004294b0..0042d150).

#include "common.h"

#include	"console.h"
#include	"DebugLog.h"		// init:ZERO_FVECTOR, init:ID_FMATRIX
#include	"Platform.h"
#include	"game.h"
#include	"player.h"
#include	"BattleEngine.h"
#include	"DXMemBuffer.h"
#include	"FrontEndText.h"
#include	"Texture.h"
#include	"Controller.h"
#include	"DX.h"
#include	"ltshell.h"
#include	"state.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"FastVB.h"
#include	"text.h"
#include	"CLIParams.h"
#include	<math.h>
#define DIRECTINPUT_VERSION 0x0800
#include	<dinput.h>
#include	<stdarg.h>
#include	<process.h>

// FUNCTION: 0x004294b0 init:ZERO_FVECTOR
// FUNCTION: 0x004294d0 init:ID_FMATRIX



// FUNCTION: 0x004295a0 init:CONSOLE
CConsole	CONSOLE;
static float	sLoadingStartTime;							// 00663490	// defined after CONSOLE, so it comes first (placement evidence)

//******************************************************************************************
// The console's draw (empty on the PC) that no code above defined. Each compiles to a body another function already
// has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address
// each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x0040c640
void CConsole::Render()
{
}
// The key trap while the console is open: a key goes to ProcessKey, a character to the input line.
// FUNCTION: 0x004295c0
bool ConsoleKeyTrap(char key, int event)
{
	if (event == KEY_EVENT_DOWN)
		CONSOLE.ProcessKey(key);
	else if (event == KEY_EVENT_CHAR)
	{
		if (key > 31 && key < 160 && strlen(CONSOLE.mInputLine) < 255)
		{
			char s[2];
			s[0] = key;
			s[1] = 0;
			strcat(CONSOLE.mInputLine, s);
			CONSOLE.mTabLength = -1;
		}
	}
	return TRUE;
}

// FUNCTION: 0x00429650
void con_showcmds(char *cmd)
{
	CONSOLE.Print("Command list\n\n");
	for (CConsoleCommand *c = CONSOLE.mCommands; c; c = c->mNext)
		CONSOLE.Print("%s - %s\n", c->mName, c->mHelp);
	CONSOLE.Print("\n");
}

// FUNCTION: 0x004296b0
void con_showvars(char *cmd)
{
	char	type[32];

	CONSOLE.Print("Variable list\n\n");
	for (CConsoleVar *v = CONSOLE.mVariables; v; v = v->mNext)
	{
		CONSOLE.GetTypeName(v, type);
		CONSOLE.Print("%s (%s) - %s\n", v->mName, type, v->mHelp);
	}
	CONSOLE.Print("\n");
}

// FUNCTION: 0x00429720
void con_show(char *cmd)
{
	char	name[256];
	char	value[256];

	if (sscanf(cmd, "%*s %s", name) != 1)
	{
		CONSOLE.Print("Syntax : show <variable name>\n");
		return;
	}

	CConsoleVar *v = CONSOLE.FindVariable(name);
	if (v)
	{
		CONSOLE.Print("%s = ", name);
		CONSOLE.GetValueString(v, value);
		CONSOLE.Print("%s\n", value);
	}
	else
		CONSOLE.Print("No such variable\n");
}

// FUNCTION: 0x004297e0
void con_set(char *cmd)
{
	char	value[256];
	char	name[256];

	if (sscanf(cmd, "%*s %s %s", name, value) != 2)
	{
		CONSOLE.Print("Syntax : set <variable name> <value>\n");
		return;
	}

	CONSOLE.SetVariable(name, value);
}

// FUNCTION: 0x00429850
void con_bind(char *cmd)
{
	char	key[16];
	char	keyname[16];
	char	command[256];

	if (sscanf(cmd, "%*s %s", key) != 1)
	{
		CONSOLE.Print("Syntax : bind [-]key [command]\n");
		return;
	}

	if (strlen(cmd) > strlen(key) + 6)
		strcpy(command, cmd + strlen(key) + 6);
	else
		strcpy(command, "");

	if (strlen(command) > 63)
	{
		CONSOLE.Print("Bind comand too long!\n");
		return;
	}

	bool down = true;
	strcpy(keyname, key);
	if (key[0] == '-')
	{
		if (strlen(key) > 1)
		{
			down = false;
			strcpy(keyname, key + 1);
		}
	}
	else if (key[0] == '+')
	{
		if (strlen(key) > 1)
		{
			down = true;
			strcpy(keyname, key + 1);
		}
	}

	int up = 0;
	if (!down)
		up = 1;

	int keynum = 0;
	char *name;
	int i;
	for (i = 0, name = CONSOLE.mKeyNames[0]; i < CONSOLE_KEYS - 1; i++, name += 16)
		if (!stricmp(keyname, name))
			keynum = i;

	if (!keynum)
	{
		CONSOLE.Print("Unknown key\n");
		return;
	}

	strcpy(CONSOLE.mBinds[keynum][up], command);
}

// FUNCTION: 0x00429a40
void con_listbinds(char *cmd)
{
	CONSOLE.ListBinds();
}

// FUNCTION: 0x00429a50
void con_echo(char *cmd)
{
	if (strlen(cmd) > 5)
		CONSOLE.Print("%s\n", cmd + 5);
}

// FUNCTION: 0x00429a80
void con_exec(char *cmd)
{
	if (strlen(cmd) > 5)
		CONSOLE.Exec(cmd + 5);
}

// FUNCTION: 0x00429ab0
void con_quit(char *cmd)
{
	GAME.SetQuit(QT_QUIT_TO_FRONTEND);
}

// FUNCTION: 0x00429ad0
void con_useconfiguration(char *cmd)
{
	char	name[256];
	int		n;

	if (strlen(cmd) > 16)
	{
		sscanf(cmd, "%s %d", name, &n);
		CONSOLE.mPlayer->GetBattleEngine()->SetConfiguration(n);
	}
}

// FUNCTION: 0x00429b30
void con_menu(char *cmd)
{
	CONSOLE.mMenuActive = !CONSOLE.mMenuActive;
	if (CONSOLE.mMenuActive)
		CONSOLE.mCurrentMenu = CONSOLE.mRootMenu;
}

// FUNCTION: 0x00429b50
void con_memstats(char *cmd)
{
	char	name[256];

	if (sscanf(cmd, "%*s %s", name))
		MEM_MANAGER.OutputStats(name);
}

// FUNCTION: 0x00429b90
void con_dumpmem(char *cmd)
{
	if (strlen(cmd) > 8)
		MEM_MANAGER.DumpMemory(cmd + 8);
}

// The console's menus. CConsoleVariableMenu is declared first, so its type record is created first (retail's RTTI order is
// Command, Root, Variable, Menu: the reverse of creation), and defined last, after Command, where retail emits its GetName.
class CConsoleVariableMenu;
class CConsoleRootMenu : public CConsoleMenu
{
public:
	// FUNCTION: 0x00429e30
	virtual void	GetName(char *name)				{ strcpy(name, "Onslaught"); }
	virtual int		GetNumEntries()					{ return 0; }
	// FUNCTION: 0x00429e60
	virtual void	GetEntry(int num, char *text)	{ strcpy(text, "???"); }
	virtual void	OnClick(int num)				{}
	virtual bool	GetShowSubmenus()				{ return true; }
};

class CConsoleCommandMenu : public CConsoleMenu
{
public:
	// FUNCTION: 0x00429e90
	virtual void	GetName(char *name)				{ strcpy(name, "Console commands"); }
	virtual int		GetNumEntries();
	virtual void	GetEntry(int num, char *text);
	virtual void	OnClick(int num);
	virtual bool	GetShowSubmenus()				{ return true; }
};

class CConsoleVariableMenu : public CConsoleMenu
{
public:
	// FUNCTION: 0x00429ec0
	virtual void	GetName(char *name)				{ strcpy(name, "Console variables"); }
	virtual int		GetNumEntries();
	virtual void	GetEntry(int num, char *text);
	virtual void	OnClick(int num);
	virtual bool	GetShowSubmenus()				{ return true; }
};

// FUNCTION: 0x00429bc0
void CConsole::Init()
{
	int i;

	mCommands = NULL;
	mVariables = NULL;
	mAlpha = 200;
	mTabLength = -1;
	mTabCount = 1;
	mHistoryPos = -1;
	mLoading = false;
	mRenderingLoadingScreen = false;
	mStatusDepth = 0;
	mMenuActive = false;
	mTrace = FALSE;
	mLoadingTo = 0;
	mLoadingFrom = 0;
	mLoadingFraction = 0;

	if (CLIPARAMS.mResBuilderMode)
	{
		for (i = 0; i < 20; i++)
		{
			char name[256];
			sprintf(name, "Console Line %d", i);
			PLATFORM.SetRegKey(name, "");
		}
	}

#line 242
	mRootMenu = new (MEMTYPE_CONSOLE) CConsoleRootMenu;
	mCurrentMenu = mRootMenu;

#line 247
	mCommandMenu = new (MEMTYPE_CONSOLE) CConsoleCommandMenu;
	mVariableMenu = new (MEMTYPE_CONSOLE) CConsoleVariableMenu;
	mRootMenu->AddSubmenu(mCommandMenu);
	mRootMenu->AddSubmenu(mVariableMenu);

	InitKeyNames();

	for (i = 0; i < CONSOLE_KEYS; i++)
		strcpy(mBinds[i][0], "");
	for (i = 0; i < CONSOLE_LINES; i++)
		strcpy(mLines[i], "");
	for (i = 0; i < CONSOLE_HISTORY; i++)
		strcpy(mHistory[i], "");

	mActive = false;
	PLATFORM.SetKeytrap(NULL);
	mInitialised = true;

	Print("Battle Engine Aquila console OK\n--------------------\n");
}

// FUNCTION: 0x00429ef0
void CConsole::InitDefaultCommands()
{
	RegisterCommand("Help", "Displays a list of console commands", con_showcmds);
	RegisterCommand("ShowCmds", "Displays a list of console commands", con_showcmds);
	RegisterCommand("ShowVars", "Displays a list of console variables", con_showvars);
	RegisterCommand("Show", "Displays the value of a console variable", con_show);
	RegisterCommand("Set", "Sets the value of a console variable", con_set);
	RegisterCommand("Bind", "Binds a command to a key", con_bind);
	RegisterCommand("ListBinds", "Lists the current key bindings", con_listbinds);
	RegisterCommand("Echo", "Echos text to the console", con_echo);
	RegisterCommand("Exec", "Executes a console script from disc", con_exec);
	RegisterCommand("UseConfiguration", "Switches the Battle Engine to the specified configuration", con_useconfiguration);
	RegisterCommand("Exit", "Exits the game", con_quit);
	RegisterCommand("Quit", "Exits the game", con_quit);
	RegisterCommand("Menu", "Toggle menu", con_menu, true);
	RegisterCommand("MemStats", "Output current memory stats to file", con_memstats);
	RegisterCommand("DumpMem", "Dump memory map data", con_dumpmem);

	RegisterVariable("cg_consolealpha", "Alpha of the console background (0-255)", CVar_int, &mAlpha);
}

// FUNCTION: 0x0042a410
void CConsole::ResetLayout()
{
	mLoadingScreen = NULL;
	mUnknown2388 = PLATFORM.GetWindowHeight() - 256;
	mUnknown2384 = PLATFORM.GetWindowHeight();
	mUnknownB3CC = PLATFORM.GetWindowHeight() / 8;
}

// FUNCTION: 0x0042a460
void CConsole::ListBinds()
{
	Print("Key bindings :\n");
	for (int i = 0; i < CONSOLE_KEYS; i++)
	{
		if (strlen(mBinds[i][0]))
			Print(" +%s = %s\n", mKeyNames[i], mBinds[i][0]);
		if (strlen(mBinds[i][1]))
			Print(" -%s = %s\n", mKeyNames[i], mBinds[i][1]);
	}
	Print("\n");
}

// FUNCTION: 0x0042a4f0
void CConsole::HandleBind(int key, int event)
{
	int n = 0;
	if (event == KEY_EVENT_DOWN)
		n = 0;
	else if (event == KEY_EVENT_UP)
		n = 1;

	char *bind = mBinds[(char)key][n];
	if (strlen(bind))
		ExecuteCommand(bind);
}

// FUNCTION: 0x0042a540
void CConsole::GetTypeName(CConsoleVar *var, char *text)
{
	if (var->mType == CVar_int)
		strcpy(text, "int");
	else if (var->mType == CVar_DWORD)
		strcpy(text, "DWORD");
	else if (var->mType == CVar_string)
		strcpy(text, "string");
	else if (var->mType == CVar_bool)
		strcpy(text, "bool");
	else if (var->mType == CVar_float)
		strcpy(text, "float");
	else if (var->mType == CVar_fvector)
		strcpy(text, "fvector");
	else if (var->mType == CVar_fmatrix)
		strcpy(text, "fmatrix");
	else
		strcpy(text, "unknown");
}

// FUNCTION: 0x0042a5f0
void CConsole::GetValueString(CConsoleVar *var, char *text)
{
	if (var->mType == CVar_int)
		sprintf(text, "%d", *(int *)var->mData);
	else if (var->mType == CVar_DWORD)
		sprintf(text, "%d", *(DWORD *)var->mData);
	else if (var->mType == CVar_string)
		sprintf(text, "%s", *(char *)var->mData);
	else if (var->mType == CVar_float)
		sprintf(text, "%f", *(float *)var->mData);
	else if (var->mType == CVar_fvector)
	{
		FVector *v = (FVector *)var->mData;
		sprintf(text, "%f,%f,%f", v->X, v->Y, v->Z);
	}
	else if (var->mType == CVar_fmatrix)
	{
		FMatrix *m = (FMatrix *)var->mData;
		sprintf(text, "\n%f,%f,%f\n%f,%f,%f\n%f,%f,%f",
			m->Row[0].X, m->Row[0].Y, m->Row[0].Z,
			m->Row[1].X, m->Row[1].Y, m->Row[1].Z,
			m->Row[2].X, m->Row[2].Y, m->Row[2].Z);
	}
	else if (var->mType == CVar_bool)
	{
		if (*(bool *)var->mData)
			sprintf(text, "True");
		else
			sprintf(text, "False");
	}
	else
		sprintf(text, "Unknown type!");
}

// FUNCTION: 0x0042a770
CConsoleCommand *CConsole::FindCommand(char *name)
{
	for (CConsoleCommand *c = mCommands; c; c = c->mNext)
		if (!stricmp(c->mName, name))
			return c;
	return NULL;
}

// One Print("OK\n") follows the type chain, and the error paths that print no OK return (found
// 2026-10-05). VC6 copies the shared tail into each arm, as retail lays it out, but `this` stays one web (edi), so
// name takes esi and postfix ebp, and the lone uses of `this` after strcpy and strlen reload it into edi. With a
// Print in every arm, postfix's colouring split `this` and its piece took esi (a near miss).
// FUNCTION: 0x0042a7b0
void CConsole::SetVariable(char *name, char *value)
{
	char *postfix;
	char *dot = strchr(name, '.');
	if (dot)
	{
		postfix = dot + 1;
		*dot = 0;
	}
	else
		postfix = NULL;

	CConsoleVar *v = FindVariable(name);
	if (v)
	{
		if (v->mReadOnly)
		{
			Print("Variable is read only\n");
			return;
		}

		if (v->mType == CVar_int)
			*(int *)v->mData = atoi(value);
		else if (v->mType == CVar_DWORD)
			*(DWORD *)v->mData = atoi(value);
		else if (v->mType == CVar_bool)
		{
			if (value[0] == 'T' || value[0] == 't' || value[0] == '1')
				*(bool *)v->mData = true;
			else
				*(bool *)v->mData = false;
		}
		else if (v->mType == CVar_string)
			strcpy((char *)v->mData, value);
		else if (v->mType == CVar_float)
			*(float *)v->mData = (float)atof(value);
		else if (v->mType == CVar_fvector)
		{
			if (!postfix)
			{
				float x, y, z;
				if (sscanf(value, "%f,%f,%f", &x, &y, &z) != 3)
				{
					CONSOLE.Print("FVectors must be specified in <x>,<y>,<z> form.\n");
					return;
				}
				((FVector *)v->mData)->X = x;
				((FVector *)v->mData)->Y = y;
				((FVector *)v->mData)->Z = z;
			}
			else if (!stricmp(postfix, "x"))
				((FVector *)v->mData)->X = (float)atof(value);
			else if (!stricmp(postfix, "y"))
				((FVector *)v->mData)->Y = (float)atof(value);
			else if (!stricmp(postfix, "z"))
				((FVector *)v->mData)->Z = (float)atof(value);
			else
				CONSOLE.Print("Only .X .Y and .Z are valid postfixes for FVectors.\n");
		}
		else if (v->mType == CVar_fmatrix)
		{
			if (!postfix)
			{
				float x1, y1, z1, x2, y2, z2, x3, y3, z3;
				if (sscanf(value, "%f,%f,%f,%f,%f,%f,%f,%f,%f", &x1, &y1, &z1, &x2, &y2, &z2, &x3, &y3, &z3) != 9)
				{
					CONSOLE.Print("FMatrices must be specified in <x1>,<y1>,<z1>,<x2>,<y2>,<z2>,<x3>,<y3>,<z3> form.\n");
					return;
				}
				((FMatrix *)v->mData)->Row[0].X = x1;
				((FMatrix *)v->mData)->Row[0].Y = y1;
				((FMatrix *)v->mData)->Row[0].Z = z1;
				((FMatrix *)v->mData)->Row[1].X = x2;
				((FMatrix *)v->mData)->Row[1].Y = y2;
				((FMatrix *)v->mData)->Row[1].Z = z2;
				((FMatrix *)v->mData)->Row[2].X = x3;
				((FMatrix *)v->mData)->Row[2].Y = y3;
				((FMatrix *)v->mData)->Row[2].Z = z3;
			}
			else
			{
				if (strlen(postfix) != 2)
				{
					CONSOLE.Print("FMatrix postfixes must be in the form \".<row><column>\".\nEg : \".12\", \".2x\" or \".yx\".\n");
					return;
				}

				int row;
				if (postfix[0] == '1' || postfix[0] == 'x' || postfix[0] == 'X')
					row = 1;
				else if (postfix[0] == '2' || postfix[0] == 'y' || postfix[0] == 'Y')
					row = 2;
				else if (postfix[0] == '3' || postfix[0] == 'z' || postfix[0] == 'Z')
					row = 3;
				else
				{
					CONSOLE.Print("Invalid row postfix.\n");
					return;
				}

				if (postfix[1] == '1' || postfix[1] == 'x' || postfix[1] == 'X')
					((FMatrix *)v->mData)->Row[row - 1].X = (float)atof(value);
				else if (postfix[1] == '2' || postfix[1] == 'y' || postfix[1] == 'Y')
					((FMatrix *)v->mData)->Row[row - 1].Y = (float)atof(value);
				else if (postfix[1] == '3' || postfix[1] == 'z' || postfix[1] == 'Z')
					((FMatrix *)v->mData)->Row[row - 1].Z = (float)atof(value);
				else
				{
					CONSOLE.Print("Invalid column postfix.\n");
					return;
				}
			}
		}
		else
		{
			Print("Unknown type!\n");
			return;
		}
		Print("OK\n");
	}
	else
		Print("No such variable\n");
}

// FUNCTION: 0x0042ad30
void CConsole::Exec(char *filename)
{
	Print("Executing script %s\n", filename);

	CDXMemBuffer	file;
	if (!file.InitFromFile(filename, MEMTYPE_MEMBUFFER, TRUE, 0))
	{
		Print("File not found\n");
		return;
	}

	char	line[256];
	while (!file.EndOfFile())
	{
		char	c = '.';
		int		n = 0;
		while (!file.EndOfFile() && c != '\n')
		{
			file.Read(&c, 1);
			if (c != '\n' && c != '\r' && c != 0 && !file.EndOfFile())
				line[n++] = c;
		}
		line[n] = 0;
		ExecuteCommand(line);
	}
	file.Close();
}

// FUNCTION: 0x0042ae70
void CConsole::Shutdown()
{
	void *next;
	CConsoleCommand *c = mCommands;
	while (c)
	{
		next = c->mNext;
		delete c;
		c = (CConsoleCommand *)next;
	}
	CConsoleVar *v = mVariables;
	while (v)
	{
		next = v->mNext;
		delete v;
		v = (CConsoleVar *)next;
	}
	mCommands = NULL;
	mVariables = NULL;

	SAFE_DELETE(mRootMenu);
	SAFE_DELETE(mCommandMenu);
	SAFE_DELETE(mVariableMenu);
}

// FUNCTION: 0x0042af20
void CConsole::ClearCommandsAndVariables()
{
	void *next;
	CConsoleCommand *c = mCommands;
	while (c)
	{
		next = c->mNext;
		delete c;
		c = (CConsoleCommand *)next;
	}
	CConsoleVar *v = mVariables;
	while (v)
	{
		next = v->mNext;
		delete v;
		v = (CConsoleVar *)next;
	}
	mCommands = NULL;
	mVariables = NULL;
}

// FUNCTION: 0x0042af80 ?RegisterCommand@CConsole@@QAEXPAD0P6AX0@Z_N@Z
void CConsole::RegisterCommand(char *name, char *help, void (*command)(char *), bool flag)
{
	CConsoleCommand *c = FindCommand(name);
	if (!c)
	{
#line 805
		c = new (MEMTYPE_CONSOLE) CConsoleCommand;
		c->mNext = mCommands;
		mCommands = c;
	}
	strcpy(c->mName, name);
	strcpy(c->mHelp, help);
	c->mFunction = command;
	c->mFlag = flag;
}

// FUNCTION: 0x0042b040
void CConsole::RegisterVariable(char *name, char *help, ECVarType type, void *var, bool read_only, bool b)
{
	CConsoleVar *v = FindVariable(name);
	if (!v)
	{
#line 830
		v = new (MEMTYPE_CONSOLE) CConsoleVar;
		v->mNext = mVariables;
		mVariables = v;
	}
	strcpy(v->mName, name);
	strcpy(v->mHelp, help);
	v->mType = type;
	v->mData = var;
	v->mReadOnly = read_only;
	v->mUnknownA9 = b;
}

inline void CConsole::Toggle()
{
	mActive = !mActive;
	if (mActive)
	{
		PLATFORM.SetKeytrap((pKeyTrapper)ConsoleKeyTrap);
		mMenuActive = false;
	}
	else
		PLATFORM.SetKeytrap(NULL);
}

// FUNCTION: 0x0042b120
void CConsole::ProcessKey(char key)
{
	if (key == DIK_GRAVE)
	{
		Toggle();
		return;
	}

	if (key == (char)DIK_UP)
	{
		if (mHistoryPos < CONSOLE_HISTORY - 1)
			mHistoryPos++;
		strcpy(mInputLine, mHistory[mHistoryPos]);
		return;
	}

	if (key == (char)DIK_DOWN)
	{
		if (mHistoryPos >= 0)
			mHistoryPos--;
		if (mHistoryPos > -1)
			strcpy(mInputLine, mHistory[mHistoryPos]);
		else
			strcpy(mInputLine, "");
		return;
	}

	if (key == DIK_ESCAPE)
	{
		GAME.SetQuit(QT_QUIT_TO_FRONTEND);
		return;
	}

	if (key == DIK_TAB)
	{
		if (mTabLength != -1)
			mInputLine[mTabLength] = 0;
		else
			mTabCount = 1;

		mTabLength = strlen(mInputLine);
		int count = mTabCount++;

		if (_strnicmp(mInputLine, "Show ", 5) && _strnicmp(mInputLine, "Set ", 4))
		{
			for (CConsoleCommand *c = mCommands; c; c = c->mNext)
			{
				if (!_strnicmp(mInputLine, c->mName, strlen(mInputLine)) && !--count)
				{
					strcpy(mInputLine, c->mName);
					strcat(mInputLine, " ");
					return;
				}
			}
		}
		else
		{
			char	cmd[256];
			char	part[256];
			sscanf(mInputLine, "%s %s", cmd, part);
			for (CConsoleVar *v = mVariables; v; v = v->mNext)
			{
				if (!_strnicmp(part, v->mName, strlen(part)) && !--count)
				{
					sprintf(mInputLine, "%s %s ", cmd, v->mName);
					return;
				}
			}
		}
		mTabLength = -1;
		return;
	}

	if (key == DIK_RETURN || key == (char)DIK_NUMPADENTER)
	{
		for (int i = CONSOLE_HISTORY - 1; i > 0; i--)
			strcpy(mHistory[i], mHistory[i - 1]);
		strcpy(mHistory[0], mInputLine);
		mHistoryPos = -1;
		Print(">%s\n", mInputLine);
		ExecuteCommand(mInputLine);
		strcpy(mInputLine, "");
		return;
	}

	if (key == DIK_BACK)
	{
		if (strlen(mInputLine))
			mInputLine[strlen(mInputLine) - 1] = 0;
	}
}

// FUNCTION: 0x0042b500
void CConsole::Status(char *text)
{
	if (!mInitialised)
		return;

	char	line[256];
	line[0] = 0;
	for (int i = mStatusDepth; i > 0; i--)
		strcat(line, " ");
	strcat(line, text);
	while (strlen(line) < 80)
		strcat(line, " ");
	line[80] = 0;
	strcat(line, "[...]");
	Print("%s\n", line);

	mStatusDepth++;
	if (mStatusDepth > 40)
		mStatusDepth = 0;
}

// FUNCTION: 0x0042b650
void CConsole::StatusUpdateLine(char *text, char *result)
{
	if (!mInitialised)
		return;

	char	line[256];
	line[0] = 0;
	for (int i = mStatusDepth - 1; i > 0; i--)
		strcat(line, " ");
	strcat(line, text);
	while (strlen(line) < 80)
		strcat(line, " ");
	line[80] = 0;

	int n;
	for (n = 0; n < CONSOLE_LINES; n++)
	{
		if (!_strnicmp(mLines[n], line, 80))
		{
			mLines[n][80] = 0;
			strcat(mLines[n], result);
			if (n == 0)
				Print("\n");
			if (mLoading)
				CONSOLE.RenderLoadingScreen();
			return;
		}
	}

	Print("%s (untraceable status line) : %s\n", text, result);
	if (mLoading)
		CONSOLE.RenderLoadingScreen();
}

// FUNCTION: 0x0042b800
void CConsole::StatusDone(char *text, bool ok)
{
	if (mInitialised)
	{
		if (ok)
			StatusUpdateLine(text, "[OK]");
		else
			StatusUpdateLine(text, "[FAILED]");
		mStatusDepth--;
	}
}

// FUNCTION: 0x0042b840
void CConsole::Print(char *fmt, ...)
{
	if (!mInitialised)
		return;

	char	name[256];
	char	text[0x2000];
	va_list	args;
	va_start(args, fmt);
	vsprintf(text, fmt, args);

	if (mTrace)
	{
		TRACE("Con>");
		TRACE(text);
	}

	char *line = mLines[0];
	int pos = 0;
	for (int i = 0; i < (int)strlen(text); i++)
	{
		if (text[i] != '\n')
			line[pos++] = text[i];
		else
		{
			line[pos] = 0;
			pos = 0;
			for (int j = CONSOLE_LINES - 1; j > 0; j--)
				strcpy(mLines[j], mLines[j - 1]);
			strcpy(line, "");
		}
	}

	if (CLIPARAMS.mResBuilderMode)
	{
		for (int i = 0; i < 20; i++)
		{
			sprintf(name, "Console Line %d", i);
			PLATFORM.SetRegKey(name, mLines[i]);
		}
	}
}

// FUNCTION: 0x0042b9c0
void CConsole::ExecuteCommand(char *line)
{
	char	cmd[256];

	if (strlen(line) < 1)
		return;

	int i = 0;
	while (line[i] != ' ' && line[i] != '\t' && i < (int)strlen(line))
	{
		cmd[i] = line[i];
		i++;
	}
	cmd[i] = 0;

	CConsoleCommand *c = mCommands;
	bool found = false;
	while (c && !found)
	{
		if (!stricmp(c->mName, cmd))
		{
			c->mFunction(line);
			found = true;
		}
		c = c->mNext;
	}

	if (!found)
		Print("Unknown command\n");
}

// FUNCTION: 0x0042ba90
BOOL CConsole::MenuUp()
{
	if (!mMenuActive)
		return FALSE;

	mMenuSelection--;
	if (mMenuSelection < 0)
		mMenuSelection = 0;
	return TRUE;
}

// FUNCTION: 0x0042bac0
BOOL CConsole::MenuDown()
{
	if (!mMenuActive)
		return FALSE;

	mMenuSelection++;
	int n = mCurrentMenu->GetNumEntries();
	if (mCurrentMenu->GetShowSubmenus())
	{
		n += mCurrentMenu->mNumChildren;
		if (mCurrentMenu->mParent)
			n++;
	}
	if (mMenuSelection >= n)
		mMenuSelection = n - 1;
	return TRUE;
}

static BOOL SelectConsoleMenuEntry(CConsole *console)
{
	int sel = console->mMenuSelection;
	if (sel < console->mCurrentMenu->GetNumEntries())
	{
		console->mCurrentMenu->OnClick(sel);
		return TRUE;
	}

	CConsoleMenu *m = console->mCurrentMenu->mFirstChild;
	for (int i = console->mMenuSelection - console->mCurrentMenu->GetNumEntries(); i; i--)
		m = m->mNext;

	if (m)
	{
		console->mCurrentMenu = m;
		console->mUnknown23ac = 0;
		console->mMenuSelection = 0;
	}
	else
	{
		console->mCurrentMenu = console->mCurrentMenu->mParent;
		console->mUnknown23ac = 0;
		console->mMenuSelection = 0;
	}
	return TRUE;
}

// FUNCTION: 0x0042bb30
BOOL CConsole::MenuSelect()
{
	if (!mMenuActive)
		return FALSE;
	return SelectConsoleMenuEntry(this);
}

// FUNCTION: 0x0042bbc0
void CConsole::SetLoading(bool on, BOOL a, BOOL b)
{
	if (mLoading == on)
		return;

	CController::SetNonInteractiveSection(on);

	if (on)
	{
		sLoadingStartTime = PLATFORM.GetSysTimeFloat();
		if (a && !mLoadingScreen)
		{
			mLoadingScreen = CDXTexture::FindTexture("loadingscreen.tga", 0, 0, 1, 1, 1);
			mLoadingScreen->mUnknown155 = 1;
		}
		mLoadingFraction = 0;
		mLoadingTo = 0;
		mLoadingFrom = 0;
	}
	else
	{
		char	text[200];

		TRACE("Deleting loading screen resources...\n");
		SAFE_RELEASE(mLoadingScreen);
		TRACE("...done!\n");
		sprintf(text, "Loading took %f seconds\n", PLATFORM.GetSysTimeFloat() - sLoadingStartTime);
		TRACE(text);
	}
	mLoading = on;
}

// FUNCTION: 0x0042bcf0
void CConsole::InitKeyNames()
{
	int i;

	for (i = 0; i < CONSOLE_KEYS - 1; i++)
		strcpy(mKeyNames[i], "Unknown");

	for (i = '0'; i <= '9'; i++)
	{
		mKeyNames[i][0] = i;
		mKeyNames[i][1] = 0;
	}
	for (i = 'A'; i <= 'Z'; i++)
	{
		mKeyNames[i][0] = i;
		mKeyNames[i][1] = 0;
	}

	strcpy(mKeyNames[DIK_BACK], "Backspace");
	strcpy(mKeyNames[DIK_TAB], "Tab");
	strcpy(mKeyNames[DIK_RETURN], "Return");
	strcpy(mKeyNames[DIK_LSHIFT], "Shift");
	strcpy(mKeyNames[DIK_LCONTROL], "Ctrl");
	strcpy(mKeyNames[DIK_ESCAPE], "Escape");
	strcpy(mKeyNames[DIK_SPACE], "Space");
	strcpy(mKeyNames[DIK_END], "End");
	strcpy(mKeyNames[DIK_HOME], "Home");
	strcpy(mKeyNames[DIK_LEFT], "Left");
	strcpy(mKeyNames[DIK_RIGHT], "Right");
	strcpy(mKeyNames[DIK_UP], "Up");
	strcpy(mKeyNames[DIK_DOWN], "Down");
	strcpy(mKeyNames[DIK_INSERT], "Insert");
	strcpy(mKeyNames[DIK_DELETE], "Delete");
	strcpy(mKeyNames[DIK_NUMPAD0], "Numpad0");
	strcpy(mKeyNames[DIK_NUMPAD1], "Numpad1");
	strcpy(mKeyNames[DIK_NUMPAD2], "Numpad2");
	strcpy(mKeyNames[DIK_NUMPAD3], "Numpad3");
	strcpy(mKeyNames[DIK_NUMPAD4], "Numpad4");
	strcpy(mKeyNames[DIK_NUMPAD5], "Numpad5");
	strcpy(mKeyNames[DIK_NUMPAD6], "Numpad6");
	strcpy(mKeyNames[DIK_NUMPAD7], "Numpad7");
	strcpy(mKeyNames[DIK_NUMPAD8], "Numpad8");
	strcpy(mKeyNames[DIK_NUMPAD9], "Numpad9");
	strcpy(mKeyNames[DIK_MULTIPLY], "Numpad*");
	strcpy(mKeyNames[DIK_ADD], "Numpad+");
	strcpy(mKeyNames[DIK_SUBTRACT], "Numpad-");
	strcpy(mKeyNames[DIK_DIVIDE], "Numpad/");
	strcpy(mKeyNames[DIK_F1], "F1");
	strcpy(mKeyNames[DIK_F2], "F2");
	strcpy(mKeyNames[DIK_F3], "F3");
	strcpy(mKeyNames[DIK_F4], "F4");
	strcpy(mKeyNames[DIK_F5], "F5");
	strcpy(mKeyNames[DIK_F6], "F6");
	strcpy(mKeyNames[DIK_F7], "F7");
	strcpy(mKeyNames[DIK_F8], "F8");
	strcpy(mKeyNames[DIK_F9], "F9");
	strcpy(mKeyNames[DIK_F10], "F10");
	strcpy(mKeyNames[DIK_F11], "F11");
	strcpy(mKeyNames[DIK_F12], "F12");
	strcpy(mKeyNames[DIK_PAUSE], "Numlock");			// retail: 0xc5 (0042c3b9), not DIK_NUMLOCK
	strcpy(mKeyNames[DIK_SCROLL], "Scroll");
}

// FUNCTION: 0x0042c420
CConsoleMenu::CConsoleMenu()
{
	mNext = NULL;
	mParent = NULL;
	mFirstChild = NULL;
	mNumChildren = 0;
}

// FUNCTION: 0x0042c440
void CConsoleMenu::LinkChildAtHead(CConsoleMenu *child)
{
	child->mParent = this;
	child->mNext = mFirstChild;
	mFirstChild = child;
	mNumChildren++;
}

// FUNCTION: 0x0042c460
void CConsoleMenu::UnlinkChild(CConsoleMenu *child)
{
	if (child == CONSOLE.mCurrentMenu)
	{
		CONSOLE.mCurrentMenu = child->mParent;
		CONSOLE.mUnknown23ac = 0;
		CONSOLE.mMenuSelection = 0;
	}

	CConsoleMenu *m = mFirstChild;
	while (m)
	{
		if (m == child)
		{
			mFirstChild = m->mNext;
			m->mNext = NULL;
			m->mParent = NULL;
			mNumChildren--;
			return;
		}
	}
}

// FUNCTION: 0x0042c4b0
int CConsoleCommandMenu::GetNumEntries()
{
	int n = 0;
	for (CConsoleCommand *c = CONSOLE.mCommands; c; c = c->mNext)
		n++;
	return n;
}

// FUNCTION: 0x0042c4d0
void CConsoleCommandMenu::GetEntry(int num, char *text)
{
	CConsoleCommand *c = CONSOLE.GetCommand(num);
	if (c)
		sprintf(text, "%s (%s)", c->mName, c->mHelp);
	else
		sprintf(text, "???");
}

// FUNCTION: 0x0042c530
void CConsoleCommandMenu::OnClick(int num)
{
	char	text[256];

	CConsoleCommand *c = CONSOLE.GetCommand(num);
	sprintf(text, "%s ", c->mName);
	strcpy(CONSOLE.mInputLine, text);
	CONSOLE.Toggle();
}

// FUNCTION: 0x0042c5e0
int CConsoleVariableMenu::GetNumEntries()
{
	int n = 0;
	for (CConsoleVar *v = CONSOLE.mVariables; v; v = v->mNext)
		n++;
	return n;
}

// FUNCTION: 0x0042c600
void CConsoleVariableMenu::GetEntry(int num, char *text)
{
	char	value[256];
	char	type[256];

	CConsoleVar *v = CONSOLE.GetVariable(num);
	if (v)
	{
		CONSOLE.GetValueString(v, value);
		CONSOLE.GetTypeName(v, type);
		sprintf(text, "%s (%s) %s\n %s", v->mName, type, v->mHelp, value);
	}
	else
		sprintf(text, "???");
}

// FUNCTION: 0x0042c6a0
void CConsoleVariableMenu::OnClick(int num)
{
	char	text[256];

	CConsoleVar *v = CONSOLE.GetVariable(num);
	sprintf(text, "set %s ", v->mName);
	strcpy(CONSOLE.mInputLine, text);
	CONSOLE.Toggle();
}

// FUNCTION: 0x0042c750
void CConsole::FatalFileError(char *name, SINT a)
{
	char	text[400];

	strcpy(text, FromWCHAR(Localization_GetStringById(0xcc)));
	strcat(text, " ");
	strcat(text, name);
	FatalError(text, -1);
}

bool	pause_for_showing_controls = false;						// 0066e8c0 (Stuart's name, game.cpp)
// The disc-error message, three lines in each language (English, French, German, Spanish, Italian, Dutch): retail's
// console.obj defines this table just before draw_string_at_bottom (006248f4, its strings after it) and nothing in the
// executable reads it. The names are ours; the text is the specimen's. Its user is not in the PC build.
char	*DISC_ERROR_TEXT[6][3] =							// 006248f4 (name ours)
{
	{ "THERE IS A PROBLEM WITH", "THE DISC YOU ARE USING.", "IT MAY BE DIRTY OR DAMAGED." },
	{ "LE DISQUE UTILISE PRESENTE", "UNE ANOMALIE. IL EST", "PEUT-ETRE SALE OU ENDOMMAGE." },
	{ "BEI DER BENUTZTEN DVD IST EIN", "PROBLEM AUFGETRETEN. M\xd6GLICHERWEISE", "IST SIE VERSCHMUTZT ODER BESCH\xc4" "DIGT." },
	{ "HAY UN PROBLEMA CON", "EL DISCO QUE EST\xc1 USANDO.", "PUEDE ESTAR SUCIO O DA\xd1" "ADO." },
	{ "IL DISCO IN USO HA", "QUALCHE PROBLEMA. POTREBBE", "ESSERE SPORCO O DANNEGGIATO." },
	{ "ER IS EEN PROBLEEM MET DE", "DVD DIE U GEBRUIKT. DE DVD", "IS MOGELIJK VUIL OF BESCHADIGD." },
};
#line 1249
bool	draw_string_at_bottom = true;					// 0062493c (Stuart's name, game.cpp)

// A FASTVB vertex: screen position, rhw, colour, texture coordinate.
struct SLoadingVertex
{
	float	x, y, z, rhw;
	DWORD	col;
	float	u, v;
};

inline void SetLoadingVertex(SLoadingVertex *v, float x, float y, DWORD col)
{
	v->x = x;
	v->y = y;
	v->z = 0;
	v->rhw = 1.f;
	v->col = col;
	v->u = x / PLATFORM.GetWindowWidth();
	v->v = y / PLATFORM.GetWindowHeight();
}

// A texture's size before the resolution loss shift. As functions of the texture, the inlined bodies bind it as a parameter,
// which gives retail's byte load of the shift (mov cl,[eax+0xb2]) and its order of th and tw.
static inline int ShiftedHeight(CDXTexture *t) { return t->mHeight << t->mLossShift; }
static inline int ShiftedWidth(CDXTexture *t) { return t->mWidth << t->mLossShift; }

// STAND-IN (frame slots): one position variable px, set to `f * width + 80.f` before
// each SetLoadingVertex. Retail keeps one variable with two definitions in a slot of its own, apart from each call's
// copy; with it px (weight 4) opens its own class and y joins retail's class with the conversion temporaries and tw2.
// The expression written in each call, or two separate locals, gives 0/32; one px computed once 22/96. Which source
// Stuart wrote is unknown. Natural part: x's initialiser has no outer parentheses. The parenthesised product cost a
// 0x162 x87 node in scheduler region 33 (81-node cap); without it the cut moves one node, so the 4th DrawText's x load
// is issued after its first push as in retail (mov edx,[esp+0x48]; region 34: 21 -> 20 nodes).
// FUNCTION: 0x0042c810
void CConsole::RenderLoadingScreen(BOOL a, char mode)
{
	if (mRenderingLoadingScreen)
		return;
	if (!mLoadingScreen && mode != 2 && mode != 3)
		return;

	if (mLoadingFraction != 100.f && mode != 1)
	{
		if (PLATFORM.GetSysTimeFloat() < mLastLoadingRender + 0.05f && PLATFORM.GetSysTimeFloat() >= mLastLoadingRender)
			return;
	}

	if (mode < 2)
		PLATFORM.Process();
	mLastLoadingRender = PLATFORM.GetSysTimeFloat();
	mRenderingLoadingScreen = true;
	STATE.UseDefault();

	if (PLATFORM.BeginScene())
	{
		PLATFORM.ClearScreen(0);
		LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
		LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		LT.SRS(D3DRS_ZENABLE, FALSE);
		RENDERINFO.SetFogEnabled(false);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		RENDERINFO.Apply(0, 1);

		float top = (float)(PLATFORM.GetWindowHeight() - 55);
		float bottom = (float)(PLATFORM.GetWindowHeight() - 35);
		float right = (float)(PLATFORM.GetWindowWidth() - 80);

		if (mLoadingScreen)
		{
			int th = ShiftedHeight(mLoadingScreen);
			int tw = ShiftedWidth(mLoadingScreen);
			int w = PLATFORM.GetWindowWidth();
			int h = PLATFORM.GetWindowHeight();
			CSPRITERENDERER::DrawColouredSprite(0.f, 0.f, 0.01f, mLoadingScreen, 0xffffffff,
				(float)PLATFORM.GetWindowWidth() / tw, (float)PLATFORM.GetWindowHeight() / th);

			int th2 = ShiftedHeight(mLoadingScreen);
			int tw2 = ShiftedWidth(mLoadingScreen);
			int w2 = PLATFORM.GetWindowWidth();
			int h2 = PLATFORM.GetWindowHeight();
			CSPRITERENDERER::DrawSprite(78.f, top - 2.f, 0.009f, mLoadingScreen, SA_TOP_LEFT, SPT_NONE, 0, 0, 0xff000000,
				(right + 4.f - 80.f) / tw2, (bottom + 4.f - top) / th2,
				80.f / PLATFORM.GetWindowWidth(), right / PLATFORM.GetWindowWidth(),
				top / PLATFORM.GetWindowHeight(), bottom / PLATFORM.GetWindowHeight());
		}

		float t = (float)fmod(PLATFORM.GetSysTimeFloat(), 20.0) * (PI / 2);
		SLoadingVertex *verts;
		if (FASTVB.LockAligned((void **)&verts, 42) != 0xffff)
		{
			SLoadingVertex *v = verts;
			float width = right - 80.f;
			for (float f = 0; f <= 1.001f; f += 0.05f)
			{
				int alpha = (int)((((float)sin(t - f * (PI * 2)) + 1.f) * 0.2f) * -255.f);
				DWORD col = 0xff000000 - alpha * 0x10101;
				// STAND-IN (see the comment above the function): one px, set to the same value before each call.
				float px = f * width + 80.f;
				SetLoadingVertex(v, px, bottom, col);
				v++;
				if (bottom != top)
				{
					px = f * width + 80.f;	// STAND-IN: the same value again
					SetLoadingVertex(v, px, top, col);
					v++;
				}
			}
			if (mLoadingScreen)
				LT.D3D_SetTexture(0, mLoadingScreen->GetTexture());
			else
				LT.D3D_SetTexture(0, NULL);
			FASTVB.RenderStrip();
		}

		LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
		DWORD col = PLAYABLE_DEMO ? 0xffffffff : 0xffa0a0a0;
		if (draw_string_at_bottom && PLATFORM.Font(FONT_NORMAL) && PLATFORM.Font(FONT_NORMAL)->IsValid())
		{
			// The lookup itself, not the inline GetString wrapper: the wrapper's parameter copy is coloured eax before the
			// chooser runs and gives the flag's select an eax preference; retail builds it in edx (0042cd4b..0042cd66).
			WCHAR *text = TEXT.GetStringById(pause_for_showing_controls ? 0x7dea02c : 0x3848a7);
			if (mode == 3)
				text = Localization_GetStringById(0xdc);

			SIZE size;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
			float x = (PLATFORM.GetWindowWidth() - size.cx) * 0.5f;
			float y = (float)(PLATFORM.GetWindowHeight() - size.cy) - 55.f;
			PLATFORM.Font(FONT_NORMAL)->DrawText(x - 1.f, y, 0xff000000, text, 0, 0, 1.f);
			PLATFORM.Font(FONT_NORMAL)->DrawText(x + 1.f, y, 0xff000000, text, 0, 0, 1.f);
			PLATFORM.Font(FONT_NORMAL)->DrawText(x, y - 1.f, 0xff000000, text, 0, 0, 1.f);
			PLATFORM.Font(FONT_NORMAL)->DrawText(x, y + 1.f, 0xff000000, text, 0, 0, 1.f);
			PLATFORM.Font(FONT_NORMAL)->DrawText(x, y, col, text, 0, 0, 1.f);
		}

		PLATFORM.EndScene();
		PLATFORM.Flip(FALSE);
		LT.SRS(D3DRS_ZENABLE, TRUE);
		LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
		LT.SetModulate(0);
	}

	mRenderingLoadingScreen = false;
	if (mLoadingFraction == 100.f)
		mLoadingFraction = 100.001f;
}

// FUNCTION: 0x0042cf40
void CConsole::SetLoadingRange(float from, float to)
{
	mLoadingFrom = from;
	mLoadingTo = to;
	mLoadingFraction = from;
	RenderLoadingScreen(TRUE);
}

// FUNCTION: 0x0042cf70
void CConsole::SetLoadingFraction(float fraction)
{
	mLoadingFraction = (mLoadingTo - mLoadingFrom) * fraction + mLoadingFrom;
	RenderLoadingScreen(TRUE);
}

// FUNCTION: 0x0042cfa0
void CConsole::FatalError(char *text, SINT aParam)
{
	char	message[2000];

	SETUP_HISTORY.AddMessage("Fatal error: %s %d", text, aParam);
	ShutdownMouse();

	if (aParam == -1)
		sprintf(message, "message ~%s~%s~", text, FromWCHAR(Localization_GetStringById(0xcb)));
	else
		sprintf(message, "message ~%s~%s %d~", text, FromWCHAR(Localization_GetStringById(0xcb)), aParam);

	for (char *c = message; *c; c++)
		if (*c == '\n' || *c == '\r' || *c == ',')
			*c = ' ';

	_spawnl(_P_NOWAIT, "message", message, NULL);
	ExitProcess(1);
}

// FUNCTION: 0x0042d080
void CConsole::FatalError(bool bNonFatal, SINT aStringId, SINT aParam)
{
	if (!bNonFatal)
		FatalError(FromWCHAR(Localization_GetStringById(aStringId)), aParam);
}

// FUNCTION: 0x0042d0b0
void CConsole::FatalResourceFileError(char *name)
{
	char	text[400];

	strcpy(text, FromWCHAR(Localization_GetStringById(0xcc)));
	strcat(text, " ");
	strcat(text, name);
	FatalError(text, -1);
}
