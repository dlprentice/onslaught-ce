// SPDX-License-Identifier: GPL-3.0-only
// Authored PCM and controlled SDL/events/reader scheduling; actual mixer and complete music worker source.
// SPDX-License-Identifier: GPL-3.0-only
// The complete production DirectSound mixer with controlled SDL boundaries. No device, game data or real timer.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <cstdarg>
#ifdef AUDIO_REFILL_OVERLAP
static void FixturePublishBoundary();
#endif
#ifdef AUDIO_REFILL_ORDER_TEST
static unsigned fixtureSnapshotCalls, fixtureOrderCalls;
static void FixtureWaitFailedBoundary();
#endif
#include MIXER_SOURCE

struct SDL_Mutex { std::recursive_mutex value; };
struct SDL_AudioStream { bool resumed = false; };
struct Timer { SDL_TimerID id; SDL_TimerCallback callback; void *data; bool canceled; };
static std::vector<Timer> timers;
static SDL_TimerID nextTimer = 1;
static std::atomic<Uint64> realNow{1000000000ull}, virtualNow{1000000000ull};
static bool virtualMode, resumeFails, initFails, openFails;
// Optional authored track identities for the playlist/actual-stream transition fixture. Other tests keep the
// original evolving half-buffer pattern and never inspect real audio data.
static bool namedMusicReads;
static bool diagnosticsEnabled=true;static unsigned clockReads,refillLines,riskLines;static unsigned firstRiskSeq;
#ifdef AUDIO_COUNTERS
static uint64_t readDelayNs;
static unsigned obsValues[PORT_SESSION_OBSERVATIONS], countedRefills, secondVoiceLines;
#endif
#ifdef AUDIO_REFILL_ORDER_TEST
static unsigned fixtureOrderLines;
static std::vector<std::string> fixtureOrderMessages;
static bool fixtureFailNextRead, fixtureWaitFailOnce;
#endif
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
#ifdef AUDIO_REFILL_OVERLAP
static std::recursive_mutex fixtureStreamGuard;
static thread_local unsigned fixtureGuardDepth;
static std::atomic<unsigned> fixtureOrderViolations{0};
static std::atomic<bool> fixtureShutdownAttempted{false};
static std::atomic<bool> fixtureManualWorkerDone{true};
static std::atomic<bool> fixtureCallbackLockAttempted{false};
static bool fixtureExpectStreamGuard;
static int fixtureFramesRemaining = -1;
static unsigned fixtureReaderCloses;
#endif
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
#ifdef AUDIO_REFILL_OVERLAP
	if (role == 5 && fixtureExpectStreamGuard && mutex == gMixer.lock && fixtureGuardDepth == 0) ++fixtureOrderViolations;
	if (role == 4 && mutex == gMixer.lock) fixtureCallbackLockAttempted = true;
#endif
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
#ifdef AUDIO_REFILL_REAL_LOGGER
extern "C" Uint64 SDLCALL SDL_GetTicks() { if (mediaInCallback) ++mediaCallbackLogCalls; return realNow.load() / 1000000; }
#endif
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
bool PortCeLayer();
#ifdef AUDIO_REFILL_REAL_LOGGER
#include "session-logger.inc"
#else
static int gEnabled=-1;
#include "admission.inc"
bool PortDevLog() { return false; }
void PortSessionLog(PortSessionCategory category, bool error, const char *format, ...) {
#ifdef AUDIO_REFILL_ORDER_TEST
 if(category==PORT_SESSION_MUSIC&&!strncmp(format,"refill order anomaly",20))++fixtureOrderCalls;
#endif
 if(!diagnosticsEnabled)return;char line[1024];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);
#ifdef AUDIO_REFILL_ORDER_TEST
 if(category==PORT_SESSION_MUSIC&&!strncmp(line,"refill order anomaly",20)){++fixtureOrderLines;fixtureOrderMessages.emplace_back(line);}
#endif
 if(category==PORT_SESSION_AUDIO&&!strncmp(line,"media ",6)){mediaLines.emplace_back(line);if(mediaInCallback)++mediaCallbackLogCalls;}
