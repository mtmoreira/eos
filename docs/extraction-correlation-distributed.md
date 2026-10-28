# Extraction correlation addendum: distributed Magic SPEF, resistance, and T4 re-judged (unit F2)

Date: 2026-10-27
Status: unit F2 deliverable, an addendum to
docs/extraction-correlation.md (unit 3.2). Unit 3.2's
verdicts stand in that document: capacitance correlation
passed on basis C, and timing tolerance T4 failed as
measured on a lumped, resistance-free Magic SPEF, with the
cause proven to be the SPEF construction and a distributed
Magic SPEF named as the gap. This unit builds that
distributed SPEF, re-judges T4 on it, measures resistance
correlation for the first time, and re-checks the basis-C
capacitance correlation on the distributed extraction.
Verdict as measured: T4 passes on all three metrics,
resistance correlation passes all three of its tolerances,
and the capacitance re-check reproduces unit 3.2's basis-C
result exactly. P3-2 is closed to the extent stated in
Section 6.

## 1. Subject and setup

- Design: the same routed gcd block as unit 3.2, inputs
  hash-identical to that unit: `6_final.def`
  a87dbecbba977fd4ee22bd77fb3f39189fec82824f7dc1d05682c96121acff25;
  `6_final.spef` (OpenRCX, unmodified)
  b53c40f1594f9f177fada8dc3d7f80213c3f3f457e424ac70aef659eaed79e0f;
  `6_final.v`
  1d282985ea0ba947a737f1afe15ce480d0438d400c40786dc0e03b5e615611b2;
  `6_final.sdc`
  c17cd37ae9baff66a35d28c8d20564950e923e226014acf9d5ba6dee827eef54.
- Population: the 392 regular nets of the DEF, as in unit
  3.2; the 2 PG special nets are excluded on both sides.
- Magic: 8.3.685 (the EOS pin), the pinned open_pdks
  `sky130.tech` as in unit 3.2.
- The distributed Magic SPEF built by this unit has
  SHA-256
  c3b569df208c67c99ea0dc23e0b5b7d5420aad21dd94ce350ad6ac1469540ac5.
- Local evidence run; the extraction binaries do not run
  in GitHub CI. This is a docs-only PR; counts are
  unchanged: 218 GoogleTest cases, 24 ctest suites,
  checker pytest 11.

## 2. Tolerances (stated before any F2 numbers were computed)

Written to the unit's evidence record before the
distributed extraction, the resistance correlation, or any
timing run of this unit was performed. Capacitance
tolerances are unit 3.2's T1 to T3, unchanged. Resistance
tolerances are new in this unit. Timing is unit 3.2's T4,
unchanged.

- T1 (capacitance total): sum over the 392 nets, Magic vs
  OpenRCX within plus or minus 15 percent.
- T2 (capacitance median): median per-net ratio within
  [0.75, 1.33].
- T3 (capacitance distribution): at least 70 percent of
  nets with per-net ratio within [0.5, 2.0].
- Resistance accounting, stated with the tolerances:
  per-net total resistance is the sum of all resistor
  segment values in that net's distributed RC network, the
  identical accounting on both sides (OpenRCX: sum of the
  `*RES` entries of the net; Magic: sum of the resistor
  elements of the network built in Section 3). This is a
  network-sum statistic, stated as such; it is not an
  effective driver-to-load resistance. Nets where either
  side's total is zero have no defined ratio; they are
  counted as a class, included in R1, excluded from R2
  and R3. As measured, no net is in that class (both sides
  are nonzero on all 392 nets).
- R1 (resistance total): sum over the 392 nets within
  plus or minus 30 percent (ratio in [0.70, 1.30]).
- R2 (resistance median): median per-net ratio within
  [0.50, 2.00].
- R3 (resistance distribution): at least 70 percent of
  ratio-defined nets within [0.50, 2.00].
- T4 (timing): through EOS's TimingService, extracted
  scenes on both SPEFs with identical netlist, SDC and
  Liberty: absolute delta of worst setup slack at most
  0.10 ns, of worst hold slack at most 0.05 ns, and of
  setup TNS at most 0.50 ns.

## 3. Method

### 3.1 The extresist attempt, and what it measured

Magic's own detailed resistance extractor, `extresist`,
was run on the unit 3.2 extraction, both on the
hierarchical layout and on a flattened copy, with its
thresholds set to zero so every net was a candidate. As
measured, it produced detailed networks for only the 54
port nets in both cases (run logs: Total Nets 1566, Nets
extracted 54 on the hierarchical cell; Total Nets 410,
Nets extracted 54 on the flattened cell), and the top
cell's `.res.ext` file carries `rnode` records for those
54 ports and no `resist` segments for the 392 signal nets.
The reason is structural, not a tuning miss: `extresist`
builds a network around a driving device terminal, and
this layout's cells are LEF abstracts, which contain no
devices at all, so no internal net has a drive point for
it to start from. The port nets, which do have a terminal
point, are exactly the 54 it processed. `extresist` is
therefore not a usable source of per-net resistance
networks for an abstract-cell layout, and this unit does
not use it. Magic's basic extractor does record a per-node
lumped resistance in its `.ext` output (in milliohms); that
value is a single-node simulation parameter, not a network
resistance (for example net36 records 3.380 ohm lumped
against a 1401.9 ohm segment sum on the OpenRCX side), and
it is not used as one here.

### 3.2 The distributed network construction

