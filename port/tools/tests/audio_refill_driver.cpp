// SPDX-License-Identifier: GPL-3.0-only
// Authored PCM and controlled SDL/events/reader scheduling; actual mixer and complete music worker source.
// SPDX-License-Identifier: GPL-3.0-only
// The complete production DirectSound mixer with controlled SDL boundaries. No device, game data or real timer.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <cstdarg>
#include MIXER_SOURCE

struct SDL_Mutex { std::recursive_mutex value; };
struct SDL_AudioStream { bool resumed = false; };
struct Timer { SDL_TimerID id; SDL_TimerCallback callback; void *data; bool canceled; };
static std::vector<Timer> timers;
static SDL_TimerID nextTimer = 1;
static std::atomic<Uint64> realNow{1000000000ull}, virtualNow{1000000000ull};
static bool virtualMode, resumeFails, initFails, openFails;
static bool diagnosticsEnabled=true;static unsigned clockReads,refillLines,riskLines;static unsigned firstRiskSeq;
static unsigned opens, destroys, audioInits, audioQuits, checks, notifications, submissions;
static std::vector<Sint16> submitted;
static int putLimit;
static bool hitLimit;
static bool mediaQueueModel, mediaQueueError, mediaInCallback;
static unsigned mediaQueueCalls, mediaCallbackLogCalls;
static std::vector<Sint16> mediaPending;
static std::vector<std::string> mediaLines;
static void (*mediaQueueHook)() = nullptr;
struct MixLimit {};
static thread_local int role, lockDepth;
static std::mutex gateMutex;
static std::condition_variable gateCv;
static bool holdBeforeLock, atLock, releaseLock;
static bool dispatchDuringAdd;
static bool holdAllocate, atAllocate, releaseAllocate, stopAtLock;
static std::thread earlyDispatch;
static Uint32 earlyResult;
static bool earlyDone;

