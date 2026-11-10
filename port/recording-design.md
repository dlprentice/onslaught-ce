# Recording CE features: design and acceptance

Status: Proposed; the CE-aware recording mode described here is not implemented.
Last updated: 2026-11-01
Summary: How to add recordings of selected CE features without changing the existing faithful reference tapes,
and what must be demonstrated before a new recording can be relied on.

## Starting point

The existing reference recorder and player use a line-oriented tape and a virtual clock. Its current input
capture is for keyboard and mouse; controller input and CE's added controller defaults are not captured as a
playable controller session. Recording also fences off some CE additions to preserve the reference run. The
[recorder and player](platform/common/replay.cpp), [mapped-input fence](patches/0115-ce-tape-mapped-input.patch)
and [controller guide](controller-input.md) own the current behavior. The existing
[queue tests](tools/record_queue_tests.py) exercise the reference path; a recording-session log check alone is
not proof that a tape can replay a controller or a CE feature.

The maintainer wants a second way to record CE behavior, while keeping reference recordings useful for measuring the
original game. This document describes an intended direction and test gate. It does not choose a final tape
format or say that any new recording mode is available.

## Two explicit profiles

1. **Reference.** The existing `--record` behavior and existing tape playback stay compatible. Its clock, input,
   feature fences, output and comparison results remain the baseline. A new option must not silently enable CE
   behavior in an old recording or change how an old tape is interpreted.
2. **CE features.** A player deliberately chooses the new profile. It records only the CE features that have
   been implemented and tested for this profile; it must not imply that every CE feature, device or platform is
   supported. The interface names the profile, what it captures and any unsupported inputs before recording.

The new profile needs an unambiguous version and capability declaration in the recording itself. A missing,
unsupported, inconsistent or truncated declaration must stop playback with a clear reason before gameplay or
save writes. A separate optional sidecar alone is not a safe discriminator: if it is missing, a player could
otherwise open an incomplete CE recording as a reference tape. Whether the present reference reader rejects an
unknown header is **not yet established**; test it with a synthetic tape and the existing runtime before choosing
an encoding. Do not rely on an older binary recognizing a format it has not been tested with.

## What a supported capture must preserve

- Capture the input and connection changes the game actually consumes, after any CE mapping needed for that
  profile. Include enough ordering and timing information to reproduce multiple reads within the same rendered
  frame, or first prove with a focused control that one sample per frame is sufficient. A frame count alone is
  not evidence of input equivalence.
- Identify the enabled and supported CE features and any settings that affect the recorded simulation. Record
  the relevant build and content compatibility information so an incompatible replay can be refused clearly.
  Do not assume two builds or platforms are deterministic merely because both can read a file.
- Reuse the reference clock and random-state machinery only where tests show it produces the intended CE
  behavior. Presentation-only changes can be added to the supported set after proving they do not disturb the
  simulation or the recorded input sequence.
- Do not silently overwrite a player's career when replaying. Where an initial career or other private state is
  necessary, require an informed local choice and preserve the existing backup and write-safety rules. A hash
  can identify a required local input; it is not a substitute for preserving that input.
- Keep the recording local. It may contain a player's input history, so collect only what the chosen replay needs:
  no hardware identifiers, account names, absolute paths, unrelated files, telemetry or automatic upload.

## Delivery gate

Build the first vertical slice around one specifically named, supported CE input or feature in local solo play.
Keep it separate from reference recordings and expose a clear way to select it. Multiplayer sessions, remote
players' inputs, notice and consent are later design work and are outside this first milestone. Recording remains
local, with no telemetry or automatic upload. Before calling that slice usable:

1. Record and replay synthetic input through the actual runtime. Check what the game consumes and its resulting
   state, including repeated polls, simultaneous inputs, device disconnect/reconnect and a deliberately broken
   capture as a negative control. A codec-only round trip is not enough.
2. Exercise unsupported capability, wrong version, missing data, malformed data and interrupted recording. Each
   must either be rejected clearly or have documented, tested recovery; none may silently become a faithful tape.
3. Run the same reference-tape comparisons before and after the change. Existing tapes and their expected results
   must remain intact, including the CE-disabled recording and replay fences.
4. Verify career and settings safety using isolated copies, including failure and cancellation. No test should
   use the player's live saves, installed game or original recording directory.
5. Test the supported device classes and both operating systems separately. Synthetic devices and a VM are useful
   evidence, but physical controllers and a real Windows graphics/audio setup remain separate acceptance checks.
   Cross-platform replay needs its own comparison; a Linux-only pass does not establish it.

The first cheap compatibility question is whether the current reader refuses an unknown tape header without
starting gameplay or touching saves. The first input question is the actual sequence of game-visible controller
reads in a bounded synthetic session. Preserve the answers and their exact tested build before fixing the new
format or promising a supported feature list.
