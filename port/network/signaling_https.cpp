// SPDX-License-Identifier: GPL-3.0-only
#if defined(CE_NETWORK_ICE) || defined(CE_NETWORK_ONLINE_HTTPS)
#include "signaling_https.h"
#include "monocypher.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <mutex>
#else
#include <curl/curl.h>
#include <unistd.h>
#if LIBCURL_VERSION_NUM < 0x074a00
#error Native signaling requires libcurl 7.74 or newer
#endif
#endif

namespace {
constexpr size_t kBodyMax=65536,kHeadersMax=8192,kBearerMax=4096;
void Wipe(std::string& text) { if (!text.empty()) crypto_wipe(&text[0],text.size()); text.clear(); }
struct Origin { std::string text,host; uint16_t port=443; };
bool ParseOrigin(const char* raw,Origin& origin) {
    if (!raw) return false;
    size_t n=0; while (n<=2048 && raw[n]) ++n;
    if (n<9 || n>2048) return false;
    origin.text.assign(raw,n);
    if (origin.text.compare(0,8,"https://")) return false;
    std::string authority=origin.text.substr(8);
    if (authority.back()=='/') authority.pop_back();
    auto colon=authority.find(':');
    origin.host=authority.substr(0,colon);
    if (colon!=std::string::npos) {
        unsigned port=0; auto value=authority.substr(colon+1);
        if (value.empty() || value.size()>5) return false;
        for (char c:value) { if (c<'0'||c>'9') return false; port=port*10+unsigned(c-'0'); }
        if (!port || port>65535) return false;
        origin.port=uint16_t(port);
    }
    if (origin.host.empty() || origin.host.size()>253) return false;
    size_t part=0;
    for (size_t i=0;i<=origin.host.size();++i) {
        char c=i==origin.host.size()?'.':origin.host[i];
        if (c=='.') {
            if (!part || part>63 || origin.host[i-part]=='-' || origin.host[i-1]=='-') return false;
            part=0;
        } else {
            if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) return false;
            ++part;
        }
    }
    origin.text="https://"+origin.host+":"+std::to_string(origin.port);
    return true;
}
bool Route(const char* route) {
    if (!route) return false;
    for (const char* allowed:{"pair/current","pair/accept","pair/close","stage/request","stage/poll","stage/record","stage/ack"})
        if (!std::strcmp(route,allowed)) return true;
    return false;
}
bool Bearer(const char* bytes,size_t size) {
    if (!bytes || !size || size>kBearerMax) return false;
    size_t padding=0;
    for (size_t i=0;i<size;++i) {
        const char c=bytes[i];
        if (c=='=') { if (++padding>2) return false; }
        else if (padding || !((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
                             c=='.'||c=='_'||c=='~'||c=='+'||c=='/'||c=='-')) return false;
    }
    return size>padding;
}
struct Headers {
    unsigned code=0;size_t bytes=0;bool complete=false,hasLength=false,allowLobbyCodes=false;
    size_t length=0;std::vector<std::pair<std::string,std::string>> fields;
    bool Line(const char* data,size_t size) {
        if (!data || size<2 || size>4096 || data[size-2]!='\r' || data[size-1]!='\n' || complete || size>kHeadersMax-bytes) return false;
        bytes+=size;size-=2;
        for (size_t i=0;i<size;++i) if (data[i]<32 || data[i]>126) return false;
        if (!code) {
            if (size<12 || size>1090 || (std::memcmp(data,"HTTP/1.1 ",9) && std::memcmp(data,"HTTP/1.0 ",9))) return false;
            if (data[9]<'0'||data[9]>'9'||data[10]<'0'||data[10]>'9'||data[11]<'0'||data[11]>'9') return false;
            code=unsigned(data[9]-'0')*100+unsigned(data[10]-'0')*10+unsigned(data[11]-'0');
            if (allowLobbyCodes && (code==404||code==409)) return true;
            return code==200||code==400||code==401||code==403||code==429||code==503;
        }
        if (!size) {
            auto get=[&](const char* name)->std::string { for (const auto& f:fields) if (f.first==name) return f.second;return {}; };
            auto mediaType=[](const std::string& raw)->std::string {
                const size_t semi=raw.find(';');
                std::string base=(semi==std::string::npos)?raw:raw.substr(0,semi);
                while (!base.empty() && base.back()==' ') base.pop_back();
                for (char& c:base) if (c>='A'&&c<='Z') c=char(c+32);
                return base;
            };
            if (mediaType(get("content-type"))!="application/json" || get("cache-control")!="no-store" ||
                get("pragma")!="no-cache" || get("x-content-type-options")!="nosniff" || !get("location").empty()) return false;
            const auto encoding=get("content-encoding"),transfer=get("transfer-encoding");
            if ((!encoding.empty()&&encoding!="identity") || (!transfer.empty()&&transfer!="chunked")) return false;
            const auto len=get("content-length");
            if (!len.empty()) {
                if (len.size()>6 || !transfer.empty()) return false;
                for (char c:len) { if (c<'0'||c>'9') return false; length=length*10+size_t(c-'0'); }
                if (length>kBodyMax) return false;
                hasLength=true;
            }
            complete=true;return true;
        }
        const size_t maxFields=allowLobbyCodes?48:32;
        if (fields.size()>=maxFields) return false;
        std::string line(data,size);const auto colon=line.find(':');
        if (!colon || colon==std::string::npos || colon>64) return false;
        std::string name=line.substr(0,colon),value=line.substr(colon+1);
        for (char& c:name) { if (c>='A'&&c<='Z') c=char(c+32); if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) return false; }
        while (!value.empty() && value.front()==' ') value.erase(0,1);
        while (!value.empty() && value.back()==' ') value.pop_back();
        if (name=="set-cookie") return true;
        if (value.size()>1024 || name=="location") return false;
        const bool singleValue=(name=="content-type"||name=="content-length"||name=="transfer-encoding"||
                                name=="content-encoding"||name=="cache-control"||name=="pragma"||
                                name=="x-content-type-options");
        if (singleValue) {
            for (const auto& field:fields) if (field.first==name) return false;
        }
        if ((name=="content-length"||name=="transfer-encoding")&&value.empty()) return false;
        fields.emplace_back(std::move(name),std::move(value));return true;
    }
    bool Body(size_t size) const { return complete && size<=kBodyMax && (!hasLength||size==length); }
};

