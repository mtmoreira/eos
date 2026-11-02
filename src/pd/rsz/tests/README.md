# rsz Tcl regression suite (PR4)

The adapted upstream repair-driver suite, run through the
registry command eos::repair_timing (R1 step 2, Q4). Each
case is one Tcl script plus one .ok file; the CTest
harness (run_case.py, committed under this dir) runs the
script with build/apps/eos/eos, extracts the verdict line
starting with `status=ok` (or the error line for the
negative case), and diffs exactly that line against the
.ok file. A missing verdict or a nonzero eos exit fails
the case. Scripts are generated under the F1 loop (see
PATCHES.md); the old repairSetup loop is not preserved,
and the old-loop gcd anchor numbers never appear as
expected results except inside the re-baseline note
below.

## STATUS (measured, PR4)

- Scripts: all 42 adapted cases plus the gcd command case
  exist (repair_<case>.tcl, plus repair_gcd.tcl).
- .ok files: 28 exist (27 suite cases plus gcd). The
  other 15 suite cases segfault in the engine (see the
  crash table); a segfault produces no verdict line, so
  no .ok file can honestly be written for them. Their
  scripts are committed and marked CRASH below, not
  silently dropped, and they get .ok files only after
  the engine crashes are fixed.
- Harness: run_case.py is committed. Usage:
  `python3 run_case.py repair_setup1.tcl` from this dir
  (EOS_BIN overrides the eos binary path). A case with
  no .ok file fails the harness with ".ok file not
  found", which is the correct behavior until its crash
  is fixed.
- CTest entries: NONE yet. Every case is pending-licence
  (see below), so no case is runnable in CI. When the
  mock-library unit lands, entries go in
  tests/unit/CMakeLists.txt (the dbsta precedent: one
  add_test per case, COMMAND python3 run_case.py
  repair_<case>.tcl, WORKING_DIRECTORY this dir), the
  pinned ctest count in tools/tests/test_dep_graph.py
  moves from 30 by the entry count, and
  tools/dependency-graph.json is regenerated. The
  dep-graph rule mapping an add_test block mentioning
  rsz/tests to modules [rsz, registry] is already in
  place (tools/dep_graph.py), with a synthetic-repo
  pytest case covering it, so the future entries are
  selected on rsz/registry changes.
- Registry count: 52 (PR4 adds repair_timing and
  set_wire_rc), pinned in
  tests/unit/ui/registry/registry_test.cc.

## Disposition (47 upstream drivers; 42 adapt, 5 drop)

ADAPT: setup1, setup2 (design+setup, SDC file), setup3
(set_load via SDC), hold2, hold3, hold4, hold5
(allow_setup_violations, set_max_delay via SDC), hold6,
hold7 (max_utilization, catch -> utilization_limited),
hold8 (slack_margin .2, delays via SDC), hold9
(max_buffer_percent, catch -> buffer_limited), hold1
(pin hold via -end_pin + -buffer_cell, delays via SDC),
slew1, slew2 (extra lib/lef), slew3, slew4, slew5
(set_load + set_max_transition via SDC), slew6, slew8
(dont_use, expanded through STA's own PatternMatch),
slew9, cap1, cap2, cap3, fanout1, fanout2, fanout3 (no
upstream .ok; EOS .ok from the verified run,
cross-checked against the design pass counts), wire1,
wire2, wire4, wire5 (multi-lib/lef: repeatable
-lef/-liberty options), wire6, wire7, wire3 (NEGATIVE
case: missing-RC fail-fast, see below), tie1, tie2,
tie3, tie4 (design+tie), tie5, tie6, tie7, clk_nets1,
clk_inverters1.

DROP with reason: hold10 (needs global_route +
detailed_placement + global-routing parasitics; EOS has
no router until P2 grt lands), setup4, hold11, slew7,
wire8 (all genuinely two-corner: define_corners +
per-corner liberty; the command is single-scene, and
multi-scene session support is out of Q4 scope).
The drop list is part of the PR, not a silent cut.

wire3 special case (Q6 behavior change, NOT a silent
drop): upstream sets layer RC to 0.0 and repairs anyway
with warnings. EOS Q6 forbids proceeding on 0.0. The
adapted wire3 is a NEGATIVE case: eos::repair_timing
with -wire_layer naming a layer with no tech data must
fail with the fail-fast error naming the layer and the
fix, and the adapted .ok is that error line (Tcl error
path: the script uses catch and puts the error, so eos
exits 0 and the harness diffs the error line).
Documented as a verdict-mandated behavior change, not
as upstream parity. Layer choice: every real Nangate45
metal layer carries complete RC data, so no real layer
can exercise the fail-fast path; the script names
metal99, a layer absent from the tech, which takes
the same fail-fast branch. The .ok error line names
metal99 and the fix (load tech data, pass -wire_layer
naming a layer with data, or pass -wire_r/-wire_c).

Adapted .ok files are EOS-shaped (result lines, not
upstream transcripts). RSZ counts below are the
engine's own log lines (RSZ-0032 hold buffers,
RSZ-0038 wire buffers, RSZ-0039 design resizes,
RSZ-0040 setup buffers, RSZ-0041 setup resizes,
RSZ-0042 tie instances), compared against the
upstream .ok counts. Payload snapshot counts
(inserted/upsized) can differ from RSZ op counts
when one instance is resized more than once; where
they differ, the RSZ op count is the comparison
value (setup1: payload upsized 5, RSZ-0041 17;
setup3: payload upsized 1, RSZ-0041 2; gcd: payload
upsized 4, RSZ-0041 8).

