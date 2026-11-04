# finish (L4 engines)

Chip finish: filler insertion, basic power grid synthesis, optional
metal fill, and GDSII stream-out preparation.

Status: implemented under unit P7 of the place-and-route phase.
`eos::finish [-pdn] [-fill_config <rules.json>]` runs on the routed
design in the live database: it inserts filler cells into the row
gaps (through the in-tree detailed placer's fillerPlacement, over
the library's CORE SPACER masters), and with -pdn it builds the
basic power grid (core grid, followpins, straps, and grid vias
through the imported PdnGen engine in src/pd/pdn) on the design's
power and ground nets. With -fill_config it runs metal density
fill through the imported Finale engine in src/pd/fin. The command
fails fast on an unrouted design, on a library with no filler
master, and (with -pdn) on a design with no power or ground net.

`eos::write_gds <path>` writes the finished layout as GDSII. It
fails fast when finish has not run on the block. The writer is
EOS-owned glue in this module (src/gds_stream.cc): no OpenDB GDSII
writer exists at the ead3236 pin, and the io/gds module is still a
skeleton, so the plan's "OpenDB writer already in tree" premise
does not hold at this pin. The writer emits the top structure (one
SREF per placed instance, at the instance origin with its
orientation), one structure per used master (pin rectangles as
boundaries), routed wires as PATH elements, vias as their own layer
boxes, special-wire boxes (the power grid), and metal fills as
boundaries. It writes no text labels and no blockages, and branched
wire junction seeding is best-effort (linear nets, the fixture
shape, decode exactly).

A successful finish sets the eos_finish marker (and eos_finish_pdn
when the grid ran) on the block; write_gds refuses a design without
the marker. A second finish run is idempotent: filler placement
fills only remaining gaps, and an existing grid is counted, not
rebuilt.

The stable public API lives in include/eos/pd/finish.h and is
versioned per the EOS Code Structure document. Correlation evidence
against the ORFS gcd baseline lives in the module note
docs/correlation-note.md (fixture numbers only; the gcd evidence
run was infeasible, see that note for the verbatim evidence).
