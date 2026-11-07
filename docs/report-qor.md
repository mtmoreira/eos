# `report_qor` and `report_bottleneck` in the EOS registry (Immediate unit 9)

`eos::report_qor` and `eos::report_bottleneck` are the
quality-of-results reports, review item I9: the first thing a
signoff review asks for after the constraints are known, and
EOS previously could only show per-scene scalars
(`run_timing` gives worst slack and TNS per scene), never the
per-group table with cell counts, area, DRC summary and
compile CPU, and never which cells the violating paths run
through. `report_qor` reports, per scene, the per-group
WNS/TNS and violating-path counts for setup and hold, the cell
census and total area, the DRC summary, and the compile CPU.
`report_bottleneck` ranks the cell instances by the number of
violating worst paths passing through them. A third command,
`eos::set_bottleneck_capture`, is the capture setting the
bottleneck report requires before the first timing update.

```text
report_qor [-scene <name>] [-file <path>]
report_bottleneck [-delay_type max|min] [-max_cells <n>] [-scene <name>] [-file <path>]
set_bottleneck_capture on|off
```

`report_qor` runs the timing analysis itself for the selected
scenes: it does not reuse a previous `run_timing`, because
the session keeps no live engine. It needs an active
scenario, a Liberty for the scenario's corner, a design
source (`read_verilog` or `import_odb`) and an SDC file,
exactly as `run_timing` and `report_timing` do. One report
binding per scene is registered through `session/reports`,
named `qor_report_<scene>`.

`report_bottleneck` never runs timing. It reads the worst
paths the last timing update saved while the capture was on,
so the ranking is the design as of that update. It needs a
`run_timing` to have run with the capture on; see
`set_bottleneck_capture` below. One report binding is
registered per call, named `bottleneck_report_<scene>`, or
`bottleneck_report_all` when no `-scene` is given.

## The path-group tables

One table per check kind (setup, then hold), one row per
path group, in this order:

| Column | Meaning |
|---|---|
| `Group` | The path group: the clock name for the default groups, or a `group_path` name. |
| `WNS (ns)` | The worst slack over every endpoint in the group. |
| `TNS (ns)` | The sum of the negative slacks in the group. |
| `Violating paths` | The violating-endpoint count in the group. |

The groups are the timing engine's groups: the default
groups are named after their clocks, and a `group_path`
exception takes precedence over the clock group, so an
endpoint matching a `group_path` command sits in the named
group and in no clock group. Special groups are `path_delay`,
`gated_clk`, `async` and `unconstrained`. An endpoint can
belong to several named groups at once, which is why TNS is
reported two ways, below.

## Union TNS vs every-group TNS

Both numbers are shown, because they differ when an endpoint
belongs to several groups. The union TNS counts each
violating endpoint once, and equals the `run_timing`
`total_negative_slack_ns` by construction: the group
population and the scalar population are the same endpoints.
The every-group TNS counts a violating endpoint once per
group it belongs to. Example, one endpoint at -0.1 ns in two
named groups:

```text
Path groups (setup):
Group  WNS (ns)  TNS (ns)  Violating paths
-----  --------  --------  ----------------
fast   -0.1      -0.1      1
slow   -0.1      -0.1      1
-----
TNS (union over 1 endpoints): -0.1
TNS (every group, 2 endpoints): -0.2
```

The review's worked example is the same shape at larger
scale: union -14 over 2 paths vs every-group -23 over 4
paths for the same endpoints. Neither number is the "true"
TNS on its own: the union is the design's TNS, the
every-group total is the sum the group table adds up to.

## The cell census and area

One row per Liberty cell type, most instances first, ties by
cell name, plus the totals:

```text
Cells: 3, total area 22 (Liberty area units)
Cell Type  Count  Area
---------  -----  ----
DFF        2      20
BUF        1      2
```

The counts are leaf instances in the design as analyzed; the
area is the Liberty cell area summed over the instances of
that type. An instance with no Liberty cell is counted under
`(no liberty cell)` with zero area, never dropped.

## The DRC summary

The three electrical checks from the timing engine (unit
F5), one row each, with the violating-pin count and the worst
slack. The limits come from the Liberty library, exactly as
in `report_constraint`. A check with no limit on any pin
reports `no limit`, never a clean worst value.

```text
DRC summary:
Check            Violations  Worst Slack
---------------  ----------  -----------
max_transition   0           no limit
max_capacitance  0           no limit
max_fanout       0           no limit
```

## Compile CPU