## Per-case cross-checks (upstream .ok vs EOS run)

MATCH means the RSZ op counts agree. MISMATCH and
VACUOUS and CRASH are recorded, not waved through.

- setup1: MATCH. Upstream 4 inserted, 17 resized,
  RSZ-0062. EOS 4 inserted (RSZ-0040), 17 resized
  (RSZ-0041), RSZ-0062, end -0.011657 ns (upstream
  -0.012). This is the F1 exact-match evidence.
- setup2: MISMATCH (split). Upstream design pass: 3
  inserted (RSZ-0038), 4 resized (RSZ-0039); setup
  pass: 3 resized (RSZ-0041). EOS design pass: 0
  inserted, 2 resized; setup pass: 1 resized. Cause:
  upstream repair_design defaults to a computed max
  wire length (853 um, RSZ-0058) and repairs 3 long
  wires; EOS passes max_wire 0 (none) without an
  explicit -max_wire_length, so it skips wire repair
  and the resize counts shift with it. Adopting the
  computed default would invoke findMaxWireLength,
  whose load-slew approximation is P-23-dormant and
  needs an explicit decision first; it is not adopted
  silently. End WNS EOS -0.109683 ns (upstream
  -0.10 after). Design+setup totals: upstream 3
  inserted + 7 resized; EOS 0 inserted + 3 resized.
- setup3: MATCH (ops). Upstream 2 resized (RSZ-0041),
  RSZ-0062. EOS RSZ-0041 2, RSZ-0062, end -1.788579 ns
  (upstream .ok carries no end slack). SDC carries
  the clock because the command rejects -sdc with
  -clock/-period.
- hold1: MISMATCH. Upstream 2 hold buffers, repaired.
  EOS 4 hold buffers (RSZ-0032), RSZ-0060 max buffer
  count, RSZ-0066 unable to repair, end hold still
  -0.086759 ns (unchanged from start). Cause: the
  pin-hold loop inserts until the cap because the
  inserted buffers are invisible to re-timing (no
  graph rebuild in the hold path; contrast F1 for
  setup). The 20% default cap fires on top of that.
  Raising the cap to 100% inserts 16 buffers and
  still does not move the slack, confirming the
  visibility cause, not the cap. Follow-up needs
  owner decision (hold-path rebuild mirroring F1).
- hold2, hold3, hold4: VACUOUS (kept doc-literal).
  Upstream 11 / 2 / 3 hold buffers. EOS 0 inserted
  in all three: the design-doc -clock/-period form
  makes an ideal clock, and without propagated-clock
  skew (and, for hold2, without the input/output
  delays) the designs have no hold violations to
  repair. Faithful SDCs (propagated clock plus
  delays) were probed and segfault in whole-design
  hold (see crash table), so the committed scripts
  stay doc-literal and these three cases must NOT be
  read as hold-repair coverage.
- hold5: MATCH. Upstream 0 inserted (blocked by the
  max_delay setup constraint). EOS 0 inserted, end
  hold still -0.142044 ns, setup still violated.
