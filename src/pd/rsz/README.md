# pd/rsz: timing-repair engine

The OpenROAD v2.0 resizer engine (R1 step 2, Q2), as its own L4
module at src/pd/rsz. It repairs setup, hold, slew, capacitance,
fanout, and long-wire violations by gate resizing and buffer
insertion, driven by the merged dbsta adapter and the pdr shared
support module. The registry command eos::repair_timing (R1 step 2,
Q4, PR4) wraps it, and the adapted Tcl regression suite lives
under this module's tests/ dir.

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

## Wire RC (Q6 policy)

The engine consumes wire RC as per-scene scalars via
setWireSignalRC / setWireClkRC (see EstimateWireParasitics.cc).
It never derives them and never defaults them: with RC unset,
wireSignalResistance/Capacitance return 0.0, findMaxWireLength
divides by zero, and the zero-RC pi model NaNs (STA-1040).
Derivation, per-layer override, and the fail-fast backstop live
in the registry command layer (src/ui/registry/src/
repair_commands.cc), not in this engine:

- Derivation (tech load): for each routing layer, width from
  dbTechLayer::getWidth over dbu-per-micron; R_ohm_per_m =
  RPERSQ / width_m; C_F_per_m = (CPERSQDIST * width_um +
  2 * EDGECAPACITANCE) * 1e-6. A layer is complete only when
  width, resistance, and capacitance data are all present
  and positive; an incomplete layer can never be selected.
  Measured against sky130 met2: 8.93e5 ohm/m, 7.789e-11 F/m.
- Scalar selection, in order: (1) explicit -wire_r/-wire_c
  override (both together, positive); (2) explicit -wire_layer
  (or an eos::set_wire_rc override for that layer, which wins
  over the tech-derived value for the layer); (3) tech default
  layer: the lowest-numbered complete routing layer,
  excluding li1-class local-interconnect layers when a metal
  (non-li) complete layer exists. Clock RC defaults to the
  signal scalar; -wire_layer_clk overrides the clock side
  only. The chosen layer and source are printed in the
  result (wire_layer, wire_rc_source) so the choice is
  auditable.
- Fail-fast backstop (Q6): when no signal RC source resolves,
  the command fails with a catchable Tcl error naming the
  layer and the fix (load tech data, pass -wire_layer, or
  pass -wire_r/-wire_c). Never 0.0, never NaN, never silent.
  eos::set_wire_rc records a per-layer override on the
  command context (plain data, no engine types in the
  header), cleared by ResetProject on close_design.
- The per-layer layerRC storage exists in the engine but is
  never read by the repair path (upstream shape); the repair
  itself consumes one scalar per scene via setWireSignalRC.

## PR4 command (eos::repair_timing, eos::set_wire_rc)

The command is a self-contained per-call repair session: it
builds its own dbSta session from explicit file options
(-tlef, repeatable -lef, -def, repeatable -liberty, -sdc
or -clock/-period), mirroring the PR3 C++ driver sequence,
because the timing session has no LEF/DEF load path
(import_odb is binary .odb only). It does not depend on
ctx.timing and leaves no session state behind: every call
creates and destroys its own database, STA session, and Tcl
interp. Loader model: with -tlef, lefin createTech then
createLib per -lef; without -tlef, the first -lef goes
through createTechAndLib (Nangate45's combined file) and
the rest through createLib. SITE workaround (general, not
sky130-specific): lefin drops tech-file SITE definitions
when no library is being created, so before DEF read the
loader parses SITE blocks from the tech file and creates
any site missing from the tech via the odb API; a DEF that
still fails fails fast with the loader error, never on a
partial DB.

Pass flags, fixed order in one call: design, setup, hold,
tie, clk_nets, clk_inverters; only requested passes run.
With no pass flag the default is setup+hold, exactly
upstream repair_timing. Pass-specific options without
their pass are rejected fail-fast (for example -tie_port
without -tie, -max_wire_length without -design or
-clk_nets); -sdc and -clock/-period together are rejected
as ambiguous; -wire_r and -wire_c must be given together
and positive. Result keys (machine-readable, one line):
status, start/end WNS/TNS/hold, start/end instances,
upsized, inserted, removed, per-pass inserted/upsized/
removed, wire_layer, wire_rc_source, wire_r/c, clock-side
wire values, design area, utilization, utilization_limited,
buffer_limited, and tie cross-check fields when -tie runs
(every net driven by a tie instance must have exactly 2
connections). Limit errors RSZ-0050 (max-utilization) and
RSZ-0060 (max-buffer-count) are recorded as
utilization_limited/buffer_limited flags, further passes
stop, and the command still returns Ok with post-repair
metrics, mirroring the upstream catch-and-continue
scripts. Name resolution uses existing APIs only:
-end_pin inst/pin, -buffer_cell cell, -tie_port cell/pin
(repeatable, one repairTieFanout per port in order),
-clock_net net (dbNet lookup, setSigType CLOCK);
unresolved names fail fast with the name in the message.
dont_use globs expand through STA's own PatternMatch.

DECIDED (owner, 2026-11-01): fix F1 lands in PR4 and the
gcd anchor is re-baselined to the corrected loop's numbers
(see PATCHES.md, F1 entry). The old repairSetup loop is not
preserved, and the old-loop anchor numbers never appear as
expected results again except inside the re-baseline note.
The adapted Tcl suite scripts and .ok files are generated
under the F1 loop; the suite layout, the 42-adapt / 5-drop
disposition, the per-case cross-checks, and the gcd
command-path result live in tests/README.md.

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
  liberty-loaded re-check ride with the adapted Tcl
  regression suite (PR4, pending the owner's loop
  decision), not claimed proven here.

## Link-check note (this increment)

The pdr link-check runs with this module: the repair driver
is linked with and without pdrev.cpp. If the without-link
passes the step-1 END metrics, pdrev.cpp is dropped from
the pdr module. Until that check runs, it stays.
