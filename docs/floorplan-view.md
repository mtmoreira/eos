# `floorplan_summary` and the floorplan canvas (full-flow GUI unit F3)

`eos::floorplan_summary` is the structured floorplan query the
GUI floorplan canvas reads: one payload with the die and core
rectangles, one rectangle per row, and one entry per IO pin,
plus the row, track-grid, and IO counts. It is the first
physical view, and it defines the query-command pattern the
later physical views reuse: one structured query over the
session database, never payload text parsed in the GUI.

```text
floorplan_summary
```

It is a query, not a report: it registers no report binding,
writes no file, and changes no session state. It takes no
options. It fails fast with "no technology data loaded:
eos::import_odb first" when no technology is loaded, with "no
mapped design: the imported database holds no top block" when
the database holds no top block, and with "no floorplan: the
design holds no rows (eos::floorplan first)" when the database
holds no rows.

## What it reads

The query reads the live database through RawDatabase, the L4
path, with the query command as the boundary, in the
synth_hierarchy pattern. The facade does not expose die area,
rows, or IO positions, so this is the only way the GUI can see
them. The query runs no engine and mutates nothing: die area
from `block->getDieArea()`, core area from
`block->getCoreArea()` (the extent the rows actually cover, as
dbBlock reports it), rows from `block->getRows()` plus
`row->getBBox()`, track grids from `block->getTrackGrids()`,
and IO pins from `block->getBTerms()` with the name, the IO
type (INPUT, OUTPUT, INOUT, FEEDTHRU), the first pin
placement status, and the pin bounding box when placed
(0,0,0,0 when unplaced: an unplaced pin has no geometry to
draw, and the canvas counts it instead).

The core area a query reports is the core as dbBlock reports
it, which is the rows' bounding box: the extent the rows
actually cover. It is NOT the core area the `floorplan`
payload reports: the payload carries the core the floorplan
run was asked for, while the query carries the core the rows
cover. The two agree only when the rows fill the requested
core. On the registry fixture (a 10000 by 10000 die, a 10000
by 10000 requested core, a 0.46 by 2.72 micron site), the
payload says core_area=0,0,10000,10000 and the query says
core=0,0,9660,8160: three rows of 21 sites, 9660 DBU wide and
8160 DBU tall.

## The payload

The first line is the header, then one `row` line per row
rectangle and one `pin` line per IO pin, in database order.
Values are single tokens: no value in this payload contains a
space. Counts in the header must match the lines that follow:
the row count matches the row lines, the IO total matches the
pin lines, and the IO placed count matches the pins with
placed=1. The GUI model validates all three, so a payload
whose counts disagree with its lines is malformed.

The bridged optloop demo reports this payload, measured live
from the chain in `demo/synth/README.md` (die 10000 by 10000
at 60 percent utilization, one row of 25 em_site cells, six IO
pins, ten track grids from the mock tech LEF):

```text
floorplan die=0,0,10000,10000 core=0,0,10000,6400 rows=1 track_grids=10 io_placed=6 io_total=6
row rect=0,0,10000,6400
pin name=a dir=in placed=1 rect=-100,1900,100,2100
pin name=b dir=in placed=1 rect=-100,3900,100,4100
pin name=c dir=in placed=1 rect=-100,5900,100,6100
pin name=d dir=in placed=1 rect=-100,7900,100,8100
pin name=y1 dir=out placed=1 rect=9900,3233,10100,3433
pin name=y2 dir=out placed=1 rect=9900,6566,10100,6766
```

Coordinates are database units, exactly as the database holds
them. A pin rectangle may straddle the die edge: the bridge
writes small PORT rectangles around the placed point, so a
left-edge input pin in the bridge demo reads like
-100,1900,100,2100 (a 200 by 200 rect), and a registry-fixture
pin with a 100 by 100 PORT rect reads like -50,950,50,1050.

## The canvas

The floorplan canvas lives in the center, as the third page of
the center stack beside the block diagram and the schematic.
A successful `floorplan` dispatch takes the user there, and
the Floorplan stage button takes them there too, once a
floorplan is loaded (the button stays disabled until then).
Hierarchy navigation (a tree click, an issue click) takes the
user back to the hierarchy views, and a successful
`close_design` does the same.

The canvas draws the die outline (white, heavier pen), the
core outline (white), one rectangle per row (grey), and one
marker per placed IO pin with its name label, with the payload
counts beside it: die, core, rows, track grids, IO placed over
total. The scene's Y axis is flipped, so the database's north
stays at the top of the canvas. A pin with no placed geometry
has no marker: the counts column counts it instead. The
outline style matches the schematic: white contours with no
fill, white text, on the theme background. When no floorplan
is loaded the canvas shows a placeholder
("(no floorplan yet: run eos::floorplan first)") and the counts
column shows the same text.

The model refreshes on every window refresh while a design is
open: the query is a cheap database walk, so no timing-key
guard is needed, exactly like the hierarchy refresh. The
refresh dispatches `floorplan_summary` without an echo line (a
model refresh is not a user action) and clears the model when
the query or its payload fails, so a stale floorplan never
lingers.