- hold6, hold7, hold8, hold9: CRASH (see crash table).
  Upstream counts for reference: hold6 3 buffers;
  hold7 114 buffers then RSZ-0050; hold8 40 buffers;
  hold9 20 buffers then RSZ-0060.
- slew1, slew2, slew3, slew4, slew5, slew6: MATCH.
  Upstream RSZ-0039 resized 1, 1, 1, 2, 7, 1. EOS
  RSZ-0039 identical in each case, 0 inserted.
- slew8, slew9: CRASH (see crash table). Upstream:
  slew8 14 buffers in 1 net; slew9 1 buffer,
  1 resized.
- cap1, cap2: MATCH. Upstream RSZ-0039 resized 1
  each. EOS identical, 0 inserted.
- cap3: CRASH (see crash table). Upstream 1 buffer,
  1 resized.
- fanout1: CRASH (see crash table). Upstream 4
  buffers in 1 net, 2 resized.
- fanout2: NO-COUNT both sides. Upstream .ok carries
  no repair lines (wire-load models, no RC, no
  estimate). EOS 0 inserted, 0 resized with the
  tech-default metal1 RC and estimate. Outcome
  agrees (no repair recorded); whether the default
  changes the result cannot be established from a
  .ok with no count, and is recorded as such.
- fanout3: VERIFIED-RUN (no upstream .ok). EOS
  RSZ-0038 3 buffers in 2 nets, 0 resized. The
  upstream -buffer_cell BUF_X1 option is not
  carried: EOS -design takes no buffer-cell option
  and the engine picks buffers internally.
- wire1, wire2, wire4, wire5, wire6, wire7: CRASH
  (see crash table). Upstream counts: wire1 2
  buffers + 2 resized; wire2 3 + 2; wire4 3 + 2;
  wire5 2 + 1; wire6 3 + 2; wire7 3 + 2.
- wire3: NEGATIVE PASS. .ok is the fail-fast error
  line (metal99 named, fix named). No repair runs.
- tie1, tie2, tie3, tie4, tie5, tie6, tie7: MATCH.
  Upstream RSZ-0042 tie instances 5, 4, 7, 10, 4, 2,
  2+2. EOS tie_inserted identical in each case and
  tie_bad_nets 0 in all seven. tie6 locations match
  upstream report_ties exactly (1.1,1.1; 10.1,10.6).
  tie4 design pass correctly inserts/resizes nothing
  on the tie net (design_inserted 0, design_upsized
  0). The tie cross-check counts bterms as well as
  iterms: a tie net driving an output port (tie6)
  has 1 iterm plus 1 bterm, which is 2 connections
  and passes upstream check_ties (tie6 .ok shows no
  check error); counting iterms only flagged it
  wrongly, fixed in repair_commands.cc.
- clk_nets1: CRASH (see crash table). Upstream .ok
  carries no repair count.
- clk_inverters1: NO-COUNT upstream. EOS 2 inserted,
  1 removed, 0 resized. Upstream ran with no wire RC;
  EOS applies the tech-default metal1 RC and
  estimate. Whether the default changes the result
  cannot be established from a .ok with no count.
- gcd: COUNTS MATCH, ANCHOR IS THE COMMAND RESULT
  (see below). EOS command path: RSZ-0040 44,
  RSZ-0041 8, end_instances 1090, RSZ-0062, START
  -0.294548 / -0.896869 / 0.465122 ns, END WNS
  -0.115676 ns, TNS -0.487945 ns. The .ok pins the
  command result, and the command numbers are the
  anchor (owner confirmation 2026-11-01). The
  driver numbers (START -0.183052 / -0.412361 /
  0.456131 ns, END -0.0852105 / -0.273604 /
  0.46329 ns) are SUPERSEDED: a bounded
  investigation proved the delta is entirely
  measurement configuration, not an engine
  difference (pre-estimation explains START, the
  clock-RC default explains END; combined they
  reproduce the command numbers exactly). Wire RC
  itself matches: tech-derived met2 892857.143
  ohm/m, 7.7889922e-11 F/m vs driver 8.93e5,
  7.789e-11. Do not adjust numbers to fit.

## GCD command case and re-baseline note

