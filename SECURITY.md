# Security

## Reporting a problem

Please report security problems privately through GitHub's **Report a vulnerability** form on this repository's
Security tab, not in a public issue. Describe what you found, how to reproduce it, and which version and system you
used. You will get an answer there.

Problems that matter most here:

- the launcher's update path, including the legacy feed verification, archive handling and installation swap;
- anything that writes outside the program's own folder and its user data folder, or into the game's installation;
- files from the game's data or from saves that can crash the program or make it misbehave.

## What the program sends

Automatic update checking and installation are temporarily disabled in the current source. Its launcher does not
request a release feed or download an update. Released launchers 0.1.0 through 0.1.6 have a separate startup cleanup
hazard and must not be run; see the [warning and manual migration](README.md#launcher-safety-warning).

Report a problem (the main menu's Onslaught CE page, or `--report`) is the one network action you start yourself: it
opens a GitHub page in your browser with your version and OS in its address; nothing is posted until you submit it.
Nothing else leaves your machine: no identifiers, no telemetry.

## Supported versions

Fixes target the latest release. 0.1.7 addresses the launcher cleanup hazard; migrate to it by hand from an affected
version, following the [manual migration](README.md#manual-migration-from-affected-launchers).