static void Check(bool value, const char *why)
{
	++checks;
	if (!value) { std::fprintf(stderr, "FAIL %s\n", why); std::exit(2); }
}
static void AwaitLock()
{
	std::unique_lock<std::mutex> lock(gateMutex);
	Check(gateCv.wait_for(lock, std::chrono::seconds(3), [] { return atLock; }), "callback reached lock boundary");
}
static void ReleaseLockBoundary()
{
	std::lock_guard<std::mutex> lock(gateMutex);
	releaseLock = true;
	gateCv.notify_all();
}
extern "C" SDL_Mutex *SDLCALL SDL_CreateMutex() { return new SDL_Mutex; }
extern "C" void SDLCALL SDL_LockMutex(SDL_Mutex *mutex)
{
	if (role == 3)
	{
		std::lock_guard<std::mutex> lock(gateMutex);
		stopAtLock = true; gateCv.notify_all();
	}
	if (role == 2)
	{
		std::lock_guard<std::mutex> lock(gateMutex);
		atLock = true; gateCv.notify_all();
	}
	if (role == 1 && holdBeforeLock)
	{
		std::unique_lock<std::mutex> lock(gateMutex);
		atLock = true; gateCv.notify_all();
		gateCv.wait(lock, [] { return releaseLock; });
	}
	mutex->value.lock(); ++lockDepth;
}
extern "C" void SDLCALL SDL_UnlockMutex(SDL_Mutex *mutex) { --lockDepth; mutex->value.unlock(); }
extern "C" Uint64 SDLCALL SDL_GetTicksNS() { ++clockReads; return realNow.load(); }
extern "C" void *SDLCALL SDL_malloc(size_t bytes)
{
	if (role == 1 && holdAllocate)
	{
		std::unique_lock<std::mutex> lock(gateMutex);
		atAllocate = true; gateCv.notify_all();
		gateCv.wait(lock, [] { return releaseAllocate; });
	}
	return std::malloc(bytes);
}
extern "C" void SDLCALL SDL_free(void *p) { std::free(p); }
extern "C" const char *SDLCALL SDL_getenv(const char *name) { return std::getenv(name); }
extern "C" int SDLCALL SDL_strcmp(const char *a, const char *b) { return std::strcmp(a, b); }
extern "C" const char *SDLCALL SDL_GetError() { return "controlled failure"; }
extern "C" bool SDLCALL SDL_InitSubSystem(SDL_InitFlags) { ++audioInits; return !initFails; }
extern "C" void SDLCALL SDL_QuitSubSystem(SDL_InitFlags) { ++audioQuits; }
extern "C" const char *SDLCALL SDL_GetCurrentAudioDriver() { return "controlled"; }
extern "C" SDL_AudioStream *SDLCALL SDL_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *)
{
	++opens; return openFails ? nullptr : new SDL_AudioStream;
}
extern "C" bool SDLCALL SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream)
{
	if (resumeFails) return false;
	stream->resumed = true; return true;
}
extern "C" void SDLCALL SDL_DestroyAudioStream(SDL_AudioStream *stream) { ++destroys; if(mediaQueueModel)mediaPending.clear(); delete stream; }
extern "C" SDL_AudioDeviceID SDLCALL SDL_GetAudioStreamDevice(SDL_AudioStream *) { return 7; }
extern "C" bool SDLCALL SDL_GetAudioDeviceFormat(SDL_AudioDeviceID, SDL_AudioSpec *spec, int *frames)
{
	*spec = {SDL_AUDIO_S16LE, 2, 44100}; *frames = 441; return true;
}
extern "C" const char *SDLCALL SDL_GetAudioDeviceName(SDL_AudioDeviceID) { return "controlled device"; }
extern "C" int SDLCALL SDL_GetAudioStreamQueued(SDL_AudioStream *) {
 ++mediaQueueCalls;if(mediaQueueHook)mediaQueueHook();
 return mediaQueueError ? -1 : mediaQueueModel ? (int)mediaPending.size()*2 : 12000;
}
extern "C" bool SDLCALL SDL_ClearAudioStream(SDL_AudioStream *) { return true; }
extern "C" bool SDLCALL SDL_PutAudioStreamData(SDL_AudioStream *, const void *pcm, int bytes)
{
	++submissions;
	if (putLimit && submissions >= (unsigned)putLimit) throw MixLimit{};
	submitted.assign((const Sint16 *)pcm, (const Sint16 *)pcm + bytes / 2);
 if(mediaQueueModel)mediaPending.insert(mediaPending.end(),submitted.begin(),submitted.end());
	return true;
}
extern "C" SDL_TimerID SDLCALL SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *data)
{
	SDL_TimerID id = nextTimer++;
	timers.push_back({id, callback, data, false});
	if (dispatchDuringAdd)
	{
		dispatchDuringAdd = false;
		realNow += 10000000;
		earlyDispatch = std::thread([=] {
			role = 2; earlyResult = callback(data, id, interval);
			std::lock_guard<std::mutex> lock(gateMutex); earlyDone = true; gateCv.notify_all();
		});
		AwaitLock();
		if (!lockDepth)
		{
			std::unique_lock<std::mutex> lock(gateMutex);
			Check(gateCv.wait_for(lock, std::chrono::seconds(3), [] { return earlyDone; }), "unlocked publication dispatch returned");
		}
	}
	return id;
}
extern "C" bool SDLCALL SDL_RemoveTimer(SDL_TimerID id)
{
	// Match SDL 3.4's cancellation: an already-dispatched callback is allowed to finish after this returns.
	for (auto &timer : timers) if (timer.id == id) { timer.canceled = true; return true; }
	return false;
}
static BOOL SignalEvent(HANDLE);
extern "C" BOOL WINAPI SetEvent(HANDLE h) { ++notifications; return SignalEvent(h); }
unsigned long long PortClockFrames() { return 0; }
bool PortClockVirtual() { return virtualMode; }
unsigned long long PortClockPeekNs() { return virtualNow.load(); }
void PortAudioGuardCheck() {}
static int gEnabled=-1;bool PortCeLayer();
#include "admission.inc"
bool PortDevLog() { return false; }
void PortSessionLog(PortSessionCategory category, bool error, const char *format, ...) {
 if(!diagnosticsEnabled)return;char line[1024];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);
 if(category==PORT_SESSION_AUDIO&&!strncmp(line,"media ",6)){mediaLines.emplace_back(line);if(mediaInCallback)++mediaCallbackLogCalls;}
 if(category==PORT_SESSION_MUSIC&&!strncmp(line,"refill seq=",11)) {++refillLines;if(error){++riskLines;if(!firstRiskSeq)sscanf(line,"refill seq=%u",&firstRiskSeq);}fprintf(stderr,"%s\n",line);}
}
void PortSessionAsset(PortSessionCategory, const char *, const char *, long) {}
void PortSessionMediaActive(PortSessionCategory, const void *, bool) {}
void PortSessionCount(PortSessionCounter) {}
void PortSessionAudioError(const char *) {}
void PortSessionSummary() {}
void PortSessionPulse(unsigned) {}