The gcd script (repair_gcd.tcl) runs setup then hold
(the command default with no pass flag, matching the
driver order) on the routed 6_final.def with the
degraded SDC (core_clock 2.9 ns) and -wire_layer
met2. RE-BASELINE NOTE: the step-1 anchor (END WNS
+0.002058 ns, TNS 0, 22 inserted, 2 resized, 1068
instances) was optimistic-loop output. Under the
corrected F1 loop the anchor is the COMMAND
numbers (owner confirmation 2026-11-01): START
-0.294548 / -0.896869 / 0.465122 ns, END WNS
-0.115676 ns, TNS -0.487945 ns, 44 inserted,
8 resized operations, 1090 instances, RSZ-0062
unable to repair, as pinned by repair_gcd.ok.
The driver-path numbers (START -0.183052 /
-0.412361 / 0.456131 ns, END -0.0852105 /
-0.273604 / 0.46329 ns) are SUPERSEDED: they
reflect the spike harness configuration (no
pre-estimation, clock RC unset), not a second
valid result. The old numbers never appear as
expected results again except inside this note.

## Crash table (engine segfaults, SIGSEGV exit -11)

These are engine crashes, not script errors: the
bisects below vary one option at a time on the same
inputs. No gdb exists on this box, so crash
mechanisms are unknown; these paths were never
exercised before PR4 (the PR3 driver ran setup+hold
only on gcd). Fixing them is engine work needing
owner decision (F2-class), not a script change, and
no case below is worked around by weakening it.

- wire1, wire2, wire4, wire5, wire6, wire7: trigger
  is -max_wire_length above 0. wire6 without the
  length option passes (0 inserted). All six crash
  with it, with or without an SDC.
- clk_nets1: trigger is the -clk_nets pass (clock
  nets + max_wire_length 600 um). No finer bisect
  was run; recorded as the clk_nets pass.
- cap3: trigger is -max_cap_margin 10. Same DEF
  with a plain -design passes (0 inserted,
  1 resized).
- slew9: trigger presumed -max_slew_margin 5 by
  analogy with cap3 on the same DEF (cap3's margin
  bisect is the evidence; slew9 without margin was
  not separately bisected).
- slew8: trigger is the met2/met5 wire layers.
  Same DEF with met3 passes with and without the
  dont_use globs, so dont_use is exonerated and the
  layers are implicated. Mechanism unknown.
- fanout1: trigger needs the design pass plus the
  max_fanout SDC on the fanout1 DEF. Evidence:
  cap1 DEF + fanout SDC crashes; cap1 DEF +
  clock-only SDC passes; cap1 DEF + fanout SDC +
  setup pass (no design) passes; fanout3 with the
  same constraint class passes. DEF-dependent.
- hold6: trigger is -allow_setup_violations. Same
  SDC and DEF with a plain -hold passes (blocked,
  0 inserted, same as hold5).
- hold7: trigger needs -slack_margin 0.4 and/or
  -max_utilization 13. Plain -hold on the gcd
  placed DEF passes with 0 inserted (ideal clock
  finds no violations there).
- hold8: trigger is whole-design hold with real
  violations from the SDC (delays + propagated
  clock). Removing -slack_margin still crashes;
  plain -hold with an ideal clock passes vacuous.
- hold9: trigger needs -slack_margin 0.4 and/or
  -max_buffer_percent 2. Buffer-percent alone
  passes with 0 inserted (ideal clock, no
  violations).
- Probes (not committed scripts): whole-design
  hold with faithful SDCs crashes for hold2
  (propagated clock + input/output delays) and
  hold3 (propagated clock only). Together with
  hold6/7/8/9, the pattern is: whole-design
  repairHold segfaults when it actually tries to
  repair violations, while pin-hold (hold1) runs
  but does not fix (see hold1 cross-check).

## OPEN runtime items: outcomes

- slew8 dont_use expansion: the command expands
  globs through STA PatternMatch (same matcher
  upstream uses). The two globs match real liberty
  cells (grep on the full sky130hd liberty: 1 cell
  matching sky130_fd_sc_hd__probe_p_*, 1 matching
  sky130_fd_sc_hd__probec_p_*), the option is
  accepted, and a met3 run with dont_use completes.
  Whether dont_use changes repair choices is NOT
  directly observed, because the full slew8 case
  (met2/met5) crashes; recorded as such.
