# Checkpoint audit (Immediate unit 2, review I11b/U5/C6)

Base: `c5fa125c` (origin/main at audit time). Method: code trace
from the `checkpoint` and `restore` registry commands through the
project save and load path, plus the test tree and docs. No live
execution was used; anything not established from code is listed
under Unresolved, not guessed.

Scope note: this is audit plus scope, not remediation. It publishes
what the checkpoint format saves, what restore reconstructs and
drops, and the remediation scope for each finding. No finding below
is fixed by this document; the scopes in section 8 are the input to
future units, not work done here.

## 1. Headline correction: there is no v74 format in code

The brief for this unit named a "checkpoint format (v74)". That
version could not be verified anywhere in the checkpoint or session
code, and the repo version is `0.0.0` (`CMakeLists.txt:4`). What
actually exists is four independent version-1 text formats, composed
into one file:

| Layer | Magic | Version constant and check |
|---|---|---|
| Project envelope | `EOSPROJ 1` | `src/session/session/src/project.cc:286` (write), `:317-320` (reject non-1 as `kUnsupported`) |
| L2 checkpoint | `EOSCKPT 1` | `src/db/checkpoint/include/eos/db/checkpoint.h:62` (`kFormatVersion = 1`); `src/db/checkpoint/src/checkpoint.cc:187, 543` |
| Ledger | `EOSLEDGER 1` | `src/session/ledger/src/ledger.cc:432, 499-502` |
| Report bindings | `EOSBINDINGS 1` | `src/session/session/src/project.cc:131, 187-190` |

This document audits the format that exists. Any reference to "v74"
elsewhere should be corrected to "format version 1 (four
sub-formats)" unless a real v74 artifact is produced from outside
the repo.

There are two different "checkpoint" concepts that must not be
conflated: (a) the L2 `Checkpoint` (`src/db/checkpoint/`, design
plus scenarios plus constraints only), and (b) the user-facing
`checkpoint` and `restore` commands, which save and load the whole
`Project` file via `Project::SaveProject` and `LoadProject`. The
commands live at `src/ui/registry/src/core_commands.cc:1563-1607`
(`checkpoint` calls `SaveProject`, `restore` calls `LoadProject`).

## 2. What the project file actually saves

`Project::SaveProject` (`src/session/session/src/project.cc:392-431`)
refuses to save with no design open (`kInvalidState`), then
serializes exactly three length-prefixed sections in fixed order
(`BuildProjectText`, `:285-299`): checkpoint text, ledger text,
bindings text. Save is atomic (temporary file plus rename,
`:408-427`), and the L2 save likewise
(`src/db/checkpoint/src/checkpoint.cc:497-523`).

### Saved, partially saved, and not saved

