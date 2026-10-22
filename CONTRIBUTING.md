# Contributing

Thank you for your interest. Please read this before opening an issue or a pull request.

## How changes are made

This repository is generated from the maintainers' working source by an export: it is not edited directly, and its
history is a series of releases. Contributions go through the maintainers. Open an issue describing the problem or
change, or a pull request with your change; the maintainers bring accepted work into the working source, check it
there, and it appears here with the next export, credited to you.

What helps most:

- bug reports with what you did, what you expected and what happened, your system (distribution or Windows version,
  graphics card and driver), and the program's log if there is one;
- testing on real Windows, which the maintainers cannot do;
- fixes in the port's platform layer (`port/`).

Changes to `src/` are held to a stricter rule: its files keep their line counts and the game's original build output,
so a change there needs the maintainers' tooling to check it.

## Licence of contributions

Inbound = outbound: by contributing you agree that your contribution is licensed under the GNU General Public License,
version 3 only (GPL-3.0-only), the licence of this project, and that you have the right to license it so.

## Never include game files

Never attach or commit any of the game's files or anything derived from them: executables, data, textures, sounds,
films, text, saves, or bytes, dumps or captures taken from them. Players supply the game's data from their own copy;
this repository contains none of it and will not accept it.
