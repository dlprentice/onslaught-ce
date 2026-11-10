# CE linked mission projects

Status: experimental source; Linux production link and isolated installed-map runs shown; Windows execution and player acceptance are separate
Last updated: 2026-11-04
Summary: a career-independent prototype loads a Toolkit-authored project and repositions existing named objects on an installed map.

Toolkit's Mission project page keeps authored placement IDs, XYZ coordinates and source text in a
`.ceproject` document. Linking an installed map adds version 2's `map` record. Opening and saving an
unlinked version-1 document preserves that earlier format.

This first runtime mode is **existing-object placement**. The linked installed level supplies its terrain,
models, object definitions and existing mission behavior. Each logical definition identifies one named
object from that level, and one authored placement supplies its initial position. CE applies the position
before that object's initialization. It does not create, clone or remove objects, edit terrain, write world
files or compile source. Installed mission behavior may subsequently move an object. Project source text
and its entry/object links are preserved; they are not executed by this consumer.

## Author, save and load

1. Add a placement in Mission project, give it a stable logical definition, and position it in the 3D view
   or XYZ fields. Coordinates are Aquila X/Y horizontal and positive Z down, in game units.
2. Select the placement. Under **Installed-map link**, enter the installed level number and exact existing
   object name, then choose **Link selected definition**. Each definition and object name can be bound once.
   The page reports missing bindings or repeated definitions without discarding an incomplete draft.
3. Use **Save as…**, then **Open…** to check that the map binding, placement and source return together.
   **Load linked map** can preview a selected installation and validate the linked level's object names.
   A name found only in the base world, or shared by multiple objects of the selected level, is refused.
   CE validates the bindings again while loading the map.
4. Use a project-enabled CE build with an explicit isolated installed-game copy and a separate custom user
   directory. The prototype entry is `BEA_CE_PROJECT` with an absolute project path. It bypasses the career
   frontend, loads one linked level, then returns from the game run. Menu integration is later work.
   On Linux the exit status is the launch's own outcome: 0 once the linked map loaded with every placement
   applied, however the level then ended, unless the loader later refused a second map load, and 1 otherwise.

The caller supplies these absolute paths; the example does not select a live installation or profile:

```sh
mkdir -m 700 "$project_user"
env BEA_GAME_DIR="$game_copy" BEA_USER_DIR="$project_user" BEA_CE_CUSTOM_USER_DIR=1 \
  BEA_CE_PROJECT="$project" "$ce_game"
```

Project startup requires explicit absolute `BEA_GAME_DIR` and `BEA_USER_DIR` values before ordinary
profile/settings initialization. A selected project never falls back to the default profile. Both roots must
fit the existing startup path caches, including their raw spelling before canonicalization.
The custom directory must already exist, belong to the current user, have no group/other write permission,
and be outside the installed-game tree. Its path must be usable without following directory links. Do not combine this entry with `BEA_NET_*` or `BEA_LOCKSTEP_*`
startup settings. On Linux, project mode refuses such combinations before profile initialization. The prototype
currently admits Linux only. A Windows build has no startup preflight: it checks neither those settings nor the roots,
starts as it would without a project (with the career fenced), and refuses the project only when `CSystem::Run` asks
for the level, returning before any level loads, with the reason on standard error. Nobody has run that path, and
Windows project execution has not been accepted.

Project selection fences career reads and all career storage create/write/delete boundaries even before
project validation (patch 0139, which also holds the launch). Patch 0202 fences the gameplay progression mutators
used for outcomes, base-object persistence, kill/goodie updates and unlock slots, and holds the world-loading
placement hooks. Invalid input stays in project mode and cannot
fall back to a career launch. Existing manual and ordinary game paths retain their behavior when the project
flag is absent. Project loading writes no project, installed map, asset or career file. Normal CE logs/settings
belong to the explicitly selected custom user directory.

## Version-2 link contract

The root retains version 1's `kind`, `coordinates`, `source_language`, `id`, `name`, `entry_script`,
`placements` and `scripts` fields, sets `version` to `2`, and requires:

```json
"map": {
  "mode": "existing-object-placement",
  "level": 777,
  "bindings": [
    { "definition": "authored.beacon", "source_object": "AuthoredAnchor" }
  ]
}
```

These numbers and names are fictional authored fixture values, not an installed level or asset catalogue.
The `level` is the installed level identifier which supplies the world and terrain. A binding names no asset
file or filesystem path. IDs remain the author's stable IDs; later added-object support can introduce a new
explicit mode without redefining them as retail object numbers.

