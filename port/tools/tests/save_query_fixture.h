// SPDX-License-Identifier: GPL-3.0-only
// Complete patched CreateSave coupled to actual namespace functions. Only the native WinAPI boundary is modeled;
// authored payload bytes and exclusive reservation files are real host files beneath the fresh test directory.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"
static std::string authoredRoot, racePhase;
static unsigned writeAdmissions;
static std::vector<std::pair<std::string,std::string>> authoredBaselines;
static std::string HostPath(const std::string &logical)
{
	std::string path=logical;for(char &c:path)if(c=='\\')c='/';return authoredRoot+"/"+path;
}
static std::string ReadAuthored(const std::string &logical)
{
	FILE *file=fopen(HostPath(logical).c_str(),"rb");std::string bytes;char buffer[256];
	if(file) {size_t n;while((n=fread(buffer,1,sizeof(buffer),file)))bytes.append(buffer,n);fclose(file);}return bytes;
}
static void AddAuthored(const std::string &role,const std::string &name,const std::string &bytes)
{
	int fd=open(HostPath(role+"/savegames/"+name).c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
	Check(fd>=0,"authored fixture would replace an existing file");
	if(fd>=0) {Check(write(fd,bytes.data(),bytes.size())==(ssize_t)bytes.size(),"authored payload write");Check(close(fd)==0,"authored payload close");}
	authoredBaselines.emplace_back(role+"/savegames/"+name,bytes);
	std::vector<WCHAR> wide(name.size()+1);int n=Convert("UTF-8","UTF-16LE",name.c_str(),name.size()+1,wide.data(),wide.size()*2,2);
	Check(n>0,"authored filename conversion");wide.resize(n);(role=="user"?nativeNames:gameNativeNames).push_back(wide);
}
static void IntroduceAlias() {AddAuthored("user","A.BES",std::string("RACED\0\xff",7));}
static HANDLE HostReserve(const WCHAR *wide)
{
	char native[520];int n=Convert("UTF-16LE","UTF-8",wide,(WideLen(wide)+1)*2,native,sizeof(native),1);
	Check(n>0,"native reservation conversion");
	if(racePhase=="short-alias") {Check(!strcmp(native,"user\\savegames\\ALPHAL~1.bes"),"short-name ambiguity requested a different native alias");error=80;return INVALID_HANDLE_VALUE;}
	if(racePhase=="before-create")IntroduceAlias();
	int fd=open(HostPath(native).c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
	if(fd<0) {error=errno==EEXIST?80:5;return INVALID_HANDLE_VALUE;}
	Check(close(fd)==0,"host reservation close");reservations++;
	const char *name=strrchr(native,'\\');Check(name!=nullptr,"native reservation parent");
	std::vector<WCHAR> added(strlen(name+1)+1);int units=Convert("UTF-8","UTF-16LE",name+1,strlen(name+1)+1,added.data(),added.size()*2,2);
	Check(units>0,"reserved entry conversion");added.resize(units);nativeNames.push_back(added);
	if(racePhase=="after-reserve")IntroduceAlias();
	return (HANDLE)3;
}
static int CreateDirectoryBoundary(const char *)
{
	if(racePhase=="between-pre-reserve")IntroduceAlias();return 0;
}
#define _mkdir CreateDirectoryBoundary
#define SAVE_DIRECTORY "savegames\\"
#define _WIN32 1
#include SAVE_CARD_CREATE_SOURCE
#undef _WIN32
#undef _mkdir

static void CoupledCreate(const std::string &test,const char *folder)
{
	authoredRoot=folder;splitRoots=strictWildcard=true;caseQueryError=120;reserveFixture=HostReserve;
	for(const char *path:{"user","game","user/savegames","game/savegames"})Check(mkdir(HostPath(path).c_str(),0700)==0,"private fixture directory create");
	const std::string original("ORIGINAL\0\xff",10);AddAuthored("user","unrelated.bin",original);
	unsigned expected=MCE_FAILURE,expectedError=0;bool newEmptyAllowed=false;
	if(test=="unknown-valid")expected=MCE_SUCCESS;
	else if(test=="unknown-uppercase") {AddAuthored("user","A.BES",original);expected=MCE_FILEEXISTS;}
	else if(test=="unknown-same-root") {AddAuthored("user","a.bes",original);AddAuthored("user","A.BES",original);expectedError=52;}
	else if(test=="unknown-cross-root") {AddAuthored("user","a.bes",original);AddAuthored("game","A.BES",original);nativeFault="identity-disagreement";expectedError=50;}
	else if(test=="unknown-short-alias") {AddAuthored("user","Alphalong"+std::string(40,'x')+".bes",original);racePhase="short-alias";expectedError=80;}
	else if(test=="unknown-partial-enum") {AddAuthored("user","x.bes",original);fault="native-next";expectedError=5;}
	else if(test=="unknown-enum-open-zero" || test=="unknown-enum-next-zero" || test=="unknown-enum-close-zero" || test=="unknown-enum-close-denied")
	{
		AddAuthored("user","x.bes",original);fault=test=="unknown-enum-open-zero"?"native-open-zero":test=="unknown-enum-next-zero"?"native-next-zero":test=="unknown-enum-close-zero"?"native-close-zero":"native-close-denied";expectedError=test=="unknown-enum-close-denied"?5:31;
	}
	else if(test=="unknown-hidden-alias" || test=="unknown-system-alias" || test=="unknown-directory-alias")
	{AddAuthored("user","A.BES",original);fixtureAttributes=test=="unknown-hidden-alias"?2:test=="unknown-system-alias"?4:16;expectedError=80;}
	else if(test=="unknown-between-prelookup-reserve") {racePhase="between-pre-reserve";expected=MCE_FILEEXISTS;}
	else if(test=="unknown-between-absence-create" || test=="unknown-after-reserve")
	{racePhase=test=="unknown-after-reserve"?"after-reserve":"before-create";expectedError=52;newEmptyAllowed=true;}
	else if(test=="unknown-query-denied" || test=="unknown-known-sensitive")
	{caseQueryError=0;nativeFault=test=="unknown-query-denied"?"query-denied":"case-sensitive";expectedError=test=="unknown-query-denied"?5:50;}
	else {Check(false,"unknown coupled case");return;}
	CPCMemoryCard card;WCHAR name[]={'a',0},shortName[]={'A','L','P','H','A','L','~','1',0};int slot=-1;
	int result=card.CreateSave(0,test=="unknown-short-alias"?shortName:name,&slot,FALSE);
	printf("%s result=%d error=%u slot=%d creates=%u reservations=%u\n",test.c_str(),result,GetLastError(),slot,creates,reservations);
	Check(result==int(expected),"actual CreateSave admission result");if(expectedError)Check(GetLastError()==expectedError,"actual CreateSave error lost");
	if(result==MCE_SUCCESS)writeAdmissions++;
	Check(writeAdmissions==(test=="unknown-valid"?1u:0u),"refused namespace reached write admission");
	Check(ReadAuthored("user/unrelated.bin")=="","fixture path sanity");
	Check(ReadAuthored("user/savegames/unrelated.bin")==original,"unselected authored payload changed");
	if(test=="unknown-uppercase" || test=="unknown-same-root" || test=="unknown-hidden-alias" || test=="unknown-system-alias" || test=="unknown-directory-alias")
		Check(ReadAuthored("user/savegames/A.BES")==original,"existing alias bytes changed");
	if(test=="unknown-same-root" || test=="unknown-cross-root")Check(ReadAuthored("user/savegames/a.bes")==original,"existing literal bytes changed");
	if(test=="unknown-cross-root")Check(ReadAuthored("game/savegames/A.BES")==original,"cross-root bytes changed");
	if(test=="unknown-short-alias")Check(ReadAuthored("user/savegames/Alphalong"+std::string(40,'x')+".bes")==original,"short-name native alias bytes changed");
	if(test=="unknown-between-prelookup-reserve" || newEmptyAllowed)Check(ReadAuthored("user/savegames/A.BES")==std::string("RACED\0\xff",7),"racing alias bytes changed");
	if(newEmptyAllowed)Check(reservations==1 && ReadAuthored("user/savegames/a.bes").empty(),"owned reservation must be empty after post-lookup refusal");
	else if(test!="unknown-valid")Check(reservations==0,"refused namespace reserved a file");
	for(const auto &baseline:authoredBaselines)Check(ReadAuthored(baseline.first)==baseline.second,"an existing/racing authored file changed bytes");
	for(const auto &pattern:nativePatterns)Check(pattern.size()==1 && pattern[0]=='*',"save enumeration relied on *.bes native wildcard");
}
