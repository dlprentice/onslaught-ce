// SPDX-License-Identifier: GPL-3.0-only
#if defined(CE_NETWORK_ICE)
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
    unsigned code=0;size_t bytes=0;bool complete=false,hasLength=false;
    size_t length=0;std::vector<std::pair<std::string,std::string>> fields;
    bool Line(const char* data,size_t size) {
        if (!data || size<2 || size>1092 || data[size-2]!='\r' || data[size-1]!='\n' || complete || size>kHeadersMax-bytes) return false;
        bytes+=size;size-=2;
        for (size_t i=0;i<size;++i) if (data[i]<32 || data[i]>126) return false;
        if (!code) {
            if (size<12 || (std::memcmp(data,"HTTP/1.1 ",9) && std::memcmp(data,"HTTP/1.0 ",9))) return false;
            if (data[9]<'0'||data[9]>'9'||data[10]<'0'||data[10]>'9'||data[11]<'0'||data[11]>'9') return false;
            code=unsigned(data[9]-'0')*100+unsigned(data[10]-'0')*10+unsigned(data[11]-'0');
            return code==200||code==400||code==401||code==403||code==429||code==503;
        }
        if (!size) {
            auto get=[&](const char* name)->std::string { for (const auto& f:fields) if (f.first==name) return f.second;return {}; };
            if (get("content-type")!="application/json" || get("cache-control")!="no-store" ||
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
        if (fields.size()>=32) return false;
        std::string line(data,size);const auto colon=line.find(':');
        if (!colon || colon==std::string::npos || colon>64) return false;
        std::string name=line.substr(0,colon),value=line.substr(colon+1);
        for (char& c:name) { if (c>='A'&&c<='Z') c=char(c+32); if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) return false; }
        while (!value.empty() && value.front()==' ') value.erase(0,1);
        while (!value.empty() && value.back()==' ') value.pop_back();
        if (value.size()>1024 || name=="location" || name=="set-cookie") return false;
        for (const auto& field:fields) if (field.first==name) return false;
        if ((name=="content-length"||name=="transfer-encoding")&&value.empty()) return false;
        fields.emplace_back(std::move(name),std::move(value));return true;
    }
    bool Body(size_t size) const { return complete && size<=kBodyMax && (!hasLength||size==length); }
};

#ifndef _WIN32
bool curlReady=false;
size_t curlTransports=0;
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
struct SensitiveWide {
    std::wstring value;
    ~SensitiveWide(){if(!value.empty())crypto_wipe(&value[0],value.size()*sizeof(wchar_t));}
};
struct WinCall {
    HINTERNET session=nullptr,connection=nullptr,request=nullptr;
    std::mutex mutex;std::shared_ptr<WinCall> closingOwner;
    bool sent=false,headers=false,read=false,failed=false,cancelled=false;
    DWORD count=0;std::array<char,4096> buffer{};std::string body,received;Headers parsed;
    uint64_t deadline=0;
    ~WinCall() { if (connection) WinHttpCloseHandle(connection);if (session) WinHttpCloseHandle(session);Wipe(body);Wipe(received);crypto_wipe(buffer.data(),buffer.size()); }
    static void CALLBACK Callback(HINTERNET,DWORD_PTR context,DWORD status,void*,DWORD size) noexcept {
        auto* p=reinterpret_cast<WinCall*>(context);if (!p) return;
        std::shared_ptr<WinCall> finalOwner;
        { std::lock_guard<std::mutex> lock(p->mutex);
          if (status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) finalOwner=std::move(p->closingOwner);
          else if (!p->cancelled) {
              if (status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE) p->sent=true;
              else if (status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) p->headers=true;
              else if (status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE) { p->count=size;p->read=true; }
              else if (status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR||status==WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) p->failed=true;
          }
        }
    }
};
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
        if (call_||!Route(route)||!Bearer(bearer,bearerSize)||body.size()>kBodyMax||deadline<=now) return false;
        try {
            auto p=std::make_shared<WinCall>();p->body=body;p->deadline=std::min(deadline,now+std::min<uint64_t>(10000,deadline-now));
            p->session=WinHttpOpen(L"OnslaughtCE-Signaling/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
            if (!p->session) return false;
            DWORD isolate=TRUE;
            if (!WinHttpSetOption(p->session,WINHTTP_OPTION_DISABLE_GLOBAL_POOLING,&isolate,sizeof(isolate))) return false;
            const int timeout=int(std::min<uint64_t>(10000,deadline-now));
            if (!WinHttpSetTimeouts(p->session,timeout,std::min(timeout,5000),timeout,timeout)) return false;
            DWORD tls=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            if (!WinHttpSetOption(p->session,WINHTTP_OPTION_SECURE_PROTOCOLS,&tls,sizeof(tls))) return false;
            std::wstring host(origin_.host.begin(),origin_.host.end());
            p->connection=WinHttpConnect(p->session,host.c_str(),origin_.port,0);if (!p->connection) return false;
            std::string narrow="/ce-signaling/v1/";narrow+=route;std::wstring path(narrow.begin(),narrow.end());
            p->request=WinHttpOpenRequest(p->connection,L"POST",path.c_str(),L"HTTP/1.1",WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
            if (!p->request) return false;
            DWORD disabled=WINHTTP_DISABLE_AUTHENTICATION|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_REDIRECTS;
            DWORD cap=kHeadersMax;
            if (!WinHttpSetOption(p->request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) ||
                !WinHttpSetOption(p->request,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&cap,sizeof(cap))) { WinHttpCloseHandle(p->request);p->request=nullptr;return false; }
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
        if (!call_) return;
        auto p=std::move(call_);HINTERNET request=nullptr;
        { std::lock_guard<std::mutex> lock(p->mutex);p->cancelled=true;request=p->request;p->request=nullptr; }
        if (request) WinHttpCloseHandle(request);
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
#endif