| State | Status | Evidence |
|---|---|---|
| Design graph: name, design id, allocator `next` per kind (8 slots) | Fully | `src/db/checkpoint/src/checkpoint.cc:149-185` (`AppendDesign`); state shape `src/db/design/include/eos/db/design.h:86-95` |
| Cells (id, `is_macro`, name), instances (id, cell, name), nets (id, name) | Fully | `checkpoint.cc:162-174` |
| Ports (id, direction, term, name), terms (id, port), pins (id, instance, net, `pin_name`) | Fully, with one derivation | `checkpoint.cc:175-185`; pin display name (`<instance>/<pin>`) is not stored, it is rebuilt on parse (`checkpoint.cc:389-397`) |
| Scenarios: `next_id`, per-scenario id, active flag, parasitics level, name, mode, corner | Fully | `checkpoint.cc:188-199`; `src/db/scenarios/include/eos/db/scenarios.h:39-53` |
| Constraints: `next_id`, id, kind (0-7), status, bound object id, name, `target_spec` exactly as written | Fully as modelled | `checkpoint.cc:201-214`; `src/db/constraints/include/eos/db/constraints.h:64-75`. Caveat: there is no separate numeric-value field. Whatever an SDC value is, it survives only insofar as it is inside `target_spec` (see Unresolved). |
| Ledger: current revision, `next_txn`, `next_artifact`, pending transaction, full history, all artifacts | Fully | `src/session/ledger/src/ledger.cc:431-466` (`SnapshotToText`); snapshot shape `src/session/ledger/include/eos/session/ledger.h:116-124` |
| Ledger transaction detail: label, tool build, changed-object ids, before and after metrics (value plus unit, `%.17g`), invalidated artifact ids, proof, active flag, rollback checkpoint (equals input revision) | Fully | `ledger.cc:360-386, 440-466`; record shape `ledger.h:84-100` |
| Ledger artifact: id, kind, name, bound revision, proof | Fully | `ledger.cc:440-445`. Validity and current-ness are derived, never stored (`ledger.h:177-186`, `ledger.cc:88-112`) |
| Report bindings: artifact id, bound revision, optional uniform parasitics level, name, timing model, tool build, plus per-scenario snapshot (id, parasitics, name, mode, corner) | Fully as modelled, with two gaps | `project.cc:131-157` (`BindingsToText`); shape `src/session/session/include/eos/session/project.h:56-84` |
| Binding proof, validity, and freshness | Not stored (by design) | Proof is read live from the ledger artifact (`src/session/reports/src/reports.cc:109-130` `StatusFor`); validity is derived from ledger history |
| Collections (`eos_coll_N` to object-id lists) | Not saved | `CommandContext.collections` is interface-session state only (`src/ui/registry/include/eos/ui/registry.h:94-97`) |
| Timing session: ODB database, Verilog path and top, SDC path, Liberty-by-corner paths, SPEF path, last scene results (including worst-slack and TNS numbers) | Not saved, not even the paths | `registry.h:107-114` (comment: "it names files and a physical database the project checkpoint does not hold"); cleared on restore (section 3) |
| Synthesis session: parsed, elaborated, mapped, opt, equivalence, and output records and file paths | Not saved | `registry.h:129-226` (same "not project state" comment) |
| Wire-RC overrides (`set_wire_rc` per-layer values) | Not saved | `registry.h:228-235`; cleared only by `ResetProject` (`core_commands.cc:235`), not by restore (section 3, finding F5) |
| Liberty file contents, SDC file contents, SPEF contents, ODB placement and routing, timing numbers, GUI state | Not saved | No serialization path exists for any of them; the GUI keeps only the last checkpoint and restore path string as a label (`src/ui/gui/src/gui_session.cc:50-53`, `src/ui/gui/src/main_window.cc:178-181`), which is not file content |
| Tool variables and settings | Not applicable, not found | No `set_variable`-style store exists in `core_commands.cc` (grep: no matches); there is nothing of this kind to save |
| Synthesis state, Liberty corner mapping, RC extraction state, technology-package revision | Explicitly future, not saved | `src/db/checkpoint/include/eos/db/checkpoint.h:7-10`: these "join the format as their owning modules land" |

Net effect: a restored project can re-derive which reports were
certified, at which revision, under which scenario names, but not
the timing numbers themselves, not the inputs that produced them
(Liberty, SDC, SPEF, Verilog, ODB), and not any session state.
Re-running timing after restore requires re-establishing all of
those by hand (proven by test, section 5).

## 3. Restore path: what it reconstructs, drops, and does not verify

`Project::LoadProject` (`project.cc:433-505`) parses and validates
everything before adopting anything (checkpoint text, then ledger
text, then bindings text, then rebuild design, scenarios, and
constraints, then `Ledger::Restore`), then adopts in one step. A
corrupt file leaves the live project untouched (tested, section 5).
The command handler then clears session state
(`core_commands.cc:1591-1594`, see section 4).

Reconstructed: design, scenario set, constraint set, ledger
(including a pending transaction, which the user must then commit or
abort), and report bindings that link to a report artifact.

Dropped on restore (by design, counted):

- Report bindings whose artifact is absent from the restored ledger,
  or present but of kind `kAnalysis` rather than `kReport`
  (`project.cc:483-496`). Count only. The dropped names are never
  listed anywhere.
- Constraints are not dropped: a `kBound` constraint whose object is
  missing is flipped to `kUnresolved`, its `bound_object` cleared to
  null, and counted (`checkpoint.cc:576-595`;
  `RestoreInfo.constraints_marked_unresolved`,
  `checkpoint.h:54-58`). Its `target_spec` is retained, so a later
  `RebindAll` (`src/db/constraints/src/constraints.cc:53`) can
  rebind it if the object reappears. Unresolved and empty
  constraints pass through unchanged.

