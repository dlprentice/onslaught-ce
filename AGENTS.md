# Onslaught CE: guide for automated agents

Status: active
Last updated: 2026-11-11
Summary: public fork contributions and maintainer export ownership.

This repository is generated from the maintainers' working source. Contributors may edit and commit proposals on
branches in their public forks, then open a pull request against `main` (`CONTRIBUTING.md`). Maintainers import accepted
work, validate it and regenerate upstream `main`; do not maintain that branch or generated artifacts by hand.
Private repository access and a maintainer's private analysis database are not prerequisites for public contributions.

- Licence: GPL-3.0-only (`COPYING`, `REUSE.toml`); third-party components keep their own licences (`THIRD-PARTY.md`).
- Never add the game's files or anything derived from them (executables, data, saves, captures, byte dumps).
- `src/` files keep their line counts: the game's build depends on them.
- The launcher checks a signed `ce-update-v2.json` feed at start (unless turned off) and asks before installing
  (`port/updater/UPDATER-V2.md`); never publish a `latest.json` feed.
  Report a problem opens a GitHub page in the user's browser with their version and OS in its address; nothing is
  posted until they submit it. The program has no telemetry.
- Build: `README.md`, "Building from source". Layout, rules and the guides for each part: `port/README.md`.
