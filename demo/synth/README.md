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

This folder is reference data and documentation. It is outside the
seven-layer code structure: it adds no CMake target and no test.
