// SPDX-License-Identifier: GPL-3.0-only
// Whole production savecard caller + codec, authored files only. OS enumeration/path delivery is the fake boundary.
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "common.h"
#include "save_names.h"

static std::string root;
static std::vector<std::string> entries;
static size_t nextEntry;
static unsigned operations, lastError;
static int failures;
static unsigned lookupFailure;
static bool reserveRace;
static void Check(bool ok, const char *message) { if (!ok) { fprintf(stderr, "%s\n", message); failures++; } }
void SetLastError(DWORD value) { lastError = value; }
DWORD GetLastError() { return lastError; }
static size_t WideLen(const WCHAR *p) { size_t n = 0; while (p[n]) n++; return n; }
static WCHAR *WideCopy(WCHAR *d, const WCHAR *s) { WCHAR *r = d; while ((*d++ = *s++)) {} return r; }
static int WideCompare(const WCHAR *a, const WCHAR *b)
{
	while (*a && *a == *b) a++, b++;
	return int(*a) - int(*b);
}
static int WideCaseCompare(const WCHAR *a, const WCHAR *b)
{
	auto fold = [](WCHAR c) { return c >= 'A' && c <= 'Z' ? WCHAR(c + 32) : c; };
	while (*a && fold(*a) == fold(*b)) a++, b++;
	return int(fold(*a)) - int(fold(*b));
}
// Explicit bounded Windows boundary model: ASCII and U+00C9/U+00E9 only, not a native NTFS claim.
static WCHAR NativeFold(WCHAR c) { return c >= 'A' && c <= 'Z' ? WCHAR(c + 32) : c == 0xc9 ? WCHAR(0xe9) : c; }
int CompareStringOrdinal(const WCHAR *a, int alen, const WCHAR *b, int blen, BOOL ignoreCase)
{
	Check(alen == -1 && blen == -1 && ignoreCase, "ordinal identity flags changed");
	while (*a && NativeFold(*a) == NativeFold(*b)) a++, b++;
	return NativeFold(*a) == NativeFold(*b) ? 2 : NativeFold(*a) < NativeFold(*b) ? 1 : 3;
}
#define wcslen WideLen
#define wcscpy WideCopy
#define wcscmp WideCompare
#define _wcsicmp WideCaseCompare
#include CONVERTER_SOURCE

