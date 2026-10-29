// SPDX-License-Identifier: GPL-3.0-only
// Actual reader, CRT stream table and worker handler; authored file, controlled pathname/buffer/log endpoints.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <mutex>
using HRESULT=int;using UINT=unsigned;using BYTE=unsigned char;using CHAR=char;using BOOL=int;using DWORD=uint32_t;
#define S_OK 0
#define S_FALSE 1
#define E_FAIL ((int)0x80004005u)
#define FALSE 0
#define TRUE 1
#define TRACE(...) ((void)0)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
class CBaseWaveFileRead {public:virtual ~CBaseWaveFileRead()=default;virtual HRESULT Open(CHAR*)=0;virtual HRESULT Read(UINT,BYTE*,UINT*)=0;virtual HRESULT Close()=0;virtual BOOL HasFormat()=0;virtual DWORD GetSampleRate()=0;virtual DWORD GetChannels()=0;};
#include "oggvorbis.h"
#include "reader.h"
#ifdef FIXTURE_PRIVATE_READER
#undef CE_VERSION
#endif
static void Check(bool,const char*);
static int file_mode,saved_fd=-1,owned_fd=-1,file_error_seen,close_result_seen,close_injections;
static FILE *owned_file;
static const char *owned_name;
static bool file_injected;
// Production CRT registration/close table, with only its pathname open mapped to normal stdio.
#define PORT_LOG_PREFIX "fixture: "
extern "C" FILE *port_fopen(const char *name,const char *mode){return std::fopen(name,mode);}
#include "crtfile.inc"
static int ObserveClose(FILE *file) {
 unsigned matches=0;for(FILE *entry:gOpen)if(entry==file)++matches;
 Check(matches==1,"actual CRT table owns reader stream before close");
 if(file!=owned_file)return bea_crt_fclose(file);
 file_error_seen=std::ferror(file)!=0;
 if(file_mode==1) {
  Check(fcntl(owned_fd,F_GETFD)==-1&&errno==EBADF,"invalidated owned fd has not been reused");
  Check(dup2(saved_fd,owned_fd)==owned_fd,"restore only owned fd for independent close control");
  Check(close(saved_fd)==0,"close owned backup descriptor");saved_fd=-1;
 } else if(file_mode==2) Check(close(owned_fd)==0,"invalidate only owned fd at terminal close");
 ++close_injections;owned_file=nullptr;close_result_seen=bea_crt_fclose(file);
 for(FILE *entry:gOpen)Check(entry!=file,"actual CRT table retires failed close once");
 return close_result_seen;
}
#define fopen bea_crt_port_fopen
#define fclose ObserveClose
#include "reader.inc"
#undef fopen
#undef fclose
#ifdef FIXTURE_PRIVATE_READER
#define CE_VERSION 1
#endif
struct ReadRecord {unsigned requested,got;int status,before,after,state;};
static std::vector<ReadRecord> records;
static unsigned long long pcm_hash=1469598103934665603ull,pcm_bytes;static FILE *pcm_out;
struct ObservedReader:COggFileRead {
 HRESULT Open(CHAR *name) override {
  HRESULT result=COggFileRead::Open(name);
  if(file_mode&&!file_injected&&!strcmp(name,owned_name)&&HasFormat()) {
   owned_file=mFile;owned_fd=fileno(mFile);Check(owned_fd>=0,"reader owns a valid descriptor");
   if(file_mode==1) {saved_fd=dup(owned_fd);Check(saved_fd>=0,"duplicate only owned source fd");Check(close(owned_fd)==0,"invalidate only reader source fd after Open");}
   file_injected=true;
  }
  return result;
 }
 HRESULT Read(UINT n,BYTE *p,UINT *got) override {
  bool before=HasFormat();HRESULT r=COggFileRead::Read(n,p,got);
  for(unsigned i=0;i<*got;++i){pcm_hash^=p[i];pcm_hash*=1099511628211ull;}pcm_bytes+=*got;if(pcm_out&&*got)fwrite(p,1,*got,pcm_out);
  records.push_back({n,*got,r,(int)before,(int)HasFormat(),mState});return r;
 }
};
static unsigned checks,errors,shorts,refills,eofdrained,stops;
static void Check(bool b,const char*s){++checks;if(!b){fprintf(stderr,"FAIL %s\n",s);exit(2);}}
enum {PORT_SESSION_MUSIC,PORT_COUNT_MUSIC_REFILLS,PORT_COUNT_MUSIC_SHORT_READS,PORT_COUNT_MUSIC_READ_FAILED};
static void PortSessionCount(int n){if(n==PORT_COUNT_MUSIC_REFILLS)++refills;if(n==PORT_COUNT_MUSIC_SHORT_READS)++shorts;if(n==PORT_COUNT_MUSIC_READ_FAILED)++errors;}
static void PortSessionAsset(int,const char*action,const char*,HRESULT=0){if(!strcmp(action,"EOF drained"))++eofdrained;}
static void PortSessionMediaActive(int,void*,bool){}
static void PortSessionLog(int,bool,const char*,...){}
constexpr DWORD DSBSTATUS_PLAYING=1,DSBSTATUS_LOOPING=4,DSBPLAY_LOOPING=1,DSBLOCK_ENTIREBUFFER=2;
constexpr DWORD STREAM_BUFFER_SIZE=524288,STREAM_HALF=262144;
// The extracted worker uses an authored buffer/publication boundary; the reader itself is production code.
static bool sPortSafeMusicRefill = true;
static BYTE sPortMusicRefill[STREAM_HALF];
static void PortAudioPublishMusicHalf(void *destination,const void *source,DWORD bytes){memcpy(destination,source,bytes);}
struct IDirectSoundBuffer {
 std::vector<BYTE> data=std::vector<BYTE>(STREAM_BUFFER_SIZE);bool playing=false;
 HRESULT GetStatus(DWORD*p){*p=playing?DSBSTATUS_PLAYING|DSBSTATUS_LOOPING:0;return 0;}
 HRESULT Lock(DWORD off,DWORD n,void**p,DWORD*sz,void**p2,DWORD*sz2,DWORD flags){if(flags)n=STREAM_BUFFER_SIZE;Check(off+n<=data.size(),"buffer bounds");*p=data.data()+off;*sz=n;*p2=nullptr;*sz2=0;return 0;}
 HRESULT Unlock(void*,DWORD,void*,DWORD){return 0;}HRESULT SetCurrentPosition(DWORD){return 0;}
 HRESULT Play(DWORD,DWORD,DWORD){playing=true;return 0;}
};
static IDirectSoundBuffer buffer,*sBuffer=&buffer;
static CBaseWaveFileRead*sReader;
static bool MUSIC_STREAM_FINISHED,sCeStreamLockReady,sCeStreamPending,sCeStreamStopped,sPortRefillDiagnostics;
static bool sPortRefillOrderKnown,sPortRefillOrderPending; static unsigned sPortRefillExpectedHalf;
static bool sThreadAwake;static void*sPlayEvent;
static DWORD sBytesStreamed;static int sSilentHalves;static char sTrackName[4096];
static unsigned sPortRefillSequence;static unsigned long long sPortRefillLastNs;
struct CPortMusicStreamGuard{};struct CPortRefillObservation{bool playing=false;};
static CPortRefillObservation PortRefillObservation(){return {};}
static void PortRefillForget(){sPortRefillOrderKnown=false;sPortRefillOrderPending=false;}
static void PortRefillOrder(DWORD){}
static void PortRefillReport(DWORD,const CPortRefillObservation&){}
static void ResetEvent(void*){}
static void SetMusicStreamFinished(bool b){MUSIC_STREAM_FINISHED=b;}
static void StopMusicStream(){++stops;buffer.playing=false;}
#include "worker.inc"
int main(int argc,char**argv){
 if(argc!=7)return 1;pcm_out=fopen(argv[4],"wb");if(!pcm_out)return 1;unsigned short cw=0x027f;__asm__ volatile("fldcw %0"::"m"(cw));
 bool fixed=!strcmp(argv[1],"fixed")||!strcmp(argv[1],"worker-fixed"),bad=!strcmp(argv[2],"bad");
 file_mode=!strcmp(argv[2],"read-io")?1:!strcmp(argv[2],"close-io")?2:0;owned_name=argv[3];
 alignas(ObservedReader) unsigned char storage[sizeof(ObservedReader)]{};auto*r=new(storage)ObservedReader;sReader=r;
 // Unopened and failed-open remain failures; close/reset must never inherit a prior EOF state.
 BYTE p[4];UINT got=7;Check(FAILED(r->Read(4,p,&got))&&got==0,"unopened read failure");
 Check(FAILED(r->Open((char*)"/this/authored/path/does/not/exist")),"failed open");got=7;Check(FAILED(r->Read(4,p,&got))&&got==0,"failed-open read failure");Check(FAILED(r->Open(argv[5]))&&!r->HasFormat(),"malformed initial header open failure");got=7;Check(FAILED(r->Read(4,p,&got))&&got==0,"header-open failure is not EOF");
 Check(r->Open(argv[6])==S_OK&&!r->HasFormat(),"empty stream completes normally");got=7;Check(r->Read(4,p,&got)==(fixed&&strcmp(argv[1],"worker-fixed")?S_FALSE:E_FAIL)&&got==0,"empty stream completion status");r->Close();records.clear();
 snprintf(sTrackName,sizeof sTrackName,"%s",argv[3]);Check(MusicStreamHandle(0)==0&&buffer.playing,"actual worker initial playback");
 unsigned event=0;while(!MUSIC_STREAM_FINISHED&&event<512){MusicStreamHandle(1+(event++%2));if(bad&&!records.back().got)break;}
 if(bad){Check(errors==1&&records.back().status==E_FAIL&&records.back().before&&records.back().after,"actual malformed chain is decoder failure with live file");Check(!MUSIC_STREAM_FINISHED&&!eofdrained,"decode error not clean EOF proof");}
 else if(file_mode) {
  fprintf(stderr,"FILE_FAILURE mode=%d ferror=%d fclose_result=%d read_failed=%u short_reads=%u eof_drained=%u final_state=%d\n",file_mode,file_error_seen,close_result_seen,errors,shorts,eofdrained,r->mState);
  Check(file_injected&&close_injections==1&&saved_fd==-1,"one owned FILE fault injected and retired");
  Check((file_mode==1&&file_error_seen&&close_result_seen==0)||(file_mode==2&&!file_error_seen&&close_result_seen==EOF),"real read and close failures isolated");
  Check(MUSIC_STREAM_FINISHED&&eofdrained==1&&stops==1,"FILE failure retains native drain decision");
  Check(errors==2,"FILE failure cannot become successful EOF drains");
  auto n=records.size();Check(n>=3&&records[n-3].status==S_OK&&!records[n-3].after,"terminal short read retains native first HRESULT");
  Check(records[n-2].status==E_FAIL&&records[n-1].status==E_FAIL&&!records[n-2].got&&!records[n-1].got,"FILE failure retains both failed zero-byte drain reads");
 }
 else {
  Check(MUSIC_STREAM_FINISHED&&eofdrained==1&&stops==1&&shorts==3,"actual worker three-half drain");Check(errors==(fixed?0u:2u),"clean EOF classification");
  Check(records.size()>=3,"actual reads retained");auto n=records.size();Check(records[n-3].status==S_OK&&records[n-3].got>0&&records[n-3].got<STREAM_HALF&&!records[n-3].after,"clean final short read closes source");
  Check(records[n-2].got==0&&records[n-1].got==0&&records[n-2].status==(fixed?S_FALSE:E_FAIL)&&records[n-1].status==(fixed?S_FALSE:E_FAIL),"two drain reads preserve bytes but distinguish EOF");
  Check(FAILED(r->Open(argv[5])),"decoder-open failure clears EOF");got=7;Check(FAILED(r->Read(4,p,&got))&&got==0,"decoder-open failure remains error after EOF");
  Check(FAILED(r->Open((char*)"/this/authored/path/does/not/exist")),"failed reopen clears EOF");got=7;Check(FAILED(r->Read(4,p,&got))&&got==0,"failed reopen read remains failure");
  Check(r->Close()==0,"close EOF reader");got=7;Check(FAILED(r->Read(4,p,&got))&&got==0,"explicit close clears EOF status");
  Check(r->Open(argv[3])==0&&r->HasFormat(),"reopen EOF reader");got=0;Check(r->Read(4,p,&got)==S_OK&&got==4,"reopened reader returns first frame");
  Check(r->Close()==S_OK&&r->Close()==S_OK,"active and repeated close");got=7;Check(FAILED(r->Read(4,p,&got))&&got==0,"active close is not EOF");
  Check(MusicStreamHandle(0)==0&&buffer.playing&&!MUSIC_STREAM_FINISHED&&sSilentHalves==0,"actual worker restart resets EOF/drain");
 }
 printf("{\"variant\":\"%s\",\"case\":\"%s\",\"file_error\":%d,\"close_result\":%d,\"reader_size\":%u,\"checks\":%u,\"refills\":%u,\"short_reads\":%u,\"read_failed\":%u,\"eof_drained\":%u,\"reads\":[",argv[1],argv[2],file_error_seen,close_result_seen,(unsigned)sizeof(COggFileRead),checks,refills,shorts,errors,eofdrained);
 for(size_t i=0;i<records.size();++i){auto&x=records[i];printf("%s{\"requested\":%u,\"got\":%u,\"status\":%d,\"format_before\":%d,\"format_after\":%d,\"state\":%d}",i?",":"",x.requested,x.got,x.status,x.before,x.after,x.state);}puts("]}");
 printf("{\"pcm_bytes\":%llu,\"pcm_fnv64\":\"%016llx\"}\n",pcm_bytes,pcm_hash);fclose(pcm_out);r->Close();r->~ObservedReader();return 0;
}
