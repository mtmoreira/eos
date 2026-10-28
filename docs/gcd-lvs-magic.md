# LVS on gcd by the Magic extraction path (unit F3)

Date: 2026-10-27
Status: unit F3 deliverable. Measured LVS verdict on the
Phase 0 gcd final layout by the extraction path unit 3.1
named but did not attempt: Magic extraction plus Netgen
comparison against the unmodified reference CDL. Verdict
as measured: D1 (the substrate net split) is closed under
this extraction model; D2 (the a21oi_2 discrepancy) is
resolved at the cell level as a modeling artifact of the
KLayout extraction path (Section 5); and one successor
residual of the same cell, with its cause proven, still
keeps the Netgen verdict at NOT CLEAN under the pinned
setup (Sections 5 and 6). Unit 3.1's NOT CLEAN verdict on
the KLayout extraction path stands in its own document
(docs/gcd-pv-results.md); this document's verdict belongs
to the Magic extraction path only and does not rewrite
3.1. Nothing in the layout, the reference netlist or the
pinned collateral was edited; every local deviation is
listed in Section 3.2.

## 1. Subject and setup

- Layout: the Phase 0 gcd final merged layout from the
  pinned ORFS v3.0 reference flow, `6_final.gds`,
  identical in content to `6_1_merged.gds` (898,868 bytes
  each, SHA-256
  66e93941bcbac4815bbafad19e51f68bc849748df3014d4e293ae6acdb7525e5),
  re-verified by hash in this unit.
- Reference netlist: the flow's own `6_final.cdl`
  (SHA-256
  89fcfed0f7a3bd5c8187858fb6ab07173d61fb008978fb087efe8752ca6cdc6f)
  concatenated with the platform library CDL
  `sky130hd.cdl` (SHA-256
  ae02cdc40c81943bbc3f4a16a6760e01afd12764a94e7836b7e168edf40ae29c),
  exactly as in unit 3.1 Section 1. The concatenation
  built for this unit is byte-identical to the flow's own
  `6_final_concat.cdl` (SHA-256
  0f0fe44eaaaa2df5316c075861f99b1a3f918ca6ffbf9802fe38cb8e2312b275
  for both). Netgen accepts the unmodified reference as
  shipped, as it did in unit 3.1.
- Tools: Magic 8.3.685 (the EOS pin) and Netgen 1.5.324
  (the unit 3.1 pin). PV binaries cannot run in GitHub
  CI; this is a local evidence run published as a docs
  PR, per the unit definition.
- Magic technology: the open_pdks sky130A technology of
  the ciel build on this machine (open_pdks revision
  8afc8346a57fe1ab7934ba5a6056ea8b43078e71), used through
  its own `sky130A.magicrc`; the technology file's
  SHA-256 is
  d9ef56e3ab01b8fb605edf964fcfa01a0bf14ca8503f6ccecdecfd71f0907519.
  This is the processed form of the same open_pdks data
  whose unprocessed template form unit 3.2 pinned
  (`sky130.tech`, SHA-256
  5f96a22bd00169807b2228742e17a27e3624e5fb010c523447bbc4fee30a4f97);
  the LVS flow uses the processed sky130A form, and this
  section records which file produced the numbers.
- Netgen setup: the same build's `sky130A_setup.tcl`
  (SHA-256
  7a33f3f54fab57a9d9b0887ac7b7b64ce6c454110e8b7f01f3cfb90a1282dc94,
  copied byte-identically into the unit's evidence
  record), plus the local addendum of Section 3.2.

## 2. Judging rules (stated before the results they judge)

Written to the unit's evidence record before any F3
extraction or comparison was run. The LVS verdict is
Netgen's own equivalence verdict on the comparison, plus
any named residuals; no numeric tolerance is invented
for the verdict itself. Property handling is the pinned
sky130A setup's own: MOSFET source and drain permutation
on, parallel combining on with W summed and L critical,
W and L tolerance 1 percent, parasitic property names
deleted on the transistor classes. D1 is reported closed
if the Magic extraction carries no substrate split (the
nfet containing classes equivalent without a substrate
net mismatch, and no residual attributable to a bulk and
rail split). D2 is decided from the standalone cell
extraction: a single VGND net carrying all of the cell's
VGND terminals decides it a modeling artifact of the
KLayout path; a reproduced VGND split, with a fragment
carrying nfet terminals and no in-cell conductor joining
it to the VGND pin shape, would decide it a genuine
cell/layout property.

## 3. Method

### 3.1 Extraction and comparison

