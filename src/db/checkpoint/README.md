# checkpoint (L2 design-data)

Saves and restores full project checkpoints.

Status: implemented (Phase 1) for the L2 state that exists: design graph with allocator state, scenario set, constraint set. Line-oriented versioned format with an FNV-1a checksum, atomic save via temp file and rename, validated save, all-or-nothing load, and honest restore (dangling constraint bindings are marked unresolved and counted, never dropped). The state ledger and the remaining Section 10 components join through the L5 session layer as their modules land.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
