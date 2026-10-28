// SPDX-License-Identifier: GPL-3.0-only
// Actual mixer, complete music worker, and Bink buffer lifecycle; controlled decoder/SDL endpoints only.
#define main ExistingRefillMain
#include "audio_refill_driver.cpp"
#undef main
#include <climits>

#ifdef AUDIO_MEDIA_BASELINE
bool PortAudioMediaRole(const void *, PortSessionCategory) { return false; }
#endif
using U32 = uint32_t;
using S32 = int32_t;
using HBINK = struct Video *;
constexpr int AVMEDIA_TYPE_AUDIO = 1;
struct Codec { int codec_type = AVMEDIA_TYPE_AUDIO; };
struct AVStream { Codec *codecpar = nullptr; };
struct AVPacket {};
struct AVFrame {};
struct AVCodecContext {};
struct AVIOContext { void *buffer = nullptr; };
struct AVFormatContext { unsigned nb_streams = 0; AVStream **streams = nullptr; AVIOContext *pb = nullptr; };
struct Video {
 struct { U32 FrameNum=1; } pub;
 unsigned sessionId=1;char sessionAsset[121]="authored.vid";
 AVFormatContext *fmt=nullptr;std::vector<AVPacket*> packets;AVFrame*frame=nullptr;AVCodecContext*dec=nullptr;
 IDirectSoundBuffer *sound=nullptr;DWORD soundRate=0,soundBlock=0;bool started=false;LARGE_INTEGER start{};U32 played=0;
};
static IDirectSound8 *gSound;
static U32 gTrack;
static bool gTrackSet;
static unsigned decodedTrack=UINT_MAX,decodeCalls,filmFrames=64;
static bool decodeFails;
static bool DecodeAudio(Video*,int stream,std::vector<short>&pcm,int*rate,int*channels){
 ++decodeCalls;decodedTrack=(unsigned)stream;*rate=44100;*channels=2;pcm.assign(filmFrames*2,2000);return !decodeFails;
}
static bool DecodeFrame(Video*,U32){return false;}
static void av_packet_free(AVPacket**){}
static void av_frame_free(AVFrame**){}
static void avcodec_free_context(AVCodecContext**){}
static void av_freep(void**){}
static void avio_context_free(AVIOContext**){}
static void avformat_close_input(AVFormatContext**p){if(*p){for(unsigned i=0;i<(*p)->nb_streams;++i){delete(*p)->streams[i]->codecpar;delete(*p)->streams[i];}delete[](*p)->streams;delete*p;*p=nullptr;}}
extern "C" BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER*p){p->QuadPart=realNow;return TRUE;}
#include "bink-owner.inc"
static Video *Film(unsigned tracks=1){
 auto v=new(std::malloc(sizeof(Video)))Video;v->fmt=new AVFormatContext;v->fmt->nb_streams=tracks;v->fmt->streams=new AVStream*[tracks];
 for(unsigned i=0;i<tracks;++i)v->fmt->streams[i]=new AVStream{new Codec};
 MakeSound(v);BinkDoFrame(v);return v;
}
static void StartMusic(){InitMusicStream();char track[]="authored.ogg";PlayMusicStream(track);Check(Dispatch()==1,"actual worker admits one start");}
static std::vector<short> captured;
static void FeedFrames(unsigned n){
 mediaInCallback=true;Feed(nullptr,gMixer.stream,(int)n*4,(int)n*4);mediaInCallback=false;
 captured.insert(captured.end(),submitted.begin(),submitted.end());
}
static void Expect(unsigned start,unsigned n,short value){for(unsigned i=start*2;i<(start+n)*2;++i)Check(captured.at(i)==value,"actual mixed PCM matches authored lifecycle");}
#ifndef AUDIO_MEDIA_BASELINE
static void EpochBoundary(){Check(lockDepth==0,"SDL queue sampled outside mixer mutex");SDL_LockMutex(gMixer.lock);++gMixer.streamGeneration;SDL_UnlockMutex(gMixer.lock);mediaQueueHook=nullptr;}
#endif
#ifndef AUDIO_MEDIA_MAIN
#define AUDIO_MEDIA_MAIN main
#endif
int AUDIO_MEDIA_MAIN(int argc,char**argv){
 if(argc!=2)return 1;const std::string mode=argv[1];diagnosticsEnabled=PortSessionEnabled();mediaQueueModel=true;
 if(mode=="resume-failure")resumeFails=true;
 device=Make<Sound>();gSound=device;
 Video*film=nullptr;bool music=false;
 if(mode=="music"||mode=="overlap"||mode=="short-film"||mode=="muted"||mode=="disabled"||mode=="saturation"){StartMusic();music=true;}
 if(mode=="film"||mode=="overlap"||mode=="short-film"||mode=="tail"||mode=="muted"||mode=="decoder-fail"||mode=="track-selection"){
  if(mode=="short-film")filmFrames=16;if(mode=="decoder-fail")decodeFails=true;
  if(mode=="track-selection"){gTrackSet=true;gTrack=1;}film=Film(mode=="track-selection"?2:1);
  if(mode=="muted")film->sound->SetVolume(DSBVOLUME_MIN);
 }
 if(mode=="resume-failure"){
#ifndef AUDIO_MEDIA_BASELINE
  Check(!gMixer.stream&&!gMixer.mediaStreamActive&&gMixer.timer,"failed resume retires the observation stream before timer fallback");
  Check(!mediaQueueCalls&&gMixer.queueLastBytes==-1,"failed resume does not fabricate a queue observation");
#endif
 }else if(mode=="invalid-tags"){
#ifndef AUDIO_MEDIA_BASELINE
  errno=EDOM;Check(!PortAudioMediaRole(nullptr,PORT_SESSION_MUSIC)&&!PortAudioMediaRole((void*)1,PORT_SESSION_FILM),"foreign/null pointers refused without dereference");Check(errno==EDOM,"tag refusal preserves errno");
  DSBUFFERDESC d{};d.dwSize=sizeof d;d.dwFlags=DSBCAPS_CTRLVOLUME;d.dwBufferBytes=8;auto b=Make<Buffer>(&d,(Data*)nullptr);
  Check(!PortAudioMediaRole(static_cast<IDirectSoundBuffer8*>(b),PORT_SESSION_WINDOW),"non-media category refused");
  Check(PortAudioMediaRole(static_cast<IDirectSoundBuffer8*>(b),PORT_SESSION_MUSIC),"owned stopped buffer accepts role");unsigned serial=b->mediaSerial;
  Check(PortAudioMediaRole(static_cast<IDirectSoundBuffer8*>(b),PORT_SESSION_MUSIC)&&serial==b->mediaSerial,"repeated tag is idempotent");
  Check(!PortAudioMediaRole(static_cast<IDirectSoundBuffer8*>(b),PORT_SESSION_FILM)&&b->mediaKind==PORT_SESSION_MUSIC,"conflicting media owner is refused");
  b->Release();Check(!PortAudioMediaRole(b,PORT_SESSION_FILM),"released buffer no longer in registry");
#endif
 }else if(mode=="queue-fail"||mode=="epoch"){
#ifndef AUDIO_MEDIA_BASELINE
  if(mode=="queue-fail")mediaQueueError=true;else mediaQueueHook=EpochBoundary;
#endif
  FeedFrames(32);Expect(0,32,0);
#ifndef AUDIO_MEDIA_BASELINE
  if(mode=="queue-fail")Check(gMixer.queueLastBytes==-1&&gMixer.queueSamples==0&&gMixer.queueFailures==1,"failed queue observation remains unknown");
  else Check(gMixer.queueSamples==0&&gMixer.retiredQueueObservations==1,"same-pointer newer epoch cannot admit stale queue");
#endif
 }else if(mode=="reopen"){
  FeedFrames(32);
#ifndef AUDIO_MEDIA_BASELINE
  Uint64 previous=gMixer.streamGeneration;Check(gMixer.mediaStreamActive&&gMixer.queueGeneration==previous,"first actual stream queue bound");
#endif
  device->Release();device=Make<Sound>();gSound=device;
#ifndef AUDIO_MEDIA_BASELINE
  Check(gMixer.mediaStreamActive&&gMixer.streamGeneration==previous+2&&gMixer.queueGeneration!=gMixer.streamGeneration,"actual Stop and Start retire and publish stream epochs");
#endif
  FeedFrames(32);
#ifndef AUDIO_MEDIA_BASELINE
  Check(gMixer.queueGeneration==gMixer.streamGeneration&&gMixer.queueSamples==2,"new actual stream admits only its queue sample");
#endif
 }else if(mode=="tail"){
  FeedFrames(32);Free(film);film=nullptr;StartMusic();music=true;FeedFrames(32);Expect(0,32,2000);Expect(32,32,1000);
#ifndef AUDIO_MEDIA_BASELINE
  Check(gMixer.bothRoleFrames==0&&gMixer.filmFrames==32&&gMixer.musicFrames==32,"queued film tail is not same-frame mixed role overlap");Check(mediaPending==captured,"controlled downstream queue retains sequential old/new blocks");Check(gMixer.queueLastBytes==256&&gMixer.queueMaxBytes==256,"queue uses input-byte units");
#endif
 }else if(mode=="reused-buffer"){
#ifndef AUDIO_MEDIA_BASELINE
  WAVEFORMATEX f{};f.wFormatTag=1;f.nChannels=2;f.nSamplesPerSec=44100;f.wBitsPerSample=16;f.nBlockAlign=4;
  DSBUFFERDESC d{};d.dwSize=sizeof d;d.dwFlags=DSBCAPS_CTRLVOLUME;d.dwBufferBytes=64;d.lpwfxFormat=&f;
  alignas(Buffer) unsigned char storage[sizeof(Buffer)];auto b=new(storage)Buffer(&d,nullptr);auto publicPointer=static_cast<IDirectSoundBuffer8*>(b);
  Check(PortAudioMediaRole(publicPointer,PORT_SESSION_FILM),"first owned incarnation tagged");unsigned serial=b->mediaSerial;b->~Buffer();
  b=new(storage)Buffer(&d,nullptr);Check(b->mediaKind==-1&&b->mediaSerial!=serial,"same-address new buffer does not inherit role/serial");b->~Buffer();
#endif
 }else{
#ifndef AUDIO_MEDIA_BASELINE
  if(mode=="saturation")gMixer.musicFrames=gMixer.bothRoleFrames=~Uint64(0)-2;
#endif
  FeedFrames(64);
  if(mode=="music"||mode=="disabled"||mode=="saturation"||mode=="muted")Expect(0,64,1000);
  else if(mode=="overlap")Expect(0,64,3000);
  else if(mode=="short-film"){Expect(0,16,3000);Expect(16,48,1000);}
  else if(mode=="decoder-fail")Expect(0,64,0);
  else Expect(0,64,2000);
#ifndef AUDIO_MEDIA_BASELINE
  if(diagnosticsEnabled){
   unsigned wantMusic=music?64:0,wantFilm=film&&!decodeFails?(mode=="short-film"?16:64):0;
   if(mode=="saturation")Check(gMixer.musicFrames==~Uint64(0),"counter saturates without wrapping");
   else Check(gMixer.musicFrames==wantMusic&&gMixer.filmFrames==wantFilm&&gMixer.bothRoleFrames==std::min(wantMusic,wantFilm),"exact active-role processed frame spans");
   Check(gMixer.callbackFrames==64&&gMixer.callbackMaxFrames==64,"callback demand frames observed");
  }else Check(!gMixer.musicFrames&&!gMixer.filmFrames&&!gMixer.callbackFrames&&!mediaQueueCalls,"excluded diagnostics make no observations");
#endif
 }
 if(mode=="track-selection")Check(decodeCalls==1&&decodedTrack==1,"actual MakeSound selects one requested decoder stream");
 if(film){auto pointer=film->sound;Free(film);
#ifndef AUDIO_MEDIA_BASELINE
  Check(!pointer||!PortAudioMediaRole(pointer,PORT_SESSION_FILM),"actual Free retires film owner and registry");
#endif
 }
 if(music)ShutdownMusicStream();
#ifndef AUDIO_MEDIA_BASELINE
 Check(mediaCallbackLogCalls==0,"callback does not format or log media observations");
#endif
 device->Release();unsigned long long hash=1469598103934665603ull;for(short x:captured){hash^=(unsigned short)x;hash*=1099511628211ull;}
 printf("{\"case\":\"%s\",\"checks\":%u,\"frames\":%zu,\"pcm_hash\":\"%016llx\",\"queue_calls\":%u,\"media_lines\":%zu}\n",mode.c_str(),checks,captured.size()/2,hash,mediaQueueCalls,mediaLines.size());return 0;
}