Verified on restore:

- Exact version match per sub-format (any non-1 gives
  `kUnsupported`); fixed section order; strict record shapes,
  duplicate-name and duplicate-artifact rejection in bindings
  (`project.cc:187-275`).
- Per-sub-text FNV-1a-64 checksum over all preceding bytes
  (`checkpoint.cc:501-524`, `project.cc:166-184`, `ledger.cc:499+`);
  truncation, bad magic, and malformed records all fail.
- Module consistency via `FromState` and `Ledger::Restore`: unique
  ids, names, and triples, no dangling design references, allocator
  counters ahead of used ids, dense ledger revision chain, current
  revision equal to last active output, counters ahead of every id,
  pending transaction starting at current revision and invalidating
  only known artifacts (`ledger.cc:156-230`).
- Save-time validation is stricter in one respect: `Serialize`
  refuses a `kBound` constraint pointing outside the design
  (`checkpoint.cc:471-479`), while load tolerates the same situation
  by flipping it to unresolved. That asymmetry is intentional
  ("honest restore").

Not verified on restore (each is a finding, section 8):

- No same-build or tool-version check. No build stamp exists in the
  `EOSPROJ` or `EOSCKPT` envelope at all. `tool_build` strings exist
  only inside bindings (`project.cc:131-157`) and ledger transaction
  records, and are never compared to the running build on load.
- No file integrity beyond FNV-1a. FNV-1a is a non-cryptographic
  checksum: anyone who can edit the file can recompute the trailing
  `end <hex>` line. The tests themselves prove this by hand-repairing
  a checksum to craft a dangling binding
  (`tests/unit/db/checkpoint/checkpoint_test.cc:191-244`).
  Tamper-evidence covers accidents only, not forgery.
- No envelope checksum. The project file itself has no trailing
  checksum; integrity rests entirely on the three embedded texts'
  checksums plus length-prefix truncation checks
  (`project.cc:299-375`).
- No referenced-file check. Moot for Liberty, SDC, SPEF, Verilog,
  and ODB (their paths are not in the file at all), but also: binding
  `timing_model` and `tool_build` strings, binding scenario
  snapshots, and ledger `changed_objects` ids are never cross-checked
  against the restored scenario set, design, or running tool. A
  binding whose scenario set has since changed, or whose
  `bound_revision` is stale, restores silently; staleness is only
  surfaced later by the export gate (section 5).
- No cross-check that ledger transaction `changed_objects` refer to
  objects in the restored design. `Ledger::Restore` checks
  invalidated-artifact existence but not changed-object existence
  (`ledger.cc:196-209`).
- No warning channel. See section 5.

## 4. Restore's session-state wipe, and collections

`restore` handler (`core_commands.cc:1575-1607`), on success only:

```cpp
ctx.collections.clear();          // :1591
ctx.next_collection = 1;          // :1592, handles restart at eos_coll_1
ctx.timing = TimingSession();     // :1593, ODB, paths, last results gone
ctx.synth = SynthSession();       // :1594, all synth records gone
```

Collections: intended behavior, not a bug. The handler comment
(`:1586-1588`) states the rationale verbatim: "Collections named
objects of the previous project state; they do not survive a
restore." The same carve-out is documented in `registry.h:94-114`
for collections and the timing session, and `ResetProject`
(`core_commands.cc:229-236`) does the same on `close_design`. What
is missing is any user-facing documentation of this (only code
comments; section 7). Handle reuse (`next_collection = 1`) is
consistent within the cleared context, but means a script holding a
pre-restore handle like `eos_coll_1` will silently address a
different (or nonexistent) collection after restore. The commands
fail on unknown handles (`core_commands.cc:1353-1370`), so this
fails loudly, not silently.

Finding F5, wire-RC overrides survive restore (likely bug).
`ResetProject` clears `wire_rc_overrides` (`:235`), but the restore
handler does not. After a restore, stale per-layer RC overrides from
the previous design remain live and are consulted by `repair_timing`
(`src/ui/registry/src/repair_commands.cc:637-638, 667-668`). Nothing
in the restore path re-validates them against the restored design.
Remediation scope: add `ctx.wire_rc_overrides.clear()` to the
restore handler (one line) plus a registry test asserting it; or
document it as intended if the override is meant to be global, which
the `ResetProject` behavior contradicts.

