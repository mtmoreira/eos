# `route`, the route session record, and `route_summary` (full-flow GUI unit F6)

`eos::route` runs the in-tree TritonRoute engine on the
session's placed database (grt for global routing, drt for
detailed routing), with the engine defaults:

```text
route [-detailed_iterations <n>] [-detailed_bound <n>] [-layer <name>...]
```

The command requires the session database with technology
and a top block, a floorplanned and placed design, and a
loaded Liberty; each missing prerequisite fails fast with
the stage-command message for it. An unknown layer fails
with the driver's "unknown layer" message, zero detailed
iterations fail with the driver's "iterations must be
positive" message, and bad options fail with the usage
message. A clockless design routes fine: route needs a
placed design and a Liberty, not a clock.

The payload reports the engine's counts and states its
honesty limit in words:

```text
route routed=<n> total=<n> guides=<n> overflow=<n> violations=<n> detailed_iterations=<n> detailed_bound=<n> stopped_at_bound=<bool> liberty_corner=<c> layer_1=<name> layer_2=<name> ... layer_10=<name> scene=<name> worst_slack_ns=<value>
```

`routed` is the count of nets the engine reports routed,
`total` the count of nets it was asked to route, `guides`
the guide count, `overflow` the overflow total, and
`violations` TritonRoute's DRC-marker count. The detailed
router runs at most `detailed_bound` iterations (64 by
default) and reports how many it used; `stopped_at_bound`
is true exactly when the router hit the bound. The layer
terms name the routing layers in the default range (10 on
the mock, em1 through em10). The payload's timing truth is
the per-scene post-route worst slack from the session
timing service on the routed database (estimated
parasitics, the same analysis `place` reports). When the
session is not configured for timing on the database, the
payload carries `sta_slack_ns=unavailable (<reason>)`
instead.

On demo/clocked, the unavailable form is what the run
reports today: `sta_slack_ns=unavailable (scene func_tt:
could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc
line 2, empty ports/pins/nets argument.)`. The root cause is
in the timing service's SDC read, not in routing: it
re-reads `clk_top.sdc` against the flat imported database,
and the SDC's generated-clock source, `[get_pins u_div/q]`,
does not resolve there (the flat DEF names the divider
`u_div/_17_`, pin `Q`; OpenSTA warns `pin 'u_div/q' not
found`). Every ODB-path analysis on this demo reports the
same posture, `place` and both `cts` runs included, and
`run_timing` fails with the same error before and after
route. A separate SDC pin-resolution fix addresses it;
until then the payload, the record, and `route_summary`
carry the statement verbatim rather than a finite slack
the analysis never produced.

`violations` is the router's own DRC-marker count, reported
and never failed on, and never labeled as a DRC verdict:
that is `drc_run`'s separate engine. A run with violations
above zero is still a successful route; the count is
recorded so the number is visible, not hidden.

## The route session record

On success only, the run is stored in a session record
(`ctx.route`), mirroring the place and CTS records: a
single run, replaced by the latest successful run. A failed
run leaves the record untouched. The record is interface
session state, not project state: it is cleared when the
design closes, and a restore does not bring it back.
`route` does not clear the timing session's last results
and does not touch the place or CTS records, so
`place_summary` and `cts_summary` keep reporting their
runs and `timing_summary` keeps answering from the last
`run_timing` until a new one replaces it.

Unlike CTS, a re-run of `route` succeeds: the runner
strips the prior guides and wires first, so the second
run genuinely re-routes, and the record holds the second
run's numbers.

## `route_summary`

```text
route_summary
```

The structured query the GUI reads: the recorded run, plus
the live wire geometry of the routed database. It is a
query, not a report: it runs no engine, writes no file,
and changes no session state. It takes no options. With an
empty record it fails fast:

```text
EOS invalid_configuration: no routing: eos::route first
```

Payload shape (the demo/clocked record, verbatim except
the SDC path inside the note, which echoes the path the
session read the file from; the measured numbers are
recorded in demo/clocked/README.md):

