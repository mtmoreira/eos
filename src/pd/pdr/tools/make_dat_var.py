#!/usr/bin/env python3
"""EOS-owned FLUTE table generator for the pdr module.

This script IS the module's generator. It replaces upstream's
MakeDatVar.tcl by owner decision 2026-11-01: the upstream script
is GPL-3.0 and is not imported into this repo. This generator is
EOS-owned code with the same output contract: read a .dat file,
base64-encode its bytes, and emit a C++ file defining
namespace stt { std::string <var> = "<b64>"; }.

Usage: make_dat_var.py <var> <out.cpp> <in.dat>

Verified byte-identical against the upstream-generated POWV9.cpp /
POST9.cpp on both tables (see the module PROVENANCE.md).
"""
import base64
import sys


def main(argv):
    if len(argv) != 4:
        raise SystemExit("usage: make_dat_var.py <var> <out.cpp> <in.dat>")
    var, out_path, in_path = argv[1], argv[2], argv[3]
    with open(in_path, "rb") as f:
        encoded = base64.b64encode(f.read()).decode("ascii")
    with open(out_path, "w") as f:
        f.write("#include <string>\n")
        f.write("namespace stt {\n")
        f.write(f'std::string {var} = "{encoded}";\n')
        f.write("}\n")


if __name__ == "__main__":
    main(sys.argv)
