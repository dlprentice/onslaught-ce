# Contributing

Status: active
Last updated: 2026-11-06
Summary: where each kind of work goes, what a first contribution needs and runs, how a pull request on this generated repository is taken in, the areas still changing quickly, and the evidence boundaries.

Thank you for your interest. Please read this before opening an issue or a pull request.

## Where to contribute

The project is several public repositories, and each kind of work has one home. The
[contributing guide in Onslaught-Career-Editor](https://github.com/dlprentice/Onslaught-Career-Editor/blob/main/CONTRIBUTING.md)
is the canonical map; this is the short version.

| Your work is about | It goes to |
| --- | --- |
| Playing Onslaught CE: its bugs, Linux and Windows issues, the launcher and updates (Internet multiplayer is planned, not in this version yet) | This repository, [onslaught-ce](https://github.com/dlprentice/onslaught-ce). It is generated: open issues and pull requests here as usual (see [How a pull request is taken in](#how-a-pull-request-is-taken-in)). |
| The byte-matching decompilation: functions, tools, matching notes | [onslaught-decomp](https://github.com/dlprentice/onslaught-decomp). Also generated, with the same route. Matching work needs your own copy of the game plus the compiler and SDK inputs its guide names. A change to `src/` is proposed there. |
| The Godot rebuild | [onslaught-godot](https://github.com/dlprentice/onslaught-godot), edited directly. |
| How the game works: file formats, saves, mechanics, levels, engine | [onslaught-preservation](https://github.com/dlprentice/onslaught-preservation), edited directly. It uses onslaught-decomp's function names. |
| The Companion (saves, backups, settings, lore, music), and the program's goals and plan (`GOAL.md`, `PROGRAM.md`) | [Onslaught-Career-Editor](https://github.com/dlprentice/Onslaught-Career-Editor). Its `rebuild/` and `reverse-engineering/` folders are frozen copies: rebuild work goes to onslaught-godot, documentation to onslaught-preservation. The Companion's features are moving into Onslaught CE, so new feature work belongs here. |

The Onslaught and AYAResourceExtractor forks on the same account are kept as pinned references, not contribution
targets. Security reports follow this repository's [SECURITY.md](SECURITY.md). [REUSE.toml](REUSE.toml)
says which licence covers which files, and a contribution comes in under the same terms as the files it changes.

Everyone contributes by fork and pull request. No invitation, collaborator access or maintainer-only repository is
needed, and no response time is promised.

Questions and ideas: https://github.com/dlprentice/onslaught-ce/discussions
(problems go to this repository's Issues, using the problem report form).

## How a pull request is taken in

Onslaught CE's `main` is generated: it is written by an export from the maintainers' working source, and nothing is
committed to it by hand. So a pull request here is not merged directly. The maintainer reviews it, imports the accepted
work into the working source with credit to you (a `Co-authored-by` line naming you in the commit that publishes it) and a
reference to your pull request, runs the checks there, and the
next export publishes it. The pull request is closed after the export that carries the change has landed. Do not
hand-edit generated build mirrors or release artifacts: an export would erase the edit.

## Your first hour

What you need depends on what you change. There is no hosted CI: the checks below are yours to run, and your pull
request should say which you ran and which you could not.

| You are changing | You need | Run before the pull request |
| --- | --- | --- |
| Text, links or documentation | Git; nothing else | `git diff --check`, then open every link you changed |
| Port code (`port/`) | Linux x86-64 (WSL2 on Windows), Python 3.12 or newer, Git, Ninja, Make, patch, pkgconf, Bison 3.6 or newer and a host C/C++ compiler (`cc`, or the one `CC` names). To build and run: the dependency download and the two files from your own licensed copy of the game that [Building from source](README.md#building-from-source) names | The no-game baseline below, then the check for your area |
| Anything that reads or runs the game | Your own licensed copy of *Battle Engine Aquila*, used from a copy or an isolated user folder (`BEA_USER_DIR`), never your only install | The affected check, and a note of which system ran it: Wine, a Windows virtual machine or physical hardware |
| `src/` | The matching check, which needs your own game, the compiler and the SDK (see [onslaught-decomp](https://github.com/dlprentice/onslaught-decomp#reproducing-the-match)) | Propose it in onslaught-decomp, where the check can run |

The no-game baseline needs no game files and no dependency download, only the programs above:

```sh
python3 port/tools/build_tests.py
python3 port/tools/deps_tests.py
python3 port/tools/package_readme_tests.py
python3 port/tools/manual_update_package_tests.py
```

The other checks sit beside these and are named for their area; the first lines of each say what it covers and how to
run it. Some need more than the baseline. A few that are easy to find:

| Area | Check | It also needs |
| --- | --- | --- |
| Launcher: log, recording, Report a Problem | `python3 port/updater/tests/launcher_tests.py` | a Linux `--public` build first |
| Audio: Ogg reader and music worker | `python3 port/tools/audio_eof_tests.py` | a Linux build first, and a host `ffmpeg` with the `libvorbis` encoder |
| Update packages and the manual migration | `python3 port/tools/manual_update_package_tests.py` | the baseline only |

Steps:

1. Fork this repository, clone your fork and create a branch from `main` (`git switch -c my-change`). Note the commit
   you started from (`git rev-parse HEAD`) and any edits you already had (`git status --short`).
2. Read the [build guide](README.md#building-from-source), the [port documentation](port/README.md) and the licence
   notices. CE has no repository submodules to initialize. [AGENTS.md](AGENTS.md) holds the rules automated agents
   follow; as a person you need this file and the build guide.
3. Run the baseline and write down the result. Then make one bounded change in the files that own the behaviour.
   Ordinary development does not need approval from a maintainer first; for something larger or overlapping other
   work, open an issue first (see [Areas under active development](#areas-under-active-development)).
4. Run the checks for your change, before and after it. Commit only the paths you changed and push the branch to your
   fork. Commit messages describe the change itself; keep session links, private paths and anything copied from the
   game out of them. A `Co-Authored-By` trailer is welcome when someone else wrote part of the change.
5. Open the pull request. The form asks for the problem, the change, the base commit, the exact checks you ran and the
   ones you could not. Maintainers complete the remaining acceptance gates before the work is imported. Say whether a
   result came from Wine, a Windows virtual machine or physical hardware. Do not add hosted CI or release workflows.

## Areas under active development

Commander, Video Options, camera feel and the planned network play, map editor and museum are changing quickly. The
maintainers' working source has unreleased changes in these areas that this repository does not show yet. Open an issue
before starting larger work in them, so the change is not made against code that is about to move.

## Related repositories

The former single repository now has separate public owners, listed in [Where to contribute](#where-to-contribute).
Onslaught CE is not a combined build of them. Read the owner's own contribution guide and evidence for work there;
planned multiplayer, editor or commander features are not automatically available in a published package. You only
need to clone and set up the repositories your task touches. For Companion work, follow
[its setup](https://github.com/dlprentice/Onslaught-Career-Editor#build-and-run); for the rebuild,
[Godot's setup](https://github.com/dlprentice/onslaught-godot#building-and-testing).

## Useful contributions

What helps most:

- bug reports with what you did, what you expected and what happened, your system (distribution or Windows version,
  graphics card and driver), and the program's log if there is one;
- testing on Windows PCs with real graphics cards and their drivers; current Windows checks use Wine and a
  Windows 11 virtual machine with software rendering;
- fixes in the port's platform layer (`port/`).

Changes to `src/` are held to a stricter rule: its files keep their line counts and the game's original build output,
so a change there needs the matching check, which this tree cannot run. The
[public decompilation](https://github.com/dlprentice/onslaught-decomp#reproducing-the-match) documents that
function-check route using your own pinned game and toolchain inputs; propose `src/` changes there. The matching
milestone retains documented concessions and does not establish complete gameplay parity or completion of planned CE
features.

## Licence of contributions

Inbound = outbound: by contributing you agree that your contribution is licensed under the GNU General Public License,
version 3 only (GPL-3.0-only), the licence of this project, and that you have the right to license it so.

## Never include game files

Never attach or commit any of the game's files or anything derived from them: executables, data, textures, sounds,
films, text, saves, or bytes, dumps or captures taken from them. Players supply the game's data from their own copy;
this repository contains none of it and will not accept it.

Review logs for personal paths and device details before sharing sanitized excerpts. A session tape and its metadata
can contain typed text and encoded careers or other user files: do not upload the raw pair to a public issue. Begin
with a written reproduction. If more evidence is needed, say in the issue that a recording exists, attach nothing, and
a maintainer will agree an evidence route with you there. Nothing should be uploaded automatically.
