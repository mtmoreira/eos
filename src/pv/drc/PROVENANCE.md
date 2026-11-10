# drc: provenance

No upstream import. The DRC engine in this module is EOS-owned
original code, written from scratch for the F13 DRC engine unit
(2026-11-10). No file was copied from any upstream tree, no
submodule, no external process at runtime, and no new licence
surface is added by this module.

## What it reads

The engine reads geometry from the live in-tree OpenDB database
(through `odb::dbWireDecoder`, instance and top pin rects, and
special wires) and reads DRC rules from the technology LEF through
the in-tree OpenDB `dbTechLayer` tables (min width, spacing,
enclosure, min area, min step, cut spacing and enclosure). Those
OpenDB sources are themselves the EOS in-tree import covered by
`src/db/odb/PROVENANCE.md` (OpenROAD commit ead3236); this module
adds no further upstream dependency.

## Licence audit

There is nothing to audit: the module contains only EOS-owned
original code (`src/drc.cc`, `include/eos/pv/drc.h`) plus the
EOS-owned registry command, GUI model, and tests. No third-party
licence text is present in this module.
