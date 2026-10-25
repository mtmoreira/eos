# ledger (L5 session)

The state ledger: revisions, transactions, invalidations and rollback.

Status: implemented (Phase 1). Two-phase transactions (begin, then commit or abort) over dense revisions; artifacts (analyses and reports) register against a revision, and their validity is derived from the record, never stored, so rollback restores earlier validity exactly. Snapshot text serialization (format version 1, checksummed) lives in this module beside Snapshot/Restore; the L5 project file embeds it next to the L2 checkpoint text.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
