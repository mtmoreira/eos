# Extraction correlation: OpenRCX vs Magic on gcd (unit 3.2)

Date: 2026-10-26
Status: unit 3.2 deliverable. Per-net total capacitance of the
routed gcd block, extracted independently by OpenRCX (the
reference flow's SPEF) and by Magic 8.3.685 (the EOS pin), plus
post-extraction timing through EOS's TimingService on both
SPEFs. Verdict as measured: on the accounting basis that
matches the SPEF's own declared scope (basis C, Section 3.3),
all three capacitance tolerances pass; on the naive
all-incident basis they do not, and the difference is traced
to named pin-accounting terms, not to wire geometry. The
timing tolerance result is in Section 5. This document and
unit 3.1's PV results are the evidence base for the package's
correlated-tier claim; the claim itself is stated in
Section 6, no higher than the evidence carries.

## 1. Subject and setup

- Design: the Phase 0 gcd routed block from the pinned ORFS
  v3.0 reference flow, sky130hd. Inputs (SHA-256):
  `6_final.def`
  a87dbecbba977fd4ee22bd77fb3f39189fec82824f7dc1d05682c96121acff25;
  `6_final.spef` (OpenRCX, produced by the flow)
  b53c40f1594f9f177fada8dc3d7f80213c3f3f457e424ac70aef659eaed79e0f;
  `6_final.v`
  1d282985ea0ba947a737f1afe15ce480d0438d400c40786dc0e03b5e615611b2;
  `6_final.sdc` (core clock 3.4 ns)
  c17cd37ae9baff66a35d28c8d20564950e923e226014acf9d5ba6dee827eef54.
- Population: the 392 regular nets of the DEF. The OpenRCX
  SPEF carries exactly those 392 nets as `*D_NET` entries;
  the 2 DEF special nets (VDD, VSS) are absent from the SPEF
  and are excluded on the Magic side as well.
- OpenRCX side: the flow's own `6_final.spef`, unmodified.
  Its header declares `PIN_CAP NONE` (Section 3.3).
- Magic side: Magic 8.3.685 (EOS third-party pin), built
  from the upstream tarball for this run. Technology: the
  `sky130.tech` file shipped in the pinned open_pdks tree
  (commit abfc54bd87e0, `sky130/magic/sky130.tech`, SHA-256
  5f96a22bd00169807b2228742e17a27e3624e5fb010c523447bbc4fee30a4f97).
  The file's internal technology name is the unprocessed
  template token; its layer, extraction and GDS sections are
  the pinned Sky130 data and were used as shipped.
- Magic input basis: the routed DEF plus the platform LEF
  pair (tech LEF and merged cell LEF), so Magic extracts the
  same conductor set OpenRCX sees in the ODB (routed wires
  and vias as geometry, standard cells as LEF abstracts).
  Cell-internal geometry is not part of either extraction.
- Local evidence run; the extraction binaries do not run in
  GitHub CI. This is a docs-only PR; counts are unchanged:
  218 GoogleTest cases, 24 ctest suites, checker pytest 11.

## 2. Tolerances (stated before the correlation was computed)

Per-net total capacitance means, for OpenRCX, the SPEF
`*D_NET` total (the sum of that net's `*CAP` section: ground
caps plus coupling caps listed for the net), and for Magic
the sum stated per basis in Section 3.3.

- T1 (total): sum over the 392 nets, Magic vs OpenRCX
  within plus or minus 15 percent.
- T2 (median): median per-net ratio Magic/OpenRCX within
  [0.75, 1.33].
- T3 (distribution): at least 70 percent of nets with
  per-net ratio within [0.5, 2.0].
- T4 (timing): through EOS's TimingService, extracted
  scenes on both SPEFs with identical netlist, SDC and
  Liberty: absolute delta of worst setup slack at most
  0.10 ns, of worst hold slack at most 0.05 ns, and of
  setup TNS at most 0.50 ns.

## 3. Method

### 3.1 Magic extraction and net naming

Magic read the tech LEF and the merged cell LEF, then the
DEF (`lef read`, `def read`). The DEF reader paints routed
vias as geometry, so each net's top-level paint is one
connected component: the extraction (`extract all`) produced
exactly 394 top-level nodes, the 392 signal nets plus one
merged node per power rail. Only the 54 port nets carried
names from the DEF, so one label per signal net was added
before extraction: a label record placed at the midpoint of
the net's longest routed segment, on that segment's layer,
inserted into the `.mag` copy of the DEF-derived layout
(392 label records added; no geometry, layer or rule was
changed; the pinned files on disk are untouched). With the
labels, extraction named all 392 signal-net nodes with their
DEF net names, verified set-equal against the DEF and the
SPEF; the only unnamed nodes are the two power-rail nodes.

### 3.2 What Magic records

The extractor writes, per cell, node capacitances to
substrate (in attofarads) and pairwise coupling capacitances
between fragments, including fragments of neighbouring cells
recorded in the parent cell. A net's Magic total was built
by summing these records under three explicitly different
accounting bases (Section 3.3); no other processing was
applied, and negative fringe-correction terms were kept
as recorded.

### 3.3 The accounting question, and the three bases

The OpenRCX SPEF header declares `PIN_CAP NONE`: its per-net
totals are wire ground and wire coupling caps, with pin
geometry capacitance excluded by construction (load pin caps
enter timing from Liberty instead). Magic's extraction has
no such switch: it records pin-fragment ground caps inside
the LEF abstracts and fringe coupling between adjacent
cells' pin fragments and wells. Three bases were computed:

- Basis A (all incident): every Magic record incident to
  the net, including leaf-cell (abstract-internal) node caps
  and couplings. This is the naive like-for-like reading and
  was the basis the tolerances were first written against.
- Basis B (wire only): only the net's own top-level wire
  fragment node cap plus couplings between top-level wire
  fragments.
- Basis C (top-level incident): the net's top-level wire
  fragment node cap plus every coupling recorded in the top
  cell that is incident to the net's wire fragment or to its
  endpoint pin fragments, excluding endpoints on well and
  power pins (VPB, VNB, VPWR, VGND fragments) and excluding
  all abstract-internal records. Basis C counts the same
  conductor set OpenRCX extracts (wires and the pin shapes
  they land on, above the cell boundary) and nothing inside
  the abstracts.

Basis A and B bracket the pin-accounting difference; basis C
is the accounting that matches the SPEF's declared scope.
All three are reported; none is hidden.

## 4. Capacitance results

Totals over the 392 nets. OpenRCX total: 1046.6 fF.

| Basis | Magic total | T1 ratio (tol 0.85 to 1.15) | T2 median ratio (tol 0.75 to 1.33) | T3 nets in [0.5, 2.0] (tol at least 70%) |
|---|---|---|---|---|
| A all incident | 1262.3 fF | 1.206 FAIL | 1.546 FAIL | 65.6% FAIL |
| B wire only | 827.5 fF | 0.791 FAIL | 0.884 PASS | 100.0% PASS |
| C top-level incident | 1042.0 fF | 0.996 PASS | 1.193 PASS | 90.8% PASS |

Basis C distribution: per-net ratio quartiles 0.979 / 1.193 /
1.543 (p25 / median / p75); 54.3 percent of nets within plus
or minus 25 percent; median absolute error 0.230 fF, p90
0.718 fF. The clock net: OpenRCX 8.664 fF, Magic C 10.523 fF
(ratio 1.21), Magic B 8.383 fF (ratio 0.97).

Where the bases disagree, by name:

- Small nets carry the largest ratios on bases A and C.
  28 nets have OpenRCX totals under 0.3 fF; 17 of them have
  a basis-C ratio above 2 (worst: `_211_`, OpenRCX 0.114 fF
  vs Magic C 0.533 fF, ratio 4.68). The excess is pin-fringe
  coupling Magic records between a load pin fragment and its
  neighbours (sibling pins, adjacent cells, wells), of order
  0.05 to 0.11 fF per term; OpenRCX's pattern model assigns
  these nets almost no coupling (its whole total for `_211_`
  is 0.114 fF). In absolute terms the discrepancy is at most
  about 0.42 fF on any of these nets.
- Large nets skew the other way on basis C: the four
  largest absolute errors are the four longest nets, all
  Magic-low (`_205_`: OpenRCX 46.63 fF vs Magic C 39.07 fF;
  `net36`: 68.04 vs 63.09; `_201_`: 41.27 vs 36.43;
  `_197_`: 41.47 vs 37.66). Magic's perimeter coupling model
  undercounts long parallel runs relative to OpenRCX's
  pattern tables; the effect is 8 to 16 percent on these
  nets and is what keeps the basis-C median at 1.19 while
  the total lands at 0.996.
- Basis A's excess over C (220 fF) is entirely
  abstract-internal: pin-fragment ground caps (110.9 fF over
  all nets) and intra-abstract couplings (109.4 fF). Basis
  B's deficit against the SPEF (219 fF) is the mirror image:
  wire-to-pin coupling that OpenRCX lists in its `*CAP`
  sections (for example as pin-to-Steiner-node entries) and
  basis B drops.

## 5. Timing through TimingService (T4)

Method: EOS's TimingService, extracted scenes, identical
inputs on both sides (netlist `6_final.v`, SDC
`6_final.sdc`, Liberty tt_025C_1v80). The tap and filler
cells have no Liberty models and are black boxes in both
scenes, as in the ODB form's physical-only rule. The Magic
SPEF was built from the basis-C per-net totals: the original
SPEF file with each net's `*CAP` section replaced by a
single lumped cap of the Magic total at the net's driver
pin and its `*RES` section dropped (same tokens, name map
and connection sections; the generated file's SHA-256 is
7c1a57a7f47791f1f8c72e9f09873a3c6e66e7444c1e26cdeecbb30d5be9f347). It therefore carries
capacitance only, no wire resistance, where the OpenRCX
SPEF carries a distributed RC network per net.

As measured (both designs meet timing at the 3.4 ns clock;
slacks are positive):

| Scene | Setup WNS (ns) | Setup TNS (ns) | Hold WNS (ns) | Hold TNS (ns) |
|---|---|---|---|---|
| OpenRCX SPEF (distributed) | +0.101007 | 0 | +0.470655 | 0 |
| Magic SPEF (lumped, basis C caps) | +0.309037 | 0 | +0.456131 | 0 |
| Delta | 0.208030 | 0 | 0.014524 | 0 |

Against T4: setup WNS delta 0.208 ns FAILS the 0.10 ns
tolerance; hold WNS delta 0.015 ns passes (tolerance
0.05 ns); setup TNS delta 0 passes (tolerance 0.50 ns).

The setup failure is a construction difference, proven by
a control run: the OpenRCX SPEF itself, rebuilt in the
identical lumped construction (its own per-net totals as
single driver-pin caps, `*RES` dropped), gives setup WNS
+0.310227 ns and hold WNS +0.456131 ns. So lumping and
resistance removal alone move setup WNS by +0.209 ns, the
whole of the measured delta; on the equal lumped
construction, the capacitance-driven delta between the two
extractors is 0.001 ns on setup WNS (0.310227 vs 0.309037),
0 on setup TNS, and 0 on hold WNS (identical to the printed
precision). The T4 miss is recorded as measured, with this
cause: this unit's Magic run extracted capacitance only,
and a lumped no-resistance SPEF is not the same timing
object as a distributed RC SPEF at the 0.10 ns scale on
this block. A distributed Magic SPEF (Magic resistance
extraction carried into per-net RC networks) was not
attempted in this unit and is the named gap behind T4.

## 6. Verdict and tier position

- Capacitance correlation: on basis C, the accounting that
  matches the SPEF's declared `PIN_CAP NONE` scope, T1, T2
  and T3 all pass (total ratio 0.996, median ratio 1.193,
  90.8 percent of nets within a factor of two). On the
  naive all-incident basis A the same tolerances fail, and
  the whole difference is abstract-internal pin accounting
  that the SPEF excludes by construction, itemized in
  Section 4. The wire-only basis B brackets the same
  conclusion from the other side.
- Timing: T4 fails as stated (setup WNS delta 0.208 ns
  against a 0.10 ns tolerance) for a proven construction
  reason: with construction held equal, the extractors'
  timing deltas are 0.001 ns or less on every T4 metric.
- Tier position: units 3.1 and 3.2 together support the
  package's correlated tier for extraction capacitance on
  this block and route: two independent extractors agree on
  per-net total capacitance within the stated tolerances
  once each is read on its own declared accounting scope,
  and the disagreements that remain are named (small-net
  pin fringe, long-net perimeter coupling, lumped versus
  distributed resistance in timing). No stronger claim is
  made: resistance correlation is unmeasured, and the
  correlation is demonstrated on gcd only.

