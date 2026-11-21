# Controller input in the native port

Status: CE gamepad and flight-stick support was released in 0.1.9. The adjustable CE keyboard actions and Dodge Button
below describe a development build; they are not a claim about a published release. Physical-controller acceptance remains open.
Last updated: 2026-11-13
Summary: how the native port finds controllers, the default gamepad and flight-stick inputs, adjustable keyboard actions,
the Dodge Button, where they are switched off, and what has and has not been checked.

## Devices

The Linux and Windows builds share the SDL3 DirectInput adapter in
[`platform/common/dinput.cpp`](platform/common/dinput.cpp). When DirectInput starts it offers the game up to four pads:
SDL gamepads first, then other joysticks (flight sticks). A gamepad reports SDL's button order with the D-pad as the
POV hat, the right stick X as `lZ` and Y as `lRz`, and its triggers as buttons **16** and **17** (pressed at about half
travel, released below about three-eighths). A flight stick reports its X and Y as the right-stick axes, its twist as
the left stick's X and its hat as the POV.

Because every such pad has a Z axis, retail's `UpdateJoystick` rotates the first four buttons "into 'joybox' format"
([`src/ltshell.cpp`](../src/ltshell.cpp)): game button 0 is B, 1 X, 2 Y and 3 A. The controls editor shows and stores
these game numbers.

**Hotplug.** A controller plugged in after the game started, or unplugged and plugged in again, is bound to the first
free game pad slot the next time the game reads it; the log says `input: controller connected to a game pad slot`. A
slot without a controller reads as an idle pad. The adapter retries an empty slot at most every half second, so a press
in the first half second after plugging in can be missed. In ordinary CE play, a game started with no controller has one
empty slot, so only the first controller plugged in later is picked up. With controllers present at start, the game gets
one slot for each, up to four, and a later controller takes a slot only when an unplug has emptied one. Plug both in
before starting for two players.

## What a controller does without setup

[`game/ce_gamepad.cpp`](game/ce_gamepad.cpp), read by the game's controller update through patch 0106
(`patches/0106-ce-gamepad-defaults.patch`), adds these inputs beside the player's bindings:

- **Menus** (always): D-pad or left stick to move, A to select, B to go back, Start to pause; A also skips a cutscene
  or the opening pan. A flight stick uses its hat, trigger and thumb button the same way.
- **Play**, only while no configurable control is bound to a joystick (a player who set up their own joystick bindings
  keeps exactly those): left stick moves and strafes, right stick turns and pitches, RT charges and fires the gun pod,
  LT the landing jets, A morphs, X changes weapon, Y cloaks, RB and LB zoom. A flight stick: stick to turn and pitch,
  twist to strafe, hat to move, trigger to fire, thumb button to change weapon, buttons 2 and 3 to morph and cloak, 4
  landing jets, 5 zoom in and 7 zoom out (6 is Pause, as on every pad). These numbers are SDL's button indexes, counted
  from 0; the hints show each one plus 1.

Retail sends a centred mouse to the turn and pitch controls on every update; a control that only the centred mouse set
takes the controller's value, and a moving mouse still wins. SDL button and trigger changes are queued in order and
delivered at most once per button per controller update, so a press and its release between two updates are both
delivered, in order, while different buttons may act together. The queues are bounded: a per-pad queue overflow discards
that pad's pending changes; overflow of the shared incoming SDL event buffer discards pending changes for every pad. A
disconnect or focus change also discards affected pending changes. The current device state becomes the new baseline.
The sticks, D-pad and flight-stick hat are sampled as levels, read as a held state at each update. If an SDL gamepad
mapping assigns multiple physical inputs to one logical button or trigger, that output is sampled too, because SDL's
individual release event can disagree with its aggregate held state; a sub-poll tap on such an output can be missed. If
its mapping cannot be inspected, all of that pad's buttons are sampled. Buttons are sampled as well if SDL cannot
install the event watch. For sampled inputs, an unusually short tap between samples can still be missed. The game's own
15% per-axis dead zone applies (`CController::GetMappedInputValue`, [`src/Controller.cpp`](../src/Controller.cpp)).

