# Contributing

Status: active
Last updated: 2026-11-02
Summary: public fork contributions, product ownership, local checks and evidence boundaries.

Thank you for your interest. Please read this before opening an issue or a pull request.

## How changes are made

Contributors may edit and commit proposals in their public forks and open a pull request against
this repository's `main`. No collaborator invitation, private repository access, maintainer computer or private analysis
database is required. Upstream `main` is generated from the maintainers' working source: accepted work is imported,
checked and published by the next export with credit. Do not hand-edit generated build mirrors or release artifacts.

1. Fork and clone this repository, create a branch, and record the upstream commit and existing edits.
2. Read `AGENTS.md`, the [build guide](README.md#building-from-source), relevant [port documentation](port/README.md)
   and licence notices. CE has no repository submodules to initialize.
3. Choose one bounded issue in its owning source files. Preserve unrelated work.
4. Run the affected checks. `python3 port/tools/build_tests.py` is a no-game fixture check requiring Python, Git
   and Ninja, without a game build or network access. Ordinary game builds use the Linux-hosted `--public` route
   and require your own game installation and pinned build inputs.
5. Describe the problem, change, base commit, exact checks and unavailable checks in the PR. Maintainers complete
   remaining acceptance gates before integration. Distinguish Wine, Windows VMs and physical Windows hardware.
   Do not add hosted CI or release workflows.

## Returning from the original shared repository

CE is the playable native Linux/Windows port and the main entry point for port work. The former single repository
has separate public product owners:

- [onslaught-decomp](https://github.com/dlprentice/onslaught-decomp): reconstructed source and function matching.
- [onslaught-godot](https://github.com/dlprentice/onslaught-godot): the C# Godot rebuild and its measured parity.
- [onslaught-preservation](https://github.com/dlprentice/onslaught-preservation): maintained game findings.
- [Onslaught-Career-Editor](https://github.com/dlprentice/Onslaught-Career-Editor): Companion, editors, catalogue tools
  and lore. Its copies of the rebuild and research documentation are frozen.

CE is not a combined build of these products. Read each owner's current contribution guide and evidence; planned
multiplayer, editor or commander features are not automatically available in a published package. Keep existing
reference submodules at their recorded pins in repositories that actually declare them.

## Useful contributions

What helps most:

- bug reports with what you did, what you expected and what happened, your system (distribution or Windows version,
  graphics card and driver), and the program's log if there is one;
- testing on Windows PCs with real graphics cards and their drivers; current Windows checks use Wine and a
  Windows 11 virtual machine with software rendering;
- fixes in the port's platform layer (`port/`).

Changes to `src/` are held to a stricter rule: its files keep their line counts and the game's original build output,
so a change there needs the maintainers' tooling to check it. The separate
[public decompilation](https://github.com/dlprentice/onslaught-decomp#reproducing-the-match) documents a function-check
route using your own pinned game and toolchain inputs. The matching milestone retains documented concessions and
does not establish complete gameplay parity or completion of planned CE features.

## Licence of contributions

Inbound = outbound: by contributing you agree that your contribution is licensed under the GNU General Public License,
version 3 only (GPL-3.0-only), the licence of this project, and that you have the right to license it so.

## Never include game files

Never attach or commit any of the game's files or anything derived from them: executables, data, textures, sounds,
films, text, saves, or bytes, dumps or captures taken from them. Players supply the game's data from their own copy;
this repository contains none of it and will not accept it.

Review logs for personal paths and device details before sharing sanitized excerpts. A session tape and its metadata
can contain typed text and encoded careers or other user files: do not upload the raw pair to a public issue. Begin
with a written reproduction and agree a suitable evidence route with a maintainer if more is needed. Nothing should
be uploaded automatically.
