# `synth_summary` and the Synthesis view (full-flow GUI unit F1)

`eos::synth_summary` is the mapped synthesis query: the one
structured read over the session synth record (`ctx.synth`) that
the GUI Synthesis view consumes. It reports the `synth_map`
record, the `synth_opt` loop record, and the most recent
`check_equivalence` proof, as three machine-readable lines. It
never mutates session state and never parses report text: every
number comes from the record the synthesis commands already
maintain.

```text
synth_summary
```

It fails fast when no mapped design exists: `no mapped design:
eos::synth_map first`. The loop and equivalence sections report
their placeholders until `synth_opt` and `check_equivalence`
run.

## Payload

Three lines, fixed key order per line, single-space `key=value`
tokens, `none` for absent values, full-precision doubles.

```text
mapped target=area liberty=<path> verilog=<path> cells=4 types=AND2:3,BUF:1 judged=true worst_slack_ns=-0.35 tns_ns=-0.35
opt run=true winner=classic iterations=1 verilog=<path> cells=4 types=AND2:4 worst_slack_ns=0.05 tns_ns=0
equiv run=true verdict=equivalent first=<path> second=<path>
```

Slack prints at full precision, so a real payload may carry
trailing digits (for example `-0.350000029` for -0.35 ns); the
numbers above are the values to 1e-4, matching
`demo/synth/README.md`.

| Line | Meaning |
|---|---|
| `mapped` | The `synth_map` record: target, Liberty and netlist paths, cell count, cell types as `Type:count` in name order, and the STA judgement. `judged=false` (no SDC at map time) carries `worst_slack_ns=none tns_ns=none`, never an invented zero. |
| `opt` | The loop record: winner strategy, iteration count, winner netlist, cells, types, worst slack and TNS after the loop. All placeholders (`run=false ...`) when the loop never ran. |
| `equiv` | The most recent proof: verdict `equivalent`, `not_equivalent`, or `unknown`, and the two files proven. All placeholders when no proof ran. |

## The GUI view

The Results tab shows the mapped section after the live-stats
block: mapped cells by type, worst slack and TNS before the loop,
the loop winner with its iterations, cells by type, worst slack
and TNS after the loop, and the equivalence verdict. Each section
names its own "not run yet" state instead of hiding, and an
unjudged mapping renders `worst slack=n/a, TNS=n/a`.

The stepper state contract is `SessionView::SynthesisStep()`,
Done once a mapped design exists. The stepper widget itself is
unit F11's.

## Acceptance numbers

On the `demo/synth` chain (`synth_map -target area`, then
`synth_opt -strategies classic`, then `check_equivalence` of the
mapped netlist against the loop winner), the view shows mapped 4
cells (AND2:3, BUF:1) at worst slack -0.35 ns, loop winner
`classic` at worst slack +0.05 ns with equivalence `equivalent`,
matching `demo/synth/README.md`.