#include <strings.h>
#define _tcsicmp strcasecmp
#define _tcsncpy strncpy
#define ZeroMemory(p,n) memset(p,0,n)
struct EventBoundary {bool signaled=false;};static std::vector<EventBoundary*> eventOwners;
static BOOL SignalEvent(HANDLE h){((EventBoundary*)h)->signaled=true;return TRUE;}
extern "C" HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES,BOOL,BOOL state,LPCSTR){auto e=new EventBoundary{bool(state)};eventOwners.push_back(e);return (HANDLE)e;}
extern "C" BOOL WINAPI ResetEvent(HANDLE h){for(auto e:eventOwners)if((HANDLE)e==h){e->signaled=false;return TRUE;}return FALSE;}
extern "C" DWORD WINAPI WaitForMultipleObjects(DWORD count,const HANDLE *events,BOOL,DWORD){for(DWORD i=0;i<count;++i)if(((EventBoundary*)events[i])->signaled){((EventBoundary*)events[i])->signaled=false;return i;}return WAIT_TIMEOUT;}
extern "C" HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD){return (HANDLE)1;}
extern "C" BOOL WINAPI GetExitCodeThread(HANDLE,LPDWORD code){*code=0;return TRUE;}
extern "C" BOOL WINAPI CloseHandle(HANDLE){return TRUE;}
extern "C" void WINAPI Sleep(DWORD){}
extern "C" DWORD WINAPI GetLastError(){return 0;}
extern "C" void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION){}
extern "C" void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION){}
extern "C" void WINAPI EnterCriticalSection(LPCRITICAL_SECTION){}
extern "C" void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION){}
#include "ce-admission.inc"
static IDirectSound8 *device;
struct SoundBoundary {IDirectSound8 *GetDirectSound(){return device;}} SOUND;
struct CBaseWaveFileRead {
 unsigned frame=0;
 virtual ~CBaseWaveFileRead()=default;void Close(){}void Open(char*){frame=0;}bool HasFormat(){return true;}
 DWORD GetChannels(){return 2;}DWORD GetSampleRate(){return 44100;}
 HRESULT Read(UINT size,BYTE *out,UINT *got){for(unsigned i=0;i<size/4;++i){short v=1000+1000*(frame++/65536);memcpy(out+i*4,&v,2);memcpy(out+i*4+2,&v,2);}*got=size;return 0;}
};
using COggFileRead=CBaseWaveFileRead;
#include "music.inc"
static unsigned Dispatch(){HANDLE events[4]={sPlayEvent,sFirstHalfEvent,sSecondHalfEvent,sQuitEvent};unsigned count=0;
 for(;;){DWORD next=WaitForMultipleObjects(4,events,FALSE,0);if(next>3)break;MusicStreamHandle(next);++count;}return count;}
static std::vector<short> MixFrames(unsigned frames){std::vector<short> pcm(frames*2);SDL_LockMutex(gMixer.lock);gMixer.Mix(pcm.data(),frames); realNow+=((uint64_t)frames*1000000000ull/44100);SDL_UnlockMutex(gMixer.lock);return pcm;}
int main(int argc,char **argv){
 if(argc!=2)return 1;diagnosticsEnabled=PortSessionEnabled();device=Make<Sound>();InitMusicStream();char name[]="authored sequence";PlayMusicStream(name);Check(Dispatch()==1,"actual play wake admitted");
 Check(gMixer.numVoices==1,"one actual looping music buffer");unsigned diagnosticClocks=clockReads;unsigned total=0,mismatch=0,first_bad=0,dispatches=0;bool stalled=!strncmp(argv[1],"starved",7);bool limit=strstr(argv[1],"limit");
 auto consume=[&](unsigned n){auto pcm=MixFrames(n);for(unsigned i=0;i<n;++i){short input=1000+1000*((total+i)/65536);short want=(short)lrintf((float)input*(32767.f/32768.f));if(pcm[i*2]!=want){if(!mismatch)first_bad=total+i;++mismatch;}}total+=n;};
 if(stalled){for(unsigned i=0;i<(limit?20u:1u);++i){consume(131072+4410);dispatches+=Dispatch();}}
 while(total<(limit?65536*150u:262144u)){unsigned n=std::min(441u,(limit?65536*150u:262144u)-total);consume(n);dispatches+=Dispatch();}
 Check((mismatch>0)==stalled,"delayed worker control discriminates native stream continuity");
 Check(!stalled||limit||(mismatch==4410&&first_bad==131072),"missed segment is bounded and subsequent refill order recovers");
 Check(gMixer.numVoices==1,"underrun remains single voice, not dual ownership");
 #if defined(CE_VERSION) && !defined(FIXTURE_BASELINE)
 if(diagnosticsEnabled) {Check(refillLines<=std::min(dispatches,144u)&&(!limit?refillLines==dispatches:true),"bounded refill reports actual coupled state");Check(!limit||refillLines==(stalled?144u:128u),"per-session normal and risk log quotas bound output");Check((riskLines>0)==stalled,"ordinary flag diagnoses only forced starvation risk");Check(!stalled||firstRiskSeq==1,"delayed first half is flagged in event order");Check(clockReads>diagnosticClocks,"ordinary mode uses monotonic observations");}
 else {Check(refillLines==0&&clockReads==diagnosticClocks,"muted diagnostics have no clock or log observations");Check(!PortAudioStreamSnapshot(nullptr,nullptr,nullptr,nullptr,nullptr)&&clockReads==diagnosticClocks,"disabled snapshot returns before reading caller fields");}
 #else
 Check(refillLines==0&&clockReads==diagnosticClocks,"baseline/private diagnostics have no clock or log observations");
 #endif
 printf("{\"case\":\"%s\",\"checks\":%u,\"frames\":%u,\"mismatched_frames\":%u,\"first_bad_frame\":%u,\"refill_dispatches\":%u,\"voices\":%d,\"reader_frames\":%u,\"refill_lines\":%u,\"risk_lines\":%u,\"diagnostic_clock_reads\":%u}\n",argv[1],checks,total,mismatch,first_bad,dispatches,gMixer.numVoices,((COggFileRead*)sReader)->frame,refillLines,riskLines,clockReads-diagnosticClocks);
 ShutdownMusicStream();device->Release();return 0;
}
