#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the career fence around source-extracted session shutdown.

The guard, boot flag load/store, and shutdown body come from the checked-out
source. Writers, transport and lockstep state are synthetic. This deliberately
does not run the game or claim that host/guest event dispatch was exercised.
"""

import argparse
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
GUARD = ROOT / "port/patches/0185-network-career-write-fence.patch"
BOOT = ROOT / "port/game/ce_network_boot.cpp"
SESSION = ROOT / "port/game/ce_network_session.cpp"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def without_comments_and_strings(text):
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: "".join("\n" if c == "\n" else " " for c in m.group()), text)


def function_body(source, name_pattern):
    clean = without_comments_and_strings(source)
    pattern = r"\b(" + name_pattern + r")\s*\(\s*(?:void\s*)?\)\s*\{"
    matches = list(re.finditer(pattern, clean, re.IGNORECASE))
    require(len(matches) == 1, "expected one source function")
    match = matches[0]
    start = match.end() - 1
    depth = 0
    for pos in range(start, len(clean)):
        if clean[pos] == "{":
            depth += 1
        elif clean[pos] == "}":
            depth -= 1
            if depth == 0:
                return match.group(1), source[start + 1:pos]
    raise RuntimeError("unbalanced source function")


def extract_guards(patch):
    lines = patch.splitlines()
    pattern = re.compile(
        r"^\s*if\s*\(\s*([A-Za-z_]\w*)\s*\(\s*\)\s*"
        r"\|\|\s*([A-Za-z_]\w*)\s*\(\s*\)\s*\)\s*$"
    )
    found = []
    for pos, line in enumerate(lines):
        if not line.startswith("+") or line.startswith("+++"):
            continue
        match = pattern.fullmatch(line[1:])
        if not match or "boot" not in match.group(1).lower() or "lockstep" not in match.group(2).lower():
            continue
        found.append((pos, match.groups()))
    require(len(found) == 3 and len({names for _, names in found}) == 1,
            "expected three matching career guards")

    def added(pos):
        require(pos < len(lines) and lines[pos].startswith("+") and not lines[pos].startswith("+++"),
                "career guard patch shape changed")
        return lines[pos][1:].strip()

    blocks = []
    pointer = None
    reset_value = None
    for index, (pos, _names) in enumerate(found):
        header = added(pos)
        if index == 0:
            block = [added(pos + i) for i in range(5)]
            require(block[1] == "{" and block[4] == "}", "create guard block changed")
            inner = re.fullmatch(r"if\s*\(\s*([A-Za-z_]\w*)\s*\)\s*\*\s*([A-Za-z_]\w*)\s*=\s*(-?\d+)\s*;", block[2])
            require(inner is not None and inner.group(1) == inner.group(2), "create guard pointer changed")
            require(re.fullmatch(r"return\s+-?\d+\s*;", block[3]) is not None,
                    "create guard return changed")
            pointer, reset_value = inner.group(1), inner.group(3)
        else:
            block = [header, added(pos + 1)]
            require(re.fullmatch(r"return\s+-?\d+\s*;", block[1]) is not None,
                    "writer guard return changed")
        blocks.append(block)
    return found[0][1], blocks, pointer, reset_value


def extract_boot(source, getter):
    pattern = (r"\b" + re.escape(getter) + r"\s*\(\s*(?:void\s*)?\)\s*\{"
               r"\s*return\s+__atomic_load_n\s*\(\s*&\s*([A-Za-z_]\w*)\s*,\s*"
               r"(__ATOMIC_[A-Z_]+)\s*\)\s*;\s*\}")
    matches = list(re.finditer(pattern, source, re.S))
    require(len(matches) == 1, "boot request getter changed")
    variable = matches[0].group(1)
    require(len(re.findall(r"\bint\s+" + re.escape(variable) + r"\s*;", source)) == 1,
            "boot request declaration changed")
    clean = without_comments_and_strings(source)
    require(len(re.findall(r"\b" + re.escape(variable) + r"\b", clean)) == 3,
            "boot request uses changed; recheck lifetime")
    store_pattern = (r"__atomic_store_n\s*\(\s*&\s*" + re.escape(variable) +
                     r"\s*,\s*(?:1|true)\s*,\s*__ATOMIC_[A-Z_]+\s*\)\s*;")
    stores = list(re.finditer(store_pattern, source))
    require(len(stores) == 1, "boot request store changed")
    getter_body = re.search(r"\{(.*)\}\s*$", matches[0].group(), re.S).group(1)
    return variable, getter_body, stores[0].group()


def extract_shutdown(source, lockstep_getter):
    shutdown, body = function_body(source, r"[A-Za-z_]\w*shutdown\w*")
    clean = without_comments_and_strings(body)
    require("#" not in clean and "::" not in clean, "shutdown shape changed")
    pairs = re.findall(r"\b([A-Za-z_]\w*)\s*\.\s*([A-Za-z_]\w*)", clean)
    require(pairs and len({base for base, _ in pairs}) == 1, "shutdown state shape changed")
    state = pairs[0][0]
    fields = sorted({field for _, field in pairs})
    require(not re.search(r"\b" + re.escape(state) + r"\s*\.\s*\w+\s*(?:->|\.|\[)", clean),
            "shutdown state needs a richer fake")
    reset = re.findall(r"\b" + re.escape(state) + r"\s*=\s*([A-Za-z_]\w*)\s*\(\s*\)", clean)
    require(len(reset) == 1, "shutdown state reset shape changed")
    names = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", clean))
    names -= {"if", "while", "for", "switch", "return", "sizeof"}
    require(lockstep_getter in names, "shutdown no longer reads lockstep request")
    endings = [n for n in names if "lockstep" in n.lower() and
               any(word in n.lower() for word in ("end", "shutdown", "stop", "close", "reset", "destroy", "cleanup", "cancel", "abort"))]
    require(len(endings) == 1, "lockstep teardown call changed")
    return shutdown, body, state, fields, names, endings[0], reset[0]


def make_fixture(broken_guard=False, broken_lifetime=False):
    names, blocks, pointer, reset_value = extract_guards(GUARD.read_text(encoding="utf-8"))
    boot, lockstep = names
    variable, getter_body, store = extract_boot(BOOT.read_text(encoding="utf-8"), boot)
    _shutdown, shutdown_body, state, fields, calls, end_call, reset_call = extract_shutdown(
        SESSION.read_text(encoding="utf-8"), lockstep)
    if broken_guard:
        blocks = [[line.replace("||", "&&", 1) if i == 0 else line
                   for i, line in enumerate(block)] for block in blocks]
    field_decls = "\n".join("    int " + name + " = 1;" for name in fields)
    stubs = ["static FakeSession " + reset_call + "(void) { return FakeSession{}; }"]
    for name in sorted(calls - {lockstep, reset_call}):
        action = "++fake_calls;"
        if name == end_call:
            action += " ++fake_end_calls; fake_lockstep = 0;"
            if broken_lifetime:
                action += " __atomic_store_n(&" + variable + ", 0, __ATOMIC_RELAXED);"
        stubs.append("template<class... T> int " + name + "(T...) { " + action + " return 0; }")
    return r'''#include <cstring>
static int __BOOT_VARIABLE__;
static int fake_lockstep = 0;
static int fake_calls = 0;
static int fake_end_calls = 0;
static int fake_writes[3] = {0, 0, 0};
static int __BOOT_GETTER__(void) { __BOOT_BODY__ }
static int __LOCKSTEP_GETTER__(void) { return fake_lockstep; }
static void fake_boot_start(void) { __BOOT_STORE__ }
struct FakeSession {
__FIELDS__
};
static FakeSession __STATE__;
__STUBS__
static void actual_shutdown_body(void) { __SHUTDOWN_BODY__ }
static int fake_create(int *__POINTER__) {
__CREATE_GUARD__
    ++fake_writes[0];
    if (__POINTER__) *__POINTER__ = 17;
    return 17;
}
static int fake_write(void) {
__WRITE_GUARD__
    ++fake_writes[1];
    return 17;
}
static int fake_delete(void) {
__DELETE_GUARD__
    ++fake_writes[2];
    return 17;
}
static int blocked(void) {
    int slot = 91;
    fake_create(&slot); fake_write(); fake_delete(); fake_create(nullptr);
    return fake_writes[0] == 0 && fake_writes[1] == 0 && fake_writes[2] == 0 && slot == __RESET_VALUE__;
}
int main(int argc, char **argv) {
    if (argc != 2) return 90;
    if (!std::strcmp(argv[1], "offline")) {
        int slot = 91;
        if (__BOOT_GETTER__() || __LOCKSTEP_GETTER__()) return 1;
        fake_create(&slot); fake_write(); fake_delete();
        return fake_writes[0] == 1 && fake_writes[1] == 1 && fake_writes[2] == 1 && slot == 17 ? 0 : 2;
    }
    if (!std::strcmp(argv[1], "lockstep-only")) {
        fake_lockstep = 1;
        return blocked() ? 0 : 3;
    }
    if (!std::strcmp(argv[1], "boot-only")) {
        fake_boot_start();
        return blocked() ? 0 : 3;
    }
    if (std::strcmp(argv[1], "protected-shutdown")) return 91;
    fake_boot_start(); fake_lockstep = 1;
    if (!blocked()) return 4;
    actual_shutdown_body();
    if (!__BOOT_GETTER__() || fake_lockstep || fake_end_calls != 1 || !fake_calls) return 5;
    return blocked() ? 0 : 6;
}
'''.replace("__BOOT_VARIABLE__", variable).replace("__BOOT_GETTER__", boot).replace(
        "__BOOT_BODY__", getter_body).replace("__LOCKSTEP_GETTER__", lockstep).replace(
        "__BOOT_STORE__", store).replace("__FIELDS__", field_decls).replace("__STATE__", state).replace(
        "__STUBS__", "\n".join(stubs)).replace("__SHUTDOWN_BODY__", shutdown_body).replace(
        "__POINTER__", pointer).replace("__CREATE_GUARD__", "\n".join(blocks[0])).replace(
        "__WRITE_GUARD__", "\n".join(blocks[1])).replace("__DELETE_GUARD__", "\n".join(blocks[2])).replace(
        "__RESET_VALUE__", reset_value)


def run_process(command, cwd, timeout=30):
    env = {"PATH": "/usr/bin:/bin", "HOME": str(cwd), "TMPDIR": str(cwd), "LC_ALL": "C"}
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pidfd = os.pidfd_open(process.pid) if hasattr(os, "pidfd_open") else None
    try:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            require(pidfd is not None and hasattr(signal, "pidfd_send_signal"),
                    "cannot safely stop timed-out child")

            def stop_recorded_child(sig):
                cmdline = Path("/proc", str(process.pid), "cmdline").read_bytes().split(b"\0", 1)[0]
                require(Path(os.fsdecode(cmdline)).name == Path(command[0]).name,
                        "timed-out child identity changed")
                signal.pidfd_send_signal(pidfd, sig)

            stop_recorded_child(signal.SIGTERM)
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                stop_recorded_child(signal.SIGKILL)
                process.communicate(timeout=5)
            raise RuntimeError("synthetic child timed out") from exc
    finally:
        if pidfd is not None:
            os.close(pidfd)
    return process.returncode, stdout, stderr


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scratch", type=Path, required=True, help="private scratch parent")
    args = parser.parse_args()
    compiler = shutil.which("g++")
    require(compiler is not None, "C++ compiler unavailable")
    args.scratch.mkdir(parents=True, exist_ok=True, mode=0o700)
    scratch = Path(tempfile.mkdtemp(prefix="career-lifetime-", dir=args.scratch))
    scenarios = ("offline", "lockstep-only", "boot-only", "protected-shutdown")
    for label, flags in (("control", (False, False)), ("guard-mutant", (True, False)),
                         ("lifetime-mutant", (False, True))):
        source = scratch / (label + ".cpp")
        binary = scratch / label
        source.write_text(make_fixture(*flags), encoding="utf-8")
        rc, stdout, stderr = run_process([compiler, "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
                                         str(source), "-o", str(binary)], scratch)
        (scratch / (label + ".compiler.log")).write_bytes(stdout + stderr)
        require(rc == 0, "synthetic control did not compile; see private scratch")
        if label == "control":
            selected = [(scenario, 0) for scenario in scenarios]
        elif label == "guard-mutant":
            selected = [("protected-shutdown", 6), ("boot-only", 3), ("lockstep-only", 3)]
        else:
            selected = [("protected-shutdown", 5)]
        for scenario, expected in selected:
            rc, stdout, stderr = run_process([str(binary), scenario], scratch)
            (scratch / (label + "-" + scenario + ".log")).write_bytes(stdout + stderr)
            require(rc == expected,
                    "synthetic control or negative control failed")
    print("career lifetime: four synthetic cases and four negative controls passed")


if __name__ == "__main__":
    main()
