# `extract_parasitics`, the extract session record, and `extract_summary` (full-flow GUI unit F7)

`eos::extract_parasitics` runs the in-tree OpenRCX
extractor on the session's routed database (the P6
runner, `pv::ExtractParasitics`), annotates every
extracted net in place with RC segments and capacitance
nodes, and writes a SPEF file:

```text
extract_parasitics <spef_path> [-model <path>]
```

The model file is required, not defaulted: without a
rules file the extractor logs a warning and extracts
nothing, and EOS ships no default model because a
default would silently stand in for the process data
the numbers depend on. A missing `-model` fails with
`no extraction model file: pass -model <rules file>`.
The command requires the session database with
technology and a top block; the runner then fails fast,
each time naming the missing stage and the command that
supplies it: no rows (not floorplanned), no placed
instance (not placed), no signal net carrying a wire
(not routed; guides alone do not count), an unreadable
model file, a run that leaves zero RC segments or zero
capacitance nodes (the model may not cover the routed
layers), and a SPEF file that was not written or is
empty.

The payload is one line plus the run's estimated STA
leg:

```text
nets=<n> rsegs=<n> cap_nodes=<n> cc_segs=<n> spef=<path> scene=<name> worst_slack_ns=<value>
```

`nets` is the count of nets the extractor annotated,
`rsegs` the RC segment count, `cap_nodes` the
capacitance node count, and `cc_segs` the coupling
segment count. The timing leg is the estimated-
parasitics analysis of the routed database (the same
path `route` reports), because the extract command's
own leg is estimated by design; the extracted
comparison is demonstrated by the extracted scenario
below, not faked into this payload. When the session is
not configured for timing on the database, the payload
carries `sta_slack_ns=unavailable (<reason>)` instead,
and extraction itself still succeeds.

For the clocked demo the model is the mock PDK's rules
file, `src/pd/rsz/tests/mock/eos_mock.rules`, generated
by the mock's own generator (`gen_mock.py` `emit_rules()`)
with invented fixture values documented as invented in
the mock README: a mock extraction validates mechanics,
never a signoff claim.

## Wire preservation

The extractor's pre-pass (`odb::orderWires`) re-encodes
every routed net's wire stream in place as a connectivity
traversal tree before the RC builder reads it: segments
off the chosen tree are dropped, layers change, and via
stubs can be left geometrically disconnected. That
reordered encoding is the extractor's input, not a
product of the run. The runner therefore snapshots each
affected net's wire stream before the extractor runs and
restores it after the SPEF is written, so the session
database keeps the routed wires the router wrote and
downstream geometry consumers (finish, the GDS writer,
the DRC and LVS walks) never see the traversal trees. On
success the only change the command leaves in the
database is the RC annotation; on failure the wire
geometry and the nets' wire-ordered/disconnected flags
are restored to their pre-run state as well. One named
exception: a processed net's special wires are not
preserved (odb offers no verbatim SWire copy; their
destruction inside the ordering pass is pre-existing
engine behavior, unchanged by the runner).

## The extract session record

On success only, the run is stored in a session record
(`ctx.extract`), mirroring the route record: a single
run, replaced by the latest successful run. A failed
run leaves the record untouched. The record carries the
engine's counts, the SPEF path, the model path, and the
run's estimated STA outcome (per-scene slacks, or the
payload's unavailable statement verbatim when the leg
could not analyze). It is interface session state, not
project state: it is cleared when the design closes
(ResetProject), and a restore does not bring it back.
The SPEF file itself is the durable product, named by
path in the record.

Unlike a first extraction, a re-run is a real
re-extraction: the runner replaces the first run's RC
data and rewrites the SPEF, so the second run succeeds
with the same counts and the record holds the second
run's values.

The extracted-parasitics comparison is not stored in
the record: `extract_summary` computes it from the
timing session's `last_results` at query time, so the
record never holds a second copy of a `run_timing`
result.

## `extract_summary`

```text
extract_summary
```

