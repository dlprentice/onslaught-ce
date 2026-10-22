# Security

## Reporting a problem

Please report security problems privately through GitHub's **Report a vulnerability** form on this repository's
Security tab, not in a public issue. Describe what you found, how to reproduce it, and which version and system you
used. You will get an answer there.

Problems that matter most here:

- the launcher's update path: the release feed and its Ed25519 signature, the downloaded archive and its SHA-256, and
  how the update is unpacked and swapped in;
- anything that writes outside the program's own folder and its user data folder, or into the game's installation;
- files from the game's data or from saves that can crash the program or make it misbehave.

## What the program sends

The launcher makes two HTTPS requests when it starts (the signed release feed, `latest.json` and its signature, from
this repository's releases), and downloads an update archive from this repository's releases only if you agree to an
update. The update check can be turned off (`update_check=0` in `ce.ini`, or `BEA_NO_UPDATE_CHECK=1`).

Report a problem (the main menu's Onslaught CE page, or `--report`) is the one network action you start yourself: it
opens a GitHub page in your browser with your version and OS in its address; nothing is posted until you submit it.
Nothing else leaves your machine: no identifiers, no telemetry.

## Supported versions

Only the latest release receives fixes.
