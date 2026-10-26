# flow (L5 session)

Runs flows as orchestrated sequences of stage operations under
the project's ledger and report registry.

Status: the timing closure loop (Phase 2 unit 2.5) is this
module's first feature: `RunTimingClosure` in
`include/eos/session/flow.h` diagnoses the violating scenes,
generates sizing, resynthesis and buffering candidates, screens
them through the ECO transaction protocol, records one ledger
transaction per accepted iteration, and registers the loop's
`timing_closure` report binding at the end. Since unit 2.9 the
generation aims first at the violating endpoints' fanin cones
(worst endpoint first, instances in path order), with the
whole-design enumeration as fallback, and screening carries
two economics that never change acceptance: a rejection memory
for targeted candidates whose cone and endpoint slack are
unchanged since their rejection, and a stale-target skip for
candidates whose endpoint has closed. Each screening also
hands the transaction the loop's current analysis as its
before side, so a screening pays one full re-analysis instead
of two. A general flow DAG runner remains future work.
