// SPDX-License-Identifier: GPL-3.0-only
#if defined(CE_NETWORK_ICE)
#include "ice_exchange.h"
#include "monocypher.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr size_t kHeader = 83, kTag = 16, kMaxSdp = 4095;
constexpr uint64_t kLifetime = 300, kClockSkew = 30;
const char kMagic[] = "CEICE002";
void Put64(uint8_t* p, uint64_t n) { for (int i=0;i<8;++i) p[i]=uint8_t(n>>(i*8)); }
uint64_t Get64(const uint8_t* p) {
    uint64_t n=0; for (int i=0;i<8;++i) n|=uint64_t(p[i])<<(i*8); return n;
}
bool Text(const char* p, size_t size) {
    if (!p || !size || size>kMaxSdp) return false;
    for (size_t i=0;i<size;++i) {
        const unsigned char c=static_cast<unsigned char>(p[i]);
        if (c!='\r' && c!='\n' && c!='\t' && (c<32 || c>126)) return false;
    }
    return true;
}
std::string Hex(const uint8_t* bytes) {
    const char digits[]="0123456789abcdef";
    std::string s;
    for (size_t i=0;i<32;++i) { s+=digits[bytes[i]>>4]; s+=digits[bytes[i]&15]; }
    return s;
}
struct Directory {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    std::wstring path;
    ~Directory() { if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool Open(const char* name) {
        int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,nullptr,0);
        if (!count) return false;
        std::vector<wchar_t> wide(static_cast<size_t>(count));
        if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,wide.data(),count)) return false;
        path=wide.data();
        handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        return handle!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle,&info) &&
            (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) &&
            !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
    }
    std::wstring Child(const std::string& name) const {
        return path+L"\\"+std::wstring(name.begin(),name.end());
    }
    int Read(const std::string& name, std::vector<uint8_t>& bytes) {
        HANDLE file=CreateFileW(Child(name).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
            OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if (file==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND?0:-1;
        BY_HANDLE_FILE_INFORMATION info{}; DWORD got=0;
        bool ok=GetFileInformationByHandle(file,&info) && !info.nFileSizeHigh &&
            !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
            info.nFileSizeLow>=kHeader+kTag && info.nFileSizeLow<=kHeader+kTag+kMaxSdp;
        if (ok) {
            bytes.resize(info.nFileSizeLow);
            ok=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr) && got==bytes.size();
        }
        CloseHandle(file); return ok?1:-1;
    }
    bool Publish(const std::string& name, const std::vector<uint8_t>& bytes) {
        auto temporary=Child("."+name+".pending");
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if (file==INVALID_HANDLE_VALUE) return false;
        DWORD written=0;
        bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
            written==bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (ok) ok=MoveFileExW(temporary.c_str(),Child(name).c_str(),MOVEFILE_WRITE_THROUGH)!=0;
        if (!ok) DeleteFileW(temporary.c_str());
        return ok;
    }
#else
    int handle=-1;
    ~Directory() { if (handle>=0) close(handle); }
    bool Open(const char* name) {
        handle=open(name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        struct stat s{};
        return handle>=0 && fstat(handle,&s)==0 && S_ISDIR(s.st_mode) &&
            s.st_uid==geteuid() && !(s.st_mode&0077);
    }
    int Read(const std::string& name, std::vector<uint8_t>& bytes) {
        int file=openat(handle,name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if (file<0) return errno==ENOENT?0:-1;
        struct stat s{};
        bool ok=fstat(file,&s)==0 && S_ISREG(s.st_mode) && s.st_size>=static_cast<off_t>(kHeader+kTag) &&
            s.st_size<=static_cast<off_t>(kHeader+kTag+kMaxSdp);
        if (ok) {
            bytes.resize(static_cast<size_t>(s.st_size)); size_t pos=0;
            while (pos<bytes.size()) {
                ssize_t n=read(file,bytes.data()+pos,bytes.size()-pos);
                if (n<0 && errno==EINTR) continue;
                if (n<=0) { ok=false; break; }
                pos+=static_cast<size_t>(n);
            }
            uint8_t extra=0; if (ok && read(file,&extra,1)!=0) ok=false;
        }
        close(file); return ok?1:-1;
    }
    bool Publish(const std::string& name, const std::vector<uint8_t>& bytes) {
        const std::string temporary="."+name+".pending";
        int file=openat(handle,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if (file<0) return false;
        size_t pos=0; bool ok=true;
        while (pos<bytes.size()) {
            ssize_t n=write(file,bytes.data()+pos,bytes.size()-pos);
            if (n<0 && errno==EINTR) continue;
            if (n<=0) { ok=false; break; }
            pos+=static_cast<size_t>(n);
        }
        if (ok) ok=fsync(file)==0;
        if (close(file)!=0) ok=false;
        if (ok) ok=linkat(handle,temporary.c_str(),handle,name.c_str(),0)==0;
        unlinkat(handle,temporary.c_str(),0);
        return ok;
    }
#endif
};
}

struct CeIceExchange::Impl {
    Directory directory;
    CE_NetRole role=CE_NET_HOST;
    std::array<uint8_t,32> key{},context{},attempt{},local_fingerprint{},remote_fingerprint{};
    uint64_t created=0,expires=0;
    bool published=false,remote=false;
    const char* error="";
    std::string offer,answer;
    ~Impl() { crypto_wipe(key.data(),key.size()); crypto_wipe(attempt.data(),attempt.size()); }
    CeIceExchangeResult Fail(const char* text) { error=text; return CeIceExchangeResult::Error; }
    bool Timely(uint64_t now) const {
        return now<expires && (now>=created || created-now<=kClockSkew);
    }
    CeIceExchangeResult Decode(const std::vector<uint8_t>& record, CE_NetRole expected,
                               bool bind_attempt, uint64_t now, std::vector<uint8_t>& plain) {
        if (record.size()<kHeader+kTag+1 || record.size()>CeIceExchange::MaxRecordBytes)
            return Fail("ICE exchange context or lifetime mismatch");
        const size_t length=size_t(record[81])|(size_t(record[82])<<8);
        const uint64_t issued=Get64(record.data()+9),end=Get64(record.data()+17);
        if (std::memcmp(record.data(),kMagic,8) || record[8]!=expected ||
            !length || length>kMaxSdp || record.size()!=kHeader+length+kTag ||
            issued>=end || end-issued>kLifetime || now>=end ||
            (issued>now && issued-now>kClockSkew) ||
            (bind_attempt && (end>expires || crypto_verify32(record.data()+25,attempt.data()))))
            return Fail("ICE exchange context or lifetime mismatch");
        plain.resize(length);
        const bool valid=crypto_aead_unlock(plain.data(),record.data()+kHeader+length,key.data(),
            record.data()+57,record.data(),kHeader,record.data()+kHeader,length)==0 &&
            Text(reinterpret_cast<const char*>(plain.data()),length);
        if (!valid) {
            crypto_wipe(plain.data(),plain.size()); plain.clear();
            return Fail("ICE exchange authentication or description failed");
        }
        return CeIceExchangeResult::Ready;
    }
};

CeIceExchange::CeIceExchange(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
CeIceExchange::~CeIceExchange()=default;
const char* CeIceExchange::Error() const { return impl_->error; }
const uint8_t* CeIceExchange::Context() const { return impl_->context.data(); }
CE_NetRole CeIceExchange::Role() const { return impl_->role; }

bool CeIceExchange::ContextForConfig(const CE_NetConfig* config,uint8_t context[32]) noexcept {
    static_assert(CE_NET_SECRET_BYTES==32 && CE_NET_DIGEST_BYTES==32,"CE context size");
    if (!config || !context || (config->role!=CE_NET_HOST && config->role!=CE_NET_GUEST)) return false;
    uint8_t nonzero=0; for (auto byte:config->secret) nonzero|=byte;
    if (!nonzero) return false;
    const char label[]="CE-ICE-context-v1";
    uint8_t message[sizeof(label)-1+CE_NET_DIGEST_BYTES];
    std::memcpy(message,label,sizeof(label)-1);
    std::memcpy(message+sizeof(label)-1,config->compatibility,CE_NET_DIGEST_BYTES);
    crypto_blake2b_keyed(context,32,config->secret,CE_NET_SECRET_BYTES,message,sizeof(message));
    return true;
}

std::unique_ptr<CeIceExchange> CeIceExchange::Open(const CE_NetConfig& config,
                                                const char* path,uint64_t now) {
    static_assert(CE_NET_SECRET_BYTES==32 && CE_NET_DIGEST_BYTES==32,"CE context size");
    if (!path || !*path || (config.role!=CE_NET_HOST && config.role!=CE_NET_GUEST) ||
        now>std::numeric_limits<uint64_t>::max()-kLifetime) return nullptr;
    uint8_t nonzero=0; for (auto byte:config.secret) nonzero|=byte;
    if (!nonzero) return nullptr;
    std::unique_ptr<Impl> p(new Impl);
    if (!p->directory.Open(path)) return nullptr;
    auto derive=[&](const char* label,uint8_t* output) {
        std::vector<uint8_t> message(label,label+std::strlen(label));
        message.insert(message.end(),config.compatibility,config.compatibility+CE_NET_DIGEST_BYTES);
        crypto_blake2b_keyed(output,32,config.secret,CE_NET_SECRET_BYTES,message.data(),message.size());
    };
    if (!ContextForConfig(&config,p->context.data())) return nullptr;
    derive("CE-ICE-record-key-v1",p->key.data());
    p->role=static_cast<CE_NetRole>(config.role); p->created=now; p->expires=now+kLifetime;
    const std::string prefix="ce-ice-"+Hex(p->context.data());
    p->offer=prefix+".offer"; p->answer=prefix+".answer";
    std::vector<uint8_t> old;
    // Other stage contexts may coexist. Same-context files are never reused or overwritten.
    if (p->directory.Read(p->answer,old)!=0 ||
        (p->role==CE_NET_HOST && p->directory.Read(p->offer,old)!=0)) return nullptr;
    if (p->role==CE_NET_HOST && !CE_NetGenerateSecret(p->attempt.data())) return nullptr;
    return std::unique_ptr<CeIceExchange>(new CeIceExchange(std::move(p)));
}

CeIceExchangeResult CeIceExchange::PublishLocal(const char* sdp,size_t size,uint64_t now) {
    auto& p=*impl_;
    if (*p.error) return CeIceExchangeResult::Error;
    if (p.published) return CeIceExchangeResult::Ready;
    if (!p.Timely(now)) return p.Fail("ICE exchange expired");
    if (!Text(sdp,size)) return p.Fail("Invalid local ICE description");
    if (p.role==CE_NET_GUEST && !p.remote) return CeIceExchangeResult::Pending;
    std::array<uint8_t,32> random{};
    if (!CE_NetGenerateSecret(random.data())) return p.Fail("ICE exchange randomness unavailable");
    std::vector<uint8_t> record(kHeader+size+kTag);
    std::memcpy(record.data(),kMagic,8); record[8]=static_cast<uint8_t>(p.role);
    Put64(record.data()+9,p.created); Put64(record.data()+17,p.expires);
    std::memcpy(record.data()+25,p.attempt.data(),32);
    std::memcpy(record.data()+57,random.data(),24); crypto_wipe(random.data(),random.size());
    record[81]=uint8_t(size); record[82]=uint8_t(size>>8);
    crypto_aead_lock(record.data()+kHeader,record.data()+kHeader+size,p.key.data(),
        record.data()+57,record.data(),kHeader,reinterpret_cast<const uint8_t*>(sdp),size);
    if (!p.directory.Publish(p.role==CE_NET_HOST?p.offer:p.answer,record))
        return p.Fail("Cannot publish fresh ICE exchange record");
    crypto_blake2b(p.local_fingerprint.data(),p.local_fingerprint.size(),record.data(),record.size());
    p.published=true;
    return CeIceExchangeResult::Ready;
}

CeIceExchangeResult CeIceExchange::PollRemote(char* sdp,size_t capacity,size_t* size,uint64_t now) {
    auto& p=*impl_;
    if (size) *size=0;
    if (*p.error) return CeIceExchangeResult::Error;
    if (p.remote) {
        std::vector<uint8_t> retained;
        std::array<uint8_t,32> fingerprint{};
        if (p.directory.Read(p.role==CE_NET_HOST?p.answer:p.offer,retained)!=1)
            return p.Fail("Accepted ICE exchange record disappeared");
        crypto_blake2b(fingerprint.data(),fingerprint.size(),retained.data(),retained.size());
        if (crypto_verify32(fingerprint.data(),p.remote_fingerprint.data()))
            return p.Fail("Accepted ICE exchange record replaced");
        return CeIceExchangeResult::Pending;
    }
    if (!p.Timely(now)) return p.Fail("ICE exchange expired");
    if (!sdp || !size || !capacity) return p.Fail("ICE exchange output unavailable");
    if (p.role==CE_NET_HOST && !p.published) return CeIceExchangeResult::Pending;
    std::vector<uint8_t> record;
    const int read=p.directory.Read(p.role==CE_NET_HOST?p.answer:p.offer,record);
    if (!read) return CeIceExchangeResult::Pending;
    if (read<0) return p.Fail("Invalid ICE exchange file");
    const auto expected=p.role==CE_NET_HOST?CE_NET_GUEST:CE_NET_HOST;
    std::vector<uint8_t> plain;
    if (p.Decode(record,expected,p.role==CE_NET_HOST,now,plain)!=CeIceExchangeResult::Ready)
        return CeIceExchangeResult::Error;
    if (plain.size()>=capacity) {
        crypto_wipe(plain.data(),plain.size());
        return p.Fail("ICE exchange authentication or description failed");
    }
    std::memcpy(sdp,plain.data(),plain.size()); sdp[plain.size()]='\0'; *size=plain.size();
    crypto_wipe(plain.data(),plain.size());
    crypto_blake2b(p.remote_fingerprint.data(),p.remote_fingerprint.size(),record.data(),record.size());
    if (p.role==CE_NET_GUEST) {
        std::memcpy(p.attempt.data(),record.data()+25,32);
        p.expires=std::min(p.expires,Get64(record.data()+17));
    }
    p.remote=true;
    return CeIceExchangeResult::Ready;
}

CeIceExchangeResult CeIceExchange::ExportLocalRecord(uint8_t* bytes,size_t capacity,
                                                     size_t* size,uint64_t now) {
    auto& p=*impl_;
    if (size) *size=0;
    if (*p.error) return CeIceExchangeResult::Error;
    if (!bytes || !size || !capacity) return CeIceExchangeResult::Error;
    if (!p.Timely(now)) return p.Fail("ICE exchange expired");
    if (!p.published) return CeIceExchangeResult::Pending;
    std::vector<uint8_t> record;
    if (p.directory.Read(p.role==CE_NET_HOST?p.offer:p.answer,record)!=1)
        return p.Fail("Published ICE exchange record disappeared");
    std::array<uint8_t,32> fingerprint{};
    crypto_blake2b(fingerprint.data(),fingerprint.size(),record.data(),record.size());
    if (crypto_verify32(fingerprint.data(),p.local_fingerprint.data()))
        return p.Fail("Published ICE exchange record replaced");
    std::vector<uint8_t> plain;
    if (p.Decode(record,p.role,true,now,plain)!=CeIceExchangeResult::Ready)
        return CeIceExchangeResult::Error;
    crypto_wipe(plain.data(),plain.size());
    if (record.size()>capacity) return CeIceExchangeResult::Error;
    std::memcpy(bytes,record.data(),record.size()); *size=record.size();
    return CeIceExchangeResult::Ready;
}

CeIceExchangeResult CeIceExchange::ImportPeerRecord(const uint8_t* bytes,size_t size,uint64_t now) {
    auto& p=*impl_;
    if (*p.error) return CeIceExchangeResult::Error;
    if (!bytes || size<kHeader+kTag+1 || size>MaxRecordBytes)
        return p.Fail("ICE exchange context or lifetime mismatch");
    std::vector<uint8_t> record(bytes,bytes+size),retained;
    const std::string& name=p.role==CE_NET_HOST?p.answer:p.offer;
    if (p.remote) {
        if (p.directory.Read(name,retained)!=1)
            return p.Fail("Accepted ICE exchange record disappeared");
        std::array<uint8_t,32> fingerprint{},incoming{};
        crypto_blake2b(fingerprint.data(),fingerprint.size(),retained.data(),retained.size());
        crypto_blake2b(incoming.data(),incoming.size(),record.data(),record.size());
        if (crypto_verify32(fingerprint.data(),p.remote_fingerprint.data()) ||
            crypto_verify32(incoming.data(),p.remote_fingerprint.data()))
            return p.Fail("Accepted ICE exchange record replaced");
        return CeIceExchangeResult::Ready;
    }
    if (!p.Timely(now)) return p.Fail("ICE exchange expired");
    if (p.role==CE_NET_HOST && !p.published) return CeIceExchangeResult::Pending;
    std::vector<uint8_t> plain;
    const auto expected=p.role==CE_NET_HOST?CE_NET_GUEST:CE_NET_HOST;
    if (p.Decode(record,expected,p.role==CE_NET_HOST,now,plain)!=CeIceExchangeResult::Ready)
        return CeIceExchangeResult::Error;
    crypto_wipe(plain.data(),plain.size());
    const int read=p.directory.Read(name,retained);
    if (read<0) return p.Fail("Invalid ICE exchange file");
    if (read==1) {
        if (retained!=record) return p.Fail("Accepted ICE exchange record replaced");
        return CeIceExchangeResult::Ready;
    }
    if (!p.directory.Publish(name,record)) {
        retained.clear();
        if (p.directory.Read(name,retained)!=1 || retained!=record)
            return p.Fail("Cannot publish fresh ICE exchange record");
    }
    // PollRemote owns the one-time SDP delivery and attempt adoption.
    return CeIceExchangeResult::Ready;
}
#endif
