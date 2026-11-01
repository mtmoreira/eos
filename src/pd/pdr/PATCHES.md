# pdr module: upstream adaptations (EOS-owned record)

Upstream sources are never patched. Every change below is either an
adaptation applied at import time (recorded here) or an EOS-owned file
(CMakeLists.txt, README.md, this file, PROVENANCE.md, the unit test).
Upstream pin: OpenROAD v2.0, commit a5a56584 (candidate tree).

## graph.cpp / node.cpp / edge.cpp

Source: the R1 step 1 driver build copies
(`eos-rsz-spike/r1step1/pdr/`), which equal the upstream files except:
- The 3-line driver header comment is stripped at import (it marked the
  copies as scratch-only).
- `graph.cpp` keeps the fmt 9 adaptation: explicit
  `fmt::formatter<pdr::Node>` / `fmt::formatter<pdr::Edge>` ostream
  formatters. Upstream relied on fmt 8 automatic ostream formatting,
  which fmt 9 removed; EOS builds against system fmt 9.1.0, and the
  formatters are required because `graph.cpp` formats Node/Edge values
  through `debugPrint` ("{}"). An explicit specialization wins over
  fmt 8's generic mechanism too, so the formatters are harmless if a
  future build ever drops back to fmt 8.
- `graph.cpp` keeps the PDR -> STT ToolId mapping: EOS's `utl::ToolId`
  has STT but no PDR, so every `debugPrint` / `debugCheck` site logs
  under `utl::STT`. Log prefix only; no behavior change.

## pdrev.cpp

Source: the R1 step 1 driver copy (`eos-rsz-spike/r1step1/pdrev_driver.cpp`).
Relative to upstream `src/pdr/src/pdrev.cpp`:
- `#include "gui/gui.h"` dropped.
- `using utl::PDR;` replaced by an STT-mapping comment; the two internal
  consistency error sites (666, 667) log under `utl::STT`.
- GUI section deleted (upstream lines 284-363): `LinesRenderer`,
  `PdRev::highlightGraph`, `highlightSteinerTree`. No consumer calls
  them (checked: no `highlightSteinerTree` / `highlightGraph` call in
  the rsz port tree).
- Additionally at import: the now-dead `PdRev::highlightGraph()`
  declaration is deleted from the class (its definition is gone).

## pdrev.h

Upstream `src/pdr/include/pdr/pdrev.h` minus the `gui` forward
declaration and the `highlightSteinerTree` declaration (definition
deleted above; leaving the declaration would invite a link failure).
`reportSteinerTree` is kept: it is logger-only and used by regressions.

## FLUTE (flute.h, flute.cpp, data, tools)

Upstream verbatim, no adaptation, except the generator (below).
`flute.cpp` builds with
`LUT_SOURCE LUT_VAR`, so the tables come from the generated
`POWV9.cpp` / `POST9.cpp`, not from runtime file reads: the unit test
needs no `.dat` files at run time, and the module needs no data-file
install step.

Generator decision (owner, 2026-11-01): upstream's
`src/stt/etc/MakeDatVar.tcl` is GPL-3.0 and is NOT imported.
The module's generator is the EOS-owned `tools/make_dat_var.py`,
which replicates the upstream argument contract (var, out.cpp,
in.dat) and output format without using or copying the upstream
script, and is verified byte-identical against the
upstream-generated tables on both POWV9 and POST9. The generated
tables stay build artifacts and are never committed.

## Build warnings (not patched, per the zero-patch rule)

`graph.cpp` emits two C++20 `-Wdeprecated` warnings (implicit `this`
capture via `[=]` in lambdas at two sort call sites). EOS does not
build with `-Werror`, so these are warnings only. They are upstream
text and are intentionally left alone: fixing them would be patching
upstream code for cosmetics.

`POWV9.cpp` (about 3.8 MB) and `POST9.cpp` (about 8.7 MB) are generated
by the CMake custom command in `CMakeLists.txt`, which runs python3
over `tools/make_dat_var.py` with the upstream argument order
(`powv9` / `post9`, output, input). If python3 is absent,
configuration fails fast with a message naming the requirement.

## Not imported

- `tools/MakeDatVar.tcl` (upstream FLUTE table generator): GPL-3.0,
  rejected by owner decision 2026-11-01; replaced by the EOS-owned
  `tools/make_dat_var.py` (see above).
- `MakePdrev.h` / `MakePdrev.cpp` (the ord factory): decided by the
  rsz-stage link-check, not by fiat. Expected not needed (no ord
  factory in EOS), but the check decides.
- `pdrev.i` (SWIG bindings): no upstream .i bindings are imported
  anywhere in this step (stage-1 plan 05).