#ifndef _WIN32
bool curlReady=false;
size_t curlTransports=0;
bool ConfigureLinuxCaBundle(CURL* easy) noexcept {
    static const char* const kLinuxCaBundles[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        "/etc/ssl/cert.pem",
    };
    for (const char* bundle : kLinuxCaBundles) {
        if (access(bundle, R_OK) == 0) {
            return curl_easy_setopt(easy, CURLOPT_CAINFO, bundle) == CURLE_OK;
        }
    }
    return true;
}
class CurlHttps final:public CeSignalHttps {
    Origin origin_;CURLM* multi_=nullptr;CURL* easy_=nullptr;curl_slist* list_=nullptr;
    std::string body_,received_,url_;Headers headers_;uint64_t deadline_=0;bool active_=false,failed_=false;
    static size_t Header(char* data,size_t size,size_t count,void* self) noexcept {
        auto& p=*static_cast<CurlHttps*>(self);
        if (size && count>std::numeric_limits<size_t>::max()/size) return 0;
        const size_t n=size*count;
        try { if (p.headers_.Line(data,n)) return n; } catch (...) {}
        p.failed_=true;return 0;
    }
    static size_t Data(char* data,size_t size,size_t count,void* self) noexcept {
        auto& p=*static_cast<CurlHttps*>(self);
        if (size && count>std::numeric_limits<size_t>::max()/size) return 0;
        const size_t n=size*count;
        if (!p.headers_.complete || n>kBodyMax-p.received_.size()) { p.failed_=true;return 0; }
        try { p.received_.append(data,n);return n; } catch (...) { p.failed_=true;return 0; }
    }
    void ClearHandles() noexcept {
        if (easy_ && multi_) curl_multi_remove_handle(multi_,easy_);
        if (easy_) curl_easy_cleanup(easy_);
        easy_=nullptr;
        for (auto* node=list_;node;node=node->next) if (node->data) crypto_wipe(node->data,std::strlen(node->data));
        if (list_) curl_slist_free_all(list_);
        list_=nullptr;Wipe(body_);url_.clear();
    }
public:
    explicit CurlHttps(Origin origin):origin_(std::move(origin)),multi_(curl_multi_init()) { ++curlTransports; }
    ~CurlHttps() override { Cancel();if (multi_) curl_multi_cleanup(multi_);--curlTransports; }
    bool Valid() const { return multi_!=nullptr; }
    bool Begin(const char* route,const char* bearer,size_t bearerSize,const std::string& body,uint64_t now,uint64_t deadline) noexcept override {
        if (active_||!multi_||!Route(route)||!Bearer(bearer,bearerSize)||body.size()>kBodyMax||deadline<=now) return false;
        try {
            ClearHandles();Wipe(received_);headers_=Headers{};failed_=false;
            body_=body;url_=origin_.text+"/ce-signaling/v1/"+route;
            easy_=curl_easy_init();if (!easy_) return false;
            auto add=[&](const std::string& value) { auto* next=curl_slist_append(list_,value.c_str());if (!next) return false;list_=next;return true; };
            std::string authorization="Authorization: Bearer ";authorization.reserve(32+bearerSize);authorization.append(bearer,bearerSize);
            const bool added=add(authorization);Wipe(authorization);
            if (!added||!add("Content-Type: application/json")||!add("Accept-Encoding: identity")||!add("Expect:")) { ClearHandles();return false; }
            const long timeout=long(std::min<uint64_t>(10000,deadline-now));
#define CE_CURL_SET(option,value) if (curl_easy_setopt(easy_,option,value)!=CURLE_OK) { ClearHandles();return false; }
            CE_CURL_SET(CURLOPT_URL,url_.c_str());CE_CURL_SET(CURLOPT_POST,1L);
            CE_CURL_SET(CURLOPT_POSTFIELDS,body_.data());CE_CURL_SET(CURLOPT_POSTFIELDSIZE_LARGE,curl_off_t(body_.size()));
            CE_CURL_SET(CURLOPT_HTTPHEADER,list_);CE_CURL_SET(CURLOPT_PROXY,"");CE_CURL_SET(CURLOPT_NOPROXY,"*");
            CE_CURL_SET(CURLOPT_NETRC,long(CURL_NETRC_IGNORED));CE_CURL_SET(CURLOPT_HTTPAUTH,long(CURLAUTH_NONE));
            CE_CURL_SET(CURLOPT_FOLLOWLOCATION,0L);CE_CURL_SET(CURLOPT_MAXREDIRS,0L);
            if (!ConfigureLinuxCaBundle(easy_)) { ClearHandles();return false; }
            CE_CURL_SET(CURLOPT_PROTOCOLS,long(CURLPROTO_HTTPS));CE_CURL_SET(CURLOPT_REDIR_PROTOCOLS,long(CURLPROTO_HTTPS));
            CE_CURL_SET(CURLOPT_SSL_VERIFYPEER,1L);CE_CURL_SET(CURLOPT_SSL_VERIFYHOST,2L);CE_CURL_SET(CURLOPT_SSLVERSION,long(CURL_SSLVERSION_TLSv1_2));
            CE_CURL_SET(CURLOPT_NOSIGNAL,1L);CE_CURL_SET(CURLOPT_TIMEOUT_MS,timeout);CE_CURL_SET(CURLOPT_CONNECTTIMEOUT_MS,std::min(timeout,5000L));
            CE_CURL_SET(CURLOPT_HTTP_VERSION,long(CURL_HTTP_VERSION_1_1));CE_CURL_SET(CURLOPT_HTTP_CONTENT_DECODING,0L);
            CE_CURL_SET(CURLOPT_DISALLOW_USERNAME_IN_URL,1L);CE_CURL_SET(CURLOPT_HEADERFUNCTION,&Header);CE_CURL_SET(CURLOPT_HEADERDATA,this);
            CE_CURL_SET(CURLOPT_WRITEFUNCTION,&Data);CE_CURL_SET(CURLOPT_WRITEDATA,this);
#undef CE_CURL_SET
            if (curl_multi_add_handle(multi_,easy_)!=CURLM_OK) { ClearHandles();return false; }
            deadline_=std::min(deadline,now+uint64_t(timeout));active_=true;return true;
        } catch (...) { ClearHandles();Wipe(received_);return false; }
    }
    CeSignalHttpsState Poll(uint64_t now,CeSignalHttpsReply* out) noexcept override {
        if (!active_||!out) return CeSignalHttpsState::Failed;
        if (now>=deadline_) { Cancel();return CeSignalHttpsState::Failed; }
        int running=0;
        if (curl_multi_perform(multi_,&running)!=CURLM_OK || failed_) { Cancel();return CeSignalHttpsState::Failed; }
        int left=0;CURLMsg* msg=nullptr;
        while ((msg=curl_multi_info_read(multi_,&left))) if (msg->msg==CURLMSG_DONE && msg->easy_handle==easy_) {
            long code=0;bool ok=msg->data.result==CURLE_OK && curl_easy_getinfo(easy_,CURLINFO_RESPONSE_CODE,&code)==CURLE_OK &&
                code==long(headers_.code) && headers_.Body(received_.size());
            ClearHandles();active_=false;
            if (!ok) { Wipe(received_);return CeSignalHttpsState::Failed; }
            out->code=unsigned(code);Wipe(out->body);out->body.swap(received_);return CeSignalHttpsState::Ready;
        }
        return CeSignalHttpsState::Pending;
    }
    void Cancel() noexcept override { ClearHandles();Wipe(received_);active_=false;failed_=false; }
};
#else
// Callback state outlives Cancel until HANDLE_CLOSING. Callbacks only record
// bounded completion flags; the caller's Poll performs subsequent WinHTTP work.
bool ConfigureWinHttpTlsProtocols(HINTERNET session) noexcept {
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls)))
        return true;
    if (GetLastError() != ERROR_INVALID_PARAMETER)
        return false;
