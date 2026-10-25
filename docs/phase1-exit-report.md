# Phase 1 exit report: coherent session

Date: 2026-10-24
Status: Phase 1 exit condition met on the evidence below. This
report is published as a pull request and is not merged by the
build driver: closing Phase 1 is the project owner's decision.

## 1. What Phase 1 required, and the verdict

From the approved plan (Section 17, Roadmap):

> Phase 1 deliverable: Tcl shell on the command registry, project
> schema, state ledger, scenario model, full checkpoint and
> restore, reports bound to revisions. Exit condition: state
> invalidation and constraint coverage tests pass; restart and
> rollback tests pass; no stale result can certify an export.

| Requirement | Verdict | Evidence |
|---|---|---|
| Tcl shell on the command registry | Met | Increment 4 (PR 7): one registry, 29 core commands, Tcl 8.6 shell exposing every command as `eos::<name>` |
| Project schema | Met | Unit 5.1 (PR 8): the Project owns design, scenarios, constraints and ledger; the project file is a single `EOSPROJ 1` file |
| State ledger | Met | Increment 1 (PR 4): two-phase transactions, derived artifact validity, rollback that restores validity exactly |
| Scenario model | Met | Increment 2 (PR 5): scenario identity is the (mode, corner, parasitics) triple; report bindings snapshot the active set |
| Full checkpoint and restore | Met | Increment 3 (PR 6) and unit 5.1 (PR 8): checkpoint format v1 with checksum and all-or-nothing load; project restore is real, including the ledger and any pending transaction |
| Reports bound to revisions | Met | Unit 5.2 (PR 9): a report binding is created with its ledger artifact and records the bound revision, scenario snapshot, parasitics level, timing model and tool build |
| State invalidation tests pass | Met | Unit suites plus qualification test `StateInvalidationDemotesReports` |
| Constraint coverage tests pass | Met | Unit suites plus qualification test `ConstraintCoverageAccountingIsExact` |
| Restart and rollback tests pass | Met | Unit suites plus qualification tests `RestartReproducesStateIncludingTheLedger`, `RollbackAfterRestartRestoresValidityExactly`, and both crash mid-transaction variants |
| No stale result can certify an export | Met | Unit 5.2 export gate (one refusal test per blocker class) plus qualification test `StaleOrUnprovenReportMakesExportCheckRefuse` through `eos::export_check` |

## 2. Per-increment summary

Every increment merged on green CI, verified at the check-run
level (both jobs, "CMake build" and "Structure and layer checks")
on the pull request and again on main after the merge.

