# abc (L4 engines)

ABC, adopted for logic optimization, delay mapping and
combinational equivalence proving. The module is imported at
the pinned commit; see PROVENANCE.md for the exact commit, the
inventory, the compiled closure and the patch list (zero).

Status: imported in Phase 2 unit 2.6. EOS uses the engine
through its own facade, include/eos/synth/abc.h
(eos::synth::AbcEngine):

- MapForDelay maps a gate-level Verilog netlist for delay
  against a genlib library generated from the design's Liberty
  cells, in process, and writes the mapped netlist back as
  Verilog with the input module's name and port names kept.
- ProveEquivalent runs ABC's own cec on the two netlists (via
  AIGER intermediates; see PROVENANCE.md) and returns a
  three-state verdict. kUnknown means the proof did not
  conclude; it is never treated as a pass by any caller.

The facade is move-only and guards ABC's single global frame:
one live engine at a time per process. One runtime note: ABC's
command interpreter keeps a command history file (abc.history)
in the host process's working directory; that is upstream
behaviour of the embedded interpreter, not EOS state.

The first consumer is the timing ECO resynthesis adapter
(src/eco/timing-eco), which remaps the cone behind a violating
endpoint through this facade and requires an equivalence proof
of the full design before the splice is allowed to stand.
