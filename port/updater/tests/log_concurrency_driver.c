/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual launcher log callers on authored paths. Only rename failure is injected; no game or SDL initialization. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail_rotation;
static FILE *fixture_fopen(const char *, const char *);
static int fixture_rename(const char *, const char *);
static int fixture_remove(const char *);
static int unused_launcher_main(int, char **);
#define fopen fixture_fopen
#define rename fixture_rename
#define remove fixture_remove
#define main unused_launcher_main
#include CE_LAUNCH_SOURCE
#undef main
#undef fopen
#undef rename
#undef remove

static const char *log_kind(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return !strcmp(base,"onslaught-ce.log") ? "current" : !strcmp(base,"onslaught-ce.old.log") ? "old" : "other";
}
static FILE *fixture_fopen(const char *path, const char *mode)
{
    struct stat before = {0}, after = {0};
    int existed = !stat(path,&before);
    FILE *file = fopen(path,mode);
    if (file) fstat(fileno(file),&after);
    if (!strcmp(log_kind(path),"current"))
        printf("{\"event\":\"open\",\"mode\":\"%s\",\"success\":%s,\"existed\":%s,\"before_size\":%llu,\"after_size\":%llu,\"inode\":%llu}\n",
            mode,file?"true":"false",existed?"true":"false",(unsigned long long)before.st_size,
            (unsigned long long)after.st_size,(unsigned long long)after.st_ino);
    return file;
}
static int fixture_rename(const char *from,const char *to)
{
    int result;
    if (fail_rotation && !strcmp(log_kind(from),"current") && !strcmp(log_kind(to),"old"))
    {errno=EACCES;result=-1;}
    else result=rename(from,to);
    int error=result?errno:0;
    printf("{\"event\":\"rename\",\"result\":%d,\"errno\":%d,\"forced\":%s}\n",result,error,fail_rotation?"true":"false");
    errno=error;return result;
}
static int fixture_remove(const char *path)
{
    struct stat before={0};int existed=!stat(path,&before),result=remove(path),error=result?errno:0;
    printf("{\"event\":\"remove\",\"kind\":\"%s\",\"result\":%d,\"errno\":%d,\"existed\":%s,\"inode\":%llu}\n",
        log_kind(path),result,error,existed?"true":"false",(unsigned long long)before.st_ino);
    errno=error;return result;
}
static void state(const char *ack)
{
    struct stat info={0};int fd=sLog?fileno(sLog):-1;
    if(fd<0 || fstat(fd,&info))exit(91);
    printf("{\"ack\":\"%s\",\"pid\":%lu,\"inode\":%llu,\"links\":%llu,\"size\":%llu,\"file_error\":%d}\n",
        ack,(unsigned long)getpid(),(unsigned long long)info.st_ino,(unsigned long long)info.st_nlink,
        (unsigned long long)info.st_size,ferror(sLog));
}
int main(int argc,char **argv)
{
    if(argc!=4 || strlen(argv[1])>=sizeof sUser || strlen(argv[2])>16)return 2;
    setvbuf(stdout,NULL,_IOLBF,0);
    snprintf(sUser,sizeof sUser,"%s",argv[1]);fail_rotation=!strcmp(argv[3],"force-rename");
    log_open(0);if(!sLog)return 90;
    log_release("Authored log fixture; no rendering device");state("OPEN");
    char command[128];unsigned operations=0;
    while(operations++<32 && fgets(command,sizeof command,stdin))
    {
        command[strcspn(command,"\r\n")]=0;
        if(!strcmp(command,"CLOSE")) {log_close();puts("{\"ack\":\"CLOSE\"}");return 0;}
        if(!strcmp(command,"CAP"))
        {
            char block[65536];memset(block,'X',sizeof block);
            for(unsigned i=0;i<80;++i)log_file(block,sizeof block);
        }
        else if(strcmp(command,"STATE"))
        {
            if(strspn(command,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=strlen(command))return 3;
            char marker[256];int n=snprintf(marker,sizeof marker,"fixture_marker writer=%s token=%s\n",argv[2],command);
            if(n<=0 || n>=(int)sizeof marker)return 4;
            game_output(marker,(size_t)n);fflush(stderr);
        }
        state(command);
    }
    log_close();return 5;
}
