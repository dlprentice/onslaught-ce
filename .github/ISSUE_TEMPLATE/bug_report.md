---
name: Problem report
about: Something went wrong, or the game behaves differently from the original
title: "Problem: "
labels: bug
---

<!-- In 0.1.7 or later, `onslaught-ce --report` (or `onslaught-ce.exe --report`) opens this page with your version and
     system filled in. -->

> **Safety:** do not start an Onslaught CE 0.1.0–0.1.6 launcher to fill in this report, not even to check its
> version: its startup cleanup can delete neighbouring `<program folder>.old-*` paths. Give the version you downloaded
> instead, and see the README's
> [manual migration](https://github.com/dlprentice/onslaught-ce#manual-migration-from-affected-launchers).

**What happened**



**Steps to reproduce**

1. 

**Does the original game do the same?**

Yes, no, or not checked. If the original behaves differently, say how.

**How did you run it?**

Native Linux, native Windows, or the Windows package under Wine or Proton (which version?).

**Files**

- Please attach `onslaught-ce.log` from your user folder: `$XDG_DATA_HOME/onslaught-ce` (when `XDG_DATA_HOME`
  is set and nonempty;
  otherwise `~/.local/share/onslaught-ce`) (Linux) or
  `%APPDATA%\Onslaught CE` (Windows). `onslaught-ce.old.log` beside it is the run before.
  If you set `BEA_USER_DIR` to a folder, the log is in that folder instead. Logs can include local paths, user
  names and device details;
  please review them before posting publicly.
- Optional: if you recorded the session (`onslaught-ce --record`, or `record_sessions=1` in `ce.ini`), put its `.tape`
  and `.tape.json` from the `tapes` folder in a zip and attach that too. It can help us reproduce what happened.

**System**

- Onslaught CE version (0.1.7 or later: main menu, bottom left, or `onslaught-ce --version`; 0.1.0–0.1.6: the version
  you downloaded, without starting it):
- Linux distribution or Windows version:
- GPU:
