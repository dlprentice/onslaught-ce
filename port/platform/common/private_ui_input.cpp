#include "private_ui_input.h"
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
bool PortPrivateUiConfigured() noexcept { return false; }
int PortPrivateUiStatus() noexcept { return PORT_PRIVATE_UI_DISABLED; }
void PortPrivateUiPump(uint64_t,PortPrivateUiChar,void*) noexcept {}
bool PortPrivateUiChooseFolder(PortPrivateUiFolderCallback,void*) noexcept { return false; }
void PortPrivateUiCancelFolder() noexcept {}
#else
namespace {
struct State {
    bool initialized, configured, ready, cancelled, pumped;
    int fd, status;
    uint64_t lastPresent;
    unsigned char bytes[16+4096+1];
    size_t used, need, offset, length;
    unsigned kind, tag, chooser;
    uint32_t due;
    PortPrivateUiFolderCallback folder;
    void* folderContext;
};
State state{};
struct TransferredInput {
    int fd;
    ~TransferredInput() { close(fd); }
};
void Wipe(void* memory, size_t size) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(memory);
    while (size--) *p++ = 0;
}
void ClearRecord() {
    Wipe(state.bytes, sizeof state.bytes);
    state.used=state.offset=state.length=0; state.need=16; state.ready=false;
}
void CloseInput() {
#if !defined(_WIN32)
    if (state.fd>=0) close(state.fd);
#endif
    state.fd=-1;
}
void Refuse() {
    CloseInput(); ClearRecord(); state.status=PORT_PRIVATE_UI_REFUSED;
    state.cancelled=state.folder!=nullptr;
}
bool ConflictingInput() {
    const char* names[]={"BEA_PORT_RECORD","BEA_PORT_TAPE","BEA_PORT_INPUT_LOG",
                         "BEA_PORT_INPUT_DUMP","BEA_PORT_D3DLOG"};
    for (const char* name:names) {
        const char* value=getenv(name);
        if (value && *value) return true;
    }
    return false;
}
void Initialize() {
    if (state.initialized) return;
    state.initialized=true; state.fd=-1; state.need=16;
    const char* raw=getenv("BEA_PORT_UI_INPUT_FD");
    if (!raw) return;
    state.configured=true; state.status=PORT_PRIVATE_UI_REFUSED;
#if !defined(_WIN32)
    unsigned descriptor=0, digits=0;
    bool valid=*raw!=0;
    for (const unsigned char* p=reinterpret_cast<const unsigned char*>(raw); *p; ++p) {
        if (*p<'0' || *p>'9' || ++digits>7 || descriptor>1000000) { valid=false; break; }
        descriptor=descriptor*10+*p-'0';
    }
    // No stale numeric descriptor can be mistaken for a launch-authority FD.
    unsetenv("BEA_PORT_UI_INPUT_FD");
    if (!valid || descriptor<3) return;
    const int input=static_cast<int>(descriptor);
    TransferredInput inputOwner{input};
    // run.py pass_fds makes this descriptor inheritable. Seal it before any
    // later validation can refuse, and close it on every return path.
    int descriptorFlags=fcntl(input,F_GETFD);
    if (descriptorFlags<0) return;
    if (fcntl(input,F_SETFD,descriptorFlags|FD_CLOEXEC)) return;
    if (ConflictingInput()) return;
    struct stat st{};
    int flags=fcntl(input,F_GETFL);
    if (flags<0 || (flags&O_ACCMODE)==O_WRONLY || fstat(input,&st) ||
        st.st_uid!=geteuid() || (st.st_mode&0077) ||
        (!S_ISFIFO(st.st_mode) && !(S_ISREG(st.st_mode) && st.st_nlink==1))) return;
    int owned=fcntl(input,F_DUPFD_CLOEXEC,3);
    if (owned<0) return;
    if (fcntl(owned,F_SETFL,flags|O_NONBLOCK)) { close(owned); return; }
    state.fd=owned; state.status=PORT_PRIVATE_UI_WAITING;
#endif
}
uint32_t U32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
bool Header() {
    const unsigned char* p=state.bytes;
    if (memcmp(p,"CEUI",4) || p[4]!=1 || (p[5]!=1 && p[5]!=2)) return false;
    state.kind=p[5]; state.tag=unsigned(p[6]) | unsigned(p[7])<<8;
    state.due=U32(p+8); state.length=U32(p+12);
    if ((state.kind==1 && (state.tag || !state.length || state.length>512)) ||
        (state.kind==2 && (!state.tag || state.length>4096))) return false;
    state.need=16+state.length; return true;
}
bool Payload() {
    const unsigned char* p=state.bytes+16;
    if (state.kind==2 && state.length && p[0]!='/') return false;
    for (size_t i=0;i<state.length;++i)
        if (p[i]<32 || p[i]==127 || (state.kind==1 && p[i]>126)) return false;
    state.bytes[state.need]=0; return true;
}
void ReadRecord() {
#if !defined(_WIN32)
    if (state.fd<0 || state.ready) return;
    // One bounded record per Present; an empty pipe never waits for a writer.
    while (!state.ready) {
        if (state.used==state.need) {
            if (state.need==16 && !Header()) { Refuse(); return; }
            if (state.used==state.need) {
                if (!Payload()) { Refuse(); return; }
                state.ready=true; return;
            }
        }
        ssize_t n=read(state.fd,state.bytes+state.used,state.need-state.used);
        if (n>0) { state.used+=size_t(n); continue; }
        if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) return;
        if (n<0 || state.used) { Refuse(); return; }
        CloseInput(); state.status=PORT_PRIVATE_UI_END;
        state.cancelled=state.folder!=nullptr; return;
    }
