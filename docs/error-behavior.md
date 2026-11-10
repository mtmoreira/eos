# Error behavior in the EOS registry (Immediate unit: error-behavior)

Every command failure carries one diagnostic code and one message. The
code names the failure kind; the message names the offending object, the
bad value, or the missing input, with the command that supplies it. A
script can match on the code; a user can read the message and know what
to do next. Nothing fails silently, and nothing fails for two reasons at
once.

## The taxonomy

`Diag` is the registry's diagnostic code. The Tcl shell renders a failure
as `EOS <diag>: <message>`, where `<diag>` is the DiagName string below.

| Diag | Prefix string | Meaning |
|---|---|---|
| `kOk` | `ok` | Success. Never a failure code. |
| `kInvalidArgument` | `invalid_argument` | A bad argument: wrong count, unknown option, duplicate or missing option value, bad number or enum, conflicting options, or an argument of the wrong kind. |
| `kNotFound` | `not_found` | A named object that does not exist: cell, instance, net, clock, constraint kind, object kind, artifact, scenario, scene, endpoint, group, check, collection (outside the collections family), top module, generic script. |
| `kInvalidState` | `invalid_state` | The session is in the wrong state for the call: a design is already open, a design is already loaded, a transaction is already pending, nothing is pending to commit or abort, the timing session has no scenario. |
| `kStaleRevision` | `stale_revision` | A revision that is not the current one: a transaction whose input revision moved, a rollback target that no longer exists. |
| `kOutOfRange` | `out_of_range` | An index or count outside its range. Reserved by the taxonomy; the collections family keeps its own out-of-range code today (see below). |
| `kInvalidConfiguration` | `invalid_configuration` | The session lacks something a command needs, or an input is malformed: no parsed, elaborated, or mapped design; no Liberty, SDC, SPEF, technology data, placement, or timing results; an unparseable signoff report; a tier-2 or not-yet-implemented form. |
| `kUnclosedDesign` | `unclosed_design` | No design is open, or the imported database holds no top block. |
| `kInvalidCommand` | `invalid_command` | An unknown command name, or a help pattern matching nothing. |
| `kUnsupportedFeature` | `unsupported_feature` | The build cannot do this: no Tcl, no engine for the request. |
| `kIoError` | `io_error` | A file or device failure: unreadable input, unwritable output, a failed engine invocation that reports through the OS. |
| `kProofUnknown` | `proof_unknown` | A proof that did not pass. `check_equivalence` keeps this code for failing proofs, and the message carries the exact verdict (`equivalence proof: not_equivalent for <first> vs <second>`). The session records the verdict either way. |
| `kVerifiedPass` | `verified_pass` | A proof or closure that passed. A success code, carried on an Ok result, never a failure. |
| `kToolCrash` | `tool_crash` | Reserved. No command fabricates it: the Yosys log_error process abort is unreportable by construction. |

The L0 status codes map 1:1 onto the taxonomy through `FromStatus`:
`kInvalidArgument`, `kNotFound`, `kInvalidState`, `kStaleRevision`,
`kOutOfRange`, `kInvalidConfiguration`, `kIoError`, and `kUnsupported`
(to `kUnsupportedFeature`). Engine and ledger failures therefore surface
with the code their producer chose; the registry never collapses them
into `kInvalidConfiguration`. Hand-written `CommandResult::Fail` calls
follow the same table.

## Rules R1-R12

- R1 Fail fast before work. A command validates its arguments and its
  preconditions before it touches an engine, a file, or session state.
- R2 One failure, one reason. A failure names exactly one cause.
- R3 Name the offending object with the bad value quoted (`got:
  <value>`).
- R4 Name the missing input and the command that supplies it (`no
  Liberty for corner tt (scene func_tt): eos::read_liberty first`).
- R5 Nothing is silently dropped. A command that consumes engine
  results names what it could not use, never silently skips it. This
  applies to engine output too: a failing engine command prints the
  drained engine log in the Tcl shell (see below).
- R6 Verdicts are payloads, never failures. A check that ran and found
  problems returns Ok with `verdict=fail`; only a check that could not
  run fails.
- R7 Empty results are empty payloads, never errors. A query that
  matches nothing returns an empty collection or `count=0`.
- R8 The diag code matches the failure kind (see the table above).
- R9 A failure that clears session state names the recovery in the
  message (`the synth session was cleared, re-run eos::read_verilog
  and eos::elaborate`).
- R10 Engine failures name the command and the input (`elaborate:
  Yosys failed: ...`, `synth_generic: optimized netlist has no top
  module: <top>`).
