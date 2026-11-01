# pd/pdr: shared physical-design support

Shared support module for physical-design engines (R1 step 2, Q3):
the FLUTE Steiner-tree lookup tables and the pdr Prim-Dijkstra
Steiner-tree implementation. This module is engine support, not an
engine: it owns no command and no repair policy. Consumers (rsz first)
link it and call `pdr::primDijkstra` / `stt::flute`.

## Contents

- `include/pdr/flute.h`, `src/flute.cpp`: FLUTE (BSD-3, upstream v2.0
  verbatim). The lookup tables are NOT committed: `data/POWV9.dat` and
  `data/POST9.dat` are committed as module data, and `CMakeLists.txt`
  generates `POWV9.cpp` / `POST9.cpp` at build time with the
  EOS-owned `tools/make_dat_var.py` (same argument contract as the
  upstream generator: var, out.cpp, in.dat).
- `include/pdr/graph.h`, `node.h`, `edge.h`, `src/graph.cpp`,
  `node.cpp`, `edge.cpp`: pdr graph primitives (upstream v2.0 plus the
  adaptations in PATCHES.md).
- `include/pdr/pdrev.h`, `src/pdrev.cpp`: Prim-Dijkstra Steiner tree
  (`pdr::primDijkstra`, `pdr::primDijkstraRevII`) plus the regression
  helper `reportSteinerTree`. The GUI display surface
  (`highlightGraph`, `highlightSteinerTree`, `LinesRenderer`) is
  deleted: EOS has no GUI in this layer, and no consumer calls it.
- `tools/make_dat_var.py`: EOS-owned table generator (see below).

## Licence decision (owner, 2026-11-01)

Upstream's table generator `src/stt/etc/MakeDatVar.tcl` is GPL-3.0
(Parallax Software 2019), not BSD-3 as the stage-1 audit stated,
so it is NOT imported: no upstream generator script appears in
this module. By owner decision 2026-11-01, the module uses the
EOS-owned generator `tools/make_dat_var.py` instead, verified
byte-identical against the upstream-generated tables on both
POWV9 and POST9. The generated POWV9.cpp / POST9.cpp files are
build artifacts and are never committed. The module's BSD-3-only
claim therefore holds: every other file is BSD-3 (see
PROVENANCE.md).

## ToolId note

EOS's `utl::ToolId` has no PDR entry, so the pdr sources log under
`utl::STT` (Steiner-tree home). The id only affects the log prefix.

## Link-check note (rsz stage)

`pdrev.cpp` is the only definition of `pdr::primDijkstra`, which the
resizer calls. At the rsz stage the repair run is linked with and
without `pdrev.cpp`; if the without-link passes, `pdrev.cpp` is
dropped from this module. Until that check runs, it stays.
