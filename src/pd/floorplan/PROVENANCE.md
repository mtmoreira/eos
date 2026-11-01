# floorplan: provenance

Module: src/pd/floorplan, target eda_pd_floorplan, layer L4
engines. Adopted by place-and-route unit P1 (owner approval
2026-10-31, ported dbNetwork authorized 2026-10-31 19:08 PDT).

## Upstream

OpenROAD, commit ead3236e56d57362a8dc8b25658fb41a191eba17, the
same commit that supplies the in-tree OpenDB (REPIN 2026-10-28)
and the dbsta module. Evaluation tree:
~/workspace/openroad-ead3236 (read-only; never modified by this
unit). No other commit is substituted anywhere in this module.

## Imported files (port copies, not verbatim upstream)

Every file below is a port copy: the upstream file with the
adaptations listed in PATCHES.md applied, so it compiles against
the in-tree OpenSTA (commit 0f2529d) and the in-tree OpenDB
(ead3236). No file under src/analysis/timing/sta is modified, and
no upstream file is patched in place: the pin tree is untouched.

| EOS path | Upstream path | SHA-256 of the upstream file |
|---|---|---|
| include/ifp/InitFloorplan.hh | src/ifp/include/ifp/InitFloorplan.hh | 200c8d2ab6d0f8793f070c91d3502fa583e92a7c109f0ba79446178512bcf838 |
| src/InitFloorplan.cc | src/ifp/src/InitFloorplan.cc | dbee1fd4ea468ec6fd1b973f850ef3c88635688c0b0d0976d1bcf3c249c9a7ba |
| include/upf/upf.h | src/upf/include/upf/upf.h | 332a40ddea7fe65ae35edd967fc274cefc0bd2167b452b156692180286b42bba |
| src/upf.cpp | src/upf/src/upf.cpp | ebcdd2a1eefd92b7dd013896625b86fb8b2ddeec44d7bec9c162e6e2100cec98 |

## EOS-owned files (not upstream)

- include/eos/pd/floorplan.h and src/floorplan.cc: the EOS
  runner over InitFloorplan (die/core areas, rows, tracks, IO
  report). EOS-authored, no upstream text.
- CMakeLists.txt: EOS-owned build for the module.

## Licence audit

BSD-3-Clause only. Every imported file carries the BSD-3-Clause
header text (UC Regents / Parallax Software); ifp and upf carry
no directory LICENSE file, so the verdict is by header, the same
as P0's verdict for them. No GPL, LGPL, AGPL, MPL, EPL, MIT,
Apache or Boost licence text appears in any imported file. No
licence commitment is made by this unit.

## Deliberately not imported in P1

- The ifp Tcl bindings (MakeInitFloorplan, InitFloorplan.tcl)
  and the SWIG interface: EOS registers eos::floorplan through
  its own command registry instead.
- IO placement as a separate engine (upstream ppl/IOPlacer):
  this pin version's InitFloorplan declares autoPlacePins but
  defines no such function, and IOPlacer is a separate engine
  for a later unit. eos::floorplan therefore runs
  initFloorplan plus makeTracks and reports IO placement as
  measured (placed BTerms over total BTerms); it does not invent
  a pin placer. A design whose pins arrive unplaced reports
  io_placed=0, which the acceptance numbers state rather than
  hide.
- Tie-cell insertion (insertTiecells): defined in the import and
  available to later units, but not driven by eos::floorplan v1,
  whose signature takes no tie cell.