- tie2 separation unit conversion: upstream
  -separation is in Liberty units (microns, per the
  OpenROAD resizer docs); EOS -separation is in
  meters, so the script passes 0.000002 m for
  upstream 2. tie2 matches upstream (4 tie
  instances, tie_bad_nets 0).
- fanout3 buffer-cell handling: not carried (see
  fanout3 cross-check). No upstream .ok exists to
  compare against.
- fanout2 and clk_inverters1 no-RC vs default:
  recorded in their cross-checks (no-count
  upstream, so no count delta can be established).
- SDC-plus-clock conflicts: the command rejects
  -sdc with -clock/-period, so setup3, slew1, and
  fanout1 carry their clocks inside their SDCs.
  This is a documented deviation from the design
  doc's terse option map, not a silent
  substitution: the clock values are unchanged.
- hold7/hold9 clock naming: upstream names the
  clock core_clock on port clk; EOS -clock names
  clock and port alike, so the scripts use clk.
  Timing is equivalent; only the name differs.

## Pending-licence (no CTest entries)

All 42 cases plus gcd are pending-licence: each
references PDK lib/lef files that are not committed,
and no case is runnable in CI until the
mock-library unit ports them to EOS-owned data.
Per-case feature needs (cells, arcs, constraints,
tech parameters, SITE/ROW) live in
pending-licence-needs.md. Excluded from the commit,
with reasons:

- Any Nangate45 lib/lef, full or trimmed, including
  Nangate45_typ.trimmed.lib and its .cells.txt
  sidecar: proprietary header (Copyright 2004-2011
  Nangate Inc., trade secrets). Owner rule: no
  Nangate files anywhere, ever. Local use to
  generate .ok files is fine; committed content
  contains zero Nangate file content. This includes
  gcd_nangate45_placed.def (the DEF hold7 and hold9
  reference): it is a Nangate45-technology placed
  DEF, so it stays local-only even though it is a
  DEF and not a lib/lef.
