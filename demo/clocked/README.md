# Clocked demo (clock network and CTS demo design)

A small self-contained clocked design for the F10 clock-network
view and the F5 CTS demo: one clock `clk` at the top port, one
generated clock `clk_div` from a divide-by-2 toggle flop, and two
sink groups, one per clock. No signal crosses between the domains.

This folder is reference data and documentation. It is outside the
seven-layer code structure: it adds no CMake target and no test.

## The design

```
clk_top
├── u_div (clk_div)      <- clk domain, 1 flop; q is the clk_div source
├── u_ctrl (clk_ctrl)    <- clk domain, FSM + run counter
├── u_bank (clk_bank)    <- clk domain, 8-bit register bank
├── u_acc (clk_acc)      <- clk domain, 8-bit accumulator
├── u_cnt (clk_cnt)      <- clk_div domain, 8-bit counter
└── u_cap (clk_cap)      <- clk_div domain, 8-bit capture register
```

Two sink groups, one per clock. The domains are independent by
construction: no registered signal crosses from one domain to the
other. The FSM ends its RUN state on its own clk-domain counter;
the divided counter's terminal count goes only to the top output
`div_done`.

Ports of the top: `clk`, `rst_n`, `start`, `data[7:0]`, `done`,
`div_done`, `result[15:0]`. `u_bank` drives `result[7:0]`,
`u_acc` drives `result[15:8]`. `u_cap` samples the input data on
the divided clock; its output is intentionally unconnected, because
the top port list is fixed and the capture register exists here as
a `clk_div` sink group for the clock network, not as a data output.

Sink counts, designed vs measured. The designed counts are 21
`clk` sinks (u_div 1, u_ctrl 4, u_bank 8, u_acc 8) and 16 `clk_div`
sinks (u_cnt 8, u_cap 8), 37 flops total. The measured elaborated
counts are 23 `clk` sinks and 16 `clk_div` sinks, 39 flops total.
The difference is in `u_ctrl`: Yosys recodes its two-bit FSM state
register one-hot during elaboration, so the state takes 4 flops
instead of 2, plus the 2-bit run counter, for 6 flops in `u_ctrl`
(5 `$_DFF_PN0_` and 1 `$_DFF_PN1_`). The per-leaf elaborated flop
counts, from the `aigmap` pass, are: `clk_cap` 8, `clk_cnt` 8,
`clk_acc` 8, `clk_bank` 8, `clk_ctrl` 6, `clk_div` 1. The measured
counts are recorded here, not edited to match the designed ones.

## Files

- `rtl/clk_top.v`: top, instantiates the five leaves.
- `rtl/clk_div.v`: divide-by-2 toggle flop. `q` is the source of
  the generated clock `clk_div`.
- `rtl/clk_ctrl.v`: two-bit FSM (IDLE, LOAD, RUN, DONE) plus a
  two-bit run counter. RUN ends on its own counter.
- `rtl/clk_bank.v`: eight-bit register bank. `q <= data` while
  enabled.
- `rtl/clk_acc.v`: eight-bit accumulator. `q <= q + data` while
  running, cleared on load.
- `rtl/clk_cnt.v`: eight-bit counter on the divided clock.
  Terminal count (255) pulses `tc`, exposed as `div_done`.
- `rtl/clk_cap.v`: eight-bit capture register on the divided
  clock. `q <= data` while enabled.
- `design.f`: filelist for the demo (the seven design sources).
- `clk_top.sdc`: the constraints (see below).
- `elab-demo.tcl`: the GUI demo script.

## The SDC: clk_top.sdc

```text
create_clock -name clk -period 5 -waveform {0 2.5} [get_ports clk]
create_generated_clock -name clk_div -divide_by 2 -source [get_ports clk] [get_pins u_div/q]
set_input_delay -clock clk 0.5 [all_inputs]
set_output_delay -clock clk 0.5 [all_outputs]
set_clock_uncertainty -setup 0.1 [all_clocks]
```

Period 5 ns (200 MHz): mock cell delays are sub-nanosecond per
stage, so 5 ns leaves a clearly passing worst slack without being
vacuous. The input and output delays are 0.5 ns, nonzero on
purpose, so input-to-register and register-to-output paths carry
real numbers. The 0.1 ns setup uncertainty gives the skew column
a stored value to read.