## 5. Dropped bindings: the review claim is confirmed, with one important qualification

Confirmed: `LoadProject` drops and counts (`project.cc:483-496`,
`ProjectRestoreInfo` at `project.h:93-98`), and the `restore`
command returns success with only counts in its payload
(`core_commands.cc:1595-1606`: `restored=1 design=... revision=...
unresolved=... bindings_restored=... bindings_dropped=...`). There
is no warning, no diagnostic, no fail-fast, and no listing of which
bindings were dropped. The GUI path likewise only records the
checkpoint path on success (`gui_session.cc:50-53`) and surfaces
nothing about drops.

What happens afterwards: this is fail-closed, not silently wrong,
but only at the export gate:

- `export_check` calls `ReportRegistry::Evaluate`
  (`src/session/reports/src/reports.cc:131-175`): a dropped binding
  means `StatusFor` finds nothing (`found=false`), which blocks as
  `kMissing` ("no report registered under this name"). A
  restored-but-stale binding blocks as `kStale`; an invalid one as
  `kInvalid`. So a dropped binding cannot certify an export.
- `run_timing` and `report_timing` never consult a pre-existing
  binding at all: they rebuild scenes from the (now-cleared) timing
  session via `BuildTimingScenes` (`core_commands.cc:725-775`) and
  fail fast with "no timing design source...", "no SDC...", or "no
  Liberty for corner..." until the user re-runs `read_liberty`,
  `read_verilog`, `import_odb`, `read_sdc`, or `set_spef`. They then
  `RegisterReport`, which overwrites any restored binding of the
  same name wholesale (repeatable reports; `reports.cc:22-88`). So
  there is no "silent wrong result" path through re-running timing.
  The failure mode is inconvenience (full re-setup), not corruption.
- The genuinely silent part is narrower: a user who restores, sees
  `bindings_dropped=2` in a payload, and never runs `export_check`
  has no way to learn which reports vanished, and no persistent
  warning is recorded anywhere (not in the ledger, not in the
  project, not in GUI state).

Remediation scope (per sub-finding):

- Dropped-binding names in the restore payload and report: extend
  `ProjectRestoreInfo` with the dropped names (and unresolved
  constraint names) and print them in the `restore` payload; add a
  registry-level test. No format change required if names are
  reported only, not persisted.
- Warning and fail-fast rule, if the review wants one: decide the
  policy (for example, restore succeeds but sets a sticky "restore
  dropped bindings" flag that `export_check` already enforces, or
  restore fails when `bindings_dropped > 0`). Implement at the
  `restore` handler level, not in `LoadProject`, so library-level
  load stays policy-free. This is a product decision, not just a
  code fix. The current code's stated policy is "dropped and
  counted, never silently kept" (`project.h:18-20`), which the
  counting satisfies literally.

## 6. Test coverage and gaps

Checkpoint unit tests, `tests/unit/db/checkpoint/checkpoint_test.cc`:

- `RoundTripsTheFullState` (design plus scenarios plus constraints,
  including one inactive scenario, bound and unresolved
  constraints), `RestoredAllocatorNeverReusesAnId`,
  `SaveIsAtomicAndLeavesNoTempFile`,
  `LoadRejectsCorruptionTruncationAndStrangers` (flipped byte gives
  checksum mismatch; truncation; non-checkpoint; missing file),
  `SaveRefusesAConstraintBoundOutsideTheDesign`,
  `LoadMarksDanglingBindingsUnresolvedAndCountsThem` (checksum
  repaired by hand), `TextFormRoundTripsAndMatchesTheFileForm`,
  `FromStringRejectsCorruptText`,
  `SerializeRefusesTheSameStatesSaveRefuses`.

Project tests, `tests/unit/session/session/project_test.cc`:

- `RoundTripPreservesDesignScenariosConstraintsAndLedger` (revision
  2, history labels, artifact validity and currentness,
  next-artifact counter), `RoundTripWithAPendingTransaction`
  (commit-after-restore works),
  `CorruptProjectFileIsRejectedAndLiveStateUntouched`,
  `BindingsRoundTripWithTheProject` (including mixed-parasitics
  binding), `BindingWhoseArtifactIsMissingIsDroppedAndCounted`
  (covers both drop causes: unknown id 99 and a present-but-
  `kAnalysis` artifact),
  `ProjectFileWithoutAReportsSectionLoadsWithNoBindings` (the sole
  backward-compat case), `CorruptBindingsSectionIsRejectedAndLiveStateUntouched`,
  `SaveWithoutADesignFails`, `ResetClears...`.

