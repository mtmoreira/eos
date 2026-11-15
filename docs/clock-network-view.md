# `clock_network_summary` and the Clocks tab (full-flow GUI unit F10 view)

`eos::clock_network_summary` is the structured clock-network
query the GUI Clocks tab reads: one payload with the clock
table from the session constraint set, each clock's source,
and its sink tree at the deepest stage the session holds. It
is the read side of the F2 caveat fix: `read_sdc` wires the
SDC file's `create_clock`, `create_generated_clock`, and
`set_clock_uncertainty` statements into the session constraint
set (see `docs/sdc-commands.md`), so the clocks a session
holds, whether defined interactively or read from a file, are
the clocks every view reports.

```text
clock_network_summary
```

It is a query, not a report: it registers no report binding,
writes no file, and changes no session state. It takes no
options and never fails for a missing database: an empty
session answers `stage=constraints clocks=0`.

## The F2 fix: file clocks in the session set

`read_sdc` parses the recorded file with the scoped parser in
the L1 `io/sdc` module (statements: `create_clock`,
`create_generated_clock`, `set_clock_uncertainty`; everything
else is ignored, and a clock statement the wiring cannot
represent is reported as `unwired` in the payload, never
silently dropped). Wired clocks become ordinary constraint
rows with the interactive handlers' param names plus a
`source_file` provenance param; `set_clock_uncertainty`
folds onto the clock rows as `uncertainty_setup_ns` /
`uncertainty_hold_ns` params. An interactively defined clock
wins a name collision over the file's row (counted as
`skipped`), and re-reading the same file replaces its rows
instead of stacking them. The `read_sdc` payload counts what
happened:

```text
sdc=clk_top.sdc clocks=1 generated=1 uncertainty=2 skipped=0 unwired=0
```

Three readers of the stored uncertainty changed with it:
`report_clock -skew`, the `timing_summary` clock rows, and
the Timing model's skew validation (a number is accepted,
`none` still is). `timing_summary` also gained a clocks-only
mode: with clock rows in the set but no timing run, it
answers the header (`scenes=0`) plus the clock rows; with
neither, its failure is the pre-unit one, byte-identical
("no timing results: run eos::run_timing first").

## The three stages

The payload's stage is what the session holds:

- `constraints`: no design walked yet. One source node per
  clock (a `-` source for a virtual clock), sinks `-`.
- `elaborated`: the session holds an elaborated design. The
  tree is per leaf instance: each leaf with sequential cells
  is one sink node carrying the module's flop count.
- `mapped`: the session holds a mapped design in the
  database. The tree is per pin: the walk follows clock pins
  through buffers, and each clock-pin load is one sink.

A generated clock's source resolves by exact name, then by a
unique instance-name suffix, then structurally (the divider
is a sink of its master whose output net reaches clock pins
without belonging to the master's tree). All three failing,
the clock reports `tree=none` with a note naming the missing
source: the view never guesses a tree. Latency is `-` at
every stage: latency is a post-CTS STA result, delivered by
F5's insertion-delay attribute and the STA read-back, and the
payload's final note says so.

## The payload

The first line is the header, then per clock one `clock`
line followed by its `node` lines, then `shared` lines for
sink sets two clocks share, then `note` lines. Values are
single tokens, except a `{...}` brace group, which may
contain spaces (source lists, node names, note text). The
GUI model validates the header count against the clock
lines, the stage against the three known stages, and every
line's key set, so a payload that disagrees with itself is
malformed.

On the clocked demo (`demo/clocked`) the stages report what
the design actually holds at each stage, which is not the
designed counts. At the elaborated stage the sink counts are
the facade's sequential-cell counts per leaf module: 5 on
`clk` (u_div 1, u_ctrl 2, u_bank 1, u_acc 1) and 1 on
`clk_div` (u_cnt). One designed leaf never appears at all:
`elaborate`'s fsm pass runs `opt_clean`, which removes the
u_cap instance from the top because its only output is
unconnected (the demo design leaves it unconnected on
purpose), so no stage of this view can show its 8 flops.
The mapped stage is not reachable on this design yet: the
F0 bridge's netlist reader understands the ABC-written
mapped format only, and sequential `synth_map` (PR 141)
writes Yosys-format Verilog, so `write_def` fails and the
query keeps reporting the constraint rows with a note
saying the live design was replaced. The mapped walk
itself is proven on LEF/DEF fixtures (a clock buffer
between the source and two flop sinks). Judged per pin on
the mapped netlist that does exist, the domains hold 23
clock pins on `clk` and 8 on `clk_div`: the 39-flop total
PR 141 measured counts the orphaned clk_cap module's cells
design-wide, and mapping preserves each surviving flop's
domain, not the pruned instance's.

## The Clocks tab

The right panels gain a Clocks tab beside Timing. It renders
the model as text: one block per clock (period, waveform,
sources, skew, sinks, latency), the generated clock's
derivation line, the indented sink tree (a divider node
names the clock it generates), the line tying a generated
clock to its master's tree, and the notes. With no clocks in
the session it reads "(no clock network yet: no clocks are
defined in this session. Define one with create_clock or
read an SDC file with read_sdc.)". The model refreshes on
every window refresh, like the floorplan and placement
models: the query is a cheap constraint read plus at most
one design walk, and it never re-runs a flow step. A failed
query or a malformed payload keeps the last good model and
logs the failure, so a stale tree never silently lingers and
never vanishes either.
