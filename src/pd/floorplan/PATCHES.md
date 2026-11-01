# floorplan: patch list (P1 port of ead3236 ifp + upf)

Every change below lives in the port copies in this module; the
pin tree at ~/workspace/openroad-ead3236 is untouched, and no
file under src/analysis/timing/sta is modified. Classes, as in
the resizer spike memo: (M) mechanical rename/signature, (A)
API-shape adaptation, (S) semantic-mapping decision needing
validation. The in-tree STA is OpenSTA commit 0f2529d.

## InitFloorplan.cc

- F-1 (M): sta/Vector.hh include deleted. The header is gone
  in-tree, and InitFloorplan uses nothing from it: its only
  Vector-era remnants were the using-declarations below.
- F-2 (M): using sta::stdstrPrint and using sta::StringVector
  deleted (both gone in-tree; StringVector is never used in the
  file). The two row-name constructions become
  std::format("ROW_{}", row_index + row), which is the same
  string the printf form produced.
- F-3 (M): FuncExpr::op_zero / op_one become
  FuncExpr::Op::zero / FuncExpr::Op::one (in-tree enum class
  Op, FuncExpr.hh). Same values, same comparisons.
- F-4: no other change. The odb calls (rows, tracks, die area,
  voltage domains, cutRows), the utl Logger / Validator calls
  and the eval_upf call are unchanged: the in-tree odb surface
  at ead3236 matches the pin's for every call this file makes,
  which is why the P0 probe's InitFloorplan failures were all
  routed through dbNetwork.hh and the two items above, not
  through odb itself.

## upf.cpp / upf.h

- U-1 (A): the db_sta/dbSta.hh include is dropped. upf.cpp
  uses no dbSta symbol (verified by inspection: its only STA
  uses are dbNetwork, FuncExpr and Liberty); importing the
  whole dbSta Sta subclass, its Tcl bindings and its GUI
  renderer for one unused include would have pulled P1 scope
  far past what ifp needs. The dbNetwork include stays.
- U-2 (M): FuncExpr::Operator::op_not / op_or / op_and
  become FuncExpr::Op::not_ / or_ / and_ (in-tree enum class
  Op). Same values, same switch.
- U-3 (M): upf.h's bare "db.h" include becomes "odb/db.h"
  (the in-tree odb header location, promoted to PUBLIC by P1's
  CMake change); "utl/Logger.h" is unchanged (in-tree utl).
- U-4: no other change. eval_upf's early return for designs
  with no power domains is unchanged, so a standard-cell design
  without UPF pays only the logic-port instantiation pass.

## EOS-owned runner (not a patch)

- R-1: include/eos/pd/floorplan.h and src/floorplan.cc are
  EOS-authored. The runner builds a dbNetwork over the live
  database (init, setBlock), hands it to InitFloorplan, runs
  initFloorplan plus makeTracks, and reports die/core areas,
  row count, track-grid count and placed-vs-total IO counts
  from the mutated block. It does not add engine behaviour:
  defaults (die area from the block, core area equal to the die
  area) and the site option are command-level choices,
  documented in the header.