The network is built per net from the routed DEF geometry
(the same geometry Magic read in unit 3.2), with every
resistance value taken from Magic's extraction parameters
in the pinned `sky130.tech`: sheet resistances li 12.8,
met1 0.125, met2 0.125, met3 0.047, met4 0.047 ohm per
square (the `resist` lines of the tech file, identical to
the `resistclasses` recorded in the `.ext` header, in
milliohms), and contact resistances mcon (li to met1) 9.3,
via1 (met1 to met2) 4.5, via2 (met2 to met3) 3.41 ohm per
contact (the `contact` lines). Layer widths are the tech
LEF default widths (li 0.17, met1 and met2 0.14, met3 and
met4 0.30 um). Vertices sit at segment endpoints,
junctions, via points, and pin points, with segments split
at interior junction, via, and pin points; each resulting
segment is one resistor of sheet resistance times length
over width, and each DEF via is one contact resistor. The
392 nets give 5305 vertices and 4898 resistors (2570 wire
segments and 2328 vias). All 1226 pin connections attach
to a graph vertex within 0.35 um of the pin's transformed
LEF shape, with no stub resistors needed.

Capacitance per net is the Magic basis-C total from unit
3.2 (top-level incident accounting, Section 3.3 of that
document), distributed over the net's vertices in
proportion to incident segment half-lengths, so the
distributed network reproduces the basis-C per-net totals
exactly (verified to 0.000000 fF). Coupling capacitances
are carried as ground capacitance at the vertices, the
accounting the timing reader applies to them.

The SPEF keeps the OpenRCX file's header, name map, ports,
and every net's `*CONN` section verbatim; only the `*CAP`
and `*RES` sections are replaced by the distributed
network. Internal vertices are named in the SPEF
internal-node form `<net>:<number>` that the OpenSTA
SPEF reader requires for parasitic nodes.

## 4. Resistance results (first measurement)

Totals over the 392 nets. OpenRCX total: 24222.1 ohm.
Magic total: 21845.2 ohm.

| Test | Result | Tolerance | Verdict |
|---|---|---|---|
| R1 total ratio | 0.9019 | [0.70, 1.30] | PASS |
| R2 median per-net ratio | 0.9326 | [0.50, 2.00] | PASS |
| R3 nets in [0.5, 2.0] | 392 of 392, 100.0% | at least 70% | PASS |

Per-net ratio quartiles are 0.898 / 0.933 / 0.959
(p25 / median / p75). The distribution is one-sided: 391
of 392 nets are Magic-low, with ratios in [0.822, 1.000],
and the single net above 1 is the clock net `clk` at 1.140
(35.81 ohm Magic against 31.40 ohm OpenRCX on a single
segment-sum accounting). The worst ratio is `_178_` at
0.822 (45.5 against 55.3 ohm). The largest absolute
differences are the longest nets, all Magic-low: `net36`
(1227.6 against 1401.9 ohm), `_205_` (533.0 against
636.5), `_197_` (522.1 against 620.1), `_201_` (522.4
against 619.7), `_157_` (478.9 against 565.0). The offset
is systematic at about 7 to 10 percent across the whole
distribution rather than concentrated in a net class; the
sheet resistances of the two extractors' tech data agree
for met1, met2, and met3 (the LEF RPERSQ values match
Magic's), and differ only on li (12.2 in the LEF against
12.8 in Magic's tech), which acts in the opposite
direction, so the offset is recorded as measured and
attributed to the segment and contact accounting
difference between the two extractors, with no finer
attribution claimed.

## 5. Capacitance re-check and timing (T4 re-judged)

Capacitance, basis C, recomputed from the distributed
network's per-vertex caps: total 1042.0 fF against OpenRCX
1046.6 fF, ratio 0.9956 (T1 PASS); median per-net ratio
1.1932 (T2 PASS); 356 of 392 nets, 90.8 percent, within
[0.5, 2.0] (T3 PASS). These are unit 3.2's basis-C numbers
reproduced exactly, as the construction requires.

Timing through EOS's TimingService, extracted scenes,
identical inputs on both sides (netlist `6_final.v`, SDC
`6_final.sdc`, Liberty tt_025C_1v80), the Magic side now
the distributed SPEF of Section 3.2:

| Scene | Setup WNS (ns) | Setup TNS (ns) | Hold WNS (ns) |
|---|---|---|---|
| OpenRCX SPEF (distributed) | +0.101007 | 0 | +0.470655 |
| Magic SPEF (distributed, this unit) | +0.091064 | 0 | +0.472585 |
| Delta | 0.009942 | 0 | 0.001930 |

Against T4: setup WNS delta 0.010 ns PASS (tolerance
0.10 ns); hold WNS delta 0.002 ns PASS (tolerance
0.05 ns); setup TNS delta 0 PASS (tolerance 0.50 ns).
Both designs meet timing at the 3.4 ns clock. For
contrast, unit 3.2's lumped Magic SPEF measured a setup
WNS delta of 0.208 ns on the same tolerances; the
distributed construction removes the construction
difference that unit proved was the whole of that delta,
and the extractor-to-extractor timing difference that
remains is about 0.01 ns.

## 6. Verdict

- Resistance correlation is measured for the first time
  and passes R1, R2, and R3 as stated in Section 2, with
  a named systematic offset of about 7 to 10 percent
  (Magic low) across the distribution.
- Capacitance correlation on the distributed extraction
  reproduces unit 3.2's basis-C pass exactly.
- T4, re-judged on the distributed Magic SPEF, passes on
  all three metrics. Unit 3.2's T4 failure stands in its
  own document as the verdict on the lumped construction
  it measured; it is not rewritten by this addendum.
- P3-2 is therefore closed as measured: the distributed
  Magic SPEF whose absence was the named gap behind T4
  now exists, T4 passes on it, and resistance correlation
  is no longer unmeasured. The closure is on gcd only,
  with the accounting definitions of Section 2; no
  stronger claim is made.
