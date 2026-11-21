# `close_timing`, the ECO session record, and `eco_summary` (full-flow GUI unit F9)

`eos::close_timing` is the ECO stage's run: the judged
timing-closure loop over the session's live physical
database (Phase 2). It derives its sizing families and
buffer masters from the database, screens each candidate
edit through the ECO transaction protocol (checkpoint,
edit, full re-analysis of every required scene, all-scene
verdict, commit or roll back), and stores the final
analysis into the timing session:

```text
close_timing <max_iterations>
```

The command requires the session's physical database
with a top block, an SDC (`read_sdc`), and at least one
active scenario with a Liberty for its corner. Its
payload is unchanged by this unit:

```text
closed=<0|1> iterations=<n> candidates_tried=<n> candidates_rejected=<n> revision=<n> report=timing_closure
scene=<name> worst_slack_ns=<value|none> total_negative_slack_ns=<value|none> worst_hold_slack_ns=<value|none> total_negative_hold_slack_ns=<value|none> initial_worst_slack_ns=<value|none>
stop_reason=<text>
```

Closure acceptance runs at estimate parasitics fidelity
only: a session holding an active extracted scenario is
refused before any analysis or edit, because after an
edit the extracted scene's SPEF no longer describes the
design, and EOS has no incremental extraction to
re-derive it. The refusal is the engine's doctrine and
this unit does not soften it.

`repair_timing` is not part of this stage: it repairs a
private database it builds from files, leaves no session
state, and has no path back into the session. The four
per-cell edit commands (`size_cell`, `swap_cell`,
`insert_buffer`, `remove_buffer`) are the manual half of
the surface; they keep their own posture (no record, no
navigation) and are untouched by this unit.

## The SDC pin map reaches the closure

Since PR 146, the timing service's database analyses
accept an `SdcPinMap`: an SDC may name pins
hierarchically (`[get_pins u_div/q]`) that the flat
bridged database carries under bridged names, and the
mapped analysis translates the SDC before reading it.
The bridge records the map at `write_def`
(`ctx.timing.sdc_pin_map`), and `run_timing` and the
stage commands have passed it since PR 146. The closure
path never got it: `close_timing` on a bridged design
failed at its first analysis, on the SDC read.

This unit extends the same shape to the closure:
`session::CloseTimingRequest` and `eco::EcoRequest` each
carry an `SdcPinMap sdc_pin_map` field (empty by
default), the closure's initial analysis and the
transaction protocol's before- and after-analyses call
the mapped overload, and the `close_timing` handler
passes the session's recorded map. With an empty map the
mapped overload is byte-identical to the unmapped one,
so every existing caller behaves exactly as before;
sessions that hold a map gain a closure that can read
their SDC.

## The ECO session record

On every completed run, closed or not, the handler
stores the run's outcome in a session record
(`ctx.eco`), field for field with the
`session::ClosureOutcome`'s reportable facts: the
closed flag, the four candidate counters (tried,
rejected, skipped stale, skipped memory), the accepted
steps' labels in run order, the stop reason, the
parasitics note, the iteration bound the run was given,
and per-scene initial and final metrics (worst and
total-negative setup and hold slacks). The iteration
count is not stored: it is the step count. A per-scene
metric the analysis did not produce records as absent,
never as a fabricated zero.

The record's posture is the stage family's: a single
run, replaced wholesale by the next completed run; a
failed `close_timing` (bad bound, no design, no SDC, no
scenarios, the extracted-scenario refusal, an engine
failure) returns before an outcome exists and leaves
the record untouched. It is interface session state,
not project state: cleared when the design closes
(ResetProject) and by a restore, and never persisted in
the project file. An ECO run clears no other stage's
record, and no later stage's run clears the ECO record;
signoff and DRC verdict staleness after an accepted edit
stays with the design fingerprint, as it does after a
per-cell edit.

## `eco_summary`

```text
eco_summary
```

A record query in the sibling summaries' shape: it runs
no engine, writes no file, and changes no session state.
With an empty record it fails fast with `no timing
closure: eos::close_timing first`. Its grammar is total:
every completed run produces exactly this shape.

```text
eco closed=<0|1> iterations=<n> candidates_tried=<n> candidates_rejected=<n> candidates_skipped_stale=<n> candidates_skipped_memory=<n> max_iterations=<n>
scene name=<scene> initial_worst_slack_ns=<value|none> initial_worst_hold_slack_ns=<value|none> worst_slack_ns=<value|none> total_negative_slack_ns=<value|none> worst_hold_slack_ns=<value|none> total_negative_hold_slack_ns=<value|none>
step <accepted edit label, verbatim>
stop_reason <text, verbatim>
note <text, verbatim>
```

One header line; one scene line per scene the run
analyzed, in the run's order; one step line per accepted
iteration, in run order (absent when no edit was
accepted); then the stop reason and the parasitics note,
each verbatim on its own line.

## The GUI surface

The Timing tab gains an ECO (timing closure) section
after the Extraction section, fed by a Qt-free
`EcoModel` the session view refreshes from `eco_summary`
on every refresh, in the extract model's shape
(all-or-nothing parse; a failed query or malformed
payload keeps the last good model). The section renders
the verdict (closed or not, iterations against their
bound, the candidate counters), one line per scene with
its worst setup and hold slacks before and after, the
accepted edits by label (or "no edits were accepted"),
and the stop reason. The parasitics note is carried by
the record and the query but not rendered: it is a
constant statement about the closure's fidelity, not a
per-run fact.

The stage bar's ECO entry becomes a button in the
Extract branch's shape: disabled until an ECO run is
recorded, and its click shows the Timing tab. The
stepper gains a twelfth step, ECO, after GDS export;
its click replicates the ECO stage button exactly. The
step is state order, not script order: the demo runs
its ECO commands mid-flow (below), and the step reads
Done once a `close_timing` run has completed in the
session, whenever in the flow that was. A console
`close_timing` auto-navigates to the Timing tab.
`repair_timing` and the per-cell edit commands set no
record and flip no step: neither is the stage's recorded
run.

## The demo posture

In `demo/clocked/flow-demo.tcl`, `close_timing 5` and
`eco_summary` sit between the post-route `run_timing`
and `extract_parasitics`: the first point where the
routed database, the recorded pin map, and an
estimate-only scenario set all hold, and the last point
before the extracted scenario makes the closure refuse.
Placing ECO before extraction keeps every downstream
artifact (the SPEF, the extracted comparison, the
finish, the export) describing the design the ECO left.
On the clocked demo the gate is already met: the run
reports `closed=1 iterations=0`, worst slack
3.292352835 ns and worst hold slack 0.1642400105 ns,
initial equal to final, and accepts no edit. The
edit-accepting case is proven in the registry suite on
the closure fixture, where a -0.1 ns slack closes to
+0.5 ns by one accepted sizing edit the record names;
no honest mechanism stages a violation on the demo
(its setup margin is 3.29 ns of a 5 ns clock, and the
closure has no margin option).
