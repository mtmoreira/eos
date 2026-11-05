# Hierarchical demo (multi-level design for GUI testing)

A realistic small design for exercising the EOS GUI more properly
than the flat optloop demo in `demo/synth/` can: multiple hierarchy
levels to navigate, a mix of combinational and sequential logic,
and a macroblock that resolves only through library linking.

This folder is reference data and documentation. It is outside the
seven-layer code structure: it adds no CMake target and no test.

## The design

A small DSP-flavored block. A control FSM sequences a datapath
that runs a multiply-accumulate plus an ALU stage and captures the
result in an output register.

```
hier_top
├── u_ctrl (hier_ctrl)
│   └── u_cnt (hier_counter)
└── u_data (hier_datapath)
    ├── u_alu (hier_alu)
    ├── u_mac (eos_macro_mac)      <- library macroblock, from lib/
    └── u_out (hier_outreg)
```

Three hierarchy levels: top, submodule, leaf. Combinational logic:
`hier_alu` (add/sub/xor/and). Sequential logic: `hier_ctrl` FSM,
`hier_counter`, the datapath accumulator, `eos_macro_mac`
(registered multiply-accumulate), `hier_outreg`.

Ports of the top: `clk`, `rst_n`, `start`, `data_a[7:0]`,
`data_b[7:0]`, `done`, `result[15:0]`.

## Files

- `rtl/hier_top.v`: top, instantiates control and datapath.
- `rtl/hier_ctrl.v`: four-state FSM (IDLE, LOAD, RUN, DONE).
- `rtl/hier_counter.v`: leaf counter, ends the RUN state.
- `rtl/hier_datapath.v`: MAC plus ALU stage plus output register.
- `rtl/hier_alu.v`: leaf combinational ALU.
- `rtl/hier_outreg.v`: leaf output register.
- `lib/eos_macro_mac.v`: library macroblock (see below).
- `design.f`: filelist for the demo (design sources plus library).
- `design_no_lib.f`: negative control, same design minus the
  library file. Elaborating from it still succeeds, but `u_mac`
  vanishes from the hierarchy tree and block diagram (see
  "Library linking").
- `broken/unresolved_top.v`: intentionally unresolved variant
  (see "Broken variant" below). Never part of the default demo
  path.
- `elab-demo.tcl`: the GUI demo script.

## Library linking: the macroblock

`eos_macro_mac` stands in for a hard macro (a DSP
multiply-accumulate block) that a real flow links from a library
instead of compiling with the design sources. It lives in `lib/`,
not in `rtl/`, and it resolves only because `design.f` links
`lib/eos_macro_mac.v` as its last entry.

EOS filelists are deliberately narrow: one file path per line,
resolved relative to the list file, no library-directory search.
So "linking the library" here means exactly that entry in the
list.

Measured link test, and it is not the naive one: elaboration
succeeds with or without the library file, because EOS runs
plain `hierarchy -top` without `-check`, so an unlinked module
becomes an opaque cell instead of an error. What changes is the
visible hierarchy. With the library entry (`design.f`),
`synth_hierarchy` shows `hier_top/u_data/u_mac` with its full
port list, and `synth_blocks hier_datapath` shows `u_mac`
alongside `u_alu` and `u_out`. Without it
(`design_no_lib.f`), `u_mac` vanishes from both: the hierarchy
walk descends only into known modules. Top-level stats are
identical either way, so the tree, not the stats, is the test:
the macroblock appears in the hierarchy only when its library
file is linked.