#ifdef AUDIO_COUNTERS
 if(category==PORT_SESSION_MUSIC&&error&&!strncmp(line,"a second music-tagged buffer started",36))++secondVoiceLines;
#endif
 if(category==PORT_SESSION_MUSIC&&!strncmp(line,"refill seq=",11)) {++refillLines;if(error){++riskLines;if(!firstRiskSeq)sscanf(line,"refill seq=%u",&firstRiskSeq);}fprintf(stderr,"%s\n",line);}
}
void PortSessionAsset(PortSessionCategory, const char *, const char *, long) {}
void PortSessionMediaActive(PortSessionCategory, const void *, bool) {}
void PortSessionSetFinalMedia(const PortSessionFinalMedia *) {}
#ifdef AUDIO_COUNTERS
// Patch 0187's controls: the same arithmetic as portlog.cpp (adds saturate, maxima keep the largest), recorded where the test can read them.
void PortSessionCount(PortSessionCounter c) { if (diagnosticsEnabled && c == PORT_COUNT_MUSIC_REFILLS) ++countedRefills; }
void PortSessionObserve(PortSessionObservation w, unsigned n) { if (!diagnosticsEnabled || w < 0 || w >= PORT_SESSION_OBSERVATIONS) return; obsValues[w] = ~0u - obsValues[w] < n ? ~0u : obsValues[w] + n; }
void PortSessionObserveMax(PortSessionObservation w, unsigned v) { if (!diagnosticsEnabled || w < 0 || w >= PORT_SESSION_OBSERVATIONS) return; if (v > obsValues[w]) obsValues[w] = v; }
#else
void PortSessionCount(PortSessionCounter) {}
void PortSessionObserve(PortSessionObservation, unsigned) {}
void PortSessionObserveMax(PortSessionObservation, unsigned) {}
#endif
void PortSessionAudioError(const char *) {}
void PortSessionSummary() {}
void PortSessionPulse(unsigned) {}
#endif

#include <strings.h>
#define _tcsicmp strcasecmp
#define _tcsncpy strncpy
#define ZeroMemory(p,n) memset(p,0,n)
#ifdef AUDIO_REFILL_OVERLAP
struct EventBoundary { std::atomic<bool> signaled; explicit EventBoundary(bool state) : signaled(state) {} };
#else
struct EventBoundary {bool signaled=false;};
#endif
static std::vector<EventBoundary*> eventOwners;
static BOOL SignalEvent(HANDLE h){((EventBoundary*)h)->signaled=true;return TRUE;}
extern "C" HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES,BOOL,BOOL state,LPCSTR){auto e=new EventBoundary{bool(state)};eventOwners.push_back(e);return (HANDLE)e;}
extern "C" BOOL WINAPI ResetEvent(HANDLE h){for(auto e:eventOwners)if((HANDLE)e==h){e->signaled=false;return TRUE;}return FALSE;}
extern "C" DWORD WINAPI WaitForMultipleObjects(DWORD count,const HANDLE *events,BOOL,DWORD){
#ifdef AUDIO_REFILL_ORDER_TEST
 if(fixtureWaitFailOnce){fixtureWaitFailOnce=false;return WAIT_FAILED;}
#endif
 for(DWORD i=0;i<count;++i)if(((EventBoundary*)events[i])->signaled){((EventBoundary*)events[i])->signaled=false;return i;}return WAIT_TIMEOUT;}
extern "C" HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD){return (HANDLE)1;}
extern "C" BOOL WINAPI GetExitCodeThread(HANDLE,LPDWORD code){
#ifdef AUDIO_REFILL_OVERLAP
 *code=fixtureManualWorkerDone ? 0 : STILL_ACTIVE;
#else
 *code=0;
#endif
 return TRUE;}
