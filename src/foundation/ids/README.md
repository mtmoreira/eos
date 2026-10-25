# ids (L0 foundation)

Persistent object ID types, and their allocation and serialization rules.

Status: implemented (Phase 1). ObjectId packs kind (top 4 bits) and a per-kind index (low 28 bits) into one 32-bit value; IdAllocator hands out indices monotonically and restores them from checkpoints without reuse.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
