#!/usr/bin/env python3
"""EOS-owned FLUTE table generator for the stt module.

Adapted from the pdr module's generator (same output contract, but
the variables live in namespace stt::flt, as the ead3236 flute.cpp
declares them: extern std::string post9 / powv9 inside
namespace stt::flt). The upstream MakeDatVar.tcl is GPL-3.0 and is
not imported; this generator is EOS-owned code. It reads a .dat
file, base64-encodes its bytes, and emits a C++ file defining
namespace stt::flt { std::string <var> = "<b64>"; }.

The .dat inputs are the pdr module's already-committed data files
(src/pd/pdr/data/*.dat), referenced by CMake path, so the 12.5 MB
of tables are not duplicated in the repo.

Usage: make_dat_var.py <var> <out.cpp> <in.dat>
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
        f.write("namespace stt::flt {\n")
        f.write(f'std::string {var} = "{encoded}";\n')
        f.write("}\n")


if __name__ == "__main__":
    main(sys.argv)
