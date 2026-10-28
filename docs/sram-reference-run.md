# SRAM-bearing reference run (unit F4)

Date: 2026-10-28
Status: unit F4 deliverable, fix track. A sky130 SRAM-bearing
block was hardened through the pinned ORFS v3.0 reference flow
and spot-checked. Measured verdicts: the flow completes and
produces a macro-bearing final layout; the detailed router
reports 0 DRC violations and static IR drop passes; KLayout
DRC on the merged GDS fails, with the count attributed below
(the macro as shipped carries nearly all of it standalone);
antenna fails marginally on the macro output nets; LVS is
NOT CLEAN under either of the two macro treatments run
(black box and full view), with different named residuals.
This closes the plan Section 14 macro-bearing item to the
extent stated in Section 6, no further.

## 1. Subject and setup

- Macro: `sky130_sram_1rw1r_44x64_8`, an OpenRAM SRAM from
  the pinned ORFS v3.0 tree (tag v3.0, commit 181e91337761,
  the Phase 0 pin of the unit 3.0 package). The pinned tree
  ships a dedicated platform, `platforms/sky130ram`, with
  four such macros carrying LEF, GDS, Liberty, Verilog and
  SPICE views; this is the smallest (64 words x 44 bits, one
  read-write port plus one read port, LEF SIZE 445.24 x
  237.415 um, CLASS BLOCK, 157 LEF pins of which vdd and gnd
  are USE POWER / GROUND with SHAPE ABUTMENT). Working-copy
  hashes match the pristine pinned tree byte for byte:
  GDS 9aa7b4a55bdcff1299149baf725edab0a86b55da34246d12ef50408aae1ead54,
  LEF 784dd7256e8ed67bff4266b223e7f9d364f31c8c9d84e29cd6418faf9ba87a4e,
  Liberty e92808d1dd8d3cc4d827e36e64fc07c7c080a93e7d62853c4f28e6a5ad3cd26f,
  Verilog 4e7a3700d9c9a46b4a3414b3a3d92dc5a7406e184bd0724b477e240dba308d91,
  SPICE a06ffb4a0af3d01191c84c8e77cd9a4dc1ae153af0edcfc77728d6bd2b1912c1,
  LVS SPICE b26e3dd6a979aa25e384313e9f2599c17cc82fef369252475da26c750a5af926.
  No macro was invented, generated or fetched from elsewhere.
- Block: `sram_ref`, a minimal wrapper written for this unit
  in the local ORFS working copy only (never committed to
  EOS): the macro plus a registered read-port address
  counter and output register, so every macro input is
  driven and both macro outputs are loaded by real logic.
  Die 560 x 400 um, core 15,15 to 545,385.
- Flow and tools, as pinned: ORFS v3.0 working copy
  (`~/workspace/orfs/flow`, Makefile identical to the
  pinned tree), OpenROAD at the ORFS pin (ead3236 build),
  Yosys 0.33, KLayout 0.30.12, Magic 8.3.685, Netgen
  1.5.324. PV binaries cannot run in GitHub CI; this is a
  local evidence run published as a docs PR, per the unit
  definition.
