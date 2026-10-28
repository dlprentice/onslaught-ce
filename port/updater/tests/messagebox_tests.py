#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Linux launcher's questions when the message box cannot be shown: no zenity (SDL's own X11 dialog, with the
32-bit libXrandr hidden, which crashed 0.1.8's and 0.1.9's launchers), zenity failing, a dialog backend that crashes
(spawn_crash_shim.c), no display, and the terminal fallback. On a private Xvfb on an explicit display (never
-displayfd); keys are sent with XTest. The question asked is the game-folder one (no Steam library in the private
HOME): Escape answers Quit ("no supported ... install was chosen"), and nobody asked says "set BEA_GAME_DIR".

    XVFB=/path/to/Xvfb python3 port/updater/tests/messagebox_tests.py [-v]   # needs build.py --public --target linux
"""
import ctypes
import ctypes.util
import os
import pty
import resource
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PORT / "tools")); sys.path.insert(0, str(PORT))
import deps  # noqa: E402

LAUNCHER = deps.WORK / "build/linux/onslaught-ce-launch"
XVFB = os.environ.get("XVFB") or shutil.which("Xvfb")
INSTALL = os.environ.get("BEA_GAME_DIR") or str(Path.home() / ".local/share/Steam/steamapps/common/Battle Engine Aquila")
XRANDR32 = [p for p in ("/usr/lib32/libXrandr.so.2", "/usr/lib/i386-linux-gnu/libXrandr.so.2") if os.path.exists(p)]
QUIT = "onslaught-ce: no supported Battle Engine Aquila install was chosen"
NOBODY = "onslaught-ce: could not find Battle Engine Aquila in Steam's libraries; set BEA_GAME_DIR to its folder"
ZENITY = "#!/bin/sh\nif [ \"$1\" = --version ]; then echo 4.0.0; exit 0; fi\n%s\n"
GAME = "#!/bin/sh\necho stand-in game\nexit 0\n"
ESCAPE, RETURN = 0xff1b, 0xff0d


def build_shim(out: Path) -> Path:
    import build  # noqa: E402
    so = out / "spawn-crash-shim.so"
    subprocess.run(build.TARGETS["linux"]["cc"] + ["-m32", "-shared", "-fPIC", "-O1", str(Path(__file__).with_name(
        "spawn_crash_shim.c")), "-o", str(so)] + deps.sysroot_link_flags(), check=True)
    return so


def build_mutant(out: Path) -> Path:
    """The launcher with ask_box showing the box in its own process (no child): a crashing backend takes it down."""
    import build  # noqa: E402
    src = out / "port"
    shutil.copytree(PORT / "updater", src / "updater")
    c = src / "updater/ce_launch.c"
    text = c.read_text()
    old = "\t\tpid_t pid = fork();\n\t\tif (pid == 0)\n\t\t{\n\t\t\tint w = -1;"
    assert text.count(old) == 1, "ask_box's child no longer matches the mutant"
    c.write_text(text.replace(old, "\t\tpid_t pid = 0;\n\t\tif (pid == 0)\n\t\t{\n\t\t\tint w = -1;"))
    exe, real = out / "mutant-launch", build.PORT
    build.PORT = src
    try:
        if build.build_launcher("linux", "0.0.0-mutant", exe):
            raise RuntimeError("mutant launcher build failed")
    finally:
        build.PORT = real
    return exe


@unittest.skipUnless(LAUNCHER.exists(), f"{LAUNCHER}: build with build.py --public --target linux")
@unittest.skipUnless(XVFB and os.access(XVFB, os.X_OK), "no Xvfb (set XVFB)")
@unittest.skipUnless(ctypes.util.find_library("Xtst"), "no libXtst for the test's key presses")
class MessageBox(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-messagebox-test-"))
        cls.display = next(n for n in range(160, 200)
                           if not Path(f"/tmp/.X11-unix/X{n}").exists() and not Path(f"/tmp/.X{n}-lock").exists())
        cls.xvfb = subprocess.Popen([XVFB, f":{cls.display}", "-screen", "0", "1280x800x24", "-nolisten", "tcp"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            if Path(f"/tmp/.X11-unix/X{cls.display}").exists():
                break
            time.sleep(0.1)
        cls.x11 = ctypes.CDLL(ctypes.util.find_library("X11"))
        cls.xtst = ctypes.CDLL(ctypes.util.find_library("Xtst"))
        cls.x11.XOpenDisplay.restype = ctypes.c_void_p
        cls.dpy = cls.x11.XOpenDisplay(f":{cls.display}".encode())
        if not cls.dpy:
            cls.xvfb.terminate()
            raise unittest.SkipTest("the private Xvfb did not start")
        cls.shim = build_shim(cls.tmp)

    @classmethod
    def tearDownClass(cls):
        cls.x11.XCloseDisplay(ctypes.c_void_p(cls.dpy))
        cls.xvfb.terminate()
        cls.xvfb.wait(timeout=20)
        shutil.rmtree(cls.tmp)

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="case-", dir=self.tmp))
        for d in ("bin", "fakebin", "home", "user"):
            (self.dir / d).mkdir()
        shutil.copy2(LAUNCHER, self.dir / "bin/onslaught-ce-launch")
        (self.dir / "bin/bea").write_text(GAME)
        (self.dir / "bin/bea").chmod(0o755)

    def zenity(self, body):
        z = self.dir / "fakebin/zenity"
        z.write_text(ZENITY % body)
        z.chmod(0o755)

    def newest_window_centre(self):
        """The centre of the newest top-level window (the dialog), or the screen's when there is none."""
        d, u, i = ctypes.c_void_p(self.dpy), ctypes.c_ulong, ctypes.c_int
        root, parent, n = u(), u(), ctypes.c_uint()
        children = ctypes.POINTER(u)()
        if not self.x11.XQueryTree(d, u(self.x11.XDefaultRootWindow(d)), ctypes.byref(root), ctypes.byref(parent),
                                   ctypes.byref(children), ctypes.byref(n)) or not n.value:
            return 640, 400
        x, y, w, h, bw, depth = i(), i(), ctypes.c_uint(), ctypes.c_uint(), ctypes.c_uint(), ctypes.c_uint()
        self.x11.XGetGeometry(d, u(children[n.value - 1]), ctypes.byref(root), ctypes.byref(x), ctypes.byref(y),
                              ctypes.byref(w), ctypes.byref(h), ctypes.byref(bw), ctypes.byref(depth))
        self.x11.XFree(children)
        return x.value + w.value // 2, y.value + h.value // 2

    def keys(self, stop, keysym):
        """Press the key every second, with the pointer on the dialog (no window manager: the window under the pointer
        gets the key), until stop is set."""
        kc = self.x11.XKeysymToKeycode(ctypes.c_void_p(self.dpy), ctypes.c_ulong(keysym))
        while not stop.wait(1.0):
            self.xtst.XTestFakeMotionEvent(ctypes.c_void_p(self.dpy), 0, *self.newest_window_centre(), 0)
            self.xtst.XTestFakeKeyEvent(ctypes.c_void_p(self.dpy), kc, 1, 0)
            self.xtst.XTestFakeKeyEvent(ctypes.c_void_p(self.dpy), kc, 0, 0)
            self.x11.XFlush(ctypes.c_void_p(self.dpy))

    def run_launcher(self, exe=None, display=True, crash=False, key=None, game=False, tty_answer=None):
        env = {"PATH": str(self.dir / "fakebin"), "HOME": str(self.dir / "home"), "BEA_USER_DIR": str(self.dir / "user"),
               "BEA_NO_UPDATE_CHECK": "1", "LD_LIBRARY_PATH": str(deps.PREFIX / "lib")}
        if display:
            env["DISPLAY"] = f":{self.display}"
        if crash:
            env["LD_PRELOAD"] = str(self.shim)
        if game:
            env["BEA_GAME_DIR"] = INSTALL
        cmd = [str(exe or self.dir / "bin/onslaught-ce-launch")]
        if XRANDR32:  # hide the 32-bit libXrandr, as on a desktop without it
            hide = []
            for p in XRANDR32:
                hide += ["--ro-bind", "/dev/null", p]
            cmd = ["bwrap", "--dev-bind", "/", "/", *hide, "--die-with-parent", "--", *cmd]
        stop = threading.Event()
        if key:
            threading.Thread(target=self.keys, args=(stop, key), daemon=True).start()
        try:
            # no core files from the crashes this provokes
            nocore = lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0))  # noqa: E731
            if tty_answer is None:
                r = subprocess.run(cmd, env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120,
                                   preexec_fn=nocore)
                return r.returncode, r.stderr
            master, slave = pty.openpty()
            p = subprocess.Popen(cmd, env=env, stdin=slave, stdout=subprocess.DEVNULL, stderr=slave, preexec_fn=nocore)
            os.close(slave)
            out, answered = b"", False
            deadline = time.time() + 120
            while time.time() < deadline:
                if select.select([master], [], [], 0.2)[0]:
                    try:
                        chunk = os.read(master, 4096)
                    except OSError:
                        chunk = b""
                    if not chunk:
                        break
                    out += chunk
                    if not answered and b"Choose 1-" in out:
                        os.write(master, tty_answer.encode() + b"\n")
                        answered = True
                elif p.poll() is not None:
                    break
            os.close(master)
            return p.wait(timeout=30), out.decode(errors="replace").replace("\r\n", "\n")
        finally:
            stop.set()

    def log(self):
        return (self.dir / "user/onslaught-ce.log").read_text(errors="replace")

    def test_zenity_answers(self):
        self.zenity("echo Quit")
        rc, err = self.run_launcher()
        self.assertEqual(rc, 1)
        self.assertIn(QUIT, err)
        self.assertNotIn("message box", err)

    def test_no_zenity_uses_sdls_own_dialog(self):
        # 0.1.8 and 0.1.9 crashed here (SIGSEGV in SDL's X11 dialog) when the 32-bit libXrandr was missing.
        rc, err = self.run_launcher(key=ESCAPE)
        self.assertEqual(rc, 1, err)
        self.assertIn(QUIT, err)
        self.assertNotIn("message box", err)

    def test_zenity_errors_then_sdls_own_dialog(self):
        self.zenity("exit 255")  # an error: SDL falls back to its own dialog
        rc, err = self.run_launcher(key=ESCAPE)
        self.assertEqual(rc, 1, err)
        self.assertIn(QUIT, err)

    def test_zenity_says_nothing(self):
        # zenity failing like a dismissed box (exit 1, no button): nobody was asked, nothing is chosen.
        self.zenity("exit 1")
        rc, err = self.run_launcher()
        self.assertEqual(rc, 1, err)
        self.assertIn("onslaught-ce: the message box could not be shown", err)
        self.assertIn(NOBODY, err)

    def test_crashing_backend(self):
        rc, err = self.run_launcher(crash=True)
        self.assertEqual(rc, 1, err)
        self.assertIn("onslaught-ce: the message box crashed (signal 11)", err)
        self.assertIn(NOBODY, err)
        self.assertIn("the message box crashed (signal 11)", self.log())

    def test_no_display(self):
        rc, err = self.run_launcher(display=False)
        self.assertEqual(rc, 1, err)
        self.assertIn(NOBODY, err)
        self.assertNotIn("message box", err)

    @unittest.skipUnless(Path(INSTALL, "data").is_dir(), "no game install (BEA_GAME_DIR)")
    def test_terminal_answers_when_the_box_cannot(self):
        # The session-recording question on the terminal: after a crashed box, and with no display; 3 = Stop recording.
        for display, crash in ((True, True), (False, False)):
            with self.subTest(display=display, crash=crash):
                (self.dir / "user/ce.ini").write_text("record_sessions=1\n")
                rc, out = self.run_launcher(display=display, crash=crash, game=True, tty_answer="3")
                self.assertEqual(rc, 0, out)
                self.assertIn("Choose 1-3:", out)
                self.assertIn("session recording switched off", out)
                self.assertIn("record_sessions=0", (self.dir / "user/ce.ini").read_text())
                self.assertEqual("the message box crashed (signal 11)" in out, crash)

    @unittest.skipUnless(Path(INSTALL, "data").is_dir(), "no game install (BEA_GAME_DIR)")
    def test_no_terminal_keeps_the_default(self):
        # record_sessions=1 with nobody to ask records, as set (the question's safe default), and does not crash.
        (self.dir / "user/ce.ini").write_text("record_sessions=1\n")
        rc, err = self.run_launcher(crash=True, game=True)
        self.assertGreaterEqual(rc, 0, err)  # (the stand-in game writes no tape, so the launcher reports that: 3)
        self.assertIn("nobody could be asked (no working message box or terminal)", err)
        self.assertIn("onslaught-ce: recording this session", err)
        self.assertIn("record_sessions=1", (self.dir / "user/ce.ini").read_text())

    def test_mutant_without_the_child_crashes(self):
        rc, err = self.run_launcher(exe=build_mutant(self.dir), crash=True)
        self.assertLess(rc, 0, f"the mutant survived a crashing backend: {rc}\n{err}")


if __name__ == "__main__":
    unittest.main()
