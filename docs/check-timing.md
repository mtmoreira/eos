# `check_timing` in the EOS registry (Immediate unit 4)

`eos::check_timing` is the constraint-completeness gate, review
item I1: the check the guides put before trusting any violation
report. It runs over the session constraint set and the session
design, the constraints the user authored with `eos::` commands.
It needs an open design and nothing else: no Liberty, no SDC
file, and no STA run, so the gate can run before `run_timing`,
where the guides put it.

```text
check_timing [-include <check>]... [-exclude <check>]... [-verbose] [-summary]
```

It is the EOS replacement for the internal OpenSTA `CheckTiming`,
not a duplicate of it. OpenSTA's `CheckTiming` stays internal and
unexposed, and this command never calls it. The two check
different things: OpenSTA's checks the SDC file under a full STA
session, while this one checks the session constraints.

## Checks

The default set runs unless `-exclude` removes a check.
`-include` adds optional checks. Check names are the PrimeTime
names from the plan.

| Check | Set | What it reports |
|---|---|---|
| `generated_clocks` | default | Generated clocks whose master clock names no bound SDC clock row, whose source names no design object, or whose target never bound. |
| `loops` | default | Not run: the design model carries no pin directions, so combinational cycles cannot be traced. |
| `no_driving_cell` | default | Not run: the session model has no driving-cell constraint. |
| `no_input_delay` | default | Input and inout top ports, excluding clock ports, with no bound input-delay constraint. |
| `no_output_delay` | default | Output and inout top ports with no bound output-delay constraint. |
| `partial_input_delay` | default | Not run: input-delay rows carry no clock association. |
| `pulse_clock_cell_type` | default | Not run: the session model has no clock cell types. |
| `unconstrained_endpoints` | default | Output and inout top ports with no bound output delay, max delay, or multicycle path. A false path counts as a deliberate exemption, not a missing requirement. |
| `case_analysis` | optional | Not run: case-analysis rows carry no value. |
| `clock_crossing` | optional | Not run: the design model has no clock domains. |
| `gated_clock` | optional | Not run: the design model has no clock-gating structure. |
| `multiple_clock` | optional | Not run: the design model has no sequential elements. |
| `multicycle_path` | optional | Multicycle-path rows that never bound. Multicycle rows carry no setup or hold values in the session model, so an unbound target is the only checkable finding. |

Rules shared by the runnable checks:

- Only bound rows cover. An unresolved row names an object
  that does not exist, so it covers nothing. `check_timing`
  reads stored binding status and never mutates session state,
  so run `rebind_constraints` first when the design changed
  after the constraints were authored.
- A clock port is the target of a bound clock or generated-clock
  row. Clock ports are excluded from `no_input_delay`: they need
  no input delay.
- A generated clock's master must be a real SDC clock row
  (a `period_ns` parameter, bound). A construction record
  (`bind_clock`) names a clock net but carries no period, so it
  cannot be a master. A combinational generated clock needs no
  master.
- Inout ports are checked by both `no_input_delay` and
  `no_output_delay`, following OpenSTA's input/output
  direction rules.

## Verdict

Each check reports one status, and the summary never hides
which one it is: `pass` (ran, nothing found), `fail` (ran,
findings listed), `not_run` (the session model cannot express
it, with the reason named). The verdict is:

- `pass`: every selected check ran and passed.
- `fail`: at least one selected check failed.
- `incomplete`: no check failed, but at least one selected
  check did not run.

A clean pass is impossible while any selected check cannot run
(review item H1: a clean verdict on an unchecked constraint set
is forbidden). The `incomplete` verdict is the forcing function
that makes the gaps visible: the next units close them by
extending the session model (pin directions for `loops`,
a driving-cell constraint for `no_driving_cell`, clock
association for input delays, clock cell types, sequential
structure for the optional clock checks).

The command returns Ok with the verdict in the payload in all
three cases, like `report_timing` returns Ok with negative
slack: the verdict is the gate, and a script that must stop on
a failed gate tests `verdict=`. Findings never make the command
fail, so the per-check payload stays available to scripts.

Waivers (waiver-model unit, see `docs/waiver-model.md`): a
finding a check waiver covers is still listed, marked with
its waiver name, and no longer fails the gate. Per-check
`status=` stays raw (fail iff findings non-empty) and
`findings=` stays the total count; each `check=` line gains
`waived=<n>`, each `-verbose` finding gains
`waived=<name or ->`, and the verdict line gains
`waived=<n>`. The verdict is `pass` with the waived count
named when every finding is waived, `fail` when any finding
is unwaived. There is no fourth verdict: a script that
accepts only `pass` sees `pass` with the waived count named.

## Output

The payload is the summary form: `verdict=` first, then one
`check=` line per selected check in table order, each with its
status, finding count, and the not-run reason when it applies.
`-verbose` adds one `finding=` line per finding, in sorted
order. `-summary` selects the summary form, which is also the
default, and conflicts with `-verbose`.

Example, a design with one uncovered input and one uncovered
output:

```text
verdict=fail
check=generated_clocks status=pass findings=0
check=loops status=not_run findings=0 reason=the design model carries no pin directions, so combinational cycles cannot be traced
check=no_driving_cell status=not_run findings=0 reason=the session model has no driving-cell constraint
check=no_input_delay status=fail findings=1
check=partial_input_delay status=not_run findings=0 reason=input-delay rows carry no clock association
check=pulse_clock_cell_type status=not_run findings=0 reason=the session model has no clock cell types
check=unconstrained_endpoints status=fail findings=1
```

With `-verbose`, the two failing checks also report:

```text
finding=no_input_delay b missing bound input_delay
finding=unconstrained_endpoints y no covering constraint
```

## Boundary

- `check_timing` does not include `check_constraints` rule
  checking with waivers, or `check_design`. Those are separate
  items, not part of I1.
- Unknown check names fail fast, naming the known checks.
  Excluding a check that is not in the run fails fast.
  Excluding every check fails fast (`no checks selected`).
- Registry count pin: 70 to 71.
