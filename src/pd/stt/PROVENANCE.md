# stt: provenance

Upstream: OpenROAD at commit ead3236e56d57, imported as EOS's own
code under unit P2 (placement) of the place-and-route phase, as a
support import for grt (Steiner trees for FastRoute). The pin tree
itself is never modified; the files below are copies into this
module, with the adaptations listed in PATCHES.md.

Note on pd.cpp: this module imports the ead3236 Prim-Dijkstra
(src/stt/src/pdr/src/pd.cpp), NOT the v2.0 pdr module's
primDijkstra. The two have different signatures (const-reference
parameters and the new vector-based stt::Tree here, versus
by-value parameters and the old array-based Tree in pdr) and
different mangled names, so both coexist; a translation unit
including only stt/pd.h calls this module's version. Linking stt
against the v2.0 pdr overload alone would be an ABI mismatch
(same function name shape, different Tree layout), which is why
the ead3236 pd.cpp is imported here rather than reused.

Note on FLUTE tables: flute.cpp declares extern std::string
post9 / powv9 inside namespace stt::flt. The tables are generated
at build time by the EOS-owned tools/make_dat_var.py from the pdr
module's committed .dat files (referenced by CMake path, not
duplicated). The upstream MakeDatVar.tcl is GPL-3.0 and is not
imported; the pdr module's generator precedent (same decision)
applies.

## Licence audit (run at import time, 2026-11-01)

Every file in the import set was grepped for licence text
(GNU/General Public/Lesser/Affero/Mozilla/Eclipse/Apache/MIT/Boost
phrases) and the LICENSE file under src/stt/src/flt was read
(BSD 3-Clause, Iowa State University 2018 and UFRGS 2019).
Result: BSD-3-Clause only. No copyleft text was found. The only
"gnuplot" hit is a comment about plot output format in flute.cpp.
The Tcl factory (MakeSteinerTreeBuilder) and the LinesRenderer GUI
helper are not imported.

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pd/stt) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| include/stt/SteinerTreeBuilder.h | src/stt/include/stt/SteinerTreeBuilder.h | 9b6f137b682a57b35a54817aeaf06b3764f059ad16f946ea32c3d821e96e10d4 |
| include/stt/flute.h | src/stt/include/stt/flute.h | e9d77bb27a487c00ab8106d565cb87337f831ec7fd83b9f6a31d710f00854e41 |
| include/stt/pd.h | src/stt/include/stt/pd.h | e8ea4050857b05c25f84d27a03189f1b374edd99c5b5c6132a7a7cd4e22586be |
| src/SteinerTreeBuilder.cpp | src/stt/src/SteinerTreeBuilder.cpp | 2ce8a8afc082b1e9a5b8185f7609d817826da5ae77dbed0f1ed9bf6ec3aee535 |
| src/flute.cpp | src/stt/src/flt/flute.cpp | ed70a4a10e3b246f37a26986e0ab63f231ccbe977f5a8ed5ed35c0aacf722dda |
| src/pd.cpp | src/stt/src/pdr/src/pd.cpp | 89533659f451df7ca8d7b4f3a4570ad78a514fdcbb256d57b9f51f540aad1169 |
