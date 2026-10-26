/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual launcher log callers on authored paths. Narrow filesystem/stdio failures are injected; no game or SDL initialization. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail_rotation, fail_flush, fail_close, header_failed;
static const char *fixture_mode;
static FILE *fault_file;
static int fixture_open(const char *,int,...);
static int fixture_link(const char *,const char *);
static int fixture_flock(int,int);
static int fixture_fstat(int,struct stat *);
static int fixture_fflush(FILE *);
static int fixture_fclose(FILE *);
static FILE *fixture_fopen(const char *, const char *);
static int fixture_rename(const char *, const char *);
static int fixture_remove(const char *);
static int unused_launcher_main(int, char **);
#define open fixture_open
#define link fixture_link
#define flock fixture_flock
#define fstat fixture_fstat
#define fflush fixture_fflush
#define fclose fixture_fclose
#define fopen fixture_fopen
#define rename fixture_rename
#define remove fixture_remove
#define main unused_launcher_main
#include CE_LAUNCH_SOURCE
#undef main
#undef open
#undef link
#undef flock
#undef fstat
#undef fflush
#undef fclose
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
static int fixture_open(const char *path,int flags,...)
{
    mode_t mode=0; if(flags&O_CREAT) {va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}
    if(!strcmp(fixture_mode,"race-create") && (flags&O_EXCL) && !strcmp(log_kind(path),"current"))
    {
        int foreign=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(foreign>=0) {write(foreign,"raced foreign sentinel",22);close(foreign);}
    }
    return open(path,flags,mode);
}
static int fixture_fstat(int fd,struct stat *info)
{
    if(!strcmp(fixture_mode,"stat-error") || !strcmp(fixture_mode,"stat-zero")) {errno=!strcmp(fixture_mode,"stat-zero")?0:EACCES;return -1;}
    return fstat(fd,info);
}
static int fixture_flock(int fd,int operation)
{
    if(!strcmp(fixture_mode,"unsupported-lease") && (operation&LOCK_EX)) {errno=ENOTSUP;return -1;}
    return flock(fd,operation);
}
static int fixture_link(const char *from,const char *to)
{
    if(fail_rotation && !strcmp(log_kind(to),"old")) {errno=EACCES;return -1;}
    return link(from,to);
}
static int fixture_fflush(FILE *file)
{
    int result=fflush(file);if(!header_failed && !strcmp(fixture_mode,"header-flush-failure") && file==sLog){header_failed=1;errno=ENOSPC;return EOF;}if(fail_flush && file==fault_file) {fail_flush=0;errno=ENOSPC;return EOF;}return result;
}
static int fixture_fclose(FILE *file)
{
    int result=fclose(file);if(fail_close && file==fault_file) {fail_close=0;errno=EIO;return EOF;}return result;
}
static void state(const char *ack)
{
    struct stat info={0},lease={0};int fd=sLog?fileno(sLog):-1;
    if(fd>=0 && fstat(fd,&info))exit(91);
    if(sLogLease>=0 && fstat(sLogLease,&lease))exit(92);
    printf("{\"ack\":\"%s\",\"pid\":%lu,\"persisted\":%s,\"slot\":%d,\"inode\":%llu,\"links\":%llu,\"size\":%llu,\"file_error\":%d,\"lease_inode\":%llu,\"lease_cloexec\":%s,\"holding\":%d,\"hold_bytes\":%u,\"hold_present\":%s}\n",
        ack,(unsigned long)getpid(),sLog?"true":"false",sLogSlot,(unsigned long long)info.st_ino,(unsigned long long)info.st_nlink,
        (unsigned long long)info.st_size,sLog?ferror(sLog):0,(unsigned long long)lease.st_ino,
        sLogLease<0 || (fcntl(sLogLease,F_GETFD)&FD_CLOEXEC)?"true":"false",sHolding,(unsigned)sHoldLen,sHold?"true":"false");
}
int main(int argc,char **argv)
{
    if(argc!=4 || strlen(argv[1])>=sizeof sUser || strlen(argv[2])>16)return 2;
    setvbuf(stdout,NULL,_IOLBF,0);
    snprintf(sUser,sizeof sUser,"%s",argv[1]);fixture_mode=argv[3];fail_rotation=!strcmp(argv[3],"force-rename");
    log_open(!strcmp(fixture_mode,"continue"));
    if(strcmp(fixture_mode,"header-flush-failure"))log_release("Authored log fixture; no rendering device");state("OPEN");
    char command[128];unsigned operations=0;
    while(operations++<32 && fgets(command,sizeof command,stdin))
    {
        command[strcspn(command,"\r\n")]=0;
        if(!strcmp(command,"CLOSE")) {if(!strcmp(fixture_mode,"close-failure")){fault_file=sLog;fail_close=1;}log_close();puts("{\"ack\":\"CLOSE\"}");return 0;}
        if(!strcmp(command,"CAP"))
        {
            char block[65536];memset(block,'X',sizeof block);
            for(unsigned i=0;i<80;++i)log_file(block,sizeof block);
        }
        else if(!strcmp(command,"LEASE_PROBE")) {unsigned long error=0;if(log_acquire_lease(0,&error)!=1)return 7;}
        else if(!strcmp(command,"METADATA")) {char path[4096];snprintf(path,sizeof path,"%s/../%s.metadata.json",sUser,argv[2]);FILE *f=fopen(path,"wb");if(!f)return 6;int first=1;size_t total=0;fputc('{',f);json_start_files(f,sUser,"",&first,&total);fputs("}\n",f);fclose(f);}
        else if(!strcmp(command,"REPORT")) {char url[32768];ce_report_url("fixture","log-lease",sUser,url,sizeof url);fprintf(stderr,"report_url=%s\n",url);}
        else if(!strcmp(command,"FAIL_FLUSH")) {fault_file=sLog;fail_flush=1;game_output("fixture_flush_failure\n",22);}
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
