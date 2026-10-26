/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual Windows helper bodies on a bounded host ABI model: no native Windows/kernel behavior is claimed. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define _WIN32 1
#define CE_LOG_SLOT_COUNT 3
#define LOG_CAP (4u<<20)
#define PATHN 4096
#define SEP "\\"
typedef uintptr_t HANDLE;
typedef uint32_t DWORD;
typedef struct { DWORD dwFileAttributes,nFileSizeHigh,nFileSizeLow,nNumberOfLinks; } BY_HANDLE_FILE_INFORMATION;
typedef struct { DWORD zero; } OVERLAPPED;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define ERROR_GEN_FAILURE 31
#define ERROR_INVALID_NAME 123
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_LOCK_VIOLATION 33
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define OPEN_ALWAYS 4
#define CREATE_NEW 1
#define OPEN_EXISTING 3
#define FILE_FLAG_OPEN_REPARSE_POINT 0x200000
#define FILE_ATTRIBUTE_DIRECTORY 16
#define FILE_ATTRIBUTE_REPARSE_POINT 1024
#define FILE_ATTRIBUTE_NORMAL 128
#define FILE_TYPE_DISK 1
#define HANDLE_FLAG_INHERIT 1
#define LOCKFILE_EXCLUSIVE_LOCK 2
#define LOCKFILE_FAIL_IMMEDIATELY 1
#define _O_BINARY 0
#define _O_RDONLY 0
#define _O_RDWR 2
#define _O_APPEND 8
static char sUser[PATHN]="authored";
static const char *sLogNames[]={"onslaught-ce.log","onslaught-ce.concurrent-1.log","onslaught-ce.concurrent-2.log"};
static int sLogSlot=-1;
static HANDLE sLogLease=INVALID_HANDLE_VALUE;
static DWORD last,query_error,links=1;
static int query_fails,closed,locks,osfd,failures;
static DWORD GetLastError(void) {return last;}
static HANDLE CreateFileA(const char *p,DWORD a,DWORD s,void *sa,DWORD d,DWORD f,void *t)
{(void)p;(void)a;(void)s;(void)sa;(void)d;(void)f;(void)t;return 1;}
static int GetFileInformationByHandle(HANDLE h,BY_HANDLE_FILE_INFORMATION *info)
{(void)h;if(query_fails){memset(info,0xcc,sizeof *info);last=query_error;return 0;}memset(info,0,sizeof *info);info->nNumberOfLinks=links;return 1;}
static int GetFileType(HANDLE h) {(void)h;return FILE_TYPE_DISK;}
static int CloseHandle(HANDLE h) {(void)h;closed++;last=0;return 1;}
static int SetHandleInformation(HANDLE h,DWORD m,DWORD f) {(void)h;(void)m;(void)f;return 1;}
static int LockFileEx(HANDLE h,DWORD f,DWORD z,DWORD lo,DWORD hi,OVERLAPPED *o)
{(void)h;(void)z;(void)hi;(void)o;locks++;if(f!=3 || lo!=1)failures++;return 1;}
static int UnlockFileEx(HANDLE h,DWORD z,DWORD lo,DWORD hi,OVERLAPPED *o)
{(void)h;(void)z;(void)lo;(void)hi;(void)o;return 1;}
static int _open_osfhandle(intptr_t h,int f) {(void)h;(void)f;osfd++;return 4;}
static FILE *_fdopen(int f,const char *m) {(void)f;(void)m;last=777;return NULL;}
static int _close(int fd) {(void)fd;return 0;}
enum LogOpen { LOG_INSPECT,LOG_APPEND,LOG_CREATE };
#include LOG_WINDOWS_FUNCTIONS
int main(void)
{
    for(int kind=0;kind<2;kind++)for(int test=0;test<4;test++)
    {
        query_fails=test<2;query_error=test==0?5:0;links=test==2?0:2;closed=locks=osfd=0;last=0;
        unsigned long error=0;int result=kind?log_open_file("authored",LOG_APPEND,&error)!=NULL:log_acquire_lease(0,&error);
        unsigned long expected=test==0?5:test==1?31:123;
        int ok=(kind?result==0:result==-1) && error==expected && closed==1 && !locks && !osfd && sLogLease==INVALID_HANDLE_VALUE;
        printf("kind=%s test=%d query_failed=%d links=%u error=%lu expected=%lu closed=%d pass=%d\n",kind?"data":"lease",test,query_fails,links,error,expected,closed,ok);
        if(!ok)failures++;
    }
    query_fails=0;links=1;closed=locks=0;unsigned long error=0;
    int owned=log_acquire_lease(0,&error);log_release_lease();
    if(owned!=1 || locks!=1 || closed!=1 || sLogSlot!=-1 || sLogLease!=INVALID_HANDLE_VALUE)failures++;
    return failures?1:0;
}