| Increment | PR | Merge commit | What landed | Tests after merge |
|---|---|---|---|---|
| 1. Ledger core | [PR 4](https://github.com/mtmoreira/eos/pull/4) | `4c66a5c6ef7c94139ebf0b5082799e63605846d5` | L0 ids (32-bit packed ObjectId, monotonic allocator) and errors (Status and Result); L5 ledger core: two-phase transactions, derived artifact validity, rollback restoring validity exactly, artifact freshness semantics | 24 GoogleTest cases |
| 2. Design data | [PR 5](https://github.com/mtmoreira/eos/pull/5) | `d910aeabc8211258714670b954bc1c3b1534e15c` | L2 design (flat persistent-ID object graph, consistency-checked creation), scenarios (identity is the mode, corner and parasitics triple, active set), constraints (bound, unresolved or empty, never dropped, coverage accounting, RebindAll) | 43 GoogleTest cases |
| 3. Checkpoint | [PR 6](https://github.com/mtmoreira/eos/pull/6) | `2e0d84b68984ff59f58a983b33a113c4b5a27965` | ExportState and FromState with full validation; checkpoint format v1 (FNV-1a checksum, atomic save, all-or-nothing load, dangling bindings flipped to unresolved and counted); ledger snapshot and restore with crash mid-transaction abort and commit tests | 54 GoogleTest cases |
| 4. Registry and Tcl shell | [PR 7](https://github.com/mtmoreira/eos/pull/7) | `a61894f556a03d7546a960b9ae526d6b183d5fe0` | L6 registry (diagnostic taxonomy, CommandResult, 28 core commands at that point, glob collections as live ID handles, unsupported options rejected never ignored), Tcl shell, and the `apps/eda` binary (batch scripts exit 1 on first error, stdin REPL) | 68 GoogleTest cases, 10 ctest suites |
| 5.1 Session and project | [PR 8](https://github.com/mtmoreira/eos/pull/8) | `f1a5f5fd505be1212f5ad6b285edc850c67b542e` | Checkpoint text serialization, ledger snapshot text round trip, the L5 Project (design, scenarios, constraints and ledger as one owned unit), project-level `eos::checkpoint` and a real `eos::restore` | 80 GoogleTest cases, 12 ctest suites |
| 5.2 Reports, metrics, export gate | [PR 9](https://github.com/mtmoreira/eos/pull/9) | `1025198a6d25cf5ceada80eb8d1327d58872ca91` | Immutable report bindings, metric diff with unit checks, the lexicographic CostPolicy verdict (timing, then power, then area, deciding metric named), and `eos::export_check` returning verified_pass or proof_unknown naming every blocker | 102 GoogleTest cases, 14 ctest suites |
| 5.3 Exit suite and this report | This PR | Not merged (owner decision) | The qualification suite under `tests/qualification/` and this report | 109 GoogleTest cases, 15 ctest suites |

## 3. The qualification suite

`tests/qualification/phase1_exit_test.cc`, wired into CTest as
`qualification.phase1_exit`. The unit suites prove each mechanism
in isolation; this suite proves them composed in one project,
through the surfaces a real session uses. One test per exit
condition:

| Test | Exit condition it proves |
|---|---|
| `StateInvalidationDemotesReports` | A transaction that invalidates a report demotes it (not valid, not current, gate blocks as invalid), and rolling the change back restores it, because validity is derived, never stored |
| `ConstraintCoverageAccountingIsExact` | Four constraints in three classes count exactly (total 4, bound 2, unresolved 1, empty 1); the gate's coverage blocker carries the same numbers; rebinding after the missing object appears moves the count instead of hiding it; an empty set is complete for nothing |
| `RestartReproducesStateIncludingTheLedger` | A saved project loads with design, scenarios, coverage, ledger revision, full history and derived validity intact; re-saving the restored project reproduces the file byte for byte; ID counters continue without reuse |
| `RollbackAfterRestartRestoresValidityExactly` | After a restart, a new invalidation followed by rollback to the restart revision restores validity and freshness exactly, and rollback to revision 0 restores the revision 0 state exactly, with the audit history retained and flagged inactive |
| `CrashMidTransactionRestartAbort` | A project saved with a transaction pending (the crash) restarts with the decision intact; aborting ends in exactly the pre-transaction state |
| `CrashMidTransactionRestartResume` | The same restart, decided the other way: the transaction commits with full effect (revision, record, changed objects, before and after metrics, invalidation) |
| `StaleOrUnprovenReportMakesExportCheckRefuse` | Through the command registry: a current proven report passes `eos::export_check` with verified_pass; after a later commit the same report refuses as stale, and a fresh unproven report refuses on its proof, both with proof_unknown |

## 4. Final test counts, from real runs

Local gates run on 2026-10-24 on this branch, before publishing.
CI on this pull request re-runs the same gates.

- CMake configure and build: OK.
- CTest: 15 of 15 suites passed, no skips. The suites are the 14
  unit suites from unit 5.2 plus `qualification.phase1_exit`.
- GoogleTest cases: 109 in total (102 before this unit, plus 7
  qualification cases).
- `python3 tools/check_layers.py`: OK, 56 modules, 7 layers.
- `python3 -m pytest tools/tests -q`: 8 passed, no skips.

## 5. Known limitations

- Q1. Report bindings are session state, not project state. A
  project restore carries the ledger artifacts across the restart,
  but the bindings that say what a report certifies are cleared, so
  a report must be registered again in the new session before
  `eos::export_check` will pass it. A pre-restart report name reads
  as missing to the gate until then. This is a deliberate Phase 1
  boundary, recorded in the reports module header; checkpointing
  bindings is a later-phase decision.
- Q2. No engines run yet. Phase 1 is the coherent session only:
  the L3 and L4 modules beyond the adopted placeholders are stubs,
  no synthesis, placement or routing happens, and the metrics and
  CostPolicy machinery compares values that callers supply rather
  than values an engine measured. That is the Phase 2 scope
  (closure under ECO transactions), not a Phase 1 gap.
- Q3. The design model is flat and append-only. Names are unique
  per kind within one design, one design is open at a time, and
  object deletion, with its invalidation consequences, arrives
  with the ECO work in Phase 2. Hierarchy arrives in Phase 4.
- Q4. Checkpoint and project formats are version 1 only. A file
  with an unknown version is rejected, never migrated; format
  evolution has no compatibility promise yet.
- Q5. The Tcl shell needs Tcl at build time. When Tcl is not
  found, the build falls back to a stub shell and the registry
  still works through the C++ API; CI installs Tcl, so the shell
  suite (`ui.tcl`) runs there and in the local run reported above.
- Q6. The Phase 0 limitation L1 stands unchanged: the pinned
  OpenROAD build has no Qt, so ORFS final report images are not
  produced. Phase 1 does not touch the upstream baseline.

## 6. Exit assessment

Every element of the Phase 1 exit condition is exercised by a
passing test at the project level, not only at the unit level:
state invalidation demotes reports and rollback restores them,
constraint coverage accounting is exact and reaches the export
gate with its numbers intact, restart reproduces the whole
project including the ledger (byte for byte on re-save), rollback
after restart restores artifact validity exactly, a crash
mid-transaction restarts into a decision that can go either way
with the correct outcome each way, and neither a stale nor an
unproven report can certify an export. The limitations above are
boundaries the phase set for itself, plus Q1, which is stated
plainly for the owner's judgement.

Phase 1 is complete on that basis, subject to the owner's
decision on this pull request. Phase 2 (closure) gets its own
breakdown and its own go; nothing in this report starts it.
