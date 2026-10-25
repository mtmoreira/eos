# checkpoint (L2 design-data)

Saves and restores full project checkpoints.

Status: implemented (Phase 1) for the L2 state that exists: design graph with allocator state, scenario set, constraint set. Line-oriented versioned format with an FNV-1a checksum, atomic save via temp file and rename, validated save, all-or-nothing load, and honest restore (dangling constraint bindings are marked unresolved and counted, never dropped). Save and Load are thin wrappers over the public text forms (Serialize and FromString); the L5 session layer embeds the checkpoint text in the project file beside the ledger snapshot text. The remaining Section 10 components join as their modules land.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
