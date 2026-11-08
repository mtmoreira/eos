# `timing_summary` and the Timing panel (full-flow GUI unit F2)

`eos::timing_summary` is the structured timing query the GUI
Timing panel reads: one payload with the per-scene slack summary
from the last `run_timing`, the clock table from the session
constraint set, the worst-violator table from the same results,
the worst paths across endpoints (worst first) from the timing
service, the coverage summary, and the QoR numbers.

```text
timing_summary [-max_paths N] [-scene <name>]
```

It is a query, not a report: it registers no report binding,
writes no file, and changes no session state. It fails fast with
"no timing results: run eos::run_timing first" when no timing run
exists, and with an unknown-scenario error when `-scene` names a
scene the last run never covered.

## What it reads, and what it re-runs

The slack summary, the violator rows, and the clock table are
reads of session state: `ctx.timing.last_results` and the
project constraint set. The worst paths, the coverage summary,
and the QoR rows come from the timing service itself
(`ReportPaths`, `AnalyzeCoverage`, `AnalyzeQor`), never from
report text: the service runs one fresh engine session per
scene for each of the three calls, exactly as `report_timing`,
`report_analysis_coverage`, and `report_qor` do. That costs up
to four engine sessions per scene per call. The GUI refreshes
the panel only when the timing inputs change (a new run, a new
SDC, a new scenario, or a new synthesis record), so a plain
refresh never pays it twice; a CLI call pays it every time.

The scenes reported are the scenes of the last `run_timing`, in
run order. A scenario added after that run is not shown until
`run_timing` runs again.

The clock table reads the session constraint set only, exactly
like `report_clock`: a clock defined in an SDC file does not
appear, because `read_sdc` records the file path only and never
writes the file's clocks into the session set. Define the clock
with the interactive `create_clock` to see its row; the two
systems are disjoint (see `docs/read-sdc-audit.md` section 5),
so defining it interactively does not disturb the STA run,
which reads its clocks from the file. Wiring file clocks into
the session set is a later unit's job, not this query's.

## The payload

The first line is the header, then the clock rows, then one
group of rows per scene. Values are single tokens, except a
`{...}` brace group, which may contain spaces (source lists
and waveform modifications).

```text
source=verilog scenes=1 revision=3
clock name=vclk generated=0 period=1.25 rise=0 fall=0.625 attrs= skew=none sources={-}
scene=func_tt mode=func corner=tt parasitics=estimate worst_slack_ns=0.05 total_negative_slack_ns=0 worst_hold_slack_ns=1.2 total_negative_hold_slack_ns=0 max_transition_ns=0 max_transition_violations=0 max_capacitance_pf=0 max_capacitance_violations=0 max_fanout=0 max_fanout_violations=0
scene=func_tt class=max_delay violator=- slack=0.05 cost=0 weight=1 violations=0
scene=func_tt class=min_delay violator=- slack=1.2 cost=0 weight=1 violations=0
scene=func_tt class=max_transition violator=- limit=none weight=1 violations=0
path scene=func_tt check=setup endpoint=y1 startpoint=a slack=0.05 arrival=1.2 required=1.25
path scene=func_tt check=hold endpoint=y1 startpoint=a slack=1.2 arrival=1.2 required=2.4
coverage scene=func_tt checks=4 tested=4 untested=0
coverage scene=func_tt type=out_setup tested=2 untested=0 percent=100.0
coverage scene=func_tt type=out_hold tested=2 untested=0 percent=100.0
qor scene=func_tt setup_groups=1 hold_groups=1 union_tns=0 union_endpoints=0 every_group_tns=0 every_group_endpoints=0 hold_union_tns=0 hold_union_endpoints=0 hold_every_group_tns=0 hold_every_group_endpoints=0 cells=4 area=8 compile_cpu_s=0.01
qor scene=func_tt group=vclk check=setup wns=0.05 tns=0 violating_paths=0
qor scene=func_tt cell=AND2 count=4 area=8
qor scene=func_tt drc=max_transition violations=0 worst_slack=none
```

| Row | Meaning |
|---|---|
| `source=` | Design source (`verilog` or `odb`), scene count, ledger revision. |
| `scene=` | One scene's slack summary: mode, corner, parasitics level, the four slack metrics, and the worst electrical value plus violating-pin count per check. Slack values use the same rendering as `run_timing`. |
| `clock` | One clock-table row: name, generated flag, period and waveform rise/fall in ns (`-` when underivable), attribute letters, skew (`none`: no clock uncertainty is defined yet), and sources in braces. Same entries `report_clock` renders. |
| `genclock` | One generated-clock row: name, master clock, waveform modification in braces, master source in braces, generated sources in braces. |
| `bindclock` | One clock construction record (`bind_clock`, no period): name and target in braces. |
| `scene=... class=` | One worst-violator row per class (`max_delay`, `min_delay`, `max_transition`, `max_capacitance`, `max_fanout`): the worst violator, its slack, the exact cost, weight 1, and the violating-pin count. A DRC class with no limit reports `limit=none` instead of slack and cost. Same rows `report_constraint` renders. |
| `path` | One worst path: scene, check (`setup` or `hold`), endpoint and startpoint pins, slack, arrival, and required time in ns. At most `-max_paths` per check per scene (default 5), across endpoints, worst first. Per-endpoint enumeration is not implemented, so `-to` with more than one path is not a thing here either: the view shows worst paths across endpoints, never several paths to one endpoint. |
| `coverage` | One scene's coverage totals (`checks`, `tested`, `untested`), then one row per check type with checks defined (`type`, `tested`, `untested`, `percent`). Same counts `report_analysis_coverage` renders. |
| `qor` | One scene's QoR scalars (group counts, union and every-group TNS with endpoint counts for setup and hold, cell census, compile CPU seconds, `n/a` when no synthesis run was recorded), then per-group rows, per-cell rows, and per-DRC-class rows. Same numbers `report_qor` renders; a DRC class with no limit reports `worst_slack=none`. |

## The Timing panel

The right panels gain a sixth tab, Timing, between Results and
Opt log. It renders the model in sections: the per-scene slack
summary, the clock table, the worst-violator table, the worst
paths, the coverage summary, and the QoR numbers. With no
timing run it reads "(no timing results yet: run
eos::run_timing first)". The panel refreshes through
`SessionView::RefreshTiming`, which dispatches `timing_summary`
without an echo line and rebuilds the model only when the
timing inputs changed; a failed query or a malformed payload
clears the panel, so stale timing never lingers.

## Acceptance

On the optloop session (`demo/synth`, `synth_opt` winner
`classic`), `run_timing` then the Timing panel shows worst
slack +0.05 ns for the `func_tt` scene. The clock table shows
`vclk` at 1.25 ns once the session defines it with the
interactive `create_clock` (which needs an open design, so the
clock is defined on a port of an opened design); in a
`read_sdc`-only flow the table is empty, because the file's
clocks never enter the session constraint set (see above).
Showing the file's clocks without that step needs the later
unit that wires file constraints into the session set.
