# lvs_run and the in-tree connectivity LVS engine (F13)

The first in-tree LVS, connectivity only: `eos::lvs_run`
compares two independent data structures inside the one live
database, the logical topology (each net's instance pins and
the block's top-level pins, written by the importer) against
geometric connectivity derived from the routed shapes alone
(written by the router), and stores the verdict in the
session, so `eos::signoff_summary` and the Signoff panel
report it instead of the LVS verdict parsed from the external
PV report. It recognizes no devices, compares no W or L, and
folds nothing; it is never device-level LVS, and the panel
labels it "connectivity" wherever it appears.

## Commands

- `eos::lvs_run` (no options in v1): runs connectivity LVS
  on the live database and stores the record plus the design
  fingerprint it was run against. Fails fast with the
  offending state named when there is no technology data, no
  top block, no routed design (no wires at all), or no
  logical netlist (no net carries a pin). Only a successful
  run updates the stored record; a failed run leaves the
  previous record untouched.
- `eos::signoff_summary` (unchanged options): the LVS section
  comes from the in-tree record when an `lvs_run` has stored
  one (`lvs_source=intree`, with the comparison counts in the
  header and the per-kind and mismatch lines in place of the
  report's LVS block), else from the external report
  (`lvs_source=report`, byte-identical to before this unit).
  The DRC section follows the same shape from `drc_run`.

## The comparison

Every routed shape (wire segment rectangles, via metal boxes
at their placed positions, special-wire boxes, and the pin
rectangles themselves) is a node in one union-find. Two
shapes union when they touch on the same layer: wire-wire
and pin-wire contact union on edge-or-overlap (bounding
boxes overlap in area, or abut along an edge segment of
positive length; a corner-only contact does not union: the
design's closed-box rule was narrowed as built under design
section 11, after the engine suite's diagonal-abut case
fired under it). Pin-pin contact unions on area overlap
only (design section 18): LEF pin rectangles are access
geometry, not poured metal, so adjacent instances' pin rects
abutting edge to edge do not join their nets. All boxes of
one via instance union together,
because a via is one conductor; that clause is what joins
layers. The instance-pin-to-net assignment plays no part in
building the components: a component is pure geometry.

Per net, the verdict is:

- **open**: the net's pins fall into more than one component.
  One mismatch per pin outside the primary component (the
  one holding the net's alphabetically first pin), naming
  the pin (`<inst>:<pin>`, or the top pin's name) and its
  rectangle. A pin whose rectangle touches no wire shape is
  its own component; a net with pins and no wire at all
  reports every pin after the first.
- **short**: a component holds shapes of two or more
  compared nets. One mismatch per additional net against the
  alphabetically first, naming both nets, the layer of the
  contact, and the contact box.
- **floating**: a component holds wire shapes and no pin.
  One mismatch per component, charged to its net, naming
  the layer of its first shape and the component's bounding
  box. A stub still touching its net's main component is
  part of that component and is not floating; it is floating
  only when it touches nothing.

## Verdict shape

The `lvs_run` payload is one header line, the three
`lvs_check` lines (always printed, clean or not), and one
`lvs_mismatch` line per mismatch, in deterministic order
(kind: open, short, floating; then net, then pin, then
bounding box):

```
lvs source=live-database nets_compared=185 pins_compared=612 mismatches=0 single_pin_nets=0 special_nets_skipped=0
lvs_check kind=open mismatches=0
lvs_check kind=short mismatches=0
lvs_check kind=floating mismatches=0
lvs_mismatch kind=open net=<net> pin=<inst>:<pin> bbox=x1,y1,x2,y2
lvs_mismatch kind=short net=<a> other_net=<b> layer=<layer> bbox=x1,y1,x2,y2
lvs_mismatch kind=floating net=<net> layer=<layer> bbox=x1,y1,x2,y2
```

A clean design reports zero per kind, never a bare PASS
with no counts. The header also counts `single_pin_nets`
(compared nets with exactly one pin: they can be shorted or
carry floating geometry, but they cannot be open, and the
count is reported so their silence is not mistaken for
coverage) and `special_nets_skipped` (the exclusions below).

## Exclusions, named not silent

- Nets whose signal type is POWER or GROUND, and nets
  OpenDB marks special, are excluded from the comparison
  and counted in `special_nets_skipped`. Their geometry in
  this flow is special-wire and grid geometry (the finish
  PDN posture), whose connectivity contract is the grid's,
  not the netlist's.
- Instances whose masters carry no terminals (fillers, tap
  cells) contribute no pins and no shapes and never enter
  either side. This is what makes LVS after `finish`
  well-defined: the fillers finish adds are invisible to the
  comparison by construction, not by a name filter.
- Metal fills are not walked. They are not in any wire; the
  finish unit's fills are boundaries in the GDS, not
  database wire geometry.
- A pin with no net is on neither side and is not reported
  in this version: it belongs to no logical net, so no
  comparison can charge it.

## Stale evidence

The stored LVS record carries the design fingerprint from
the run. `signoff_summary` flags `stale=1` when the current
fingerprint differs from it, independent of the report
re-baseline: re-reading the reports does not refresh the
in-tree LVS verdict. Running LVS after `finish` is the
recommended posture: the verdict then covers the finished,
exported design, and stays fresh until anything moves the
fingerprint. A verdict taken before finish reads stale the
moment finish runs, even though the counts would come out
identical (fillers add no pins and no compared shapes);
freshness is about which design state was measured. A re-run
after an edit measures the database as it stands: after an
`insert_buffer`, LVS honestly reports opens for the new
buffer's pins until the design is re-routed.

## Non-goals (this unit)

- Device-level LVS is a later phase, gated on cell geometry
  data. This engine compares connectivity only.
- No KLayout or Netgen import. The engine is EOS-owned and
  adds no licence surface.
- No cap on reported mismatches in v1: the deterministic
  order bounds the payload's shape and the demo scale bounds
  its size.
- Pairwise shape contact is O(n^2) per layer: fine at mock
  and fixture scale, documented, not a production-scale
  sweep.
- The engine reads no LEF rule, so the DRC engine's "no rule
  at all fails fast" condition has no analogue here.

## Tests

- `tests/unit/pv/lvs/lvs_test.cc` (ctest `pv.lvs`): a clean
  routed fixture compares clean with the fixture's counts
  (including the skipped POWER net, the single-pin net, and
  the invisible filler); each seeded mismatch is caught
  exactly once (a dropped segment's open, an unwired net's
  opens, a bridge short with its contact box, a dangling
  stub's floating); a via-joined two-layer net compares
  clean; fail-fast with a null database, no technology, no
  design, no routed design, and no logical netlist. Every
  broken design is built in a temp-dir scratch database by
  the test that judges it; no committed broken fixture
  exists.
- `tests/unit/ui/registry/registry_test.cc`: `lvs_run`
  fail-fast and argument rejection in the shared process;
  the clocked end-to-end test (its own ctest entry,
  `ui.registry.lvs.end_to_end`) runs the finished demo,
  cross-checks `nets_compared` against the route record,
  consumes the record through `signoff_summary`, flips the
  stale flag with an edit, severs one routed net's wire
  through the raw database handle and reads the opens back,
  and proves a failed run leaves the record untouched; the
  restore test clears the record with the other nine
  sessions and falls back to the report source.
- `tests/unit/ui/gui/gui_test.cc`: the Signoff model parses
  in-tree LVS payloads and their located mismatches, rejects
  malformed in-tree payloads (verdict/count disagreement,
  kind counts not summing, mismatch lines not matching kind
  counts), reads older payloads without an `lvs_source` key
  as "report", and the Signoff tab renders the connectivity
  section.