Magic, batch mode with the NULL graphics device and the
sky130A magicrc: `gds read` of `6_final.gds`, load of the
top cell `gcd`, `extract all`, then `ext2spice` in LVS
mode. The extracted netlist (`gcd.spice`, SHA-256
83f2fb025a21c50fdc7eb6b19386a820c5743563cdfacd9797be29d0ae6a3137)
carries one subcircuit per GDS cell: 68 standard cell
classes plus the gcd top cell. Netgen then compared the
extracted netlist (circuit 1) against the reference of
Section 1 (circuit 2), top cell `gcd`, with the setup of
Section 1. The same flow, applied to the standalone
`sky130_fd_sc_hd__a21oi_2` cell from the platform library
GDS, produced the cell-level evidence of Section 5.

### 3.2 Local deviations (complete list)

None of these changes geometry, connectivity, the layout
or the reference netlist.

- Loader shim for Magic: this machine has no
  `libGLU.so.1` (the package is not installed and its
  package lists are unavailable), and Magic's Tcl module
  links eight GLU tessellation symbols. A local stub
  library providing those eight symbols was placed on
  the loader path for the Magic runs only. Batch
  extraction runs on the NULL graphics device, where
  tessellation (a display path) is never called; the
  shim satisfies the loader and takes no part in
  extraction.
- Reference filename: Netgen's reader selects its input
  format by filename extension and rejects `.cdl`, so
  the reference was passed under a `.spice` filename.
  The file is byte-identical to the concatenation of
  Section 1 (same SHA-256); no line was changed.
- Extracted netlist format normalization: Magic's
  `ext2spice` writes primitive MOSFETs as subcircuit
  instances (X lines) of its msubcircuit device classes
  (`sky130_fd_pr__nfet_01v8`, `sky130_fd_pr__pfet_01v8_hvt`,
  `sky130_fd_pr__special_nfet_01v8`). Netgen cannot match
  a subcircuit instance element against the reference
  CDL's M device elements (a first run in that form
  failed with placeholder pin mismatches on effectively
  every class; its output is kept in the evidence
  record). Each such X line was therefore rewritten as
  the equivalent M line with the CDL's own model name
  for the same device (`nfet_01v8`, `pfet_01v8_hvt`;
  the CDL folds the special nfet geometry, found only
  in the dfxtp latch transistors, into `nfet_01v8`).
  Terminal order (drain, gate, source, bulk), nets and
  properties were carried over unchanged; 976 device
  lines were converted and no other line was touched.
  The normalized netlist's SHA-256 is
  31bb7ced7aa6586b860582504362b58f5f9dd5d2b411c0ca6116003635198751.
- Netgen setup addendum: the sky130A setup applies its
  transistor handling to the long `sky130_fd_pr__` class
  names, which neither circuit carries after the
  normalization above. The addendum mirrors that exact
  handling (source and drain permutation, parallel
  combining with W summed and L critical, 1 percent W
  and L tolerance, the same parasitic property names
  deleted) onto the short names `nfet_01v8` and
  `pfet_01v8_hvt` in both circuits. It changes no
  netlist on either side.
- Two diagnostic runs, neither used for the verdict: a
  cell-level run with series combining additionally
  enabled on the transistor classes (it changed nothing;
  the counts of Section 5 were identical), and the
  midpoint identification run of Section 5.3.

## 4. Results

Netgen compared 68 standard cell classes and the gcd top
level. 67 of the 68 classes are topologically equivalent
to their CDL definitions, with cell pin lists equivalent
in every class. The single exception is
`sky130_fd_sc_hd__a21oi_2` (Section 5). There are no W or
L property errors anywhere in the output, and no property
errors of any kind after the setup's stated deletions.

Top level: the pin lists are equivalent (54 signal ports
on both sides, identical as sets). With the one
non-matching class flattened into the top level by
Netgen, the top level counts are 370 devices against 366
and 400 nets against 398; the whole difference is the
two instances of `a21oi_2` (per instance, 2 devices and
1 net, Section 5). Expanded to transistor fingers (each
CDL device counted with its multiplicity m), the flat
totals agree exactly: 3,676 fingers extracted against
3,676 in the reference. For contrast, unit 3.1's KLayout
path measured 2,941 extracted devices against 2,937 on
the same reference, with the difference split between
D1's substrate model and D2.

## 5. D2 resolved at the cell level, and the successor residual

### 5.1 What the standalone cell extraction shows

Magic extraction of `sky130_fd_sc_hd__a21oi_2` alone,
from the platform library GDS (no neighbours, no top
level metal), gives 12 transistor fingers (6 nfet, 6
pfet) on 10 nodes, with the same 8 pins as the CDL
definition. The CDL defines 6 devices, each with m=2,
on 10 nets; the finger counts and the net count agree
once multiplicity is expanded. The subcircuit Magic
extracts for this cell inside the full gcd layout is
line-identical to the standalone extraction.

On the question unit 3.1 left open: VGND in the
extracted cell is a single net. Its node record in the
extraction is one metal1 node spanning the cell, and it
carries all four nfet source and drain terminals that
the CDL assigns to VGND (the two VGND terminals of the
A1/A2 series pair's lower device and of the B1 device,
each with multiplicity 2). No fragment net exists. The
join is made by the cell's own metal1 and local
interconnect geometry inside the cell boundary; no
abutment to neighbouring cells and no top level rail is
needed to complete it.

