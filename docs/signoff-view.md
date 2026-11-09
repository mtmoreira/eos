# `signoff_summary` and the Signoff panel (full-flow GUI unit F13)

The first build of the approved option (a) sequence: the panel
first, fed by parsing the existing external KLayout and Netgen
reports, then the LEF-driven DRC engine, then connectivity
LVS. This unit adds no engine: the verdicts come from the
recorded evidence documents, never from hard-coded strings,
and a missing or unparseable report fails fast with the path
named, never a silent empty panel.

## What it reads

`eos::signoff_summary [-pv <path>] [-lvs <path>]` reads two
evidence documents and returns the verdict record as
machine-readable lines. The defaults are the recorded gcd
verdicts, relative to the working directory:
`docs/gcd-pv-results.md` and `docs/gcd-lvs-magic.md`. The
parsers live in the L4 module `src/pv/signoff` (target
`eda_pv_signoff`), pure text processing over the documents,
with section and table cross-checks: the DRC category total
must agree with the summary table, the LVS class counts must
agree between the two documents, and a discrepancy named in
one document but not the other fails the parse. Antenna is
reported from the PV document's antenna section; wiring the
already-imported AntennaChecker is a separate later unit,
not this one.

## The payload

Run from the repository root with no design loaded, the gcd
evidence renders as:

```
signoff drc=pass lvs=fail antenna=pass stale=0 drc_categories=145 drc_violations=0 antenna_net_violations=0 antenna_pin_violations=0 pv_report=docs/gcd-pv-results.md lvs_report=docs/gcd-lvs-magic.md fingerprint=3c0d17bad169557b
drc family=m1 categories=8 violations=0
drc family=m2 categories=8 violations=0
drc family=m3 categories=6 violations=0
drc family=m4 categories=6 violations=0
drc family=m5 categories=4 violations=0
drc family=via categories=6 violations=0
drc family=via1 categories=2 violations=0
drc family=via2 categories=7 violations=0
drc family=via3 categories=5 violations=0
drc family=via4 categories=4 violations=0
drc family=li categories=6 violations=0
drc family=ct categories=3 violations=0
drc family=nsm categories=4 violations=0
drc family=pad categories=1 violations=0
lvs path=klayout classes_compared=68 classes_equivalent=67 verdict=fail
lvs path=magic classes_compared=68 classes_equivalent=67 fingers_extracted=3676 fingers_reference=3676 verdict=fail
lvs issue=D1 cell=none kind=extraction_artifact path=klayout
lvs issue=D2 cell=sky130_fd_sc_hd__a21oi_2 kind=unresolved path=klayout
lvs issue=D1 cell=none kind=closed path=magic
lvs issue=D2 cell=sky130_fd_sc_hd__a21oi_2 kind=resolved_artifact path=magic
lvs_residual cell=sky130_fd_sc_hd__a21oi_2 transistors=4 instances=2 cause=proven path=magic
```

DRC: 0 violations in 145 categories, across the fourteen
families above (70 categories; the remaining 75 are one
offgrid or angle category per layer). LVS: NOT CLEAN on both
extraction paths, 67 of 68 classes equivalent, with D1 (the
substrate net model, an extraction artifact on the KLayout
path, closed under Magic) and D2 (the `a21oi_2` series
midpoint, unresolved on the KLayout path, resolved as an
extraction artifact under Magic) named, and the single Magic
residual (4 transistors in 2 instances, cause proven).
Antenna: 0 net violations, 0 pin violations. The fingerprint
is the FNV-1a hash of the design identity (synth record,
timing record, top block, instance and net counts, placement
hash); with no design loaded it hashes "none".

## The panel

The Signoff tab is the seventh right-panel tab (index 6). It
renders the verdicts in plain text: DRC PASS with the
violation count and the per-family table, LVS NOT CLEAN with
the per-path comparison and the named discrepancies, the
residual, antenna PASS, and the fingerprint, each with its
report path. A successful `signoff_summary` dispatch takes
the user there, and the Signoff stage button takes them there
too once verdicts are loaded. The SignoffModel is Qt-free and
parses the payload all-or-nothing, with the same cross-checks
the command applies (a DRC pass means zero violations, an LVS
pass means both paths pass, the family category sum stays
within the header total, the family violation sum equals the
header total).

## Stale evidence

The session keeps a baseline: the fingerprint plus the two
report paths and their content hashes. The first run
baselines (stale=0). A changed report re-baselines (stale=0).
A baseline taken with no design adopts the design when one
loads instead of flagging stale. Otherwise a changed design
fingerprint with unchanged reports is stale=1, and the
record is left untouched. The panel shows STALE in that case
until the checks are re-run. The refresh re-dispatches the
query on every window refresh (a cheap file parse plus a
design walk, like the floorplan refresh), so the stale flag
updates promptly after any design change. A floorplan-only
or routing-only change moves no instance, so it does not flip
the fingerprint in v1; a place or a new mapping does.

## What it does not do

This unit adds no DRC engine, no connectivity LVS, no
device-level LVS, and no KLayout import. The external
evidence path keeps working: `docs/gcd-pv-results.md` and
`docs/gcd-lvs-magic.md` remain the recorded verdicts for the
ORFS gcd layout, and no in-tree run re-states them.
`report_qor` keeps its electrical DRC summary unchanged:
layout DRC verdicts live in the PV commands and the Signoff
panel, not in `report_qor`. Mock runs validate mechanics
only; no mock-based run is ever cited as a signoff claim.