Supporting suites: ledger text and snapshot round-trips and tamper
rejection (`tests/unit/session/ledger/ledger_text_test.cc`,
`ledger_snapshot_test.cc`); reports gate including
`BindingsSurviveRestoreAndTheGateVerdictIsIdentical` and
`BindingImmutabilityAcrossRestore`
(`tests/unit/session/reports/reports_test.cc:307-388`); registry-level
`checkpoint` and `restore` round-trip plus corrupt-file refusal
(`tests/unit/ui/registry/registry_test.cc:220-265`), and the
timing-session-does-not-restore test with re-setup plus
overwrite-not-duplicate (`:880-900`); qualification
`phase1_exit_test.cc:250-267` (restore preserves design and
scenarios, bindings 2/0).

Not tested (gaps):

1. No explicit unsupported-version test at any layer (code paths
   exist at `checkpoint.cc:543`, `project.cc:190, 320`,
   `ledger.cc:502`; no test feeds `EOSCKPT 2`, `EOSPROJ 2`, and
   similar).
2. No test that `restore` clears collections, resets
   `next_collection`, or clears the synth session; no test for
   wire-RC override survival (F5) either way.
3. No command-level test where a binding is dropped and the user
   then runs `export_check` (the gate's `kMissing` path is tested
   only in-memory, `reports_test.cc:219`).
4. No test that dropped-binding names are surfaced (they are not,
   see section 5).
5. No test for a stale-but-restored binding's gate verdict after
   restore (stale path tested only in-memory,
   `reports_test.cc:243`).
6. No test for version migration (there is no migration code to
   test) or for loading a checkpoint written by a different tool
   build.
7. No test asserting what `restore` does to GUI state beyond the
   checkpoint-path label (`tests/unit/ui/gui/gui_test.cc:1301-1329`
   covers only dispatch bookkeeping).

## 7. Versioning, migration, and docs claim vs. code

Versioning is exact-match only, per sub-format, with no migration
code anywhere (grep for migration and backward-compat across
`src/db/checkpoint/`, `src/session/session/`, and
`src/session/ledger/` returns nothing substantive). An older
checkpoint loads only if it is already version 1 in every
sub-format. A newer (v2+) checkpoint is rejected outright with
`kUnsupported`. The single backward-compatibility carve-out is
structural, not versioned: a project file with no reports section at
all (written before bindings became project state in Phase 2 unit
2.0) loads with an empty binding set (`project.cc:362-375`,
`project.h:21-23`; tested at `project_test.cc:300-331`).

Docs claim vs. code: `src/db/checkpoint/README.md` says "Saves and
restores full project checkpoints" but immediately qualifies it to
"the L2 state that exists"; `src/session/session/README.md` calls
the file "the single-file project checkpoint" holding design,
scenarios, constraints, ledger, and bindings. Both are accurate for
`Project` members but overstate if read as "the whole session":
collections, timing session, synth session, and wire-RC overrides
are excluded by code comments, not by any user-facing doc. There is
no stated checkpoint-contents policy in `docs/` (grep for `EOSPROJ`,
`EOSCKPT`, and contents-policy across `docs/` returns only
exit-report mentions of `EOSPROJ 1`); the authoritative statement
is the header comments in `checkpoint.h:1-10` (which also promise
future components: synthesis state, Liberty corner mapping, RC
extraction state, technology-package revision) and
`project.h:1-23`.

Remediation scope: (a) add a one-page checkpoint-contents policy to
`docs/` stating the saved set, the session-state exclusions, the
exact-match version rule, and the pre-bindings carve-out,
reconciling the two READMEs' "full project" wording; (b) if
cross-version loading is ever wanted, add a version-dispatch and
migration layer in `FromString` and `SplitProjectText` plus
version-rejection tests. That is a format project, not a patch.

## 8. Findings list (each with remediation scope)