#endif
    DWORD tls12 = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    return WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls12, sizeof(tls12)) != FALSE;
}
struct SensitiveWide {
    std::wstring value;
    ~SensitiveWide(){if(!value.empty())crypto_wipe(&value[0],value.size()*sizeof(wchar_t));}
};
struct WinCall {
    HINTERNET session=nullptr,connection=nullptr,request=nullptr;
    std::mutex mutex;std::shared_ptr<WinCall> closingOwner;
    bool sent=false,headers=false,read=false,failed=false,cancelled=false,handleClosed=false;
    DWORD count=0;std::array<char,4096> buffer{};std::string body,received;Headers parsed;
    uint64_t deadline=0;
    ~WinCall() { if (connection) WinHttpCloseHandle(connection);if (session) WinHttpCloseHandle(session);Wipe(body);Wipe(received);crypto_wipe(buffer.data(),buffer.size()); }
    static void CALLBACK Callback(HINTERNET,DWORD_PTR context,DWORD status,void*,DWORD size) noexcept {
        auto* p=reinterpret_cast<WinCall*>(context);if (!p) return;
        std::shared_ptr<WinCall> finalOwner;
        { std::lock_guard<std::mutex> lock(p->mutex);
          if (status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { p->handleClosed=true;finalOwner=std::move(p->closingOwner); }
          else if (!p->cancelled) {
              if (status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE) p->sent=true;
              else if (status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) p->headers=true;
              else if (status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE) { p->count=size;p->read=true; }
              else if (status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR||status==WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) p->failed=true;
          }
        }
    }
};
struct RetiredWinCallPool {
    std::mutex mutex;
    std::vector<std::shared_ptr<WinCall>> calls;
};
RetiredWinCallPool& RetiredWinCalls() noexcept {
    static RetiredWinCallPool* pool = new RetiredWinCallPool();
    return *pool;
}
void RetainRetiredWinCall(const std::shared_ptr<WinCall>& p) noexcept {
    if (!p) return;
    try {
        auto& pool = RetiredWinCalls();
        std::lock_guard<std::mutex> lock(pool.mutex);
        pool.calls.push_back(p);
    } catch (...) {}
}
void ReapRetiredWinCalls() noexcept {
    std::vector<std::shared_ptr<WinCall>> ready;
    try {
        auto& pool = RetiredWinCalls();
        std::lock_guard<std::mutex> lock(pool.mutex);
        auto it = pool.calls.begin();
        while (it != pool.calls.end()) {
            bool closed = false;
            {
                std::lock_guard<std::mutex> callLock((*it)->mutex);
                closed = (*it)->handleClosed;
            }
            if (closed) {
                ready.push_back(std::move(*it));
                it = pool.calls.erase(it);
            } else {
                ++it;
            }
        }
    } catch (...) {}
}
class WinHttps final:public CeSignalHttps {
    Origin origin_;std::shared_ptr<WinCall> call_;unsigned phase_=0;
    static bool ReadHeaders(WinCall& p) {
        DWORD size=0;
        if (WinHttpQueryHeaders(p.request,WINHTTP_QUERY_RAW_HEADERS_CRLF,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&size,WINHTTP_NO_HEADER_INDEX) ||
            GetLastError()!=ERROR_INSUFFICIENT_BUFFER || size>2*(kHeadersMax+1) || size<2) return false;
        std::vector<wchar_t> raw(size/2+1);
        if (!WinHttpQueryHeaders(p.request,WINHTTP_QUERY_RAW_HEADERS_CRLF,WINHTTP_HEADER_NAME_BY_INDEX,raw.data(),&size,WINHTTP_NO_HEADER_INDEX)) return false;
        std::string text;
        for (size_t i=0;i<raw.size()&&raw[i];++i) { if (raw[i]>127) return false;text+=char(raw[i]); }
        size_t pos=0;
        while (pos<text.size()) { auto end=text.find("\r\n",pos);if (end==std::string::npos || !p.parsed.Line(text.data()+pos,end-pos+2)) return false;pos=end+2; }
        return p.parsed.complete;
    }
public:
    explicit WinHttps(Origin origin):origin_(std::move(origin)) {}
    ~WinHttps() override { Cancel(); }
    bool Begin(const char* route,const char* bearer,size_t bearerSize,const std::string& body,uint64_t now,uint64_t deadline) noexcept override {
        ReapRetiredWinCalls();
        if (call_||!Route(route)||!Bearer(bearer,bearerSize)||body.size()>kBodyMax||deadline<=now) return false;
        try {
            auto p=std::make_shared<WinCall>();p->body=body;p->deadline=std::min(deadline,now+std::min<uint64_t>(10000,deadline-now));
            p->session=WinHttpOpen(L"OnslaughtCE-Signaling/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
            if (!p->session) return false;
            DWORD isolate=TRUE;
            // Best-effort: WINHTTP_OPTION_DISABLE_GLOBAL_POOLING requires Windows 10 2004+/11 and is unimplemented on Wine/Proton.
            (void)WinHttpSetOption(p->session,WINHTTP_OPTION_DISABLE_GLOBAL_POOLING,&isolate,sizeof(isolate));
            const int timeout=int(std::min<uint64_t>(10000,deadline-now));
            if (!WinHttpSetTimeouts(p->session,timeout,std::min(timeout,5000),timeout,timeout)) return false;
            if (!ConfigureWinHttpTlsProtocols(p->session)) return false;
            std::wstring host(origin_.host.begin(),origin_.host.end());
            p->connection=WinHttpConnect(p->session,host.c_str(),origin_.port,0);if (!p->connection) return false;
            std::string narrow="/ce-signaling/v1/";narrow+=route;std::wstring path(narrow.begin(),narrow.end());
            p->request=WinHttpOpenRequest(p->connection,L"POST",path.c_str(),L"HTTP/1.1",WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
            if (!p->request) return false;
            DWORD disabled=WINHTTP_DISABLE_AUTHENTICATION|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_REDIRECTS;
            DWORD cap=kHeadersMax;
            if (!WinHttpSetOption(p->request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled))) { WinHttpCloseHandle(p->request);p->request=nullptr;return false; }
            // Best-effort: ReadHeaders also enforces kHeadersMax strictly on every response.
            (void)WinHttpSetOption(p->request,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&cap,sizeof(cap));
            DWORD_PTR context=reinterpret_cast<DWORD_PTR>(p.get());
            if (!WinHttpSetOption(p->request,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context)) ||
                WinHttpSetStatusCallback(p->request,&WinCall::Callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES|WINHTTP_CALLBACK_FLAG_SECURE_FAILURE,0)==WINHTTP_INVALID_STATUS_CALLBACK) {
                WinHttpCloseHandle(p->request);p->request=nullptr;return false;
            }
            p->closingOwner=p;call_=p;phase_=0;
            SensitiveWide headers;headers.value=L"Content-Type: application/json\r\nAccept-Encoding: identity\r\nAuthorization: Bearer ";headers.value.reserve(128+bearerSize);
            for (size_t i=0;i<bearerSize;++i) headers.value+=wchar_t(bearer[i]);
            headers.value+=L"\r\n";
            const BOOL started=WinHttpSendRequest(p->request,headers.value.c_str(),DWORD(headers.value.size()),p->body.empty()?WINHTTP_NO_REQUEST_DATA:&p->body[0],DWORD(p->body.size()),DWORD(p->body.size()),context);
            if (!started) { Cancel();return false; }
            return true;
        } catch (...) { Cancel();return false; }
    }
    CeSignalHttpsState Poll(uint64_t now,CeSignalHttpsReply* out) noexcept override {
        ReapRetiredWinCalls();
        if (!call_||!out) return CeSignalHttpsState::Failed;
        try {
            auto p=call_;bool failed=false,event=false;DWORD count=0;
            { std::lock_guard<std::mutex> lock(p->mutex);failed=p->failed;
              if (phase_==0) { event=p->sent;p->sent=false; }
              else if (phase_==1) { event=p->headers;p->headers=false; }
              else { event=p->read;p->read=false;count=p->count; }
            }
            if (failed || now>=p->deadline) { Cancel();return CeSignalHttpsState::Failed; }
            if (!event) return CeSignalHttpsState::Pending;
            if (phase_==0) { phase_=1;if (!WinHttpReceiveResponse(p->request,nullptr)) { Cancel();return CeSignalHttpsState::Failed; } }
            else {
                if (phase_==1) { if (!ReadHeaders(*p)) { Cancel();return CeSignalHttpsState::Failed; }phase_=2; }
                else {
                    if (count>p->buffer.size() || count>kBodyMax-p->received.size()) { Cancel();return CeSignalHttpsState::Failed; }
                    if (!count) {
                        if (!p->parsed.Body(p->received.size())) { Cancel();return CeSignalHttpsState::Failed; }
                        out->code=p->parsed.code;Wipe(out->body);out->body.swap(p->received);Cancel();return CeSignalHttpsState::Ready;
                    }
                    p->received.append(p->buffer.data(),count);crypto_wipe(p->buffer.data(),p->buffer.size());
                }
                if (!WinHttpReadData(p->request,p->buffer.data(),DWORD(p->buffer.size()),nullptr)) { Cancel();return CeSignalHttpsState::Failed; }
            }
            return CeSignalHttpsState::Pending;
        } catch (...) { Cancel();return CeSignalHttpsState::Failed; }
    }
    void Cancel() noexcept override {
        ReapRetiredWinCalls();
        if (!call_) return;
        auto p=std::move(call_);HINTERNET request=nullptr;
        { std::lock_guard<std::mutex> lock(p->mutex);p->cancelled=true;request=p->request;p->request=nullptr; }
        RetainRetiredWinCall(p);
        p.reset();
        if (request) WinHttpCloseHandle(request);
        ReapRetiredWinCalls();
    }
};
#endif

struct OnlineOrigin { std::string text, host; uint16_t port = 443; bool https = true; };

bool ParseOnlineOrigin(const char* raw, bool allowLoopbackHttp, OnlineOrigin& out) {
    if (!raw) return false;
    Origin httpsOrigin;
    if (ParseOrigin(raw, httpsOrigin)) {
        out.text = std::move(httpsOrigin.text);
        out.host = std::move(httpsOrigin.host);
        out.port = httpsOrigin.port;
        out.https = true;
        return true;
    }
    if (!allowLoopbackHttp) return false;
    size_t n = 0;
    while (n <= 64 && raw[n]) ++n;
    if (n < 18 || n > 64) return false;
    std::string s(raw, n);
    if (s.compare(0, 17, "http://127.0.0.1:") != 0) return false;
    if (s.back() == '/') s.pop_back();
    const std::string portStr = s.substr(17);
    if (portStr.empty() || portStr.size() > 5 || portStr[0] == '0') return false;
    unsigned port = 0;
    for (char c : portStr) {
        if (c < '0' || c > '9') return false;
        port = port * 10u + unsigned(c - '0');
    }
    if (!port || port > 65535u) return false;
    out.host = "127.0.0.1";
    out.port = uint16_t(port);
    out.https = false;
    out.text = "http://127.0.0.1:" + std::to_string(out.port);
    return true;
}

bool IsHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool OnlinePathValid(const char* pathAndQuery) {
    if (!pathAndQuery) return false;
    size_t n = 0;
    while (n <= 1024 && pathAndQuery[n]) ++n;
    static constexpr char kPrefix[] = "/ce-lobby/v1/";
    static constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (n <= kPrefixLen || n > 1024) return false;
    if (std::memcmp(pathAndQuery, kPrefix, kPrefixLen) != 0) return false;
    std::string s(pathAndQuery, n);
    if (s.find("..") != std::string::npos) return false;
    for (unsigned char c : s) {
        if (c < 0x21 || c > 0x7E || c == '#' || c == '\\' || c == '"' ||
            c == '\'' || c == '<' || c == '>' || c == '`')
            return false;
    }
    const size_t qpos = s.find('?');
    const std::string path = (qpos == std::string::npos) ? s : s.substr(0, qpos);
    if (path.size() <= kPrefixLen || path.back() == '/') return false;
    if (path.find("//") != std::string::npos || path.find('%') != std::string::npos) return false;
    size_t pos = kPrefixLen;
    while (pos < path.size()) {
        size_t slash = path.find('/', pos);
        if (slash == std::string::npos) slash = path.size();
        const size_t segLen = slash - pos;
        if (segLen == 0) return false;
        if (segLen == 1 && path[pos] == '.') return false;
        if (segLen == 2 && path[pos] == '.' && path[pos + 1] == '.') return false;
        for (size_t i = pos; i < slash; ++i) {
            const char c = path[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
                return false;
        }
        pos = (slash < path.size()) ? slash + 1 : slash;
    }
    if (qpos != std::string::npos) {
        const std::string query = s.substr(qpos + 1);
        if (query.empty()) return false;
        for (size_t i = 0; i < query.size(); ++i) {
            const char c = query[i];
            if (c == '?') return false;
            if (c == '%') {
                if (i + 2 >= query.size() || !IsHexChar(query[i + 1]) || !IsHexChar(query[i + 2]))
                    return false;
                i += 2;
                continue;
            }
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '=' || c == '&' || c == '.' || c == '+' || c == '-' || c == '~'))
                return false;
        }
    }
    return true;
}