- Design configuration disclosures (all local to the
  working copy): ADDITIONAL_LIBS / LEFS / GDS point at the
  macro views above, the mechanism the pinned coyote_tc
  design uses for the same macros. Macro placement is
  manual (a MACRO_PLACEMENT file, u_ram at R0 40,60) because
  automatic macro placement found no partition solution
  (attempt 1 failed at PDN with PDN-0235, macro unplaced).
  The design PDN file is the platform sky130hd pdn.tcl
  plus two global connections mapping the macro's lowercase
  vdd / gnd pins onto VDD / VSS (the platform patterns only
  cover the uppercase and VPWR / VGND names) and one
  add_pdn_connect of met3 to met4 in the macro grid (the
  macro's power pins are a met3 mesh; the platform macro
  grid connects only met4 to met5, an omission the pinned
  coyote_tc design's own pdn.tcl also corrects). The CDL
  step was invoked with CDL_FILE on the make command line
  pointing at a local masters file (platform CDL plus the
  macro's shipped .lvs.sp pin header as an empty
  subcircuit), because the platform config.mk is included
  after the design config and overrides a design-level
  CDL_FILE, and write_cdl otherwise fails with ODB-0287
  (macro master not in the masters CDL files).

## 2. Judging rules (stated before the results they judge)

Written to the unit's evidence record before the flow run.
Flow PASS requires finish artifacts (final DEF, merged GDS,
final CDL) with the macro instance present, with the flow's
own reported metrics quoted. DRC PASS is 0 violations under
the platform KLayout deck on the merged GDS. Antenna PASS
is 0 violating nets and pins from OpenROAD check_antennas
on the final DEF. LVS is reported under two explicitly
distinct macro treatments (black box and full view, both
Magic extraction plus Netgen against the flow CDL plus the
platform CDL), and a verdict belongs to its treatment.
Timing is quoted, not judged: the 10 ns SDC period is a
wrapper convenience. One amendment was recorded in the
evidence before any LVS result existed: the black-box
treatment is applied to the extracted netlist rather than
to the GDS, because emptying the macro cell in a GDS copy
(with or without its labels) makes Magic drop the macro
instance from the extraction entirely; the amendment and
both failed GDS attempts are in the evidence record.

## 3. Flow results

The full default flow ran to its finish artifacts:
`6_final.def`, `6_final.v`, `6_final.spef`, `6_final.sdc`,
`6_final.cdl`, `6_final.gds` and `6_1_merged.gds`
(6,738,872 bytes; SHA-256: DEF
1f2b1a62f9110740ebeafc62d1783a6030067cc114893d883fc126909f2dd8cc,
merged GDS
2dfd2fec33f5e1ef63e256c108fc425ccc6762428eadc4bccac5e846007b993f,
CDL 1046e779011812e2b33318889942a888a9c23bc4a089652fd13e8588987cd249,
netlist 84c5705f573be62a5a0a67acba309b3152aa47a39b4522c6a54bb4b021fbfff0).
The macro instance `u_ram` is present and FIXED in the
final DEF (at 40,60, as placed) and instantiated in the
final netlist and CDL. As reported by the flow's finish
report: setup worst slack +1.31 ns at the 10 ns period,
TNS 0.00, min-delay paths all MET (smallest slack printed
in the min report section 0.53 ns), design area 113,085
um2 at 43 percent utilization, total power 4.61 mW.
Detailed router: 0 violations (DRT-0199). Static IR drop
in the finish report passes: worst drop 1.25 mV on VDD and
1.24 mV on VSS, all PDN stripes connected; the macro is
connected to the grid (the earlier PSM-0042 failure mode
is what the met3 to met4 macro-grid connection of
Section 1 fixes; 27 PDN-0110 via warnings remain at
individual met3/met4 crossing points).

One flow caveat, identical on the Phase 0 gcd run: the
last step of `6_report` aborts with
`invalid command name "gui::show"` in the headless pinned
OpenROAD build, after every metric above was reported.
The remaining finish targets (GDS merge, CDL) were run
explicitly and completed with exit 0. This is a property
of the pinned tool build, not of this design; gcd's
6_report log ends with the same error.

## 4. DRC

Method: KLayout 0.30.12, platform deck
`platforms/sky130hd/drc/sky130hd.lydrc` unmodified, on the
final merged GDS, the ORFS `drc` target invocation; the
same deck on the macro GDS alone as a baseline.

Result: FAIL as measured. The merged GDS report database
holds 110,292 violations in 14 categories. Attribution by
the cell each violation is reported in: 110,162 sit inside
the macro's own hierarchy (31 macro cell types, dominated
by the bitcell arrays; the largest categories are m1.4
with 79,814 and li.3 with 24,443), 130 are reported in the
top cell at coordinates inside the macro footprint
(sampled items sit on the macro pin field at its lower
edge; categories m1.1, m1.2, m1.7, m2.2, m3.2, m4.2), and 0
in any standard cell. The macro GDS alone, under the same
deck, reports 110,066 violations: the macro as shipped
carries essentially the whole count, because its bitcell
array is drawn at geometries the standard-cell deck
rejects (m1.4 and li.3 spacing classes). Integration adds
226 net (110,292 against 110,066, of which 130 are the
top-cell items above; the remainder is recounting of
macro-internal items in the merged hierarchy). No layout,
deck or macro geometry was edited to change these numbers.

## 5. Antenna

Method: OpenROAD (ORFS pin) check_antennas on
`6_final.def`, as in unit 3.1.

Result: FAIL as measured, marginally. 26 net violations
and 27 pin violations, every one a side-area ratio between
403.89 and 456.78 against the limit of 400. The violating
nets are the macro output nets (dout1_raw[*] into the
output register, the dout0 path, and three internal glue
nets); the violating pins are the glue register and buffer
inputs those nets drive. The flow's antenna repair did
insert diodes (63 diode_2 cells in the final netlist) but
did not clear these: the charge collects on the long
routes fanning out of the macro's output pins. For
contrast, the gcd layout of unit 3.1 was antenna-clean;
the difference is the macro fanout, and it is recorded
here rather than repaired in this unit, whose scope is
the reference run and its spot checks.