Both consumers reject unknown/duplicate record fields, invalid Unicode, dangling script references, duplicate
IDs, repeated definition/object bindings, non-finite coordinates and excessive input. The runtime requires
at least one placement, a binding for each placement's definition, at most one placement per definition, a level in the
format's 1–9999 range, and coordinate magnitudes at most 1048576. These are format safety bounds, not a claim that every
such level or coordinate exists in an installation. The document is bounded to 8 MiB, 4096 placements/bindings, 128
sources, 1 MiB per source and 4 MiB combined source. A placement whose definition has no binding, a placed binding whose
object the linked level holds not at all or more than once, a different map, a failed/second map load, or a game run
that returns without loading the linked map stop startup with a `CE project:` diagnostic. A binding that no placement
uses is accepted and moves nothing: the runtime never looks its object up, so a level without that object still starts.
A linked level that is not installed at all ends earlier, in the game's own fatal file error while it loads the level's
resources (exit status 1). No partial project is reported as loaded.

## Focused verification

```sh
python3 port/tools/ce_project_tests.py --causal-controls
```

The runner applies the actual patches to a private copy of the mirror's source, in the build's order and at fuzz 0 as
the build does, compiles the real runtime owner in C++14,
and extracts the actual launch/world/career entry hooks into an authored fixture. It checks coordinate
application before object initialization, refusal paths, the acceptance of a binding no placement uses, ordinary-mode
behavior, career fences and unchanged authored input files. Compiling causal controls must fail their intended
assertions. The optional `--project` argument accepts only the exact authored fixture published by Toolkit's
`CeMissionRuntimeLinkTests` through its production file adapter, allowing a connected editor-save → CE-loader check
without retail data.

Those checks establish the contract and hooks. In a production build `ce_project.cpp` is a host-allocator unit
(`pcxx` in `build.py`, as the boot authority and invitation parser are): it owns host STL buffers, exceptions and
native file handles. It is not a network module, because the default public build's hooks call it as well.
Both of its patches are main's, not the network's: 0139 applies ahead of the network patches that share its files, so
main's queue stands without them, and in a build with the network each card writer's first statement is still the
network career guard, with the project's storage guard right after it.

```sh
python3 port/tools/run_ce_project_tests.py --help    # lab-only: an installed game copy, gdb and the offscreen runner
```

That runner holds a frozen production binary to an installed map, keeping names, coordinates and frames in ignored
`local-data/`. In Linux runs of the production builds, the default public one and the one with the network, a project
moved a named, level-owned object that the map simulates: the live object was at the authored X and Y in the first
Update and kept the authored offset as the mission moved it, no other thing differed there from a deterministic
ordinary control, and a project binding the object at its own position equalled that control update for update and
pixel for pixel (the runner requires a byte-identical update dump and an identical frame). Placed ahead of the player,
the object was seen in a captured frame where the project put it, as the game's current target: under the lock ring,
with its icon and health bar in the target box. Height stays the object's own: `Init` may replace the authored Z, as it
does for a ship on the water. An object's components move with it: for a carrier, every other thing that differed from
the control was one of its components, moved by the authored offset. Moving an object can change how its mission plays
out, since scripts and AI react to where it is. In the build with the network, with a real save in the user root, the
career fence was consulted once in each run, from `CCareer::UpdateGoodieStates`: it refused the project run and allowed
the ordinary control. Neither run entered memory-card storage and the save was unchanged after both, so at run time
these runs show the fence only at that call; the storage guards rest on the source-bound checks.

Without a project the same builds behaved as main's: equal update dumps and frames in the ordinary runs of both
builds, and, for the default public build, equal probes on the six golden tapes of the default-change fence
(`tools/fence`), whose runs reach the placement and career hooks (counted with breakpoints at their entry addresses).
The fence's own by-name counts of those hooks read 0 because it counted `PortCeProjectRequested` first: set by name,
each hook's breakpoint falls on the same instruction as an inlined copy of that function, and where counting
breakpoints share an instruction gdb runs only the first one's commands, since they resume the game.

An additional independently reproduced Linux check used the Mission project page's production save path: its
file-selection signal saved a project, and the default public CE build loaded those exact bytes on level 221.
The linked boat received the authored X and Y; its initialization supplied its height, and the copied career
was unchanged. Moving it into another part of the battle also changed later AI behavior, so this is placement
evidence rather than a promise that other objects never react. The page was driven programmatically; nobody
used its file dialog or dragged a placement by hand in this check.

These checks do not show a level played to its end or the career code that follows it (the runs end by closing
the window; the rest of the
career fence is covered by the source-bound checks above), the refusal of a bound name that only the base world holds
or that several of the level's objects share (the maps of those runs have neither; the source-bound checks cover
both), what the game does once the loader has refused a second map load in a project run (a Retry or a restart may
ask for one; the refusal marks the launch failed, so its exit status is then 1), Windows project execution, or how a
placement feels in play.

The source/compiler handoff still needed for full mission authoring is an explicit CE consumer for preserved
source IDs/text and its diagnostics; this placement consumer does not pretend that handoff exists.
