# Final-layout PV on gcd (unit 3.1)

Date: 2026-10-26
Status: unit 3.1 deliverable. Measured verdicts on the Phase 0
gcd final layout: DRC clean, antenna clean, LVS NOT clean, with
every discrepancy named below. This document replaces Phase 0's
"LVS unknown" (item L3 of the Phase 0 exit report) with a
measured verdict. Nothing in the layout, the reference netlist
or the pinned collateral was edited to reach these results; the
only edited files are local reader-input copies, enumerated in
Section 3.2.

## 1. Subject and setup

- Layout: the Phase 0 gcd final merged layout from the pinned
  ORFS v3.0 reference flow, `6_1_merged.gds`, identical in
  content to `6_final.gds` (898,868 bytes each, SHA-256
  66e93941bcbac4815bbafad19e51f68bc849748df3014d4e293ae6acdb7525e5).
- Reference netlist: the flow's own `6_final.cdl` (SHA-256
  89fcfed0f7a3bd5c8187858fb6ab07173d61fb008978fb087efe8752ca6cdc6f)
  concatenated with the platform library CDL
  `sky130hd.cdl` (SHA-256
  ae02cdc40c81943bbc3f4a16a6760e01afd12764a94e7836b7e168edf40ae29c,
  matching the unit 3.0 package manifest), exactly as ORFS's
  own `lvs` target builds its reference.
- Tools, as pinned: KLayout 0.30.12 (EOS component pin) for DRC
  and extraction, with 0.28.16 cross-checked where noted;
  OpenROAD at the ORFS pin ead3236 for the antenna check;
  Netgen 1.5.324 for the LVS comparison. PV binaries cannot run
  in GitHub CI; this is a local evidence run published as a
  docs PR, per the unit definition.
- The collateral is the ORFS v3.0 platform tree identified by
  the unit 3.0 package (open_pdks revision abfc54bd87e0 per the
  Phase 0 licence audit). No other PDK build was substituted.

## 2. DRC

Method: KLayout 0.30.12, platform deck
`platforms/sky130hd/drc/sky130hd.lydrc` unmodified, on
`6_final.gds`, same invocation as ORFS's `drc` target.

Result: the report database holds 145 rule categories and 0
violations. This reproduces the Phase 0 PV run exactly (same
category count in its report database, 0 violations).
Coverage by rule family (categories per family): metal layers
m1 8, m2 8, m3 6, m4 6, m5 4; vias via 6, via1 2, via2 7,
via3 5, via4 4; li 6, ct 3, nsm 4, pad 1; every remaining
family is a single offgrid or angle category per layer (diff,
poly, nwell, dnwell, psdm, nsdm, tap, licon, npc, ncm, hvi,
hvntm, hvtp, hvtr, lvtn, rpm, mf, tunm, uhvi, vhvi, vpp, pwbm,
pwde, pwell_rs, areaid_re).

Position (plan Section 13): this is the open KLayout deck the
platform ships, run on the final merged geometry. It is not
router DRC (the detailed router reported 0 errors separately
in Phase 0) and it is not a foundry signoff deck; no fuller
open deck exists for this platform at the pin. The claim is
exactly: 0 violations in the 145 categories this deck checks.

## 3. LVS

### 3.1 The KLayout comparison path at the unmodified pin

