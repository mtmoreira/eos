# `finish`, `write_gds`, the finish and GDS session records, and the GDS viewer (full-flow GUI unit F8)

`eos::finish` runs the finish stage on the session's
routed database: it places filler instances in the row
gaps, optionally builds a basic power grid (`-pdn`) and
metal fill (`-fill_config <path>`), and marks the block
finished. `eos::write_gds <path>` streams the finished
design out as GDSII. Neither command changed shape in
this unit; what F8 adds is the session records behind
them, the two summary queries, an in-tree GDS reader,
and the GUI surfaces that render all of it.

```text
finish [-pdn] [-fill_config <path>]
write_gds <path>
```

The runner fails fast, each time naming the missing
stage: no database, no technology, no rows (not
floorplanned), no placed instance (not placed), no
signal net carrying a wire (not routed), no filler
master in the library, a `-pdn` run without exactly
one POWER and one GROUND net, and an unreadable fill
rules file. `write_gds` refuses a design `finish` has
not marked. A second `finish` is idempotent: it
reports the same counts and places nothing new.

The payloads are one line each:

```text
fillers=<n> grid_shapes=<n> metal_fills=<n> pdn=<true|false>
gds=<path> size=<bytes> instances=<n> structures=<n>
```

`fillers` is the count of filler instances the run
placed, `grid_shapes` the special-wire boxes of the
PDN grid (zero without `-pdn`; the grid is a basic
core grid, explicitly not a signoff PDN),
`metal_fills` the density-fill shapes (zero without a
rules file; EOS ships no default rules, on the same
principle as the extraction model). For `write_gds`,
`instances` is the placed instance count of the
finished design (fillers included), `structures` one
plus the distinct masters those instances use, and
`size` the exported file's size in bytes.

## The finish and GDS session records

On success only, each run is stored in a session
record (`ctx.finish`, `ctx.gds`), mirroring the route
and extract records: a single run each, stored field
for field from the run's report, replacing any earlier
record wholesale. A failed run leaves the record
untouched, so a failed `finish -pdn` after a successful
bare `finish` leaves the summaries reporting the
successful run. The records are cleared by project
reset and are not persisted. The GDS record stores no
geometry: `gds_summary` reads the exported file back
through the reader at query time, so what the viewer
draws is the file, not a memory of it.

## `finish_summary` and `gds_summary`

```text
finish fillers=<n> grid_shapes=<n> metal_fills=<n> pdn=<true|false>
```

`finish_summary` answers from the finish record: the
finish payload's four values under the `finish` lead
word. Before any successful finish it fails fast with
`no finish: eos::finish first`.

`gds_summary` answers from the GDS record plus the
file. Its header carries the record's values (what
`write_gds` reported, never re-measured), then a
structure line per structure in file order, a layer
census, and the flattened top-structure geometry the
viewer draws:

```text
gds path=<path> size=<bytes> instances=<n> structures=<n> library=<name> dbu_per_micron=<n>
structure name=<name> top=<0|1> boundaries=<n> paths=<n> srefs=<n>
layer gds=<number> name=<tech-name|-> datatype=<n> boundaries=<n> paths=<n>
boundary structure=<name> layer=<tech-name|-> gds_layer=<number> datatype=<n> points=<x>,<y>[,...]
path structure=<name> layer=<tech-name|-> gds_layer=<number> datatype=<n> width=<n|none> points=<x>,<y>[,...]
sref structure=<name> target=<name> x=<n> y=<n> reflect=<0|1> angle=<degrees|none>
```

`dbu_per_micron` is derived from the file's own UNITS
(the reciprocal of the first real: one database unit
in user units, the user unit being the micron in every
file the writer produces). Before any successful
export the query fails fast with `no gds:
eos::write_gds first`. With a record in place, a file
that was deleted or replaced outside the session fails
the query (naming the path, or the header consistency
check when the recount disagrees with the record):
never a header-only success over a missing or foreign
file.

## The GDS reader: `src/io/gds`

`eos::io::ReadGdsFile` parses a GDSII stream into
structures, boundaries, paths, and SREFs, exactly and
without rounding. Its required input domain is the
writer's own output: the record grammar `pd/finish`
emits (an `ENDEL` after an element is tolerated
because some writers emit one; EOS's does not).
Unmodeled elements (TEXT, NODE, BOX, AREF) are skipped
by length, not rejected. Malformed streams (truncated
records, a record length under 4, an element outside
a structure, a structure with no name, a non-integer
XY payload) fail fast and totally, naming the file
and the offset. It is a reader for EOS-written files
and a stated subset of the format, not a general GDS
importer.

## The via translation fix

Files written before this unit carry a defect in
their via geometry: the writer emitted each via's
boxes at via-local coordinates, so every via boundary
sat at the layout origin instead of at its via point.
Measured on the pd/finish fixture before the fix: 12
vias, 36 via box boundaries, all 36 centered at the
origin, none at a via point. The writer now translates
each via box by the decoder's last point on the wire,
the same rule the DRC engine's `ViaBoxRect` applies,
in both the `VIA` and `TECH_VIA` cases; a via opcode
with no preceding point keeps the old emission, so
the fix never invents a position. Record counts and
file size are unchanged. After the fix, the read-back
census on the fixture and on the clocked demo centers
every via boundary at a via point the route geometry
query reports. Files written before this unit are not
rewritten and still carry the defect.

## The GUI surfaces

The stage bar gains a Finish button between Extract
and STA, enabled once a finish record exists. Running
`finish` or `write_gds` from the console lands on the
GDS canvas, the sixth center page: a counts column
(the finish line, the export header, the per-layer
census, the top structure's SREF count) beside the
drawn geometry, with a layer selector that filters
what is drawn, in the routing canvas's shapes. The
Signoff tab gains a Finish section rendering the
finish record and the export header. The underlying
models (`FinishModel`, `GdsModel`) are Qt-free and
all-or-nothing: a malformed payload or a failed query
keeps the last good model. Finish moves the design
fingerprint, so signoff and DRC verdicts measured
before a finish read as stale until re-measured; this
unit re-runs no PV check and renders no verdict of
its own.

## The mock filler fixture

The clocked demo's finish runs on the mock PDK's
`EM_FILL`, a `CLASS CORE SPACER` filler master. The
cell is one site wide (0.2 um at the mock's 0.2 um
site): filler gaps are counted in sites, and a wider
filler cannot tile the 1-to-4-site gaps the placed
rows leave (a 5-site `EM_FILL` made `finish` fail
with DPL-0002 on this design). On the demo, finish
places 1085 fillers and exports 1259 instances in 21
structures at 233416 bytes; those numbers, like every
mock number, validate mechanics only, never a
signoff claim.
