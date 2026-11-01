# dbsta: provenance

Module: src/pd/dbsta, target eda_pd_dbsta, layer L4 engines.
Adopted by place-and-route unit P1 (owner approval 2026-10-31,
ported dbNetwork authorized 2026-10-31 19:08 PDT).

## Upstream

OpenROAD, commit ead3236e56d57362a8dc8b25658fb41a191eba17, the
same commit that supplies the in-tree OpenDB (REPIN 2026-10-28).
Evaluation tree: ~/workspace/openroad-ead3236 (read-only; never
modified by this unit). No other commit is substituted anywhere
in this module.

## Imported files (port copies, not verbatim upstream)

Every file below is a port copy: the upstream file with the
adaptations listed in PATCHES.md applied, so it compiles against
the in-tree OpenSTA (commit 0f2529d) and the in-tree OpenDB
(ead3236). No file under src/analysis/timing/sta is modified, and
no upstream file is patched in place: the pin tree is untouched.

| EOS path | Upstream path | SHA-256 of the upstream file |
|---|---|---|
| include/db_sta/dbNetwork.hh | src/dbSta/include/db_sta/dbNetwork.hh | a803de89af6a225e2f10f4954ac3b15a6908acd4660a9fe45dc33775f4814a1f |
| src/dbNetwork.cc | src/dbSta/src/dbNetwork.cc | c4122c229ad58c0560ea8522f41451b407ae7b52a7fb2828aef7f7109c2a9199 |
| include/gui/gui.h | src/gui/include/gui/gui.h | f89f0697a669bbfa303ea687daa3327c8d982e4834d939bb5d8a399e338c6c0d |
| include/gui/heatMap.h | src/gui/include/gui/heatMap.h | 09a25544e7ab464a5f15181a4df6f6c15ba41be95c865258dfa5bdbb22f9cfc0 |
| include/gui/MakeGui.h | src/gui/include/gui/MakeGui.h | 04fde5de367da9605145da480de935b844dc7897d178c122df22c8bda1e331e9 |
| src/stub.cpp | src/gui/src/stub.cpp | c12ab79b35a67db0b223549419005468f31c437e99ba090259e73572e8836e0e |
| src/stub_heatMap.cpp | src/gui/src/stub_heatMap.cpp | 3870d965e47028f8cce1a30b48b0546a876e51af171b524dac89773a5ec2af99 |

## EOS-owned files (not upstream)

- include/ord/OpenRoad.hh and src/ord_shim.cc: the ord shim
  surface (see PATCHES.md, entry O-1). EOS-authored, no upstream
  text.
- CMakeLists.txt: EOS-owned build for the module.

## Licence audit

BSD-3-Clause only. Every imported file carries the BSD-3-Clause
header text (UC Regents); dbSta, gui carry no directory LICENSE
file, so the verdict is by header, the same as P0's verdict for
ifp, rcx and pdn. No GPL, LGPL, AGPL, MPL, EPL, MIT, Apache or
Boost licence text appears in any imported file. No licence
commitment is made by this unit.

## Deliberately not imported in P1

- dbSta itself (dbSta.hh/dbSta.cc), dbReadVerilog and
  dbSdcNetwork: the floorplan command needs none of them. ifp
  needs dbNetwork only; upf includes dbSta.hh upstream but uses
  no dbSta symbol, so the port drops that include (PATCHES.md,
  entry U-1) instead of importing the Sta subclass, its Tcl
  bindings and its GUI renderer. They land with the units that
  need them.
- The gui stub's Qt implementation: EOS builds the stub path
  only, as P0 records; no Qt dependency is added.