bool OptionalBearerValid(const char* bearer, size_t size) {
    if (size == 0) return true;
    return Bearer(bearer, size);
}

#ifndef _WIN32
class CurlOnlineHttps final : public CeOnlineHttps {
    OnlineOrigin origin_;
    CURLM* multi_ = nullptr;
    CURL* easy_ = nullptr;
    curl_slist* list_ = nullptr;
    std::string body_, received_, url_;
    Headers headers_;
    uint64_t deadline_ = 0;
    bool active_ = false, failed_ = false;

    static size_t Header(char* data, size_t size, size_t count, void* self) noexcept {
        auto& p = *static_cast<CurlOnlineHttps*>(self);
        if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
        const size_t n = size * count;
        try { if (p.headers_.Line(data, n)) return n; } catch (...) {}
        p.failed_ = true;
        return 0;
    }
    static size_t Data(char* data, size_t size, size_t count, void* self) noexcept {
        auto& p = *static_cast<CurlOnlineHttps*>(self);
        if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
        const size_t n = size * count;
        if (!p.headers_.complete || n > kBodyMax - p.received_.size()) { p.failed_ = true; return 0; }
        try { p.received_.append(data, n); return n; } catch (...) { p.failed_ = true; return 0; }
    }
    void ClearHandles() noexcept {
        if (easy_ && multi_) curl_multi_remove_handle(multi_, easy_);
        if (easy_) curl_easy_cleanup(easy_);
        easy_ = nullptr;
        for (auto* node = list_; node; node = node->next)
            if (node->data) crypto_wipe(node->data, std::strlen(node->data));
        if (list_) curl_slist_free_all(list_);
        list_ = nullptr;
        Wipe(body_);
        url_.clear();
    }

public:
    explicit CurlOnlineHttps(OnlineOrigin origin)
        : origin_(std::move(origin)), multi_(curl_multi_init()) { ++curlTransports; }
    ~CurlOnlineHttps() override {
        Cancel();
        if (multi_) curl_multi_cleanup(multi_);
        --curlTransports;
    }
    bool Valid() const { return multi_ != nullptr; }

