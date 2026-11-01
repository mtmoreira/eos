# floorplan (L4 engines)

Floorplanning: die and core areas, rows, tracks and IO report,
from the imported OpenROAD InitFloorplan engine.

Status: adopted in place-and-route unit P1. The command is
eos::floorplan (see the registry): it runs initFloorplan plus
makeTracks on the session's OpenDB design and reports the die
and core areas, the row count, the track-grid count and the
placed-vs-total IO count. The stable public API of this module
is include/eos/pd/floorplan.h (the EOS-owned runner); the
imported engine sources and their port patches are documented
in PROVENANCE.md and PATCHES.md. Macro placement is not in v1
(the test vehicle has no macros); tie-cell insertion is
imported but not driven by eos::floorplan v1.

The stable public API of this module will live in include/ and is versioned per the EOS Code Structure document.
