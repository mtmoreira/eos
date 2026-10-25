# transactions (L4 engines)

The ECO transaction engine: the Section 9 protocol (checkpoint,
propose edit, re-analyze all required scenes, commit or roll back
on the all-scene verdict) as code, over an ODB database.

Status: implemented in Phase 2 unit 2.4. The design checkpoint is
an ODB binary snapshot; acceptance is a full re-analysis of every
required scene through the L3 timing service at estimate fidelity
(extracted scenes are refused: their SPEF describes the pre-edit
netlist). The verdict follows the Plan, Section 8: a metric that
crosses below the timing gate, or a violating metric that gets
worse, is a regression in any scene and rolls the edit back. The
ledger wrap is the caller's, because L5 owns the ledger: begin a
ledger transaction, run the ECO inside it, commit or abort the
ledger transaction with the outcome. The stable public API lives
in include/eos/eco/transactions.h.
