#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create deterministic, single-module dashboard inputs without installing a bundler."""
import argparse
import hashlib
import json
from pathlib import Path
import re

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[2]


def replace_once(text: str, pattern: str, value: str) -> str:
    result, count = re.subn(pattern, lambda _: value, text, flags=re.M)
    if count != 1:
        raise ValueError("module shape differs from the reviewed bundle recipe")
    return result


def without_exports(text: str) -> str:
    result = re.sub(r"^export (?=(?:async )?(?:class|function|const)\b)", "", text, flags=re.M)
    if re.search(r"^export\b", result, flags=re.M):
        raise ValueError("unhandled export in bundle input")
    return result


def bundle(output: Path) -> dict:
    output = output.absolute()
    output.resolve().relative_to((ROOT / "local-data").resolve())
    sources = {name: (SOURCE / name).read_text() for name in ("parcel_crypto.mjs", "worker.mjs", "issuer.mjs")}
    parcel = without_exports(sources["parcel_crypto.mjs"])
    crypto_scope = "const { ParcelCipher, hex } = (() => {\n" + parcel + "\nreturn { ParcelCipher, hex };\n})();\n"
    worker = replace_once(sources["worker.mjs"], r'^import \{ ParcelCipher, hex \} from "\./parcel_crypto\.mjs";\n', crypto_scope)
    inner = replace_once(worker, r'^import \{ DurableObject, WorkerEntrypoint \} from "cloudflare:workers";\n', "")
    inner = replace_once(inner, r'^export default ', "const publicEntry = ")
    inner = without_exports(inner)
    helpers = "bodyBytes, fields, integer, nowSeconds, require, strictJson, verifyPermit"
    issuer = replace_once(sources["issuer.mjs"], r'^import \{ bodyBytes, fields, integer, nowSeconds, require, strictJson, verifyPermit \} from "\./worker\.mjs";\n',
        'import { DurableObject, WorkerEntrypoint } from "cloudflare:workers";\n' +
        f"const {{ {helpers} }} = (() => {{\n" + inner + f"\nreturn {{ {helpers} }};\n}})();\n")
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    artifacts = {"worker.mjs": worker, "issuer.mjs": issuer}
    for name, text in artifacts.items():
        if re.search(r'^import .* from "\./', text, flags=re.M):
            raise ValueError("bundle still requires a local module")
        (output / name).write_text(text)
    receipt = {"schema": 1, "sources": {name: hashlib.sha256(text.encode()).hexdigest() for name, text in sources.items()},
               "bundles": {name: {"sha256": hashlib.sha256(text.encode()).hexdigest(), "bytes": len(text.encode())}
                           for name, text in artifacts.items()}}
    (output / "bundles.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    print(json.dumps(bundle(parser.parse_args().output)))
