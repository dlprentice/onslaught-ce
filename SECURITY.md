# Security

## Reporting a problem

Please report security problems privately through GitHub's **Report a vulnerability** form on this repository's
Security tab, not in a public issue. Describe what you found, how to reproduce it, and which version and system you
used. You will get an answer there.

Problems that matter most here:

- the launcher's update path: the feed's signature check and parsing, downloads, unpacking, the `versions/` folder
  and its state file, the switch between versions and the removal of old versions;
- anything that writes outside the program's own folder and its user data folder, or into the game's installation;
- files from the game's data or from saves that can crash the program or make it misbehave.

## What the program sends

The current source's launcher (from 0.1.8) requests the public update feed (`ce-update-v2.json` and its signature)
from the latest GitHub release when it starts, unless checking is turned off, and downloads a package only after you
choose Update ([Updates and privacy](README.md#updates-and-privacy)). These requests carry nothing but what any
download does. Released launchers 0.1.0 through 0.1.6 have a separate startup cleanup hazard and must not be run;
see [Getting the latest version](README.md#getting-the-latest-version-one-download-by-hand-then-it-updates-itself).

Report a problem (the main menu's Onslaught CE page, or `--report`) is the one network action you start yourself: it
opens a GitHub page in your browser with your version and OS in its address; nothing is posted until you submit it.
Nothing else leaves your machine: no identifiers, no telemetry.

## Supported versions

Fixes target the latest release, which 0.1.8 and later deliver through the signed updater. From 0.1.0–0.1.7, install
the latest release by hand once ([Getting the latest version](README.md#getting-the-latest-version-one-download-by-hand-then-it-updates-itself)).
