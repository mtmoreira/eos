# `report_constraint` in the EOS registry (Immediate unit 6)

`eos::report_constraint` is the verdict-shaped constraint
report, review item I3, worst-violator tier: the first thing a
signoff review asks for after the clocks are known, and EOS
previously could only show that a constraint record existed
(`list_constraints`), never whether the design met it. It
reports, per constraint class, the worst violator, its slack,
the cost, the weight and the violating-pin count, plus the
critical-range table of every violating endpoint or pin per
class, worst first. The spelling is the singular DC form,
chosen deliberately over the ICC2 plural
`report_constraints`; see the review's correction 13.

```text
report_constraint [-verbose] [-all_violators] [-scene <name>] [-file <path>]
```

It runs the timing analysis itself for the selected scenes:
it does not reuse a previous `run_timing`, because the session
keeps no live engine. It needs an active scenario, a Liberty
for the scenario's corner, a design source
(`read_verilog` or `import_odb`) and an SDC file, exactly as
`run_timing` and `report_timing` do. One report binding per
scene is registered through `session/reports`, named
`constraint_report_<scene>`.

## The summary table

One row per constraint class, in this order:

| Column | Meaning |
|---|---|
| `Constraint` | The class: `max_delay` (setup), `min_delay` (hold), `max_transition`, `max_capacitance`, `max_fanout`. |
| `Worst Violator` | The worst violating endpoint (timing classes) or pin (DRC classes), or `-` when nothing violates. |
| `Slack` | The worst slack in the class's unit, or `-` when the class has no constrained endpoints (timing) or no limit at all (DRC). |
| `Cost` | The exact sum of the violations: `-TNS` for the timing classes, the sum of `-slack` over the violating pins for the DRC classes. A DRC cost is never a worst magnitude wearing a cost label. `0` when nothing violates. |
| `Weight` | `1` for every class. EOS has no constraint-weight command yet; the gap is named in the report footer instead of hidden. |
| `Violations` | The violating-endpoint or violating-pin count. |

Units: slack and cost are in ns for `max_delay`,
`min_delay` and `max_transition`, in pF for
`max_capacitance`, and in fanout load for `max_fanout`.

Example, a design with a setup violation and no electrical
limits:

```text
Constraint        Worst Violator  Slack  Cost  Weight  Violations
----------------  --------------  -----  ----  ------  ----------
max_delay         r2/D            -0.1   0.1   1       1
min_delay         -               0.45   0     1       0
max_transition    -               -      -     1       0
max_capacitance   -               -      -     1       0
max_fanout        -               -      -     1       0
----------------
Units: slack and cost in ns for max_delay, min_delay and
max_transition; in pF for max_capacitance; in fanout load for
max_fanout. Weight is 1 for every class: EOS has no
constraint-weight command yet.
max_transition: no limit defined for this design.
max_capacitance: no limit defined for this design.
max_fanout: no limit defined for this design.
```

## The critical-range tables

One section per class with violations. The critical range is
slack below 0: every violating endpoint or pin, worst first.
EOS has no `set_critical_range` command yet, so the range is
the violating set itself, and the report says so by listing
only violators. A class with no violations has no section.

Timing classes list endpoint and slack:

```text
Critical range (max_delay): endpoints with slack below 0, worst first:
Endpoint  Slack (ns)
--------  ----------
r2/D      -0.1
--------
```

DRC classes list pin and slack by default. `-verbose`
expands the rows to pin, value, limit and slack, in the
check's unit:

```text
Critical range (max_transition): pins with slack below 0, worst first:
Pin   Value (ns)  Limit (ns)  Slack (ns)
----  ----------  ----------  ----------
u1/Y  0.05        0.04        -0.01
----
```

## `-verbose`

`-verbose` adds, for each timing class with a violator, the
full worst path to that endpoint (the same rendering as
`report_timing` with `-path_type full`), and expands the DRC
critical-range rows to pin, value, limit and slack as above.
The paths are computed by one extra timing run per violated
timing class per scene, so `-verbose` costs up to two extra
engine sessions per scene.

## The DRC classes

The DRC classes (`max_transition`, `max_capacitance`,
`max_fanout`) are evaluated from the timing engine's
electrical checks (unit F5), not from EOS constraint rows:
there is no `max_transition`, `max_fanout` or
`max_capacitance` constraint kind, and no SDC command in EOS
sets such a limit. The limits come from the Liberty library
(the cell and pin `max_transition`, `max_capacitance` and
`max_fanout` attributes). A class with no limit on any pin
reports `limit=none` in the payload and a "no limit defined
for this design" line in the report, never a clean worst
value: "no limit" and "clean" are different verdicts.

The DRC cost is the exact sum of the violations, which
needs the full violator list per check. The timing engine
exposes it through the facade's violator enumeration, which
replicates the upstream bulk checks pin by pin through the
public per-pin API (the bulk check objects themselves are
private members of the upstream analyzer). The worst
violator of each check is the list head, and the list size
is the check's violation count.

## `-all_violators` (tier 2, not built)

`-all_violators` is the tier 2 form: the full violator list
with required and actual path delays per violator. It waits
on the I8 path enumeration (several paths to one endpoint),
so passing it fails fast with a message naming tier 2,
instead of silently reporting the tier 1 form. Tier 1 and
tier 2 differ only in that per-violator detail: tier 1
already lists every violating endpoint and pin in the
critical-range tables; what tier 2 adds is the required and
actual path delay of each violator.

## The payload

The payload is machine-readable key/value lines, then the
rendered report text. Per scene: `scene=<name>
violations=<total> worst_slack_ns=<worst setup slack or
none> report=constraint_report_<name>
artifact=<id>`, plus one line per class:
`scene=<name> class=<class> violator=<pin or -> slack=<slack
or none> cost=<cost> weight=1 violations=<count>`, or
`limit=none` instead of `slack=` and `cost=` for a DRC class
with no limit. `-file` writes the rendered text to the file
and adds a `file=<path>` line.

## Boundary

- `report_constraint` reports the five classes above. It
  does not report `max_delay`/`min_delay` constraint rows
  set with `set_max_delay`/`set_min_delay` as separate rows:
  those commands do not exist in EOS yet, and the timing
  classes are the engine's setup and hold analyses. When
  those commands land, the classes stay the same shape.
- It does not fix violations: DRC fixing is ECO, the next
  phase after the Immediate sequence.
- It does not include the full violator list with required
  and actual path delays: that is `-all_violators`, tier 2,
  waiting on I8.
- It does not include a clock-network summary, skew, or
  `report_clock_timing`: those are separate items, listed
  under clock-network reporting in the review.
- Unknown options and positional arguments fail fast.
  `-verbose` or `-all_violators` given twice fails fast.
  An unknown `-scene` fails fast.
- Registry count pin: 74 to 75.
