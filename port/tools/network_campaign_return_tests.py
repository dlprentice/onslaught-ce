#!/usr/bin/env python3
"""Retained authored checks for the confirmed campaign return owner."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "port/game/ce_network_campaign_return.h"
DRIVER = ROOT / "port/tools/tests/network_campaign_return_driver.cpp"
NETCODE = ROOT / "port/platform/common/ce_netcode.cpp"


def trial(root, label, header, m32, expected=None):
    directory = root / label
    directory.mkdir()
    (directory / HEADER.name).write_text(header)
    other = directory / "other.cpp"
    other.write_text('#include "ce_network_campaign_return.h"\n'
                     'bool read_campaign_return_in_other_unit(CeCampaignReturn *out) {\n'
                     '    return ce_campaign_return_get(out);\n}\n')
    binary = directory / "test"
    sha = directory / "sha256.o"
    cc = [os.environ.get("CC", "gcc"), "-O1"]
    if m32:
        cc.append("-m32")
    cc += ["-c", str(ROOT / "port/updater/sha256.c"), "-o", str(sha)]
    with (directory / "sha-build.private.log").open("w") as log:
        built_sha = subprocess.run(cc, stdout=log, stderr=subprocess.STDOUT)
    if built_sha.returncode:
        return {"label": label, "m32": m32, "passed": False,
                "sha_build_exit": built_sha.returncode}
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-O1"]
    if m32:
        command.append("-m32")
    command += ["-I", str(directory), "-I", str(ROOT / "port/game"),
                "-I", str(ROOT / "port/platform/common"),
                str(DRIVER), str(other), str(NETCODE), str(sha), "-o", str(binary)]
    with (directory / "build.private.log").open("w") as log:
        build = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    result = {"label": label, "m32": m32, "build_exit": build.returncode,
              "header_sha256": hashlib.sha256(header.encode()).hexdigest(),
              "netcode_sha256": hashlib.sha256(NETCODE.read_bytes()).hexdigest(),
              "driver_sha256": hashlib.sha256(DRIVER.read_bytes()).hexdigest()}
    if build.returncode:
        return {**result, "passed": False}
    with (directory / "run.private.log").open("w") as log:
        run = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT)
    output = (directory / "run.private.log").read_text()
    result.update(exit_code=run.returncode,
                  checks=sum(l.startswith(("PASS ", "FAIL ")) for l in output.splitlines()),
                  failures=sum(l.startswith("FAIL ") for l in output.splitlines()))
    result["passed"] = (run.returncode != 0 and f"FAIL {expected}" in output
                        if expected else run.returncode == 0 and result["checks"] >= 25)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m32", action="store_true")
    args = parser.parse_args()
    os.umask(0o077)
    parent = ROOT / "local-data/port/network-campaign-return-tests"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    header = HEADER.read_text()
    mutations = [
        ("confirmation-removed", "!campaign || !exchange_ready || failed", "!campaign || failed",
         "unconfirmed exchange cannot return continuation"),
        ("failure-removed", "!campaign || !exchange_ready || failed", "!campaign || !exchange_ready",
         "runtime failure cannot return continuation"),
        ("phase-check-removed", "if (!win && !loss)", "if (false)",
         "non-agreed phase cannot return continuation"),
        ("clear-removed", "ce_campaign_return_storage() = {};", "(void)ce_campaign_return_storage();",
         "new mission clears earlier return context"),
    ]
    results = [trial(output, "actual", header, args.m32)]
    if results[0]["passed"]:
        for label, before, after, expected in mutations:
            if header.count(before) != 1:
                raise RuntimeError("causal control anchor changed")
            results.append(trial(output, label, header.replace(before, after), args.m32, expected))
    passed = len(results) == 5 and all(r["passed"] for r in results)
    (output / "result.json").write_text(json.dumps({"passed": passed, "results": results,
        "limits": ["Pure authored state cases; runtime capture seam and process restart not exercised.",
                   "No automatic mission progression, peer reconnection, game or network acceptance."]}, indent=2) + "\n")
    print(json.dumps({"passed": passed, "receipt": str(output / "result.json")}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
