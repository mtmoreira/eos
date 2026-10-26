#!/usr/bin/env python3
"""Namespace the bundled CUDD symbols inside the built ABC archive.

Usage: namespace_cudd.py <archive> <prefix> <cudd-member>...

ABC bundles its own copy of CUDD, and the sta module carries
CUDD 3.0.0 as its own vendored closure; a binary linking both
engines would collide on the CUDD global symbols. This script
runs as a post-build step on libeda_synth_abc.a only:

1. extract the archive into a scratch directory,
2. collect the global symbols defined by the archive members
   whose names are passed in (the objects compiled from the
   nine src/bdd modules),
3. rename each of those symbols to <prefix><symbol> in every
   member, definitions and references alike (objcopy
   --redefine-syms; symbols absent from a member are ignored),
4. rebuild the archive from the renamed members.

The imported sources and the object files in the build tree are
never modified; the renaming happens on the extracted copies.
EOS code outside this module never references CUDD symbols, so
nothing outside the archive needs the prefixed names.
"""

import os
import subprocess
import sys
import tempfile


def run(*args):
    try:
        subprocess.run(args, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as error:
        sys.stderr.write(error.stdout or "")
        sys.stderr.write(error.stderr or "")
        raise


def defined_globals(obj_path):
    out = subprocess.run(
        ["nm", "-g", "--defined-only", obj_path],
        check=True, capture_output=True, text=True).stdout
    symbols = set()
    for line in out.splitlines():
        parts = line.split()
        # nm output forms: "<addr> <type> <name>" for defined
        # globals; type letters include T, D, B, C, R, A, W, V.
        if len(parts) == 3 and parts[1] not in ("U",):
            symbols.add(parts[2])
    return symbols


def main():
    if len(sys.argv) < 4:
        sys.exit("usage: namespace_cudd.py <archive> <prefix> "
                 "<cudd-member>...")
    archive = os.path.abspath(sys.argv[1])
    prefix = sys.argv[2]
    cudd_members = set(sys.argv[3:])

    # The scratch directory lives next to the archive, in the
    # build tree: the archive is about 100 MB and /tmp can be a
    # small tmpfs where extracting it and rebuilding it would
    # run out of space.
    scratch_root = os.path.join(os.path.dirname(archive), ".abc-ns")
    os.makedirs(scratch_root, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="eos-abc-ns-",
                                     dir=scratch_root) as scratch:
        subprocess.run(["ar", "x", archive], cwd=scratch, check=True)
        members = sorted(
            f for f in os.listdir(scratch)
            if os.path.isfile(os.path.join(scratch, f)))
        missing = cudd_members - set(members)
        if missing:
            sys.exit("abc namespace step: CUDD members missing "
                     "from the archive: " + ", ".join(sorted(missing)))

        symbols = set()
        for member in members:
            if member in cudd_members:
                symbols |= defined_globals(os.path.join(scratch, member))
        if not symbols:
            sys.exit("abc namespace step: no CUDD symbols found")

        map_path = os.path.join(scratch, "rename.map")
        with open(map_path, "w") as handle:
            for symbol in sorted(symbols):
                handle.write(f"{symbol} {prefix}{symbol}\n")

        for member in members:
            path = os.path.join(scratch, member)
            run("objcopy", f"--redefine-syms={map_path}", path)

        rebuilt = os.path.join(scratch, "rebuilt.a")
        run("ar", "qcs", rebuilt,
            *[os.path.join(scratch, member) for member in members])
        run("ranlib", rebuilt)
        os.replace(rebuilt, archive)
    print(f"abc: namespaced {len(symbols)} CUDD symbols "
          f"with prefix {prefix}")


if __name__ == "__main__":
    main()
