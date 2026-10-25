# design (L2 design-data)

Owns the Design object graph on the physical database.

Status: implemented (Phase 1, flat model). Cells, instances, nets, ports with paired terms, and pins, all on persistent IDs with per-kind unique names and consistency checks on creation. Append-only in this increment; deletion arrives with the Phase 2 ECO work.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
