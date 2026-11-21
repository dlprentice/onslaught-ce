// SPDX-License-Identifier: GPL-3.0-only
#include "../common/ce_career_backup.h"
#include "../common/ce_backup_name.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

// Use the same SHA-256 implementation as the updater, with local linkage.
namespace digest {
#include "../../updater/sha256.c"
}

namespace {
struct Fd {
    int fd;
    explicit Fd(int f = -1): fd(f) {}
    ~Fd() { if (fd >= 0) close(fd); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    void reset(int f) { if (fd >= 0) close(fd); fd = f; }
};
const int dirs = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
const int input = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
const int create = O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC;
bool same(const struct stat &a, const struct stat &b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
bool sameTime(const timespec &a, const timespec &b) { return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec; }
bool unchanged(const struct stat &a, const struct stat &b) {
    return same(a,b) && a.st_mode == b.st_mode && a.st_size == b.st_size &&
           a.st_nlink == 1 && b.st_nlink == 1 && sameTime(a.st_ctim,b.st_ctim) && sameTime(a.st_mtim,b.st_mtim);
}
bool safeText(const char *s, size_t max) {
    if (!s || !*s || strnlen(s, max) >= max) return false;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    while (*p) {
        unsigned a = *p++;
        if (a < 32 || a == 127 || a == '\\') return false;
        if (a < 128) continue;
        unsigned more = a < 0xe0 ? 1 : a < 0xf0 ? 2 : 3;
        if (a < 0xc2 || a > 0xf4 || (a == 0xe0 && *p < 0xa0) ||
            (a == 0xed && *p >= 0xa0) || (a == 0xf0 && *p < 0x90) || (a == 0xf4 && *p >= 0x90)) return false;
        while (more--) { if ((*p & 0xc0) != 0x80) return false; ++p; }
    }
    return true;
}
bool absolute(const char *s) {
    if (!safeText(s,4096) || s[0] != '/' || !s[1]) return false;
    std::string path(s);
    return path.back() != '/' && path.find("//") == path.npos &&
           (path + "/").find("/../") == path.npos && (path + "/").find("/./") == path.npos;
}
bool inside(const std::string &path, const std::string &parent) {
    return path == parent || (path.size() > parent.size() && path.compare(0,parent.size(),parent) == 0 &&
                               path[parent.size()] == '/');
}
int directory(const std::string &path) {
    Fd current(open("/",dirs)); size_t pos = 1;
    while (current.fd >= 0 && pos < path.size()) {
        size_t end = path.find('/',pos);
        current.reset(openat(current.fd,path.substr(pos,end-pos).c_str(),dirs));
        if (end == path.npos) break;
        pos = end + 1;
    }
    int result = current.fd; current.fd = -1; return result;
}
bool namedDir(int fd, const std::string &path) {
    Fd fresh(directory(path)); struct stat a{}, b{};
    return fresh.fd >= 0 && fstat(fd,&a) == 0 && fstat(fresh.fd,&b) == 0 && same(a,b);
}
bool underIdentity(int child, int parent) {
    // Compare actual ancestry as well as path text. Fail closed on a traversal error.
    Fd at(dup(child)); struct stat protectedDir{}, here{}, next{};
    if (fstat(parent,&protectedDir)) return true;
    for (unsigned depth=0; depth<2048 && at.fd>=0; ++depth) {
        if (fstat(at.fd,&here) || same(here,protectedDir)) return true;
        Fd up(openat(at.fd,"..",dirs));
        if (up.fd<0 || fstat(up.fd,&next)) return true;
        if (same(next,here)) return false;
        at.reset(up.fd); up.fd=-1;
    }
    return true;
}
bool namedFile(int dir, const char *name, const struct stat &st) {
    struct stat now{}; return fstatat(dir,name,&now,AT_SYMLINK_NOFOLLOW)==0 && unchanged(st,now);
}
bool readExact(int fd, const void *wanted, size_t n) {
    const unsigned char *p = static_cast<const unsigned char *>(wanted);
    unsigned char buf[4096]; size_t at=0;
    while (at<n) {
        size_t chunk=n-at<sizeof(buf)?n-at:sizeof(buf); ssize_t got=pread(fd,buf,chunk,at);
        if (got<0 && errno==EINTR) continue;
        if (got<=0 || memcmp(buf,p+at,size_t(got))) return false;
        at+=size_t(got);
    }
    ssize_t end; do { end=pread(fd,buf,1,n); } while(end<0 && errno==EINTR);
    return end==0;
}
bool writeVerified(int fd, const void *data, size_t n) {
    size_t at=0; while (at<n) {
        ssize_t done=write(fd,static_cast<const char *>(data)+at,n-at);
        if(done<0 && errno==EINTR) continue;
        if(done<=0) return false;
        at+=size_t(done);
    }
    return fsync(fd)==0 && readExact(fd,data,n);
}
std::string hash(const void *data,size_t len) {
    digest::SHA256_CTX c; unsigned char out[32]; char result[65];
    digest::sha256_init(&c); digest::sha256_update(&c,static_cast<const unsigned char *>(data),len); digest::sha256_final(&c,out);
    for(unsigned i=0;i<32;++i) snprintf(result+2*i,3,"%02x",unsigned(out[i]));
    return result;
}
std::string quoted(const std::string &s) {
    std::string out="\""; for(char c:s){ if(c=='"' || c=='\\') out+='\\'; out+=c; } return out+'"';
}
bool validName(const char *n) {
    if(!safeText(n,196) || strchr(n,'/') || strpbrk(n,"<>:\"|?*")) return false;
    size_t len=strlen(n);
    if(len<=4 || strcasecmp(n+len-4,".bes") || n[0]=='.' || n[len-5]=='.' || n[len-5]==' ') return false;
    std::string stem(n,strcspn(n,".")); for(char &c:stem) if(c>='a'&&c<='z') c-=32;
    return stem!="CON"&&stem!="PRN"&&stem!="AUX"&&stem!="NUL" &&
        !(stem.size()==4 && (stem.substr(0,3)=="COM"||stem.substr(0,3)=="LPT") && stem[3]>='1'&&stem[3]<='9');
}
}
bool CeBackupStore(const CeBackupInput &in, char *location, size_t room, const char *&error) {
    error="Choose an existing folder outside the game, its package and your careers.";
    if(!location || !room) return false;
    location[0]=0;
    if(in.filename && CeBackupFileHasDirectionMark(in.filename,strnlen(in.filename,196))) {
        error=CeBackupDirectionReason(); return false;
    }
    if(!absolute(in.folder)||!absolute(in.user)||!absolute(in.game)||!absolute(in.package)) return false;
    const std::string root=in.folder,user=in.user,game=in.game,package=in.package;
    if(inside(root,user)||inside(root,game)||inside(root,package)) return false;
    Fd rootFd(directory(root)), userFd(directory(user)), gameFd(directory(game)), packageFd(directory(package));
    if(rootFd.fd<0 || userFd.fd<0 || gameFd.fd<0 || packageFd.fd<0 || underIdentity(rootFd.fd,userFd.fd) ||
       underIdentity(rootFd.fd,gameFd.fd) || underIdentity(rootFd.fd,packageFd.fd)) return false;
    error="This career is not a separate, unchanged file in your CE saves.";
    if(!validName(in.filename)||!in.selected||in.length!=10004) return false;
    Fd saves(openat(userFd.fd,"savegames",dirs));
    if(saves.fd<0) return false;
    Fd source(openat(saves.fd,in.filename,input)); struct stat initial{};
    if(source.fd<0 || fstat(source.fd,&initial) || !S_ISREG(initial.st_mode) || initial.st_nlink!=1 ||
       initial.st_size!=static_cast<off_t>(in.length) || !readExact(source.fd,in.selected,in.length)) return false;
    const std::string savesPath=user+"/savegames";
    auto sourceValid=[&](){return namedDir(saves.fd,savesPath) && namedFile(saves.fd,in.filename,initial);};
    if(!sourceValid()) return false;
    time_t now=time(nullptr); struct tm utc{}; char stem[64],date[40],leaf[96];
    error="A new backup folder could not be created. Choose another folder.";
    if(!gmtime_r(&now,&utc)||!strftime(stem,sizeof(stem),"onslaught-backup-%Y%m%d-%H%M%S",&utc)||
       !strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%SZ",&utc)) return false;
    Fd set; bool made=false;
    for(int suffix=0;suffix<1000;++suffix) {
        if(suffix) snprintf(leaf,sizeof(leaf),"%s-%d",stem,suffix); else snprintf(leaf,sizeof(leaf),"%s",stem);
        if(!namedDir(rootFd.fd,root)) return false;
        if(!mkdirat(rootFd.fd,leaf,0700)){made=true;break;}
        if(errno!=EEXIST) return false;
    }
    if(!made) return false;
    const std::string folder=root+"/"+leaf;
    error="The backup was not verified. An incomplete folder may remain.";
    set.reset(openat(rootFd.fd,leaf,dirs));
    if(set.fd<0 || !namedDir(set.fd,folder) || folder.size()>=room) return false;
    Fd copy(openat(set.fd,in.filename,create,0600)); struct stat copyInfo{};
    if(copy.fd<0 || !writeVerified(copy.fd,in.selected,in.length) || fstat(copy.fd,&copyInfo)) return false;
    const std::string inputHash=hash(in.selected,in.length);
    // Read the copy again from its directory entry before committing the manifest.
    Fd check(openat(set.fd,in.filename,input)); struct stat checkInfo{};
    unsigned char copied[10004]; size_t at=0;
    if(check.fd<0||fstat(check.fd,&checkInfo)||!unchanged(copyInfo,checkInfo)) return false;
    while(at<sizeof(copied)){ssize_t n=read(check.fd,copied+at,sizeof(copied)-at);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return false;
        at+=size_t(n);
    }
    if(inputHash!=hash(copied,sizeof(copied)) || !sourceValid()) return false;
    std::string manifest="{\"Folder\":"+quoted(folder)+",\"Created\":"+quoted(date)+",\"Game\":"+quoted(user)+
        ",\"Files\":[{\"Name\":"+quoted(in.filename)+",\"Original\":"+quoted(savesPath+"/"+in.filename)+
        ",\"Size\":10004,\"Sha256\":"+quoted(inputHash)+"}],\"Reason\":\"Manual: selected CE career\",\"Target\":1}\n";
    Fd stage(openat(set.fd,".manifest-pending",create,0600)); struct stat stageInfo{};
    if(stage.fd<0 || !writeVerified(stage.fd,manifest.data(),manifest.size()) || fstat(stage.fd,&stageInfo) ||
       !namedFile(set.fd,".manifest-pending",stageInfo) || !namedFile(set.fd,in.filename,copyInfo) ||
       !sourceValid() || !namedDir(set.fd,folder) || fsync(set.fd) || fsync(rootFd.fd)) return false;
    // RENAME_NOREPLACE publishes the entire manifest or leaves it absent. Never replace or delete a set.
    if(renameat2(set.fd,".manifest-pending",set.fd,"onslaught-backup.json",RENAME_NOREPLACE)) return false;
    struct stat published{}; Fd final(openat(set.fd,"onslaught-backup.json",input));
    if(final.fd<0 || fstat(final.fd,&published) || !same(stageInfo,published) || published.st_nlink!=1 ||
       !readExact(final.fd,manifest.data(),manifest.size()) || !namedFile(set.fd,in.filename,copyInfo) ||
       !namedDir(set.fd,folder) || fsync(set.fd) || fsync(rootFd.fd)) return false;
    memcpy(location,folder.c_str(),folder.size()+1); error=nullptr; return true;
}