extern "C" BOOL WINAPI CloseHandle(HANDLE){return TRUE;}
extern "C" void WINAPI Sleep(DWORD delay){
#ifdef AUDIO_REFILL_ORDER_TEST
 if(delay==100)FixtureWaitFailedBoundary();
#endif
#ifdef AUDIO_REFILL_OVERLAP
 std::this_thread::yield();
#endif
}
extern "C" DWORD WINAPI GetLastError(){return 0;}
extern "C" void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION){}
extern "C" void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION){}
#ifdef AUDIO_REFILL_OVERLAP
extern "C" void WINAPI EnterCriticalSection(LPCRITICAL_SECTION){if(lockDepth)++fixtureOrderViolations;if(role==6)fixtureShutdownAttempted=true;fixtureStreamGuard.lock();++fixtureGuardDepth;}
extern "C" void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION){--fixtureGuardDepth;fixtureStreamGuard.unlock();}
#else
extern "C" void WINAPI EnterCriticalSection(LPCRITICAL_SECTION){}
extern "C" void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION){}
#endif
#include "ce-admission.inc"
static IDirectSound8 *device;
struct SoundBoundary {IDirectSound8 *GetDirectSound(){return device;}} SOUND;
#ifdef AUDIO_REFILL_OVERLAP
static void RefillWriteBoundary(unsigned index);
#endif
struct CBaseWaveFileRead {
 unsigned frame=0;short marker=1000;
 virtual ~CBaseWaveFileRead()=default;void Close(){
#ifdef AUDIO_REFILL_OVERLAP
 ++fixtureReaderCloses;
#endif
 }void Open(char*name){frame=0;if(namedMusicReads)marker=strstr(name,"a.ogg")?2000:strstr(name,"b.ogg")?3000:1000;}bool HasFormat(){return true;}
 DWORD GetChannels(){return 2;}DWORD GetSampleRate(){return 44100;}
 HRESULT Read(UINT size,BYTE *out,UINT *got){
#ifdef AUDIO_COUNTERS
 realNow+=readDelayNs;
#endif
#ifdef AUDIO_REFILL_ORDER_TEST
 if(fixtureFailNextRead){fixtureFailNextRead=false;*got=0;return E_FAIL;}
#endif
 unsigned frames=size/4;
#ifdef AUDIO_REFILL_OVERLAP
 if(fixtureFramesRemaining>=0){frames=std::min(frames,(unsigned)fixtureFramesRemaining);fixtureFramesRemaining-=frames;}
#endif
 for(unsigned i=0;i<frames;++i){short v=namedMusicReads?marker:1000+1000*(frame/65536);++frame;memcpy(out+i*4,&v,2);
#ifdef AUDIO_REFILL_OVERLAP
 RefillWriteBoundary(i);
#endif
 memcpy(out+i*4+2,&v,2);}
#ifdef AUDIO_REFILL_OVERLAP
 *got=frames*4;
#else
 *got=size;
#endif
 return 0;}
};
using COggFileRead=CBaseWaveFileRead;
#include "music.inc"
static unsigned Dispatch(){HANDLE events[4]={sPlayEvent,sFirstHalfEvent,sSecondHalfEvent,sQuitEvent};unsigned count=0;
 for(;;){DWORD next=WaitForMultipleObjects(4,events,FALSE,0);if(next>3)break;MusicStreamHandle(next);++count;}return count;}