Compile CPU is the process CPU time of the synthesis runs in
this session: `synth_map` plus `synth_opt`, each recorded at
its successful completion. It is the EOS compile, so the
number is the synthesis cost, not a wall-clock build time.
When neither command has run in this session the line reads
`n/a (no synthesis run in this session)` instead of a
fabricated zero.

```text
Compile CPU: 0.123 s (synth_map plus synth_opt)
```

## `set_bottleneck_capture` and the capture prerequisite

PrimeTime saves pin arrival and slack with the variable
`timing_save_pin_arrival_and_slack`, set before the first
timing update. EOS has no variable mechanism, so the setting
is the command `eos::set_bottleneck_capture on|off`
(default off). It must be set before the timing update:
`run_timing` saves the worst path of every violating
endpoint, one per endpoint, setup and hold separately, only
while the setting is on. Setting it after `run_timing` has
no effect until `run_timing` runs again, and a later
`run_timing` with the capture off clears the saved paths, so
stale paths can never serve a later report.

`report_bottleneck` fails fast unless the last `run_timing`
captured, naming `set_bottleneck_capture` and re-running
`run_timing`. A scene that ran with no violations has no
saved entry, which ranks as empty, not as missing data.

## The bottleneck ranking

One row per cell instance, most violating paths first:

| Column | Meaning |
|---|---|
| `Rank` | 1-based, after sorting. |
| `Cell Instance` | The instance the violating paths run through. |
| `Cell Type` | Its Liberty cell. |
| `Paths` | The number of saved violating worst paths passing through it. |
| `Worst Slack (ns)` | The worst slack over those paths. |

A cell counts once per path it lies on, however many of the
path's pins touch it. Ties rank by worst slack, then
instance name, so the order is deterministic. `-delay_type
max` ranks the setup paths (the default), `-delay_type min`
the hold paths. `-max_cells` caps the table at that many
rows (default 10). Without `-scene` the ranking pools the
captured paths of every scene; with `-scene` it ranks one
scene's paths only.

Example, one violating setup path through `r1`, `u1` and
`r2`:

```text
Bottleneck (setup, 1 violating paths, scene all):
Ranking from the paths saved by the last run_timing with bottleneck capture on.
Rank  Cell Instance  Cell Type  Paths  Worst Slack (ns)
----  -------------  ---------  -----  ----------------
1     r1             DFF        1      -0.1
2     r2             DFF        1      -0.1
3     u1             BUF        1      -0.1
```

## The payload

The payload is machine-readable key/value lines, then the
rendered report text. Per scene, `report_qor` emits
`scene=<name> setup_groups=<n> hold_groups=<n>
union_tns=<setup union> union_endpoints=<n>
every_group_tns=<setup every-group>
every_group_endpoints=<n> hold_union_tns=<hold union>
hold_union_endpoints=<n> hold_every_group_tns=<hold
every-group> hold_every_group_endpoints=<n> cells=<n>
area=<total> compile_cpu_s=<seconds or n/a>
report=qor_report_<name> artifact=<id>`, plus one line per
group (`scene=<name> group=<group> check=setup|hold
wns=<worst> tns=<tns> violating_paths=<n>`), one line per
cell type (`scene=<name> cell=<type> count=<n>
area=<area>`), and one line per DRC check (`scene=<name>
drc=<check> violations=<n> worst_slack=<slack or no limit>`).
`-file` writes the rendered text to the file and adds a
`file=<path>` line.

`report_bottleneck` emits `scene=<name or all>
delay_type=max|min paths=<n> cells=<n>
report=bottleneck_report_<name or all> artifact=<id>`, plus
one line per ranked cell (`scene=<name or all> rank=<i>
cell=<instance> type=<cell> paths=<n>
worst_slack=<slack>`).

## Boundary

- `report_qor` reports the per-group table, cell census,
  DRC summary and compile CPU. It does not report
  `report_area` / `report_cell` / report gates: those are
  I11, not I9.
- `report_bottleneck` ranks cells by violating-path count
  from the saved paths. It does not re-run timing, and it
  does not rank by arrival time or by slack alone: the count
  is the ranking key.
- Compile CPU covers `synth_map` and `synth_opt` only. It
  does not cover `elaborate`, `synth_generic`, place and
  route, or the timing runs themselves.
- Unknown options and positional arguments fail fast.
  `-delay_type` must be `max` or `min`; `-max_cells` must be
  a positive integer. An unknown `-scene` fails fast.
- Registry count pin: 77 to 80. Three commands, not one:
  the two reports plus the capture setting, which the
  bottleneck report requires before the first timing update.