#endif
}
void CompleteFolder(const char* path) {
    auto callback=state.folder; void* context=state.folderContext;
    state.folder=nullptr; state.folderContext=nullptr; state.cancelled=false;
    const char* files[]={path,nullptr};
    if (callback) callback(context,path ? files : nullptr,0);
}
}

bool PortPrivateUiConfigured() noexcept { Initialize(); return state.configured; }
int PortPrivateUiStatus() noexcept { Initialize(); return state.status; }
bool PortPrivateUiChooseFolder(PortPrivateUiFolderCallback callback, void* context) noexcept {
    Initialize();
    if (!state.configured) return false;
    if (!callback) return true;
    if (state.folder || state.status!=PORT_PRIVATE_UI_WAITING || state.chooser==65535) {
        callback(context,nullptr,0); return true;
    }
    ++state.chooser; state.folder=callback; state.folderContext=context;
    return true;
}
void PortPrivateUiCancelFolder() noexcept {
    Initialize();
    // A partial or unread reply cannot be resynchronised after cancellation.
    // Retire the harness stream now; only the null callback waits for Pump.
    if (state.folder) Refuse();
}
void PortPrivateUiPump(uint64_t present, PortPrivateUiChar deliver, void* context) noexcept {
    Initialize();
    if (!state.configured || (state.pumped && present<=state.lastPresent)) return;
    state.pumped=true; state.lastPresent=present;
    if (state.cancelled) {
        if (state.ready && state.kind==2 && state.tag==state.chooser) ClearRecord();
        CompleteFolder(nullptr); return;
    }
    ReadRecord();
    if (state.cancelled) { CompleteFolder(nullptr); return; }
    if (!state.ready || present<state.due) return;
    if (state.kind==2) {
        if (!state.folder) return;
        if (state.tag!=state.chooser) { Refuse(); CompleteFolder(nullptr); return; }
        CompleteFolder(state.length ? reinterpret_cast<const char*>(state.bytes+16) : nullptr);
        ClearRecord(); return;
    }
    if (!deliver) return;
    for (unsigned n=0;n<16 && state.offset<state.length;++n) {
        unsigned char character=state.bytes[16+state.offset];
        if (!deliver(context,character)) return;
        state.bytes[16+state.offset++]=0;
    }
    if (state.offset==state.length) ClearRecord();
}
#endif
