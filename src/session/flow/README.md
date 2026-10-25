# flow (L5 session)

Runs flows as orchestrated sequences of stage operations under
the project's ledger and report registry.

Status: the timing closure loop (Phase 2 unit 2.5) is this
module's first feature: `RunTimingClosure` in
`include/eos/session/flow.h` diagnoses the violating scenes,
generates sizing and buffering candidates, screens them through
the ECO transaction protocol, records one ledger transaction
per accepted iteration, and registers the loop's
`timing_closure` report binding at the end. A general flow DAG
runner remains future work.
