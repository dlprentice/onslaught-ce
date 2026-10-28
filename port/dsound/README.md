<!-- SPDX-License-Identifier: GPL-3.0-only -->

# port/dsound: DirectSound 8 over SDL3 audio

Status: active (mixing, notifications and DirectSound 3D in place; nobody has listened to the result yet)
Last updated: 2026-10-28 (playlist transitions; bounded ordinary media-mix observations)
Summary: what the game's sound code needs from DirectSound, how this module provides it, and how it is checked.

Replaces port/platform/common/dsound.cpp (the platform's silent form; `build.py` drops it when this directory
has sources) and keeps its COM shape: the same exports (`DirectSoundCreate8`, `DirectSoundCreate`,
`DirectSoundEnumerateA`), primary buffer to `IDirectSound3DListener`, `DSBCAPS_CTRL3D` buffers to
`IDirectSound3DBuffer`, `DSBCAPS_CTRLPOSITIONNOTIFY` buffers to `IDirectSoundNotify`, duplicates sharing memory.

## What the game uses

`pcsoundmanager.cpp` decodes its ADPCM sample bank into mono buffers (16-bit at 44.1 kHz by default, or the options'
lower rate or 8 bits), plays each event on a `DuplicateSoundBuffer` copy with volume, frequency (pitch variance) and
DirectSound 3D position, velocity and distances, sets the listener once per frame with `DS3D_DEFERRED` and commits it,
and polls `GetStatus` to end events whose sample has finished. `PCMusicStream.cpp` streams Ogg tracks through a
512 KB looping 44.1 kHz stereo buffer from a worker thread that waits on the buffer's two position notifications.
It never reads a play cursor.

## How it works

One mixer per process, fed by SDL3's audio callback (44.1 kHz, 16-bit stereo; SDL converts for the device). A
buffer's play cursor moves only as the mixer consumes it, so `GetStatus` reports a sample stopped, and a notification
fires (`SetEvent`, from the audio thread), when that part of the buffer has been mixed. Each voice is resampled at
its frequency by a 32-tap windowed sinc (Blackman, 256 phases, cut off at 0.90 of the source's Nyquist frequency:
for a 22.05 kHz sample flat to 8 kHz, -0.65 dB at 9 kHz, -6.9 dB at 10 kHz, images -28 dB just above 11 kHz and
-75 dB from 12 kHz up). The sinc runs whenever a voice's step differs from 1, so also for a 44.1 kHz buffer whose
`SetFrequency` changed; only a voice at exactly the mix rate is copied unfiltered. A step above 1 (a buffer played
faster than 44.1 kHz) gets no extra low-pass, so it can alias, which the game's small pitch variance keeps negligible.
Voices are scaled by DirectSound's volume (hundredths of a decibel). DirectSound 3D renders as the
game's "no virtualization" method: rolloff `min / (min + rolloff * (d - min))` beyond the minimum distance, silence
beyond the maximum for `DSBCAPS_MUTE3DATMAXDISTANCE`, an equal-power stereo pan from the listener's frame, and Doppler
from the velocities along the line of sight; `DS3D_DEFERRED` settings wait for `CommitDeferredSettings`. Gains ramp
over each mix to avoid clicks.

`BEA_AUDIO_CALLLOG=file` logs every DirectSound call in the retail replay shim's log format (frames are the port's Presents), so
the port's calls and retail's can be compared after both pass through C's dsnorm.py. `BEA_AUDIO_RECORD=file.wav` writes the final mix (the header is kept valid about once a second, so an interrupted run
leaves a playable file). `BEA_AUDIO_CLOCK=timer` mixes on a 10 ms timer without opening a device, which is also the
fallback when no device opens; offscreen and test runs use it so nothing plays through the desktop's speakers.

## Ordinary session observations

Ordinary CE logs distinguish outstanding media requests from frames actually mixed from explicitly tagged music
and film buffers. A stopped or naturally ended film no longer contributes active-role frames even if its handle
has not closed. These counters include silent or muted buffers: they do not establish audible two-track content.
The shared-role frame count is music plus film; it does not count two music voices separately or observe other
processes. A voice-count snapshot describes only the instant at which it was taken.
Each buffer has a process-local serial; no audio payload, media name, microphone input or user identifier is added.