- repair_slew2.lib/lef, repair_fanout3.lef,
  repair_tie7.lib: these small test files carry the
  same Nangate proprietary header verbatim, so they
  are excluded like the PDK files (their cases are
  pending-licence regardless). repair_slew4.lib is
  excluded on the same rule: it carries a Nangate
  copyright comment ("Copyright (c) 2004-2011
  Nangate Inc. All Rights Reserved."), found by the
  final pre-commit grep; only repair_slew4.lef is
  committed. An earlier "clean" call on slew4.lib
  was wrong and is corrected here.
- fakeram45_64x32.lib/lef: Nangate45-family files,
  excluded under the broad no-Nangate rule (wire5
  is pending-licence regardless).
- Any sky130 tlef/lef (hd and hs, tech and std-cell):
  not in this PR. sky130 cases are pending-licence
  exactly like the Nangate cases, pending the future
  mock-library unit, not pending any acceptance
  verdict.
- Full sky130 libs (sky130hd_tt.lib,
  sky130hs_tt.lib, ORFS tt lib): not committed;
  only the trimmed sky130hd lib below is.
- gcd inputs (6_final.def, gcd_degraded.sdc, ORFS
  tlef/merged lef/tt lib): local-only flow
  artifacts, not committed; the gcd script and .ok
  are committed and the inputs are described in
  pending-licence-needs.md.
- Dropped-case DEFs (repair_hold10.def and friends):
  not committed; their cases are dropped, not
  pending.

Committed fixtures (includable, verified by grep to
carry no licence/copyleft text): the 9 generated
EOS-owned DEFs (cap1, cap2, fanout1, slew1, slew2,
slew6, slew8, max_slew_cap, tie4); the per-case
port-tree DEFs for adapted cases except
gcd_nangate45_placed.def (see above). One committed
DEF carries a Nangate string: repair_setup2.def has
an upstream provenance comment naming its source
file (td1_nangate_netlist_1000.def). That is a
filename in a comment, not PDK lib/lef content, and
the file carries no licence text;
repair_setup2.sdc
(as shipped) and the adapted repair_<case>.sdc
files; repair_slew4.lef, repair_tie7.lef,
pad.lib/pad.lef (clean upstream test fixtures;
repair_slew4.lib is excluded, see above);
sky130hd_tt.trimmed.lib plus its .cells.txt
sidecar (no licence text; trimmed for the slew6
DEF, 36 cells; the suite scripts reference the
full lib names, so the trimmed lib is a
forward artifact for the mock unit, not a file
any committed script loads).

## Fixture provenance and licences

- Per-case inputs (DEF, SDC, extra lib/lef) come from
  the R1 spike port tree (rsz-spike-port/rsz/test/) and
  the OpenROAD v2.0 pin tree (openroad-v2.0-pin/test/).
  The port tree's Nangate45/sky130hd symlinks are
  dangling in this workspace; the pin tree is the
  source of truth for the PDK files, and pad.lib/
  pad.lef come from the pin tree (the port-tree
  pad symlinks are dangling too). Per-case fixture
  copies in fixtures/ that are excluded above stay
  untracked and are never staged.
- Generated DEFs (fixtures/*.def, 9 files: cap1, cap2,
  fanout1, slew1, slew2, slew6, slew8, max_slew_cap,
  tie4) are EOS-owned, regenerated byte-identically
  by gen_fixtures.py, and carry no licence text.
  Regenerate: `python3 gen_fixtures.py fixtures`
  from this dir; the generator writes only the 9 DEFs
  and prints their sizes.
- Trimmed libraries (fixtures/*.trimmed.lib) are
  produced by trim_lib.py from the full PDK libraries.
  The tool preserves the library header verbatim,
  including any licence header, and drops only
  non-selected cell groups. LICENCE FLAGS, read before
  any PR includes these files (trimming does not
  change a file's licence status):
  - Nangate45_typ.trimmed.lib: the full library carries
    a proprietary header (Copyright 2004-2011 Nangate
    Inc., "All rights reserved", trade secrets),
    preserved verbatim by the trimmer. NEVER
    committed (see pending-licence above).
  - sky130hd_tt.trimmed.lib: the full library carries
    no licence text, and neither does the trimmed
    output. Committed (see above). The sky130 tech
    files DO carry Apache-2.0 headers (sky130hd.tlef,
    sky130_fd_sc_hd_merged.lef, Copyright 2020 The
    SkyWater PDK Authors); those files are NOT
    committed in this PR.
  - Trimmed LEF files are not committed.
- Trimming rule (trim_lib.py): keep all non-cell
  top-level groups verbatim (templates, operating
  conditions, header comments); drop only non-selected
  cell groups by brace matching. Keep set = DEF
  -referenced masters, plus drive-strength variants of
  their families, plus buffer / inverter / clock-buffer
  / tie families (Nangate45: BUF_X*, INV_X*, CLKBUF_X*,
  LOGIC1_X*; sky130: __buf_*, __inv_*, __clkbuf_*,
  __conb_*). Each trimmed lib ships with a .cells.txt
  sidecar listing the kept cells for audit.
  Trimming equality evidence (full vs trimmed, same
  command, same inputs, full result payload compared):
  cap1 design pass, Nangate45, identical payloads (61
  instances, 1 upsized, metal3 tech RC); slew6 design
  pass, sky130, identical payloads (51 instances, 1
  upsized, met3 tech RC). Trimmed sizes: Nangate45
  134 cells / 6,691,483 bytes to 25 cells / 200,426
  bytes; sky130 428 cells / 12,800,135 bytes to 36
  cells / 417,537 bytes. The committed trimmed lib
  is the sky130 one; the Nangate45 trimmed lib stays
  local-only.

## CTest harness

- One add_test entry per adapted case is the target
  form (see STATUS: none yet, all pending-licence),
  invoking build/apps/eos/eos with the case script
  and cwd = this dir. The dependency graph maps an
  add_test block mentioning rsz/tests to modules [rsz,
  registry] (tools/dep_graph.py), so a change to
  either module selects the suite entries once they
  exist.
- The driver extracts the verdict line and diffs
  exactly that line. Missing verdict or nonzero eos
  exit fails the case. No failure-hiding
  normalization: logger lines start with
  [INFO]/[WARNING]/[ERROR] and can never match the
  verdict prefix, so the filter is mechanical.
- Unconstrained cases (cap2, fanout2, fanout3, tie*,
  slew3, slew4, slew5, slew6, wire3-adjacent) report
  the STA no-clock worst-slack sentinel
  (1000000015047466219876688855040000000000.000000)
  in start/end WNS and hold fields. It is
  deterministic for this STA build and is pinned
  verbatim in the .ok files; it is a sentinel, not
  a timing measurement.