    bool Begin(CeOnlineHttpMethod method, const char* pathAndQuery,
               const char* bearer, size_t bearerSize,
               const std::string& body, uint64_t now, uint64_t deadline) noexcept override {
        if (active_ || !multi_ || !OnlinePathValid(pathAndQuery) ||
            !OptionalBearerValid(bearer, bearerSize) || deadline <= now)
            return false;
        if (method == CeOnlineHttpMethod::Get && !body.empty()) return false;
        if (body.size() > kBodyMax) return false;
        try {
            ClearHandles();
            Wipe(received_);
            headers_ = Headers{};
            headers_.allowLobbyCodes = true;
            failed_ = false;
            body_ = body;
            url_ = origin_.text + pathAndQuery;
            easy_ = curl_easy_init();
            if (!easy_) return false;
            auto add = [&](const std::string& value) {
                auto* next = curl_slist_append(list_, value.c_str());
                if (!next) return false;
                list_ = next;
                return true;
            };
            if (bearerSize > 0) {
                std::string authorization = "Authorization: Bearer ";
                authorization.reserve(32 + bearerSize);
                authorization.append(bearer, bearerSize);
                const bool added = add(authorization);
                Wipe(authorization);
                if (!added) { ClearHandles(); return false; }
            }
            if (!add("Accept: application/json") || !add("Accept-Encoding: identity")) {
                ClearHandles();
                return false;
            }
            if (method == CeOnlineHttpMethod::Post) {
                if (!add("Content-Type: application/json") || !add("Expect:")) {
                    ClearHandles();
                    return false;
                }
            }
            const long timeout = long(std::min<uint64_t>(30000, deadline - now));
#define CE_ONLINE_CURL_SET(option, value) if (curl_easy_setopt(easy_, option, value) != CURLE_OK) { ClearHandles(); return false; }
            CE_ONLINE_CURL_SET(CURLOPT_URL, url_.c_str());
            if (method == CeOnlineHttpMethod::Post) {
                CE_ONLINE_CURL_SET(CURLOPT_POST, 1L);
                CE_ONLINE_CURL_SET(CURLOPT_POSTFIELDS, body_.empty() ? "" : body_.data());
                CE_ONLINE_CURL_SET(CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(body_.size()));
            } else {
                CE_ONLINE_CURL_SET(CURLOPT_HTTPGET, 1L);
            }
            CE_ONLINE_CURL_SET(CURLOPT_HTTPHEADER, list_);
            CE_ONLINE_CURL_SET(CURLOPT_PROXY, "");
            CE_ONLINE_CURL_SET(CURLOPT_NOPROXY, "*");
            CE_ONLINE_CURL_SET(CURLOPT_NETRC, long(CURL_NETRC_IGNORED));
            CE_ONLINE_CURL_SET(CURLOPT_HTTPAUTH, long(CURLAUTH_NONE));
            CE_ONLINE_CURL_SET(CURLOPT_FOLLOWLOCATION, 0L);
            CE_ONLINE_CURL_SET(CURLOPT_MAXREDIRS, 0L);
            if (origin_.https) {
                if (!ConfigureLinuxCaBundle(easy_)) { ClearHandles(); return false; }
                CE_ONLINE_CURL_SET(CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
                CE_ONLINE_CURL_SET(CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
                CE_ONLINE_CURL_SET(CURLOPT_SSL_VERIFYPEER, 1L);
                CE_ONLINE_CURL_SET(CURLOPT_SSL_VERIFYHOST, 2L);
                CE_ONLINE_CURL_SET(CURLOPT_SSLVERSION, long(CURL_SSLVERSION_TLSv1_2));
            } else {
                CE_ONLINE_CURL_SET(CURLOPT_PROTOCOLS, long(CURLPROTO_HTTP));
                CE_ONLINE_CURL_SET(CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTP));
            }
            CE_ONLINE_CURL_SET(CURLOPT_NOSIGNAL, 1L);
            CE_ONLINE_CURL_SET(CURLOPT_TIMEOUT_MS, timeout);
            CE_ONLINE_CURL_SET(CURLOPT_CONNECTTIMEOUT_MS, std::min(timeout, 5000L));
            CE_ONLINE_CURL_SET(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
            CE_ONLINE_CURL_SET(CURLOPT_HTTP_CONTENT_DECODING, 0L);
            CE_ONLINE_CURL_SET(CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
            CE_ONLINE_CURL_SET(CURLOPT_HEADERFUNCTION, &Header);
            CE_ONLINE_CURL_SET(CURLOPT_HEADERDATA, this);
            CE_ONLINE_CURL_SET(CURLOPT_WRITEFUNCTION, &Data);
            CE_ONLINE_CURL_SET(CURLOPT_WRITEDATA, this);
#undef CE_ONLINE_CURL_SET
            if (curl_multi_add_handle(multi_, easy_) != CURLM_OK) { ClearHandles(); return false; }
            deadline_ = std::min(deadline, now + uint64_t(timeout));
            active_ = true;
            return true;
        } catch (...) {
            ClearHandles();
            Wipe(received_);
            return false;
        }
    }

    CeSignalHttpsState Poll(uint64_t now, CeSignalHttpsReply* out) noexcept override {
        if (!active_ || !out) return CeSignalHttpsState::Failed;
        if (now >= deadline_) { Cancel(); return CeSignalHttpsState::Failed; }
        int running = 0;
        if (curl_multi_perform(multi_, &running) != CURLM_OK || failed_) {
            Cancel();
            return CeSignalHttpsState::Failed;
        }
        int left = 0;
        CURLMsg* msg = nullptr;
        while ((msg = curl_multi_info_read(multi_, &left))) {
            if (msg->msg == CURLMSG_DONE && msg->easy_handle == easy_) {
                long code = 0;
                const bool ok = msg->data.result == CURLE_OK &&
                                curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK &&
                                code == long(headers_.code) && headers_.Body(received_.size());
                ClearHandles();
                active_ = false;
                if (!ok) { Wipe(received_); return CeSignalHttpsState::Failed; }
                out->code = unsigned(code);
                Wipe(out->body);
                out->body.swap(received_);
                return CeSignalHttpsState::Ready;
            }
        }
        return CeSignalHttpsState::Pending;
    }

    void Cancel() noexcept override {
        ClearHandles();
        Wipe(received_);
        active_ = false;
        failed_ = false;
    }
};
#else
class WinOnlineHttps final : public CeOnlineHttps {
    OnlineOrigin origin_;
    std::shared_ptr<WinCall> call_;
    unsigned phase_ = 0;

    static bool ReadHeaders(WinCall& p) {
        DWORD size = 0;
        if (WinHttpQueryHeaders(p.request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                nullptr, &size, WINHTTP_NO_HEADER_INDEX) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER || size > 2 * (kHeadersMax + 1) || size < 2)
            return false;
        std::vector<wchar_t> raw(size / 2 + 1);
        if (!WinHttpQueryHeaders(p.request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                 raw.data(), &size, WINHTTP_NO_HEADER_INDEX))
            return false;
        std::string text;
        for (size_t i = 0; i < raw.size() && raw[i]; ++i) {
            if (raw[i] > 127) return false;
            text += char(raw[i]);
        }
        size_t pos = 0;
        while (pos < text.size()) {
            const auto end = text.find("\r\n", pos);
            if (end == std::string::npos || !p.parsed.Line(text.data() + pos, end - pos + 2))
                return false;
            pos = end + 2;
        }
        return p.parsed.complete;
    }

public:
    explicit WinOnlineHttps(OnlineOrigin origin) : origin_(std::move(origin)) {}
    ~WinOnlineHttps() override { Cancel(); }

    bool Begin(CeOnlineHttpMethod method, const char* pathAndQuery,
               const char* bearer, size_t bearerSize,
               const std::string& body, uint64_t now, uint64_t deadline) noexcept override {
        ReapRetiredWinCalls();
        if (call_ || !OnlinePathValid(pathAndQuery) ||
            !OptionalBearerValid(bearer, bearerSize) || deadline <= now)
            return false;
        if (method == CeOnlineHttpMethod::Get && !body.empty()) return false;
        if (body.size() > kBodyMax) return false;
        try {
            auto p = std::make_shared<WinCall>();
            p->parsed.allowLobbyCodes = true;
            p->body = body;
            p->deadline = std::min(deadline, now + std::min<uint64_t>(30000, deadline - now));
            p->session = WinHttpOpen(L"OnslaughtCE-Lobby/1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
            if (!p->session) return false;
            DWORD isolate = TRUE;
            // Best-effort: WINHTTP_OPTION_DISABLE_GLOBAL_POOLING requires Windows 10 2004+/11 and is unimplemented on Wine/Proton.
            (void)WinHttpSetOption(p->session, WINHTTP_OPTION_DISABLE_GLOBAL_POOLING, &isolate, sizeof(isolate));
            const int timeout = int(std::min<uint64_t>(30000, deadline - now));
            if (!WinHttpSetTimeouts(p->session, timeout, std::min(timeout, 5000), timeout, timeout))
                return false;
            if (origin_.https && !ConfigureWinHttpTlsProtocols(p->session))
                return false;
            std::wstring host(origin_.host.begin(), origin_.host.end());
            p->connection = WinHttpConnect(p->session, host.c_str(), origin_.port, 0);
            if (!p->connection) return false;
            std::string narrow(pathAndQuery);
            std::wstring path(narrow.begin(), narrow.end());
            const wchar_t* verb = (method == CeOnlineHttpMethod::Post) ? L"POST" : L"GET";
            const DWORD flags = origin_.https ? WINHTTP_FLAG_SECURE : 0;
            p->request = WinHttpOpenRequest(p->connection, verb, path.c_str(), L"HTTP/1.1",
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (!p->request) return false;
            DWORD disabled = WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS;
            DWORD cap = kHeadersMax;
            if (!WinHttpSetOption(p->request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) {
                WinHttpCloseHandle(p->request);
                p->request = nullptr;
                return false;
            }
            // Best-effort: ReadHeaders also enforces kHeadersMax strictly on every response.
            (void)WinHttpSetOption(p->request, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &cap, sizeof(cap));
            DWORD_PTR context = reinterpret_cast<DWORD_PTR>(p.get());
            if (!WinHttpSetOption(p->request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
                WinHttpSetStatusCallback(p->request, &WinCall::Callback,
                                         WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                             WINHTTP_CALLBACK_FLAG_HANDLES |
                                             WINHTTP_CALLBACK_FLAG_SECURE_FAILURE,
                                         0) == WINHTTP_INVALID_STATUS_CALLBACK) {
                WinHttpCloseHandle(p->request);
                p->request = nullptr;
                return false;
            }
            p->closingOwner = p;
            call_ = p;
            phase_ = 0;
            SensitiveWide headers;
            headers.value = L"Accept: application/json\r\nAccept-Encoding: identity\r\n";
            if (method == CeOnlineHttpMethod::Post)
                headers.value += L"Content-Type: application/json\r\n";
            if (bearerSize > 0) {
                headers.value += L"Authorization: Bearer ";
                for (size_t i = 0; i < bearerSize; ++i) headers.value += wchar_t(bearer[i]);
                headers.value += L"\r\n";
            }
            LPVOID reqData = (method == CeOnlineHttpMethod::Post && !p->body.empty())
                                 ? static_cast<LPVOID>(&p->body[0])
                                 : WINHTTP_NO_REQUEST_DATA;
            const DWORD reqLen = (method == CeOnlineHttpMethod::Post) ? DWORD(p->body.size()) : 0;
            const BOOL started = WinHttpSendRequest(p->request, headers.value.c_str(),
                                                    DWORD(headers.value.size()),
                                                    reqData, reqLen, reqLen, context);
            if (!started) { Cancel(); return false; }
            return true;
        } catch (...) {
            Cancel();
            return false;
        }
    }

    CeSignalHttpsState Poll(uint64_t now, CeSignalHttpsReply* out) noexcept override {
        ReapRetiredWinCalls();
        if (!call_ || !out) return CeSignalHttpsState::Failed;
        try {
            auto p = call_;
            bool failed = false, event = false;
            DWORD count = 0;
            {
                std::lock_guard<std::mutex> lock(p->mutex);
                failed = p->failed;
                if (phase_ == 0) { event = p->sent; p->sent = false; }
                else if (phase_ == 1) { event = p->headers; p->headers = false; }
                else { event = p->read; p->read = false; count = p->count; }
            }
            if (failed || now >= p->deadline) { Cancel(); return CeSignalHttpsState::Failed; }
            if (!event) return CeSignalHttpsState::Pending;
            if (phase_ == 0) {
                phase_ = 1;
                if (!WinHttpReceiveResponse(p->request, nullptr)) { Cancel(); return CeSignalHttpsState::Failed; }
            } else {
                if (phase_ == 1) {
                    if (!ReadHeaders(*p)) { Cancel(); return CeSignalHttpsState::Failed; }
                    phase_ = 2;
                } else {
                    if (count > p->buffer.size() || count > kBodyMax - p->received.size()) {
                        Cancel();
                        return CeSignalHttpsState::Failed;
                    }
                    if (!count) {
                        if (!p->parsed.Body(p->received.size())) { Cancel(); return CeSignalHttpsState::Failed; }
                        out->code = p->parsed.code;
                        Wipe(out->body);
                        out->body.swap(p->received);
                        Cancel();
                        return CeSignalHttpsState::Ready;
                    }
                    p->received.append(p->buffer.data(), count);
                    crypto_wipe(p->buffer.data(), p->buffer.size());
                }
                if (!WinHttpReadData(p->request, p->buffer.data(), DWORD(p->buffer.size()), nullptr)) {
                    Cancel();
                    return CeSignalHttpsState::Failed;
                }
            }
            return CeSignalHttpsState::Pending;
        } catch (...) {
            Cancel();
            return CeSignalHttpsState::Failed;
        }
    }

    void Cancel() noexcept override {
        ReapRetiredWinCalls();
        if (!call_) return;
        auto p = std::move(call_);
        HINTERNET request = nullptr;
        {
            std::lock_guard<std::mutex> lock(p->mutex);
            p->cancelled = true;
            request = p->request;
            p->request = nullptr;
        }
        RetainRetiredWinCall(p);
        p.reset();
        if (request) WinHttpCloseHandle(request);
        ReapRetiredWinCalls();
    }
};
#endif
}

bool CeSignalHttpsOriginValid(const char* raw) noexcept {
    try { Origin origin;return ParseOrigin(raw,origin); }
    catch (...) { return false; }
}
bool CeInitializeSignalHttps() noexcept {
#ifndef _WIN32
    if(!curlReady)curlReady=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
    return curlReady;
#else
    return true;
#endif
}
bool CeShutdownSignalHttps() noexcept {
#ifndef _WIN32
    if(curlTransports)return false;
    if(curlReady){curl_global_cleanup();curlReady=false;}
#else
    ReapRetiredWinCalls();
#endif
    return true;
}
std::unique_ptr<CeSignalHttps> CeCreateSignalHttps(const char* raw) noexcept {
    try {
        Origin origin;if (!ParseOrigin(raw,origin)) return nullptr;
#ifndef _WIN32
        const auto* version=curlReady?curl_version_info(CURLVERSION_NOW):nullptr;
        if (!version || !(version->features&CURL_VERSION_ASYNCHDNS) || !(version->features&CURL_VERSION_SSL)) return nullptr;
        auto p=std::make_unique<CurlHttps>(std::move(origin));if (!p->Valid()) return nullptr;return p;
#else
        return std::make_unique<WinHttps>(std::move(origin));
#endif
    } catch (...) { return nullptr; }
}

bool CeOnlineHttpsOriginValid(const char* explicitOrigin, bool allowLoopbackHttp) noexcept {
    try { OnlineOrigin origin; return ParseOnlineOrigin(explicitOrigin, allowLoopbackHttp, origin); }
    catch (...) { return false; }
}

bool CeOnlineHttpsPathValid(const char* pathAndQuery) noexcept {
    try { return OnlinePathValid(pathAndQuery); }
    catch (...) { return false; }
}

std::unique_ptr<CeOnlineHttps> CeCreateOnlineHttps(const char* explicitOrigin,
                                                   bool allowLoopbackHttp) noexcept {
    try {
        OnlineOrigin origin;
        if (!ParseOnlineOrigin(explicitOrigin, allowLoopbackHttp, origin)) return nullptr;
#ifndef _WIN32
        const auto* version = curlReady ? curl_version_info(CURLVERSION_NOW) : nullptr;
        if (!version || !(version->features & CURL_VERSION_ASYNCHDNS)) return nullptr;
        if (origin.https && !(version->features & CURL_VERSION_SSL)) return nullptr;
        auto p = std::make_unique<CurlOnlineHttps>(std::move(origin));
        if (!p->Valid()) return nullptr;
        return p;
#else
        return std::make_unique<WinOnlineHttps>(std::move(origin));
#endif
    } catch (...) { return nullptr; }
}
#endif
