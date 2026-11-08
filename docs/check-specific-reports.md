# Check-specific reports in the EOS registry (Immediate unit 10)

`eos::report_global_timing`, `eos::report_global_slack`,
`eos::report_min_period`, `eos::report_min_pulse_width` and
`eos::report_clock_gating_check` are the check-specific reports,
review item T8: the five PrimeTime chapter 19 reports (period,
pulse width, gating, per-pin slack) as EOS commands. They are
FROM-SCRATCH over EOS timing data: every value is measured by
the engine under the SDC file the session read, in one engine
session per scene, exactly as `report_qor` runs its analysis.
A check the engine did not evaluate is reported as not
evaluated, never as a pass.

```text
report_global_timing [-delay_type max|min] [-scene <name>] [-file <path>]
report_global_slack [-delay_type max|min] [-slack_lesser_than <ns>] [-scene <name>] [-file <path>]
report_min_period [-verbose] [-scene <name>] [-file <path>]
report_min_pulse_width [-violators] [-scene <name>] [-file <path>]
report_clock_gating_check [-scene <name>] [-file <path>]
```

All five need an active scenario, a Liberty for the scenario's
corner, a design source (`read_verilog` or `import_odb`) and an
SDC file, exactly as `run_timing` and `report_timing` do. One
report binding per scene is registered through
`session/reports`, named `global_timing_report_<scene>`,
`global_slack_report_<scene>`, `min_period_report_<scene>`,
`min_pulse_width_report_<scene>` and
`clock_gating_check_report_<scene>`.

## `report_global_timing`

The global timing summary: the design-wide worst slack and
total negative slack (the union over every constrained
endpoint, so they equal the `run_timing` scalars), then the
worst path of every path group for the selected check kinds.
The worst path of a group is the worst path to the group's
worst endpoint: the startpoint names the launch, the slack is
the group's WNS.

| Column | Meaning |
|---|---|
| `Group` | The path group: the clock name for the default groups, or a `group_path` name. |
| `WNS (ns)` | The worst slack over every endpoint in the group. |
| `Worst endpoint` | The group's worst endpoint pin. |
| `Startpoint` | The first pin of the worst path to that endpoint. |
| `Slack (ns)` | The worst path's slack, equal to the group's WNS. |

`-delay_type max` reports setup only, `min` hold only; with
neither flag both sections are reported. A design with no
constrained endpoints reports no groups and a WNS of `-`.

## `report_global_slack`

The per-pin slack table: every constrained endpoint pin with
its worst slack for the selected check kinds, worst first.
This is the report that names every pin that violates, not
just the worst one per group: an endpoint that meets timing
appears too, so a clean design reports its margins rather
than an empty table. `-slack_lesser_than <ns>` keeps only
endpoints with slack strictly below the ceiling.
`-delay_type max` reports setup only, `min` hold only; with
neither flag both sections are reported.

## `report_min_period`

The minimum period each clock can close at: for every
clock, period minus the worst setup slack of the clock's
setup path group. One row per clock (its worst endpoint)
by default:

| Column | Meaning |
|---|---|
| `Clock` | The clock the check is measured under. |
| `Period (ns)` | The clock's current period. |
| `Min period (ns)` | The smallest period at which the clock's group still meets setup timing (period minus worst setup slack). |
| `Slack (ns)` | Period minus min period, the group's worst setup slack; negative when the clock is too fast for its paths. |
| `Worst pin` | The endpoint whose setup slack is worst for this clock. |

`-verbose` lists every endpoint of each clock's setup
group instead (Pin, Clock, Period, Min period, Slack).
A clock with no setup group (no constrained endpoints)
yields no row: the report says so instead of inventing
a period. Endpoints reassigned to a named `group_path`
are not in their clock group, so they do not contribute
to the clock's minimum period. Setup only: hold slack
does not depend on the period. Note the upstream bulk
minimum-period check was not used here: it only reports
Liberty `minimum_period` constraints and SDF
annotations, none of which exist in EOS fixtures.

## `report_min_pulse_width`

The minimum-pulse-width checks: every evaluated clock
pulse, its measured width against the required minimum,
worst slack first.

| Column | Meaning |
|---|---|
| `Pin` | The clock pin the pulse is measured on. |
| `Transition` | The opening edge: `rise` for the high pulse, `fall` for the low pulse. |
| `Width (ns)` | The measured pulse width. |
| `Min width (ns)` | The required minimum: the SDC `set_min_pulse_width` value when one is set, else the Liberty min-pulse-width arc. |
| `Slack (ns)` | Width minus min width; negative when the pulse is too narrow. |

`-violators` keeps only the violating checks. A pin with a
minimum-width constraint but no evaluated pulse (no clock
path, or no close path) is listed in a second section,
"Defined but not evaluated", with its coverage reason: a
missing row is never presented as a pass.

## `report_clock_gating_check`

The clock-gating checks: every gated-clock enable the engine
recognizes, with the setup and hold slacks the engine
measured for it.

| Column | Meaning |
|---|---|
| `Enable pin` | The enable pin of the gating cell. |
| `Gated clock pin` | The clock pin of the gating cell. |
| `Setup slack (ns)` | The gating setup slack, or `-` when the engine produced no gated-clock path end for it. |
| `Hold slack (ns)` | The gating hold slack, or `-` when the engine produced no gated-clock path end for it. |
| `Status` | `tested`, or `untested: <reason>` with the coverage reason. |

The enable pin and its gated clock pin come from the
analysis coverage; the slacks come from the gated-clock path
group, whose endpoint is the enable pin. A defined check the
engine never evaluated (no clocked path reaches the enable)
is listed with its coverage reason and no slack, never as a
pass. A design with no gated-clock enable reports no checks
at all.

Known engine gap: in the current EOS session the engine
produces no gated-clock path end at all, even for a check
the coverage marks tested (enable driven by a clocked
flop), with or without an explicit `set_clock_gating_check`
in the SDC. So a tested check lists with `-` slacks and the
report adds a note that the slack is unmeasured, not
passing. The slacks are never invented from a hand
calculation. If the engine ever produces a gated-clock end,
the report carries its slack values unchanged.

## What the engine sees, and what it does not

The margins the gating and pulse-width checks are measured
against come from the SDC file the session read
(`read_sdc`): `set_clock_gating_check` and
`set_min_pulse_width` in that file. EOS-native rows for the
same commands live in the session constraint set and reach
the engine only after `write_sdc` and a fresh `read_sdc`,
exactly as `report_clock` documents for clocks. The
minimum-period bound needs no SDC margin: it is derived
from the clock period and the data paths alone.

- Registry count pin: 83 to 88. Five commands, all new:
  `report_global_timing`, `report_global_slack`,
  `report_min_period`, `report_min_pulse_width`,
  `report_clock_gating_check`.