## 6. LVS

Method (both treatments): Magic 8.3.685 extraction of the
unmodified final merged GDS (sky130A technology of the
pinned open_pdks build, as in unit F3), ext2spice in LVS
mode, Netgen 1.5.324 with the unit F3 setup file carried
over unchanged, against the flow's `6_final.cdl` plus the
platform CDL. Format normalizations, extracted side only,
complete list: the unit F3 device converter restricted to
standard-cell subcircuits (406 device lines; the macro's
shipped SPICE uses the extraction's X form natively, so
macro subcircuits pass through untouched), and, for the
black-box treatment only, the macro restriction of
Section 2. Extracted netlist SHA-256
0c3a7bc4243832b7dc6834e85b1f30b9e76acb4ce09bde302a3a7d59271150b1.

### 6.1 Black-box treatment: NOT CLEAN

The macro is an empty subcircuit on both sides (reference:
the shipped .lvs.sp pin header as an empty subcircuit).
Under this treatment the macro class itself is consistent:
one instance on each side, identical 157-pin lists, and
the 314 disconnected-node notes are exactly the empty
cell's pins on the two sides. Every standard-cell class is
present with equal instance counts on both sides (21
classes). The top cell nevertheless mismatches: devices
505 against 507, nets 553 against 589, and Netgen reports
the top level failed pin matching (the extracted top
cell's pin order differs from the CDL's). The named
residuals are in the glue's power and substrate modeling,
not in the macro interface: the extracted side carries a
single VSUBS net fragment holding the standard cells'
VPWR, VGND, VNB and VPB terminals together (the substrate
phenomenon of unit F3's D1, here unresolved at the top
level), Netgen flattened the 2,085 extracted tapvpwrvgnd_1
instances as an unmatched subcell (their device fragments
account for the nfet 18 to 9 against 18 to 7 and pfet
18 to 9 against 18 to 7 fragments), and diode_2 parallel
merging differs (63 to 56 extracted against 63 to 62
reference).

### 6.2 Full-view treatment: NOT CLEAN

The macro's reference is its shipped .lvs.sp, full device
level. Netgen compared 79 cell classes; the standard-cell
classes are equivalent (clkbuf_1 and the rest, 107
equivalence lines across the report), and the macro's
hierarchy names largely correspond between the GDS and
the .lvs.sp (pdriver_6, dff_buf_array_0,
global_bitcell_array and others compared by name). The
verdict is NOT CLEAN at the macro and at the top: the
extracted macro holds 18,491 devices and 6,867 nets
against the reference's 30,016 devices and 6,698 nets,
and the top cell totals are 21,772 against 33,370 devices.
The first divergence is at the bitcell class itself,
`sky130_fd_bd_sram__openram_dp_cell`: Magic's extraction
merges the bitcell's parallel devices (special_nfet_latch
counted 12 to 10 after merging, nets 11 against 9) where
the .lvs.sp models the same geometry unmerged (10 to 8),
and the reference carries a special_pfet_latch pair in
the cell that the extraction does not produce as such.
This is a device-modeling divergence between the Magic
extraction of the OpenRAM bitcell and the macro's shipped
LVS netlist, measured at the cell level; it is not a
top-level connectivity error, and no netlist or model was
edited to close it.

## 7. What this closes, and what it does not

The Section 14 macro-bearing item asked for a sky130
SRAM-bearing block through the reference flow with PV
spot checks. That run now exists and is reported above:
the flow side is satisfied as measured (finish artifacts,
macro placed and connected, router DRC 0, timing and IR
drop as reported), and the PV spot checks were run and
are reported as measured, which is two FAILs (KLayout
DRC, antenna) and two NOT CLEAN LVS verdicts with named,
attributed residuals. The item is therefore closed as a
run and explicitly not closed as a clean-PV claim: the
shipped OpenRAM macro is not DRC-clean under the
platform's standard-cell deck, its fanout nets are
marginally antenna-violating in this wrapper, and neither
LVS treatment reaches equivalence, for the modeling
reasons named. Nothing here is a foundry signoff claim,
an SRAM functional verification, or a macro
characterization. Evidence (flow, DRC, antenna, extraction
and Netgen logs, the prestated judging rules and the
amendment) is in the unit's evidence record,
hidden_files/wake-f4-evidence/.
