// SPDX-License-Identifier: GPL-3.0-only
// Complete production save-page Process/CreateSave/ButtonPressed + actual PCMemoryCard and codec/approval code.
// UI/render/card status are authored boundaries; file bytes are real private fixtures. No real save is used.
#define main UnusedSaveNamesHarnessMain
#include "save_names_driver.cpp"
#undef main
#include <new>
#include <tuple>
#include "ui_constants.inc"
#define ASSERT(condition) Check(bool(condition), "frontend assertion")
#define FO(value) value
#define TRACE(...) ((void)0)
#define MEMTYPE_SCRATCHPAD 1
#define SAFE_DELETE(value) delete[] value
#define XBOX 2
#define PS2 3
using BYTE = unsigned char;
using SINT = int;
using EFEPState = int;
using EFETextHack = int;
void *operator new[](size_t bytes, int) { return ::operator new[](bytes); }
static unsigned uiWrites, uiDeletes, uiAdmissions;
static bool cardPresent = true, cardFormatted = true;
struct CardBoundary : CPCMemoryCard
{
	void Update() {}
	int GetCardInfo(int card, BOOL *present, BOOL *formatted, int *free, int *total)
	{
		int result = CPCMemoryCard::GetCardInfo(card,present,formatted,free,total);
		if (present) *present=cardPresent; if(formatted) *formatted=cardFormatted;
		return result;
	}
	bool CardBeenUnpluggedSinceLastTimeIAsked(int) { return false; }
	int Format(int) { return 0; }
	int CreateSave(int card,WCHAR *name,int *slot,BOOL overwrite)
	{ uiAdmissions++; return CPCMemoryCard::CreateSave(card,name,slot,overwrite); }
	int WriteSave(int card,int slot,WCHAR *name,void *buffer,int size)
	{ uiWrites++; return CPCMemoryCard::WriteSave(card,slot,name,buffer,size); }
	int DeleteSave(int card,int slot,WCHAR *name)
	{ uiDeletes++; return CPCMemoryCard::DeleteSave(card,slot,name); }
} MEMORYCARD;
struct TextBoundary
{
	template<class... A> WCHAR *GetText(A...) { static WCHAR text[]={'x',0}; return text; }
};
struct FrontendBoundary
{
	enum { AUTO_SAVE_NOT, AUTO_SAVE_NORMAL, AUTO_SAVE_PRETEND };
	int autosave=AUTO_SAVE_NOT, page=0;
	TextBoundary mFrontEndText;
	struct { bool m_Fresh=true; } mFEPVirtualKeyboard;
	int GetMemoryCardNumber() { return 0; }
	int GetAutoSave() { return autosave; }
	void SetAutoSave(int value) { autosave=value; }
	void SetSaveMode(BOOL) {}
	void SetPage(int value,int) { page=value; }
	int GetSuccessPage() { return FEP_MAIN; }
	int GetSuccessTransTime() { return 0; }
	void StartMCOperation(WCHAR *) {}
	void EndMCOperation() {}
	void PlaySound(int) {}
} FRONTEND;
using CFrontEnd = FrontendBoundary;
#define FET FRONTEND.mFrontEndText
struct MessboxBoundary
{
	bool displayed=false;
	int question=0,choice=0;
	bool BeingDisplayed() { return displayed; }
	int GetQuestion() { return question; }
	int GetFinalChoice() { return choice; }
	void Kill() { displayed=false; question=0; }
	void SetOnDismiss(int,int) {}
	template<class... A> void Create(A... args)
	{ displayed=true; auto values=std::make_tuple(args...); question=int(std::get<sizeof...(A)-1>(values)); }
} FEMESSBOX;
struct { int Font(int) { return 0; } } PLATFORM;
struct { void SetFogEnabled(bool) {} } RENDERINFO;
struct CareerBoundary
{
	int SizeOfSaveGame() { return 7; }
	bool InProgress() { return true; }
	void SaveAndMarkInProgress(char *data) { memcpy(data,"UPDATED",7); }
} CAREER;
char gSaveFromOptions;
struct CFEPSaveGame
{
	WCHAR mSaveGameName[256]={},mExistingSaveName[256]={};
	int mExistingSave=-1,mCX=0,mCY=0;
	bool mCareerWasInProgress=false;
	void Process(EFEPState);
	void CreateSave();
	void ButtonPressed(SINT,float);
	void RemovedMUWhinge(EFETextHack) { FEMESSBOX.Kill(); }
	void AskIfYouWantToDelete(BOOL,BOOL,BOOL) {}
};
static void ask_if_you_want_to_overwrite()
{ FEMESSBOX.displayed=true; FEMESSBOX.question=FEMB_Q_OVERWRITE; }
#define _WIN32 1
#include UI_SOURCE
#undef _WIN32
int main(int argc,char **argv)
{
	if(argc!=3)return 2;
	root=argv[2]; std::string test=argv[1];
	bool ascii=test=="ascii-yes";
	std::string file=ascii?"alpha.bes":"\xc3\xa9.bes";
	PutFile(file,"ORIGINAL");
	CFEPSaveGame page;
	std::vector<WCHAR> name=ascii?std::vector<WCHAR>{'A','L','P','H','A',0}:std::vector<WCHAR>{0xc9,0};
	WideCopy(page.mSaveGameName,name.data());
	page.CreateSave();
	Check(FEMESSBOX.question==FEMB_Q_OVERWRITE && page.mExistingSave==0 && !uiAdmissions && !uiWrites && !uiDeletes && Contents(file)=="ORIGINAL",
		"native case alias precheck missed ordinary overwrite question/preserved bytes");
	if(test=="admission-failure") lookupFailure=50;
	else if(test=="mismatch") { page.mSaveGameName[0]=0xc3;page.mSaveGameName[1]=0xa9;page.mSaveGameName[2]=0; }
	else if(test=="early-failure") cardFormatted=false;
	else if(test=="cancel") Check(!PortSaveApproveOverwrite(page.mSaveGameName,page.mExistingSaveName),"cancel approval setup");
	else if(test=="back") Check(!PortSaveApproveOverwrite(page.mSaveGameName,page.mExistingSaveName),"back approval setup");
	else if(test=="missing-index") page.mExistingSave=-1;
	else if(test!="unicode-yes" && test!="ascii-yes") return 2;
	FEMESSBOX.choice=test=="cancel"?FEMB_NO:FEMB_YES;
	if(test=="back") page.ButtonPressed(BUTTON_FRONTEND_MENU_BACK,1.f);
	else page.Process(FEPS_ACTIVE);
	Check(!uiDeletes,"overwrite YES deleted the existing career before admission/write");
	if(test=="unicode-yes" || test=="ascii-yes")
		Check(uiAdmissions==1 && uiWrites==1 && Contents(file)=="UPDATED","approved immediate retry did not complete ordinary write");
	else
	{
		Check(!uiWrites && Contents(file)=="ORIGINAL","failed/cancelled/mismatched retry changed existing bytes");
		Check(!PortSaveConsumeOverwrite(name.data()),"failed/cancelled retry retained authority");
		lookupFailure=0; cardFormatted=true; FEMESSBOX.Kill(); WideCopy(page.mSaveGameName,name.data());
		page.CreateSave();
		Check(FEMESSBOX.question==FEMB_Q_OVERWRITE && !uiWrites && Contents(file)=="ORIGINAL","later retry reused stale overwrite authority");
	}
	return failures?1:0;
}
