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
spec = importlib.util.spec_from_file_location("ce_package_readme", HERE / "package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageReadmeTests(unittest.TestCase):
    def assert_player_guide(self, content, windows):
        text = content.decode("utf-8")
        flat = " ".join(text.split())
        self.assertTrue(text.startswith("Onslaught CE V0.1.10\n"))
        self.assertNotIn("\r", text)
        self.assertLessEqual(max(map(len, text.splitlines())), package.README_WIDTH)
        for phrase in (
            "Main menu > Onslaught CE: Bonus Levels, CE Settings, Cheats, Help & Info and Report a Problem",
            "Each row has a hint under the list", "The Options page is the original's",
            "What's Coming (planned features and their limits; it does not start those features)",
            "it does not start those features", "Aquila Prototype is the original playable-demo level (201)",
            "Building Defence is a two-player local split-screen defence map", "which are the same map, so the list has it once",
            "each comes back to this list with its row highlighted",
            "Race Challenge 1 to 5", "Special Thanks", "The game's Credits remain separate",
            "CE Settings: a list of choices, each written to ce.ini in your user folder when you make it",
            "Help & Info: Controls & Tips", "Updates & Rollback",
            "Onslaught CE > Cheats: the game's own cheats as On/Off switches", "Unlock All Levels (original code TURKEY)",
            "the pause menu also has a Photo Mode row", "The row then reads End Photo Mode",
            "one-player time trials (levels 901 to 905)", "those routes save your career as the original does",
            "Race Challenge 1 after grades of C or better in 26 campaign levels",
            "each later race when the one before it is finished fast enough",
            "(under 145, 195, 150 and 105 seconds for races 1 to 4; the fifth opens nothing more)",
            "saves nothing and returns to the Bonus Levels list",
            "two-player local split-screen defence map (level 856)", "Multiplayer start page", "player 2 uses the second set of keys",
            "Plug in both gamepads before starting",
            "Settings can still be saved during play, including into a career file; bonus-level progress is left out",
            "Console: during an eligible solo level, press the grave key", "type help and press Enter",
            "Esc or grave closes it; the game does not pause", "unavailable in multiplayer, demos, recordings and replays",
            "Video Options > Window mode offers Windowed or Fullscreen", "Choose Apply to switch without restarting",
            "the menu says the change is for this session only", "-forcewindowed game argument takes priority",
            "saved display-mode choice is ignored while recording or replaying",
            "eligible ordinary single-player play with widescreen modes enabled", "HUD automatically scales up",
            "BEA_PORT_HUD_SCALE=0", "off in recordings and replays, split-screen multiplayer, and when BEA_PORT_WIDESCREEN=0",
            "These switches are off in recordings and replays", "Typing a code into a career's name still works",
            "wheel down zooms in and wheel up zooms out", "right bumper zooms in and left bumper out",
            "Direct gamepad and flight-stick input is disabled during recording and replay",
            "An older tape made with live controller input cannot recover it and may replay differently",
            "By default, recording targets 30 frames a second for play and uses the replay clock", "The larger HUD, console",
            "the saved display mode are off", "tapes folder beside ce.ini", ".tape and a .tape.json",
            "encoded copies of some CE user-folder files", "game options, careers and possibly other files",
            "Session recording does not capture video, game audio or microphone audio",
            "Both files can contain personal information; review them before sharing", ".incomplete file",
            "BEA_USER_DIR can select another folder", "it puts record_sessions=1 in ce.ini in your user folder",
            "update check sends requests for the public feed and its signature",
            "Physical audio devices and listening quality have not been validated",
            "Only virtual devices have been tested; physical controllers and Windows drivers remain open",
            "Windows reparse-point behaviour is unmeasured",
            "Mods (experimental): on Linux, Onslaught CE can read replacement game files",
            "On Windows mods are not active in this release: a mods= line is ignored and the log says so",
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
            "Your own music is off in recordings, replays, network games and demos",
            "the next time the console opens for Console Text",
        ):
            with self.subTest(phrase=phrase, windows=windows):
                self.assertIn(phrase, flat)
        # The photo-mode contexts are the public README's, word for word (the 0.1.10 reviewers found the three texts differing).
        self.assertIn("Both work in single-player levels, but not while a session is recorded or replayed, in multiplayer, "
                      "a network game or a demo.", flat)
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
            for phrase in ("a filename whose in-game name exceeds 31 16-bit units is omitted from the list",
                           "emits one name-free diagnostic, normally captured in a launcher log, but shows no on-screen warning",
                           "If a career is missing, keep its file and ask for help",
                           "the older spelling can be chosen from the original folder; keep both"):
                self.assertIn(phrase, flat)
        return text

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
                    "later launches can replace log files",
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
                    self.assertEqual(whole.count("when unset or empty"), 3)

    def test_readme_names_the_experimental_block_and_the_new_profile_video_defaults(self):
        """What a player finds without asking: the [Experimental] block the first start writes into ce.ini, and Full models Off with
        Terrain detail distance Extended on a new profile. public/README.md has both; the README.txt in the download must too, and the
        facts it states are the ones the code holds (the block's header and warning are ce.cpp's, the fresh rows are ce_graphics_policy.cpp's)."""
        policy = (ROOT / "port/game/ce_graphics_policy.cpp").read_text()
        self.assertIn('FreshBeyondRetailRow("full_models", CE_FullModelsRequest, 0);', policy, "Full models starts Off")
        self.assertIn("const int kFreshTerrainDetail = 1;", policy, "Terrain detail distance starts Extended")
        ce = (ROOT / "port/platform/common/ce.cpp").read_text()
        self.assertIn('Unfinished features, off in this release. They may change or break. Set a line to 1 to try one.', ce)
        public = " ".join((ROOT / "public/README.md").read_text().split())
        self.assertIn("under `[Experimental]` in `ce.ini`", public)
        self.assertIn("**Full models** starts **Off**", public)
        self.assertIn("**Terrain detail distance** starts at **Extended**", public)
        phrases = (
            "Video Options on a new profile: the first time you start with no saved options of your own",
            "every quality row starts at its highest choice except Full models",
            "anti-aliasing starts at the highest level your graphics device lists",
            "Full models starts Off",
            "is the largest cost in frame rate",
            "Terrain detail distance starts at Extended",
            "choose Normal for the original's distance",
            "Anything you saved before is kept as it was",
            "Unfinished features: the first start adds an [Experimental] block to ce.ini that lists them, each set to 0 (off).",
            "They are unsupported and may change or break; a line set to 1 turns that feature on from the next start.",
            "A line you already set in an earlier version (0.1.13 or before) is kept as it is.",
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
                    cheats = {re.sub(r" \(.*", "", name) for name in re.findall(r'\{L"([^"]+): ", \w+, \{', front_end)}
                    self.assertEqual(len(cheats), 5)
                    flat = " ".join(text.split())
                    for label in ("Bonus Levels", "CE Settings", "Cheats", "Help & Info", "Report a Problem", "Aquila Prototype",
                                  "Building Defence", "Race Challenge 1", "Controls & Tips",
                                  "What's Coming", "Updates & Rollback", "About Onslaught CE", "Special Thanks"):
                        self.assertIn(label, labels)
                        self.assertIn(label, flat)
                    for label in cheats:
                        self.assertIn(label, flat)
                    # CE Settings is its table: every row's label is named in the generated README, and so is each place its
                    # line is described (the keys the README gives for a hand edit are the table's own)
                    table = front_end[front_end.index("static const SCeSetting\tkSettings[]"):front_end.index("#define CE_NUM_SETTINGS")]
                    rows = re.findall(r'^\t\{L"([^"]+): ", "(\w+)"', table, re.M)
                    self.assertTrue(rows)
                    # the Commander Map row is behind the commander line under [Experimental] in ce.ini (off by default): the README does not offer it
                    for label, key in rows:
                        if key == "commander":
                            self.assertNotIn(label, flat)
                            continue
                        self.assertIn(label, flat)
                    self.assertNotIn("ommander", flat)
                    for key in ("hud_scale", "hud_anchor", "sharp_ui", "update_check", "record_sessions", "music_dir",
                                "music_playtype"):
                        self.assertIn(key, [row[1] for row in rows])
                        self.assertIn(key, flat)
                    self.assertIn("Record Play for Bug Reports", flat)

    def test_the_game_s_readme_pointers_name_parts_of_the_readmes(self):
        """The game's dialogs send the player to a part of the README by its name ("see Your own music in the README"). The README is
        README.txt, the one in the download: on both systems it has that part, as a list item's or a line's lead ("Your own music:");
        "the project README" is public/README.md, where the part is a heading. The game's font has no underscore, so the names are the
        only way the dialogs can point at a ce.ini line."""
        source = re.sub(r'"\s*"', "", (ROOT / "port/game/ce_frontend.cpp").read_text())     # adjacent string literals joined
        named = set(re.findall(r'([A-Z][a-z]+(?: [a-z]+)*) in the (project )?README', source))
        self.assertTrue({("Your own music", ""), ("Updates", ""), ("Mods", "project ")} <= named, named)
        public = (ROOT / "public/README.md").read_text()
        for name, project in sorted(named):
            with self.subTest(name=name):
                if project:
                    self.assertRegex(public, r"(?m)^#+ " + re.escape(name) + r"\b", "the project README has the part as a heading")
                    continue
                for windows in (False, True):
                    text = package.readme("0.1.10", windows).decode("utf-8")
                    self.assertRegex(text, r"(?m)^(?:- )?" + re.escape(name) + r":", "README.txt has the part the game names")
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
        "Network game (experimental)", "Main menu > Onslaught CE > Network Game", "new and unfinished", "expect problems",
        "the same Onslaught CE version and their own copy of the game", "If the two games ever disagree, the game ends with a message.",
        "A network game does not change your careers.", "There is no matchmaking server",
        "the host forwards a UDP port on its router (net_port in ce.ini, default 27652)",
        "Not tested: play through routers over the real internet, more than two players, Windows on real hardware.",
        "contacts no server", "on Windows that asks the system to look up this PC's own name",
        "Planned, not in this version yet: matchmaking, versus play, general mod support and additional HUD options.")
    PLANNED_DEFAULT = "- Planned, not in this version yet: Internet multiplayer, general mod support and additional HUD options."
    PLANNED_NETWORK = "- Planned, not in this version yet: matchmaking, versus play, general mod support and additional HUD options."

    def test_network_flavour_readme_carries_the_network_game_labels_and_warnings(self):
        # The page's strings as the compiler joins them (adjacent literals), so the two texts are compared sentence by sentence.
        page = " ".join(re.sub(r'"\s*\n\s*"', "", (ROOT / "port/game/ce_netpages.cpp").read_text()).split())
        project = " ".join((ROOT / "port/README.md").read_text().split())
        not_tested = "Not tested: play through routers over the real internet, more than two players, Windows on real hardware"
        mismatch = "If the two games ever disagree, the game ends with a message."
        careers = "does not change your careers."
        self.assertIn("EXPERIMENTAL: co-op over a network is new and unfinished. Expect problems.", page)
        self.assertIn("There is no matchmaking server", page)
        self.assertIn(not_tested + ".", page)
        self.assertIn(mismatch, page)
        self.assertIn("A co-op game " + careers, page)
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
                for phrase in ("Network game (experimental)", "matchmaking", "Not tested: play through routers"):
                    self.assertNotIn(phrase, flat)
                self.assertIn(self.PLANNED_DEFAULT, flat)

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