The triggers can also be bound by hand: select an action in the controls editor and press the trigger while it is
capturing. A saved binding that already names button 16 or 17 responds to that trigger.

In the development build, the English Level 100 help shows the connected controller's available, active dodge and zoom inputs alongside keyboard
or mouse controls when both fit. It reads the actual bindings and the CE defaults; a customized joystick disables those
defaults. Unavailable, unrecognized or potentially blocked secondary inputs are not named. Flight-stick button hints
count the SDL inputs from 1; the controls editor displays the game's internal button numbers, which can differ. Dodging
calls for full controller travel; the displayed time suggestion is inside the dodge window (more than half and less than
all of the walker's dash time), or omitted if that time is unavailable. The prompts update on remapping and on a
controller being connected or removed. If a description will not fit, the cue uses a shorter or generic one. Other
languages and the faithful, tape and network paths keep their existing behaviour. These changes have focused tests; a
player-facing tutorial run and a physical controller check remain open.

## Adjustable keyboard actions (development build)

Open **Options → Controls** and select a keyboard action to change these CE bindings:

| Action | Default key | What it does |
| --- | --- | --- |
| **Dodge modifier** | Left Ctrl | Hold with a movement direction to use the Dodge Button. |
| **Take Unit** | E | Take an eligible friendly unit in your sights. |
| **Commander** | M | Open or leave the Commander view. |

Select a row, press Enter and then press an unused keyboard key. Escape cancels and keeps the previous binding. The
controls screen refuses keys already used by an original control, the console or another enabled CE action; it does not
silently replace the old binding. It also refuses assigning an original control to a key already owned by an enabled CE
action. You can assign Take Unit or Commander before enabling its feature; it does not reserve its key while that feature
is off in CE Settings. If an older setting later conflicts with an original control when a feature is enabled, the
original control keeps its key and the CE shortcut is unavailable until the conflict is resolved in Controls.

**Take Unit** requires **Play as a Unit** in CE Settings. While controlling a unit, use your bound **Morph** action
(Space by default) to return to the Battle Engine; pressing Take Unit instead tries to take another eligible target.
**Commander** requires **Commander Map** in CE Settings and an eligible mission. Press its key again, or Escape, to
close the view. Both features can be enabled together: close Commander before taking a unit, and return to the Battle
Engine before opening Commander.

## The Dodge Button (development build)

The original walker's ground dodge uses two full movement inputs in opposite directions on the same axis: first away from
the desired direction, then toward it, with the second between half and the whole of `g_dash_time` (0.2 s, so 0.1 to
0.2 s) after the first. With default keys, A then D dodges right and S then W dodges forward. Repeating the same direction
is not the gesture. The first push counts only when the previous value on that axis was not already full in that direction,
so it must register as a fresh push. This original gesture remains available with or without the Dodge Button.

**To dodge:** with the walker on the ground, hold the **Dodge modifier** (default **Left Ctrl**) and press one movement
input: **W** forward, **S** back, **A** left or **D** right on the default keyboard layout. Use your own movement bindings
if you changed them. On a gamepad, hold **B** (the right face button on an Xbox-layout pad) and push the left stick
fully in one of those four directions. The movement input or the modifier can come first. A diagonal or simultaneous
opposite directions do not start a new dodge, and adding a second direction to a chosen one does not start another.
Change the modifier in **Options → Controls** as described above.

[`game/ce_dodge_assist.cpp`](game/ce_dodge_assist.cpp) and its [rules](game/ce_dodge_assist_rules.h) send the two
pushes on the chosen movement axis: first away from the chosen direction, then toward it. The second push is 0.55 to
0.95 of `g_dash_time` later by the game's own clock (`EVENT_MANAGER.GetTime()`). In ordinary play the
game's logic steps that clock 0.05 s at a time (twenty updates a second), so three updates, 0.15 s, is the centre of the window (0.1 s,
0.2 s) and what the assist sends (its log line says 150 or 151 ms); two and four updates sit on the window's edges, where the
rounding of the game's float clock decides, and the original gesture by hand lands at two, three or four. The walker's own
movement code decides whether to dodge. While the assist supplies those two pushes, the player's inputs on that axis
cannot join or break the sequence. A press kept for 0.35 s waits for the walker to be ready (a dash under
way, or a push of the player's own that the first push would complete the wrong way: the first push then waits until that has
passed, about 0.1 s at worst). A slow frame that carries the second push past the window starts the sequence once more, with
a neutral action on the chosen axis first when the game would not count the first push, unless the walker would not take it. Holding both keys
dodges once; for the next, let go of either and press again, or change direction (D to A, say) with the button still held. A press
made during the first part of a dash (a dash lasts 0.75 s) is dropped; one made in its last 0.35 s dodges when it is over. Flight sticks have no Dodge Button; a key or button the player has bound to a control of their own is
not one; the original opposite-direction gesture works as before.