Reproduced from Phase 0 and extended: KLayout's netlist reader
rejects the platform CDL before any comparison, under both
0.28.16 and 0.30.12. First failure: pin count mismatch at
`sky130_fd_sc_hd__macro_sparecell` instance XI1 (concat file
line 11782), the unit 3.0 finding F1. Reading past that line
surfaces the same defect on all 7 instance lines of
macro_sparecell (each presents its nets plus a "/" separator
token against the callee's pin count), and then a second
reader blocker: resistor lines with the literal value `short`
and no numeric value, which the reader refuses ("Can't find a
value for a R, C or L device"). Those lines exist in exactly
three library cells: `sky130_fd_sc_hd__conb_1` (2 lines),
`sky130_fd_sc_hd__probe_p_8` (1 line) and
`sky130_fd_sc_hd__probec_p_8` (3 lines).

### 3.2 Local reader-input deviations (complete list)

To let any comparison run at all, a local copy of the library
CDL was made readable with 13 changed lines, all inside four
cells that gcd never reaches: a transitive-closure check over
the library shows gcd's 69 instantiated cell types reach none
of macro_sparecell, conb_1, probe_p_8 or probec_p_8.

- Lines 11169 to 11175 (macro_sparecell XI1 to XI7): the " /"
  separator token before the subcircuit name removed, 7 lines.
- Lines 4945, 4946 (conb_1), 17412 (probe_p_8), 17444 to
  17446 (probec_p_8): the resistor value `short` replaced by
  `0.001`, 6 lines. These cells are never instantiated in the
  compared design, so the substituted values cannot affect the
  gcd verdict either way.

The copy's SHA-256 is
92534c51c63f25b1ec49bf438a2f086ed42b7f75f7685a19f0a94ad8a1cad572.
The pinned platform file on disk is untouched (its hash still
matches the unit 3.0 manifest). With this copy, KLayout's own
comparison (platform `sky130hd.lylvs` unmodified) runs to
completion and prints "Netlists don't match". This KLayout
build writes no LVS report database in batch mode (verified by
searching the filesystem after runs with two report paths), so
the KLayout path yields a verdict without per-circuit detail.
The detailed verdict below therefore comes from the unit's
other named path: KLayout extraction plus Netgen comparison,
run against the unmodified reference CDL of Section 1
(Netgen's reader accepts it as shipped, with 1 warning: a
placeholder created for a cell defined later in the file).

### 3.3 Extraction setup (local runset variants, complete list)

Extraction used the platform `sky130hd.lylvs` extraction rules
with three local variations, none of which changes geometry,
layers or device rules:

- The schematic load and the comparison were removed
  (extraction only; Netgen compares).
- Pin-label attachment: the shipped runset never connects the
  pin layers (x/16) or the pin texts to the metal nets, and
  the gcd IO labels sit on pin shapes (for example clk on
  70/16, anchor outside the 70/20 drawing polygon), so as
  shipped the extracted top circuit carries no signal pins.
  The variant adds connect statements joining each metal's pin
  layer to the metal and each text layer to its pin layer.
  With them, the extracted top circuit carries exactly the 71
  signal ports of the reference plus the substrate net VNB.
- Two modes were run: deep (hierarchical, per-cell circuits)
  and flat (single circuit). One further variant, removing the
  runset's `connect_global(SUB, "VNB")`, was tried and made
  the result worse (Section 3.5); it was not used.

Netgen setup was Netgen's own default (`permute default`,
`property default`): MOSFET source/drain permutation on,
device properties compared with default tolerances.

### 3.4 Results

Hierarchical comparison (deep extraction vs reference):
68 cell classes compared. Every stdcell class gcd uses is
topologically equivalent to its CDL definition except
`sky130_fd_sc_hd__a21oi_2` (Section 3.5). Transistor W and L
match everywhere they were compared: there are no W or L
property errors in the output. The property errors Netgen
lists are exclusively unmatched parasitic property names
(extracted AS, AD, PS, PD against CDL sa, sb, sd, area, perim,
topography, mult), which carry no connectivity or geometry
information. The schematic's 77 `tapvpwrvgnd_1` instances sit
in an empty subcircuit (the CDL models the tap cell with no
devices); Netgen flattened those instances during the
comparison, which is the correct treatment for a device-free
cell.

Flat comparison (flat extraction vs reference, Netgen
flattening the schematic): pfet count equal at 1467;
nfet 1474 against 1470 after parallel combining (1838 raw);
total devices 2941 against 2937; nets 1544 against 1541.
Top-level pin lists are equivalent (71 signal ports, plus the
extracted substrate net VNB, which the reference carries
inside its cells as the VSS net). All remaining differences
localize to Section 3.5.

### 3.5 The named discrepancies

D1. Substrate net model (extraction artifact, named, not a
layout error): the runset forces the substrate to a global
net named VNB (`connect_global(SUB, "VNB")`), so in the
extracted netlist every nfet bulk (1474 connections) sits on
VNB, while the VGND rail forms its own net (1023 nfet
source/drain connections). The reference ties each cell's VNB
pin to VSS, so bulk and rail are one net there. The wells do
merge with VPWR on the extracted side (the nwell is not
global-forced), which is why the pfet side matches exactly
and the nfet side carries the split. Removing the
connect_global statement does not fix it: the substrate then
fragments into per-region nets (1550 nets against 1541), a
worse mismatch. The two nets are at the same potential in the
layout by construction (tap cells tie substrate to VGND);
this is a modeling limit of the shipped extraction rules, and
it is reported as such, not corrected by editing either side.

D2. `sky130_fd_sc_hd__a21oi_2` (a real extraction-level
difference, cause not fully resolved): in hierarchical
extraction the cell has 8 devices and 11 nets against the
reference's 6 devices (after combining) and 10 nets: inside
the extracted cell the VGND net appears as two nets (the pin
net and a fragment net carrying 3 nfet source/drain
connections), so two nfet parallel pairs cannot combine (5
nfets extracted against 3 combined in the reference). In flat
extraction the fragment joins the top-level rail, and what
remains is exactly 4 nfets (2 in each of the 2 instances of
this cell in gcd) that do not parallel-combine against their
reference partners. No other cell class shows this behavior.
Whether the intra-cell VGND split is a second modeling
artifact of the same family as D1 or a genuine property of
this cell's GDS (its VGND contact geometry is joined only
through neighbouring cells' rails) is not decided by this
run's evidence; it is recorded as the one open discrepancy.
Its blast radius is bounded: 4 transistors in 2 instances of
one cell type, with every net they touch identified in the
comparison output.

### 3.6 LVS verdict

LVS does not pass clean on gcd at this pin. The comparison is
complete and the topology matches everywhere except D1 (a
named extraction-model artifact) and D2 (one cell type, 4
transistors, cause unresolved between modeling artifact and
cell geometry). This replaces Phase 0's LVS UNKNOWN with a
measured NOT CLEAN verdict and named causes. A clean pass
would need either an extraction setup whose substrate model
matches the CDL's (the open_pdks Magic route is the known
candidate; Magic is not among the pinned tools on this
machine) or a resolution of D2 at the cell level. Neither is
attempted silently here; both are candidates for later units
if the tier claim in unit 3.2's exit evidence needs them.

## 4. Antenna

Method: OpenROAD at the ORFS pin (ead3236), `check_antennas`
on the final database `6_final.odb`, antenna ratios from the
platform tech LEF (ANTENNAMODEL OXIDE1, cumulative diff area
ratios as piecewise-linear rules per layer) and gate areas
from the cell LEF; the package's antenna diode cell is
`sky130_fd_sc_hd__diode_2`. This is a final-geometry check,
not the global-route-stage check the flow already ran.

Result: 0 net violations, 0 pin violations. No antenna diodes
were required and none are present in the design netlist.

## 5. Summary and tier position

| Check | Verdict | Evidence |
|---|---|---|
| DRC (KLayout deck, 145 categories) | PASS, 0 violations | Section 2 |
| Antenna (final geometry) | PASS, 0 net and 0 pin violations | Section 4 |
| LVS (KLayout extraction + Netgen) | NOT CLEAN | Sections 3.4 to 3.6 |

Unit 3.1's deliverable is the measured verdict set above, and
it is met on those terms. The correlated-tier claim for the
package remains gated on unit 3.2's extraction correlation as
well; nothing in this document claims that tier. Counts are
unchanged by this docs-only unit: 218 GoogleTest cases, 24
ctest suites, checker pytest 11.
