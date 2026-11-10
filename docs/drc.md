# drc_run and the in-tree DRC engine (F13)

The first in-tree layout DRC: `eos::drc_run` runs the LEF-driven
engine on the live ODB and stores the verdict in the session, so
`eos::signoff_summary` and the Signoff panel report it instead of
the DRC verdicts parsed from the external PV report.

## Commands

- `eos::drc_run` (no options in v1): runs DRC on the live
  database and stores the record plus the design fingerprint
  it was run against. Fails fast with the offending state named
  when there is no technology data, no top block, or no routed
  design. Only a successful run updates the stored record; a
  failed run leaves the previous record untouched.
- `eos::signoff_summary` (unchanged options): the DRC section
  comes from the in-tree record when a `drc_run` has stored
  one (`drc_source=intree`), else from the external report
  (`drc_source=report`). The LVS and antenna verdicts always
  come from the external reports until the connectivity LVS
  unit lands.

## Verdict shape

The `drc_run` payload is one header line plus one `drc_check`
line per check and one `drc_violation` line per located
violation:

```
drc source=live-database total_violations=1
drc_check check=metal1.width layer=metal1 rule=180 violations=1
drc_violation check=metal1.width layer=metal1 net=n_mid detail=width=100 bbox=2950,6650,3050,7350
```

A clean design reports zero violations per check, never a bare
PASS with no counts. The header's `rule` is the rule value in
DBU (square DBU for min-area). The violation detail is a single
token: `width=<min dimension>` for a width violation,
`gap=<edge-to-edge distance>` for a spacing or cut-spacing
violation, `area=<merged shape area>` for a min-area violation,
`step=<jog length>` for a min-step violation, `metal=<layer>`
for an enclosure violation (the metal layer that fails to
enclose the cut).

## Check families (first version)

Rules come from the technology LEF through OpenDB
(dbTechLayer tables). A layer with no rule for a family is not
checked for that family. Checks are named `<layer>.<family>`:

| Check | Rule | Shapes |
| --- | --- | --- |
| `<layer>.width` | LEF MINWIDTH | wire, pin, and via-metal rects: min dimension >= MINWIDTH |
| `<layer>.spacing` | LEF SPACING | same-layer rect pairs from different nets: edge-to-edge distance >= SPACING (overlap counts as gap 0, so a short between two nets is flagged here) |
| `<layer>.min-area` | LEF AREA | merged shape per net per layer (union of touching/overlapping rects): area >= AREA |
| `<layer>.min-step` | LEF MINSTEP | wire-path jogs: a centerline segment perpendicular to both its neighbors, with length < MINSTEP |
| `<cut>.enclosure` | LEF ENCLOSURE | every via cut rect enclosed by the metal rects above and below by >= the overhangs (first = below, second = above); one violation per (cut rect, metal layer) |
| `<cut>.cut-spacing` | LEF SPACING on the cut layer | cut-rect pairs from different nets on the same cut layer: edge-to-edge distance >= SPACING |

Geometry comes from the live ODB through RawDatabase(): net
wires decoded with dbWireDecoder (paths, junctions, vias,
patch rects, non-default rules), special wires, instance pin
rects via dbMTerm/dbMPin transformed to absolute coordinates,
and top-level pin rects. The engine checks the live database,
NOT the GDS file (no GDS reader exists); the verdict names its
source ("live-database") so the signoff claim says which
artifact was checked.

## Stale evidence

The stored DRC record carries the design fingerprint from the
run. `signoff_summary` flags `stale=1` when the current
fingerprint differs from it, independent of the report
re-baseline: re-reading the reports does not refresh the
in-tree DRC verdicts. The fingerprint covers the synth record,
the timing record, and the live database identity (top block,
counts, placement hash). A floorplan-only or routing-only
change moves no instance, so it does not flip the fingerprint
in v1; a place or a new mapping does. That is the documented
v1 limitation of the fingerprint, shared with the external
report evidence.

## Non-goals (this unit)

- Density and antenna checks stay out.
- Device-level LVS is a later phase; the LVS source stays on
  the external reports.
- No KLayout or Netgen import. The engine is EOS-owned and
  adds no licence surface.
- Metal fills are not checked in this version.
- A wire under a non-default rule is measured at the rule
  width when the decoder reports one, else at the layer
  default width. That can miss a spacing violation on a
  widened wire, never invent one.
- Min-area uses the sum of the rect areas, which
  double-counts the overlap where two rects join; that
  over-counts area, so it can only miss a violation, never
  invent one.
- Min-step covers wire-path jogs only; notches in merged
  shapes are not covered.
- Pairwise spacing is O(n^2) per layer: fine at mock and
  fixture scale, documented, not a production-scale sweep.

## Mock PDK rules

The mock tlef gains DRC rules through the sanctioned
gen_mock.py route (invented fixture values, documented as
invented, never process data): per routing layer, SPACING =
pitch - width, MINWIDTH = width - 0.02 um, AREA = width *
0.1 um2, MINSTEP = 0.1 um. SPACING equals the edge-to-edge
distance between adjacent-track wires, so router output on
adjacent tracks sits exactly at the rule. The mock has no cut
layers and no VIA definitions; via enclosure and cut spacing
are exercised by the DRC engine unit tests with their own
inline fixtures.

## Tests

- `tests/unit/pv/drc/drc_test.cc` (ctest `pv.drc`): a clean
  routed fixture reports 0 violations per check with the rule
  values read from the LEF; each seeded violation is caught
  exactly once with the right rule, layer, and bounding box
  (narrow wire, close wires, bad via enclosure, small shape,
  short jog, close vias); fail-fast with no technology, no
  design, and no routed design.
- `tests/unit/ui/registry/registry_test.cc`: `drc_run`
  fail-fast and payload shape, `signoff_summary` using the
  in-tree record, the report source without a run, and the
  stale flag after a design change.
- `tests/unit/ui/gui/gui_test.cc`: the Signoff model parses
  in-tree payloads and their located violations, rejects
  malformed in-tree payloads, and reads older payloads without
  a `drc_source` key as "report".
