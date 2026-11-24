#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check both generated package READMEs using authored files; no game, release binaries or network."""
import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
# The project README: public/README.md in this repository, README.md at the root of the exported tree.
PROJECT_README = ROOT / "public/README.md" if (ROOT / "public/README.md").is_file() else ROOT / "README.md"
spec = importlib.util.spec_from_file_location("ce_package_readme", HERE / "package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageReadmeTests(unittest.TestCase):
    def test_generated_guide_describes_current_menu_routes_and_prototype_limits(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                guide = " ".join(package.readme("source-doc-check", windows).decode().split())
                for wording in ("Onslaught CE > Catalogue", "Find by name", "Clear filter",
                                "Collections remember separate filters",
                                "extras=0", "catalogue=0", "mods_menu=0", "Controller Options",
                                "Onslaught CE > Mods", "enable, disable and order",
                                "commander_tactical_map=0", "Commander and Take Unit",
                                "Native-model RTS", "Museum exhibits can disappear up close",
                                "player-start workflow and reported interaction lag remain unresolved",
                                "Windows compilation does not demonstrate its UI or text entry",
                                "Audio backend initialization alone does not demonstrate audible playback"):
                    self.assertIn(wording, guide)
                for obsolete in ("There is no mod menu yet", "V there switches to the paused map and back",
                                 "general mod support"):
                    self.assertNotIn(obsolete, guide)

    def assert_player_guide(self, content, windows):
        text = content.decode("utf-8")
        flat = " ".join(text.split())
        self.assertTrue(text.startswith("Onslaught CE V0.1.10\n"))
        self.assertNotIn("\r", text)
        self.assertLessEqual(max(map(len, text.splitlines())), package.README_WIDTH)
        for phrase in (
            "Main Menu > Onslaught CE: Bonus Levels, Extras, Mods, Catalogue, CE Settings, Cheats, Experimental Features, Help & Info, Report a Problem and Grand Museum (each optional feature is included by default and can be toggled in Experimental Features or ce.ini)",
            "Each row has a hint under the list", "The Options page is the original's",
            "What's Coming (planned features and their limits; it does not start those features)",
            "it does not start those features", "Aquila Prototype is the original playable-demo level (201)",
            "Building Defence is a two-player local split-screen defence map", "which are the same map, so the list has it once",
            "each comes back to this list with its row highlighted",
            "Race Challenge 1 to 5", "Special Thanks", "The game's Credits remain separate",
            "CE Settings: a list of choices, each written to ce.ini in your user folder when you make it",
            "Help & Info: Controls & Tips", "Updates (how updating works and how to turn the checks off)",
            "Onslaught CE > Cheats: the game's own cheats as On/Off switches", "Unlock All Levels (original code TURKEY)",
            "the Pause Menu also has a Photo Mode row", "The row then reads End Photo Mode",
            "single-player time trials (levels 901 to 905)", "those routes save your career as the original does",
            "Race Challenge 1 after grades of C or better in 26 campaign levels",
            "each later race when the one before it is finished fast enough",
            "(under 145, 195, 150 and 105 seconds for races 1 to 4; the fifth opens nothing more)",
            "saves nothing and returns to the Bonus Levels list",
            "two-player local split-screen defence map (level 856)", "Multiplayer start page", "player 2 uses the second set of keys",
            "Plug in both gamepads before starting",
            "Settings can still be saved during play, including into a career file; bonus-level progress is left out",
            "Console: during an eligible single-player level, press the grave key", "type help and press Enter",
            "Esc or grave closes it; the game does not pause", "unavailable in multiplayer, demos, recordings and replays",
            "Video Options > Window mode offers Windowed or Fullscreen", "Choose Apply to switch without restarting",
            "the row reads (not saved) and the change is for this session only", "-forcewindowed game argument takes priority",
            "saved display-mode choice is ignored while recording or replaying",
            "eligible ordinary single-player levels with widescreen modes enabled", "HUD automatically scales up",
            "BEA_PORT_HUD_SCALE=0", "off in recordings and replays, split-screen multiplayer, and when BEA_PORT_WIDESCREEN=0",
            "These switches are off in recordings and replays", "Typing a code into a career's name still works",
            "wheel down zooms in and wheel up zooms out", "right bumper zooms in and left bumper out",
            "Direct gamepad and flight-stick input is disabled during recording and replay",
            "An older tape made with live controller input cannot recover it and may replay differently",
            "By default, recording targets 30 frames a second for play and uses the replay clock", "The larger HUD, console",
            "the saved display mode are off", "tapes folder beside ce.ini", ".tape and a .tape.json",
            "encoded copies of some user-folder files", "game options, careers and possibly other files",
            "Session recording does not capture video, game audio or microphone audio",
            "Both files can contain personal information; review them before sharing", ".incomplete file",
            "BEA_USER_DIR can select another folder", "it puts record_sessions=1 in ce.ini in your user folder",
            "update check sends requests for the public feed and its signature",
            "Physical audio devices and listening quality have not been validated",
            "Only virtual devices have been tested; physical controllers and Windows drivers remain open",
            "Windows reparse-point behaviour is unmeasured",
            "Mods (experimental): on Linux and Windows, Onslaught CE can read replacement game files",
            "Onslaught CE > Mods is enabled by default",
            "A save that fails now says so", "An error occurred during saving.",
            "after a language change in the front end it appears in the usual message box",
            "Sharper text is off by default: the menus' and HUD's text keeps the original's soft look",
            "Choose Sharper Text: Yes in CE Settings (sharp_ui=1 in ce.ini) to turn it on from the next start",
            "with widescreen modes enabled on a non-4:3 picture",
            "bitmap text drawn at least twice its original size horizontally or vertically (for example, menu text at 1920 x 1080)",
            "In ordinary use it does not apply in recordings and replays, on 4:3 screens or when BEA_PORT_WIDESCREEN=0",
            "a valid BEA_PORT_WIDE_TEST=WIDTHxHEIGHT forces wide modes even during recording or replay and when "
            "BEA_PORT_WIDESCREEN=0. Unset it before making a faithful reference tape",
            # Your own music, which the game's Your Music dialog sends the player to (the review of bbcdb864: README.txt had no such part)
            "Your own music: a music_dir= line in ce.ini, with the absolute path of a folder (on Linux ~/ is your home folder), "
            "plays the folder's tracks in place of the level music",
            "A track must be Ogg Vorbis, stereo, 44100 Hz, with a full path of at most 255 bytes",
            "Music Order in CE Settings (music_playtype) sets how they play",
            "Both lines are read once a session, so a change applies the next time you start the game",
            "the console command music adddir <folder> adds a folder's tracks without changing ce.ini",
            "Your own music is off in recordings, replays and demos",
            "the next time the console opens for Console Text",
        ):
            with self.subTest(phrase=phrase, windows=windows):
                self.assertIn(phrase, flat)
        # The photo-mode contexts are the public README's, word for word (the 0.1.10 reviewers found the three texts differing).
        self.assertIn("Both work in single-player levels, but not while a session is recorded or replayed, in multiplayer "
                      "or a demo.", flat)
        # The sharper text is off unless sharp_ui=1 (0.1.13); no text may still promise it on wide screens by default.
        self.assertNotIn("Onslaught CE draws it from a sharper copy", flat)
        if windows:
            for phrase in ("Run onslaught-ce.exe", "%APPDATA%\\Onslaught CE", "onslaught-ce.exe --record",
                           "Windows builds have been checked under Wine on Linux", "Windows 11 virtual machine",
                           "this version's release notes list the routes checked for it",
                           "Physical Windows graphics cards and drivers remain unvalidated for this version"):
                self.assertIn(phrase, flat)
            self.assertNotIn("This Windows build was tested", text)
            self.assertNotIn("Linux careers:", flat)
            # Dated wording the reviewers flagged: nothing says "earlier" builds, and the retired updater's 0.1.3 observation is gone.
            for phrase in ("Earlier Windows builds", "During the 0.1.3 release checks", "retired 0.1.0-0.1.6 updater"):
                self.assertNotIn(phrase, flat)
        else:
            self.assertIn("Run ./onslaught-ce", flat)
            self.assertIn("./onslaught-ce --record", flat)
            self.assertIn("$XDG_DATA_HOME/onslaught-ce", flat)
            self.assertNotIn("Run onslaught-ce.exe", flat)
            # Linux requirements: the optional tools and libraries the launcher uses when present (the 0.1.10 reviewers' D11).
            # The preflight notes the X11 libraries on an X11 session and libdecor on a Wayland one (package.py's preflight text).
            self.assertIn("Optional: curl (the update check and download use it), zenity (SDL shows the launcher's message "
                          "boxes with it, or in its own window without it), on an X11 session the 32-bit libXrandr, libXcursor, "
                          "libXi and libXfixes (SDL works without them, with fewer display features) and on a Wayland session "
                          "the 32-bit libdecor (without it the windows have no title bar on some desktops). The launcher only "
                          "notes a missing library.", flat)
            sessions = package.PREFLIGHT.split('if [ -n "${WAYLAND_DISPLAY:-}" ]; then', 1)[1].split("\nfi\n", 1)[0]
            wayland, x11 = sessions.split("\nelse\n", 1)
            self.assertIn("libdecor-0.so.0", wayland, "the preflight notes libdecor on a Wayland session")
            for library in ("libXrandr.so.2", "libXcursor.so.1", "libXi.so.6", "libXfixes.so.3"):
                self.assertIn(library, x11, f"the preflight notes {library} on an X11 session")
                self.assertNotIn(library, wayland)
            for phrase in ("A career whose name is longer than 31 characters is left out of the Load Game list. Keep its file and ask for help.",
                           "the older spelling can be chosen from the original folder; keep both"):
                self.assertIn(phrase, flat)
        return text

    def test_the_dodge_instructions_wrap_as_one_bullet(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                lines = package.readme("0.1.14", windows).decode().splitlines()
                start = next(i for i, line in enumerate(lines) if line.startswith("- Dodging in Walker mode"))
                end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith("  Dodge Button:"))
                paragraph = lines[start:end]
                self.assertIn("tap A then D for right, or D then A for left.", " ".join(" ".join(paragraph).split()))
                self.assertTrue(all(len(line.split()) > 1 for line in paragraph))

    def test_the_first_screen_says_what_it_is_how_to_start_and_where_the_upgrade_warning_is(self):
        """Fix 5 of the 0.1.14 polish list: the first 24 lines hold the title, what Onslaught CE is, the quick start, where the port's
        pages and the player's careers are, and a pointer to the upgrade warning; the warnings themselves are all still there, under
        their own heading, and the retired wording is gone."""
        for windows in (False, True):
            with self.subTest(windows=windows):
                text = package.readme("0.1.14", windows).decode()
                first = " ".join(" ".join(text.splitlines()[:24]).split())
                for phrase in ("Onslaught CE V0.1.14", "you need your own copy of the game",
                               ("1. Extract the download into a new, empty user-writable folder of its own" if windows else
                                "1. Extract the download into a new, empty folder of its own, not inside the game's folder."),
                               "3. It finds your Steam copy of Battle Engine Aquila by itself, or asks once where it is.",
                               "In the game, Main Menu > Onslaught CE holds what this port adds: Bonus Levels, Extras, Mods, Catalogue, CE Settings, Cheats, Experimental Features, Help & Info, Report a Problem and Grand Museum.",
                               "apart from the original game's folder.",
                               "Upgrading from Onslaught CE 0.1.0 to 0.1.6? Do not start the old launcher again. "
                               'Read "Upgrading from 0.1.0-0.1.6" below first.'):
                    self.assertIn(phrase, first)
                self.assertIn(("%APPDATA%\\Onslaught CE" if windows else "~/.local/share/onslaught-ce"), first)
                self.assertNotIn("BEA_", first, "no environment variable on the first screen")
                self.assertNotIn("launcher safety hold", text)
                after = text.split("\nUpgrading from 0.1.0-0.1.6\n", 1)[1].split("\nAdvanced\n", 1)[0]
                flat = " ".join(after.split())
                for warning in ("Do not start an Onslaught CE 0.1.0-0.1.6 launcher at all, not even to check its version or update it",
                                "startup cleanup can delete neighbouring <program folder>.old-* paths",
                                "On Linux it has followed a matching directory link into its target",
                                "Windows reparse-point behaviour is unmeasured",
                                "Keep the old installation and every .old-*, .download-* and .unpack-* path untouched",
                                "Locate and preserve both launcher and game user-data folders",
                                "Copy each whole folder to an independent backup; verify the copies and careers",
                                "If the locations or conflicting files are unclear, do not merge or overwrite them; seek help first",
                                "This release's launcher removes no neighbouring paths"):
                    self.assertIn(warning, flat)
                advanced = text.split("\nAdvanced\n", 1)[1].split("\nIn the game\n", 1)[0]
                for phrase in ("BEA_GAME_DIR", "BEA_USER_DIR", "--update-only", "BEA_NO_UPDATE_CHECK=1"):
                    self.assertIn(phrase, advanced)
                for retired in ("--apply is refused", "for its built-in UCRT", "Record Play for Bug Reports", "Stop recording",
                                "Updates & Rollback", "Developer Information, Your\n", "on a non-4:3 picture,\n", "solo", "Main menu",
                                "pause menu", "Goodies page", "level select", "launch Onslaught CE"):
                    self.assertNotIn(retired, text)

    def test_report_log_selection_and_user_folder_fallback(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                text = package.readme("0.1.10", windows).decode("utf-8")
                report = text.split("\nReport a problem\n", 1)[1].split(
                    "\nHelp make Onslaught CE more faithful\n", 1)[0]
                flat = " ".join(report.split())
                for phrase in (
                    "The usual file is onslaught-ce.log",
                    "onslaught-ce.old.log, when present, holds the previous primary log",
                    "If multiple copies ran at once, the affected run may be in "
                    "onslaught-ce.concurrent-1.log or onslaught-ce.concurrent-2.log",
                    "Check its start time to identify the run and save it promptly; "
                    "later starts can replace log files",
                    "Nothing is posted until you submit the issue",
                    "look the file over before sharing it",
                    "You can share it directly with someone helping you",
                ):
                    with self.subTest(phrase=phrase):
                        self.assertIn(phrase, flat)
                whole = " ".join(text.split())
                if windows:
                    self.assertNotIn("$XDG_DATA_HOME", whole)
                else:
                    self.assertEqual(whole.count("when unset or empty"), 2)

    def test_readme_names_the_experimental_block_and_the_new_profile_video_defaults(self):
        """What a player finds without asking: the [Experimental] block the first start writes into ce.ini, and High-detail units On,
        3D distant trees Off, and Terrain detail distance Extended on a new profile. public/README.md has both; the README.txt in the
        download must too, and the facts it states are the ones the code holds (the block's header and warning are ce.cpp's, the fresh
        rows are ce_graphics_policy.cpp's)."""
        policy = (ROOT / "port/game/ce_graphics_policy.cpp").read_text()
        self.assertIn("const int kFreshUnitModels = 1;", policy, "High-detail units starts On")
        self.assertIn("const int kFreshTreeModels = 0;", policy, "3D distant trees starts Off")
        self.assertIn("const int kFreshTerrainDetail = 1;", policy, "Terrain detail distance starts Extended")
        ce = (ROOT / "port/platform/common/ce.cpp").read_text()
        self.assertIn('CE features, enabled by default. Some are still in progress; set a line to 0 or toggle in Experimental Features to turn one off.', ce)
        public = " ".join(PROJECT_README.read_text().split())
        self.assertIn("under `[Experimental]` in `ce.ini`", public)
        self.assertIn("**High-detail units** starts at **Yes**", public)
        self.assertIn("**3D distant trees** starts at **No**", public)
        self.assertIn("**Terrain detail distance** starts at **Extended**", public)
        self.assertIn("The visible range and fog stay the same.", public)
        self.assertIn("**Screen mode** selects the resolution; **Video adapter** selects the graphics device.", public)
        phrases = (
            "Video Options on a new profile: the first time you start with no saved options of your own",
            "every quality row starts at its highest choice except 3D distant trees",
            "anti-aliasing starts at up to 4x MSAA (or the highest lower level your graphics device lists)",
            "High-detail units starts at Yes",
            "3D distant trees starts at No",
            "largest cost in frame rate",
            "Terrain detail distance starts at Extended",
            "keeps finer ground detail up to twice as far away. The visible range and fog stay the same.",
            "Choose Normal for the original terrain detail",
            "Screen mode selects the resolution; Video adapter selects the graphics device.",
            "If the row says (restart the game), restart to recover the display or input.",
            "Anything you saved before is kept as it was",
            "Unfinished features: the first start adds an [Experimental] block to ce.ini that lists them, each set to 1 (enabled by default).",
            "Some are still in progress and may change or break; toggle any feature in Onslaught CE > Experimental Features or set its line to 0 in ce.ini to turn it off from the next start.",
            "A line set in an earlier version is kept, but what it turns on has changed:",
        )
        for windows in (False, True):
            with self.subTest(windows=windows):
                flat = " ".join(package.readme("0.1.10", windows).decode("utf-8").split())
                for phrase in phrases:
                    self.assertIn(phrase, flat)

    def test_generated_text_has_current_player_paths_and_limits(self):
        with patch.object(package.subprocess, "run", side_effect=AssertionError("README generation must not run a process")):
            for windows in (False, True):
                with self.subTest(windows=windows):
                    text = self.assert_player_guide(package.readme("0.1.10", windows), windows)
                    # Compare the displayed menu labels to the actual CE menu, not just this generator's list: the rows of the
                    # pages' tables (a label and its action) and the cheat switches' labels.
                    front_end = (ROOT / "port/game/ce_frontend.cpp").read_text()
                    labels = set(re.findall(r'\{L"([^"]+)", CEA_', front_end))
                    cheats = {re.sub(r" \(.*", "", name) for name in re.findall(r'\{L"([^"]+?):\s?", \w+, \{', front_end)}
                    self.assertEqual(len(cheats), 5)
                    flat = " ".join(text.split())
                    for label in ("Bonus Levels", "CE Settings", "Cheats", "Help & Info", "Report a Problem", "Aquila Prototype",
                                  "Building Defence", "Race Challenge 1", "Controls & Tips",
                                  "What's Coming", "Updates", "About Onslaught CE", "Special Thanks"):
                        # the page's row is "Updates & Rollback" until the CE pages carry the new name (lane A's fix 20)
                        self.assertTrue(label in labels or label + " & Rollback" in labels, label)
                        self.assertIn(label, flat)
                    for label in cheats:
                        self.assertIn(label, flat)
                    # CE Settings is its table: every row's label is named in the generated README, and so is each place its
                    # line is described (the keys the README gives for a hand edit are the table's own)
                    table = front_end[front_end.index("static const SCeSetting\tkSettings[]"):front_end.index("#define CE_NUM_SETTINGS")]
                    rows = re.findall(r'^\t\{L"([^"]+?):\s?", "(\w+)"', table, re.M)     # "Label: " or "Label:" (the colon touches the value)
                    self.assertTrue(rows)
                    # the Commander Map row is behind the commander line under [Experimental] in ce.ini (off by default): the README does not offer it
                    for label, key in rows:
                        label = {"Record Play for Bug Reports": "Session Recording"}.get(label, label)    # the name the README uses
                        if key == "commander":
                            self.assertNotIn(label, flat)
                            continue
                        self.assertIn(label, flat)
                    # Commander is named only where the README says what an older commander=1 line now does (the Unfinished features
                    # item), never as something this version offers
                    unfinished = flat[flat.index("- Unfinished features:"):flat.index("- Console:")]
                    self.assertNotIn("ommander", flat.replace(unfinished, ""))
                    self.assertIn("Commander (M by default)", unfinished)
                    self.assertIn("commander_tactical_map=0", unfinished)
                    self.assertIn("still in progress", unfinished)
                    for key in ("hud_scale", "hud_anchor", "sharp_ui", "update_check", "record_sessions", "music_dir",
                                "music_playtype"):
                        self.assertIn(key, [row[1] for row in rows])
                        self.assertIn(key, flat)
                    self.assertIn("Session Recording", flat)
                    self.assertNotIn("Record Play for Bug Reports", flat)

    def test_the_game_s_readme_pointers_name_parts_of_the_readmes(self):
        """A named part of the README must exist on both platforms; project pointers must reach the public guide."""
        source = re.sub(r'"\s*"', "", (ROOT / "port/game/ce_frontend.cpp").read_text())
        # Menu text uses protected spaces and manual line breaks. Read the words as the player sees them.
        source = source.replace(r"\xa0", " ").replace(r"\n", " ")
        named = set(re.findall(r'([A-Z][a-z]+(?: [a-z]+)*) in the (project )?README', source))
        for project, name in re.findall(r'\b(project )?README\b[^."\n]{0,90}?\bunder ([A-Z][a-z]+(?: [a-z]+)*)[.]', source):
            named.add((name, project))
        self.assertTrue({("Your own music", ""), ("Updates", ""), ("Mods", "")} <= named, named)
        public = PROJECT_README.read_text()
        for name, project in sorted(named):
            with self.subTest(name=name):
                if project:
                    self.assertRegex(public, r"(?m)^#+ " + re.escape(name) + r"\b", "the project README has the part as a heading")
                    continue
                for windows in (False, True):
                    text = package.readme("0.1.10", windows).decode("utf-8")
                    self.assertRegex(text, r"(?m)^(?:- )?" + re.escape(name) + r"(?: \([^)]+\))?:", "README.txt has the part the game names")
        # and the dialog's own part says what the dialog leaves out: the line, the folder's form and the tracks that can play
        for windows in (False, True):
            flat = " ".join(package.readme("0.1.10", windows).decode("utf-8").split())
            part = flat[flat.index("- Your own music:"):]
            part = part[:part.index(" - ", 3)]
            for word in ("music_dir=", "absolute path", "~/", "Ogg Vorbis, stereo, 44100 Hz", "255 bytes", "music adddir"):
                self.assertIn(word, part)

    # What the Network game page says, as the README.txt of the build that offers it must repeat: the label, the requirements, what a mismatch does,
    # the missing matchmaking server, what was not tested and what the game contacts.
    NETWORK_PHRASES = (
        "Network game (experimental)", "Main Menu > Onslaught CE > Network Game", "new and unfinished", "expect problems",
        "the same Onslaught CE version and their own copy of the game", "If games ever disagree, the game ends with a message.",
        "A network game does not change your careers.", "2 to 4 player Co-Op or Versus game",
        "Online Play lobby rooms with ICE/TURN relay",
        "the host forwards a UDP port on its router (net_port in ce.ini, default 27652)",
        "Not tested: physical Windows hardware on every router/ISP.",
        "contacts no server", "on Windows that asks the system to look up this PC's own name",
        "Your own music, the Dodge Button, Photo Mode and F12 screenshots are off in a network game.",
        "Planned, not in this version yet: additional HUD options and further online match polish.")
    PLANNED_DEFAULT = "- Planned, not in this version yet: Internet multiplayer and additional HUD options."
    PLANNED_NETWORK = "- Planned, not in this version yet: additional HUD options and further online match polish."

    def test_network_flavour_readme_carries_the_network_game_labels_and_warnings(self):
        # The page's strings as the compiler joins them (adjacent literals), so the two texts are compared sentence by sentence.
        page = " ".join(re.sub(r'"\s*\n\s*"', "", (ROOT / "port/game/ce_netpages.cpp").read_text()).split())
        project = " ".join((ROOT / "port/README.md").read_text().split())
        not_tested = "Not tested: physical Windows hardware on every router/ISP"
        mismatch = "If the two games ever disagree, the game ends with a message."
        careers = "does not change your careers."
        # The page says what a player needs and keeps test words off it (house style W3); the README of this build keeps
        # the experimental warning and what was not tested.
        # The lobby no longer carries standing prose (house style: unfinished limits are not apologised for on a page);
        # the README of the build keeps it.
        self.assertNotIn(mismatch, page)
        self.assertIn(mismatch.rstrip("."), project)
        self.assertIn("A network game " + careers, page)
        self.assertIn('#define CE_LOBBY_HELP_LABEL\t"Connection Help"', (ROOT / "port/platform/common/ce_netlobby.h").read_text())
        self.assertIn('row "Connection Help"', " ".join(package.NETWORK_GAME_LINES))
        self.assertNotIn("Not tested", page)
        self.assertIn(not_tested, project)
        for windows in (False, True):
            with self.subTest(windows=windows), patch.object(package, "NETWORK", True):
                text = self.assert_player_guide(package.readme("0.1.10", windows), windows)
                flat = " ".join(text.split())
                for phrase in self.NETWORK_PHRASES:
                    self.assertIn(phrase, flat)
                self.assertNotIn(self.PLANNED_DEFAULT, flat)

    def test_default_flavour_readme_has_no_network_game_paragraph(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                flat = " ".join(package.readme("0.1.10", windows).decode().split())
                for phrase in ("Network game (experimental)", "Online Play lobby", "Not tested: physical Windows"):
                    self.assertNotIn(phrase, flat)
                self.assertIn(self.PLANNED_DEFAULT, flat)

    # What only the build with the network compiled in has, by any of the names the READMEs have used for it. The default
    # download has no such page, key or mode, so its README.txt must not send a player looking for one: the 0.1.14 candidate's
    # README.txt said three features were off "in network games" (the reading before the freeze, finding S3).
    # The pattern names the thing as well as the old phrases, so a reworded mention ("online co-op", "over the internet") is caught too:
    # nothing in the default build is online, so the words online, internet and LAN are not in its README outside the planned line.
    COMPILED_OUT = re.compile(r"network(?:ed)?\s+(?:game|games|play|co-op|session)|\bhost(?:s|ing)?\s+(?:a|or)\b|\bjoin(?:s|ing)?\s+a\s+game|"
                              r"matchmaking|net_port|\bENet\b|\blobby\b|direct-IP|\bUDP\b|\bonline\b|\binternet\b|\bLAN\b|"
                              r"\bover\s+(?:a|the)\s+network\b|\bremote\s+players?\b|peer-to-peer|\bIP\s+address", re.I)

    def test_default_readme_names_nothing_that_is_compiled_out_of_the_default_build(self):
        front_end = (ROOT / "port/game/ce_frontend.cpp").read_text()
        # the rows the menu has only with the network compiled in, as the file guards them: their labels are checked too
        guarded = re.findall(r"#ifndef CE_NETWORK_OFF\n((?:(?!#endif|#else).*\n)*?)#endif", front_end)
        labels = {label for block in guarded for label in re.findall(r'\{L"([^"]+)", CEA_', block)}
        self.assertEqual(labels, {"Network Game"}, "the rows ce_frontend.cpp offers only with the network compiled in")
        self.assertRegex("It is off in network games.", self.COMPILED_OUT)
        self.assertRegex("a network game or a demo", self.COMPILED_OUT)
        for reworded in ("Photo mode is also off in online co-op.", "It is off over the internet.", "with a player on your LAN"):
            self.assertRegex(reworded, self.COMPILED_OUT)
        for windows in (False, True):
            with self.subTest(windows=windows):
                text = package.readme("0.1.10", windows).decode()
                planned = [line for line in text.splitlines() if line.startswith("- Planned, not in this version yet:")]
                self.assertEqual(planned, [self.PLANNED_DEFAULT], "the one line that may name what is not in this build")
                flat = " ".join(text.replace(self.PLANNED_DEFAULT, "").split())
                self.assertEqual(self.COMPILED_OUT.findall(flat), [])
                for label in labels:
                    self.assertNotIn(label.lower(), flat.lower())
                # and the control: the same text with the network compiled in is caught by the same pattern
                with patch.object(package, "NETWORK", True):
                    self.assertTrue(self.COMPILED_OUT.findall(" ".join(package.readme("0.1.10", windows).decode().split())))
        # The project README is for players of the download first: before its build sections it names no network game either
        # (what is off in one is said once, under "Experimental network source builds").
        public = PROJECT_README.read_text()
        players, builders = public.split("\n## Building from source\n", 1)
        self.assertEqual(re.findall(r"(?i)network\s+games?", players), [])
        # by the same pattern, outside the line that lists what is planned and the lines that say they are about an experimental
        # network source build (the Mods section names what such a build switches off)
        unplanned = " ".join(line for line in players.splitlines()
                             if not line.startswith("- Planned, not in this version yet:") and "experimental network source build" not in line)
        source_notice = "- Release 0.1.13 does not offer Internet multiplayer. This source branch has opt-in 2 to 4 player network test builds; that does not establish a released Internet service. Additional HUD options remain planned."
        unplanned = " ".join(unplanned.split())
        self.assertIn(source_notice, unplanned)
        unplanned = unplanned.replace(source_notice, "")
        self.assertEqual(self.COMPILED_OUT.findall(" ".join(unplanned.split())), [])
        self.assertIn("### Experimental network source builds", builders)
        section = " ".join(builders.split("### Experimental network source builds", 1)[1].split("\n### ", 1)[0].split())
        self.assertIn("in a network game of such a build the Dodge Button, the console, photo mode and F12 screenshots, your own "
                      "music, the end-of-level statistics and rumble are off", section)

    @unittest.skipUnless((ROOT / "port/PARITY.md").is_file(), "the parity log is not in the public tree; the maintainer's checkout runs this")
    def test_readme_measurements_follow_parity_s_latest_entries(self):
        """A README paragraph that quotes a measurement says what port/PARITY.md's latest entry says. The 0.1.14 candidate's
        project README still said two-player split screen "runs short" of terrain memory with Extended, which the reserve
        correction of 2026-11-08 had ended (finding S2)."""
        parity = (ROOT / "port/PARITY.md").read_text()
        correction = re.search(r"^\*\*Terrain reserve correction \(\d{4}-\d\d-\d\d\)\.\*\*.*(?:\n(?!\n\n).*)*", parity, re.M).group(0)
        self.assertIn("all three terrain tiers", correction)
        budget = re.search(r"fixed (\d+) MiB logical budget", correction).group(1)
        reserve = (ROOT / "port/game/ce_terrain_reserve.cpp").read_text()
        self.assertIn(f"constexpr std::size_t maximumOwnedBytes = {budget}u * 1024u * 1024u;", reserve, "PARITY's budget is the code's")
        self.assertRegex(correction, r"split-screen controls, the earlier roughly \d+ failed requests per frame fell to zero")
        self.assertIn("throughput overlapped the baseline variation", correction)
        self.assertIn("appearance, motion and native Windows execution remain unobserved", correction)
        public = PROJECT_README.read_text()
        paragraph = " ".join(re.search(r"^\*\*Extended terrain and its reserve\.\*\*.*(?:\n(?!\n).*)*", public, re.M).group(0).split())
        for superseded in ("runs short", "find no free memory", "nearest tier", "170 and 210", "215, one run", "a few megabytes"):
            with self.subTest(superseded=superseded):
                self.assertNotIn(superseded, paragraph)
        self.assertIn(f"({budget} MiB at most across all three terrain tiers", paragraph)
        self.assertIn("ran without a failed terrain request, and so did **two-player split screen** in the runs made", paragraph)
        self.assertIn("the frame rate with the reserve was within the variation between runs", paragraph)
        self.assertIn("nobody has compared how the terrain looks or moves in split screen", paragraph)
        self.assertIn("none of it has run on Windows", paragraph)
        # A stale figure in other words ("ran about 400 requests a frame short") is caught by what the paragraph may quote at all: its
        # only numbers are the budget and the two one-player levels measured, and the only failure it names is the one that did not happen.
        rest = paragraph.replace("(levels 100 and 200)", "", 1).replace("without a failed terrain request", "", 1)
        self.assertEqual(re.findall(r"\d+(?:\.\d+)?", rest), [budget], "the paragraph quotes no figure but the budget and the levels measured")
        self.assertEqual(re.findall(r"(?i)\bshort\b|\bfail\w*|\brequests?\s+(?:a|per)\s+frame|\bno\s+free\s+memory|\bran\s+out\b", rest), [])
        # Full models: the two figures the README quotes are one matched pair of PARITY's (the same level and terrain setting)
        quoted = re.search(r"level (\d+) fell from (\d+) to (\d+) frames a second with the same terrain setting", public)
        self.assertIsNotNone(quoted, "the README quotes one measured pair for Full models")
        level, off, on = quoted.groups()
        matched = re.search(r"on level " + level + r" it was: Full models On with Extended on ran at ([\d.]+) \([\d.]+ ms\) with 0\.0 failed "
                            r"allocations a frame, against (\d+) with Full models Off", parity)
        self.assertIsNotNone(matched, "PARITY holds the matched pair for that level")
        self.assertEqual((off, on), (matched.group(2), str(round(float(matched.group(1))))))
        for windows in (False, True):
            flat = " ".join(package.readme("0.1.10", windows).decode().split())
            for superseded in ("runs short", "find no free memory", "nearest tier"):
                self.assertNotIn(superseded, flat)

    def test_readmes_say_what_a_read_only_ce_ini_does(self):
        """0.1.14 refuses to replace a ce.ini whose owner made it read-only, where 0.1.13 on Linux replaced it (finding S7): the
        READMEs say so, with what the player then sees, and the launcher has the words they promise."""
        writer = (ROOT / "port/updater/ce_report.h").read_text()
        self.assertIn("readonly = !(st.st_mode & S_IWUSR);", writer)
        self.assertIn("readonly = (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;", writer)
        self.assertIn("if (readonly) { free(t); ce_settings_dir_close(&dir); ce_ini_refusal = CE_INI_REFUSED_READONLY; return 0; }", writer)
        launcher = (ROOT / "port/updater/ce_launch.c").read_text()
        for words in ("could not remember to skip this version, so it will offer the update again the next time it starts.",
                      "Session recording could not be turned off, so Onslaught CE will ask about it again the next time it starts.",
                      "is read-only, and Onslaught CE does not change a read-only file."):
            self.assertIn(words, launcher)
        says = ("A ce.ini you have made read-only is never replaced (0.1.13 on Linux did replace it): the rows keep their choices, and the "
                "launcher's Skip this version and Turn recording off say that the choice could not be saved and ask again the next time you start.")
        for windows in (False, True):
            self.assertIn(says, " ".join(package.readme("0.1.10", windows).decode().split()))
        self.assertIn(says, " ".join(PROJECT_README.read_text().replace("`", "").replace("**", "").split()))

    def test_readmes_say_what_an_older_commander_line_and_a_museum_visit_now_do(self):
        """What the READMEs say about the two unfinished features a player can already have met is what the code does: with
        commander=1 the M key opens the live view (the battle is not paused), where 0.1.13's opened the paused map; and a museum
        visit refuses every save, which the pause menu reports on each resume of a named career (findings S11 and S14)."""
        commander = (ROOT / "port/game/ce_cmdmap.cpp").read_text()
        key = commander[commander.index("bool PortCeCmdKey(unsigned key, bool repeat)"):]
        key = key[:key.index("\n}\n") + 3]
        self.assertIn("\tOpen(true);\n", key, "M opens the live view")
        opening = commander[commander.index("void Open(bool strategic)"):]
        self.assertIn("if (!strategic) { GAME.Pause(); }", opening, "only the map pauses the game")
        self.assertIn('} else if (!repeat && key == DIK_V && TacticalMapAllowed()) {\n                SwitchView(false);', commander)
        gate = (ROOT / "port/game/ce_project.cpp").read_text()
        self.assertRegex(gate, r"bool PortCeProjectCareerAllowed\(\) noexcept \{\n[^}]*!\(PortCeMuseumVisitActive && PortCeMuseumVisitActive\(\)\);")
        saves = "".join(path.read_text() for path in sorted((ROOT / "port/patches").glob("0139-*.patch")))
        self.assertGreaterEqual(saves.count("+    if (!PortCeProjectCareerAllowed()) return MCE_FAILURE;"), 3)
        commander_says = ("Press V for the paused tactical map (enabled by default; commander_tactical_map=0 turns it off).")
        museum_says = ("With museum=1, a Museum Prototype visit is kept out of your career: nothing is saved during it, so with a named "
                       "career the game's save error message appears each time you leave its Pause Menu. Nothing is wrong: the career is "
                       "as it was when the visit ends.")
        for windows in (False, True):
            with self.subTest(windows=windows):
                flat = " ".join(package.readme("0.1.10", windows).decode().split())
                self.assertIn(commander_says, flat)
                self.assertIn(museum_says, flat)
        public = " ".join(PROJECT_README.read_text().replace("`", "").replace("**", "").split())
        self.assertIn(commander_says, public)
        self.assertIn(museum_says, public)
        developer = " ".join((ROOT / "port/README.md").read_text().split())
        self.assertNotIn("the CE Settings row Commander Map: Yes) keeps the Commander map on", developer, "0.1.13 had no such row")
        self.assertIn("Now Commander opens the live view: the battle keeps running", developer)

    def test_the_network_flavour_changes_only_the_network_game_lines(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                default = package.readme("0.1.10", windows).decode().splitlines()
                with patch.object(package, "NETWORK", True):
                    network = package.readme("0.1.10", windows).decode().splitlines()
                self.assertEqual([line for line in network if line not in default], package.NETWORK_GAME_LINES + [self.PLANNED_NETWORK])
                self.assertEqual([line for line in default if line not in network], [self.PLANNED_DEFAULT])

    def test_windows_notices_name_the_three_files_that_carry_llvm_mingw_source_paths(self):
        # Measured on the published 0.1.10 Windows package: bea.exe (libc++abi and libunwind sources), SDL3.dll and libz.dll
        # (MinGW-w64 runtime sources) hold them and no other file does; the 0.1.10 reviewers found "in bea.exe" alone wrong.
        flat = " ".join(package.notices("0.1.10", True).decode().split())
        self.assertIn("remain in three files: bea.exe, inside its statically linked libc++abi and libunwind, and SDL3.dll "
                      "and libz.dll, inside the MinGW-w64 runtime objects linked into them", flat)
        self.assertNotIn("in bea.exe come from", flat)
        self.assertNotIn("llvm-mingw's own build machine", " ".join(package.notices("0.1.10", False).decode().split()))

    def test_generated_readmes_are_the_actual_members_of_both_authored_packages(self):
        # Run the production package assembly with tiny authored inputs and stub only the external build, dependency,
        # licence and binary-validation boundaries. The tar/zip writer and README insertion are the real code.
        owner = ROOT / "local-data/port/package-readme-tests"
        owner.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="case-", dir=owner) as tmp:
            root = Path(tmp)
            build = root / "build"
            for rel in ("linux/bea", "linux/onslaught-ce-launch", "windows/bea.exe", "windows/onslaught-ce.exe"):
                path = build / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(("authored " + rel).encode())
            ffmpeg = root / "authored-ffmpeg-source"
            ffmpeg.write_bytes(b"authored licence boundary")
            out = root / "packages"
            args = SimpleNamespace(test=False, build=build, out=out, version="0.1.10")
            callbacks = [
                patch.object(package, "check_public"), patch.object(package.deps, "require_sdl_receipt"),
                patch.object(package.deps, "require_sdl_link_receipt"),
                patch.object(package, "strip", side_effect=lambda src, dst, windows: shutil.copyfile(src, dst)),
                patch.object(package, "machine_paths", return_value=[]),
                patch.object(package, "licences", return_value={"COPYING": b"authored licence"}),
                patch.object(package, "notices", return_value=b"authored notices"),
                patch.object(package, "third_party", return_value=[]),
                patch.object(package, "notice", return_value=b"authored notice"),
                patch.object(package, "LINUX_LIBS", []), patch.object(package, "WINDOWS_DLLS", []),
                patch.object(package, "source_epoch", return_value=1700000000),
                patch.object(package, "glibc_needed", return_value=package.GLIBC),
                patch.object(package, "sysroot_gaps", return_value=[]),
                patch.object(package.deps, "lock", return_value={"FFmpeg": {"file": ffmpeg,
                             "sha": hashlib.sha256(ffmpeg.read_bytes()).hexdigest()}}),
                patch.object(package.subprocess, "run", side_effect=AssertionError("authored package must not run a process")),
            ]
            with contextlib.ExitStack() as stack:
                for callback in callbacks:
                    stack.enter_context(callback)
                with contextlib.redirect_stdout(io.StringIO()):
                    package.package(args)
            linux = "onslaught-ce-0.1.10-linux-i686"
            windows = "onslaught-ce-0.1.10-windows-i686"
            with tarfile.open(out / (linux + ".tar.gz"), "r:gz") as archive:
                members = [member for member in archive.getmembers() if member.name == linux + "/README.txt"]
                self.assertEqual(len(members), 1)
                linux_readme = archive.extractfile(members[0]).read()
            with zipfile.ZipFile(out / (windows + ".zip")) as archive:
                self.assertEqual(archive.namelist().count(windows + "/README.txt"), 1)
                windows_readme = archive.read(windows + "/README.txt")
            for windows_flag, actual in ((False, linux_readme), (True, windows_readme)):
                with self.subTest(windows=windows_flag):
                    self.assertEqual(actual, package.readme("0.1.10", windows_flag))
                    self.assert_player_guide(actual, windows_flag)


if __name__ == "__main__":
    unittest.main()
