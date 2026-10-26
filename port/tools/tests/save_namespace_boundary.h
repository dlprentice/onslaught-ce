// SPDX-License-Identifier: GPL-3.0-only
// Bounded native boundary for complete production namespace functions. No filesystem/NTFS is claimed here.
#include <strings.h>
#define TRUE 1
#define ERROR_INVALID_NAME 123
#define ERROR_DUP_NAME 52
#define ERROR_NOT_SUPPORTED 50
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_PARAMETER 87
#define ERROR_GEN_FAILURE 31
#define FILE_READ_ATTRIBUTES 128
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define OPEN_EXISTING 3
#define CREATE_NEW 1
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_CS_FLAG_CASE_SENSITIVE_DIR 1
#define FILE_ATTRIBUTE_HIDDEN 2
#define FILE_ATTRIBUTE_SYSTEM 4
#define FILE_ATTRIBUTE_DIRECTORY 16
#define FILE_ATTRIBUTE_NORMAL 128
#define GENERIC_WRITE 0x40000000
using BOOL = int;
using FILE_INFO_BY_HANDLE_CLASS = int;
struct FILE_CASE_SENSITIVE_INFO { DWORD Flags; };
struct BY_HANDLE_FILE_INFORMATION { DWORD dwVolumeSerialNumber, nFileIndexHigh, nFileIndexLow; };
struct FILE_ID_128 { unsigned char Identifier[16]; };
struct FILE_ID_INFO { uint64_t VolumeSerialNumber; FILE_ID_128 FileId; };
enum { FileIdInfo = 18, FileCaseSensitiveInfo = 23 };
enum Mode { READ, WRITE, SHADOW };
static unsigned creates, directoryOpens, closesNative, reservations;
static unsigned identityReads, identityOpens;
static unsigned identityHandles, identityCloses;
static char gUserDir[] = "user";
static bool nativeExists = true;
static std::string nativeFault;
void SetLastError(DWORD value) { error = value; }
static WCHAR NativeFold(WCHAR c) { return c >= 'A' && c <= 'Z' ? WCHAR(c + 32) : c == 0xc9 ? WCHAR(0xe9) : c; }
int CompareStringOrdinal(const WCHAR *a, int alen, const WCHAR *b, int blen, BOOL ignoreCase)
{
	Check(alen == -1 && blen == -1 && ignoreCase, "ordinal namespace flags changed");
	if (nativeFault == "compare") { error = 5; return 0; }
	while (*a && NativeFold(*a) == NativeFold(*b)) a++, b++;
	return NativeFold(*a) == NativeFold(*b) ? 2 : NativeFold(*a) < NativeFold(*b) ? 1 : 3;
}
static size_t TestWideLen(const WCHAR *s) { return WideLen(s); }
static WCHAR *TestWideLast(const WCHAR *s, WCHAR c)
{
	WCHAR *last = nullptr;
	for (; *s; s++) if (*s == c) last = const_cast<WCHAR *>(s);
	return last;
}
#define wcslen TestWideLen
#define wcsrchr TestWideLast
static int _stricmp(const char *a, const char *b) { return strcasecmp(a,b); }
static int Resolve(const char *path, Mode mode, char *out, size_t size)
{
	snprintf(out,size,"%s\\%s", mode == READ ? "game" : "user",path); return 0;
}
static bool Exists(const char *) { return nativeExists; }
static HANDLE CreateFileW(const WCHAR *, DWORD access, DWORD share, void *, DWORD disposition, DWORD flags, void *)
{
	if (disposition == OPEN_EXISTING)
	{
		if (flags == FILE_ATTRIBUTE_NORMAL)
		{
			identityOpens++;
			Check(access == FILE_READ_ATTRIBUTES && share == 7,"identity check was not read only/shared");
			if(nativeFault=="identity-open-zero-"+std::to_string(identityOpens)) { error=0;return INVALID_HANDLE_VALUE; }
			if (!nativeExists) { error=ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
			identityHandles++;return (HANDLE)(uintptr_t)(4+identityOpens);
		}
		directoryOpens++;
		Check(access == FILE_READ_ATTRIBUTES && share == 7 && flags == FILE_FLAG_BACKUP_SEMANTICS,"directory policy handle flags");
		if (nativeFault == "directory-missing") { error = ERROR_PATH_NOT_FOUND; return INVALID_HANDLE_VALUE; }
		if (nativeFault == "directory-denied") { error = 5; return INVALID_HANDLE_VALUE; }
		return (HANDLE)2;
	}
	creates++;
	Check(disposition == CREATE_NEW && access == GENERIC_WRITE && !share && flags == FILE_ATTRIBUTE_NORMAL,"reservation was not exclusive CREATE_NEW");
	if (nativeFault == "already-exists") { error = 80; return INVALID_HANDLE_VALUE; }
	reservations++; return (HANDLE)3;
}
static BOOL Identity(FILE_ID_INFO *info)
{
	identityReads++;
	if(nativeFault=="identity-query-zero-both") { error=0;return false; }
	if(nativeFault=="identity-query-zero-"+std::to_string(identityReads)) { error=0;return false; }
	if(nativeFault=="identity-query5-close0-"+std::to_string(identityReads)) { error=5;return false; }
	if(nativeFault=="identity-denied") { error=5; return false; }
	if(nativeFault=="identity-unsupported") { error=50; return false; }
	info->VolumeSerialNumber=0x1122334400000011ull;
	memset(info->FileId.Identifier,0x5a,16);
	if(nativeFault=="identity-disagreement") info->FileId.Identifier[0]=identityReads%2;
	if(nativeFault=="identity-upper64") info->FileId.Identifier[15]=identityReads%2;
	if(nativeFault=="identity-volume-upper") info->VolumeSerialNumber=(uint64_t(identityReads%2)<<32)|17;
	if(nativeFault.compare(0,14,"identity-byte-")==0)
	{
		unsigned byte=unsigned(strtoul(nativeFault.c_str()+14,nullptr,10));
		Check(byte<16,"identity byte fixture outside128bits");
		if(byte<16) info->FileId.Identifier[byte]=identityReads%2;
	}
	return true;
}
static BOOL GetFileInformationByHandle(HANDLE, BY_HANDLE_FILE_INFORMATION *info)
{
	// Frozen126 negative: its 64-bit API sees precisely the same lower half/volume projection of this model.
	FILE_ID_INFO full={};
	if(!Identity(&full))return false;
	info->dwVolumeSerialNumber=DWORD(full.VolumeSerialNumber);
	memcpy(&info->nFileIndexLow,full.FileId.Identifier,4);
	memcpy(&info->nFileIndexHigh,full.FileId.Identifier+4,4);
	return true;
}
static BOOL GetFileInformationByHandleEx(HANDLE, int infoClass, void *output, unsigned size)
{
	if(infoClass==FileIdInfo)
	{
		Check(size==sizeof(FILE_ID_INFO),"128-bit file identity query buffer changed");
		return Identity(static_cast<FILE_ID_INFO *>(output));
	}
	Check(infoClass == FileCaseSensitiveInfo && size == sizeof(FILE_CASE_SENSITIVE_INFO),"case policy query changed");
	if (nativeFault == "old-windows") { error = ERROR_INVALID_PARAMETER; return false; }
	if (nativeFault == "query-denied") { error = 5; return false; }
	static_cast<FILE_CASE_SENSITIVE_INFO *>(output)->Flags = nativeFault == "case-sensitive" ? 1 : 0;
	return true;
}
static BOOL CloseHandle(HANDLE handle)
{
	closesNative++;
	if(uintptr_t(handle)>=5)
	{
		identityCloses++;unsigned position=unsigned(uintptr_t(handle)-4);
		if(nativeFault=="identity-close-zero-"+std::to_string(position) || nativeFault=="identity-query5-close0-"+std::to_string(position))
		{ error=0;return false; }
	}
	return true;
}