The structured query the GUI reads: the recorded run
plus the comparison. It is a query, not a report: it
runs no engine, writes no file, and changes no session
state. It takes no options. Like `cts_summary`, it
reads only the record, so it does not require the
database; with an empty record it fails fast:

```text
EOS invalid_configuration: no extraction: eos::extract_parasitics first
```

Payload shape:

```text
extract nets=<n> rsegs=<n> cap_nodes=<n> cc_segs=<n> spef=<path> model=<path>
slack scene=<name> worst_slack_ns=<value>
compare mode=<mode> corner=<corner> estimate_scene=<name> extracted_scene=<name> estimated_worst_slack_ns=<value> extracted_worst_slack_ns=<value> estimated_worst_hold_slack_ns=<value> extracted_worst_hold_slack_ns=<value>
```

The header carries the record's counts and both paths,
in the extract payload's own key order plus `model=`
(the model is part of the run's identity: two runs with
different models are different measurements). Then the
record's STA outcome: one `slack` line per scene, or
one `sta_note` line carrying the unavailable statement
verbatim; a record carries one or the other, never
both. Then one `compare` line per (mode, corner) pair
that the most recent `run_timing` holds at both
parasitics levels, in the order the estimate scenes
appear: two scenes pair when they share mode and corner
and differ in parasitics level. When `last_results`
holds no complete pair (the extracted run has not
happened yet), there are no compare lines: the absence
is the truthful state, and the panel renders the
not-run-yet sentence.

Measured on the clocked demo (185 routed nets): 2,486
resistance segments, 2,486 capacitance nodes, 1,786
coupling segments; estimated worst slack 3.292352835 ns
against extracted 3.134516868 ns, hold 0.1642400105 ns
against 0.1716079501 ns, and zero SPEF not-found
warnings in the run log (the reader resolved every name
the writer emitted). The demo README records the full
sequence.

## The GUI surface

The Timing tab's Extraction section is the extract
surface: `RenderTiming` plus `RenderExtract`, the shape
the Clocks tab uses for CTS. Extraction has no
geometry, so there is no center canvas, and the
comparison belongs under the per-scene timing lines it
pairs, so there is no new tab. An `extract_parasitics`
typed in the console navigates to the Timing tab on
success. The stage bar's Extract button is enabled once
an extract record is loaded and shows the Timing tab
when clicked. The section names the run:

```text
Extraction (post-route):
  nets <n>, RSEGs <n>, capacitance nodes <n>, coupling segments <n>
  SPEF: <path>
  model: <path>
  post-route estimated worst slack (<scene>): <x> ns
  <mode>/<corner>: estimated <a> ns (scene <estimate>) vs extracted <b> ns (scene <extracted>); hold <c> ns vs <d> ns
```

Before the extracted run, the last line reads instead:
`extracted comparison: not run yet (create the extracted
scenario, set_spef, and run_timing)`.

`ExtractStep()` is the step contract F11's unified
stepper wires: Done once an extract run has succeeded
in this session, read straight from the record.

## What changes after extraction

Extraction annotates the database in place and moves
no instance, so the placed, CTS, and routed state the
earlier summaries report is unchanged, and the design
fingerprint's placement part does not change. Naming
the SPEF with `set_spef` does feed the fingerprint, so
signoff and DRC verdicts recorded before the extracted
run read stale afterwards, which is the correct
posture.

The post-route STA flow is same-session: the SDC pin
map the analysis needs lives only in the session that
bridged the design, so the extracted scenario is a
second scenario in that session (`create_scenario
<name> <mode> <corner> extracted`), created after
extraction (an extracted scenario with no SPEF set
fails scene building), with `set_spef` naming the
just-written SPEF; one `run_timing` then analyzes the
estimate and extracted scenes side by side, and the
final `extract_summary` carries the compare line.

Extracted STA is a comparison, not a closure: the unit
shows the estimated and extracted numbers side by side
and their agreement across surfaces; it does not
iterate the design toward either number.
