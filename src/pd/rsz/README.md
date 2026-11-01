# pd/rsz: timing-repair engine

The OpenROAD v2.0 resizer engine (R1 step 2, Q2), as its own L4
module at src/pd/rsz. It repairs setup, hold, slew, capacitance,
fanout, and long-wire violations by gate resizing and buffer
insertion, driven by the merged dbsta adapter and the pdr shared
support module. It owns no command: the registry command
eos::repair_timing (next increment) wraps it, and the adapted
Tcl regression suite lives under this module's tests/ dir.

## Contents

- `include/rsz/Resizer.hh`, `src/Resizer.cc`: the repair engine
  (repairSetup, repairHold, repairSlew, repairCapacitance,
  repairFanout, repairWire, clock repair, cellWireDelay).
- `include/rsz/SteinerTree.hh`, `src/SteinerTree.cc`: Steiner-tree
  estimation for rebuffering, via `pdr::primDijkstra` and FLUTE.
- `include/rsz/BufferedNet.hh`, `src/BufferedNet.cc`: buffered-net
  model for rebuffering.
- `src/Rebuffer.cc`: rebuffering passes (bottom-up, top-down).
- `src/EstimateWireParasitics.cc`: estimated wire parasitics
  (pi model) for unrouted nets.
- `include/rsz/MakeResizer.hh`, `src/MakeResizer.cc`: ord-namespace
  factory (makeResizer/deleteResizer). The driver precedent
  constructs Resizer directly; the factory stays for future
  registry use.

## Dependencies

- eda_pd_dbsta (merged adapter): dbSta session, dbNetwork.
- eda_pd_pdr (shared support): primDijkstra, FLUTE.
- eda_db_odb, eda_analysis_sta.

## Wire RC (Q6 policy pointer)

The engine consumes wire RC as per-scene scalars via
setWireSignalRC / setWireClkRC (see EstimateWireParasitics.cc).
It never derives them and never defaults them: with RC unset,
wireSignalResistance/Capacitance return 0.0, findMaxWireLength
divides by zero, and the zero-RC pi model NaNs (STA-1040).
Derivation (tech load, from tlef RPERSQ/CPERSQDIST/
EDGECAPACITANCE), per-layer override (eos::set_wire_rc), and
the fail-fast backstop all live in the session/command layer,
not in this engine. The next increment implements them; until
then, callers must set RC explicitly, as the step-1 driver did
(sky130 met2 values). The per-layer layerRC storage exists in
the engine but is never read by the repair path (upstream
shape); the layer rule is decided at the command layer.

## Decisions recorded in PATCHES.md

- repairHold keeps the upstream shape: no P-28 graph refresh
  inside repairHoldPass. The outer repairHold loop re-times
  (findRequireds plus fresh findHoldViolations) at each pass
  boundary, so staleness is confined within a pass.
- ensure_linked / link_func_ are untouched: no rsz source
  calls ensure_linked, and the driver path binds the network
  via dbNetwork::init / setBlock without Tcl link_design.
- P-23 dormant load slew (load_slew falls back to drvr_slew;
  findMaxSlewWireLength uncalled) is recorded, not hidden.
- P-17 / P-19 / P-24 semantic mappings and the D-8
  liberty-loaded re-check are validated by the adapted Tcl
  regression suite (next increment), not claimed proven here.

## Link-check note (this increment)

The pdr link-check runs with this module: the repair driver
is linked with and without pdrev.cpp. If the without-link
passes the step-1 END metrics, pdrev.cpp is dropped from
the pdr module. Until that check runs, it stays.