| # | Finding | Evidence | Remediation scope |
|---|---|---|---|
| F1 | Format is v1 times 4, not v74; exact-match only, no migration | `checkpoint.h:62`; `project.cc:286, 317`; `ledger.cc:432, 499`; `project.cc:131, 187` | Correct the audit doc (done here); add version-rejection tests; migration only as a separate format project |
| F2 | Timing inputs and results are not saved, not even file paths; restore forces full timing re-setup | `registry.h:107-114`; `core_commands.cc:1593`; `registry_test.cc:888-900` | Document as intended (session vs. project split), or add a timing-session section to the format plus tests |
| F3 | Synth session not saved; same re-setup consequence | `registry.h:129-226`; `core_commands.cc:1594` | Same as F2 |
| F4 | Collections cleared on restore: intended, code-documented, but user-undocumented; handles restart at 1 | `core_commands.cc:1586-1594`; `registry.h:94-97` | Document in user docs; add a registry test asserting clear plus handle reset |
| F5 | Wire-RC overrides survive restore while `ResetProject` clears them: inconsistent, likely bug | `core_commands.cc:235` vs. `:1591-1594`; `repair_commands.cc:637-668` | One-line clear in restore handler plus test, or document as global if intended |
| F6 | Dropped bindings counted but never named; no warning or fail-fast rule | `project.cc:483-496`; `core_commands.cc:1595-1606` | Report dropped names in payload (plus test); decide restore-fail vs. sticky-warning policy at handler level |
| F7 | No same-build check; `tool_build` stored but never compared | No build stamp in envelope; `project.cc:131-157` | Add build and tool stamp to envelope and a load-time policy (warn vs. reject) plus test, if wanted |
| F8 | Integrity is FNV-1a only; checksum recomputable by any editor; no envelope checksum | `checkpoint.cc:501-524`; `project.cc:299` | Document as accident-detection only; HMAC or signature only if threat model requires it |
| F9 | Binding scenario snapshots, timing model, stale revisions, and ledger `changed_objects` never cross-checked against restored state | `project.cc:483-496` (artifact presence and kind only); `ledger.cc:196-209` | Add load-time cross-checks (or document that the export gate is the enforcement point) plus tests |
| F10 | "Full project checkpoint" README wording overstates coverage vs. the session-state exclusions | `src/db/checkpoint/README.md`; `src/session/session/README.md` vs. `registry.h:94-114` | Publish a checkpoint-contents policy doc; align README wording |

Note on F10 and section 7: this audit document itself is the first
half of that policy doc (the saved set, the exclusions, and the
version rule are all stated here). A future unit may fold it into a
shorter user-facing policy page and align the two README wordings;
that wording change is deliberately left out of this unit so the
audit stays an audit.

## 9. Unresolved (stated, not guessed)

1. "v74": no matching version, tag, or format number was found in
   checkpoint and session code or `CMakeLists.txt`. Possibly a
   review-item or document version external to the repo; it cannot
   be mapped to code from this tree.
2. SDC numeric values: `Constraint` carries only `target_spec` (a
   string) plus kind, status, and bound object. Whether a clock
   period or delay value is embedded in `target_spec` or lives
   elsewhere (for example only in the SDC file, which is not saved)
   could not be determined from the checkpoint code alone. The
   `create_constraint` command's construction of `target_spec` was
   outside this audit's traced path.
3. GUI hierarchy and browser state on restore:
   `SessionView::RefreshHierarchy` exists, but whether every GUI view
   fully rebuilds from the restored project (vs. caching pre-restore
   objects) was not traced to a verdict.

## 10. Relation to the review's checkpoint language

Until this audit, the review's rule applied: checkpoint coverage was
"mechanism present, semantics unaudited", and every "full project
checkpoint" style claim had to carry that label. With this audit
published, the semantics are audited for the format at `c5fa125c`:
the saved set (section 2), the restore and drop behavior (sections
3-5), the version rule (section 7), and the open findings with
scopes (section 8). Later claims about checkpoints should cite this
document and name any finding they rely on as open, rather than
reusing the old "semantics unaudited" label or claiming full
coverage. The review's dropped-binding warning and fail-fast rule
remains an open product decision (section 5), not a closed item:
the code is fail-closed at the export gate, but no warning rule
exists yet.