```text
route routed=<n> total=<n> guides=<n> overflow=<n> violations=<n> detailed_iterations=<n> detailed_bound=64 stopped_at_bound=<bool> liberty_corner=tt die=<x0>,<y0>,<x1>,<y1> core=<x0>,<y0>,<x1>,<y1>
layer number=<n> name=<layer> capacity=<n> usage=<n> overflow=<n>
wire net=<net> layer=<layer> width=<dbu> points=<x0>,<y0>,<x1>,<y1>,...
via net=<net> layers=<lower>-<upper> x=<dbu> y=<dbu>
unwired net=<net>
sta_note sta_slack_ns=unavailable (scene func_tt: could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc line 2, empty ports/pins/nets argument.)
```

The header carries the record's counts and the die and
core rectangles from the live database. One `layer` line
per layer in the default range, in layer order, with the
engine's per-layer congestion numbers (capacity, usage,
overflow). One `wire` line per path segment of each routed
signal net, decoded from the net's `dbWire`: the points
list is a single comma-separated token (an even count of
coordinates, at least two points), so the line stays
whitespace-splittable. One `via` line per via, with the
layer pair it connects. A net the engine did not route
appears on one `unwired net=<net>` line; the unwired count
matches the header's `total` minus `routed` exactly.
Supply and special nets are not listed: the query skips
them, like the engine does. A run that measured finite
slacks carries one `slack scene=<name>
worst_slack_ns=<value>` line per scene instead of the
`sta_note` line; a run carries one or the other, never
both.

Measured on demo/clocked (F6 probe, the same session as
the F5 CTS run):

```text
route routed=185 total=185 guides=520 overflow=0 violations=0 detailed_iterations=2 detailed_bound=64 stopped_at_bound=false liberty_corner=tt die=0,0,86000,86000 core=0,0,86000,83200
layer number=1 name=em1 capacity=1720 usage=155 overflow=0
layer number=2 name=em2 capacity=424 usage=109 overflow=0
layer number=3 name=em3 capacity=284 usage=0 overflow=0
layer number=4 name=em4 capacity=212 usage=0 overflow=0
layer number=5 name=em5 capacity=172 usage=0 overflow=0
layer number=6 name=em6 capacity=136 usage=0 overflow=0
layer number=7 name=em7 capacity=100 usage=0 overflow=0
layer number=8 name=em8 capacity=80 usage=0 overflow=0
layer number=9 name=em9 capacity=60 usage=0 overflow=0
layer number=10 name=em10 capacity=56 usage=0 overflow=0
```

then 1326 `wire` lines and 987 `via` lines over all 185
nets, no `unwired` lines, and the STA note verbatim. A
second `route` in the same session replaces the record
with a byte-identical summary. The clockless optloop demo
measures `routed=6 total=6 guides=22 overflow=0
violations=0 detailed_iterations=1 detailed_bound=64`.

## The GUI surface

The center routing canvas is the route surface. A `route`
typed in the console navigates to it on success. The stage
bar's Route button is enabled once a route record is
loaded and shows the canvas when clicked. The canvas draws
the die and core outlines, the placed instances faintly
behind the wires, one polyline per wire segment in its
layer's color, and one square per via in its upper layer's
color. A layer selector above the counts panel filters the
canvas to one layer's segments (and the vias touching it);
`All layers` restores the full view. The side panel names
the run:

```text
Routed: <routed> / <total>
Guides: <guides>
Overflow: <overflow>
Violations: <violations> (stopped at bound | completed)
Detailed iterations: <iterations> / <bound>
Liberty corner: <corner>
[post-route worst slack (<scene>): <x> ns | the sta note verbatim]
Vias: <n>
[per-layer congestion table, one row per layer, OVERFLOW flagged]
[unwired nets, when any]
```

(The numbers are the demo/clocked record. The violations
row is the router's DRC-marker count, not a DRC verdict.)

The canvas is the fifth page of the center stack, after
the schematic, floorplan, and placement pages. Only one
center canvas shows at a time: a successful `route` clears
the floorplan and placement flags, and a successful
`floorplan` or `place` clears the routing flag, so a
re-place after route shows the new placement.

## What changes after route

Routing moves no instance and rebuilds no tree, so
`clock_network_summary` still shows both trees per pin
with the same sink counts and buffer nodes as after CTS,
and `place_summary` and `cts_summary` are byte-identical
after a route. `run_timing`'s behavior after route matches
its behavior at the place stage on the same session: the
same failure, from the SDC pin resolution gap that
predates this unit, with no new failure mode from routing.