The macro ships as plain synthesizable RTL, not as an empty black
box: EOS has no black-box library flow, and an empty module would
not survive as a visible leaf. The README states this plainly
rather than claiming black-box semantics EOS does not have.

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
eos::read_filelist design.f hier_top
eos::elaborate
eos::synth_hierarchy
eos::synth_blocks hier_datapath
eos::synth_schematic hier_datapath
eos::synth_stats
```

`synth_generic` is deliberately absent from this sequence: it
does not complete on this design (see "Known limitation" below).
Do not append it to the demo and expect success.

To run the library-link test, use the negative control instead
and compare the hierarchy output (see "Library linking"):

```
eos::create_scenario func_tt func tt estimate
eos::read_filelist design_no_lib.f hier_top
eos::elaborate
eos::synth_hierarchy
eos::synth_blocks hier_datapath
```

To run the broken variant (see "Broken variant" below):

```
eos::create_scenario func_tt func tt estimate
eos::read_verilog broken/unresolved_top.v unresolved_top
eos::elaborate
eos::synth_hierarchy
eos::synth_blocks unresolved_top
eos::synth_generic
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
read_filelist design.f hier_top
elaborate
```

Or launch with the script file instead, which runs the same
three commands before the window opens:

```
../../build/apps/eos-gui/eos-gui elab-demo.tcl
```

What you should see: the stepper moves Read and Elaborate to
done as each command runs; the hierarchy tree shows three
levels (top, submodule, leaf) with the macroblock `u_mac` under
`u_data`; double-clicking a submodule descends into its block
diagram, the breadcrumb ascends; the leaf schematic shows the
gates of that level. After `elaborate`, the Results panel shows
the elaborated numbers (see "Expected outputs").

## Expected outputs

All values below are measured from a real run of the built
`eos` binary against these files (the full verified sequence
runs in 0.52 s wall clock, including Yosys's verbose pass log).

- `read_filelist design.f hier_top`: files=7,
  entries=rtl/hier_top.v,rtl/hier_ctrl.v,rtl/hier_counter.v,
  rtl/hier_datapath.v,rtl/hier_alu.v,rtl/hier_outreg.v,
  lib/eos_macro_mac.v, top=hier_top,
  modules=eos_macro_mac,hier_outreg,hier_alu,hier_datapath,
  hier_counter,hier_ctrl,hier_top (module order may vary).
- `elaborate`: module=hier_top cells=2 wires=9 depth=2
  types=\hier_ctrl:1,\hier_datapath:1.
- `synth_hierarchy`: exactly the tree in "The design" above:
  hier_top, hier_top/u_ctrl (hier_ctrl),
  hier_top/u_ctrl/u_cnt (hier_counter), hier_top/u_data
  (hier_datapath), hier_top/u_data/u_alu (hier_alu),
  hier_top/u_data/u_mac (eos_macro_mac),
  hier_top/u_data/u_out (hier_outreg), each with its ports.
- `synth_blocks hier_datapath`: u_alu, u_mac, u_out with
  their ports, plus the datapath nets (acc, alu_out, mac_out,
  and the rest).
- `synth_schematic hier_datapath`: the elaborated datapath
  gates ($adff, $mux cells of the accumulator register and
  its muxes) and nets. Submodule instances are not gates, so
  u_mac does not appear as a gate here; it appears as a block
  in `synth_blocks` and as a node in `synth_hierarchy`.
- `synth_stats`: module=hier_top cells=2 wires=9 depth=2
  sequential=0, type \hier_ctrl 1, type \hier_datapath 1.

## Broken variant

`broken/unresolved_top.v` instantiates
`does_not_exist_anywhere`, which is defined nowhere. Measured
behavior in current EOS: `elaborate` SUCCEEDS (opaque cell, as
explained under "Library linking"), `synth_hierarchy` shows
only the top node, and `synth_blocks unresolved_top` shows no
block instances. The unresolved reference surfaces at
`synth_generic`, which fails with exactly:

```
error: EOS io_error: ABC command failed (structural hashing): strash
Line 10: Cannot find the model for subcircuit does_not_exist_anywhere.
Reading network from file has failed.
Error: Empty network.
```

This variant therefore tests two things for the future
elaboration-issues panel: an unresolved reference that is
invisible to `elaborate`'s exit code, and the exact error it
produces downstream. A panel that only reports failed commands
would miss this case entirely.

## Known limitation: synth_generic on sequential designs

`synth_generic` does NOT complete on this design, and the
reason is an engine limitation, not a demo bug. The elaborated
netlist contains flip-flops (`$_DFF_PN0_` cells from the async
resets), the generic bridge writes them to BLIF as subcircuits,
and ABC's BLIF reader has no model for them. The exact failure
on this design is:

```
error: EOS io_error: ABC command failed (structural hashing): strash
Line 6209: Cannot find the model for subcircuit $_DFF_PN0_.
Reading network from file has failed.
Error: Empty network.
```

The failure happens for every script (`default`, `balance`,
`rewrite`, `none`): the generic flow always reads and hashes
the network before any script step runs. So any sequential
design fails `synth_generic` the same way today, and the demo
script stops at `elaborate` for that reason. Making
`synth_generic` work on sequential designs (for example by
converting flops to latches before the BLIF write, or by
mapping them to library cells) is engine work, outside this
demo's scope, and is flagged as such to the maintainers.
Do not "fix" the demo by removing the sequential logic: the
mix of combinational and sequential logic is the point of
this design, and hiding the limitation would mislead the GUI
testing this demo exists for.
