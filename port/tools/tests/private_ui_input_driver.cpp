#include "private_ui_input.h"
#if defined(PRIVATE_UI_INVITATION)
#define main InvitationBaselineMain
#include "network_invitation_driver.cpp"
#undef main

int main(int argc,char** argv) {
    if (argc!=2) return 2;
    fs::path root=argv[1];
    userRoot=(root/"user").string(); fs::create_directory(userRoot); chmod(userRoot.c_str(),0700);
    auto host=Package(root,"private-host");
    const auto beforeMeta=Read(host/"pair-admission.json"), beforeToken=Read(host/"identity-token");
    int pipefd[2]; if (pipe(pipefd)) return 2;
    std::string number=std::to_string(pipefd[0]); setenv("BEA_PORT_UI_INPUT_FD",number.c_str(),1);
    Reset();
    std::string path=host.string(); unsigned char header[16]={'C','E','U','I',1,2,1,0,0,0,0,0};
    for (unsigned i=0;i<4;++i) header[12+i]=static_cast<unsigned char>(path.size()>>(i*8));
    Check(write(pipefd[1],header,sizeof header)==sizeof header,"private-header-written");
    Check(write(pipefd[1],path.data(),path.size())==ssize_t(path.size()),"private-path-written");
    close(pipefd[1]);
    PortPrivateUiPump(1,nullptr,nullptr);
    Check(View().state!=CE_INVITATION_PREVIEW_READY && !selections,"private-folder-waits-for-actual-choose");
    Check(CE_InvitationBeginImport("https://fixture.invalid",CE_NET_HOST),"actual-import-choose-action");
    Check(View().state==CE_INVITATION_CHOOSING && pickers==0,"harness-backend-not-os-dialog");
    PortPrivateUiPump(2,nullptr,nullptr);
    Check(View().state==CE_INVITATION_PREVIEW_READY,"private-folder-reaches-real-preview");
    Check(tokenOpens==0 && selections==0 && requests==0,"preview-no-token-selection-or-contact");
    Check(CE_InvitationInstall(1000),"private-explicit-use-selects-authority");
    Check(View().state==CE_INVITATION_SELECTED && selections==1 && selected,"private-use-normal-selection");
    Check(requests==0 && tokenOpens==1,"private-use-bounded-token-and-no-contact");
    Check(Read(host/"pair-admission.json")==beforeMeta && Read(host/"identity-token")==beforeToken,
          "private-input-files-preserved");
    Check(getenv("BEA_PORT_UI_INPUT_FD")==nullptr,"private-descriptor-not-restored-to-child");
    std::printf("checks=%u failures=%u\n",checks,failures);
    return failures ? 1 : 0;
}
#else
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
int failFcntl=0, probeDescriptor=-1;
bool sealProbed=false, sealedBeforeValidation=false;
void ProbeBeforeValidation(int descriptor);
}
extern "C" int __real_fcntl(int,int,...);
extern "C" int __wrap_fcntl(int descriptor,int command,...) {
    if (command==F_GETFL && descriptor==probeDescriptor) {
        probeDescriptor=-1; ProbeBeforeValidation(descriptor);
    }
    if (failFcntl && command==failFcntl) { errno=EINVAL; return -1; }
    if (command==F_GETFD || command==F_GETFL) return __real_fcntl(descriptor,command);
    va_list values; va_start(values,command); int value=va_arg(values,int); va_end(values);
    return __real_fcntl(descriptor,command,value);
}
// Read-only observation appended to the private fixture copy, never production.
extern "C" unsigned PrivateUiRetainedForFixture();
namespace {
int failures=0, checks=0;
void Check(bool ok,const char* label) { ++checks; if (!ok) { ++failures; std::fprintf(stderr,"FAIL %s\n",label); } }
using HWND=void*; using UINT=unsigned; using WPARAM=uintptr_t; using LPARAM=intptr_t;
enum { WM_CHAR=0x0102, WM_KEYDOWN=0x0100 };
struct Message { UINT kind; WPARAM value; };
Message queue[256]; int gHead=0,gTail=0;
bool Post(HWND,UINT kind,WPARAM value,LPARAM) {
    int next=(gTail+1)%256; if (next==gHead) return false;
    queue[gTail]={kind,value}; gTail=next; return true;
}
// These two functions are extracted verbatim from the owned window consumer.
#include "private_ui_window.inc"
std::string text;
void Drain(bool selected=true) {
    while (gHead!=gTail) {
        auto message=queue[gHead]; gHead=(gHead+1)%256;
        Check(message.kind==WM_CHAR,"ordinary-wm-char-route");
        if (selected && message.kind==WM_CHAR) text+=char(message.value);
    }
}
int writer=-1;
int Channel() {
    int p[2]; if (pipe(p)) std::abort(); writer=p[1];
    std::string number=std::to_string(p[0]); setenv("BEA_PORT_UI_INPUT_FD",number.c_str(),1); return p[0];
}
void Write(const unsigned char* bytes,size_t size) {
    while (size) { ssize_t n=write(writer,bytes,size); if (n<=0) std::abort(); bytes+=n; size-=size_t(n); }
}
std::string Record(unsigned kind,unsigned tag,uint32_t due,const std::string& payload) {
    unsigned char h[16]={'C','E','U','I',1,static_cast<unsigned char>(kind),
                         static_cast<unsigned char>(tag),static_cast<unsigned char>(tag>>8)};
    for (unsigned i=0;i<4;++i) { h[8+i]=static_cast<unsigned char>(due>>(i*8)); h[12+i]=static_cast<unsigned char>(payload.size()>>(i*8)); }
    return std::string(reinterpret_cast<char*>(h),16)+payload;
}
void Send(const std::string& record) { Write(reinterpret_cast<const unsigned char*>(record.data()),record.size()); }
void Pump(uint64_t frame) { PortPrivateUiPump(frame,PrivateUiCharacter,nullptr); }
int callbacks=0; std::string folder;
void Folder(void*,const char* const* files,int filter) {
    ++callbacks; Check(filter==0,"ordinary-folder-filter"); folder=files && files[0] ? files[0] : "";
}
const char* self=nullptr;
void Disabled() {
    unsetenv("BEA_PORT_UI_INPUT_FD");
    Check(!PortPrivateUiConfigured() && PortPrivateUiStatus()==PORT_PRIVATE_UI_DISABLED,"disabled-default");
    Check(!PortPrivateUiChooseFolder(Folder,nullptr),"disabled-keeps-os-chooser"); Pump(0);
    Check(callbacks==0 && text.empty(),"disabled-no-effects");
}
void Text() {
    Channel(); std::string expected="AUTHORED-private-text-0123456789-ABC";
    Send(Record(1,0,3,expected)); close(writer);
    Pump(2); Drain(); Check(text.empty(),"text-due-present");
    Pump(3); Drain(false); Check(text.empty(),"ordinary-unselected-editor-ignores-text");
    // A separate record is not needed to prove the queue bound: the first16
    // ordinary characters were delivered but ignored by the unselected editor.
    Pump(3); Drain(); Check(text.empty(),"once-per-present");
    Pump(4); Drain(); Check(text==expected.substr(16,16),"bounded-next-sixteen");
    Pump(5); Drain(); Check(text==expected.substr(16),"remaining-characters-once");
    Pump(6); Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_END,"clean-eof");
}
void Backpressure() {
    Channel(); std::string expected="AUTHORED-private-backpressure"; Send(Record(1,0,0,expected)); close(writer);
    gHead=0;gTail=255; Pump(0); Check(gTail==255,"full-queue-does-not-post");
    gHead=gTail=0; Pump(1); Drain(); Check(text==expected.substr(0,16),"full-queue-does-not-consume");
    Pump(2); Drain(); Check(text==expected,"backpressure-retains-order");
}
void Partial() {
    Channel(); auto record=Record(1,0,0,"AUTHORED-partial"); Write(reinterpret_cast<const unsigned char*>(record.data()),7);
    Pump(0); Drain(); Check(text.empty() && PortPrivateUiStatus()==PORT_PRIVATE_UI_WAITING,"partial-is-nonblocking");
    Write(reinterpret_cast<const unsigned char*>(record.data()+7),record.size()-7); close(writer);
    Pump(1); Drain(); Check(text=="AUTHORED-partial","partial-reassembled");
}
void FolderChoice() {
    Channel(); Send(Record(2,1,0,"/owned/authored-invitation")); close(writer);
    Pump(0); Check(!callbacks,"folder-cannot-choose-itself");
    Check(PortPrivateUiChooseFolder(Folder,nullptr),"explicit-harness-chooser");
    Pump(1); Check(callbacks==1 && folder=="/owned/authored-invitation","explicit-choice-delivered");
    Pump(2); Check(callbacks==1,"chooser-delivers-once");
}
void Cancel() {
    Channel(); Check(PortPrivateUiChooseFolder(Folder,nullptr),"cancel-request");
    Pump(0); PortPrivateUiCancelFolder(); Pump(1);
    Check(callbacks==1 && folder.empty() && PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED,"explicit-cancel-retires-stream");
    Check(PortPrivateUiChooseFolder(Folder,nullptr),"replacement-stays-harness-owned");
    Check(callbacks==2 && folder.empty(),"cancel-cannot-rebind-or-fall-back"); close(writer);
}
void PartialCancel() {
    Channel(); auto record=Record(2,1,0,"/owned/AUTHORED-partial-cancel");
    Write(reinterpret_cast<const unsigned char*>(record.data()),20);
    PortPrivateUiChooseFolder(Folder,nullptr); Pump(0);
    Check(PrivateUiRetainedForFixture()>0 && callbacks==0,"partial-reply-pending");
    PortPrivateUiCancelFolder();
    Check(PrivateUiRetainedForFixture()==0,"cancel-scrubs-partial-immediately");
    Check(callbacks==0,"cancel-callback-remains-deferred");
    signal(SIGPIPE,SIG_IGN); errno=0;
    Check(write(writer,record.data()+20,record.size()-20)<0 && errno==EPIPE,"cancel-closes-unread-reply-stream");
    Pump(1); Check(callbacks==1 && folder.empty(),"partial-cancel-null-callback"); close(writer);
}
void WrongChooser() {
    Channel(); Send(Record(2,2,0,"/owned/stale-invitation")); close(writer);
    PortPrivateUiChooseFolder(Folder,nullptr); Pump(0);
    Check(callbacks==1 && folder.empty() && PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED,"wrong-ordinal-cannot-rebind");
}
void Malformed() {
    Channel(); auto r=Record(1,0,0,std::string(513,'x')); Send(r); close(writer); Pump(0); Drain();
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED && text.empty(),"oversized-refused-before-dispatch");
}
void Truncated() {
    Channel(); auto r=Record(1,0,0,"AUTHORED-truncated"); Write(reinterpret_cast<const unsigned char*>(r.data()),18); close(writer);
    PortPrivateUiChooseFolder(Folder,nullptr); Pump(0); Drain();
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED && text.empty(),"partial-eof-refuses");
    Check(callbacks==1 && folder.empty(),"eof-cancels-pending-chooser");
}
void InvalidText() {
    Channel(); Send(Record(1,0,0,std::string("abc\0def",7))); close(writer); Pump(0); Drain();
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED && text.empty(),"invalid-text-is-atomic");
}
void Permissions() {
    int fd=Channel(); fchmod(fd,0666); close(writer); Pump(0);
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED,"shared-descriptor-refused");
    Check(fcntl(fd,F_GETFD)<0 && errno==EBADF,"metadata-refusal-closes-transferred-reader");
}
void RegularFile() {
    std::string path=std::string(self)+".owned-input";
    int fd=open(path.c_str(),O_CREAT|O_EXCL|O_RDWR,0600); if (fd<0) std::abort();
    writer=fd; Send(Record(1,0,0,"AUTHORED-file")); lseek(fd,0,SEEK_SET);
    std::string number=std::to_string(fd); setenv("BEA_PORT_UI_INPUT_FD",number.c_str(),1);
    Check(PortPrivateUiConfigured() && PortPrivateUiStatus()==PORT_PRIVATE_UI_WAITING,"owned-regular-file-accepted");
    unlink(path.c_str()); Pump(0); Drain(); Check(text=="AUTHORED-file","private-file-delivery");
    Pump(1); Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_END,"private-file-eof");
}
void EmptyEof() {
    Channel(); close(writer); PortPrivateUiChooseFolder(Folder,nullptr); Pump(0);
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_END && callbacks==1 && folder.empty(),"empty-eof-cancels-chooser");
}
void BadHeader() {
    Channel(); auto record=Record(1,0,0,"AUTHORED-bad-header"); record[4]=2; Send(record); close(writer); Pump(0); Drain();
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED && text.empty(),"unsupported-header-refused");
}
void Recording() {
    int fd=Channel(); setenv("BEA_PORT_RECORD","authored-record",1); close(writer); Pump(0);
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED,"recording-refused");
    Check(fcntl(fd,F_GETFD)<0 && errno==EBADF,"recording-refusal-closes-transferred-reader");
}
void Privacy() {
    Channel(); Check(PortPrivateUiConfigured(),"configured-before-log-toggle");
    setenv("BEA_PORT_INPUT_LOG","1",1); Check(!InputLog(),"configured-input-log-suppressed"); close(writer);
    Check(getenv("BEA_PORT_UI_INPUT_FD")==nullptr,"descriptor-environment-consumed");
}
void Exec() {
    int original=Channel(); close(writer); Check(PortPrivateUiConfigured(),"exec-channel-configured");
    Check(fcntl(original,F_GETFD)<0 && errno==EBADF,"original-input-descriptor-consumed");
    int retained=-1; for (int fd=3;fd<128;++fd) if (fcntl(fd,F_GETFD)>=0) retained=fd;
    Check(retained>=3,"private-reader-retained"); if (retained<3) return;
    pid_t pid=fork(); if (!pid) { char value[16]; std::snprintf(value,sizeof value,"%d",retained); execl(self,self,"exec-check",value,nullptr); _exit(43); }
    int status=0; Check(pid>0 && waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"exec-closes-reader-and-environment");
}
bool ClosedAfterExec(int descriptor) {
    pid_t pid=fork();
    if (!pid) { char value[16]; std::snprintf(value,sizeof value,"%d",descriptor); execl(self,self,"exec-check",value,nullptr); _exit(43); }
    int status=0; return pid>0 && waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0;
}
void ProbeBeforeValidation(int descriptor) {
    sealProbed=true; sealedBeforeValidation=ClosedAfterExec(descriptor);
}
void RefusedExec() {
    int fd=Channel(); Send(Record(1,0,0,"AUTHORED-refused-exec")); close(writer);
    setenv("BEA_PORT_RECORD","authored-record",1); PortPrivateUiConfigured();
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED,"refused-exec-state");
    Check(fcntl(fd,F_GETFD)<0 && errno==EBADF,"refused-exec-original-closed");
    Check(ClosedAfterExec(fd),"refused-exec-no-private-reader-or-selector");
}
void EarliestSeal() {
    int fd=Channel(); Send(Record(1,0,0,"AUTHORED-before-refusal")); close(writer);
    fchmod(fd,0666); probeDescriptor=fd; PortPrivateUiConfigured();
    Check(sealProbed && sealedBeforeValidation,"reader-sealed-before-metadata-validation");
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED && fcntl(fd,F_GETFD)<0,"sealed-refusal-closes-original");
}
void FcntlRefusal(int operation) {
    int fd=Channel(); close(writer); failFcntl=operation;
    PortPrivateUiConfigured(); failFcntl=0;
    Check(PortPrivateUiStatus()==PORT_PRIVATE_UI_REFUSED,"fcntl-fault-refuses");
    Check(fcntl(fd,F_GETFD)<0 && errno==EBADF,"fcntl-fault-closes-original");
    bool readersClosed=true;
    for (int reader=3;reader<128;++reader) if (fcntl(reader,F_GETFD)>=0) readersClosed=false;
    Check(readersClosed && ClosedAfterExec(fd),"fcntl-fault-no-original-or-duplicate-leak");
}
void GetDescriptorFailure() { FcntlRefusal(F_GETFD); }
void SealFailure() { FcntlRefusal(F_SETFD); }
void GetFlagsFailure() { FcntlRefusal(F_GETFL); }
void DuplicateFailure() { FcntlRefusal(F_DUPFD_CLOEXEC); }
void NonblockFailure() { FcntlRefusal(F_SETFL); }
}
int main(int argc,char** argv) {
    if (argc==3 && !std::strcmp(argv[1],"exec-check"))
        return fcntl(std::atoi(argv[2]),F_GETFD)<0 && errno==EBADF && !getenv("BEA_PORT_UI_INPUT_FD") ? 0 : 42;
    self=argv[0];
    struct Case { const char* name; void (*run)(); };
    const Case cases[]={{"disabled",Disabled},{"text",Text},{"backpressure",Backpressure},
      {"partial",Partial},{"folder",FolderChoice},{"cancel",Cancel},{"malformed",Malformed},
      {"truncated",Truncated},{"invalid-text",InvalidText},{"permissions",Permissions},
      {"recording",Recording},{"privacy",Privacy},{"exec",Exec},
      {"regular-file",RegularFile},{"empty-eof",EmptyEof},{"bad-header",BadHeader},
      {"refused-exec",RefusedExec},{"earliest-seal",EarliestSeal},
      {"get-descriptor-failure",GetDescriptorFailure},{"seal-failure",SealFailure},
      {"get-flags-failure",GetFlagsFailure},{"duplicate-failure",DuplicateFailure},
      {"nonblock-failure",NonblockFailure},{"partial-cancel",PartialCancel},{"wrong-chooser",WrongChooser}};
    int failed=0;
    for (const auto& c:cases) {
        if (argc==2 && std::strcmp(argv[1],c.name)) continue;
        pid_t pid=fork(); if (!pid) { alarm(4); c.run(); _exit(failures ? 1 : 0); }
        int status=0; bool ok=pid>0 && waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0;
        if (!ok) { ++failed; std::fprintf(stderr,"FAIL case %s\n",c.name); }
    }
    std::printf("private-ui: %d failed cases\n",failed); return failed ? 1 : 0;
}
#endif
