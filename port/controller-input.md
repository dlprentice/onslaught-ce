# Controller input in the native port

Status: CE gamepad and flight-stick support, released in 0.1.9; physical-controller acceptance remains open
Last updated: 2026-10-28
Summary: how the native port finds controllers, what a gamepad or flight stick does without any setup, how hotplug
works, where all of it is switched off, and what has and has not been checked.

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
in the first half second after plugging in can be missed. A game started with no controller has one empty slot, so
only the first controller plugged in later is picked up; plug both in before starting for two players.

## What a controller does without setup

[`game/ce_gamepad.cpp`](game/ce_gamepad.cpp), read by the game's controller update through patch 0106
(`patches/0106-ce-gamepad-defaults.patch`), adds these inputs beside the player's bindings:

- **Menus** (always): D-pad or left stick to move, A to select, B to go back, Start to pause; A also skips a cutscene
  or the opening pan. A flight stick uses its hat, trigger and thumb button the same way.
- **Play**, only while no configurable control is bound to a joystick (a player who set up their own joystick
  bindings keeps exactly those): left stick moves and strafes, right stick turns and pitches, RT charges and fires the
  gun pod, LT the landing jets, A morphs, X changes weapon, Y cloaks, RB and LB zoom. A flight stick: stick to turn and
  pitch, twist to strafe, hat to move, trigger to fire, thumb button to change weapon, buttons 2 and 3 to morph and
  cloak, 4 landing jets, 5 and 6 zoom.

Retail sends a centred mouse to the turn and pitch controls on every update; a control that only the centred mouse set
takes the controller's value, and a moving mouse still wins. Presses and releases are taken per controller update
rather than per drawn frame, so a quick tap is not lost between two game updates. The game's own 15% per-axis dead
zone applies (`CController::GetMappedInputValue`, [`src/Controller.cpp`](../src/Controller.cpp)).

The triggers can also be bound by hand: select an action in the controls editor and press the trigger while it is
capturing. A saved binding that already names button 16 or 17 responds to that trigger.

## Where it is off

All of the above is CE behaviour. It is off in faithful builds and while a tape, recording, virtual clock or probe run is
active (the trigger buttons as well), so recorded and compared runs keep retail's input exactly. In multiplayer the
no-setup defaults are off (players use their Controller Options bindings); hotplug, flight-stick detection and the
trigger buttons still work there.
Controller input is not recorded by the keyboard-and-mouse tape format. There is no per-device profile. Rumble (patch 0105, `game/ce_rumble.cpp`) plays on the game pad numbered for the player (pad 1 for player 1, pad 2 for player 2), not on whichever pad custom bindings name; gamepads only, a flight stick does not rumble.

## Checks

`port/tools/input_lifetime_tests.py` checks the adapter and the extracted game input callers with SDL virtual gamepads:
axis changes, trigger edges, unplug, and the replay and recording fences. It opens no game window.

The test switch `BEA_PORT_FAKE_PAD` attaches an SDL virtual gamepad (or, with `BEA_PORT_FAKE_PAD_KIND=stick`, a
flight stick) and drives it by Present count, including `attach` and `detach` (`BEA_PORT_FAKE_PAD_LATE=1` starts
unplugged); the syntax is at the top of `dinput.cpp`. Offscreen runs of the Linux build with no keyboard input at all
have shown: the title and main menu driven by the pad, the Onslaught CE page reached and left, a morph on level 200,
left-stick movement, right-stick turn left and right and pitch up in the aircraft, a flight stick in the menus and
morphing, and a pad plugged in late or unplugged and replugged then working the menus.

Not checked: physical gamepads and flight sticks, Windows drivers (the shared adapter is compiled for Windows but
these runs were Linux), how the defaults feel in play, and the walker's turn on a level that starts on the ground.
