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

`synth_map` is deliberately absent from this sequence: it does
not complete on this design (see "Known limitation" below). Do
not append it to the demo and expect success.

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
- `read_sdc clk_top.sdc`: sdc=clk_top.sdc. The Warning 441 on
  line 3 noted above is still generated by the STA engine but
  is no longer printed on a successful `read_sdc` since PR 136.
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
`synth_map`, which is blocked by the limitation below. This
is engine work, outside this demo's scope. Do not "fix" the
demo by collapsing the seven files into one: the multi-file
layout is the design, and hiding the limitation would
mislead the F5 and clock-network units this demo exists for.

### synth_map on sequential designs

`synth_map` does NOT complete on this design, and the reason
is an engine limitation, not a demo bug. The measured failure
is:

```
error: EOS io_error: ABC command failed (mapping-time optimization): strash
Line 872: Cannot find the model for subcircuit $_DFF_PN0_.
Reading network from file has failed.
Error: Empty network.
```

Note the failure mode: `synth_map`'s sequential guard checks
the top module's own sequential count (`Stats(top)`), which
is 0 for a hierarchical design whose flops live in leaves,
so the guard lets this design through and it dies later in
ABC. The guard needs the design-wide count (`DesignTotals`,
as `synth_generic` already uses) to fail fast here. That
fix belongs to the synth_map-on-sequential unit, not this
demo. The F5 acceptance sequence in the design doc
(`synth_map`, `write_def`, `read_def`, `floorplan`, `place`,
`cts -clock <mapped net>`) does not run until that unit
lands. No mapped net names are recorded here because no
mapped netlist exists yet; the F5 unit records them after
the first run.

### The clock table is empty in a read_sdc-only session

`report_clock` prints `clocks=0` and `timing_summary` fails
with `no timing results: run eos::run_timing first`. That is
the F2 caveat: `read_sdc` records only the file path, and the
session constraint set the clock table reads is empty until
the clock-network unit lands the wiring fix. Its acceptance
is: after `create_scenario`, `read_filelist`,
`read_sdc`, `elaborate`, `timing_summary`, the table shows
`clk` at 5 ns with waveform `{0 2.5}` and `clk_div` at 10 ns
with waveform `{0 5}` and the generated flag, and
`report_clock` shows the same two clocks plus the
generated-clock table row (`clk_div`, master `clk`,
`divide_by(2)`, source `{u_div/q}`). Before the fix the
table is empty; after the fix it is not.

### Mock guardrail

The mock library validates mechanics only, never timing,
power, or area against silicon, and no mock-based run may
ever be cited as a signoff claim.
