#!/usr/bin/env python3
"""CTest harness for the adapted rsz Tcl regression suite (PR4).

Usage: python3 run_case.py <script.tcl>

Runs build/apps/eos/eos <script> with cwd = this dir (so the
script's fixtures/... paths resolve), extracts the single verdict
line, and diffs exactly that line against the sibling .ok file.

Verdict rules (mechanical, no failure-hiding normalization):
- The .ok file holds exactly one line. If it starts with
  "status=ok", the verdict is the first stdout line starting with
  "status=ok". If it starts with "error=", the verdict is the
  first stdout line starting with "error=" (the negative wire3
  case, whose script puts the caught error).
- A missing verdict line, or a nonzero eos exit, fails the case
  outright, whatever else the output contains.
- The comparison is exact string equality on the whole line.

The eos binary defaults to <repo>/build/apps/eos/eos, resolved
from this file's location; EOS_BIN overrides it.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EOS_BIN = os.environ.get(
    "EOS_BIN",
    os.path.normpath(
        os.path.join(HERE, "..", "..", "..", "..",
                     "build", "apps", "eos", "eos")))


def main(argv):
    if len(argv) != 2:
        print("usage: run_case.py <script.tcl>", file=sys.stderr)
        return 2
    script = argv[1]
    script_path = script if os.path.isabs(script) else os.path.join(HERE, script)
    if not script_path.endswith(".tcl"):
        print(f"FAIL: not a .tcl script: {script}", file=sys.stderr)
        return 1
    ok_path = script_path[:-4] + ".ok"
    if not os.path.isfile(script_path):
        print(f"FAIL: script not found: {script_path}", file=sys.stderr)
        return 1
    if not os.path.isfile(ok_path):
        print(f"FAIL: .ok file not found: {ok_path}", file=sys.stderr)
        return 1
    with open(ok_path, "r", encoding="utf-8") as f:
        expected = f.read().strip("\r\n")
    if "\n" in expected:
        print(f"FAIL: .ok file is not a single line: {ok_path}",
              file=sys.stderr)
        return 1
    if expected.startswith("status=ok"):
        prefix = "status=ok"
    elif expected.startswith("error="):
        prefix = "error="
    else:
        print(f"FAIL: .ok file has unknown verdict prefix: {ok_path}",
              file=sys.stderr)
        return 1
    if not os.path.isfile(EOS_BIN):
        print(f"FAIL: eos binary not found: {EOS_BIN}", file=sys.stderr)
        return 1
    proc = subprocess.run([EOS_BIN, script_path], cwd=HERE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, timeout=1800)
    if proc.returncode != 0:
        print(f"FAIL: eos exited {proc.returncode} for {script}")
        tail = "\n".join(proc.stderr.splitlines()[-20:])
        if tail:
            print(tail)
        return 1
    verdict = None
    for line in proc.stdout.splitlines():
        if line.startswith(prefix):
            verdict = line
            break
    if verdict is None:
        print(f"FAIL: no verdict line starting with {prefix!r} "
              f"for {script}")
        print("--- stdout tail ---")
        print("\n".join(proc.stdout.splitlines()[-20:]))
        return 1
    if verdict == expected:
        print(f"PASS: {script}")
        return 0
    print(f"FAIL: verdict mismatch for {script}")
    print(f"  expected: {expected}")
    print(f"  actual:   {verdict}")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