### 5.2 Decision on D2

D2 as recorded in unit 3.1 (the intra-cell VGND split:
the pin net and a fragment net carrying 3 nfet source
and drain connections, so the parallel pairs could not
combine) does not exist in this cell's geometry under
the Magic extraction model. By the judging rule of
Section 2, D2 is decided a modeling artifact of the
KLayout extraction path, of the same family as D1: the
KLayout runset's VGND contact handling split a net that
the layout holds together inside the cell. The blast
radius unit 3.1 recorded for it (4 transistors in 2
instances) is the same cell population as the successor
residual below, but the split net it named (VGND) is not
split in the layout.

### 5.3 The successor residual: the series midpoint

The a21oi_2 class still does not match under the pinned
setup, for a different and now proven reason. After
parallel combining, the class counts are 8 devices and
11 nets extracted against 6 devices and 10 nets in the
reference. The pfet side combines completely (6 fingers
to 3 devices): its series midpoint is one shared
diffusion node, as in the CDL. On the nfet side, the
A1/A2 series pair is drawn as two parallel strips, and
each strip's midpoint diffusion is a separate region
(extraction nodes at two distinct positions on the
cell's diffusion row), so extraction names two midpoint
nets where the CDL's m=2 notation folds both fingers
onto its single midpoint node `sndA1`. Netgen's
parallel combining cannot merge across the two distinct
midpoints, and its symmetry resolution then mismatches
(the output pairs one midpoint net with VGND).

That this representation difference is the whole of the
residual is proven by a controlled diagnostic: in a
diagnostic copy of the extracted cell netlist, the two
midpoint nets were identified (two device terminal
lines changed, nothing else). Netgen then reports the
cell as matching uniquely, 6 devices and 10 nets on both
sides, with all 6 parallel merges taken. Each strip is
by itself a complete A1/A2 series path between Y and
VGND, which is exactly what the CDL's m=2 series pair
asserts; the difference is between layout geometry (two
unconnected midpoint diffusions) and CDL multiplicity
notation (one folded node), not a connectivity error in
either. At the top level this residual is the whole of
the remaining mismatch: 4 transistors (the series
fingers, 2 per instance) in the 2 instances of this
cell in gcd, and 2 nets.

## 6. D1, and the LVS verdict for this path

D1 is closed under this extraction model. Magic does not
force the substrate to a separate global net: in the
extracted top level, the substrate node and the VGND
rail are one net (Netgen's output shows the single
extracted net carrying both the VNB pin and the VGND pin
of every instantiated cell class, fanouts equal per
class), and the nwell node and the VPWR rail are one net
likewise, matching the reference, whose top level ties
VNB to VSS and VPB to VDD. Every nfet containing class
except the Section 5.3 residual is equivalent with its
bulk connections on that shared net; there is no
substrate attributable mismatch anywhere in the
comparison. This removes the discrepancy that accounted
for the bulk of unit 3.1's flat mismatch (there, 1,474
nfet bulk connections sat on a VNB net separate from the
VGND rail's 1,023 source and drain connections).

LVS verdict for the Magic extraction path, as measured:
NOT CLEAN, with one named residual. 67 of 68 standard
cell classes are equivalent, the top level pin lists are
equivalent, the flat transistor finger totals agree
exactly (3,676 against 3,676), and there are no property
errors. The single residual is the a21oi_2 series
midpoint representation of Section 5.3 (4 transistors in
2 instances, cause proven by controlled diagnostic). A
fully clean verdict on this path would need a comparison
setup that folds multiplicity-2 series fingers onto one
midpoint node; the pinned sky130A setup does not do
this, and this unit does not invent one. P3-1 is
answered to this extent: the clean-LVS path named by
unit 3.1 has been run, D1 closes on it, and D2 is
resolved as a KLayout path modeling artifact with a
smaller, proven successor residual remaining.

## 7. Summary

| Item | Unit 3.1 (KLayout path) | Unit F3 (Magic path) |
|---|---|---|
| Cell classes equivalent | 67 of 68 (a21oi_2 excepted) | 67 of 68 (a21oi_2 excepted) |
| Flat devices (extracted vs reference) | 2,941 vs 2,937 | 3,676 vs 3,676 fingers |
| D1 substrate split | Present (extraction artifact) | Closed (no split) |
| D2 | VGND split in a21oi_2, cause unresolved | VGND split absent; decided KLayout modeling artifact |
| Remaining residual | D1 and D2 | a21oi_2 series midpoint representation, cause proven |
| Verdict | NOT CLEAN | NOT CLEAN, one named residual |

Counts are unchanged by this docs-only unit: 218
GoogleTest cases, 24 ctest suites, checker pytest 11.