- R11 Messages are stable text, and tests pin them. A message change is
  a deliberate change, listed in the PR body.
- R12 Usage failures name the command (`usage: eos::<command> ...`).

## Per-family tables

### Usage and option validation

Every command validates its arguments with `ExpectCount` (wrong count),
`SdcOptions::Parse` (unknown, duplicate, or missing-value options), or
its own option loop, and fails with `kInvalidArgument`. The message
names the command and the offending option or value. `help` with more
than one argument fails with `kInvalidArgument` and the usage line.
Dispatch on an unknown name fails with `kInvalidCommand` and
`unknown command: eos::<name>`; `help` on an unknown name fails with
`kInvalidCommand` and `unknown command: <arg>`; `help` on a pattern
matching nothing fails with `kInvalidCommand` and
`no commands match: <pattern>`.

### Missing objects

A name that resolves to nothing fails with `kNotFound` and names the
value: unknown cell, instance, net, constraint kind, object kind,
artifact, scenario, scene, endpoint, group, check, top module, generic
script. `get_timing_paths` unknown group, scene, endpoint, `-from`, and
`-through` values fail with `kNotFound`; a `-from`/`-through` value of
the wrong kind (a clock under `-through`, a path collection as a
selector) fails with `kInvalidArgument`, because the object exists but
cannot be used there. `check_timing` unknown check and `-exclude`
naming a check not in the run fail with `kNotFound`.

### Missing inputs and state

A command that cannot run because session state is missing fails with
`kInvalidConfiguration` and names the supplying command: `no parsed
design: eos::read_verilog first`, `no elaborated design:
eos::elaborate first`, `no mapped design: eos::synth_map first`,
`no Liberty for mapping: eos::synth_map -liberty <path> or
eos::read_liberty first`, `no technology data loaded:
eos::read_lef first`, `no placement: eos::place first`, `no timing
results: run eos::run_timing first`. `BuildTimingScenes` failures name
the supplying command the same way: `no active scenarios (see
eos::create_scenario)`, `no SDC: eos::read_sdc first`, `no Liberty for
corner <corner> (scene <name>): eos::read_liberty first`, `scene <name>
is at extracted parasitics but no SPEF is set: eos::set_spef first`.
A design that is already open, or already loaded, fails with
`kInvalidState`, not `kInvalidConfiguration`.

### Collections

The collections family keeps its own codes: `unknown collection`,
filter and attribute errors, `index_collection` out of range, and
`<deleted>` members all fail with `kInvalidConfiguration`, and the
collections tests pin them unchanged. The one exception is
`get_objects` with an unknown object kind, which fails with
`kNotFound` like every other unknown-kind error. An SDC command given
an unknown collection as a target, and `report_bottleneck -paths`
given an unknown collection, fail with `kNotFound`: those are argument
failures in a non-collections command, not collections failures.

### Ledger and checkpoint

Ledger failures surface through `FromStatus` with the code the ledger
chose: begin while a transaction is pending fails with
`kInvalidState` (`a transaction is already pending`); begin with a
stale input revision fails with `kStaleRevision`; begin invalidating
an unknown artifact fails with `kNotFound`; commit or abort with
nothing pending fails with `kInvalidState` (`no transaction is
pending`). `rollback` with a non-numeric revision fails with
`kInvalidArgument` (`not a revision: <arg>`). `restore` on a corrupt
project fails naming the corruption, with no live state changes.

### Timing reports

`check_timing` with findings returns Ok with `verdict=fail`; findings
never fail the command. `report_timing -to` with `-max_paths` above 1
still fails fast: per-endpoint enumeration lives in
`get_timing_paths`, and the gap is not silently fixed here.
`report_constraint -all_violators` still fails fast with the tier-2
message. `timing_summary` with no timing results fails with
`kInvalidConfiguration`; a `-scene` naming a scene with no results
fails the same way (the scene exists, the results do not).

### Synthesis recovery (R9, R10)

`elaborate` and `synth_generic` clear the synth session when the live
design may have been mutated, and the failure message says so:

```text
elaborate: <engine message>; the synth session was cleared, re-run eos::read_verilog and eos::elaborate
synth_generic: <engine message>; the synth session was cleared, re-run eos::read_verilog and eos::elaborate
```

Engine failures before any mutation (the `synth_generic` design-totals
and issues pre-gate) carry the `synth_generic: ` prefix only, with no
recovery suffix, because the session was not cleared. The
`optimized netlist has no top module: <top>` failure goes through the
after-mutation path, so it carries the prefix and the recovery suffix.
The `elaboration issue: unresolved_reference` pre-gate failure is
unchanged (`kInvalidConfiguration`).

