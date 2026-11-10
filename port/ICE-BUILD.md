# Optional ICE build

Status: experimental build support; no release or player acceptance
Last updated: 2026-11-02
Summary: explicitly build the pinned, relay-patched libjuice client and bind the CE binaries to its corresponding source.

Prepare the ordinary pinned dependencies with [deps.py](tools/deps.py), using a separate worktree and private dependency cache for this flavour:

```sh
python3 port/tools/ice_deps.py build all
python3 port/build.py --public --with-network --with-ice --target all -j 2
```

`BEA_PORT_DATA` selects that cache. Ordinary `deps.py fetch/build` and builds without `--with-ice` do not fetch, build or link this optional input. The normal `deps.py` and `deps.lock` remain unchanged, preserving their SDL receipt identities. The optional recipe requires the existing pinned CMake, compiler, sysroot and CE dependency inputs. A full game compile still needs the ordinary mirror's two pinned retail includes; this helper neither fetches game data nor launches a game or service.

[ice-deps.lock](ice-deps.lock) pins libjuice 1.7.4 at revision `b89c792e3612faf2f12cf35bcc56857313a06be3`, archive SHA-256 `713b22174e8cb2e06538fa9bac2650bda21b83c86f5128813ab4dcf8f5bea9ed`. It also requires the MPL-2.0 [relay-policy patch](network/libjuice-relay-only.patch), SHA-256 `21d2dbecacfff42e61fc16589e26795335600354ca82c45199ba8f26235341e2`. The [policy record](network/LIBJUICE-RELAY-POLICY.md) describes that modification and its authored controls.

The helper validates both pins, extracts fresh source and applies the exact patch without fuzz. A separate source receipt binds the archive, ordered patch identity and resulting source-tree digest. Every validation rehashes the actual tree. The patch file is an explicit ICE source-profile input. Modified, missing, partial or stale inputs refuse reuse; do not replace a cache another worktree uses.

Both i686 targets use a static client library, built-in hash functions and disabled upstream tests/server implementation. Upstream server API stubs remain. Pinned tools run with a fixed environment, path maps, deterministic archive mode and explicit Windows object-timestamp control. New output paths include the full patch digest under `ice-relay/`, `ice-src-relay/` and `ice-build-relay/`. The original archive and licence notices remain unchanged.

Schema 2 dependency and successful-link receipts require the patch list and patched-source digest. They also bind toolchain pins, recipe, declared source profile, installed inputs and actual game/launcher hashes. Legacy schemas, wrong JSON types and absent patch fields fail closed. A target starts with an incomplete link receipt; a positive one is written only after its game, launcher and normal SDL link receipt succeed. The validated archive is both an explicit link input and a Ninja dependency. [Build fixtures](tools/ice_build_tests.py) and [source-profile fixtures](tools/network_source_id_tests.py) cover these boundaries.

`--with-ice` requires `--public --with-network`; it defines `CE_NETWORK_ICE` and `JUICE_STATIC`, adds the optional headers to network sources and links the verified library plus Windows' bcrypt API. Without that flag, these definitions, paths and libraries are absent. [ICE-BRIDGE.md](network/ICE-BRIDGE.md) describes the session entry point. Building supplies no provider endpoint, account or TURN credentials.

Enabled binaries contain the loadable tag `CE-NET-FEATURE:ICE-LIBJUICE:1`, retained by a linker root through stripping and LTO. Their folder marker is `public-network-ice`. Ordinary packaging refuses this flavour. Any separately reviewed opt-in packaging route must require schema 2 and the complete corresponding source; the frozen upstream-only schema 1 route must reject these receipts.

Distribution requires the unchanged upstream archive, exact patch, deterministic application/build instructions, notice of the modifications and applicable MPL/GPL notices and source access. See the [pinned MPL licence](https://raw.githubusercontent.com/paullouisageneau/libjuice/b89c792e3612faf2f12cf35bcc56857313a06be3/LICENSE) and [Mozilla's combination guidance](https://www.mozilla.org/en-US/MPL/2.0/FAQ/#q14-may-i-combine-mpl-licensed-code-and-lgpl-licensed-code-in-the-same-executable-program). ENet's MIT obligations also apply. Native Windows execution, provider connectivity and two-player acceptance remain separate work.
