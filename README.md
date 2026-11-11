# Onslaught CE

Status: preview. From 0.1.8 on it updates itself; from 0.1.7 or earlier, download the latest release once by hand.
Last updated: 2026-11-11
Summary: the native Battle Engine Aquila port: how to install, play, update, report a problem and build it, and where the project's other repositories are.

This README follows this source branch, which can describe work that is not in the latest download. The README
packaged with your download and the [release notes](https://github.com/dlprentice/onslaught-ce/releases)
describe the version you installed. Versions 0.1.8 and later have a signed-update path; versions 0.1.0 through
0.1.7 require the one-time manual migration below. On Linux, if you still use 0.1.8 or 0.1.9, check the
[0.1.13](https://github.com/dlprentice/onslaught-ce/releases/tag/v0.1.13) or
[0.1.12](https://github.com/dlprentice/onslaught-ce/releases/tag/v0.1.12) release notes for their 32-bit
dependency caveat.

Onslaught CE is a native port of *Battle Engine Aquila* (Lost Toys, 2003) for Linux (i686) and Windows (i686), built
from the game's source code. You need your own copy of the game.

## Contents

- **Install and play:** [Quick start](#quick-start), [Download and install](#download-and-install) (system
  requirements for [Linux](#linux) and [Windows](#windows)), [what is in the game](#in-the-game), and
  [your own copy of the game](#you-need-your-own-copy-of-the-game).
- **Already have Onslaught CE?** [Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself),
  [update, move and uninstall](#update-move-and-uninstall), [updates and privacy](#updates-and-privacy).
- **Settings and features:** [screen modes and widescreen](#screen-modes-and-widescreen),
  [Video Options](#video-options-new-profile-defaults-window-mode-and-extended-terrain), [mods](#mods-experimental).
- **Problems and accuracy:** [report a problem](#report-a-problem),
  [record a session](#help-make-onslaught-ce-more-faithful), [how faithful it is](#how-faithful-is-it),
  [known limitations](#known-limitations).
- **Build and contribute:** [Building from source](#building-from-source), [what is in this repository](#what-is-in-this-repository),
  [contributing, questions and security](#contributing-questions-and-security), [licence](#licence).
- **About:** [related projects](#related-projects), [credits](#credits-and-special-thanks).

## Quick start

New players start here. If you already have Onslaught CE, follow
[Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself) instead.

1. Choose the package for your system from the [Releases page](https://github.com/dlprentice/onslaught-ce/releases).
   Extract it into a **new, empty, ordinary folder**, separate from the original game and every old CE folder.
2. Run `onslaught-ce` (Linux) or `onslaught-ce.exe` (Windows).
3. It finds your Steam copy of *Battle Engine Aquila* by itself, or asks once where it is.

Keep the program and its user folder separate from the original game. Careers and settings go to
`$XDG_DATA_HOME/onslaught-ce` (Linux, or `~/.local/share/onslaught-ce` when unset) or
`%APPDATA%\Onslaught CE` (Windows) by default. Keep recovery copies of your careers before changing save locations
or removing any program folder.

This is an early preview. The main menu and `onslaught-ce --version` show your version and build. The
[latest release notes](https://github.com/dlprentice/onslaught-ce/releases/latest) list the checks made and the
remaining limits; the source here may include changes that are not in that download.

## Getting the latest version: one download by hand, then it updates itself

**Why by hand, this once?** The updater in Onslaught CE 0.1.0–0.1.6 tidied up at startup by deleting any folder next
to the game folder whose name started like it (for example `Onslaught CE.old-backup` beside `Onslaught CE`). We know
of no case where this deleted anything of a player's, but it could, so 0.1.7 switched updating off. 0.1.8 brings
updates back with a new updater that only ever touches its own two folders (`versions` inside the Onslaught CE folder
and `update-v2` in your user folder), never anything beside them; it checks every update's signature before using
it, and can go back to the previous version. Old launchers cannot reach it, so you
download the latest release yourself once; after that it updates itself.

From any earlier Onslaught CE (0.1.0 to 0.1.7):

1. **Close Onslaught CE.** Do not start the old version again (from 0.1.0–0.1.6, not even to check its version).
2. **Back up your careers and settings:** copy the folder `~/.local/share/onslaught-ce` (Linux) or
   `%APPDATA%\Onslaught CE` (Windows; paste that into the Explorer address bar) somewhere safe.
3. **Download** the latest package for your system from the
   [Releases page](https://github.com/dlprentice/onslaught-ce/releases).
4. **Extract it into a new, empty folder.** Do not extract it over the old one.
5. **Run** `onslaught-ce` (Linux) or `onslaught-ce.exe` (Windows) from the new folder. It finds your careers and
   settings by itself. Then point your shortcut at the new folder.

Keep the old folder and your backup until you are happy; never run the old launcher. If you set `BEA_USER_DIR` for
an earlier version, set it the same way for the new version. Anything unusual (a relative `BEA_USER_DIR`, data in
several places, folders you are unsure of): follow the
[detailed migration](#manual-migration-from-affected-launchers) instead.

## In the game

- **Main menu > Onslaught CE:** a page with **Bonus Levels**, **CE Settings**, **Cheats**, **Help & Info** and
  **Report a Problem**. Each row has a hint under the list. The Options page is the original's.
- **Bonus Levels:** Aquila Prototype, Building Defence, and Race Challenge 1 to 5 (each below). None of them
  advances career progress. A bonus level started here comes back to this list with its row highlighted.
- **CE Settings:** a list of choices, each written to `ce.ini` in your user folder when you make it. The page is not on
  the menu while a session is recorded.
  - Rows: **HUD Size** and **HUD Layout** (the HUD on a wide screen, see
    [Screen modes and widescreen](#screen-modes-and-widescreen)), **Sharper Text** (experimental), **End-of-Level Stats**,
    **Controller Rumble**, **Dodge Button** (see [Dodging](#dodging)), **Developer Information**, **Your Music** (shows the
    folder `ce.ini` sets: see **Your own music** below), **Music Order**, **Console Text**, **Check for Updates at Start** and
    **Record Play for Bug Reports** (see [Help make Onslaught CE more faithful](#help-make-onslaught-ce-more-faithful)).
  - Choosing: Select opens a row's list of choices, and so do Left and Right, which also move to the choice before or
    after; in the list they move as Up and Down do. Nothing is written until Select picks a choice other than the one
    shown, and Escape closes the list unchanged.
  - When a change applies: the row's hint says. It is from the next level for the two HUD rows and Dodge Button, the next
    time the console opens for Console Text (the developer information text keeps its colour until the next start),
    otherwise the next time you start the game.
  - If `ce.ini` cannot be written, a message says so and the row keeps its old choice.
- **Unfinished features:** they are listed, switched off, under `[Experimental]` in `ce.ini`; they are unsupported and liable to change.
- **Help & Info:** Controls & Tips (dodge, zoom, photo mode and screenshots, the console and the free camera), What's Coming (planned features and their current limits; its entries do not start those features),
  Updates & Rollback (whether CE Settings has update checks on, how to roll back, where `ce.ini` is), About Onslaught CE (version, licence
  and links) and Special Thanks.
- **Aquila Prototype:** level 201, the original's playable-demo level, which the full game never offers. It does
  not advance career progress and returns to the Bonus Levels list. Settings can still be saved, including into a
  career file.
- **Building Defence:** a two-player co-op map (level 856) that the original's Multiplayer page leaves out: together,
  defend the island's buildings against the landing craft. The original ships this scenario twice, as levels 856 and 858,
  which are the same map, so the list has one entry. Both players share this PC, in split screen, as in the original's
  multiplayer: each picks a Battle Engine on the Multiplayer start page; player 2 uses the second set of keys in
  Controller Options, or a second gamepad. Like the other bonus levels it does not advance career progress and comes
  back to the Bonus Levels list; settings can still be saved.
- **Race Challenge 1 to 5:** the original's five one-player time trials (levels 901 to 905): three laps against the
  clock in the Racer Battle Engine. The original opens them from the Goodies page, in the five cells that follow the
  first eight of its top row: Race Challenge 1 once you have a grade of C or better in 26 campaign levels, and each
  later race when the one before it is finished fast enough (under 145, 195, 150 and 105 seconds for races 1 to 4; the
  fifth opens nothing more). The Unlock All Goodies cheat opens all five at once (see [Cheats](#cheats)). Those routes
  save your career as the original does. The five rows on the Bonus Levels list start the races with none of that.
  Like the other bonus levels, a race does not advance career progress; when it ends (finished, lost or quit from the
  pause menu) nothing is saved and you are back on the Bonus Levels list, with that race highlighted. Settings can still
  be saved during play, including into a career file; bonus-level progress is left out.
- **Cheats:** the original game's cheats as On/Off switches, on the Onslaught CE page; see [Cheats](#cheats).
- **The console:** the original's developer console, back for solo play; press the `` ` `` (grave) key, left of 1 on a US
  keyboard. See [The console](#the-console).
- **Controllers:** gamepads and flight sticks have menu controls and default play controls for solo, local two-player
  split screen (one pad for each player) and bonus co-op when joystick bindings have not been customized. Controllers
  can be plugged in while the game runs; for two players, plug both in before starting. See
  [port/controller-input.md](port/controller-input.md).
- **Keyboard:** the port releases held keys on focus loss and attempts to repair delayed releases. Typed characters
  come from key presses rather than a desktop input method; letter and digit keys use their physical positions across
  keyboard layouts. For the walker's special move, see [Dodging](#dodging).
- **Alt-Tab and minimizing:** CE holds the game clock while it waits for a minimized window, helping avoid a fast-forward when you return (the original can do that on a fast PC;
  `BEA_PORT_MINIMIZED_CLOCK=run` brings that back for comparison). This adjustment is off during recordings and replays.
- **A quiet console:** the developers' messages are hidden unless `BEA_PORT_VERBOSE=1` is set.
- **A log file** for reports in your user folder, and **session recording** (both below).

### Dodging

In walker mode, with your Battle Engine on the ground and not already dodging, the original dodge is a double tap of the
strafe keys. With the default US keyboard bindings:

- **Dodge right:** tap **A**, then **D**.
- **Dodge left:** tap **D**, then **A**.

**Dodge Button.** Onslaught CE adds one press for it. Hold **Left Ctrl** and press **A** or **D** (or press the direction
while holding **Left Ctrl**), or, on a gamepad, hold **B** (the right face button on an Xbox-style pad; the tutorial cue calls
it the right button) and push the left stick left or right. The game then makes the two taps itself, through its own controls,
and the dodge is the original one: the same speed, length and cost. For the instant it takes, about 0.15 seconds (three of the
game's twenty updates a second), your own left and right are held back so that they cannot spoil it. If you pushed the same
direction just before (0.1 to 0.2 seconds earlier), the first tap waits a moment, because the game would otherwise dash the other
way. It follows whatever inputs you use to strafe, so a remapped layout works too. For the next dodge, let go and press again, or
change direction with the button still held; a press made early in a dodge (it lasts about three quarters of a second) is
dropped, one made in its last 0.35 seconds follows it at once. A key or button you have bound to a control of your own is not
used, and flight sticks have no Dodge Button. It is on by default (**Dodge Button: On** on CE Settings, `dodge_button=0` in
`ce.ini` turns it off, from the next level), and it is off in session recordings and replays, demos and network games. The
double tap below works as before, with it on or off.

Try releasing the first key before pressing the other. Aim for about 0.15 game seconds between presses: with the
default timing, the second press must be more than 0.1 and less than 0.2 game seconds after the first. Too fast or
too slow will not trigger a dodge. If you changed the bindings or use a different keyboard layout, check the
left and right movement inputs in **Controller Options** and use those instead.

In eligible ordinary solo play, the English tutorial can show a contextual dodge cue when the radio message starts.
The cue is off during session recording and replay; use this guide while recording. The same keyboard sequence and
timing apply.

### Zooming

With the default mouse binding, scroll the wheel down to zoom in and up to zoom out. With the default gamepad
bindings in eligible ordinary play, the right bumper zooms in and the left bumper zooms out. You can change the
bindings in **Controller Options**. Manual zoom only works with weapons that support it; some weapons zoom while
charging instead. The English tutorial can show a zoom cue in eligible ordinary solo play, but not in recordings
or replays.

### Cheats

Onslaught CE > Cheats, in the main menu, turns the original game's cheats on and off. They are kept in `ce.ini` in your user folder and apply
to every career. Typing a code into a career's name works too, as in the original. The Cheats page's switches are off
while a session is being recorded and in replays; a code typed into a career's name still works then, as in the
original.

| Cheat | Original code | What it does |
| --- | --- | --- |
| Unlock All Levels | TURKEY | Every level shows as open in the level select. Your career's real progress is not changed. In the console it also allows `map`, `win` and `lose`. |
| Unlock All Goodies | MALLOY | Every goodie on the Goodies page is open. Nothing is written to your career. |
| Goodie Hints | latête | The Goodies page shows the available unlock hints. While it is on, goodies cannot be opened, even with Unlock All Goodies. |
| God Mode Option | Maladim | Adds **God** to Controller Options (in the pause menu, applying at once, and in Options, from the next level). God on: no damage and energy that never runs out. Set this way it is kept in `ce.ini`, not in your career. Turning the cheat off ends god mode. |
| Free Camera | Aurore | In a level, **F** detaches the camera so you can fly around the battle. In the console it also allows `camera` and the remote camera. |

These are default keyboard bindings. In ordinary play, Space changes the Battle Engine
between walker and aircraft. F toggles Free Camera when its cheat is enabled. With the defaults, use the arrow
keys to look around in Free Camera; ordinary mouse-look does not turn that camera.

Free Camera controls:

- **F**: free camera on or off. Off returns to your previous view.
- **W / S**: fly forward and back, in the direction you are looking.
- **A / D**: slide left and right.
- **Arrow keys**: turn left and right, look up and down. There is no up or down key and no speed key: look up or
  down, then fly, at one fixed speed.
- **Esc**: freeze or unfreeze the battle without a menu. The camera still flies while the battle is frozen, so you can
  line up a screenshot. With photo mode (F11) the view is clear of the HUD, and F12 saves it.

The battle keeps going while you look around unless you freeze it, and your Battle Engine can be hit.

The cheats themselves write nothing to your career (only the end-of-level statistics read the original's count of
god-mode uses, to show "CHEATER!"). A level you finish with their help, including one ended with the console's `win`,
counts like any other: a win opens the next levels, earns its goodies and records its grade (a level's grade is only
ever raised, never lowered). A code in a career's name works when the name contains it exactly, including latête on Linux
and Windows.

### The console

The original game has a developer console that the PC release never showed. Onslaught CE brings it back for solo play:
press the `` ` `` (grave) key, left of 1 on a US keyboard, during a level. It drops down over the top of the screen, as a
dark panel with the game's own log (what it loaded, and what commands print), and takes a command line. It never runs
the original's command interpreter: it has its own list of commands, and never runs script files or key binds (so an
`autoexec.con` in the game folder is ignored).

- **Keys:** Enter runs the line, Tab completes a command name, Up and Down recall earlier lines, PgUp and PgDn scroll
  the log, Esc or the grave key closes the console. The game does not pause for it. A line holds up to 63 printable
  characters; anything longer, or a character the console cannot take, is refused rather than cut.
- **Always available:** `help` (and `help <command>`), `version`, `status`, `showvars`, `show <variable>`,
  `set <variable> <value>` (views, weather, hit-flash colours and the console's own `cg_consolealpha`, each checked
  against its range), `cg_whatami` (graphics card), `listatmospherics`, `navmapon` and `navmapoff` (the landscape's
  navigation map), `setgammabias <0 to 0.005>` (full screen only), `fmv_play` (lists the game's films: the cutscenes
  1-33, `GILL_M_on_a_fork` and `UsTheMovie` play from the Goodies page, `ltlogo` and `openingfmv` when the game
  starts; none plays during a battle), `playsound <name>` (a sound the level has
  already loaded) and `splitscreen horizontal|vertical` (for the next two-player game).
- **Developer information:** `devstats` (or `devstats on`/`off`) shows the original developers' information text in
  the top left: level, game time, event-list size and events processed, thing count, frame rate, health, energy and
  score (or "GOD mode Score") and the Aug bar, and the game's own DEFAULT heap use. It is the text the developer build
  drew; the PC release never showed it. The TEXDATA and VBDATA heap lines of the original are left out: the PC game has
  no such heaps. **Developer Information** on CE Settings (`dev_stats=1` in `ce.ini`) turns it on in every level; `devstats` changes it until the
  level ends. It is pale yellow as in the original, or green with **Console Text: Green** (`console_theme=green`), and only reads the game.
- **Your music:** `music` says what your playlist holds and plays; `music adddir <folder>` adds a folder of your own
  tracks (the rest of the line is the folder, spaces included); `music playtype single|linear|random`; `music next`
  skips to your next track; `music clear` goes back to the game's music. These are the original's playlist commands
  (`m_adddir`, `m_playtype`), which the PC release never registered. They last for the session; `ce.ini` keeps the
  settings (below).
- **Photo mode:** `photo` (or `photo on`/`off`) hides the HUD for pictures, as F11 does, and closes the console; F12
  saves a screenshot (below).
- **With Free Camera on:** `camera` (as the F key), `remotecameraon` (views from whatever is under the crosshair) and
  `remotecameraoff`.
- **With Unlock All Levels on:** `map <level>` (leaves this level for another campaign level), `win` and `lose`. A win
  from the console counts in your career like any other win.
- **Put back at the end of each level:** every variable `set` changed, the navigation map, the gamma bias and the
  remote camera.
- **Not available:** `exec`, `bind`, `memstats`, `dumpmem`, `dumptextures` and the original's other developer
  commands that write files or run scripts; `quit` points you at the pause menu. The console is closed in multiplayer,
  in network games, in demos, and while a session is recorded or replayed.
- **Console Text: Green** on CE Settings (`console_theme=green` in `ce.ini`) draws the text in green, from the next time the console
  opens (the console reads the line each time it opens). This is an Onslaught CE choice: nothing shows that the original's console was green.

### Photo mode and screenshots

- **F11** (or the console's `photo`, `photo on`/`off`) hides the HUD for pictures: the cockpit, the HUD and its radio
  messages, the battle line and the developer information. Menus, the pause menu, the message log and the end-of-level
  screens are still drawn, since they take your input. F11 again, or the end of the level, brings everything back. It
  needs no cheat: it only hides drawing, and the battle plays out as it would without it. To fly the camera around the
  battle, and to freeze it while you frame a shot (Esc), use the Free Camera cheat (above) with it.
- **F12** saves what is on screen as a PNG in the `screenshots` folder of your user folder, named by the date and time
  (`onslaught-ce-20261029-153012.png`, then `-2`, `-3` ... in the same second). It never replaces a file. A note at the
  bottom of the screen says it was saved; the note is never in the picture. The folder holds up to 2000 files: move
  some out when it says it is full.
- **Photo Mode in the pause menu:** in a single-player level the pause menu has a **Photo Mode** row, between the
  options rows and Retry, so a gamepad can reach it. Choosing it closes the menu, resumes the battle and turns photo
  mode on, as F11 does, and writes nothing (not a career, not your settings). While photo mode is on the row reads
  **End Photo Mode**: open the pause menu again and choose it to bring everything back. In multiplayer, a
  network game, a demo, a recording or a replay the row is not drawn and cannot be chosen.
- F11 and F12 work in single-player levels, but not while a session is recorded or replayed, in multiplayer, a network
  game or a demo. A key-capturing menu or the console can take these keys first. Photo mode is tested on Linux;
  Windows testing of it is limited (see the release notes).

### Your own music, end-of-level statistics and rumble

Three things the original team built that the PC release left switched off. Each is an Onslaught CE option, a row of CE Settings or
a line in `ce.ini` in your user folder, and none of them is ever on while a session is recorded or replayed, in a network game or in a
demo.

- **Your own music** (`music_dir`, `music_playtype`): `music_dir=` the full path of a folder of `.ogg` tracks plays
  them in place of the level music (the front end and the credits keep the game's own). `music_playtype=linear` (the
  default) plays them in name order and round again, `random` shuffles them, `single` plays one and then stays quiet
  until the next level's music. **Music Order** on CE Settings sets `music_playtype` (In Order, Shuffle, One Track); **Your Music** there
  shows the folder's name, or Off, and when chosen says the folder `ce.ini` sets (the last 57 characters of a long one; the game's font
  has no backslash and no tilde, so a Windows path is drawn with forward slashes and, on Linux, a leading `~/` as your home folder), which tracks
  can play, and that this part of the README says how to change it: the folder itself is set in `ce.ini`, because the page cannot take
  a path. The page reads only
  `ce.ini` and does not look in the folder; the log says how many tracks the game found there. A track must be Ogg Vorbis, stereo, 44100 Hz (the one format the game's music player
  plays; others are left out and counted in the log), with a full path of at most 255 bytes. Up to 256 tracks are used
  from the folder, which is read, never written; subfolders are not searched. On Linux `~/` is your home folder. The
  game pauses for about three seconds between tracks, as it does between its own. The console's `music` commands
  change it for the session.
- **End-of-level statistics** (**End-of-Level Stats** on CE Settings, `debrief_stats`; on unless set to 0): the victory and defeat screens list your mission
  time, time in jet and walker mode, rounds fired, accuracy (once you have fired) and damage taken, in the original's
  own words and layout, and "CHEATER!" in red when god mode was on. The game kept counting these; the PC release left
  the panel commented out.
- **Gamepad rumble** (**Controller Rumble** on CE Settings, `rumble`; on unless set to 0): CE requests a rumble when your Battle Engine is hit or shaken
  by its own weapons, with the effect present in the upstream PC source (the released game's rumble call was empty): full
  strength for a quarter of a second. Gamepads only; flight sticks do not rumble. Player 1's request goes to the first
  controller slot and player 2's to the second. Custom Controller Options bindings do not change which slot is used.
  It needs a controller with rumble motors; nobody has tested how it feels on one yet.

Onslaught CE is an unofficial fan project. It is not affiliated with or endorsed by the game's developer, publisher or
rights holders; *Battle Engine Aquila* is a trademark of its owners.

## You need your own copy of the game

No game files are included. You need your own copy of *Battle Engine Aquila* for PC, for example from Steam.

- Onslaught CE runs from its own folder and reads the game's data from your installation. Its default user folder
  keeps careers and settings separate from that installation.
- The `onslaught-ce` launcher finds Steam installs automatically. On Linux it looks in Steam library folders and in
  native, `~/.steam` and Flatpak Steam installs. On Windows it uses Steam's library folders and the registry. If it
  can't find an install, it asks you once to pick the game folder. To choose the folder yourself, set `BEA_GAME_DIR`.
- Saves and settings are stored in:
  - Linux: `$XDG_DATA_HOME/onslaught-ce`, or `~/.local/share/onslaught-ce` when unset
  - Windows: `%APPDATA%\Onslaught CE`

`BEA_USER_DIR` overrides that location. Use a separate ordinary directory, not the game folder or a link into it.
The settings writer refuses linked parent paths; the Linux save writer also refuses linked and hard-linked write
targets. These checks do not make arbitrary redirected folders safe on every platform. Original-install careers
remain available in the load menu, with a CE career of the same on-disk name taking precedence. On Linux, if a name
has both an older one-byte spelling and a UTF-8 spelling, the older spelling can be selected even when it is in the
original folder. Keep both files and seek help before overwriting either one.

**How a career is saved (0.1.10 and later).** A career, and `defaultoptions.bea`, is written to a temporary file in the same
folder, flushed to disk and read back; only then is it moved over the old file in one step. A save that fails part-way leaves
the previous file as it was. Saving over a name that already exists asks once: Yes writes your new career into the Onslaught CE
folder (also when the name belongs to a career in the original game's `savegames` folder, which is never written), No changes
nothing. If the game or the computer stops in the middle of a save, a hidden temporary file named `.ce-write-<number>-<number>`
(on Windows with `.tmp` added) can be left in the `savegames` folder. It is not a career and the game does not list it; you may
delete it. From 0.1.11, two of the game's automatic saves say so when they fail: after the pause menu's Continue the game's own save error
message ("An error occurred during saving." in English, and in the language you chose) appears for a few seconds at the bottom of
the screen, and after a language change in the front end it appears in the usual message box. The career you had is unchanged. Its other automatic saves of
settings (leaving the Options page, after loading a career, a display reset) stay quiet. Recordings and replays show neither.

On Linux, a career filename whose in-game name exceeds 31 16-bit units is omitted from the list because it cannot fit
the game's name field. The game emits a name-free diagnostic once, normally captured in a launcher log; it does not
show an on-screen warning. This check does not change the file. If a career is missing, keep its file and ask for help
before changing it.

Close the game and launcher before editing `ce.ini` or changing it through another tool. Concurrent settings changes
can overwrite one another.

**Supported edition.** The launcher accepts an install whose two mission data files match the SHA-256 hashes in
`config/game-data.sha256`, which come from the Steam edition. Other editions can have different files and may be
refused with "not a supported Battle Engine Aquila install" (the launcher says which file and why). If yours is
refused, please [open an issue](https://github.com/dlprentice/onslaught-ce/issues) naming the edition and that message.

## Download and install

Download the latest release from this repository's [Releases](https://github.com/dlprentice/onslaught-ce/releases) page.
If you have an earlier Onslaught CE, read [Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself)
first.

### Verify a download (optional)

Download `ASSETS.txt` from the same release into the folder containing your package. It lists the SHA-256,
byte size and name of each downloadable file except itself and `RC-REPORT.txt`; releases before 0.1.2 omit the size. It is not directly a `sha256sum --check` file.
Both files are written by the maintainers' release process, whose tooling is not in this repository: `ASSETS.txt`
is the checksum list, and `RC-REPORT.txt` is that release's own check report, which you do not need to play.
In Bash on Linux, this command handles either form and checks just the package you name (change `package` to your
download's filename). A successful check prints that filename followed by `OK` and exits zero. A missing entry,
missing file, malformed list or changed download fails the check.

```bash
package=onslaught-ce-linux-i686.tar.gz   # or onslaught-ce-X.Y.Z-linux-i686.tar.gz
set -o pipefail
awk -v package="$package" '
  /^[[:space:]]*$/ || /^Onslaught CE / || /^Source: / { next }
  length($1) == 64 && $1 ~ /^[0-9a-f]+$/ && (NF == 2 || (NF == 3 && $2 ~ /^[0-9]+$/)) {
    if (seen[$NF]++) exit 1
    if ($NF == package) { found++; print $1 "  " $NF }
    next
  }
  { exit 1 }
  END { if (found != 1) exit 1 }
' ASSETS.txt | sha256sum --check --strict -
```

### Linux

1. Extract the tarball and run the `onslaught-ce` launcher.
2. The tarball bundles dxvk-native, SDL3, libxkbcommon and the FFmpeg shared libraries.
3. It needs **glibc 2.29 or newer** (`ldd --version` shows yours) and a **32-bit Vulkan 1.3 driver** that offers what
   its graphics layer (dxvk 3.1) requires: among other extensions `VK_KHR_load_store_op_none`, `VK_KHR_maintenance5`
   and `VK_KHR_maintenance6`. It also requires 256 bytes of push constants when DXVK's descriptor-heap path is not
   enabled; extension presence alone does not establish that path is usable. On older distributions the graphics
   driver, not glibc, is what stops it. Found on test systems: the Mesa drivers of Ubuntu 22.04 (23.2) and Debian 12 (22.3) do not have
   those extensions (Debian 12 has Mesa 25.0 in `bookworm-backports`), and Fedora 40's Intel driver (Mesa 24.1) offers
   only 128 bytes of push constants and is refused, while its software driver (lavapipe, the same Mesa) is accepted.
   AMD (RADV) and NVIDIA drivers have not been tried on those distributions. The preflight check below says when your
   glibc is too old or your 32-bit driver lacks the Vulkan version or those extensions (it cannot see the push-constant
   size).
4. Your system must provide these 32-bit libraries:
   - glibc, libgcc and libstdc++
   - zlib
   - the Vulkan loader and a 32-bit Vulkan driver (above)
   - the X11 or Wayland client libraries; libXrandr (X11) and libdecor (Wayland) are optional: the preflight only notes
     a missing one (SDL works without libXrandr, with fewer display features; without libdecor the windows have no title
     bar on some desktops)
   - `zenity` (any architecture), which shows the launcher's questions (the update offer, the game folder, session
     recording); without it the launcher uses SDL's own dialog, or asks on the terminal if no dialog works
   - `curl` (any architecture), which the launcher runs to check for and download updates; without it no update is
     found and the game still starts
5. A preflight check lists any missing 32-bit system libraries, with their pacman and apt package names. Set
   `BEA_SKIP_PREFLIGHT=1` to start without it.

The 32-bit libraries by distribution family (plus your GPU's 32-bit Vulkan driver):

- **Arch and derivatives** (enable the `multilib` repository in `/etc/pacman.conf` first; tested):

  ```sh
  sudo pacman -S --needed lib32-glibc lib32-gcc-libs lib32-zlib lib32-vulkan-icd-loader lib32-libx11 lib32-libxext lib32-libxrandr lib32-wayland zenity
  sudo pacman -S --needed lib32-vulkan-radeon     # or lib32-vulkan-intel, or lib32-nvidia-utils
  ```

- **Debian and Ubuntu** (Ubuntu 24.04, or Debian 12 with Mesa from backports:
  `sudo apt install -t bookworm-backports mesa-vulkan-drivers:i386`; the Mesa of Ubuntu 22.04 and stock Debian 12 is
  too old):

  ```sh
  sudo dpkg --add-architecture i386 && sudo apt update
  sudo apt install libc6:i386 libstdc++6:i386 libgcc-s1:i386 zlib1g:i386 libvulkan1:i386 libx11-6:i386 libxext6:i386 \
      libxrandr2:i386 libwayland-client0:i386 libwayland-cursor0:i386 zenity mesa-vulkan-drivers:i386   # NVIDIA: the driver's i386 libraries
  ```

- **Fedora** (Fedora 40's Intel driver, Mesa 24.1, is refused for its push-constant size; other Fedora releases and
  drivers have not been tried):

  ```sh
  sudo dnf install glibc.i686 libstdc++.i686 libgcc.i686 zlib-ng-compat.i686 vulkan-loader.i686 libX11.i686 libXext.i686 \
      libXrandr.i686 libwayland-client.i686 libwayland-cursor.i686 zenity mesa-vulkan-drivers.i686     # NVIDIA: the driver's i686 libraries
  ```

- **Steam Deck and SteamOS** (untested): add `onslaught-ce` to Steam as a non-Steam game and set its compatibility tool
  to "Steam Linux Runtime 3.0 (sniper)"; that container supplies the 32-bit libraries and the Deck's driver. The game
  ran in that container on a desktop PC, started directly from the container's own launch script. Starting it as a
  Steam shortcut with that compatibility tool has not been tried, on a Deck or anywhere else.

### Windows

The Windows build has been checked under Wine on Linux and on a Windows 11 virtual machine using Microsoft's
software renderer. It has not yet been tested on a Windows PC with a real graphics card and its driver. Please
report any problems; see Known limitations and the release notes for the routes checked.

1. Extract the zip and run the `onslaught-ce.exe` launcher. The zip also contains `bea.exe` and the SDL3, FFmpeg and
   zlib DLLs.
2. You need Windows 10 or newer, for its built-in UCRT. User and game folders whose names have
   characters outside your Windows language's character set work from Windows 10 version 1903 on (the UTF-8 setting
   the programs use is ignored by earlier versions).
3. The executable is unsigned, so SmartScreen may show a warning. Choose **More info**, then **Run anyway**.

## Update, move and uninstall

- **Update:** from 0.1.8 on, the launcher updates itself ([Updates and privacy](#updates-and-privacy)). From 0.1.0–0.1.7,
  install the latest release by hand once ([Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself)); never
  start an old launcher to reach its updater.
- **Move:** the program folder can live anywhere; move it as a whole (with its `versions` folder) while the game is
  closed. The launcher remembers your game folder in
  `ce.ini` (`gamedir`); if the game moves, it logs why the remembered folder no longer works and searches again, or
  set `BEA_GAME_DIR` to the new folder.
- **Uninstall:** first make and verify a recovery copy of the user folder, including any custom `BEA_USER_DIR`.
  It may be inside a program folder if you chose that location. Then delete the program folder. Before deleting an
  old 0.1.0–0.1.6 folder, look at any neighboring `.old-*`, `.download-*` and `.unpack-*` folders yourself: they may
  hold things of yours; never use the old launcher's cleanup.

<a id="windows-upgrading-from-015"></a>

### Manual migration from affected launchers

The detailed version of [Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself), for unusual setups.

1. Close CE and its launcher. **Do not run the affected version again.** Do not rename or delete its program folder
   or any neighboring `<program folder>.old-*`, `.download-*` or `.unpack-*` paths. They may contain unrelated files
   or the only remaining copy of your data.
2. Locate **both launcher and game user-data folders** without starting the launcher. `BEA_USER_DIR` takes priority;
   otherwise the default is `$XDG_DATA_HOME/onslaught-ce` on Linux (`~/.local/share/onslaught-ce` when unset), or
   `%APPDATA%\Onslaught CE` on Windows. A relative `BEA_USER_DIR` can put launcher files and game user data in
   different locations. For example, the 0.1.6 Linux source indicates that a launch from a folder with
   `BEA_USER_DIR=user` would resolve launcher files in `user` there and game user data under `user/user`; Windows
   behavior has not been verified. Identify the actual locations from the old launch working directory and saved
   files; do not assume they are the same.
3. Copy **each whole data folder** to an independent backup outside the original game and all CE program and staging
   folders. Verify the copies and identify the locations containing your careers (`savegames`), settings (`ce.ini`)
   and recordings when present. Keep all originals and backups; if locations or conflicting files are unclear, do not
   merge or overwrite them, and seek help before choosing a working folder.
4. Choose the user folder the new version will use. If your data is in the default folder above (no `BEA_USER_DIR`) and step 3's
   backup is verified, the new version can use that folder as it is: leave `BEA_USER_DIR` unset. Otherwise (a custom or relative
   `BEA_USER_DIR`, or locations you are unsure of), prepare a separate working copy of the identified game user-data
   folder outside the original game and all old CE program and staging folders, and set `BEA_USER_DIR` to its absolute
   path for the successor. Preserve any separately located launcher settings and logs as well; do not assume copying
   the game folder restores them. Do not put saves into the new program folder.
5. Download the **latest** platform package and `ASSETS.txt` from that specific release, and [verify the package](#verify-a-download-optional).
   Do not use 0.1.6 as an intermediate upgrade, and do not use an old launcher's automatic update path.
6. Extract the successor into a **new, empty folder writable by your account**, with no directory links/junctions
   in its path, separate from the original game and all old CE folders. Do not merge old executables or DLLs into it.
7. Run only the successor's `onslaught-ce` (Linux) or `onslaught-ce.exe` (Windows), using the same account and the
   user-folder choice from step 4. Check **Onslaught CE > About**, careers and settings, then update your shortcut.
   Keep your backup and uncertain old/staging paths. No administrator execution or disabled UAC is required.

Windows 0.1.5 also has a separate update-helper startup problem. The same manual procedure avoids invoking that
old helper. The embedded execution-level declaration in 0.1.6 does **not** fix the cleanup hazard above.

## Updates and privacy

This source's launcher (from 0.1.8) checks for updates when it starts: it downloads the public files
`ce-update-v2.json` and its signature from the latest GitHub release, checks the signature against the key built into
the launcher, and asks before installing. **Update** downloads the package, checks its size and SHA-256 against the
signed feed, and unpacks it into `versions/` inside the Onslaught CE folder; that folder's own files and everything
beside it are left alone, and the version you had stays for `--rollback`. Careers and settings are not changed.
`--update-only` checks now without starting the game; `BEA_NO_UPDATE_CHECK=1`, **Check for Updates at Start: No** on CE Settings (`update_check=0` in `ce.ini`) or
`--no-update-check` turn checking off; **Onslaught CE > Help & Info > Updates & Rollback** in the game says whether that setting has turned checking off, and where
`ce.ini` is.
On Linux the launcher downloads with the system `curl` (https only); without `curl` the check finds nothing and says so
only in the log. Releases never carry the old `latest.json` feed that launchers 0.1.0–0.1.6 fetch, and the legacy
`--apply` command is refused.

**Going back:** if an update gives you trouble, close the game and run the launcher in the Onslaught CE folder with
`--rollback`: `./onslaught-ce --rollback` (Linux), or `onslaught-ce.exe --rollback` from a Command Prompt in that folder
(Windows). The version before the update becomes the active one again, and that update is not offered again; a newer
one will be.

There is no telemetry. **Report a Problem** is a user-initiated action: it opens a GitHub page in your browser with
your version and OS in its address; nothing is posted until you submit it.

## Screen modes and widescreen

Onslaught CE starts at your desktop's resolution, and Video Options lists your monitor's sizes, widescreen ones
included. On a widescreen monitor the 3D view shows more to the sides (the original's vertical view is kept); the menus,
loading pictures and films keep their original 4:3 shape, centred with black bars. To get the original's 4:3-only list
and 640 x 480 start back, set `BEA_PORT_WIDESCREEN=0`. The pause menu, the message and briefing logs and the end-of-level menus read the mouse inside that
centred picture too, so the pointer and the items under it agree at any window size, and a click acts on the item it was made on even when the pointer had not moved first.

In eligible ordinary single-player play with widescreen modes enabled, the HUD (radar, portrait, messages, compass,
reticle, and the markers it draws on targets and objectives) scales up when the screen is wider than 640 and taller
than 480 pixels, keeping the size it has at 640 x 480 relative to the screen: at 3840 x 2160 it is 4.5 times as large.
Each marker grows about its own target. Choose **HUD Size: Original** on CE Settings (`hud_scale=0` in `ce.ini`) for the
original's fixed-size HUD, from the next level on (a `hud_scale` line written into `ce.ini` by hand is read once, when your
first level starts: restart the game after changing it); or set `BEA_PORT_HUD_SCALE=0`, which also keeps the corner groups
below in the 4:3 columns. A `BEA_PORT_HUD_SCALE` that is set and not empty decides the size instead of `ce.ini`: a value that
starts with 0 (`0`, `00`, `05` ...) gives the original's, with the corner groups in the 4:3 columns, and any other value the
larger HUD; the HUD Size row still shows what `ce.ini` says. Scaling is off in recordings and replays, in split-screen
multiplayer, and when `BEA_PORT_WIDESCREEN=0`; a two-player game on the Network Game page of an experimental network
build (below) scales the HUD as single-player play does. The developer information text (`devstats`) is not part of
this: it grows with the window, as the CE console does, whatever `hud_scale` or `BEA_PORT_HUD_SCALE` says.

In the same play the HUD's corner groups (the target panel, objectives, radar, weapon and ammunition readouts and the
radio portrait) sit in the corners of the screen, where the original's HUD sits at 4:3, while the radio messages,
warnings, compass and reticle stay in the middle. On a very wide screen the corners are far apart: choose **HUD Layout: 4:3 Columns** on CE Settings
(`hud_anchor=0` in `ce.ini`) to keep them in the columns a 4:3 picture would have, as `BEA_PORT_HUD_SCALE=0` does. In split-screen
multiplayer the HUD stays in the 4:3 columns.

Sharper text is off by default: the menus' and HUD's text keeps the original's soft look, as the rest of the picture
does. Choose **Sharper Text: Yes** on CE Settings (`sharp_ui=1` in `ce.ini`) to turn it on from the next start: with widescreen modes enabled on a non-4:3 picture, bitmap text
drawn at least twice its original size horizontally or vertically (for example, menu text at 1920 x 1080) can use a
sharper copy of the game's letters, so it can look crisp instead of soft; the letters keep their shapes, sizes and
places. The text is then deliberately sharper than the original's soft pixels and looks brighter. A font's sharper copy
is made the first time it is used, which can pause the game for a moment (about a tenth of a second). In ordinary use
it does not apply in recordings and replays, on 4:3 screens or when `BEA_PORT_WIDESCREEN=0`.

For testers: to try a screen size your monitor does not offer, set `BEA_PORT_WIDE_TEST=WIDTHxHEIGHT` and start with
the same size, for example `BEA_PORT_WIDE_TEST=2560x1440 onslaught-ce -- -res 2560 1440`. The game then treats it as
an available widescreen mode. A valid test switch forces widescreen modes on even during recording or replay and when
`BEA_PORT_WIDESCREEN=0`; unset it before making a faithful reference tape.

In ordinary use, replays and recordings use the original 4:3 behaviour. The original game shares one random number source
between visual effects and play, so a wider view can change the details of a fight; that is expected.

### Video Options: new-profile defaults, Window mode and Extended terrain

**A new profile starts at the best the game offers, except one costly row.** The first time you start with no saved options of your own (a new install,
or a profile that only has the old game folder's file), Video Options (Main menu > Options > Video Options) starts every quality row at its highest choice (except Full models, below),
**anti-aliasing at the highest level your graphics device lists**, and **extra graphical features On** where your device passes the capability test.
**Full models** starts **Off**: it draws every model in full detail at any distance and is by far the largest cost in frame rate (on the developer's laptop, a graphics card of the RTX 4060 class, turning it On took level 200
from 383 to 67 frames a second with the same terrain setting), so turn it **On** in Video Options if your computer has the room. **Terrain detail distance** starts at **Extended**, which draws the terrain
further away (see below); choose **Normal** for the original's distance. Anything you have saved before is kept as it was: the new defaults apply once, to a
profile that has no options of its own, and every row can be changed afterwards. The adapter name and the long choices on the page now fit the screen.

**Windowed or Fullscreen without a restart.** Video Options has a **Window mode** row (Windowed or Fullscreen), also in the paused game: choose and press
Apply, and the window changes at once; the choice is saved (`windowed` in `ce.ini`) and used at the next start. If the change cannot be made, the
previous mode comes back and the row says so; if it was made but could not be saved, the row says that too. It is off in session recordings and replays.

**Extended terrain and its reserve.** Extended draws the terrain further than the original's memory pools hold. When you first use it in a level, CE sets
aside a small reserve of extra terrain memory (a few megabytes at most, created once and used only after the original pools run out), so the far terrain
is drawn with the memory it needs on the one-player levels we measured (levels 100 and 200 ran without a failed terrain request). **The two-player split-screen level (860) still runs
short:** about 200 terrain requests a frame find no free memory even with the reserve, all of them in the nearest tier; we do not know what the game does with a failed request, and in the one frame we looked at the
ground was drawn. Its frame rate with Extended (170 and 210 in two runs) and with Normal (215, one run) was too noisy to tell apart. If the reserve cannot be made, the row says
**Extended (using Normal)** and the game draws the original's distance. You can always choose **Normal**. The figures for the costs of these choices are in the [release notes](https://github.com/dlprentice/onslaught-ce/releases).

## Mods (experimental)

Onslaught CE can read replacement game files, such as textures, from mods on Linux. This is an early test of the
feature: a mod can replace files the game already has, supply a level's world file and add the archive of a multiplayer
level the game has none for (both below), and there is no mod menu yet. Mods are disabled on Windows until path handling
is safe and tested there: on Windows a `mods=` line in `ce.ini` is ignored, and the log says so.

- A mod is a folder in the `mods` folder of your user folder: `~/.local/share/onslaught-ce/mods/<id>` on Linux.
  It holds a `mod.json` and the replacement files at the paths the game
  uses, for example `data/resources/dxtntextures/meshtex%BE_texA.tga(0)A1R5G5B5.aya`.
- With the game closed, back up `ce.ini` and add one line naming the mods in order: `mods=first-mod,second-mod`.
  When two mods replace the same file, the later one wins. Remove an id (or the line) to turn a mod off.
- Nothing in the game folder or in a mod's folder is ever changed. A file you put in your user folder yourself still
  wins over a mod.
- Every file is checked when the game starts: `mod.json` must list it with its size and SHA-256, it must be inside the
  mod's folder (no links), and it must replace a file the game has (or be a world file or a level archive, below). A mod
  with any problem is not loaded at all, and the log says why. The log also names each active mod and every file it
  replaces or adds, and notes the first time the game reads each one.
- To check mods without playing, run the game program in the Onslaught CE folder with `--check-mods`: `./bea --check-mods`
  checks the mods `ce.ini` enables, `./bea --check-mods first-mod` the ones you name. It runs the same checks the game runs
  when it starts and prints every problem on its own line (`refused <id>: <reason>`; where the game stops at a mod's
  first problem, this goes on to the end of the mod), then `result: <n> passed, <m> refused`, counting mods. It changes no file, not even
  to create the user folder, contacts nothing and opens no window; it exits with status 0 when nothing was refused and 1
  otherwise. It reads the same folders the game does: set `BEA_GAME_DIR` (and `BEA_USER_DIR`, if you use one) as the
  launcher would. Run `bea` itself rather than the launcher, which writes its log and may check for an update. The Windows
  `bea.exe` answers that mods are off on Windows, with status 3.
- A mod can also supply a level's world: `data/worlds/world100.wrd` (three digits, the level number) listed like any other
  file. The game then reads that world body instead of the one inside the level's archive. A level's own world and its base
  world are separate files (`world001.wrd` is the base world of level 100), and a mod may supply either or both. Careers
  remember which buildings of a base world are still standing, so edit a base world by adding things at the end, not by
  moving or removing the ones it has. If a world file changes after the game has started (an editor saving it, say), the
  game loads that level's own world instead, and the log says so.
- A mod can also add the archive of a multiplayer level that the game has none for: `data/resources/888_res_PC.aya`
  (three digits, from 850 to 899; the game's own multiplayer levels are 850 to 866). The archive holds a level's terrain,
  its texture and model lists and its world. An archive the game has is replaced like any other file, and the packed-file
  check (the one every packed file passes: the game's block reader must be able to unpack it) is all that protects a replacement: its
  chunks are not walked, because the game's own archives hold models that the checks below do not admit yet, so a replacement with a
  broken chunk can still crash the game. Nothing in the game
  starts an added level by itself, and an older Onslaught CE refuses the whole mod, so give such a mod a `ce_min` (below)
  of the first version that can load it. Because there is no original to compare with, an added archive is checked more
  closely when the game starts: at most 8 MiB packed and 32 MiB unpacked, the standard first chunks (each once), every
  chunk inside the file, texture entries of exactly the size the game reads and model entries of at least that size (a model
  that names its own file is exactly that size plus 317 bytes for each model chained to it), and exactly one world chunk,
  last, laid out as the game's own are: the archive's own level number, then the base world if it has one, then the world,
  each of at most 4 MiB. One model entry may name no file: it holds the model itself after its fixed bytes, and is
  checked the way the game reads a model, with every count, size and index in it bounded. It must be built like the
  game's own models, though not held to every detail of them: no cutscene sequences, at most 32 textures and 32
  animations, each animation's first and last frame a frame of every part, its frame scaler the last frame less the
  first (or, going forward, one more) and its rate (the share of it played each game tick) from 0 to 1, parts numbered
  in order and linked as the game's are (a part's parent comes before it and is the part that lists it as a child, its
  children come after it, no part is listed as a child twice, and no part is more than 32 levels below the top),
  each part's stored poses as the game's own models hold them (one, or one for each frame, and left out exactly where
  a part takes its position or turn from its parent, which only a part with a parent may do),
  vertices of 36 bytes in at most 12 pieces, skeleton parts of at most 19 bones whose
  vertices of 48 bytes name those bones, and camera parts with a field of view for each frame. Models chained to it are
  accepted, up to 16 in all, when each follows the one before it as the game's own chained models do and has as many
  animations as the first; a second such entry is refused.
  The log says which check refused it.
- A mod's files are read by the game itself. The checks above cover where a file is and what it contains at the byte
  level, not what a texture, archive or world inside it does, so install mods only from people you trust.
- Mods are always off whenever a run is meant to be reproducible or shared, so those stay the same for everyone: replays and recordings (and the developer settings behind them: a deterministic clock, input tapes, probes, pilots and autopilot) and network play (host, join or handoff). A test that should load a mod must run live on the real clock.

`mod.json` looks like this (`id` is the folder's name; optional `ce_min` and `ce_max`, such as `"0.2"`, limit
the Onslaught CE versions it loads in):

```json
{"schema": 1, "id": "red-aquila", "name": "Red Aquila", "version": "1.0", "author": "you",
 "description": "A red battle engine.",
 "files": [{"path": "data/resources/dxtntextures/meshtex%BE_texA.tga(0)A1R5G5B5.aya",
            "size": 146214, "sha256": "<64 hexadecimal digits>"}]}
```

`port/tools/mods.py` in the source writes a new `mod.json` for a mod folder (`manifest`) and packs and unpacks the
game's `.aya` files (`pack`, `unpack`); outputs must be new names outside the selected game folder. Set `--game` when
using a non-default game folder. The tool cannot enable mods or edit `ce.ini`. These output operations currently need
Linux directory handles, reject links and conservatively refuse alternate mounts of the game's filesystem; an
interrupted write may leave an incomplete new file. A texture `.aya` holds a DDS image.
A texture may be larger than the original (twice
the size has been tested), but its unpacked file must stay under 5 MiB, the game's limit for one texture.

## Report a problem

1. In 0.1.7 or later, choose **Onslaught CE > Report a Problem** in the main menu, or run `onslaught-ce --report`
   (Windows: `onslaught-ce.exe --report`, from a Command Prompt in the program folder or a shortcut with `--report`
   added). Do not start a 0.1.0–0.1.6 launcher to report a problem, by either route; use the
   [Issues](https://github.com/dlprentice/onslaught-ce/issues) page instead and give the version you downloaded.
   Report a Problem opens a new issue on this repository in your browser, already filled in with your version and
   system (which GitHub sees in the page's address), and prints the folder that holds your log. Nothing is posted
   until you submit the issue. You can also open an issue on the Issues page yourself.
2. Say what happened and how to make it happen again.
3. Attach the affected run's log from your user folder (`$XDG_DATA_HOME/onslaught-ce` when `XDG_DATA_HOME` is set
   and nonempty, otherwise `~/.local/share/onslaught-ce` on Linux; `%APPDATA%\Onslaught CE` on Windows). The usual
   file is `onslaught-ce.log`. If you ran multiple copies at once, the affected run may instead be in
   `onslaught-ce.concurrent-1.log` or `onslaught-ce.concurrent-2.log`. Check the start time in the log to find the
   affected run and save it promptly; later launches can replace log files. In ordinary, unrecorded play it includes
   the launcher and game identities, the game executable's SHA-256, graphics and audio configuration, every display
   (size, refresh rate, scaling, which one the game used),
   the keyboards, mice, controllers and audio playback devices by the names your system gives them, the Onslaught CE
   settings in use, level and player-mode changes, window focus changes, music/film requests and results, and counted
   audio submission errors. Events carry elapsed milliseconds and a Present
   number. These details help distinguish a mixed installation, a device failure and the order of playback requests;
   successful requests do not prove what reached your speakers. Screenshots and recordings are still useful.
   `onslaught-ce.old.log`, when present, holds the previous primary log. Each stops at 4 MB, and repeated diagnostic
   events have their own limits.
   Home paths and account-name path components are shortened; look the file over before sharing it. It needs no
   administrator access, records no microphone audio and is never uploaded automatically. You can also share the
   file directly with someone helping you. Replays and session recordings omit the detailed ordinary-play events.
4. If you recorded the session (below), mention that a recording is available. Do not attach the raw tape or metadata
   to a public issue: they can contain typed text, careers and other user files. Say in your issue that a recording
   exists and attach nothing; a maintainer will agree an evidence route with you there.

If you set `BEA_USER_DIR` to a folder, look for the log in that folder instead. Logs can include local paths,
user names and device details; review them before sharing publicly, while keeping the details needed to diagnose the
problem.

## Help make Onslaught CE more faithful

The goal is a game that behaves exactly like the original. We check that by replaying recorded input
in both and comparing them, and recordings of real play find differences our own test runs miss.

- Record one session with `onslaught-ce --record`, or every session with **Onslaught CE > CE Settings > Record Play for Bug
  Reports** in the main menu. That starts the next time you launch Onslaught CE (quit the game, then start it again); it puts
  `record_sessions=1` in `ce.ini` in your user folder. While it is on, each start asks whether to record: **Stop
  recording** there (or starting with `onslaught-ce --no-record`) turns it off again.
- By default, recording targets 30 frames a second for play and uses the replay clock. Its menus take the keyboard
  and mouse, and you can type a career's name. Direct gamepad and flight-stick input is disabled during recording and
  replay because the tape does not record it; use the keyboard and mouse. An older tape made with live controller
  input cannot recover that input and may replay differently. The screen is normally the original 4:3 (no
  widescreen), and the menus are the original's: the version text, no Onslaught CE menu and no Cheats page, and only
  cheats typed into a career's name work. These Onslaught CE additions are off too: the larger HUD, the command panel,
  the tutorial's dodge and zoom
  cues, and the saved Window mode (Windowed or Fullscreen) from Video Options. By default the game tries to play sound
  through the audio monitor when a device is available. The [dodging instructions](#dodging) above remain available in
  this guide.
- Each completed session is saved in the `tapes` folder of your user folder as
  `YYYYMMDD-HHMMSS-PID-SERIAL.tape` and a `.tape.json` beside it. The final tape appears when the recording closes
  successfully. A failed or interrupted capture may leave an `.incomplete` file; it is not a completed recording.
  Keep it for diagnosis rather than replaying it. The launcher reports that the recording failed. Earlier recordings
  are never replaced.
  The tape records key presses and releases, typed text, mouse movement and buttons, their frame numbers, and the
  game's rendering size for menu mouse input. The `.json` includes the CE version and build, platform and OS, date and
  time, clock, frame rate and audio-monitor setting, game arguments, and encoded copies of some files in your CE user
  folder at the start. These can include game options, saved careers and other files in that folder. Session recording
  does not capture video, game audio or microphone audio. Both files can contain personal information; review them
  before sharing.
- Start a report with what differed from the original and roughly when. Do not upload a raw `.tape`/`.tape.json` zip
  publicly: encoded user files can include saves and private information. Say in the issue that the recording exists and
  attach nothing; a maintainer will agree an evidence route with you there (see [CONTRIBUTING.md](CONTRIBUTING.md)).

## How faithful is it?

The port is compared with the original game using recorded replays under a virtual clock. Selected state fields are
checked at specified frames, often every 50 frames on standard levels and every frame on selected recorded sessions.
Some original-game references also synchronize music or voice loading for the comparison.

**Standard test levels (an earlier 0.1.5 candidate).** The available sampled state agreed with the retained original-game references for
levels 100, 200, 400, 521 and 611 through 10,000 frames, and level 860 through 3,250 frames. Missing player
state after death is unavailable evidence, not a match. Beyond frame 3,250, level 860's original-game recordings
disagreed with each other. The earlier level-200 health and level-860 energy differences were corrected in that
candidate; this does not prove all gameplay, pictures or sound are identical. Later development comparisons use
additional references and cover more frames; the release notes describe the checks for each published version.

**Historical 60-level sweep.** This sweep played the first ~3,000 frames (about 1 min 40 s) of 60 levels with one generic input
recording. 30 of those levels have original-game recordings to compare against:

- 20 matched the recorded fields at the sampled frames.
- 2 had small differences at the time: levels 322 and 431.
- 8 have original-game recordings that disagree with each other, so they can't decide a match.

Those 30 levels were replayed again on the 0.1 build itself; the sweep is not a claim about later releases. Replays run
in faithful mode (4:3, no CE menus or cheat switches).

**Earlier interactive check:** the front end, creating, loading and saving careers, options, videos and the tutorial
through its third zone. Complete tutorial parity with the original remains open.

**Recorded session check:** Onslaught CE 0.1.9 for Linux replayed a player-recorded Level 100 tutorial session through
its win and debrief. This covers one recorded route; it does not establish complete parity with the original.

## Known limitations

- Released launchers 0.1.0–0.1.6 contain the [startup cleanup hazard](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself); do not run them.
  Install the latest release by hand once ([Getting the latest version](#getting-the-latest-version-one-download-by-hand-then-it-updates-itself)).
  Windows 0.1.5 also has an update-helper startup failure. Neither disabling updates nor installing 0.1.6 solves the cleanup risk.
- Windows checks cover Wine and one Windows 11 virtual machine using Microsoft's software renderer, not a PC with
  a real graphics card. The [release notes](https://github.com/dlprentice/onslaught-ce/releases/latest) describe the
  routes tested for each version and its known limitations. Ordinary play does not hang at the idle title screen when films are disabled (fixed in 0.1.4);
  faithful replay retains retail behavior there. A default-fullscreen mouse hit-area mismatch and a recovery problem after minimize/focus changes, both
  reproduced in the Windows 11 virtual machine, are corrected (0.1.5). The player's AMD graphics setup remains untested.
  Please report problems with the log.
- The Windows executable is unsigned, so SmartScreen may warn you (see above).
- On Linux you need glibc 2.29 or newer and a 32-bit Vulkan 1.3 graphics driver with dxvk's required extensions (see
  Linux above). It was run in Ubuntu 24.04 and Debian 12 (with backported Mesa) systems and in the Steam Linux
  Runtime 3.0 container, all with test drivers and no window; nobody has played it on those systems or on a Steam
  Deck, and the Steam shortcut route for the Deck (above) has not been tried. Those runs used a 0.1.3 candidate build,
  not the released packages. An asset-decompression buffer overrun and an audio-file cleanup defect reproduced in a
  Fedora 40 file system with the software driver (lavapipe) are corrected (0.1.5). Bounded level-entry
  and shutdown checks pass there; this is not a full-playthrough or hardware-driver validation.
- One effect depends on leftover memory rather than on the game: a walking unit's foot-landing flags are never given
  a starting value by the original game's code, so whether its first landing makes a stomp explosion depends on what
  that memory held before. This can differ between builds and between machines: level 521's replay shuts down one
  more explosion on 0.1.3 than on 0.1.2, and nothing else the replay compares (positions, health, random numbers,
  scores) differs. A recording that reaches such a landing may play back slightly differently on another version or
  machine.
- Recording and replay were compared using an isolated native PipeWire null sink as well as silent audio;
  the tested tape, mixed audio and saved bytes agree. Ordinary-clock music transitions and clean shutdown were
  also exercised. These checks do not validate a physical audio device, listening quality or every race schedule.
  Music starts that could survive a stop request are corrected (0.1.5). Menu music that played twice, reported on
  Windows after a sound option (quality, hardware sound, device or 3D sound) had been changed, is corrected (0.1.12).
- Videos are decoded with FFmpeg's Bink decoders. Compared with the game's own Bink decoder under Wine, 333 of the
  48,627 frames of its 66 videos differ byte for byte, in five videos. The video sound has no listening check, and its
  timing against the picture is not checked.
- Complete tutorial parity with the original has not yet been established.
- Gamepad and flight-stick defaults cover menus, solo play and bonus co-op ([how](port/controller-input.md)); only
  virtual devices have been tested so far.
- Widescreen play is not compared with the original (the original has no widescreen); ordinary replays and recordings use 4:3.
- Planned, not in this version yet: Internet multiplayer, mods on Windows and additional HUD options. On Linux, mods
  are an early test ([Mods (experimental)](#mods-experimental)).
- On Windows, 0.1.12 could select the wrong pause-menu item at some display sizes and could leave the system
  cursor visible in fullscreen. 0.1.13 changes both. The pointer fix was checked on Linux (1920x1080 and 1024x768)
  and in a Windows 11 virtual machine, and the fullscreen cursor fix in that virtual machine; neither has been seen on
  a physical Windows PC with its own graphics driver. If you still see either, include your display settings and log
  in your report.
- Reports of layered or stuttering music and map-edge artifacts still need retesting on the affected Windows
  hardware. An AMD-specific cause has not been confirmed.

## Building from source

The build runs on Linux (x86-64) and makes both programs from a Git checkout of this public repository. Use Python
3.12 or newer, Git, Ninja, Make, patch, pkgconf, Bison 3.6 or newer, and a host C/C++ compiler. Packaging additionally
uses curl and GNU binutils. No private checkout or maintainer setup script is required. Windows contributors can
use a Linux development environment to produce the Windows executable; native Windows play is a separate check.

On Windows, use [WSL2](https://learn.microsoft.com/en-us/windows/wsl/install) with an x86-64 Linux distribution.
Check `wsl --list --verbose` from Windows first. If it is missing, the owner can use `wsl --install`; enabling the
features needs administrator access and a restart, so the machine's owner must approve that step. Run the build commands below inside the Linux terminal and keep the checkout in
the [Linux filesystem](https://learn.microsoft.com/en-us/windows/wsl/filesystems), such as `~/src/onslaught-ce`,
rather than `/mnt/c`. Your installed game's read-only inputs can be reached on its mounted Windows drive through
`BEA_GAME_DIR`. Test the resulting Windows zip on Windows; WSL compilation or a Linux run does not prove native
Windows graphics, input or audio. The Linux build route is established; this WSL onboarding recipe has not been
run in the Windows test VM.

Compilation reads two files from your own licensed game: `data/MissionScripts/onsldef.msl` and
`data/MissionScripts/text/text.stf`, checked against `config/game-data.sha256`. Set `BEA_GAME_DIR` to the installation
root if it is not in Steam's default folder. The full installation is needed to play. Do not commit or share these files.

Start with a no-game baseline. These authored tooling checks need no game files and no dependency download, only the
programs listed above (the last compiles a small C driver with `cc`, or the compiler `CC` names). They are the whole
no-game baseline:

```sh
python3 port/tools/build_tests.py
python3 port/tools/deps_tests.py
python3 port/tools/package_readme_tests.py
python3 port/tools/manual_update_package_tests.py
```

Then prepare the pinned dependencies and compile:

```sh
python3 port/tools/deps.py build                   # the pinned toolchains, libraries and Linux sysroot, checked by SHA-256
python3 port/build.py --public --target linux --ce-version dev
python3 port/build.py --public --target windows --ce-version dev
```

The first dependency setup downloads roughly 3.2 GiB (an estimate; `port/deps.lock` records no sizes) before extraction
and compilation. Budget additional disk space for both toolchains and their build trees; elapsed time depends on your network and machine. Subsequent
builds reuse the ignored `local-data/port/` dependencies of your clone's main working tree, which every Git worktree of
that clone shares (or the folder `BEA_PORT_DATA` names).

After building both targets, stage a self-contained development package:

```sh
python3 port/tools/package.py dev
ce_dev_dir=$(mktemp -d "$PWD/local-data/port/dev-run.XXXXXX")
tar -xzf local-data/port/release/dev/onslaught-ce-dev-linux-i686.tar.gz -C "$ce_dev_dir"
```

The extracted `onslaught-ce-dev-linux-i686/onslaught-ce` launcher is the entry point. Use a separate absolute
`BEA_USER_DIR` for development careers and settings. The Windows zip is in the same release output directory.
Keep these local packages separate from installed releases; never publish a development package as an accepted release.
Follow [CONTRIBUTING.md](CONTRIBUTING.md) to baseline, develop and submit a feature from your fork.

`--ce-version` sets the version the menu and `--version` show; without it the build calls itself
"vdev". It also sets the Windows launcher's file and product version strings. Their numeric fields use one to four
release-number parts, each from 0 to 65535; `-prerelease` and `+build` suffixes stay in the strings. Named development
builds such as `dev` use numeric 0.0.0.0. The game's `bea.exe` keeps its separate retail 1.0.0.0 resource.

`python3 port/tools/build_tests.py` checks launcher version parsing and resource linking without building the game.

After completing the Linux build, `python3 port/tools/audio_eof_tests.py` checks the actual Ogg reader and music
worker with generated audio and controlled buffer endpoints. It also needs a host `ffmpeg` command with the
`libvorbis` encoder; the pinned decoder-only FFmpeg libraries do not provide that command. This check does not
launch the game or open an audio device.

- `port/deps.lock` pins every downloaded input by URL and SHA-256.
- One input comes prebuilt: libvorbis, built with the game's original compiler. It is fetched from this repository's
  release assets, with its source and build script. The dependency setup downloads that prebuilt library; rebuilding
  it needs Wine and the pinned Visual C++ 6 toolchain, which this repository does not supply (`port/vorbis/README.md`).
- `port/README.md` describes the build in detail: the compiler flags, the platform layer and the patches the port
  applies to `src/`.

### Experimental network source builds

The default `--public` build defines `CE_NETWORK_OFF` and leaves networking out. Use a separate clean Git checkout
or worktree for each experimental flavour so its objects and build receipts stay together. Add `--with-network` to
the public build commands above for the experimental two-player Network Game page and direct-IP transport. Direct-IP
sessions contact the other player's UDP address; the game has no matchmaking server.

The further `--with-ice` opt-in compiles the experimental relay client. Prepare the ordinary dependencies first, then
run these commands in that flavour's checkout, using the same dependency cache and the default pinned `sniper` profile:

```sh
CE_REVIEW_VERSION=0.0.0-ice-review
python3 port/tools/ice_deps.py build all
python3 port/build.py --public --with-network --with-ice --target all --ce-version "$CE_REVIEW_VERSION" -j 8
```

`port/ice-deps.lock` pins the libjuice archive and required MPL-2.0 relay-policy patch by URL and SHA-256. The optional
recipe builds a static client library; ordinary dependency setup does not fetch libjuice. See
[THIRD-PARTY.md](THIRD-PARTY.md#only-in-an-explicit-ice-build) for its source and licence requirements.

An ICE-enabled build still uses direct-IP mode unless private relay directories are explicitly selected. Normal relay
mode contacts the supported `turn.cloudflare.com` UDP endpoints on ports 3478 or 443 from its protected grant; session
traffic follows the selected TURN relay. No relay grant is configured by the build, and no provider account, provider
key or TURN credentials are embedded. There is no automatic broker or cross-machine record delivery. The current
client uses UDP; it has no TURN-over-TCP/TLS fallback or credential renewal. **Windows ICE requests are refused before
private-directory I/O**; Windows direct-IP mode remains separate.
The [ICE bridge guide](port/network/ICE-BRIDGE.md#experimental-game-caller) describes the directory selections, bounded
session lifetime and current acceptance limits; [relay access](port/network/RELAY-ACCESS.md) describes the separate
operator setup. Building this flavour does not establish real-player or cross-machine gameplay acceptance.

### Local ICE review packages

Use a clean reviewed source commit, a separate flavour checkout and its own build outputs. Build against an isolated
read-only copy of your game data selected by `BEA_GAME_DIR`, with no game running. Both target builds must finish with
the same review version, ordinary SDL link receipts and the patched libjuice dependency/link receipts. Keep the pinned
compilers and default `sniper` profile; system-compiler, host-sysroot, `--test-automation` and `--commander-test` builds
cannot stand in for this profile. If dependencies are shared, prepare an owned `BEA_PORT_DATA` overlay rather than
changing inputs that another build uses.

After the commands above finish, choose a fresh ignored output directory for this one local review:

```sh
python3 port/tools/package.py "$CE_REVIEW_VERSION" --with-ice \
  --build local-data/port/build --out local-data/ice-review-package
```

This command checks both targets and creates a Linux tarball, Windows zip, FFmpeg source archive and unstripped
symbols in the selected output directory. The ICE packages carry the exact libjuice archive, patch, reconstruction
instructions and licence notices. Without `--with-ice`, an ICE binary is refused; `--test` is not allowed on this
route. Retain any partial output and the command transcript; use a new output directory for another separately
reviewed attempt.

These are local review artifacts: the example version has no public source tag or signed update feed. The command
does not run the game, sign a feed, upload or publish. Keep the archives out of the shared latest-release feed; it is
not an experimental update channel. Any later review run needs a fresh extraction, isolated user data and update
checks disabled using the [existing launcher controls](#updates-and-privacy). The Windows archive retains the runtime
ICE refusal.

Local creation does not clear an unpassed or held package check. Distribution additionally needs the exact flavour
rebuilt twice from its exported source in clean independent build roots, byte-identical complete archives, the
applicable package/source and platform gates, and real game-process acceptance for every advertised route. Default
flavour release checks do not certify an ICE build. This preparation path makes no package or gameplay acceptance
claim.

## Licence

Onslaught CE is licensed under **GPL-3.0-only** (see [COPYING](COPYING), [NOTICE](NOTICE) and
[REUSE.toml](REUSE.toml)). The upstream Onslaught source ships the GPLv3 text without a separate “or later” grant; we treat it, and license
our work, as GPL-3.0-only. Third-party components keep their own licences; see [THIRD-PARTY.md](THIRD-PARTY.md). The
FFmpeg libraries ship as replaceable shared libraries, and their source tarball is attached to each release.

The public graphics host is the GPL-3.0-only implementation in `port/platform/public/d3dapp.cpp` and `.h`.
The inherited SDK sample framework is excluded from this source tree and from the public build.

The game's data is not part of this project and is not covered by its licence.

## What is in this repository

This repository is generated: its `main` is written by an export from the maintainers' working source, so the source
folders below change only through that export (see [CONTRIBUTING.md](CONTRIBUTING.md)).

The source has three parts:

- **The upstream GPL Onslaught source** ([stuart73/Onslaught](https://github.com/stuart73/Onslaught)).
- **A byte-matching decompilation** that reconstructs more of the PC code
  ([onslaught-decomp](https://github.com/dlprentice/onslaught-decomp)), which records its measured matching coverage
  and reconstruction concessions. The native port uses a different compiler and replacement platform code.
- **A platform layer**: SDL3, with dxvk-native on Linux and the system's Direct3D 9 on Windows.

| Folder | What it holds |
| --- | --- |
| `src/` | the game's C++ source, from the decompilation of the original; its line counts are fixed because the game's own build depends on them |
| `linklib/` | extra definitions and second names for symbols the link needs, kept apart from `src/` |
| `port/` | the native port: the build script, the Linux and Windows platform layer, the launcher and updater, patches applied to a generated copy of `src/`, replacement libraries and the checks ([port/README.md](port/README.md)) |
| `config/` | SHA-256 pins for inputs the build verifies: the two game files it reads and the original compiler's files used to rebuild one library |
| `LICENSES/` | the licence texts that [REUSE.toml](REUSE.toml) names |

The main files at the top level: [CONTRIBUTING.md](CONTRIBUTING.md) (where each kind of work goes and how a pull request is
taken in), [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md), [SECURITY.md](SECURITY.md), [THIRD-PARTY.md](THIRD-PARTY.md) (the components built into or shipped with the
program), [NOTICE](NOTICE), [COPYING](COPYING) and [AGENTS.md](AGENTS.md) (the rules for automated agents).

Some comments and guides under `port/` and `src/` name files, notes and tools that live only in the maintainers' working
source and are not in this tree; [port/README.md](port/README.md) says which.

## Contributing, questions and security

Questions and ideas go to this repository's [Discussions](https://github.com/dlprentice/onslaught-ce/discussions).
Problems go to its [Issues](https://github.com/dlprentice/onslaught-ce/issues) page (use the problem report form).
Security problems are reported privately, as [SECURITY.md](SECURITY.md) describes.

[CONTRIBUTING.md](CONTRIBUTING.md) says which public repository takes which kind of work, what a first contribution
needs and how a pull request is taken in. Contributions are GPL-3.0-only, and never include game files.
Commander, Video Options, camera feel and the planned network play, map editor and museum are changing quickly, and the
maintainers' working source has unreleased changes in these areas that this tree does not show yet: open an issue
before starting larger work there.

## Credits and special thanks

- **Lost Toys**, for the original game
- **Stuart Gillam**, for his help, insight and guidance
- **Jeppi** and **BermudaMaster** on Discord, and every other early tester, for testing and helpful reports
- **The Onslaught CE contributors**, including everyone who contributed work made with their own AI coding agents
- **The Battle Engine Aquila community and preservation projects**, including everyone testing, reporting issues
  and sharing fixes

The **Onslaught CE → Special Thanks** menu carries these acknowledgements in the game. The original game's
**Credits** remain separate.

## Related projects

- [onslaught-decomp](https://github.com/dlprentice/onslaught-decomp): the byte-matching decompilation of the game's PC
  code, and the tools that judge the match.
- [onslaught-godot](https://github.com/dlprentice/onslaught-godot): a rebuild of the game in Godot (a proof of concept).
- [onslaught-preservation](https://github.com/dlprentice/onslaught-preservation): documentation of the game, its
  formats, behaviours and history.
- [Onslaught-Career-Editor](https://github.com/dlprentice/Onslaught-Career-Editor): companion app for saves, settings
  and lore; to be renamed onslaught-companion.
- [stuart73/Onslaught](https://github.com/stuart73/Onslaught): upstream GPL source for the game.
