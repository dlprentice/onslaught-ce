#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Music shutdown's actual worker-handle lifecycle on the Linux port's real kernel layer.

An authored worker waits on the real port events and the patched shutdown closes it. The negative control omits
only patch 0183. No game, audio device, Windows runtime, player data or proprietary input is used. The host needs
32-bit g++ development libraries and the pinned dxvk-native headers provided through deps.py.
"""
import os
from pathlib import Path
import resource
import subprocess
import tempfile
import unittest

import deps
import music_stream_reinit_tests as source

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "handles.h"
void PortFileClose(PortHandle *) {}
void PortClockSleep(unsigned long) {}
struct Buffer { HRESULT Stop() { return 0; } void Release() {} };
struct Reader {};
static HANDLE sThread, sQuitEvent, sFirstHalfEvent, sSecondHalfEvent, sPlayEvent;
static Buffer *sBuffer;
static Reader *sReader;
static char sTrackName[256];
static int sSilentHalves;
#include <shutdown.inc>
static DWORD WINAPI worker(LPVOID arg) {
 return WaitForSingleObject((HANDLE)arg, INFINITE) == WAIT_OBJECT_0 ? 0 : 4;
}
int main() {
 for (unsigned i=0; i<25; ++i) {
  Buffer buffer;
  sBuffer=&buffer;
  sReader=new Reader;
  sFirstHalfEvent=CreateEvent(NULL, FALSE, FALSE, NULL);
  sSecondHalfEvent=CreateEvent(NULL, FALSE, FALSE, NULL);
  sQuitEvent=CreateEvent(NULL, FALSE, FALSE, NULL);
  sPlayEvent=CreateEvent(NULL, FALSE, FALSE, NULL);
  sThread=CreateThread(NULL,0,worker,sQuitEvent,0,NULL);
  if (!sThread || !sFirstHalfEvent || !sSecondHalfEvent || !sQuitEvent || !sPlayEvent) return 2;
  HANDLE worker_handle=sThread;
  ShutdownMusicStream();
  if (PortCheck(worker_handle,PORT_THREAD)) {
   std::fprintf(stderr,"FAIL completed worker handle still registered\n");
   return 3;
  }
  if (sThread || sBuffer || sReader) return 4;
 }
 std::puts("25 actual patched shutdowns closed Linux port worker handles");
 return 0;
}
'''


class WorkerHandles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        parent = ROOT / "local-data/port/music-worker-handle-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        driver = cls.out / "driver.cpp"
        driver.write_text(DRIVER)
        cls.binaries = {}
        for label, skip in (("fixed", set()), ("without_handle", {source.HANDLE_PATCH})):
            folder = cls.out / label
            music, patches = source.music_source(folder, skip)
            if label == "fixed" and source.HANDLE_PATCH not in patches:
                raise AssertionError("the music worker handle patch is absent")
            body = source.function(music.read_text(), "void\tShutdownMusicStream()\t// FUNCTION")
            (folder / "shutdown.inc").write_text(body)
            binary = folder / "driver"
            command = [os.environ.get("CXX", "g++"), "-m32", "-std=c++17", "-pthread", "-ffunction-sections",
                       "-fdata-sections", "-I" + str(ROOT / "port/include/linux"),
                       "-I" + str(deps.DATA / "dxvk-native/usr/include/dxvk"),
                       "-I" + str(ROOT / "port/platform/linux"), "-I" + str(folder), str(driver),
                       str(ROOT / "port/platform/linux/kernel.cpp"), "-Wl,--gc-sections", "-o", str(binary)]
            build = subprocess.run(command, capture_output=True, text=True, timeout=90)
            (folder / "build.log").write_text(build.stdout + build.stderr)
            if build.returncode:
                raise AssertionError("Linux worker fixture failed to build: " + str(folder / "build.log"))
            cls.binaries[label] = binary

    def test_production_linux_worker_handles_are_closed(self):
        run = subprocess.run([str(self.binaries["fixed"])], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("25 actual patched shutdowns closed", run.stdout)

    def test_omitting_worker_handle_fix_is_detected(self):
        run = subprocess.run([str(self.binaries["without_handle"])], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 3, run.stdout + run.stderr)
        self.assertIn("completed worker handle still registered", run.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
