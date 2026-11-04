# pdn (L4 engines)

OpenROAD power distribution (PdnGen power grid synthesis),
imported as EOS-owned code from the OpenDB pin commit ead3236.

Status: implemented under unit P7 of the place-and-route phase, as
the finish runner's basic-grid engine (see src/pd/finish). The
imported files are byte-identical copies of the upstream files;
see PROVENANCE.md for the SHA-256 table and PATCHES.md for the
EOS-owned adaptations (none touch an imported file). The Tcl/SWIG
factory is not imported: the finish runner constructs pdn::PdnGen
directly.

The stable public API for consumers is pdn/PdnGen.hh; the finish
module is the only consumer. This is a basic grid only (core grid,
followpins, straps, grid vias): no IR analysis, no
current-density check, no ring or macro grid.
