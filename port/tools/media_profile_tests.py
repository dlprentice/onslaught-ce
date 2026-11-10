#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build only the unconnected media-profile helper and synthetic controls.

Requires an explicitly supplied existing Monocypher source directory. It never
downloads dependencies or builds/runs a game, ENet, ICE, relay or UI.
Its recorded dependency hashes are not compared with trusted pins. A passing
fixture is synthetic evidence against caller-supplied, unverified source;
acceptance requires separate trusted-pin and selected-platform verification.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--monocypher-source", type=Path, required=True,
                        help="Caller-supplied, unverified directory containing monocypher.c and monocypher.h")
    parser.add_argument("--c-compiler", default=os.environ.get("CC", "cc"))
    parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--word-size", type=int, choices=(32, 64))
    args = parser.parse_args()
    if os.name == "posix":
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    root = Path(__file__).resolve().parents[2]
    dependency = args.monocypher_source.resolve()
    dependency_files = [dependency / "monocypher.c", dependency / "monocypher.h"]
    if not all(path.is_file() for path in dependency_files):
        parser.error("--monocypher-source must contain existing monocypher.c and monocypher.h")
    parent = root / "local-data" / "media-profile-tests"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    sources = [root / "port/network/media_profile.h",
               root / "port/network/optional/media_profile.cpp",
               root / "port/network/tests/media_profile_tests.cpp"]
    result = {
        "status": "not_started",
        "word_size": args.word_size,
        "dependency_validation": "caller_supplied_unverified",
        "sources": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in sources},
        "dependency_sources": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in dependency_files},
        "stages": {},
    }
    word = [f"-m{args.word_size}"] if args.word_size else []
    include = ["-I", str(root / "port/network"), "-I", str(dependency)]
    c_object = output / "monocypher.o"
    header_source = output / "header-contract.c"
    header_source.write_text(
        '#include "media_profile.h"\n'
        'int media_profile_c_contract(void) {\n'
        '    const CE_MediaProfile profile = {1, 1, 0, 0};\n'
        '    return CE_MediaProfileValidate(&profile) == CE_MEDIA_PROFILE_TEXT &&\n'
        '           CE_MediaProfileChannelCount(&profile) == 5;\n'
        '}\n', encoding="utf-8")
    header_object = output / "header-contract.o"
    binary = output / "media-profile-tests"
    steps = [
        ("dependency_compile", [args.c_compiler, *word, "-std=c99", "-O0", "-g0",
                                "-c", str(dependency_files[0]), "-o", str(c_object)]),
        ("c_header_compile", [args.c_compiler, *word, "-std=c11", "-O0", "-g0",
                              "-Wall", "-Wextra", "-Werror", "-Wpedantic", *include,
                              "-c", str(header_source), "-o", str(header_object)]),
        ("helper_compile", [args.compiler, *word, "-std=c++17", "-O0", "-g0",
                            "-Wall", "-Wextra", "-Werror", "-Wpedantic", *include,
                            str(sources[1]), str(sources[2]), str(c_object),
                            str(header_object), "-o", str(binary)]),
    ]
    # Python's independent implementation checks the domain length, lack of a
    # terminating zero, base bytes, concrete enabled tuple and output length.
    # This tuple cannot detect swaps between its equal-valued descriptor fields.
    expected = hashlib.blake2b(b"CE-MEDIA-PROFILE-V1" + bytes(range(32)) +
                               bytes((1, 1, 0, 0)), digest_size=32).hexdigest()
    steps.append(("synthetic_controls", [str(binary), expected]))
    try:
        for name, command in steps:
            with (output / f"{name}.log").open("w", encoding="utf-8") as log:
                completed = subprocess.run(command, stdout=log, stderr=log,
                                           timeout=60, check=False)
            result["stages"][name] = completed.returncode
            if completed.returncode:
                result["status"] = f"{name}_failed"
                return 1
        result["status"] = "synthetic_controls_passed"
        return 0
    except subprocess.TimeoutExpired:
        result["status"] = f"{name}_timed_out"
        return 1
    except OSError as error:
        result["status"] = f"{name}_unavailable"
        result["error_type"] = type(error).__name__
        return 1
    finally:
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"{result['status']}; synthetic evidence: {output}")


if __name__ == "__main__":
    raise SystemExit(main())