### The Tcl shell

A failing command returns `TCL_ERROR` with the result
`EOS <diag>: <message>`, so scripts can catch it and still see the
diagnostic taxonomy. `TclShell::Eval` returns the command's Diag, not
a constant code: the five new codes plus `kIoError`,
`kUnsupportedFeature`, and `kInvalidConfiguration` map 1:1 onto the
L0 codes; `kUnclosedDesign` maps to `kInvalidState`; and the legacy
codes with no L0 equivalent (`kInvalidCommand`, `kProofUnknown`,
`kToolCrash`, `kVerifiedPass`) fall back to `kInvalidArgument`. A
plain Tcl error (for example `invalid command name`) keeps
`kInvalidArgument`. The interactive loop keeps its
`error: EOS ...` double prefix; scripts and users already match on
the `EOS` tail.

On failure the shell prints the drained engine log to stderr after the
error line: the lines the failed command appended to the engine log
(one `Eval`, or one script file, of lines). Nothing prints on success.
`RunFiles` prints `<file>: EOS ...` to stderr and exits 1 without
running later files, then the drained lines for that file. The GUI
terminal REPL appends the same lines to its error output, before the
window refresh drains them into the Log window.

### The GUI console and refresh

A failing console command shows the identical error line in the
transcript, the Messages tab, and the Log window. A pasted blob with a
failing middle segment runs the later segments, and the aggregate is
the first failure. A failed dispatch navigates nowhere.

A failed panel refresh keeps the last good model and logs the error to
the Messages tab; it never clears the panel. The one exception is a
dispatch failure on an empty model, which is the not-run-yet state
(for example `floorplan_summary` before `floorplan` ran, which the
refresh calls on every cycle): that stays silent. A malformed payload
always logs, because the query ran and the state is not "not run".
Guarded refreshes (hierarchy, diagram, schematic, issues, timing, and
the panels live stats) log always after their stage guard passes. The
panels mapped synthesis record follows the empty-model rule instead:
`synth_summary` fails fast until `synth_map` ran, which is the
not-run-yet state. The key-guarded refreshes (issues, timing) try a
failed refresh once per design state, so they do not spam Messages on
every refresh cycle. The unguarded refreshes (floorplan, placement,
signoff) re-run and re-log on every refresh cycle by design: a
persistent failure logs once per cycle until it clears.

`close_design` and `restore` reset the design, so the dispatch clears
the floorplan and placement models outright: they are the ODB-backed
models with no guard, and without the clear a close would leave a
stale floorplan behind while every later refresh logged the same
"no technology data" error. The signoff model does not clear on close:
it renders the recorded verdicts with no design in the session.

### Waiver commands

The waiver commands fail fast with `kInvalidArgument` for a bad
argument and `kNotFound` for a missing object: unknown kind, class, or
scene; a duplicate name; an empty reason or approver; a malformed
expiry; an unknown name on `remove_waiver`, naming the existing names.
An empty set reports `waivers=0`. `check_timing` with every finding
waived returns `verdict=pass` with the waived count named; there is no
fourth verdict.

### The imported-database top block

Nine PD commands share one guard: the imported database holds no top
block. They share one helper (`NeedTopBlock`, mirroring `NeedDesign`)
with one wording, `no mapped design: the imported database holds no
top block`, and one code, `kUnclosedDesign`.

## Known gaps (D5, not fixed here)

These are silent-ignore traps in the SDC layer, recorded so the SDC
audit unit can fix them with its own tests:

- `set_sense` without `-type` silently does nothing.
- `set_ideal_latency` is parsed but ignored.
- `create_clock` silently does nothing for timing.
- A `read_sdc`-only session shows an empty clock table (the F2
  caveat, assigned to the clock-network unit).

## Tests

The taxonomy is pinned at three levels: the codes themselves
(`FromStatus` per L0 code, `DiagName` exact strings, the `EOS <diag>:
<message>` prefix for every code), the message rules (NeedDesign,
BuildTimingScenes supplying commands, `SdcOptions::Parse` errors,
ExpectCount usage lines, the synth recovery wording and cleared
session, the proof verdict in the message, `check_timing` findings as
a payload, `write_sdc` skipped rows, corrupt restore, non-numeric
rollback, ledger codes, waiver-command failures), and the interfaces
(Tcl `TCL_ERROR` with the EOS result, `RunFiles` exit behavior, Eval
returning the command's Diag, the identical console error line, the
pasted-blob aggregate, no navigation on failed dispatch, refresh
keeping the last good model). The collections tests pass unchanged.