The callback updates fixed counters for requested mix frames and samples its own SDL input queue outside the mixer
mutex. A stream-lifetime join rejects an observation from a retired or reused stream. Negative queue results stay
unknown. Queue bytes use the 44.1 kHz, stereo S16 input format; converted audio, Windows/device buffering and physical
playback remain unobserved. Play, Stop and device teardown report snapshots through the existing bounded session-log
budget. At device teardown, one fixed-size snapshot is also staged for the next existing summary line, which is not
subject to the normal or error event budgets. It reports cumulative process-local tagged-mixer frame counts and the
last SDL input queue observation, not sound reaching the device or speakers. The launcher still has a fixed log-size
cap, and an abrupt process exit can prevent the final line: an absent snapshot cannot establish that overlap did not
occur. These observations add no callback allocation, formatting or file I/O. The existing callback does allocate
its PCM buffer and can grow mixer scratch storage. Faithful, replay, recording and private routes keep these
observations disabled.

`python3 port/tools/audio_media_tests.py` couples the complete mixer and music worker with the actual Bink
buffer-creation, first-play and cleanup bodies. Authored PCM and controlled SDL/decoder boundaries distinguish
real active-role overlap, an ended-but-unclosed film, a sequential queued tail, mute, stream reuse and failed queue
queries. Before/after PCM equality and causal mutations check the observation layer. This is not a decoder-fidelity,
Windows runtime, device-quality or reporter-reproduction result.

`python3 port/tools/audio_media_race_tests.py` uses ThreadSanitizer and controlled SDL boundaries to interleave a
snapshot with stream teardown. It also checks a mutant that restores the unlocked stream-pointer read.

`python3 port/tools/audio_media_summary_tests.py` couples the actual mixer, worker, Bink owner and session logger.
Authored PCM, exhausted event budgets, muted buffers, disabled diagnostics and maximum counters check that the final
summary survives a full event budget, stays bounded and does not claim audible output. This is not a physical-audio
or Windows-runtime observation.

The CE custom-playlist commands stop the existing music stream before starting a new track. Without that stop,
the music worker opens the new reader but sees its single looping buffer is already playing and does not fill it
from the beginning: previously queued PCM continues before later refills use the new reader. This affected adding a
folder while level music played, `music next`, and clearing the custom list. `ce_music_transition_tests.py` connects
the actual playlist, worker and mixer using distinct authored PCM; its stop-removal controls retain the old audio.
This fixes those transitions, not the reported Windows startup-film overlap, whose cause remains unconfirmed.

## Checks

`tests/run_tests.sh` builds `tests/dsound_test.cpp` for Linux and Windows and runs it on the timer clock, on SDL's
dummy driver (Linux) and under Wine (Windows). It checks what the game reads back: a 0.5 s one-shot stops after
0.50 to 0.53 s, the same at twice the frequency after 0.25 to 0.26 s, a duplicate keeps playing after its source's
release, and the music buffer's notifications alternate half-first every 1.486 s (measured 1.48 to 1.53). Then
`tests/dsound_check.py` measures the recording: a source to the right is heard on the right, one to the left on the
left, one beyond the maximum distance not at all, and a deferred move takes effect at the commit. All pass in all three
configurations.

The timings are asserted on the timer clock; SDL's dummy driver paces on real time, so under machine load its run
asserts only order, alternation and the recording's content (`DSOUND_TEST_ORDER_ONLY`, `--content-only`).

The game: level 100 (`port/tools/run.py NAME --timeout 120 -- -level 100 -skipfmv`, with `BEA_AUDIO_CLOCK=timer`
and `BEA_AUDIO_RECORD`; `BEA_AUDIO_TRACE` logs every Play and `BEA_AUDIO_DUMP=dir` writes each played buffer) records
about 115 s of mix at -17.9 LUFS integrated, -3.2 dBFS true peak, no clipped samples. What the spectrogram shows,
traced to its buffers: the music stream from the start (the 44.1 kHz Ogg track, band-limited near 17 kHz by Vorbis),
whose percussion makes the thin vertical lines that run through it (attacks with decays, not clicks); five looping
ambience samples at 22.05 kHz; and Tatiana's first voice line (a 5 s, 22.05 kHz sample with formant structure and
sibilants to 11 kHz), the bright broadband event about ten seconds in. The first build resampled linearly, which
mirrored that line's speech band up to 22 kHz; the windowed sinc ends it at about 12 kHz. That is measurement, not
listening: nobody has listened to the port's audio yet.

## The port's virtual clock

