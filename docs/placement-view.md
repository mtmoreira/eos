# `place_summary` and the placement canvas (full-flow GUI unit F4)

`eos::place_summary` is the structured placement query the
GUI placement canvas reads: one payload with the die and core
rectangles, the placed and total instance counts, HPWL, and
overlap count from the last successful place run, one line per
placed instance rectangle, and one line per scene the place
run measured post-place slack for. It is the second physical
view, and it follows the query-command pattern F3 defined: one
structured query over the session database plus the stored
place record, never payload text parsed in the GUI.

```text
place_summary
```

It is a query, not a report: it registers no report binding,
writes no file, and changes no session state. It takes no
options. It fails fast with "no technology data loaded:
eos::import_odb first" when no technology is loaded, with "no
mapped design: the imported database holds no top block" when
the database holds no top block, and with "no placement:
eos::place first" when no place run has succeeded in this
session.

## What it reads

The query reads the live database through the facade
`Instances()`, in the synth_hierarchy pattern, plus the
session place record `ctx.place` that `eos::place` stores on
success. The counts, HPWL, overlap count, and slacks come
from that record, never from the place payload text, so the
query and the payload can never disagree. The rectangles come
from the live database: each instance name, master, and
placed location from `Instances()`, with the width and height
from the master's library size (the F4 facade addition: the
same geometry the placer's own overlap check uses). The die
and core rectangles come from the same floorplan geometry
query F3 uses, so the placement canvas and the floorplan
canvas agree on the die and core.

A failed place run leaves the previous record untouched, like
`run_timing` and `last_results`: the query keeps reporting the
last successful placement until a later run succeeds. The
record is cleared by `ResetProject` alongside the timing and
synth records, so a closed design never leaves a stale
placement behind.

The post-place slack lines appear only when the place run
measured them: the session must be configured for timing
(a scenario, a Liberty for the scenario's corner, and an SDC)
at place time. Without timing, place still succeeds and the
payload states `sta_slack_ns=unavailable` with the reason,
and the summary carries no slack line instead of inventing
one. With timing, one slack line appears per scene, with the
same number the place payload reported.

## The payload

The first line is the header, then one `inst` line per placed
instance and one `slack` line per measured scene, in database
order. Values are single tokens: no value in this payload
contains a space. Counts in the header must match the lines
that follow: the total count matches the inst lines, and the
placed count never exceeds the total. The GUI model validates
both, plus unique instance names, unique slack scene names,
and a non-empty rectangle for every instance, so a payload
whose counts disagree with its lines is malformed.

The bridged optloop demo reports this payload, measured live
from the chain in `demo/synth/README.md` (the mock Liberty
maps the optloop to two cells, a 4-input AND and a buffer, on
a die 10000 by 10000 at 60 percent utilization, one row of 25
em_site cells; the SDC is the demo `top.sdc`, a 1.25 ns
virtual clock with zero input and output delays):

```text
placement die=0,0,10000,10000 core=0,0,10000,6400 placed=2 total=2 hpwl=37266 overlaps=0
inst name=g0 master=EM_AND4_X1 x=1200 y=0 w=4400 h=6400
inst name=g1 master=EM_BUFWK x=5600 y=0 w=2000 h=6400
slack scene=func_tt worst_slack_ns=0.9860799954
```

Coordinates are database units, exactly as the database holds
them. An instance rectangle is (x, y, x + width, y +
height): g0 sits at x 1200 with width 4400, so it spans
1200 to 5600, and g1 sits at x 5600 with width 2000, so it
spans 5600 to 7600. Both fill the core height (6400), side by
side. The HPWL (37266) and the slack (0.9860799954 ns) are
the same numbers the `place` payload reported on the same
run, by construction: the query reads the stored record, not
the payload text.

## The canvas

The placement canvas lives in the center, as the fourth page
of the center stack beside the block diagram, the schematic,
and the floorplan canvas. A successful `place` dispatch takes
the user there, and the Place stage button takes them there
too, once a placement is loaded (the button stays disabled
until then). Hierarchy navigation (a tree click, an issue
click), the Floorplan stage button, and a successful
`close_design` take the user back. Placement wins over
floorplan while its model is loaded: placement is the later
stage, so a successful place takes the user past the floorplan
canvas.

The canvas draws the die outline (white, heavier pen), the
core outline (white), and one rectangle per placed instance,
colored by master (outline plus a light fill of the same
color, so the color reads at any zoom), with the payload
counts beside it: die, core, placed over total, HPWL,
overlaps, one post-place worst slack per measured scene, and
the master legend, one line per master with its instance
count. A master keeps the same color across refreshes: the
color is a hash of the master name over a fixed palette. The
scene's Y axis is flipped, so the database's north stays at
the top of the canvas, like the floorplan canvas. The outline
style matches the schematic and the floorplan: white contours
for the die and core, white text, on the theme background.
When no placement is loaded the canvas shows a placeholder
("(no placement yet: run eos::place first)") and the counts
column shows the same text.

The model refreshes on every window refresh while a design is
open: the query is a cheap database walk plus the stored
place record, so no timing-key guard is needed, exactly like
the floorplan refresh. The refresh dispatches `place_summary`
without an echo line (a model refresh is not a user action)
and clears the model when the query or its payload fails, so
a stale placement never lingers.