One wart, measured: the SDC still carries a `set_input_delay`
on line 3 whose `[all_inputs]` includes the `clk` port itself,
so the STA engine still generates `Warning 441: clk_top.sdc
line 3, set_input_delay relative to a clock defined on the same
port/pin not allowed` (Sdc.tcl `sta_warn 441`). Since PR 136
(error-behavior D3), the engine log is drained and printed only
on failure, so a successful `read_sdc` no longer prints the
warning. The delay is applied to the other inputs and skipped
for `clk`. The SDC is kept as designed; the warning is
cosmetic.

## Run

Build EOS first (from the repository root):

```
cmake -S. -B build -DEOS_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=$HOME/or-tools
cmake --build build -j$(nproc)
```

On macOS, use the commands in `src/ui/gui/README.md` instead
(Homebrew prefix, `eos-gui` target). Then, from this directory,
launch the shell:

```
../../build/apps/eos/eos
```

Paste these commands at the `eos>` prompt, in order. Commands
live in the `eos::` namespace, so the prefix is required:

```
eos::create_scenario func_tt func tt estimate
eos::read_liberty tt ../../src/pd/rsz/tests/mock/eos_mock.lib
eos::read_filelist design.f clk_top
eos::read_sdc clk_top.sdc
eos::elaborate
eos::synth_hierarchy
eos::synth_stats
eos::synth_generic
```

`synth_map` completes on this design since PR 141 (see
"synth_map and the DEF bridge" below), and the flow continues
through the bridge into a DEF the PD stages load:

```
eos::synth_map -target area
eos::write_def clk_top.def -lef ../../src/pd/rsz/tests/mock/eos_mock.lef -tlef ../../src/pd/rsz/tests/mock/eos_mock.tlef
eos::read_lef ../../src/pd/rsz/tests/mock/eos_mock.tlef
eos::read_lef ../../src/pd/rsz/tests/mock/eos_mock.lef
eos::read_def clk_top.def
```

## GUI walkthrough

Build the GUI target (see `src/ui/gui/README.md`), then launch
it from this directory with no script file:

```
../../build/apps/eos-gui/eos-gui
```

The window opens on an empty session. Type these commands in
the console entry line, one per Return (the `eos::` prefix is
optional there):

```
create_scenario func_tt func tt estimate
read_filelist design.f clk_top
read_sdc clk_top.sdc
elaborate
```

Or launch with the script file instead, which runs the same
four commands before the window opens:

```
../../build/apps/eos-gui/eos-gui elab-demo.tcl
```

What you should see: the stepper moves Read and Elaborate to
done as each command runs; the hierarchy tree shows the top plus
its five leaves with their ports. After `elaborate`, the Results
panel shows the elaborated numbers (see "Expected outputs").

## Expected outputs

All values below are measured from a real run of the built
`eos` binary against these files (the full verified sequence
runs in 1.35 s wall clock, including Yosys's verbose pass log
and the ABC run inside `synth_generic`).

- `read_filelist design.f clk_top`: files=7,
  entries=rtl/clk_top.v,rtl/clk_div.v,rtl/clk_ctrl.v,
  rtl/clk_bank.v,rtl/clk_acc.v,rtl/clk_cnt.v,rtl/clk_cap.v,
  top=clk_top,
  modules=clk_cap,clk_cnt,clk_acc,clk_bank,clk_ctrl,clk_div,
  clk_top (module order may vary).