It is on by default in ordinary CE play: CE Settings, Dodge Button (`dodge_button` in `ce.ini`, 1 or absent on, 0 off; read when
a level is entered, so a change applies from the next level). It adds an input and takes none away: without a press of the
button what reaches the player is, update for update, what the controller sent before. It is off, and the controller update is
the original's, in a recording or replay, under probes or the virtual clock, for a test pilot, in a demo or attract loop, and in
network play and its boot: the lockstep samples this peer's controls as semantic actions and applies them at the same tick on
both peers, and nothing here has been run on two peers to show that the assisted sequence would be the same on both, so
it stays off there. Tutorial level 100's dodge cue names the Dodge Button first when it is on and usable.

## Where it is off

The CE defaults and trigger buttons are off in faithful builds and while a tape, recording, virtual clock or probe run
is active. In CE recordings and replays, gamepads report neutral input, including for saved joystick bindings; flight
sticks that SDL does not recognize as gamepads are not offered. The keyboard-and-mouse tape format does not capture
controller input. The mapped joystick value is also neutral while a legacy demo dispatches its recorded actions in a
CE tape session; those action IDs still dispatch. Older tapes cannot recover controller input that was never recorded.
Faithful builds keep their existing controller behavior, with no CE defaults or trigger buttons. A clock or probe run
without recording or replay does not apply the recording and replay fence above. In local two-player split screen (the original's
multiplayer maps, and the CE bonus local co-op maps: Building Defence, levels 856 and 858; see
[the public README](../README.md), Bonus Levels) the defaults apply to each player's own pad: pad 0 is player 1's
and pad 1 player 2's, the same pad that rumble uses for that player (slot 1 and slot 2 below); with one pad connected only player 1 has one and player 2's
bindings are unchanged. A pad's Start opens the pause menu and its D-pad and A work it (shown with either pad). Network
co-op samples the local peer's own pad the same way. Hotplug, flight-stick detection and the trigger buttons work in
multiplayer as well. There is no per-device profile.
When enabled in eligible CE play, rumble (patch 0105, `game/ce_rumble.cpp`) goes to the controller slot numbered for the player
(slot 1 for player 1, slot 2 for player 2), not to whichever controller custom bindings name. Rumble is off during
recording, replay, demo and network modes. CE sends rumble only to gamepads; a flight stick does not rumble.

## Checks

For ordinary CE play, fractional relative mouse deltas accumulate until they make a whole count. The first interval
after a focus transition is discarded because SDL can combine old and new motion in that poll. This is covered by
source-coupled fakes; physical mouse and compositor behavior remain untested.

