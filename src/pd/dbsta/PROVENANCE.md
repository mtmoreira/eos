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

Merged in R1 step 2 with OpenROAD v2.0 at a5a56584 (candidate
tree ~/workspace/openroad-v2.0-pin, read-only; never modified).
The merged dbNetwork.hh and dbNetwork.cc are neither verbatim
file: they are the P1 base with the R1 items in PATCHES.md
(M-1, M-2) applied. dbSta, dbReadVerilog and dbSdcNetwork are
v2.0 port copies with the M-3, M-4 and M-5 adaptations.

## Imported files (port copies, not verbatim upstream)

Every file below is a port copy: the upstream file with the
adaptations listed in PATCHES.md applied, so it compiles against
the in-tree OpenSTA (commit 0f2529d) and the in-tree OpenDB
(ead3236). No file under src/analysis/timing/sta is modified, and
no upstream file is patched in place: the pin tree is untouched.

| EOS path | Upstream path | SHA-256 of the upstream file |
|---|---|---|
| include/db_sta/dbNetwork.hh (MERGED) | ead3236 src/dbSta/include/db_sta/dbNetwork.hh | a803de89af6a225e2f10f4954ac3b15a6908acd4660a9fe45dc33775f4814a1f |
| include/db_sta/dbNetwork.hh (MERGED) | v2.0 src/dbSta/include/db_sta/dbNetwork.hh | d4606c3393ba46b2b22c7b5f1a60b63ad391906e089a1c56a45050b8124dff32 |
| src/dbNetwork.cc (MERGED) | ead3236 src/dbSta/src/dbNetwork.cc | c4122c229ad58c0560ea8522f41451b407ae7b52a7fb2828aef7f7109c2a9199 |
| src/dbNetwork.cc (MERGED) | v2.0 src/dbSta/src/dbNetwork.cc | e940ad1924b7cfd1a6b018319e91b0a6a1c8339393b1a0bb82ca2659af98d061 |
| include/db_sta/dbSta.hh | v2.0 src/dbSta/include/db_sta/dbSta.hh | a99a5d0775e386e7583fca11af799a2bf8b6e95fc0b2cc31be899981afb1987a |
| src/dbSta.cc | v2.0 src/dbSta/src/dbSta.cc | ac7d767dbe34e2a7eb33a22c8292dcaf98c5547ae067c386237cdb532c3866a1 |
| include/db_sta/dbReadVerilog.hh | v2.0 src/dbSta/include/db_sta/dbReadVerilog.hh | d4beb2ca4fe97a465d85d3f688fdfd66f7788345b426c39856ac1a9fabc8f8d6 |
| src/dbReadVerilog.cc | v2.0 src/dbSta/src/dbReadVerilog.cc | 42235af2a1990da7a1da3c61db2741757a5a8fa6b4a821db0efab4feb7ed788c |
| src/dbSdcNetwork.hh | v2.0 src/dbSta/src/dbSdcNetwork.hh | 7d370c7181f6c765732ed8d4c14378e4b06e6a09a5e93f05fbbe44272d07669c |
| src/dbSdcNetwork.cc | v2.0 src/dbSta/src/dbSdcNetwork.cc | 8c4f4e934e89f2d8c8c18fa1edf8abb6d8240c08484763496fae9b5c41b2ab2f |
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

## Deliberately not imported (merged scope)

- Upstream Tcl/SWIG surface (dbSta.tcl, dbSta.i): EOS drives
  via the C++ API plus its own registry (eos::repair_timing).
  The encoded dbSta Tcl sources are likewise not imported;
  see PATCHES.md entry M-3.
- MakeDbSta.hh (ord factory header): not imported. It carries
  GPL header text and EOS constructs dbSta directly (driver
  precedent). The ord shim (O-1) is the ord surface.
- Unused.cc: dead upstream code, never compiled.
- dbSta/test/* (adapter tests): out of the Q5 verdict scope
  (Q5 covers the repair_* set). Adapter behavior is covered
  by C++ unit tests under tests/unit/pd/dbsta/.
- Upstream CMakeLists: replaced by the EOS-owned module
  CMakeLists.
- The gui stub's Qt implementation: EOS builds the stub path
  only, as P0 records; no Qt dependency is added.