Under `BEA_PORT_CLOCK` (`run.py --virtual`, `--twice`), no device opens and the mix follows the port's virtual clock.
Every call that reads or changes playback first mixes whole 10 ms blocks up to the virtual time now, read through
`PortClockPeekNs` (which does not count as a clock read). So play and write cursors, `GetStatus`, notifications, and
every volume, position or 3D change fall on the same block in every run. The 10 ms timer only keeps notifications
coming while the game makes no call. The log says `onslaught-ce: audio virtual clock, no device` (`bea-port:` in a
non-public build).

**The record-mode monitor.** With `BEA_AUDIO_MONITOR=1` (the launcher sets it for a recorded session), the virtual
clock's mix is also played: `Mixer::Start` opens SDL's default device as a stream with no callback, and `Mix` pushes
each block it made into it (`SDL_PutAudioStreamData`, return ignored). Data only flows from the mix to the device:
no cursor, status, notification or film clock reads the monitor, it never reads the game's clock, and the device list
and caps the game sees are the same constants. The device starts once about 60 ms is queued; a queue over about
200 ms is dropped (the virtual clock catching up after a stall), and an empty one plays silence. Virtual time stands
still while a level loads, so music gaps there, and films that run one frame per Present can drift from real time.
The log says `audio virtual clock, monitor <driver>`. Tests play it on SDL's `disk` driver (`run.py --audio-monitor
OUT.raw`, with `BEA_PORT_REQUIRE_DUMMY_AUDIO=silent`), so nothing reaches the desktop.

`run.py vclock --twice --shot 1500 -- -level 100 -skipfmv` gives identical frames, with the tutorial's eighth subtitle
and its portrait on screen (2026-10-15).

Voice lines in level 100 (Presents at which each line's Play and the game's Stop fall):

- **The present clock** (`--virtual`, which also counts the main thread's sleeps): starts 190, 366, 581, 770, 940, 1012,
  1234, 1400; the same in every run.
- **The retail shim clock** (`--shim-clock`): starts 190/366/582/772/943/1015/1237/1403 and stops 342/558/748/919/991/
  1213/1379/1523. These are exactly retail's when C paces retail's DirectSound with this model under the same clock
  (C's harness golden-640/level100, two retail processes).

The present-clock list is one Present earlier three times. That comes from the clock (the sleeps it counts move the
block phase), not from the mix.

## Compared with retail

The retail replay shim logged every DirectSound call retail BEA.exe makes in level 100 under Wine (3,600 Presents, virtual clock;
a retained private capture). The port's `BEA_AUDIO_CALLLOG` over the same 3,600
Presents, both through C's dsnorm.py:

- The first 11 Plays agree: same frame (3), same order, same flags, sizes and peaks, and identical content hashes for
  all nine looping ambience samples (ADPCM from the sample bank). The music stream's first fill and the voice lines
  differ in content but not in size or peak: both are Ogg, decoded by libvorbis 1.3.7 in the port and by retail's
  2003 vorbis.dll.
- Retail's timings are not a reference for the mixing clock: its Wine device was a null ALSA output that consumes
  audio far faster than real time (the 512 KB music buffer refilled 11 times within frame 4; a 5 s voice line reported
  finished after 20 frames).
- Retail plays a new voice line every 150 to 180 frames; the port plays the first (frame 190) and no other. That is not
  this module's doing: the same happens with the platform's silent stand-in and with this module made to finish every
  one-shot at once. The message box loads each line on a worker thread (MessageBox.cpp:480, `gVoiceLoader`), which is
  where the investigation goes next (the platform layer).
- Retail's log has 172 failing Unlocks (E_INVALIDARG) out of 854, and all are one case: a whole-buffer lock unlocked
  with twice the size the buffer was created with (164 sample-bank loads by frame 2, 8 voice lines). This module's rule
  (a size beyond the buffer is rejected; the data written under the lock is kept) rejects those 172 and accepts the
  other 682. Buffers are rounded up to whole frames, as Wine's DirectSound does: retail's 242,509-byte voice line locks
  as 242,510. (Retail's Play line still says bytes=242509; this module's says 242510 and hashes that many bytes.)
- The call log also writes calls retail's log never shows (C's shim does not hook them): the listener's
  SetAllParameters and CommitDeferredSettings, and a 3D buffer's SetMode and SetAllParameters. Their lines are named
  `port.Listener.*` and `port.3D.*`; drop them before comparing (`grep -v ' port\.'`).
- The Ogg decoder behind the music and voice lines is in `port/vorbis/`: retail's libvorbis 1.0, rebuilt with VC6, matches all nine Ogg buffers in the log (the music fill and eight voice lines).
