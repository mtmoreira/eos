# EOS mock library / PDK (v1)

EOS-owned synthetic test data for the resizer suite. It lets every
engine feature be tested in CI without any real PDK file.

## Guardrails (read first)

- **Synthetic values only.** Every number in `eos_mock.lib`,
  `eos_mock.lef`, and `eos_mock.tlef` was authored for this mock
  (linear delay tables, round layer parameters). No Nangate text,
  numbers, or cell data; no sky130 numbers. Format compatibility
  only: the files parse with the same readers as real PDK files.
  If any value's provenance becomes doubtful, it is replaced with
  an authored synthetic one and this README says so.
- **Mechanics, not correlation.** A mock-based run validates that
  the engine does the right thing (inserts, resizes, ties off,
  fails fast on a missing layer). It never validates timing,
  power, or area against silicon or against a real PDK. **No
  mock-based run may ever be cited as a signoff claim.** Real-PDK
  correlation stays with the external ORFS baselines.
- **Naming rule.** Library `eos_mock`, cells `EM_*`, layers
  `em*`. A mock result can never be mistaken for a real PDK
  result. The one exception is `PAD`, reused unchanged from the
  committed `fixtures/pad.lef` for the tie3/tie5 cases (that file
  is committed and clean, and its pins are the cases' own).
- **Enrichment loop.** Each new engine need adds a cell, an arc,
  or a tech parameter together with the test that failed without
  it. Regenerate with `python3 gen_mock.py` (byte-identical
  output), port the case with `python3 port_cases.py`, and record
  the addition here.

## Files

- `gen_mock.py`: the single source of truth. Generates the three
  files below. Edit values here, never by hand in the outputs.
- `eos_mock.lib`: 39 cells. Buffers, clock buffers, inverters,
  weak buffer, combinational cells, DFF, DFF with reset, tight
  slew DFF, latch, tie cells, fill/tap, plus the LEF-only macro
  in the LEF (no Liberty cell, so a load without timing is
  exercised).
- `eos_mock.lef`: 40 macros (the 39 Liberty cells plus
  `EM_LEFONLY`). Core height 3.2 um for all core cells.
- `eos_mock.tlef`: 10 routing layers. Tech-derived wire scalars
  (R = RPERSQ / width, C = CPERSQDIST * width +
  2 * EDGECAPACITANCE, per the command formula):
  - em1: R 1,000,000 ohm/m, C 5.0e-08 F/m
  - em2: R 750,000 ohm/m, C 3.8e-08 F/m
  - em3: R 333,333.33 ohm/m, C 3.3e-08 F/m
  - em4: R 175,000 ohm/m, C 3.0e-08 F/m
  - em5: R 100,000 ohm/m, C 2.6e-08 F/m
  - em6: R 66,666.67 ohm/m, C 2.48e-08 F/m
  - em7: R 37,500 ohm/m, C 2.4e-08 F/m
  - em8: R 25,000 ohm/m, C 2.2e-08 F/m
  - em9: R 16,666.67 ohm/m, C 2.0e-08 F/m
  - em10: R 10,000 ohm/m, C 1.8e-08 F/m
  The default layer is the lowest-numbered complete routing
  layer (em1). The negative wire3 case uses em99, which is
  absent from this file by design, so the same fail-fast branch
  fires with the layer named in the message. Layers em4 and
  em6 through em10 exist because the setup2 and tie5 DEFs
  reference those layers in TRACKS statements, and the DEF
  parser rejects undefined layers; they carry no repair
  semantics in v1.
- `port_cases.py`: ports the original suite cases onto the mock
  (see "Porting transform" below). Deterministic: re-running
  overwrites identical bytes.
- `repair_<case>.tcl`, `repair_<case>.def`, `repair_<case>.sdc`:
  the ported cases. Run with `python3 run_case.py
  mock/repair_<case>.tcl` from `src/pd/rsz/tests`.
- `repair_<case>.ok`: pins mock behavior (the verdict line of a
  mock run). These are new: they do NOT assert upstream parity,
  because the values are synthetic.

## Value model

- Delay tables are linear: delay = base + a*slew + b*load, with
  monotone tables over slews [0.01, 0.05, 0.2, 0.8] ns and loads
  [0.001, 0.005, 0.02, 0.1] pF. Stronger drive means a smaller
  load coefficient (lower drive resistance), so the resizer's
  weakest-first sort orders each family correctly.
- Drive families share a footprint so the resizer can upsize:
  `EM_BUF`, `EM_CLKBUF`, `EM_INV`. All other cells use their own
  name as footprint.
- Input caps: buffer/inverter inputs 0.002 (X1) to 0.011 (X8) pF;
  combinational inputs 0.002 to 0.004 pF; DFF D 0.002 pF, CK
  0.0025 pF.
- Output max_capacitance 0.13 pF generally, except DFF Q 0.10 pF
  (a 60-load DFF fanout at 0.002 pF per pin = 0.12 pF violates it:
  the cap1 shape; a 30-load fanout = 0.06 pF does not: the slew1
  shape) and EM_O21AI_X1 Y 0.05 pF (a 50-load fanout = 0.10 pF
  violates it: the slew6 shape).
- Input max_transition 1.5 ns generally, except EM_BUFWK input A
  0.08 ns (the slew4 shape) and EM_DFF_SLEW Q 0.10 ns (the slew2
  shape).
- Combinational output pin is ZN (Nangate convention, confirmed
  by the setup2 DEF for AND2/NAND2/XNOR2 and applied to the
  whole family). Buffer outputs are Z, inverter outputs ZN,
  O21AI output Y (confirmed by the slew6 DEF). The LEF-only
  macro carries CK and D (confirmed by the fanout3 DEF). The
  latch enable is GATE_N (confirmed by the setup3 DEF).
  Unported cells' pin names are assumptions until a DEF that
  uses them proves otherwise; the port script fails loudly on
  any mismatch, which is the intended tripwire.

## Porting transform

Ported DEFs are byte-preserved except for: master renames
(original cell to EM_* mock cell), ROW statements dropped (the
coverage spec explicitly allows this; it removes the need for a
SITE workaround), PIN layer names metal*/met* to em*, units
normalized to 2000 dbu/micron with exact integer coordinate
scaling (micron distances, hence wire lengths, unchanged), and
sky130 naming deltas (DFF CLK to CK, buffer/inverter X/Y to
Z/ZN). Ported SDCs are verbatim except hold2/hold3 (below) and
the hold5 pin rename (r1/CLK to r1/CK).

Port table (mock script vs original). Status "run" means the
case passes on the mock and has a CTest entry plus a .ok file.
Status "engine-crash" means the port exists (script, DEF, SDC)
but the run segfaults in the engine (see Pending); it has no
CTest entry and no .ok file until the engine fix lands.

| case | original inputs | mock notes | status |
| ---- | --------------- | ---------- | ------ |
| setup1 | Nangate, metal3 | verbatim otherwise | run |
| setup2 | Nangate, metal3/metal5, SDC | TRACKS layers em4/em6-10 added for this DEF | engine-crash |
| setup3 | sky130, met1, SDC | DFF/latch pin renames | run |
| hold1 | Nangate, metal1, SDC, -buffer_cell BUF_X1 | buffer_cell EM_BUF_X1 | engine-crash |
| hold2 | Nangate, metal1, ideal clock | faithful port: propagated-clock SDC with input delays 0 and output delay -0.3 (the committed script's ideal clock is vacuous for hold repair) | engine-crash |
| hold3 | Nangate, metal1, ideal clock | faithful port: new propagated-clock SDC replaces -clock/-period | engine-crash |
| hold4 | sky130hs, met1 | buf/dfxbp renames, X/CLK pin renames | run |
| hold5 | sky130hs, met1, SDC | same DEF as hold4; SDC pin rename r1/CLK to r1/CK | run |
| slew1 | Nangate, metal3, SDC | verbatim otherwise | engine-crash |
| slew2 | Nangate, metal3, extra lef/lib | extra files folded into the mock (EM_DFF_SLEW) | engine-crash |
| slew3 | Nangate, metal3, SDC | verbatim otherwise | engine-crash |
| slew4 | Nangate, metal3, extra lef/lib | extra files folded into the mock (EM_BUFWK) | engine-crash |
| slew5 | Nangate, metal3, SDC | verbatim otherwise | engine-crash |
| slew6 | sky130, met1 | o21ai/dfxtp renames | engine-crash |
| cap1 | Nangate, metal3 | DFF Q max_cap shape | engine-crash |
| cap2 | Nangate, metal3 | verbatim otherwise | engine-crash |
| fanout2 | Nangate, SDC | verbatim otherwise | run |
| fanout3 | Nangate, extra lef, SDC | extra file folded into the mock (EM_LEFONLY) | engine-crash |
| wire3 | Nangate, metal99, catch form | negative case; em99 absent by design | run |
| tie1 | Nangate, LOGIC1_X1/Z | tie_port EM_TIEHI/Z | run |
| tie2 | Nangate, LOGIC1_X1/Z, -separation | tie_port EM_TIEHI/Z | run |
| tie3 | Nangate, pad.lef, LOGIC1_X1/Z | pad.lef retained; tie_port EM_TIEHI/Z | run |
| tie4 | Nangate, SDC, LOGIC1_X1/Z | tie_port EM_TIEHI/Z | run |
| tie5 | Nangate, pad.lef, LOGIC1_X1/Z | pad.lef retained; tie_port EM_TIEHI/Z; TRACKS layers em4/em6-10 | run |
| tie6 | Nangate, LOGIC1_X1/Z | tie_port EM_TIEHI/Z | run |
| tie7 | Nangate, extra lef/lib, TIE_X1/Z0+Z1 | extra files folded into the mock (EM_TIEDUAL) | run |
| clk_inverters1 | Nangate, clk1 | verbatim otherwise | run |

## Pending (explicitly not cut)

13 ported cases crash in the engine at one site: STA
replaceCell walks to a null vertex (gdb: Sta::replaceCell
via Resizer::replaceCell via resizeToTargetSlew, faulting in
VertexInEdgeIterator via delaysInvalidFromFanin). Two callers:
makeRepeater (setup2, slew1-6, cap1, cap2, fanout3) and
makeHoldDelay (hold1, hold2, hold3). The crash fires when the
resizer replaces a cell on a freshly inserted buffer (or a hold
delay cell) whose pins have no graph vertex yet; it is an
engine bug, not a mock data gap, and no honest mock change
avoids it (equalizing buffer caps to dodge the replacement
would weaken the drive-strength model to force a pass, which
is forbidden). These keep their ported files but no CTest
entry and no .ok file until the engine fix lands (F2 scope):
setup2, hold1, hold2, hold3, slew1, slew2, slew3, slew4, slew5,
slew6, cap1, cap2, fanout3.

15 suite cases were never ported because they segfault in the
engine (F2's scope: segfaults plus the hold path and P-23
evaluation). Their scripts are committed in the parent dir;
they get mock ports and .ok files only after the engine
crashes are fixed: cap3, clk_nets1, fanout1, hold6, hold7,
hold8, hold9, slew8, slew9, wire1, wire2, wire4, wire5, wire6,
wire7.

Gcd stays pending: its routed DEF and SDC/lib inputs are
local-only ORFS outputs, not committed. A mock port needs a
synthetic mock placed/routed DEF, which is follow-up work, not
v1.

## Measured results (v1)

All 14 runnable cases pass twice with byte-identical verdict
lines (determinism checked by running each case twice and
diffing). Verdicts are pinned in repair_<case>.ok. Tech
scalars match the model exactly where reported (em1
1,000,000 ohm/m and 5.0e-08 F/m; em3 333,333.33 and 3.3e-08).

Action summary (from the .ok files):
- setup1: setup path acts, 5 buffers inserted, WNS -59.98 to
  -34.62 ns (repair incomplete: "Unable to repair all setup
  violations" still warns; the .ok pins mock behavior, not a
  full repair).
- tie1-7: tie path acts (tie_inserted 2 to 10, tie_removed 1
  to 2, tie_bad_nets 0 in every case).
- clk_inverters1: clock-inverter path acts (2 inserted, 1
  removed); WNS unchanged at -55.25 ns (the violation sits on
  a different path than the repaired inverters).
- wire3: negative case, fail-fast error names em99 as designed.
- No-action pins (run clean end to end, repair dispatch
  exercised, but no insertion; documented, not hidden):
  - setup3: setup violation present (WNS -0.53 ns), repair
    attempted ("Unable to repair all setup violations"), no
    insertion on the latch path.
  - hold4: no hold violation found, correct no-op.
  - hold5: 2 hold endpoints violate (hold -0.33 ns), repair
    attempted ("Unable to repair all hold violations"), no
    insertion. A diagnostic run with a synthetic DIEAREA added
    (area 114.56 um2, utilization 4.58 percent) still inserts
    nothing, so missing-DIEAREA area accounting is ruled out
    as the blocker; the violation is unrepairable by this
    engine on this shape.
  - fanout2: no timing paths (its SDC sets only max_fanout 10,
    no clocks) and its DEF carries a single 1-load net, so no
    violation exists and -design correctly does nothing.

Tuning applied in v1 (all recorded, none of it weakens a case):
- UNITS block form: the first tech LEF wrote `UNITS DATABASE
  MICRONS 2000 ;` on one line, which the LEF grammar rejects
  (UNITS is a block ending in END UNITS; the parser reported
  the error at the following LAYER token). Fixed in
  gen_mock.py; the tech LEF now uses the block form.
- Layers em4 and em6 through em10 added with synthetic values
  following the same width/R/C trend, because the setup2 and
  tie5 DEFs reference those layers in TRACKS statements and
  the DEF parser rejects undefined layers. The 13 previously
  passing verdicts are byte-identical after the addition.
- No mock value was changed to force a pass, and no real PDK
  value was copied anywhere.

Tuning rule (standing): mock values change only if a case is
vacuous for its intended path, and any change is recorded here
with the reason. A case is never weakened to force a pass, and
no real PDK value is ever copied to fix a vacuous case.