- `read_sdc clk_top.sdc`: sdc=clk_top.sdc clocks=1
  generated=1 uncertainty=2 skipped=0 unwired=0. Since the
  clock-network unit, `read_sdc` also wires the file's clock
  statements into the session constraint set (see "The clock
  table" below); `uncertainty=2` counts the two clock rows
  the one `set_clock_uncertainty -setup 0.1 [all_clocks]`
  statement folded onto. The Warning 441 on line 3 noted
  above is still generated by the STA engine but is no
  longer printed on a successful `read_sdc` since PR 136.
- `elaborate`: module=clk_top cells=5 wires=11 depth=2
  types=\clk_acc:1,\clk_bank:1,\clk_cnt:1,\clk_ctrl:1,
  \clk_div:1. No errors, 0 problems.
- `synth_hierarchy`: exactly the tree in "The design" above:
  clk_top, clk_top/u_acc (clk_acc), clk_top/u_bank (clk_bank),
  clk_top/u_cnt (clk_cnt), clk_top/u_ctrl (clk_ctrl),
  clk_top/u_div (clk_div), each with its ports.
- `synth_stats`: module=clk_top cells=5 wires=11 depth=2
  sequential=0, type \clk_acc 1, type \clk_bank 1,
  type \clk_cnt 1, type \clk_ctrl 1, type \clk_div 1.
  `sequential=0` counts only the top module's own cells; the
  flops live in the leaves (39 total, see "The design").
- `synth_generic`: script=default elaborated_cells=5
  elaborated_wires=11 elaborated_depth=2 before_and=1250
  before_levels=6 after_and=237 after_levels=15. It succeeds
  on this sequential design through the F12 `abc -dff` path
  (PR 131). This is the first sequential design the generic
  flow completes on.

## Known limitations

### run_timing has no meaningful slack on this design yet

`read_sdc` accepts `clk_top.sdc`, but `run_timing` cannot
produce clock-driven slack on this design with the current
engine, and the README records the measured behavior rather
than a slack number:

- After `read_filelist` alone, `run_timing` fails with
  exactly:

  ```
  error: EOS unclosed_design: no timing design source: eos::import_odb or eos::read_verilog first
  ```

  `read_filelist` feeds the synthesis session only; it does
  not set the timing design source. Only `read_verilog` (one
  file) or `import_odb` does.

- With `read_verilog rtl/clk_top.v clk_top` as the timing
  source, `run_timing` succeeds mechanically
  (`source=verilog scenes=1`), but the worst slack is
  1.000000015e+39 ns: no paths at all. STA reads only that
  one file, so all six leaves become black boxes
  (`Warning 198: module clk_div not found. Creating black box
  for u_div`, and likewise for the other five). The clocks
  drive no slack because the design the STA engine sees has
  no cells in it.

The path to real slack is the mapped netlist through
`synth_map` and the DEF bridge (next section), loaded as
the timing design source; that wiring is engine work,
outside this demo's scope. Do not "fix" the demo by
collapsing the seven files into one: the multi-file layout
is the design, and hiding the limitation would mislead the
F5 and clock-network units this demo exists for.

### synth_map and the DEF bridge

`synth_map` completes on this design since PR 141 (the
synth_map-sequential unit): mapping runs inside Yosys
(dfflibmap first, then abc -dff -liberty), so the mapped
Verilog is Yosys-format, one module per leaf. The
mapped-netlist bridge unit then taught `write_def`'s reader
that format, flattening inside the reader, so the F5
acceptance sequence (`synth_map`, `write_def`, `read_def`,
`floorplan`, `place`, `cts -clock <mapped net>`) runs end
to end on this design.

Measured on the sequence above (`synth_map -target area`):

- `write_def` payload: `source=mapped design=clk_top
  cells=168 pins=29`, with `EM_DFFR_X1:31` in `types=`. The
  31 flops are the instantiated ones (u_div 1, u_ctrl 6,
  u_bank 8, u_acc 8, u_cnt 8); the mapped file's design-wide
  census of 39 also counts clk_cap's 8, in a module the
  elaborated design no longer instantiates, so the bridge
  drops them and the DEF reports the design that exists.
- The two mapped clock nets F5 passes to `cts -clock` are
  named `clk` and `div_q` (clk_top's own wire for the
  divider flop's output, instance `u_div/_17_` in the
  pinned Yosys 0.70 backend's numbering).
- Clock-pin expectations on this design are 23 clock pins
  on the `clk` net and 8 on the `div_q` net (EM_DFFR_X1
  clock pins reached), superseding the designed 21 and 16
  under the discrepancy rule above: the 21-to-23 difference
  is u_ctrl's one-hot recode, and the 16-to-8 difference is
  u_cap's pruning at elaborate. The `cts` payload's own
  `sinks` count is the engine's load-pin count, which on
  `div_q` is 9, not 8: the net also drives the
  toggle-feedback inverter's input. Both counts are named
  wherever CTS reports them (see the F5 section below).

### CTS on this design (F5, measured)

The F5 sequence (`flow-demo.tcl` in this directory) runs
`place`, then `cts -clock clk` and `cts -clock div_q`, in
one session. Measured payloads:

- `place`: `placed=168 total=168 hpwl=3965890 overlaps=0`,
  with the STA leg reporting `sta_slack_ns=unavailable` for
  the reason below. (The F5 record was `hpwl=4078316`; the
  F6 pin snap moved the top-level pin centers to the
  manufacturing grid, which moved the HPWL. The CTS counts
  below are unchanged.)
- `cts -clock clk`: `roots=1 buffers=3 sinks=23 subnets=3`,
  characterization, root, and sink buffers all
  EM_CLKBUF_X1, inferred from the mock Liberty with no
  buffer pinning.
- `cts -clock div_q`: `roots=1 buffers=3 sinks=9 subnets=3`
  (9 load pins: the 8 clock pins plus the toggle-feedback
  inverter's input, `u_div/_16_` pin A).

Both payloads end with the STA outcome as an unavailable
statement, verbatim: `sta_slack_ns=unavailable (scene
func_tt: could not read SDC file: clk_top.sdc: Error 374:
clk_top.sdc line 2, empty ports/pins/nets argument.)` (the
path echoes how the session read the SDC). The cause is
upstream of CTS: the timing service re-reads `clk_top.sdc`
against the flat imported database, where the SDC's
generated-clock source `[get_pins u_div/q]` does not
resolve (the divider is `u_div/_17_`, pin `Q`, in the flat
DEF), so every ODB-path analysis on this design reports
unavailable and `run_timing` fails with the same error
before and after CTS, exactly as it does at the place
stage. A separate SDC pin-resolution fix addresses it.

`cts_summary` after the two runs (the Clocks tab's CTS
section renders the same record):

```text
cts nets=2
net name=clk roots=1 buffers=3 sinks=23 clock_sinks=23 subnets=3 liberty_corner=tt
sta_note sta_slack_ns=unavailable (scene func_tt: could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc line 2, empty ports/pins/nets argument.)
net name=div_q roots=1 buffers=3 sinks=9 clock_sinks=8 subnets=3 liberty_corner=tt
sta_note sta_slack_ns=unavailable (scene func_tt: could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc line 2, empty ports/pins/nets argument.)
```

Post-CTS, `clock_network_summary` still shows both trees
per pin: `clk` with `sinks=23` and `clk_div` with
`sinks=8`, the divider flagged `generates=clk_div`, and 3
inserted clock buffers walked on each tree. The `clk_div`
source resolves through the root buffer CTS inserted on
`div_q`; F5 fixed the walk's structural source rule to
treat a clock buffer's input pin as clock-carrying (before
the fix, `clk_div` rendered `tree=none, sinks=-` after
CTS). The walked `clk_div` tree shows 5 buffer-kind nodes
in total: the 3 CTS buffers plus the feedback inverter and
the divider's D pin, design logic on the generated clock's
nets. Re-running `cts` on either net in the same session
fails with "no clock tree was built" (the engine skips an
already-buffered net) and leaves the record unchanged.

### Route on this design (F6, measured)

The F6 sequence continues the F5 one with `route`, then
`route_summary`, in the same session. Measured payloads:

- `route`: `routed=185 total=185 guides=520 overflow=0
  violations=0 detailed_iterations=2 detailed_bound=64
  stopped_at_bound=false liberty_corner=tt
  die=0,0,86000,86000 core=0,0,86000,83200`, with the STA
  leg reporting the same unavailable statement as place
  and CTS (the SDC pin-resolution gap, not a routing
  outcome). Per-layer congestion: em1 capacity=1720
  usage=155, em2 capacity=424 usage=109, em3 through em10
  usage=0. All 185 nets routed, none unwired.
- `route_summary`: the header above, one `layer` line per
  layer in order, then 1326 `wire` lines and 987 `via`
  lines covering all 185 nets (`clk` and `div_q` both
  wired, on em1, em2, and em3), no `unwired` lines, and
  the STA note verbatim. A second `route` in the same
  session succeeds and replaces the record; its summary is
  byte-identical to the first (the route is deterministic
  on this design).

Route leaves the place, CTS, and clock records alone:
`place_summary` and `cts_summary` are byte-identical
after a route, and `clock_network_summary` still shows
`clk` with `sinks=23` and `clk_div` with `sinks=8`. The
clockless optloop demo routes too (`routed=6 total=6
guides=22 overflow=0 violations=0 detailed_iterations=1
detailed_bound=64`, die `0,0,10000,10000`).

### Extraction and post-route STA on this design (F7, measured)

One sentence first: the `sta_note` unavailable text quoted
in the F5 and F6 sections above is stale, written before
the SDC pin-resolution fix (PR 146); since that fix the
same runs report finite slack, as the F7 numbers below
show.

The F7 sequence continues the F6 one in the same session:
`extract_parasitics clk_top.spef -model
../../src/pd/rsz/tests/mock/eos_mock.rules`, then
`extract_summary`, then a second scenario over the
written SPEF (`create_scenario func_tt_ext func tt
extracted`, `set_spef clk_top.spef`, `run_timing`), then
`extract_summary` again. The rules file is the mock
PDK's extraction model, generated by `gen_mock.py` from
the same layer table as the mock library; its values are
invented for the mock stack (see the mock README).
Measured payloads:

- `extract_parasitics`: `nets=185 rsegs=2486
  cap_nodes=2486 cc_segs=1786`, every routed net
  extracted, and the post-route STA leg over the routed
  database reporting `scene=func_tt
  worst_slack_ns=3.292352835`, the same finite slack the
  post-route `run_timing` reports (the SDC pin map from
  `write_def` is what lets the analysis resolve
  `[get_pins u_div/q]` in this session).
- `extract_summary` before the extracted run: the
  header (`extract nets=185 rsegs=2486 cap_nodes=2486
  cc_segs=1786 spef=clk_top.spef model=<the rules
  path>`) and the one slack line above, no comparison
  yet.
- The extracted `run_timing` analyzes both scenes
  (`scenes=2`): the estimated scene's measured values
  are unchanged, and the extracted scene reports worst
  slack 3.134516868 ns and worst hold slack
  0.1716079501 ns.
- `extract_summary` after it gains the comparison line:
  `compare mode=func corner=tt estimate_scene=func_tt
  extracted_scene=func_tt_ext
  estimated_worst_slack_ns=3.292352835
  extracted_worst_slack_ns=3.134516868
  estimated_worst_hold_slack_ns=0.1642400105
  extracted_worst_hold_slack_ns=0.1716079501`.
  Extraction moves the worst slack by about 0.16 ns on
  this design; that is the point of the comparison, and
  it is an extracted estimate on a mock stack, not a
  signoff number.
- SPEF annotation: the reader resolved every net,
  instance, and pin name the writer emitted; the run
  log carries zero SPEF not-found warnings.

In the GUI, the Extract stage button runs the same
command and lands on the Timing tab, whose Extraction
section renders the record and, once the extracted run
exists, the comparison.

### The clock table in a read_sdc-only session (F2 caveat, fixed)

Before the clock-network unit, this was the F2 caveat:
`report_clock` printed `clocks=0` and `timing_summary` failed
with `no timing results: run eos::run_timing first`, because
`read_sdc` recorded only the file path. The unit fixed it:
`read_sdc` wires the file's clock statements into the session
constraint set, and the measured outputs after
`create_scenario`, `read_filelist`, `read_sdc`, `elaborate`
are:

- `timing_summary` (clocks-only mode, no timing run yet)
  shows `clk` at 5 ns with waveform `{0 2.5}` and skew 0.1,
  and `clk_div` at 10 ns with waveform `{0 5}`, the
  generated flag, and the `genclock` row (master `clk`,
  `divide_by(2)`, master source `{clk}`, sources
  `{u_div/q}`).
- `report_clock` shows the same two clocks plus the
  generated-clock table row (`clk_div`, master source
  `{clk}`, generated source `{u_div/q}`, master clock `clk`,
  `divide_by(2)`), and `report_clock -skew` reads 0.1 for
  both clocks with no "no clock uncertainty" footer.
- `clock_network_summary` (the Clocks tab's query) walks
  the network at three stages: constraints (rows only) and
  elaborated (per-leaf sink groups counted in sequential
  cells: 5 on `clk`, 1 on `clk_div`, with `u_div` flagged
  as the sink that generates `clk_div`). Two measured
  caveats apply. First, `u_cap` appears in neither stage:
  `elaborate`'s fsm pass runs `opt_clean`, which removes
  the instance because its only output is unconnected (by
  design, above), so the 39-flop total in "The design"
  counts a module the elaborated hierarchy no longer
  instantiates. Second, the mapped stage walks the
  bridged DEF per pin since the mapped-netlist bridge
  unit taught the DEF bridge's reader the Yosys format:
  23 clock pins on `clk` and 8 on `clk_div`, the divider
  flop flagged as the sink that generates `clk_div`.
  Latency reads `-` at every stage: it is a post-CTS STA
  result, delivered by F5.

### Mock guardrail

The mock library validates mechanics only, never timing,
power, or area against silicon, and no mock-based run may
ever be cited as a signoff claim.