`port/tools/input_lifetime_tests.py` checks the adapter and the extracted game input callers with SDL virtual gamepads:
axis changes, trigger edges, unplug, and the replay and recording fences. A two-player hotplug case checks that one
pad's disconnect does not discard the other pad's quick tap, and that a replacement arriving held does not invent a
press. It opens no game window. SDL initialization may still discover host joystick devices; the test does not isolate
`/sys` or `/run` itself. These virtual-device checks do not establish physical-controller or native Windows behavior.

`port/tools/ce_dodge_assist_tests.py` checks all four directions, a held chord and a diagonal alongside the game's
movement mapping. It opens no game window and uses no physical device.

[`tools/ce_dodge_binding_tests.py`](tools/ce_dodge_binding_tests.py) checks the keyboard binding and Controls rows with
authored inputs, including conflicts and cancellation. Automated solo Linux play on earlier development builds exercised
all four Dodge Button directions and, separately, remapped Take Unit and Commander keys. Those runs did not exercise
the combined development build's two remapped actions, a physical controller or native Windows gameplay.

Offscreen runs of the Linux build on level 100 (authored key events and `BEA_PORT_FAKE_PAD`, the frames read) showed the keyboard
and the gamepad each making six dodges, left and right, with one session-log line apiece (`dodge button: dodge right, 151 ms
between the two pushes`), no dodge with `dodge_button=0`, and, of seven different spacings typed by hand through the same events, one landing (three
of the ten attempts of that run were mislabelled and ran at about 25 Presents, so they are not counted as separate spacings); a
review's read-only trace saw hand-entered opposite-direction pairs of two and three updates land. The figures are in `port/PARITY.md`, The Dodge Button (a file of the maintainers' working source, not in this tree). Authored events and a virtual gamepad are not a hand on a keyboard or a
physical pad.

Run these three checks separately on the exact candidate tree. Use an isolated private HOME and XDG
directories with the runtime directory set to mode 0700, disable network access, unset `DISPLAY`,
`WAYLAND_DISPLAY` and `DBUS_SESSION_BUS_ADDRESS`, and set `SDL_VIDEODRIVER=dummy`,
`SDL_AUDIO_DRIVER=dummy`, `BEA_REPORT_NO_OPEN=1` and `BROWSER=/bin/true`. Capture each command’s
stdout and stderr privately and check its exit status.

```sh
python3 port/tools/input_lifetime_tests.py
python3 port/tools/jeppi_fkey_focus_tests.py
python3 port/tools/jeppi_fkey_focus_permissions_tests.py
```

The focus check intentionally runs a failing child as a negative control; the parent command must
pass. These synthetic checks do not establish pause-menu, flight, physical-device or native Windows
behavior.

The test switch `BEA_PORT_FAKE_PAD` attaches an SDL virtual gamepad (or, with `BEA_PORT_FAKE_PAD_KIND=stick`, a
flight stick) and drives it by Present count, including `attach` and `detach` (`BEA_PORT_FAKE_PAD_LATE=1` starts
unplugged); the syntax is at the top of `dinput.cpp`. Offscreen runs of the Linux build with no keyboard input at all
have shown: the title and main menu driven by the pad, the Onslaught CE page reached and left, a morph on level 200,
left-stick movement, right-stick turn left and right and pitch up in the aircraft, a flight stick in the menus and
morphing, and a pad plugged in late or unplugged and replugged then working the menus.

On the original's two-player split-screen level 856 (offscreen Linux build, `BEA_PORT_FAKE_PAD` and `BEA_PORT_FAKE_PAD2`) pad 0's stick
moved the top view only, pad 1's the bottom view only, with one pad only the top view moved, and either pad opened and walked the
pause menu; with the virtual clock the same scripts changed nothing (`port/PARITY.md`, Split-screen gamepads; not in this tree).

Not checked: how the Dodge Button feels under a real hand on a keyboard or a physical pad, physical gamepads and flight sticks, Windows drivers (the shared adapter is compiled for Windows but
these runs were Linux), how the defaults feel in play, and the walker's turn on a level that starts on the ground.
