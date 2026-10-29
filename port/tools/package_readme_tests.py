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
            "Main menu > Onslaught CE:", "Coming soon / ideas describes planned features and their limits",
            "it does not start those features", "Bonus: Aquila Prototype", "Bonus co-op: Defence A",
            "Bonus co-op: Defence B", "Special Thanks", "The game's Credits remain separate",
            "two-player local split-screen defence maps", "Multiplayer start page", "player 2 uses the second set of keys",
            "Plug in both gamepads before starting", "do not advance career progress at level end",
            "other game paths can still save the live career and settings during play, including on pause/continue",
            "Console: during an eligible solo level, press the grave key", "type help and press Enter",
            "Esc or grave closes it; the game does not pause", "unavailable in multiplayer, demos, recordings and replays",
            "Video Options > Display mode offers Windowed or Fullscreen", "Quit and restart for a saved choice to take effect",
            "the menu says if the choice could not be saved", "-forcewindowed game argument takes priority",
            "saved display-mode choice is ignored while recording or replaying",
            "eligible ordinary single-player play with widescreen modes enabled", "HUD automatically scales up",
            "BEA_PORT_HUD_SCALE=0", "off in recordings and replays, multiplayer, and when BEA_PORT_WIDESCREEN=0",
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
        ):
            with self.subTest(phrase=phrase, windows=windows):
                self.assertIn(phrase, flat)
        # The photo-mode contexts are the public README's, word for word (the 0.1.10 reviewers found the three texts differing).
        self.assertIn("Both work in single-player levels, but not while a session is recorded or replayed, in multiplayer, "
                      "a network game or a demo.", flat)
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

    def test_generated_text_has_current_player_paths_and_limits(self):
        with patch.object(package.subprocess, "run", side_effect=AssertionError("README generation must not run a process")):
            for windows in (False, True):
                with self.subTest(windows=windows):
                    text = self.assert_player_guide(package.readme("0.1.10", windows), windows)
                    # Compare the displayed menu labels to the actual CE menu, not just this generator's list.
                    front_end = (ROOT / "port/game/ce_frontend.cpp").read_text()
                    labels = set(re.findall(r'CCeMenuItem\(L"([^"]+)"', front_end))
                    for label in ("Bonus: Aquila Prototype", "Bonus co-op: Defence A", "Bonus co-op: Defence B", "Special Thanks"):
                        self.assertIn(label, labels)
                        self.assertIn(label, text)

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
