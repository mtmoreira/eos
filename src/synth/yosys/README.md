# yosys (L4 engines)

Yosys, adopted as the synthesis front end and generic synthesis
engine.

Status: imported in synthesis unit S0. EOS uses the engine
through its own facade, include/eos/synth/yosys.h
(eos::synth::YosysEngine):

- ParseVerilog parses one Verilog file with the Yosys Verilog
  front end and returns a summary of the modules parsed: each
  module's name, its ports in declaration order, and its wire
  count. The design is fresh for every call.
- Version returns the imported Yosys version string (the pin's
  version and commit, baked in at build time).

The facade is move-only and guards Yosys's single global design:
one live engine at a time per process. Documented upstream
behaviours, all in the facade header and in PROVENANCE.md: a
Yosys log_error (a syntax error in the input) terminates the
process with _Exit, so a malformed file is a documented
limitation of this unit, not a tested failure mode, and S1 must
resolve it before read_verilog changes semantics to parse; the
Yosys session is set up once per process and never shut down,
because pass registration is one-shot per process; the facade
installs upstream's non-tty default log sink at first setup, so
Yosys output reaches stderr/stdout as upstream prints it; and
every consumer must link eda_synth_yosys whole-archive, or no
pass is registered at all.

Known limits of this unit, stated plainly: Yosys's ABC
integration is off (EOS's own ABC is the ABC path), the slang
frontend is not imported, the share directory is not wired (so
passes that need techlib .v data at runtime cannot find it yet),
and the gatemate/quicklogic generated data files are not
generated (they are runtime share data, not compile inputs).
All of these are S1 or later work, not S0 gaps discovered late:
see PROVENANCE.md for the full list and the reasons.