static std::vector<short> MixFrames(unsigned frames){std::vector<short> pcm(frames*2);SDL_LockMutex(gMixer.lock);gMixer.Mix(pcm.data(),frames); realNow+=((uint64_t)frames*1000000000ull/44100);SDL_UnlockMutex(gMixer.lock);return pcm;}
#ifdef AUDIO_COUNTERS
// Patch 0187's controls: the real mixer and the complete music worker, authored PCM and scheduling, the observation API recorded. Modes (c-*):
//  c-normal      the worker keeps up: nothing is flagged, one voice starts
//  c-starved     the worker is held back for 3.1 s once: one half is entered again without a refill (a stale replay) and the late refill is flagged
//  c-double      the 0.1.10 start (the options load starts the front end's track, then the music is initialised again) with the first stream's worker still
//                handling the first buffer's events: two refills per boundary on the second buffer. With patch 0113 the second initialisation shuts the first
//                stream down and this control is clean; without it (the predecessor) it is the defect.
//  c-double-lag  the double start with the first stream's worker 210 ms behind (the 218 ms of the player's log); c-double-late: 610 ms behind
//  c-prime       the second half is written again right after the play (before the cursor has entered it)
//  c-restart     the track is stopped and started again after 100000 frames
//  c-slowread    every read takes 3 ms of virtual time
//  *-limit       the same for 150 times 65536 frames: the lines stop at their budgets, the counters do not
static unsigned DispatchOld(const HANDLE *events){unsigned count=0;for(;;){DWORD next=WaitForMultipleObjects(4,events,FALSE,0);if(next>3)break;MusicStreamHandle(next);++count;}return count;}
static int CountersMain(const char *mode){
 diagnosticsEnabled=PortSessionEnabled();device=Make<Sound>();InitMusicStream();char first[]="authored sequence";PlayMusicStream(first);Check(Dispatch()==1,"actual play wake admitted");
 const bool stalled=strstr(mode,"starved"),twice=strstr(mode,"double"),limit=strstr(mode,"limit"),prime=strstr(mode,"prime"),restart=strstr(mode,"restart");if(strstr(mode,"slowread"))readDelayNs=3000000;
 const unsigned lagBlocks=strstr(mode,"-lag")?21:strstr(mode,"-late")?61:0;
 HANDLE oldEvents[4]={sPlayEvent,sFirstHalfEvent,sSecondHalfEvent,sQuitEvent};
 if(twice){InitMusicStream();char second[]="authored sequence";PlayMusicStream(second);Check(Dispatch()==1,"second play wake admitted");}
 if(prime){void *p1=nullptr,*p2=nullptr;DWORD n1=0,n2=0;Check(SUCCEEDED(sBuffer->Lock(262144,262144,&p1,&n1,&p2,&n2,0)),"prime lock");sBuffer->Unlock(p1,n1,p2,n2);}
 unsigned total=0,dispatches=0,oldDispatches=0,mismatch=0;uint64_t fnv=14695981039346656037ull;const unsigned target=limit?65536*150u:262144u;
 auto consume=[&](unsigned n){auto pcm=MixFrames(n);for(unsigned i=0;i<n;++i){short input=1000+1000*((total+i)/65536);short want=(short)lrintf((float)input*(32767.f/32768.f));if(pcm[i*2]!=want)++mismatch;fnv=(fnv^(uint16_t)pcm[i*2])*1099511628211ull;fnv=(fnv^(uint16_t)pcm[i*2+1])*1099511628211ull;}total+=n;};
 if(stalled){consume(131072+4410);dispatches+=Dispatch();}
 std::deque<std::pair<unsigned,DWORD>> oldQueue;unsigned block=0;bool restarted=false;
 while(total<target){consume(std::min(441u,target-total));dispatches+=Dispatch();++block;
  if(twice){for(;;){DWORD next=WaitForMultipleObjects(4,oldEvents,FALSE,0);if(next>3)break;oldQueue.push_back({block,next});}
   while(!oldQueue.empty()&&block-oldQueue.front().first>=lagBlocks){MusicStreamHandle(oldQueue.front().second);++oldDispatches;oldQueue.pop_front();}}
  if(restart&&!restarted&&total>=100000){restarted=true;StopMusicStream();char again[]="authored sequence";PlayMusicStream(again);dispatches+=Dispatch();}}
 printf("{\"case\":\"%s\",\"checks\":%u,\"frames\":%u,\"mismatched_frames\":%u,\"refill_dispatches\":%u,\"old_dispatches\":%u,\"voices\":%d,\"refill_lines\":%u,\"risk_lines\":%u,\"counted_refills\":%u,\"second_voice_lines\":%u,\"output_fnv\":\"%016llx\",\"clock_reads\":%u,\"obs\":[",
  mode,checks,total,mismatch,dispatches,oldDispatches,gMixer.numVoices,refillLines,riskLines,countedRefills,secondVoiceLines,(unsigned long long)fnv,clockReads);
 for(int i=0;i<PORT_SESSION_OBSERVATIONS;++i)printf("%s%u",i?",":"",obsValues[i]);printf("]}\n");
 ShutdownMusicStream();device->Release();return 0;}
#endif
int main(int argc,char **argv){
#ifdef AUDIO_COUNTERS
 if(argc==2&&!strncmp(argv[1],"c-",2))return CountersMain(argv[1]);
#endif
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
