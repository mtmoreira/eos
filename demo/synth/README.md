# Synthesis demo (reference design)

A small reference design for exercising the EOS synthesis flow end to
end in the interactive shell. The files are byte-identical to the
fixtures used by the registry unit tests (SynthOptFlowToMapped), so the
numbers below are the same numbers CI asserts.

- `optloop.v`: module `optloop` (a, b, c, d, y1, y2). Both outputs
  compute a & b & c & d through different association orders.
- `cells.lib`: library `eos_opt_fixture` with AND2 (0.6 ns), AND2_FAST
  (0.3 ns), INV (0.5 ns), BUF (0.4 ns).
- `top.sdc`: virtual clock `vclk`, period 1.25 ns, zero input and
  output delays.

## Run

Build EOS first (from the repository root):

```
cmake -S. -B build -DCMAKE_PREFIX_PATH=$(brew --prefix)
cmake --build build -j$(sysctl -n hw.ncpu)
```

On Linux, drop the `CMAKE_PREFIX_PATH` argument (or point it at your
dependency prefix). Then, from this directory, launch the shell:

```
../../build/apps/eos/eos
```

Paste these commands at the `eos>` prompt, in order. Commands live
in the `eos::` namespace, so the prefix is required:

```
eos::create_scenario func_tt func tt estimate
eos::read_liberty tt cells.lib
eos::read_verilog optloop.v optloop
eos::read_sdc top.sdc
eos::elaborate
eos::synth_generic
eos::synth_map -target area
eos::write_verilog mapped.v
eos::synth_opt -strategies classic
eos::write_verilog out.v
eos::check_equivalence mapped.v out.v
eos::synth_report
```

## Expected outputs

- `synth_generic`: 6 ANDs at 3 levels become 3 ANDs at 2 levels.
- `synth_map -target area`: 4 cells (AND2:3, BUF:1) at worst slack
  -0.35 ns, TNS -0.35 ns.
- `synth_opt -strategies classic`: 1 iteration, winner `classic`,
  4 cells (AND2:4) at worst slack 0.05 ns, TNS 0, still violating none.
- `check_equivalence mapped.v out.v`: equivalent.
- `synth_report`: the loop record for the winning candidate.

Note: `check_equivalence` of the original RTL against the mapped
netlist reports unknown by design (assign-based RTL converts to an
empty network for the equivalence check). This demo therefore checks
the mapped netlist against the loop winner, not the RTL against the
mapped netlist.

## Physical flow (mock PDK)

The mapped netlist can be carried into floorplan in the same
session with the synth-to-PD bridge (unit F0): `write_def` writes
the mapped netlist as DEF, `read_lef` loads the technology and
cell LEFs, and `read_def` loads the DEF into the session database.
The mock PDK lives at `src/pd/rsz/tests/mock/`: `eos_mock.lib`
(39 synthetic cells), `eos_mock.lef` (40 macros with row sites),
`eos_mock.tlef` (10 routing layers). It is synthetic EOS-owned
data, never PDK correlation: it validates engine mechanics only.

Run this chain from a fresh shell in this directory. It maps with
the mock Liberty, so the mapped cells are the mock EM_* cells the
mock LEF provides; the demo `cells.lib` maps to plain AND2 and
BUF cells, which the mock LEF does not contain.

```
eos::create_scenario func_tt func tt estimate
eos::read_liberty tt ../../src/pd/rsz/tests/mock/eos_mock.lib
eos::read_verilog optloop.v optloop
eos::elaborate
eos::synth_generic
eos::synth_map -target area
eos::write_def optloop.def -lef ../../src/pd/rsz/tests/mock/eos_mock.lef -tlef ../../src/pd/rsz/tests/mock/eos_mock.tlef
eos::read_lef ../../src/pd/rsz/tests/mock/eos_mock.tlef
eos::read_lef ../../src/pd/rsz/tests/mock/eos_mock.lef
eos::read_def optloop.def
eos::floorplan
```

What you should see: `write_def` reports the design, the cell
counts, the computed die area, and the net and pin counts;
`read_def` reports the design with its instance and net counts;
`floorplan` reports the die area, the core area, and a row count
instead of the `no technology data loaded: eos::import_odb
first` failure that floorplan used to give without an imported
database. The die area is a square at 60 percent utilization
over the summed LEF macro areas, unless `-die_area` gives one
explicitly.

This folder is reference data and documentation. It is outside the
seven-layer code structure: it adds no CMake target and no test.

## GUI walkthrough

Build the GUI target (see src/ui/gui/README.md for the Mac and
Linux build commands), then launch it from this directory with
no script file:

```
../../build/apps/eos-gui/eos-gui
```

The window opens on an empty session. Type these commands in
the console entry line, one per Return (the eos:: prefix is
optional there):

```
create_scenario func_tt func tt estimate
read_verilog optloop.v optloop
elaborate
synth_generic
```

Or launch with the script file instead, which runs the same
four commands before the window opens:

```
../../build/apps/eos-gui/eos-gui elab-demo.tcl
```

What you should see: the stepper moves Read, Elaborate, and
Generic synth to done as each command runs; the hierarchy tree
shows the flat optloop root only (optloop has no submodules);
clicking a stepper step shows that step's outputs in the right
panels (Read shows Sources, Elaborate shows Results, Generic
synth shows the Opt log); the center shows the leaf schematic,
because the flat root is a leaf level. The schematic shows the
elaborated 6-AND unshared tree, not the generic 3-AND shared
tree: schematic data comes from the elaborated-design walk, and
the generic sharing is visible in the Results panel numbers
instead (3 ANDs at 2 levels after synth_generic). The
checkpoint indicator at the right of the stepper row reads
"Checkpoint: none" throughout, since this walkthrough sets no
checkpoint.

To drive the same session from the terminal instead, type the
commands at the terminal eos> prompt while the window is open;
close the window and type show in the terminal to bring it
back on the same session.