static std::string Host(const char *name)
{
	std::string relative = name;
	std::replace(relative.begin(), relative.end(), '\\', '/');
#ifdef TEST_WINDOWS_SAVES
	if (relative.compare(0,10,"savegames/") == 0)
	{
		WCHAR requested[32];
		if (!PortSaveNameUtf16(relative.c_str() + 10, requested, 32))
		{
			DIR *directory = opendir((root + "/savegames").c_str());
			if (directory)
			{
				while (dirent *entry = readdir(directory))
				{
					WCHAR listed[32];
					if (!PortSaveNameUtf16(entry->d_name, listed, 32) && PortSaveNamesEqual(requested, listed) == 1)
					{ relative = std::string("savegames/") + entry->d_name; break; }
				}
				closedir(directory);
			}
		}
	}
#endif
	return root + "/" + relative;
}
extern "C" FILE *port_fopen(const char *name, const char *mode) { operations++; return fopen(Host(name).c_str(), mode); }
extern "C" FILE *bea_crt_port_fopen(const char *, const char *);
extern "C" int bea_crt_fclose(FILE *);
static int MakeDir(const char *name) { operations++; return mkdir(Host(name).c_str(), 0700); }
static BOOL Delete(const char *name) { operations++; return unlink(Host(name).c_str()) == 0; }
static int Fill(struct _finddata_t *data)
{
	if (nextEntry == entries.size()) return -1;
	memset(data, 0, sizeof(*data));
	const std::string &name = entries[nextEntry++];
	if (name.size() >= sizeof(data->name)) return -1;
	memcpy(data->name, name.c_str(), name.size() + 1);
	return 0;
}
static long FindFirst(const char *, struct _finddata_t *data)
{
	operations++; entries.clear(); nextEntry = 0;
	DIR *dir = opendir((root + "/savegames").c_str());
	if (!dir) return -1;
	while (dirent *entry = readdir(dir))
	{
		std::string name = entry->d_name;
		if (name.size() > 4 && name.substr(name.size() - 4) == ".bes") entries.push_back(name);
	}
	closedir(dir); std::sort(entries.begin(), entries.end());
	return Fill(data) ? -1 : 1;
}
static int FindNext(long, struct _finddata_t *data) { operations++; return Fill(data); }
static int FindClose(long) { return 0; }
// Authored host delivery model; separate namespace controls compile the actual Windows lookup/reserve functions.
extern "C" unsigned PortSaveLookupUtf16(const void *name, int *index)
{
	*index = -1; char path[260];
	unsigned error = PortSavePathUtf8(name, path, sizeof(path));
	if (error || lookupFailure) return error ? error : lookupFailure;
	_finddata_t data; long handle = FindFirst("savegames\\*.bes", &data);
	if (handle == -1) return 0;
	int admitted = 0;
	do {
		WCHAR listed[32];
		if (PortSaveNameUtf16(data.name, listed, 32)) continue;
		if (PortSaveNamesEqual(name, listed) == 1) *index = admitted;
		admitted++;
	} while (!FindNext(handle, &data));
	return 0;
}
extern "C" unsigned PortSaveReserveUtf8(const char *path)
{
	operations++;
	if (reserveRace)
	{
		FILE *f = fopen(Host(path).c_str(), "wb");
		if (f) { fwrite("RACED",1,5,f); fclose(f); }
		reserveRace = false;
	}
	int file = open(Host(path).c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (file < 0) return errno == EEXIST ? 80 : 5;
	return close(file) ? 5 : 0;
}
WCHAR *Localization_GetStringById(int) { static WCHAR empty[] = {0}; return empty; }

#define fopen bea_crt_port_fopen
#define fclose bea_crt_fclose
#define _mkdir MakeDir
#define DeleteFile Delete
#define _findfirst FindFirst
#define _findnext FindNext
#define _findclose FindClose
#ifdef TEST_WINDOWS_SAVES
#define _WIN32 1
#endif
#include SAVE_CARD_SOURCE
#ifdef TEST_WINDOWS_SAVES
#undef _WIN32
#endif
#undef fopen
#undef fclose

static void PutFile(const std::string &name, const char *bytes = "untouched")
{
	mkdir((root + "/savegames").c_str(), 0700);
	FILE *f = fopen((root + "/savegames/" + name).c_str(), "wb");
	Check(f != nullptr, "fixture create");
	if (f) { fwrite(bytes, 1, strlen(bytes), f); fclose(f); }
}
static bool HasFile(const std::string &name) { return access((root + "/savegames/" + name).c_str(), F_OK) == 0; }
static std::string Contents(const std::string &name)
{
	FILE *f = fopen((root + "/savegames/" + name).c_str(), "rb"); char bytes[128];
	size_t count = f ? fread(bytes,1,sizeof(bytes),f) : 0; if (f) fclose(f);
	return std::string(bytes,count);
}

static void Roundtrip(const std::vector<WCHAR> &input, const std::string &filename)
{
	CPCMemoryCard card;
	std::vector<WCHAR> name = input;
	int slot = -1;
	Check(card.CreateSave(0, name.data(), &slot, FALSE) == MCE_SUCCESS, "CreateSave rejected valid name");
	Check(slot == 0 && HasFile(filename), "CreateSave selected a different native filename/index");
	int count = -1;
	Check(card.GetNumSaves(0, &count) == MCE_SUCCESS && count == 1, "valid save count");
	struct { WCHAR before, name[32], after; } output;
	memset(&output, 0x5a, sizeof(output));
	Check(card.GetSaveName(0, 0, output.name) == MCE_SUCCESS && WideCompare(name.data(), output.name) == 0, "GetSaveName did not decode the original UTF-16 name");
	Check(output.before == 0x5a5a && output.after == 0x5a5a, "GetSaveName crossed the 32-unit caller buffer");
	const unsigned char payload[] = {0, 0xff, 7, 0x80, 0, 0xa5};
	Check(card.WriteSave(0, slot, name.data(), (void *)payload, sizeof(payload)) == MCE_SUCCESS, "WriteSave");
	unsigned char read[sizeof(payload)] = {};
	int got = -1;
	Check(card.ReadSave(0, slot, name.data(), read, sizeof(read), &got) == MCE_SUCCESS && got == sizeof(read) && !memcmp(payload, read, sizeof(read)), "ReadSave changed authored payload bytes");
	Check(card.CreateSave(0, name.data(), &slot, FALSE) == MCE_FILEEXISTS, "existing-name protection");
	Check(card.DeleteSave(0, slot, name.data()) == MCE_SUCCESS && !HasFile(filename), "DeleteSave selected a different native filename");
}

static void Codec()
{
	const WCHAR names[][4] = {{'A', 0}, {0xe9, 0}, {0xc3, 0xa9, 0}, {0x141, 0}, {0xd83d, 0xde00, 0}};
	const char *files[] = {"A.bes", "\xc3\xa9.bes", "\xc3\x83\xc2\xa9.bes", "\xc5\x81.bes", "\xf0\x9f\x98\x80.bes"};
	for (unsigned i = 0; i < 5; i++)
	{
		char path[260]; WCHAR decoded[32];
		Check(PortSavePathUtf8(names[i], path, sizeof(path)) == 0 && std::string(path) == std::string("savegames\\") + files[i], "strict UTF-16 path conversion");
		Check(PortSaveNameUtf16(files[i], decoded, 32) == 0 && !WideCompare(names[i], decoded), "strict UTF-8 listing conversion");
	}
	for (const char *bad : {"\xc0\xaf.bes", "\xed\xa0\x80.bes", "\xf4\x90\x80\x80.bes", "\xe9.bes", "\xe2\x82.bes"})
	{
		WCHAR output[32] = {'x'};
		Check(PortSaveNameUtf16(bad, output, 32) == 1113 && output[0] == 0, "invalid UTF-8 accepted/replaced");
	}
	WCHAR name31[32], name32[33];
	std::fill_n(name31, 31, WCHAR(0xe9)); name31[31] = 0;
	std::fill_n(name32, 32, WCHAR('x')); name32[32] = 0;
	char path[260], shortPath[15];
	Check(PortSavePathUtf8(name31, path, sizeof(path)) == 0, "31-unit name rejected");
	Check(PortSavePathUtf8(name32, path, sizeof(path)) == 206 && path[0] == 0, "32-unit name truncated");
	Check(PortSavePathUtf8(names[1], shortPath, sizeof(shortPath)) == 206 && shortPath[0] == 0, "short UTF-8 destination truncated");
	WCHAR shortName[1] = {'x'};
	Check(PortSaveNameUtf16("A.bes", shortName, 1) == 206 && shortName[0] == 0, "short UTF-16 destination truncated");
}

int main(int argc, char **argv)
{
	if (argc != 3) return 2;
	root = argv[2]; std::string test = argv[1];
	if (test == "codec") Codec();
	else if (test == "ascii") Roundtrip({'B','E','A',0}, "BEA.bes");
	else if (test == "latin1") Roundtrip({0xe9,0}, "\xc3\xa9.bes");
	else if (test == "literal-utf8-bytes") Roundtrip({0xc3,0xa9,0}, "\xc3\x83\xc2\xa9.bes");
	else if (test == "bmp") Roundtrip({0x141,0}, "\xc5\x81.bes");
	else if (test == "nonbmp") Roundtrip({0xd83d,0xde00,0}, "\xf0\x9f\x98\x80.bes");
	else if (test == "length31")
	{
		std::vector<WCHAR> name(31, 0xe9); name.push_back(0);
		std::string file; for (int i = 0; i < 31; i++) file += "\xc3\xa9";
		Roundtrip(name, file + ".bes");
	}
	else if (test == "invalid")
	{
		CPCMemoryCard card;
		const std::vector<std::vector<WCHAR>> names = {{0xd861,0},{0xdc41,0},{'.','.', '/', 'x',0},{'a','\\','b',0},{'C','O','N',0}, std::vector<WCHAR>(33, 'x')};
		unsigned index = 0;
		for (auto name : names)
		{
			name.back() = 0; int slot = -1, got = -1; char byte = 0;
			unsigned before = operations;
			Check(card.CreateSave(0,name.data(),&slot,TRUE) == MCE_FAILURE, "invalid CreateSave accepted");
			Check(card.WriteSave(0,0,name.data(),&byte,1) == MCE_FAILURE, "invalid WriteSave accepted");
			Check(card.ReadSave(0,0,name.data(),&byte,1,&got) == MCE_FAILURE, "invalid ReadSave accepted");
			Check(card.DeleteSave(0,0,name.data()) == MCE_FAILURE, "invalid DeleteSave accepted");
			Check(operations == before, "invalid save name reached a file/directory operation");
			Check(lastError == (index < 2 ? 1113u : index == names.size() - 1 ? 206u : 123u), "savecard lost the conversion/refusal error");
			index++;
		}
	}
	else if (test == "listing")
	{
		PutFile("A.bes"); PutFile("\xc3\xa9.bes"); PutFile("\xc3\x83\xc2\xa9.bes");
		PutFile(std::string(32, 'x') + ".bes"); PutFile("\xed\xa0\x80.bes"); PutFile("\xe9.bes");
		CPCMemoryCard card; int count = 0; card.GetNumSaves(0,&count); Check(count == 3, "unsupported names counted/aliased");
		WCHAR output[32]; for (int i = 0; i < count; i++) Check(card.GetSaveName(0,i,output) == MCE_SUCCESS, "filtered index/name mismatch");
		Check(HasFile(std::string(32,'x')+".bes") && HasFile("\xed\xa0\x80.bes") && HasFile("\xe9.bes"), "unsupported original entry changed");
	}
	else if (test == "linux-legacy") Roundtrip({0xe9,0}, std::string(1,'\xe9')+".bes");
	else if (test == "case-admission" || test == "ascii-case-admission")
	{
		bool ascii = test == "ascii-case-admission";
		std::string file = ascii ? "alpha.bes" : "\xc3\xa9.bes";
		std::vector<WCHAR> name = ascii ? std::vector<WCHAR>{'A','L','P','H','A',0} : std::vector<WCHAR>{0xc9,0};
		PutFile(file,"ORIGINAL"); CPCMemoryCard card; int slot = -1;
		Check(card.CreateSave(0,name.data(),&slot,FALSE) == MCE_FILEEXISTS && slot == 0, "case alias lost admitted existing slot");
		Check(Contents(file) == "ORIGINAL", "unapproved CreateSave changed existing bytes");
		Check(card.CreateSave(0,name.data(),&slot,TRUE) == MCE_SUCCESS && slot == 0, "approved case alias admission failed");
		Check(Contents(file) == "ORIGINAL", "approved CreateSave truncated existing bytes");
		lookupFailure = 50; slot = 123;
		Check(card.CreateSave(0,name.data(),&slot,TRUE) == MCE_FAILURE && slot == -1 && Contents(file) == "ORIGINAL", "admission failure destroyed bytes/returned slot");
		lookupFailure = 0;
		Check(card.WriteSave(0,0,name.data(),(void*)"UPDATED",7) == MCE_SUCCESS && Contents(file) == "UPDATED", "case alias WriteSave control");
	}
	else if (test == "case-loss-regression")
	{
		PutFile("\xc3\xa9.bes","ORIGINAL"); WCHAR name[]={0xc9,0}; CPCMemoryCard card; int slot=-1;
		int result=card.CreateSave(0,name,&slot,TRUE);
		std::string bytes=Contents("\xc3\xa9.bes");
		printf("CreateSave(U+00C9,TRUE): result=%d slot=%d existing_bytes=%zu\n",result,slot,bytes.size());
		Check(result==MCE_SUCCESS && slot==0 && bytes=="ORIGINAL","case alias admission destroyed the existing file or lost its slot");
	}
	else if(test=="native-refusal-preservation")
	{
		PutFile("\xc3\xa9.bes","ORIGINAL"); WCHAR name[]={0xc9,0};CPCMemoryCard card;int slot=17,got=-1;char byte=0;
		lookupFailure=31;unsigned before=operations;
		Check(card.CreateSave(0,name,&slot,TRUE)==MCE_FAILURE && slot==-1,"failed native identity admitted a writable slot");
		Check(card.WriteSave(0,0,name,&byte,1)==MCE_FAILURE && card.DeleteSave(0,0,name)==MCE_FAILURE && card.ReadSave(0,0,name,&byte,1,&got)==MCE_FAILURE,"native refusal bypassed at a save boundary");
		Check(lastError==31 && operations==before && Contents("\xc3\xa9.bes")=="ORIGINAL","native refusal changed bytes or reached file operations");
	}
	else if (test == "exclusive-race")
	{
		WCHAR name[] = {'R',0}; CPCMemoryCard card; int slot=-1; reserveRace=true;
		Check(card.CreateSave(0,name,&slot,TRUE) == MCE_FILEEXISTS && slot == 0 && Contents("R.bes") == "RACED", "CREATE_NEW race overwrote/implicitly approved new file");
	}
	else if (test == "distinct-names")
	{
		PutFile("\xc3\xa9.bes","EACUTE"); PutFile("\xc3\x83\xc2\xa9.bes","LITERAL");
		WCHAR a[]={0xe9,0}, b[]={0xc3,0xa9,0}; CPCMemoryCard card; int sa=-1,sb=-1;
		Check(card.CreateSave(0,a,&sa,TRUE)==0 && card.CreateSave(0,b,&sb,TRUE)==0 && sa!=sb, "distinct Unicode names collapsed");
		Check(Contents("\xc3\xa9.bes")=="EACUTE" && Contents("\xc3\x83\xc2\xa9.bes")=="LITERAL", "distinct-name admission altered bytes");
	}
	else if (test == "approval")
	{
		WCHAR a[]={0xe9,0}, upper[]={0xc9,0}, other[]={0xc3,0xa9,0}, invalid[]={0xd800,0};
		Check(!PortSaveApproveOverwrite(upper,a) && PortSaveConsumeOverwrite(upper)==1 && !PortSaveConsumeOverwrite(upper), "approval was not one-shot/case-keyed");
		Check(!PortSaveApproveOverwrite(a,a) && !PortSaveConsumeOverwrite(other) && !PortSaveConsumeOverwrite(a), "name mismatch retained approval");
		Check(!PortSaveApproveOverwrite(a,a) && PortSaveApproveOverwrite(invalid,a)==1113 && !PortSaveConsumeOverwrite(a), "failed approval retained authority");
		Check(!PortSaveApproveOverwrite(a,a), "approval setup"); PortSaveClearOverwrite();
		Check(!PortSaveConsumeOverwrite(a), "cancel retained approval");
	}
	else return 2;
	return failures ? 1 : 0;
}
