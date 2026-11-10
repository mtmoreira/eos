# drc (L4 engines)

Design rule checking: verdicts and violation reporting on a layout.

Status: implemented (F13 DRC engine unit). The LEF-driven
in-tree engine: rules from the technology LEF through OpenDB
(dbTechLayer tables), geometry from the live ODB through
RawDatabase(). Public API in include/eos/pv/drc.h: `RunDrc`
produces a `DrcRecord` (per-check violation counts, each
violation located by layer, rule, and bounding box) or fails
fast naming the offending state (no tech, no top block, no
routed design, no DRC rules).

Check families (first version): width and spacing per layer
on wires, vias, and pins; enclosure of vias by metal; min
area and min step on wire shapes; cut spacing between vias.
Density and antenna stay out; device-level LVS is a later
phase. See docs/drc.md.
